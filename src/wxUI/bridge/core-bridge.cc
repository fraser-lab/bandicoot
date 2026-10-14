// src/wxUI/bridge/core-bridge.cc
//
// Core side of the wx main window: adapts wx's GL surface and input to the
// existing GTK-era renderer and event handlers. See core-bridge.hh.
//
// During the toolkit changeover the core still reaches the GL surface through
// graphics_info_t::glarea, both as a GtkWidget (for its allocation) and as a
// "main window exists" token. A wx build therefore keeps a hidden GTK widget
// tree and a stand-in glarea whose allocation tracks the wx surface; the GL
// context itself is wx's.

#if defined (USE_PYTHON)
#include "Python.h"  // before system includes to stop "POSIX_C_SOURCE" redefined problems
#endif

#include <cstring>
#include <string>

#include <gtk/gtk.h>

#include "graphics-info.h"
#include "globjects.h"
#include "c-interface.h"
#include "cc-interface.hh"
#include "drag-and-drop.hh"
#include "c-interface-ligands-swig.hh"
#include "coot-utils/coot-package-paths.hh"

#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/main/surface.hh"

static_assert(int(BANDICOOT_MOD_SHIFT) == int(GDK_SHIFT_MASK),   "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_CONTROL) == int(GDK_CONTROL_MASK), "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_ALT) == int(GDK_MOD1_MASK),    "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_BUTTON1) == int(GDK_BUTTON1_MASK), "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_BUTTON2) == int(GDK_BUTTON2_MASK), "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_BUTTON3) == int(GDK_BUTTON3_MASK), "modifier bits must match GDK");

void bandicoot_core_gl_init() {
   if (graphics_info_t::glarea)
      init(graphics_info_t::glarea);
}

void bandicoot_core_gl_resize(int surface_width, int surface_height,
                              int window_width, int window_height) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GtkAllocation allocation;
   allocation.x = 0;
   allocation.y = 0;
   allocation.width  = surface_width;
   allocation.height = surface_height;
   gtk_widget_size_allocate(glarea, &allocation);

   if (graphics_info_t::make_current_gl_context(glarea)) {
      // the viewport is in backing-store pixels, the allocation in points
      double s = bandicoot_wx_surface_backing_scale();
      glViewport(0, 0, int(surface_width * s), int(surface_height * s));
   }
   graphics_info_t::graphics_x_size = window_width;
   graphics_info_t::graphics_y_size = window_height;
}

void bandicoot_core_gl_draw() {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventExpose event;
   memset(&event, 0, sizeof(event));
   event.type  = GDK_EXPOSE;
   event.count = 0;
   event.area.width  = glarea->allocation.width;
   event.area.height = glarea->allocation.height;
   draw(glarea, &event);
}

static GdkEventButton make_button_event(GdkEventType type, int button,
                                        double x, double y, unsigned int mods) {
   GdkEventButton event;
   memset(&event, 0, sizeof(event));
   event.type   = type;
   event.time   = GDK_CURRENT_TIME;
   event.x      = x;
   event.y      = y;
   event.x_root = x;
   event.y_root = y;
   event.state  = mods;
   event.button = button;
   return event;
}

void bandicoot_core_button_press(int button, int click_count,
                                 double x, double y, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   // GTK reports the second press of a double click twice, as a plain press
   // and then as a 2BUTTON_PRESS; the handler expects both.
   GdkEventButton event = make_button_event(GDK_BUTTON_PRESS, button, x, y, mods);
   glarea_button_press(glarea, &event);
   if (click_count == 2) {
      GdkEventButton event_2 = make_button_event(GDK_2BUTTON_PRESS, button, x, y, mods);
      glarea_button_press(glarea, &event_2);
   }
}

void bandicoot_core_button_release(int button, double x, double y, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;
   GdkEventButton event = make_button_event(GDK_BUTTON_RELEASE, button, x, y, mods);
   glarea_button_release(glarea, &event);
}

void bandicoot_core_motion(double x, double y, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventMotion event;
   memset(&event, 0, sizeof(event));
   event.type    = GDK_MOTION_NOTIFY;
   event.time    = GDK_CURRENT_TIME;
   event.x       = x;
   event.y       = y;
   event.x_root  = x;
   event.y_root  = y;
   event.state   = mods;
   event.is_hint = 0; // the handler then takes position and state from the event
   glarea_motion_notify(glarea, &event);
}

void bandicoot_core_scroll(bool up, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventScroll event;
   memset(&event, 0, sizeof(event));
   event.type      = GDK_SCROLL;
   event.time      = GDK_CURRENT_TIME;
   event.state     = mods;
   event.direction = up ? GDK_SCROLL_UP : GDK_SCROLL_DOWN;
   glarea_scroll_event(glarea, &event);
}

void bandicoot_core_key(bool press, unsigned int keyval, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventKey event;
   memset(&event, 0, sizeof(event));
   event.type   = press ? GDK_KEY_PRESS : GDK_KEY_RELEASE;
   event.time   = GDK_CURRENT_TIME;
   event.state  = mods;
   event.keyval = keyval;
   event.string = const_cast<gchar *>("");
   if (press)
      key_press_event(glarea, &event);
   else
      key_release_event(glarea, &event);
}

void bandicoot_core_drop_file(const std::string &file_name) {
   handle_drag_and_drop_single_item(file_name);
}

void bandicoot_core_drop_text(const std::string &text) {
   handle_drag_and_drop_string(text);
}

std::string bandicoot_core_pixmaps_dir() {
   return coot::package_pixmaps_dir();
}

int bandicoot_core_sidebar_style() {
   return model_toolbar_style_state();
}

std::vector<bandicoot_sidebar_icon> bandicoot_core_sidebar_icons() {
   std::vector<bandicoot_sidebar_icon> v;
   if (graphics_info_t::model_toolbar_icons)
      for (const coot::preferences_icon_info_t &i : *graphics_info_t::model_toolbar_icons)
         v.push_back({ i.icon_widget, i.show_hide_flag == 1, i.default_show_flag == 1 });
   return v;
}

void bandicoot_core_sidebar_show_icon_set(bool main_icons_only) {
   if (main_icons_only)
      show_model_toolbar_main_icons();
   else
      show_model_toolbar_all_icons();
}

extern "C" void bandicoot_save_sidebar_docked(int docked);
extern "C" int  bandicoot_load_sidebar_docked(void);

bool bandicoot_core_sidebar_docked() {
   return bandicoot_load_sidebar_docked() != 0;
}

void bandicoot_core_save_sidebar_docked(bool docked) {
   bandicoot_save_sidebar_docked(docked ? 1 : 0);
}

extern "C" void bandicoot_save_sidebar_position(int position);
extern "C" int  bandicoot_load_sidebar_position(void);

int bandicoot_core_sidebar_position() {
   return bandicoot_load_sidebar_position();
}

void bandicoot_core_save_sidebar_position(int position) {
   bandicoot_save_sidebar_position(position);
}

extern "C" void bandicoot_save_sidebar_icon_size(int points);
extern "C" int  bandicoot_load_sidebar_icon_size(void);

int bandicoot_core_sidebar_icon_size() {
   return bandicoot_load_sidebar_icon_size();
}

void bandicoot_core_save_sidebar_icon_size(int points) {
   bandicoot_save_sidebar_icon_size(points);
}

bool bandicoot_core_has_capability(const std::string &capability) {
   if (capability == "enhanced_ligand_tools")
      return enhanced_ligand_coot_p();
   return false;
}

std::vector<std::string> bandicoot_core_unsaved_molecules() {

   std::vector<std::string> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (graphics_info_t::molecules[imol].Have_unsaved_changes_p())
         v.push_back(std::to_string(imol) + "  " + graphics_info_t::molecules[imol].name_);
   }
   return v;
}

void bandicoot_core_quit() {
   // Not coot_checked_exit(): its unsaved-changes question is a GTK dialog,
   // which cannot take input while wx runs the event loop, so the wx side asks
   // instead. This is the path Ctrl-C takes; it saves the state and history.
   coot_real_exit(0);
}

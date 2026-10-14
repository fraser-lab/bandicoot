// src/wxUI/bridge/core-bridge.hh
//
// Entry points the wx main window calls into the existing core: GL setup and
// drawing, and input. Only compiled into a --with-ui=wx build.
//
// Input is handed over in GDK's terms (modifier bits, X11 keysyms) because the
// core's mouse and key handlers are written against them; reusing those
// handlers keeps every binding identical to the GTK build. No wx or GTK types
// appear here -- see wxUI/main/surface.hh.

#ifndef BANDICOOT_WX_CORE_BRIDGE_HH
#define BANDICOOT_WX_CORE_BRIDGE_HH

#include <string>
#include <vector>

// Modifier and button bits, numerically identical to GdkModifierType
// (the bridge checks this at compile time).
enum {
   BANDICOOT_MOD_SHIFT   = 1 << 0,
   BANDICOOT_MOD_CONTROL = 1 << 2,
   BANDICOOT_MOD_ALT     = 1 << 3,
   BANDICOOT_MOD_BUTTON1 = 1 << 8,
   BANDICOOT_MOD_BUTTON2 = 1 << 9,
   BANDICOOT_MOD_BUTTON3 = 1 << 10
};

// GL surface, sizes in points. The window size is what the state file records.
void bandicoot_core_gl_init();
void bandicoot_core_gl_resize(int surface_width, int surface_height,
                              int window_width, int window_height);
void bandicoot_core_gl_draw();

// Mouse. button is 1 (left), 2 (middle) or 3 (right); click_count is 2 for
// the second press of a double click. mods is the pointer state at the time of
// the event, including the button just pressed.
void bandicoot_core_button_press(int button, int click_count,
                                 double x, double y, unsigned int mods);
void bandicoot_core_button_release(int button, double x, double y, unsigned int mods);
void bandicoot_core_motion(double x, double y, unsigned int mods);
void bandicoot_core_scroll(bool up, unsigned int mods);

// Keyboard; keyval is an X11 keysym.
void bandicoot_core_key(bool press, unsigned int keyval, unsigned int mods);

// Drag and drop: a local file, or text (a URL or an accession code).
void bandicoot_core_drop_file(const std::string &file_name);
void bandicoot_core_drop_text(const std::string &text);

// The directory holding Bandicoot's icons.
std::string bandicoot_core_pixmaps_dir();

// The sidebar style the core holds: 1 icons, 2 icons and text, 3 text.
int bandicoot_core_sidebar_style();

// The sidebar tools the core can show or hide, by Coot's widget name.
struct bandicoot_sidebar_icon {
   std::string widget;
   bool shown;
   bool shown_by_default;
};
std::vector<bandicoot_sidebar_icon> bandicoot_core_sidebar_icons();
// The Settings menu's Main Icons (true) or All Icons (false).
void bandicoot_core_sidebar_show_icon_set(bool main_icons_only);

// The sidebar's saved dock state (docked unless saved otherwise), and saving it.
bool bandicoot_core_sidebar_docked();
void bandicoot_core_save_sidebar_docked(bool docked);
// The side it docks on, in Coot's numbering: 0 right (default), 1 left, 3 bottom.
int bandicoot_core_sidebar_position();
void bandicoot_core_save_sidebar_position(int position);

// Whether this build has an optional capability, e.g. "enhanced_ligand_tools"
// (the ligand tools that need RDKit).
bool bandicoot_core_has_capability(const std::string &capability);

// Molecules with unsaved changes, one "imol  name" line each.
std::vector<std::string> bandicoot_core_unsaved_molecules();

// The main window is closing: save state and history, then exit.
void bandicoot_core_quit();

#endif // BANDICOOT_WX_CORE_BRIDGE_HH

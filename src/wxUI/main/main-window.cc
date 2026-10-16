// src/wxUI/main/main-window.cc
//
// The wx main window: an OpenGL surface for the existing renderer, a
// collapsible area above it for transient bars (Accept/Reject, sequence view),
// and empty slots to the left, right and bottom for the modelling toolbar.
// Only compiled into a --with-ui=wx build.
//
// Input on the surface is forwarded to the core's existing mouse and key
// handlers (see wxUI/bridge/core-bridge.hh), so bindings match the GTK build.
//
// No GTK headers here: see surface.hh.

#include <wx/wx.h>
#include <wx/glcanvas.h>
#include <wx/splitter.h>
#include <wx/dnd.h>
#include <wx/display.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "wxUI/main/surface.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/main/main-menu.hh"
#include "wxUI/main/top-toolbar.hh"
#include "wxUI/main/file-actions.hh"
#include "wxUI/dialogs/accept-reject-dialog.hh"
#include "wxUI/main/accept-reject-bar.hh"
#include "wxUI/main/sequence-view.hh"
#include "wxUI/main/go-to-atom.hh"
#include "wxUI/main/display-manager.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/dialogs/click-through-dialog.hh"
#include "wxUI/main/refine-dialogs.hh"
#include "wxUI/main/model-dialogs.hh"
#include "wxUI/main/choosers.hh"
#include "wxUI/main/delete-waters.hh"
#include "wxUI/main/sidebar.hh"
#include "wxUI/assets/icons.hh"

namespace {

// -------------------------------------------------------------------------
// application object
// -------------------------------------------------------------------------

class BandicootApp : public wxApp {
public:
   // The command line belongs to Bandicoot, not wx; leave it unparsed.
   bool OnInit() override { return true; }
};

// -------------------------------------------------------------------------
// a timeout the core asks for: repeats until its callback returns false
// -------------------------------------------------------------------------

class CallbackTimer : public wxTimer {
public:
   explicit CallbackTimer(std::function<bool()> f) : callback(std::move(f)) {}
   void Notify() override {
      if (running) return;  // the callback may run the event loop itself
      running = true;
      bool more = callback();
      running = false;
      if (! more) {
         Stop();
         wxTheApp->CallAfter([this]() { delete this; });
      }
   }
private:
   std::function<bool()> callback;
   bool running = false;
};

// -------------------------------------------------------------------------
// keyboard: wx key codes -> X11 keysyms (the values GDK uses)
// -------------------------------------------------------------------------

unsigned int special_keysym(int wx_key) {

   switch (wx_key) {
   case WXK_LEFT:            return 0xff51;
   case WXK_UP:              return 0xff52;
   case WXK_RIGHT:           return 0xff53;
   case WXK_DOWN:            return 0xff54;
   case WXK_PAGEUP:          return 0xff55;
   case WXK_PAGEDOWN:        return 0xff56;
   case WXK_END:             return 0xff57;
   case WXK_HOME:            return 0xff50;
   case WXK_RETURN:          return 0xff0d;
   case WXK_ESCAPE:          return 0xff1b;
   case WXK_TAB:             return 0xff09;
   case WXK_BACK:            return 0xff08;
   case WXK_DELETE:          return 0xffff;
   case WXK_INSERT:          return 0xff63;
   case WXK_SHIFT:           return 0xffe1; // Shift_L
   case WXK_RAW_CONTROL:     return 0xffe3; // Control_L: on macOS WXK_CONTROL is Command
   case WXK_ALT:             return 0xffe9; // Alt_L
   case WXK_NUMPAD_ENTER:    return 0xff8d;
   case WXK_NUMPAD_ADD:      return 0xffab;
   case WXK_NUMPAD_SUBTRACT: return 0xffad;
   case WXK_NUMPAD_MULTIPLY: return 0xffaa;
   case WXK_NUMPAD_DIVIDE:   return 0xffaf;
   case WXK_NUMPAD_DECIMAL:  return 0xffae;
   default: break;
   }
   if (wx_key >= WXK_F1 && wx_key <= WXK_F12)
      return 0xffbe + (wx_key - WXK_F1);
   if (wx_key >= WXK_NUMPAD0 && wx_key <= WXK_NUMPAD9)
      return 0xffb0 + (wx_key - WXK_NUMPAD0);
   return 0;
}

unsigned int key_mods(const wxKeyboardState &k) {
   unsigned int m = 0;
   if (k.ShiftDown())      m |= BANDICOOT_MOD_SHIFT;
   if (k.RawControlDown()) m |= BANDICOOT_MOD_CONTROL;
   if (k.AltDown())        m |= BANDICOOT_MOD_ALT;
   return m;
}

// Command-key combinations belong to the menu bar, not the graphics.
bool is_command_combination(const wxKeyEvent &e) {
#ifdef __WXOSX__
   return e.CmdDown();
#else
   return false;
#endif
}

// -------------------------------------------------------------------------
// drag and drop
// -------------------------------------------------------------------------

class SurfaceDropTarget : public wxDropTarget {
public:
   SurfaceDropTarget() {
      files = new wxFileDataObject;
      text  = new wxTextDataObject;
      wxDataObjectComposite *composite = new wxDataObjectComposite;
      composite->Add(files, true); // preferred
      composite->Add(text);
      SetDataObject(composite);
   }

   wxDragResult OnDragOver(wxCoord, wxCoord, wxDragResult) override {
      return wxDragCopy;
   }

   wxDragResult OnData(wxCoord, wxCoord, wxDragResult def) override {
      if (! GetData())
         return wxDragNone;

      std::vector<std::string> file_names;
      std::string dropped_text;
      wxDataObjectComposite *composite = static_cast<wxDataObjectComposite *>(GetDataObject());
      if (composite->GetReceivedFormat() == files->GetFormat()) {
         for (const wxString &f : files->GetFilenames())
            file_names.push_back(f.utf8_string());
      } else {
         dropped_text = text->GetText().utf8_string();
      }

      // Loading can take a while; finish the drag first so the source
      // application is not left waiting on it.
      wxTheApp->CallAfter([file_names, dropped_text]() {
         for (const std::string &f : file_names)
            bandicoot_core_drop_file(f);
         if (! dropped_text.empty())
            bandicoot_core_drop_text(dropped_text);
      });
      return def;
   }

private:
   wxFileDataObject *files;
   wxTextDataObject *text;
};

// -------------------------------------------------------------------------
// the GL surface
// -------------------------------------------------------------------------

class GLSurface : public wxGLCanvas {
public:
   GLSurface(wxWindow *parent, const wxGLAttributes &attrs)
      : wxGLCanvas(parent, attrs, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                   wxFULL_REPAINT_ON_RESIZE | wxWANTS_CHARS) {

      context = new wxGLContext(this);
      SetMinSize(wxSize(64, 64));
      SetDropTarget(new SurfaceDropTarget);

      Bind(wxEVT_PAINT,            &GLSurface::on_paint,  this);
      Bind(wxEVT_SIZE,             &GLSurface::on_size,   this);
      Bind(wxEVT_DPI_CHANGED,      &GLSurface::on_dpi,    this);
      Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {}); // GL paints all of it

      Bind(wxEVT_LEFT_DOWN,    &GLSurface::on_button_down, this);
      Bind(wxEVT_MIDDLE_DOWN,  &GLSurface::on_button_down, this);
      Bind(wxEVT_RIGHT_DOWN,   &GLSurface::on_button_down, this);
      Bind(wxEVT_LEFT_DCLICK,  &GLSurface::on_button_down, this);
      Bind(wxEVT_MIDDLE_DCLICK,&GLSurface::on_button_down, this);
      Bind(wxEVT_RIGHT_DCLICK, &GLSurface::on_button_down, this);
      Bind(wxEVT_LEFT_UP,      &GLSurface::on_button_up,   this);
      Bind(wxEVT_MIDDLE_UP,    &GLSurface::on_button_up,   this);
      Bind(wxEVT_RIGHT_UP,     &GLSurface::on_button_up,   this);
      Bind(wxEVT_MOTION,       &GLSurface::on_motion,      this);
      Bind(wxEVT_MOUSEWHEEL,   &GLSurface::on_wheel,       this);
      Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent &) {});

      Bind(wxEVT_KEY_DOWN, &GLSurface::on_key_down, this);
      Bind(wxEVT_CHAR,     &GLSurface::on_char,     this);
      Bind(wxEVT_KEY_UP,   &GLSurface::on_key_up,   this);
   }

   bool make_current() {
      if (! IsShownOnScreen()) return false;
      return SetCurrent(*context);
   }

   // Draw now, as gtk_widget_draw() did: much of the core redraws
   // synchronously and expects the frame to be on screen on return.
   void render() {
      if (rendering) return;
      if (! make_current()) return;
      rendering = true;
      if (! gl_ready) {
         gl_ready = true;
         resize_core();
         bandicoot_core_gl_init();
      }
      bandicoot_core_gl_draw();
      rendering = false;
   }

   // Sees each key press first; true if it took the key.
   std::function<bool(int key_code)> key_filter;

   void redraw() {
      if (gl_ready)
         render();
      else
         Refresh(false);
   }

private:
   wxGLContext *context = nullptr;
   bool gl_ready  = false;
   bool rendering = false;
   // keysym sent on press, by raw key code, so the release reports the same one
   std::map<unsigned int, unsigned int> pressed_keysyms;

   void resize_core() {
      if (! make_current()) return;
      wxSize s = GetClientSize();
      wxSize w = wxGetTopLevelParent(this)->GetSize();
      bandicoot_core_gl_resize(s.x, s.y, w.x, w.y);
   }

   void on_paint(wxPaintEvent &) {
      wxPaintDC dc(this); // required, even though GL does the drawing
      render();
   }

   void on_size(wxSizeEvent &e) {
      if (gl_ready) resize_core();
      Refresh(false);
      e.Skip();
   }

   void on_dpi(wxDPIChangedEvent &e) {
      if (gl_ready) resize_core();
      Refresh(false);
      e.Skip();
   }

   // ---------------- mouse ----------------

   static int button_number(const wxMouseEvent &e) {
      switch (e.GetButton()) {
      case wxMOUSE_BTN_LEFT:   return 1;
      case wxMOUSE_BTN_MIDDLE: return 2;
      case wxMOUSE_BTN_RIGHT:  return 3;
      default:                 return 0;
      }
   }

   // wx on macOS reports Control+left-click as a right click, for the whole
   // gesture. The core gives Control+left-drag its own meaning (pan), so the
   // gesture is reported as the left button it physically is.
   bool control_click_is_left = false;

   unsigned int pointer_mods(const wxMouseEvent &e) const {
      unsigned int m = key_mods(e);
      if (e.LeftIsDown())   m |= BANDICOOT_MOD_BUTTON1;
      if (e.MiddleIsDown()) m |= BANDICOOT_MOD_BUTTON2;
      if (e.RightIsDown())  m |= control_click_is_left ? BANDICOOT_MOD_BUTTON1
                                                       : BANDICOOT_MOD_BUTTON3;
      return m;
   }

   int core_button(const wxMouseEvent &e) const {
      int button = button_number(e);
      if (button == 3 && control_click_is_left) button = 1;
      return button;
   }

   void on_button_down(wxMouseEvent &e) {
      if (button_number(e) == 3) {
         wxMouseState physical = wxGetMouseState(); // the hardware, not wx's rewrite
         control_click_is_left = e.RawControlDown() && physical.LeftIsDown() &&
                                 ! physical.RightIsDown();
      }
      int button = core_button(e);
      if (button == 0) return;
      SetFocus();
      if (! HasCapture()) CaptureMouse(); // keep receiving the drag outside the surface
      int click_count = e.ButtonDClick() ? 2 : 1;
      bandicoot_core_button_press(button, click_count, e.GetX(), e.GetY(), pointer_mods(e));
   }

   void on_button_up(wxMouseEvent &e) {
      int button = core_button(e);
      if (button == 0) return;
      bandicoot_core_button_release(button, e.GetX(), e.GetY(), pointer_mods(e));
      if (button_number(e) == 3)
         control_click_is_left = false;
      if (HasCapture() && ! e.LeftIsDown() && ! e.MiddleIsDown() && ! e.RightIsDown())
         ReleaseMouse();
   }

   void on_motion(wxMouseEvent &e) {
      bandicoot_core_motion(e.GetX(), e.GetY(), pointer_mods(e));
   }

   void on_wheel(wxMouseEvent &e) {
      if (e.GetWheelAxis() != wxMOUSE_WHEEL_VERTICAL) return;
      int r = e.GetWheelRotation();
      if (r == 0) return;
      // one step per wheel event, as GTK reports them
      bandicoot_core_scroll(r > 0, pointer_mods(e));
   }

   // ---------------- keyboard ----------------
   //
   // Printable keys are reported as the character typed (shifted where shift
   // is down), as GDK does. Unshifted characters and letters are resolved on
   // key-down; shifted non-letters wait for the char event, which knows what
   // the keyboard layout produces.

   void key_press(const wxKeyEvent &e, unsigned int keysym) {
      pressed_keysyms[e.GetRawKeyCode()] = keysym;
      bandicoot_core_key(true, keysym, key_mods(e));
   }

   void on_key_down(wxKeyEvent &e) {
      if (is_command_combination(e)) { e.Skip(); return; }

      int code = e.GetKeyCode();
      if (key_filter && key_filter(code)) return;
      unsigned int keysym = special_keysym(code);
      if (keysym) { key_press(e, keysym); return; }

      if (code >= 'A' && code <= 'Z') {
         key_press(e, e.ShiftDown() ? code : code - 'A' + 'a');
         return;
      }
      wxChar c = e.GetUnicodeKey();
      if (c >= 32 && c < 127 && ! e.ShiftDown()) {
         key_press(e, static_cast<unsigned int>(c));
         return;
      }
      e.Skip(); // on to on_char
   }

   void on_char(wxKeyEvent &e) {
      if (is_command_combination(e)) { e.Skip(); return; }
      wxChar c = e.GetUnicodeKey();
      if (c >= 32 && c < 127)
         key_press(e, static_cast<unsigned int>(c));
      else
         e.Skip();
   }

   void on_key_up(wxKeyEvent &e) {
      unsigned int keysym = 0;
      auto it = pressed_keysyms.find(e.GetRawKeyCode());
      if (it != pressed_keysyms.end()) {
         keysym = it->second;
         pressed_keysyms.erase(it);
      } else {
         keysym = special_keysym(e.GetKeyCode());
      }
      if (keysym)
         bandicoot_core_key(false, keysym, key_mods(e));
      else
         e.Skip();
   }
};

// -------------------------------------------------------------------------
// the main window
// -------------------------------------------------------------------------
//
//   +-----------------------------------------------+
//   |       |  transient bars (collapsible)  |       |
//   | left  |--------------------------------| right |
//   | slot  |          GL surface            | slot  |
//   |       |                                |       |
//   +-----------------------------------------------+
//   |                  bottom slot                  |
//   +-----------------------------------------------+
//
// The slots hold the modelling toolbar wherever it is docked and are hidden
// while empty. The transient-bar area shares a splitter with the surface and
// is unsplit (zero height) while no bar is showing.

wxui::Sidebar::Style sidebar_style(int coot_style) {
   if (coot_style <= 1) return wxui::Sidebar::Style::Icons;
   if (coot_style == 2) return wxui::Sidebar::Style::IconsAndText;
   return wxui::Sidebar::Style::Text;
}

// Until the menu, toolbar and sidebar are connected to the core, say what was
// chosen rather than doing nothing.
void report_unconnected(const std::string &what, const std::string &action, bool state = true) {
   std::cout << "INFO:: not yet connected in the wx build: " << what;
   if (! state) std::cout << " (off)";
   std::cout << " (" << action << ")" << std::endl;
}

class MainFrame : public wxFrame {
public:
   explicit MainFrame(const wxString &title)
      : wxFrame(nullptr, wxID_ANY, title) {

      wxui::set_icon_directory(wxString::FromUTF8(bandicoot_core_pixmaps_dir()));

      wxui::MainMenuHooks hooks;
      hooks.has_capability = [](const std::string &c) { return bandicoot_core_has_capability(c); };
      hooks.on_select = [this](const std::string &action, const std::string &path) {
         // after the menu has closed
         if (action == "customize_toolbar") { CallAfter([]() { wxui::customize_top_toolbar(); }); return; }
         if (run_menu_action(action)) return;
         report_unconnected(path, action);
      };
      hooks.has_molecule_list = [](const std::string &list) { return list == "sequence_view1"; };
      hooks.molecules = [](const std::string &) { return bandicoot_core_goto_molecules(); };
      hooks.on_molecule = [this](const std::string &list, int imol) {
         if (list == "sequence_view1") CallAfter([this, imol]() { open_sequence_view(imol); });
      };
      SetMenuBar(wxui::build_main_menu(hooks));
      wxui::build_top_toolbar(this, [this](const std::string &action, const std::string &label,
                                           bool state) {
         if (wxui::run_file_action(this, action)) return;
         if (action == "on_go_to_atom_toolbutton_clicked") { wxui::show_go_to_atom(); return; }
         if (action == "on_display_manager_toolbutton_clicked") { wxui::show_display_manager(); return; }
         // the window is wx's, so full screen is too (the core's acts on the GTK window)
         if (action == "bandicoot_action_full_screen_apply") {
            ShowFullScreen(state, wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);
            return;
         }
         if (! bandicoot_core_run_action(action, state))
            report_unconnected("Toolbar > " + label, action, state);
      });
      CreateStatusBar(1);
      Bind(wxEVT_MENU, [this](wxCommandEvent &) { Close(); }, wxID_EXIT);
      Bind(wxEVT_CLOSE_WINDOW, &MainFrame::on_close, this);
      // leaving full screen another way (Escape, the window button) lets the
      // toolbar's Full Screen follow
      Bind(wxEVT_FULLSCREEN, [this](wxFullScreenEvent &e) {
         wxui::set_top_toolbar_toggle("bandicoot_action_full_screen_apply", e.IsFullScreen());
         e.Skip();
      });

      left_slot   = make_slot();
      right_slot  = make_slot();
      bottom_slot = make_slot();

      // the sidebar starts where the user last left it: docked on the right
      // unless saved as undocked (that window opens once this one is shown)
      docked = bandicoot_core_sidebar_docked();
      side = bandicoot_core_sidebar_position();
      if (docked) place_sidebar(true);

      splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxSP_LIVE_UPDATE | wxSP_3DSASH);
      splitter->SetSashGravity(0.0); // resizing goes to the surface, not the bars
      splitter->SetMinimumPaneSize(0); // dragging the sash to the top collapses the bars
      transient_area = new wxPanel(splitter);
      transient_area->SetSizer(new wxBoxSizer(wxVERTICAL));
      transient_area->SetDropTarget(new SurfaceDropTarget);
      // The sash sizes the docked sequence view, between one chain and all
      // of them; the Accept/Reject bar has one height, so with only that up
      // the sash does not move.
      splitter->Bind(wxEVT_SPLITTER_SASH_POS_CHANGING, [this](wxSplitterEvent &e) {
         if (sv_docked) {
            int bar = ar_bar_height();
            int h = std::clamp(e.GetSashPosition() - bar, sv_docked->height_for_rows(1),
                               sv_docked->height_for_rows(sv_docked->chain_count()));
            e.SetSashPosition(h + bar);
            sv_height = h;
         } else if (ar_bar && ar_bar->IsShown()) {
            e.SetSashPosition(-1);
         }
      });
      splitter->Bind(wxEVT_SPLITTER_DOUBLECLICKED, [this](wxSplitterEvent &e) {
         if (sv_docked || (ar_bar && ar_bar->IsShown())) e.Veto();
      });

      wxGLAttributes attrs;
      attrs.PlatformDefaults().RGBA().DoubleBuffer().Depth(24)
         .SampleBuffers(1).Samplers(4).EndList();
      if (! wxGLCanvas::IsDisplaySupported(attrs)) {
         std::cout << "WARNING:: multisampled GL surface unavailable, using a plain one"
                   << std::endl;
         attrs.Reset();
         attrs.PlatformDefaults().RGBA().DoubleBuffer().Depth(24).EndList();
      }
      surface = new GLSurface(splitter, attrs);
      splitter->Initialize(surface);
      transient_area->Hide();
      setup_accept_reject();
      setup_sequence_views();
      setup_ui_view();
      wxui::install_go_to_atom(this);
      wxui::install_display_manager(this);
      wxui::install_model_dialogs(this);
      wxui::install_choosers(this);
      wxui::install_delete_waters(this);

      wxBoxSizer *middle = new wxBoxSizer(wxHORIZONTAL);
      middle->Add(left_slot,  0, wxEXPAND);
      middle->Add(splitter,   1, wxEXPAND);
      middle->Add(right_slot, 0, wxEXPAND);

      wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
      root->Add(middle,      1, wxEXPAND);
      root->Add(bottom_slot, 0, wxEXPAND);
      SetSizer(root);

      if (getenv("BANDICOOT_WX_SHOW_SLOTS"))
         show_layout_placeholders();

      // Default to a quarter of the screen width by half its height, and no
      // smaller than half of that.
      wxSize screen = wxGetDisplaySize();
      wxSize size(std::max(600, screen.x / 4), std::max(500, screen.y / 2));
      SetSize(size);
      SetMinSize(wxSize(size.x / 2, size.y / 2));
      Centre();
   }

   GLSurface *get_surface() const { return surface; }
   wxui::Sidebar *get_sidebar() const { return sidebar; }
   bool sidebar_docked() const { return docked; }

   // Dock the sidebar in this window, or undock it into a window of its own.
   // save: remember the choice for the next session.
   void set_sidebar_docked(bool state, bool save) {
      if (save) bandicoot_core_save_sidebar_docked(state);
      if (state == docked && sidebar) return;
      place_sidebar(state);
   }

   // Called once this window is on screen.
   void place_saved_sidebar() {
      if (! sidebar) place_sidebar(docked);
   }

   // Show or collapse the area above the surface.
   void show_transient_area(bool state, int height = 80) {
      if (state && ! splitter->IsSplit()) {
         transient_area->Show();
         splitter->SplitHorizontally(transient_area, surface, height);
      } else if (state) {
         splitter->SetSashPosition(height);
      }
      if (! state && splitter->IsSplit())
         splitter->Unsplit(transient_area);
   }

private:
   wxui::Sidebar *sidebar  = nullptr;
   wxFrame *floater        = nullptr;  // the undocked sidebar's window
   bool docked = true;
   int side = 0;   // where it docks, in Coot's numbering: 0 right, 1 left, 3 bottom
   wxui::AcceptRejectDialog *accept_reject = nullptr;
   wxui::AcceptRejectBar *ar_bar = nullptr;

   // The fit waiting for an answer, kept so that docking or undocking moves
   // it between the bar and the window.
   struct PendingFit {
      bool active = false;
      std::string fit_type;
      bool show_reverse = false, auto_clear = false;
      bandicoot_ar_results results;
      std::string warnings;
   } fit;

   static std::vector<wxui::AcceptRejectDialog::Light>
   ar_lights(const bandicoot_ar_results &r) {
      std::vector<wxui::AcceptRejectDialog::Light> v;
      for (const bandicoot_ar_light &l : r.lights)
         v.push_back({ wxString::FromUTF8(l.name), wxString::FromUTF8(l.label),
                       wxColour(l.red, l.green, l.blue) });
      return v;
   }

   static std::vector<wxui::AcceptRejectBar::Light>
   ar_bar_lights(const bandicoot_ar_results &r) {
      std::vector<wxui::AcceptRejectBar::Light> v;
      for (const bandicoot_ar_light &l : r.lights)
         v.push_back({ wxString::FromUTF8(l.name), l.value, wxString::FromUTF8(l.label),
                       wxColour(l.red, l.green, l.blue) });
      return v;
   }

   bool ar_pending() const { return accept_reject->pending() || ar_bar->pending(); }

   int ar_bar_height() const {
      return ar_bar && ar_bar->IsShown() ? ar_bar->GetBestSize().y : 0;
   }

   // The area above the surface holds the docked sequence view, at the
   // height last given it, over the Accept/Reject bar, which is up while
   // docked and either always shown or answering a fit; it is closed when
   // neither is there.
   void place_transient_bars() {
      bool bar_shown = bandicoot_core_ar_docked() &&
                       (bandicoot_core_ar_always_shown() || ar_bar->pending());
      ar_bar->Show(bar_shown);
      int h = ar_bar_height() + (sv_docked ? sv_height : 0);
      if (h > 0) {
         transient_area->Layout();
         show_transient_area(true, h);
         transient_area->Layout();
      } else {
         show_transient_area(false);
      }
   }

   // A main-menu item wx has taken on, run once the menu has closed. False
   // if it is not one of them.
   bool run_menu_action(const std::string &action) {
      if (wxui::is_file_action(action)) {
         CallAfter([this, action]() { wxui::run_file_action(this, action); });
         return true;
      }
      return false;
   }

   // A results list; with nothing in it, a "No ... found" message instead
   // (the title without a trailing colon names what was looked for).
   void show_results(const std::string &title, const std::vector<bandicoot_result_row> &rows) {
      if (rows.empty()) {
         std::string t = title;
         if (! t.empty() && t.back() == ':') t.pop_back();
         wxui::info(this, t.empty() ? wxString("Nothing found")
                                    : "No " + wxString::FromUTF8(t) + " found");
         return;
      }
      wxui::ClickThroughDialog *d = new wxui::ClickThroughDialog(this, wxString::FromUTF8(title));
      for (const bandicoot_result_row &r : rows) d->list()->add_item(wxString::FromUTF8(r.label));
      d->list()->on_click = [rows](size_t i) {
         if (i < rows.size()) bandicoot_core_go_to_result(rows[i]);
      };
      d->CentreOnParent();
      d->Show();
   }

   // ---- sequence views: one docked, any number in windows of their own

   wxui::SequenceView *sv_docked = nullptr;
   int sv_height = 0;
   std::map<int, std::pair<wxFrame *, wxui::SequenceView *>> sv_windows;

   wxui::SequenceView *find_sequence_view(int imol) {
      if (sv_docked && sv_docked->molecule() == imol) return sv_docked;
      auto it = sv_windows.find(imol);
      return it == sv_windows.end() ? nullptr : it->second.second;
   }

   // Draw > Sequence View: shown docked or in a window, as the preference
   // says; a view already open for the molecule is brought forward.
   void open_sequence_view(int imol) {
      if (sv_windows.count(imol)) { sv_windows[imol].first->Raise(); return; }
      if (sv_docked && sv_docked->molecule() == imol) return;
      if (bandicoot_core_sequence_view_docked()) dock_sequence_view(imol);
      else float_sequence_view(imol);
   }

   wxui::SequenceView::Hooks sequence_view_hooks(int imol, bool docked) {
      wxui::SequenceView::Hooks h;
      h.toggle_docked = [this, imol, docked]() {
         bandicoot_core_save_sequence_view_docked(! docked);
         if (docked) float_sequence_view(imol);
         else dock_sequence_view(imol);
      };
      h.close = [this, imol]() { close_sequence_view(imol); };
      return h;
   }

   // Docking replaces any view already docked, as in the GTK build.
   void dock_sequence_view(int imol) {
      close_sequence_view(imol);
      if (sv_docked) close_sequence_view(sv_docked->molecule());
      sv_docked = new wxui::SequenceView(transient_area, imol, true, sequence_view_hooks(imol, true));
      transient_area->GetSizer()->Insert(0, sv_docked, 1, wxEXPAND);
      int rows = std::min(sv_docked->chain_count(), bandicoot_core_sequence_view_max_chains());
      sv_height = sv_docked->height_for_rows(rows);
      place_transient_bars();
   }

   void float_sequence_view(int imol) {
      close_sequence_view(imol);
      wxFrame *w = new wxFrame(this, wxID_ANY, "Sequence View", wxDefaultPosition, wxDefaultSize,
                               wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);
      wxui::SequenceView *view = new wxui::SequenceView(w, imol, false, sequence_view_hooks(imol, false));
      int rows = std::min(view->chain_count(), 30);
      w->SetClientSize(wxSize(800, view->height_for_rows(rows)));
      w->SetMinClientSize(wxSize(160, view->height_for_rows(1)));
      w->Bind(wxEVT_CLOSE_WINDOW, [this, imol](wxCloseEvent &) { close_sequence_view(imol); });
      sv_windows[imol] = { w, view };
      w->CentreOnParent();
      w->Show();
   }

   void close_sequence_view(int imol) {
      auto it = sv_windows.find(imol);
      if (it != sv_windows.end()) {
         wxFrame *w = it->second.first;
         sv_windows.erase(it);
         w->Destroy();
      }
      if (sv_docked && sv_docked->molecule() == imol) {
         wxui::SequenceView *v = sv_docked;
         sv_docked = nullptr;
         transient_area->GetSizer()->Detach(v);
         v->Destroy();
         place_transient_bars();
      }
   }

   // A model changed: its views are redrawn once the change is done.
   std::set<int> sv_pending;
   void sequence_model_changed(int imol) {
      if (! find_sequence_view(imol)) return;
      bool first = sv_pending.empty();
      sv_pending.insert(imol);
      if (! first) return;
      CallAfter([this]() {
         std::set<int> pending;
         pending.swap(sv_pending);
         for (int imol : pending)
            if (wxui::SequenceView *v = find_sequence_view(imol)) v->rebuild();
         if (sv_docked) {
            int most = sv_docked->height_for_rows(sv_docked->chain_count());
            if (sv_height > most) { sv_height = most; place_transient_bars(); }
         }
      });
   }

   // Views of molecules that have been closed go with them.
   void sequence_molecules_changed() {
      std::set<int> models;
      for (const auto &m : bandicoot_core_goto_molecules()) models.insert(m.first);
      std::vector<int> gone;
      if (sv_docked && ! models.count(sv_docked->molecule())) gone.push_back(sv_docked->molecule());
      for (const auto &w : sv_windows)
         if (! models.count(w.first)) gone.push_back(w.first);
      for (int imol : gone) close_sequence_view(imol);
   }

   void setup_sequence_views() {
      bandicoot_sequence_view_hooks h;
      h.model_changed = [this](int imol) { sequence_model_changed(imol); };
      h.molecules_changed = [this]() { CallAfter([this]() { sequence_molecules_changed(); }); };
      h.highlight = [this](int imol, const std::string &chain, int resno, const std::string &ins) {
         if (wxui::SequenceView *v = find_sequence_view(imol)) v->highlight(chain, resno, ins);
      };
      bandicoot_core_set_sequence_view_hooks(h);
   }

   // Shows the pending fit in the bar or the window, as docked.
   void show_fit() {
      if (bandicoot_core_ar_docked()) {
         ar_bar->set_status(wxString::FromUTF8(fit.results.status));
         ar_bar->set_lights(ar_bar_lights(fit.results));
         ar_bar->set_warnings(wxString::FromUTF8(fit.warnings));
         ar_bar->present(fit.auto_clear);
         place_transient_bars();
      } else {
         accept_reject->set_status(wxString::FromUTF8(fit.results.status));
         accept_reject->set_lights(ar_lights(fit.results));
         accept_reject->set_warnings(wxString::FromUTF8(fit.warnings));
         if (! accept_reject->IsShown()) {
            int x, y;
            if (bandicoot_core_ar_position(x, y))
               accept_reject->Move(x, y);
            else
               accept_reject->CentreOnParent();
         }
         accept_reject->present(wxString::FromUTF8(fit.fit_type), fit.show_reverse,
                                fit.auto_clear);
      }
   }

   // Docked or always-shown changed: a waiting fit moves to where it is
   // now answered.
   void ar_settings_changed() {
      if (fit.active) {
         bool docked_now = bandicoot_core_ar_docked();
         if (docked_now && accept_reject->pending()) {
            accept_reject->stand_down();
            show_fit();
         } else if (! docked_now && ar_bar->pending()) {
            ar_bar->stand_down();
            show_fit();
         }
      }
      place_transient_bars();
   }

   // Accept/Reject: the docked bar above the surface or the free-floating
   // window, as Coot's docked preference says, answers whenever the core
   // has a fit waiting. Return and Escape on the surface answer it too.
   void setup_accept_reject() {
      wxui::AcceptRejectDialog::Hooks h;
      h.on_action = [this](const std::string &action, bool state) {
         if (action == "on_accept_reject_refinement_accept_button_clicked" ||
             action == "on_accept_reject_refinement_reject_button_clicked")
            fit.active = false;
         bandicoot_core_run_action(action, state);
      };
      h.on_hidden = [](const wxPoint &p) { bandicoot_core_save_ar_position(p.x, p.y); };
      h.dock = []() { bandicoot_core_set_ar_docked(true); };
      accept_reject = new wxui::AcceptRejectDialog(this, h);

      wxui::AcceptRejectBar::Hooks bh;
      bh.on_action = [this](const std::string &action, bool state) {
         if (action == "on_accept_reject_refinement_atom_pull_autoclear_checkbutton_toggled")
            fit.auto_clear = state;
         bandicoot_core_run_action(action, state);
      };
      bh.on_idle = [this]() {
         fit.active = false;
         place_transient_bars();
      };
      bh.always_shown = []() { return bandicoot_core_ar_always_shown(); };
      bh.set_always_shown = [](bool always) { bandicoot_core_set_ar_always_shown(always); };
      bh.undock = []() { bandicoot_core_set_ar_docked(false); };
      ar_bar = new wxui::AcceptRejectBar(transient_area, bh);
      transient_area->GetSizer()->Add(ar_bar, 0, wxEXPAND);

      bandicoot_ar_view v;
      v.present = [this](const std::string &fit_type, const bandicoot_ar_results &r,
                         bool show_reverse, bool auto_clear) {
         fit.active = true;
         fit.fit_type = fit_type;
         fit.show_reverse = show_reverse;
         fit.auto_clear = auto_clear;
         fit.results = r;
         fit.warnings.clear();
         show_fit();
      };
      v.update = [this](const bandicoot_ar_results &r) {
         if (! fit.active) return;
         fit.results.status = r.status;
         if (! r.lights.empty()) fit.results.lights = r.lights;
         if (ar_bar->pending()) {
            ar_bar->set_status(wxString::FromUTF8(r.status));
            ar_bar->set_lights(ar_bar_lights(r));
         } else if (accept_reject->pending()) {
            accept_reject->set_status(wxString::FromUTF8(r.status));
            accept_reject->set_lights(ar_lights(r));
         }
      };
      v.warnings = [this](const std::string &text) {
         fit.warnings = text;
         ar_bar->set_warnings(wxString::FromUTF8(text));
         accept_reject->set_warnings(wxString::FromUTF8(text));
      };
      v.dismiss = [this]() {
         fit.active = false;
         ar_bar->stand_down();
         accept_reject->stand_down();
         place_transient_bars();
      };
      v.settings_changed = [this]() { ar_settings_changed(); };
      bandicoot_core_set_ar_view(v);
      place_transient_bars();

      surface->key_filter = [this](int code) {
         if (! ar_pending()) return wxui::model_dialogs_take_key(code);
         bool enter = code == WXK_RETURN || code == WXK_NUMPAD_ENTER;
         if (! enter && code != WXK_ESCAPE) return false;
         if (ar_bar->pending()) {
            if (enter) ar_bar->accept(); else ar_bar->reject();
         } else {
            if (enter) accept_reject->accept(); else accept_reject->reject();
         }
         return true;
      };
   }

   // What the core asks of the window as a whole: its info dialogs, the
   // refinement map, letting go of sidebar and fixed-atom toggles, and
   // timeouts.
   void setup_ui_view() {
      bandicoot_ui_view v;
      v.info = [this](const std::string &text) { wxui::info(this, wxString::FromUTF8(text)); };
      v.select_map = [this]() { wxui::select_refinement_map(this); };
      v.results = [this](const std::string &title, const std::vector<bandicoot_result_row> &rows) {
         show_results(title, rows);
      };
      v.release_fixed_atom_toggles = []() { wxui::release_fixed_atom_toggles(); };
      v.release_sidebar_toggle = [this](const std::string &widget) {
         if (sidebar) sidebar->release(widget);
      };
      v.timeout_add = [](int interval_ms, std::function<bool()> callback) {
         CallbackTimer *t = new CallbackTimer(std::move(callback));
         t->Start(std::max(interval_ms, 1));
      };
      bandicoot_core_set_ui_view(v);
   }

   wxPanel *slot_for_side() const {
      return side == 1 ? left_slot : side == 3 ? bottom_slot : right_slot;
   }

   wxui::Sidebar::Hooks sidebar_hooks() {
      wxui::Sidebar::Hooks h;
      h.on_select = [this](const std::string &action, const std::string &label, bool state) {
         // the sidebar's windows that are wx's own
         if (action == "on_model_toolbar_refine_control_button_clicked") { wxui::show_refine_params(this); return; }
         if (action == "on_model_toolbar_fixed_atoms_button_clicked") { wxui::show_fixed_atoms(this); return; }
         if (action == "on_model_toolbar_find_water_button_clicked") { wxui::show_find_waters(this); return; }
         if (action == "on_model_toolbar_refmac_button_clicked") { wxui::run_refmac(this); return; }
         if (action == "rot_trans_by") {
            int which = label == "By Chain..." ? 1 : label == "By Molecule..." ? 2 : 0;
            bandicoot_core_rot_trans_by(which);
            return;
         }
         if (! bandicoot_core_run_action(action, state))
            report_unconnected("Sidebar > " + label, action, state);
      };
      h.icon_states = []() {
         std::vector<wxui::Sidebar::IconState> v;
         for (const bandicoot_sidebar_icon &i : bandicoot_core_sidebar_icons())
            v.push_back({ i.widget, i.shown, i.shown_by_default });
         return v;
      };
      h.choose_icon_set = [](bool main_only) {
         bandicoot_core_sidebar_show_icon_set(main_only);
      };
      h.is_docked = [this]() { return docked; };
      h.dock_side = [this]() { return side; };
      // as for toggle_docked, once the menu has closed
      h.dock_on = [this](int s) {
         CallAfter([this, s]() {
            side = s;
            bandicoot_core_save_sidebar_position(s);
            bandicoot_core_save_sidebar_docked(true);
            place_sidebar(true);
         });
      };
      // after the menu that asked has closed: the sidebar is about to be destroyed
      h.toggle_docked = [this]() {
         CallAfter([this]() { set_sidebar_docked(! docked, true); });
      };
      return h;
   }

   // Build the sidebar where it now belongs, replacing the old one. Its style
   // carries over; which tools show is the core's and needs no carrying.
   void place_sidebar(bool dock) {

      wxui::Sidebar::Style style = sidebar ? sidebar->get_style()
                                           : sidebar_style(bandicoot_core_sidebar_style());
      if (sidebar) { sidebar->Destroy(); sidebar = nullptr; }
      if (floater) { floater->Destroy(); floater = nullptr; }
      docked = dock;

      // a slot shows only while it holds the sidebar
      for (wxPanel *slot : { left_slot, right_slot, bottom_slot })
         if (slot->GetChildren().empty()) slot->Hide();

      if (dock) {
         wxPanel *slot = slot_for_side();
         sidebar = new wxui::Sidebar(slot, side != 3, style,
                                     bandicoot_core_sidebar_icon_size(), sidebar_hooks());
         slot->GetSizer()->Add(sidebar, 1, wxEXPAND);
         slot->Show();
      } else {
         // A utility window floats above Bandicoot's others. It has no close,
         // minimize or zoom button: once closed there would be no way back.
         floater = new wxFrame(this, wxID_ANY, "Model Tools", wxDefaultPosition, wxDefaultSize,
                               wxCAPTION | wxRESIZE_BORDER | wxFRAME_TOOL_WINDOW);
         floater->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent &e) {
            if (e.CanVeto()) e.Veto(); else e.Skip();
         });
         sidebar = new wxui::Sidebar(floater, true, style,
                                     bandicoot_core_sidebar_icon_size(), sidebar_hooks());
         wxBoxSizer *s = new wxBoxSizer(wxVERTICAL);
         s->Add(sidebar, 1, wxEXPAND);
         floater->SetSizer(s);

         // as tall as the tools need, within the screen; beside this window
         wxRect screen = wxGetClientDisplayRect();
         wxSize natural = sidebar->natural_size();
         int h = std::min(natural.y, screen.height - 60);
         floater->SetClientSize(natural.x, h);
         floater->SetMinClientSize(wxSize(natural.x, 120));
         wxRect me = GetScreenRect();
         int x = me.GetRight() + 8;
         if (x + floater->GetSize().x > screen.GetRight()) x = std::max(screen.x, me.x - floater->GetSize().x - 8);
         floater->Move(x, std::max(screen.y, me.y));
         floater->Show();
      }
      Layout();
   }

   wxPanel *left_slot      = nullptr;
   wxPanel *right_slot     = nullptr;
   wxPanel *bottom_slot    = nullptr;
   wxPanel *transient_area = nullptr;
   wxSplitterWindow *splitter = nullptr;
   GLSurface *surface = nullptr;

   wxPanel *make_slot() {
      wxPanel *p = new wxPanel(this);
      p->SetSizer(new wxBoxSizer(wxVERTICAL));
      p->SetDropTarget(new SurfaceDropTarget);
      p->Hide();
      return p;
   }

   // Development aid: label and show every slot, and open the transient area,
   // so the layout can be inspected before anything occupies it.
   void show_layout_placeholders() {
      struct { wxPanel *panel; const char *label; wxSize size; } slots[] = {
         { left_slot,   "left slot",   wxSize(120, -1) },
         { right_slot,  "right slot",  wxSize(120, -1) },
         { bottom_slot, "bottom slot", wxSize(-1, 60)  }
      };
      for (auto &s : slots) {
         if (! s.panel->GetChildren().empty()) continue; // already holds something
         s.panel->GetSizer()->Add(new wxStaticText(s.panel, wxID_ANY, s.label), 0, wxALL, 6);
         s.panel->SetMinSize(s.size);
         s.panel->Show();
      }
      transient_area->GetSizer()->Add(new wxStaticText(transient_area, wxID_ANY,
                                                       "transient bars"), 0, wxALL, 6);
      show_transient_area(true);
   }

   void on_close(wxCloseEvent &e) {
      std::vector<std::string> unsaved = bandicoot_core_unsaved_molecules();
      if (! unsaved.empty() && e.CanVeto()) {
         std::string list;
         for (const std::string &u : unsaved) {
            std::cout << "INFO:: unsaved changes in molecule " << u << std::endl;
            list += u + "\n";
         }
         wxMessageDialog d(this, list, "Quit with unsaved changes?",
                           wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);
         d.SetYesNoLabels("Quit", "Cancel");
         if (d.ShowModal() != wxID_YES) {
            e.Veto();
            return;
         }
      }
      bandicoot_core_quit(); // does not return
   }
};

MainFrame *main_frame = nullptr;

GLSurface *the_surface() {
   return main_frame ? main_frame->get_surface() : nullptr;
}

} // namespace

wxIMPLEMENT_APP_NO_MAIN(BandicootApp);

// -------------------------------------------------------------------------
// surface.hh
// -------------------------------------------------------------------------

bool bandicoot_wx_init(int &argc, char **argv) {

   if (! wxEntryStart(argc, argv)) {
      std::cout << "ERROR:: wxWidgets failed to initialise" << std::endl;
      return false;
   }
   // wx's own diagnostics go to the terminal, not to dialogs
   delete wxLog::SetActiveTarget(new wxLogStderr);
   wxTheApp->SetAppName("Bandicoot");
   return wxTheApp->CallOnInit();
}

void bandicoot_wx_create_main_window(const std::string &title) {

   main_frame = new MainFrame(wxString::FromUTF8(title));
   wxTheApp->SetTopWindow(main_frame);
   main_frame->Show();
   main_frame->place_saved_sidebar();
   main_frame->Raise();
   main_frame->get_surface()->SetFocus();
}

int bandicoot_wx_run_main_loop() {
   int status = wxTheApp->OnRun();
   wxEntryCleanup();
   return status;
}

bool bandicoot_wx_surface_make_current() {
   GLSurface *s = the_surface();
   return s ? s->make_current() : false;
}

void bandicoot_wx_surface_swap_buffers() {
   GLSurface *s = the_surface();
   if (s) s->SwapBuffers();
}

void bandicoot_wx_surface_redraw() {
   GLSurface *s = the_surface();
   if (s) s->redraw();
}

double bandicoot_wx_surface_backing_scale() {
   GLSurface *s = the_surface();
   return s ? s->GetContentScaleFactor() : 1.0;
}

void bandicoot_wx_sidebar_refresh() {
   static bool pending = false;
   if (! main_frame || ! main_frame->get_sidebar() || pending) return;
   pending = true;
   wxTheApp->CallAfter([]() {
      pending = false;
      if (main_frame && main_frame->get_sidebar()) main_frame->get_sidebar()->refresh_icons();
   });
}

bool bandicoot_wx_sidebar_is_docked() {
   return main_frame ? main_frame->sidebar_docked() : true;
}

void bandicoot_wx_set_sidebar_docked(bool state) {
   if (main_frame) main_frame->set_sidebar_docked(state, true);
   else bandicoot_core_save_sidebar_docked(state);
}

void bandicoot_wx_set_sidebar_style(int style) {
   if (main_frame && main_frame->get_sidebar())
      main_frame->get_sidebar()->set_style(sidebar_style(style));
}

void bandicoot_wx_set_status_text(const std::string &text) {
   if (main_frame) main_frame->SetStatusText(wxString::FromUTF8(text));
}

void bandicoot_wx_surface_set_cursor(bandicoot_wx_cursor_t cursor) {

   GLSurface *s = the_surface();
   if (! s) return;
   wxStockCursor c = wxCURSOR_ARROW;
   switch (cursor) {
   case BANDICOOT_WX_CURSOR_NORMAL: c = wxCURSOR_ARROW;  break;
   case BANDICOOT_WX_CURSOR_PICK:   c = wxCURSOR_CROSS;  break;
   case BANDICOOT_WX_CURSOR_WATCH:  c = wxCURSOR_WAIT;   break;
   case BANDICOOT_WX_CURSOR_FLEUR:  c = wxCURSOR_SIZING; break;
   }
   s->SetCursor(wxCursor(c));
}

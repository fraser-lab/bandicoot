// src/wxUI/main/surface.hh
//
// Services the wx main window provides to the rest of Bandicoot. Only compiled
// into a --with-ui=wx build.
//
// This header carries no wx or GTK types on purpose: it is included both by
// the wx side and by core files that include GTK, and the two toolkits' headers
// are kept out of each other's translation units.

#ifndef BANDICOOT_WX_SURFACE_HH
#define BANDICOOT_WX_SURFACE_HH

#include <string>

// Start wxWidgets. Must run before gtk_init(), so that the application object
// AppKit creates is wx's.
bool bandicoot_wx_init(int &argc, char **argv);

// Build and show the main window: GL surface, transient-bar area, toolbar
// slots. The core is expected to have created its (hidden) GTK widget tree first.
void bandicoot_wx_create_main_window(const std::string &title);

// Run the wx event loop until the main window closes.
int bandicoot_wx_run_main_loop();

// The GL surface. make_current returns false while there is no drawable yet.
bool   bandicoot_wx_surface_make_current();
void   bandicoot_wx_surface_swap_buffers();
void   bandicoot_wx_surface_redraw();       // synchronous, like gtk_widget_draw()
double bandicoot_wx_surface_backing_scale();

// Text for the status bar at the bottom of the main window.
void bandicoot_wx_set_status_text(const std::string &text);

// The sidebar: re-read which tools the core shows (deferred, so a run of
// changes redraws once), and apply a style (1 icons, 2 icons and text, 3 text).
void bandicoot_wx_sidebar_refresh();
void bandicoot_wx_set_sidebar_style(int style);
// Whether the sidebar is docked in the main window; dock or undock it (and
// remember the choice).
bool bandicoot_wx_sidebar_is_docked();
void bandicoot_wx_set_sidebar_docked(bool state);

enum bandicoot_wx_cursor_t {
   BANDICOOT_WX_CURSOR_NORMAL,
   BANDICOOT_WX_CURSOR_PICK,
   BANDICOOT_WX_CURSOR_WATCH,
   BANDICOOT_WX_CURSOR_FLEUR
};
void bandicoot_wx_surface_set_cursor(bandicoot_wx_cursor_t cursor);

#endif // BANDICOOT_WX_SURFACE_HH

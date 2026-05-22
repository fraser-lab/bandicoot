#ifndef BANDICOOT_APPKIT_H
#define BANDICOOT_APPKIT_H

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mirror the in-window GtkMenuBar into the macOS system menu bar.
// `menubar` must be a GtkMenuBar widget. After mirroring, the original
// widget is hidden so it no longer fights the GtkGLExt-Quartz GL backing
// layer for the main NSWindow's contentView.
//
// NSMenuItem actions are forwarded to the corresponding GtkMenuItem's
// "activate" signal via gtk_menu_item_activate(). Coot's existing handlers
// (set up by libglade in gtk2-interface.c) fire unchanged.
//
// No-op on non-macOS builds.
void bandicoot_install_native_menubar(GtkWidget *menubar);

// Backing scale factor of the main screen (2.0 on Retina, 1.0 on non-Retina).
// Used to convert GTK logical-pixel widget allocations into physical pixels
// for glViewport, which on Tahoe-quartz otherwise renders at quarter size.
double bandicoot_get_backing_scale_factor(void);

// Mirror a GtkToolbar into a native NSToolbar attached to the toplevel
// NSWindow's title bar. Walks gtk_toolbar's children to build the initial
// visible set, and walks model_toolbar (the sidebar) to populate the
// customize-palette catalog with every other available tool button.
// User-customization is enabled (right-click → Customize Toolbar… or
// call bandicoot_run_toolbar_customization), and macOS autosaves the
// user's layout in NSUserDefaults keyed on "bandicoot.main".
//
// model_toolbar may be NULL — in that case only the main toolbar's items
// (plus Bandicoot extras) populate the palette.
void bandicoot_install_native_toolbar(GtkWidget *gtk_toolbar,
                                      GtkWidget *model_toolbar);

// Show the Customize Toolbar… sheet for the Bandicoot main toolbar.
// Wired up from a menu item in the View menu so users can find it
// without right-clicking on the toolbar.
void bandicoot_run_toolbar_customization(void);

// Reparent a GtkToolbar (or any widget) into a new toplevel GtkWindow,
// making it transient to its current toplevel. The new window gets its
// own NSWindow which is not bound by the main window's GL backing layer,
// so the widgets render normally. Close button is wired to hide-on-delete
// so the window can be re-shown.
void bandicoot_float_widget_in_window(GtkWidget *widget, const char *title);

// Force the application to the foreground (raises all our NSWindows above
// the windows of other apps). Needed because GUI apps launched from a
// shell wrapper script are background apps by default on macOS.
void bandicoot_activate_app(void);

// Install a global GTK emission hook that gives every newly-realized top-
// level window a sane default position: GTK_WIN_POS_CENTER_ON_PARENT for
// transient dialogs, GTK_WIN_POS_MOUSE otherwise. Windows that explicitly
// set their own position policy (splash, etc.) are left alone. Without
// this hook, GTK-Quartz on Tahoe places unpositioned windows in the
// lower-left of the screen, far from whatever button spawned them.
// Call once at startup, before any windows are realized.
void bandicoot_setup_window_positioning(void);

// Install a global GTK emission hook that raises every top-level window
// to the front when it's mapped to the display. Fixes the cached-dialog
// re-show bug where Coot's cached widgets (e.g. "Accept Refinement?")
// pop back into the stale z-order slot they had last time — behind the
// main window — instead of coming forward. Uses AppKit's [NSWindow
// orderFront:] directly because GTK 2's Quartz backend doesn't reliably
// re-order NSWindows on its own. Call once at startup, after gtk_init().
void bandicoot_setup_window_raising(void);

// Query AppKit's modifier flags directly. GTK-Quartz on Tahoe doesn't
// populate event->state with the shift/control bits, so any code that
// relies on event->state (e.g. shift-click for atom labeling) sees no
// modifier even when it's held. Return 1 if pressed, 0 otherwise.
int bandicoot_shift_pressed(void);
int bandicoot_control_pressed(void);

// Render UTF-8 `text` at the given point size into a new GL_TEXTURE_2D
// and return the texture name (0 on failure). The texture is white text
// on a transparent background; tint via glColor before drawing. The
// caller owns the returned texture and must free it with
// bandicoot_free_text_texture once done drawing. Used as a replacement
// for the glutStrokeCharacter / glutBitmapCharacter text paths which
// silently fail in the freeglut shipped on macOS Tahoe.
unsigned int bandicoot_make_text_texture(const char *text,
                                         double point_size,
                                         int *out_width,
                                         int *out_height);
void bandicoot_free_text_texture(unsigned int tex);

#ifdef __cplusplus
}
#endif

#endif

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

// Install an NSEvent local monitor that pops a "Customize Toolbar…"
// context menu when the user right-clicks anywhere in the toolbar
// strip. Restores macOS-native right-click behaviour that GTK-Quartz's
// contentView otherwise intercepts on Tahoe. Call once at startup
// after bandicoot_install_native_toolbar.
void bandicoot_install_right_click_handler(void);

// Reparent a GtkToolbar (or any widget) into a new toplevel GtkWindow,
// making it transient to its current toplevel. The new window gets its
// own NSWindow which is not bound by the main window's GL backing layer,
// so the widgets render normally. Close button is wired to hide-on-delete
// so the window can be re-shown.
void bandicoot_float_widget_in_window(GtkWidget *widget, const char *title);

// Append a "Dock"/"Undock" toggle item to the model toolbar's settings
// popup menu (the bottom triangle button, model_toolbar_setting1_menu).
// Replaces the standalone Dock button row; the item's label flips between
// "Dock" and "Undock" as the sidebar is pinned/released. Call once at
// startup after bandicoot_float_widget_in_window.
void bandicoot_sidebar_add_dock_menu_item(GtkWidget *menu);

// Replace the model toolbar's settings triangle (model_toolbar_style_toolitem)
// with a full-width "Settings ▸" button at the bottom of the sidebar whose menu
// (settings_menu = model_toolbar_setting1_menu) pops to the SIDE. Call once at
// startup right after bandicoot_float_widget_in_window.
void bandicoot_sidebar_pin_settings_item(GtkWidget *item, GtkWidget *settings_menu);

// ---- Native Accept/Reject bar (top child window) ------------------------
// Build the always-shown native A/R bar and dock it to the top edge. Call once
// at startup after the main window is realized.
void bandicoot_install_accept_reject_bar(GtkWidget *main_window);
// Refresh the geometry lights from refinement results: n colour rectangles,
// names[i]/values[i] drawn inside, tooltips[i] the full label, colors[i] the
// distortion colour. n=0 hides them and disables Accept/Reject (idle bar).
void bandicoot_ar_bar_set_lights(int n, const char *const *names,
                                 const char *const *values,
                                 const char *const *tooltips,
                                 const GdkColor *colors);
// 1 if the native bar is handling Accept/Reject (docked == Yes), so
// do_accept_reject_dialog should route to it instead of Coot's stock dialog.
int bandicoot_ar_bar_is_active(void);
// A refinement/fit is pending: enable Accept/Reject and (in Always-hide mode)
// pop the bar into view. Call from do_accept_reject_dialog when routing to it.
void bandicoot_ar_bar_present(void);
// Apply the "Dock Accept/Reject Dialog?" preference: active = docked (Yes/No),
// always_show = Always show (1) vs Always hide (0). Updates bar visibility.
void bandicoot_ar_bar_apply_prefs(int active, int always_show);

// Force the application to the foreground (raises all our NSWindows above
// the windows of other apps). Needed because GUI apps launched from a
// shell wrapper script are background apps by default on macOS.
void bandicoot_activate_app(void);

// Disable macOS automatic window tabbing app-wide. Without this, once the user
// has entered full-screen mode (the "Prefer tabs" system default is "In Full
// Screen Only") AppKit merges Bandicoot's dialogs into the main window's tab
// group, and the grouping persists after leaving full screen — so every dialog
// opens as a tab. Call once at startup, before any windows are created.
// No-op on non-macOS builds.
void bandicoot_disable_window_tabbing(void);

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

// Pin a recreated-each-time dialog above the main window and remember where
// the user last left it. `role` is a stable key (e.g. "residue-type-chooser")
// used to restore the on-screen origin across invocations. The NSWindow is
// placed at the floating level so it stays above the normal-level main window
// until dismissed — fixing the "chooser opens then the main window buries it"
// behaviour for the Mutate residue-type / nucleotide-base choosers. The origin
// is captured on unmap (keyed by role) so the next incarnation reappears in
// the same spot. Call once on the freshly-created widget, before showing it.
// No-op on non-macOS builds.
void bandicoot_register_persistent_dialog(GtkWidget *w, const char *role);

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

// Install a native status bar as a borderless child NSWindow pinned flush to
// the bottom edge of the main window, full content width. GTK's in-window
// main_window_statusbar lives on the GL-owned contentView and is permanently
// occluded by the GtkGLExt-Quartz backing layer (same reason the menubar and
// toolbars were moved out); a child NSWindow renders above the GL layer, like
// the floated model toolbar. addChildWindow: keeps it glued to the parent on
// move and a resize/move notification observer keeps its width in sync.
// Call once at startup, after gtk_widget_show() has realized the main window.
// No-op on non-macOS builds.
void bandicoot_install_status_bar(GtkWidget *main_window);

// Update the native status bar text. Funnelled here from
// graphics_info_t::add_status_bar_text() under __APPLE__. No-op if the bar
// has not been installed or on non-macOS builds.
void bandicoot_set_status_text(const char *text);

#ifdef __cplusplus
}
#endif

#endif

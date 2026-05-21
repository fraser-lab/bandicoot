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
// NSWindow's title bar. Walks toolbar's children, builds NSToolbarItems with
// images from each GtkToolButton's GtkImage, and forwards clicks to the
// corresponding GtkToolButton's "clicked" signal. Hides the in-window toolbar.
void bandicoot_install_native_toolbar(GtkWidget *gtk_toolbar);

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

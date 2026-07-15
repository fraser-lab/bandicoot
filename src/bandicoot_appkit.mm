// Bandicoot AppKit shim: mirrors Coot's in-window GtkMenuBar into the
// macOS system menu bar (NSApp.mainMenu) and hides the in-window widget.
//
// The GtkGLExt-Quartz NSOpenGLContext is bound to the main NSWindow's
// contentView and its backing layer reattaches on every reshape, so any
// in-window GtkMenuBar/GtkToolbar is permanently covered. Moving them to
// native AppKit objects sidesteps the conflict entirely.

#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#import <OpenGL/gl.h>
#import <CoreServices/CoreServices.h>  // kCoreEventClass / kAEQuitApplication

#include <gtk/gtk.h>
#include <gdk/gdkquartz.h>

#include <string>   // for Quicksave's filename munging
#include <sys/stat.h>  // for stat() — verifying Quicksave wrote a file

#include "bandicoot_appkit.h"

// Forward-declare Coot C-interface functions we need for the Quicksave
// toolbar action. These live inside BEGIN_C_DECLS in c-interface.h, so
// extern "C" matches the actual linkage.
//
// Note: Coot's safe_python_command_by_char_star et al. are not usable
// from Bandicoot because USE_PYTHON is deliberately undefined in
// build.sh — see the comment there. Python-dispatched toolbar items
// are therefore not exposed in BANDICOOT_EXTRAS.
extern "C" {
    int first_coords_imol(void);
    int go_to_atom_molecule_number(void);   // currently-focused molecule
    const char *molecule_name(int imol);
    int save_coordinates(int imol, const char *filename);
    short int is_valid_model_molecule(int imol);

    // C functions backing the toolbar toggles and one-shot actions
    // below. All declared inside BEGIN_C_DECLS in c-interface.h.
    int  graphics_n_molecules(void);
    void set_draw_hydrogens(int imol, int istat);
    void do_cis_trans_conversion_setup(int istate);
    void set_rotamer_search_mode(int mode);
    void set_do_probe_dots_on_rotamers_and_chis(short int state);
    void set_do_probe_dots_post_refine(short int state);
    void set_do_coot_probe_dots_during_refine(short int state);
    void full_screen(int mode);
    void graphics_draw(void);
    // Defined in bandicoot_refine.cc; closes every generic-display-object
    // whose name matches a probe-dot family.
    void bandicoot_clear_probe_dot_objects(void);

    // Sphere/Tandem refine implementations live in bandicoot_refine.cc
    // so the .cc file can pull in Coot's C++ headers for the active
    // atom + neighbor-finding + refine APIs (which take/return C++
    // types and aren't usable from this .mm unit without dragging in
    // a lot of Coot's internals). Signature matches GSourceFunc so they
    // can be wired into BANDICOOT_EXTRAS.c_callback directly.
    gboolean bandicoot_action_sphere_refine(gpointer);
    gboolean bandicoot_action_sphere_refine_plus(gpointer);
    gboolean bandicoot_action_sphere_regularize(gpointer);
    gboolean bandicoot_action_sphere_regularize_plus(gpointer);
    gboolean bandicoot_action_refine_tandem(gpointer);
    gboolean bandicoot_action_local_probe_dots(gpointer);

    // Clean-exit path (c-interface-gui.cc): saves session state + history,
    // closes all molecules, then exit(retval). Backs the "Exit Bandicoot"
    // app-menu item.
    void coot_real_exit(int retval);
}

static char kGtkMenuItemKey;

// Run on the next GLib main-loop iteration, i.e. after AppKit has fully
// dismissed the NSMenu and returned to NSDefaultRunLoopMode. This avoids
// firing GTK handlers (which create dialogs and pump nested gtk_main loops)
// from inside NSMenu's modal event-tracking call stack, where GDK-Quartz's
// nextEventMatchingMask: pump can't make progress.
static gboolean bandicoot_fire_gtk_activate(gpointer data) {
    GtkMenuItem *gtk_item = (GtkMenuItem *)data;
    if (gtk_item && GTK_IS_MENU_ITEM(gtk_item)) {
        gtk_menu_item_activate(gtk_item);
    }
    return G_SOURCE_REMOVE;
}

// Forward-declare so the menu target can call it without depending on the
// later toolbar shim's declaration order.
extern "C" void bandicoot_run_toolbar_customization(void);

@interface BandicootMenuTarget : NSObject
+ (instancetype)shared;
- (void)dispatch:(NSMenuItem *)sender;
- (void)bandicootCustomizeToolbar:(id)sender;
- (void)bandicootExit:(id)sender;
- (void)bandicootHandleQuitEvent:(NSAppleEventDescriptor *)event
                  withReplyEvent:(NSAppleEventDescriptor *)reply;
- (void)bandicootSetToolbarDisplayModeIconAndLabel:(id)sender;
- (void)bandicootSetToolbarDisplayModeIconOnly:(id)sender;
- (void)bandicootSetToolbarDisplayModeLabelOnly:(id)sender;
@end

// Helper used by the toolbar-display-mode menu items: find the
// Bandicoot main NSToolbar (by identifier) and apply a new display mode.
static void bandicoot_set_toolbar_display_mode(NSToolbarDisplayMode mode) {
    for (NSWindow *w in [NSApp windows]) {
        NSToolbar *tb = [w toolbar];
        if (tb && [[tb identifier] isEqualToString:@"bandicoot.main.v2"]) {
            tb.displayMode = mode;
            return;
        }
    }
}

@implementation BandicootMenuTarget
+ (instancetype)shared {
    static BandicootMenuTarget *s;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [BandicootMenuTarget new]; });
    return s;
}
- (void)dispatch:(NSMenuItem *)sender {
    NSValue *boxed = objc_getAssociatedObject(sender, &kGtkMenuItemKey);
    GtkMenuItem *gtk_item = (GtkMenuItem *)[boxed pointerValue];
    if (gtk_item && GTK_IS_MENU_ITEM(gtk_item)) {
        g_idle_add(bandicoot_fire_gtk_activate, gtk_item);
    }
}
- (void)bandicootCustomizeToolbar:(id)sender {
    bandicoot_run_toolbar_customization();
}
- (void)bandicootExit:(id)sender {
    // Confirm before quitting. NSAlert (native) rather than a GTK dialog:
    // this item lives in the macOS app menu and the alert is pure AppKit,
    // so it's safe to run modally from inside NSMenu's event tracking.
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"Exit Bandicoot?"];
    [alert setInformativeText:@"This will close the current session. "
                               "Any unsaved changes will be lost."];
    [alert addButtonWithTitle:@"Yes"];               // first -> default (Return)
    NSButton *no = [alert addButtonWithTitle:@"No"];
    [no setKeyEquivalent:@"\033"];                   // Escape selects No
    if ([alert runModal] == NSAlertFirstButtonReturn) {
        coot_real_exit(0);  // saves state + history, closes molecules, exit(0)
    }
    // No -> fall through, returning to the session.
}
- (void)bandicootHandleQuitEvent:(NSAppleEventDescriptor *)event
                  withReplyEvent:(NSAppleEventDescriptor *)reply {
    // The Dock icon's right-click "Quit" (and any kAEQuitApplication event)
    // lands here. Route it through the same "Exit Bandicoot?" confirmation
    // and clean exit as the menu item. Without this handler the event went
    // to GDK-Quartz's default delegate, which doesn't tear down the GTK main
    // loop — so Quit appeared to do nothing.
    (void)event; (void)reply;
    [self bandicootExit:nil];
}
- (void)bandicootSetToolbarDisplayModeIconAndLabel:(id)sender {
    bandicoot_set_toolbar_display_mode(NSToolbarDisplayModeIconAndLabel);
}
- (void)bandicootSetToolbarDisplayModeIconOnly:(id)sender {
    bandicoot_set_toolbar_display_mode(NSToolbarDisplayModeIconOnly);
}
- (void)bandicootSetToolbarDisplayModeLabelOnly:(id)sender {
    bandicoot_set_toolbar_display_mode(NSToolbarDisplayModeLabelOnly);
}
@end

static NSString *string_from_gtk_label(GtkMenuItem *item) {
    const char *label = gtk_menu_item_get_label(item);
    if (!label || *label == '\0') return @"";
    NSMutableString *out = [NSMutableString string];
    BOOL stripped = NO;
    for (const char *p = label; *p; ++p) {
        if (*p == '_' && !stripped) { stripped = YES; continue; }
        stripped = NO;
        char buf[2] = { *p, 0 };
        [out appendFormat:@"%s", buf];
    }
    return out;
}

static void populate_from_gtk_menu(NSMenu *ns_menu, GtkMenuShell *gtk_menu);

// v0.1.0.2: NSMenu delegate that re-mirrors a GTK submenu every time the
// user opens it. Without this we only get a startup snapshot, missing any
// submenu items added by later C-code calls -- e.g. Draw > Sequence View
// populates `seq_view_menu` lazily in `add_on_sequence_view_choices()`
// when the user activates the parent item. Caveat: this only catches
// submenu populations that land on the REAL GtkMenu. Python-side
// extensions.py uses PyGObject (which we currently stub), so its
// dynamic submenus don't reach the GtkMenu and this delegate has
// nothing to mirror -- those remain blocked on the v0.1.1.0 PyGObject port.
@interface BandicootMenuMirror : NSObject <NSMenuDelegate>
@property (nonatomic, assign) GtkMenuShell *gtkMenuShell;
@end

static int bandicoot_gtk_menu_child_count(GtkMenuShell *shell) {
    if (!shell || !GTK_IS_MENU_SHELL(shell)) return 0;
    GList *children = gtk_container_get_children(GTK_CONTAINER(shell));
    int n = (int) g_list_length(children);
    g_list_free(children);
    return n;
}

// Sticky "this submenu re-populates itself when its parent's activate
// handler fires" marker. Stashed on the GtkMenu via g_object_set_data
// rather than on the Objective-C BandicootMenuMirror, because the parent
// NSMenu re-mirrors its children on every open -- discarding and
// recreating each child NSMenu + delegate. The GtkMenu persists across
// rebuilds, so the flag survives there. Set when our first
// activate-on-empty fires and items appear (e.g. Draw > Sequence View).
// Read on every subsequent menuNeedsUpdate to decide whether to re-fire.
#define BANDICOOT_MENU_DYNAMIC_KEY "bandicoot.menu.is_dynamic_populator"

@implementation BandicootMenuMirror
- (void)menuNeedsUpdate:(NSMenu *)menu {
    if (!self.gtkMenuShell || !GTK_IS_MENU_SHELL(self.gtkMenuShell)) return;
    GtkWidget *attached = gtk_menu_get_attach_widget(GTK_MENU(self.gtkMenuShell));
    BOOL has_parent = (attached && GTK_IS_MENU_ITEM(attached));
    int n_before = bandicoot_gtk_menu_child_count(self.gtkMenuShell);
    BOOL is_dynamic = (g_object_get_data(G_OBJECT(self.gtkMenuShell),
                                         BANDICOOT_MENU_DYNAMIC_KEY) != NULL);

    if (is_dynamic && has_parent) {
        // Known dynamic submenu: refresh on every open so molecule
        // add/remove is reflected.
        gtk_menu_item_activate(GTK_MENU_ITEM(attached));
    } else if (n_before == 0 && has_parent) {
        // First encounter with this submenu empty -- give the parent
        // menuitem's activate handler a chance to populate. If items
        // appear, mark dynamic so future opens refresh.
        // We never fire activate on already-populated submenus, to
        // avoid real-work side effects (e.g. Validate's probe_available_p()
        // which currently SEGVs via a Py3 incompat in coot_utils.find_exe).
        gtk_menu_item_activate(GTK_MENU_ITEM(attached));
        if (bandicoot_gtk_menu_child_count(self.gtkMenuShell) > 0) {
            g_object_set_data(G_OBJECT(self.gtkMenuShell),
                              BANDICOOT_MENU_DYNAMIC_KEY,
                              GINT_TO_POINTER(1));
        }
    }

    [menu removeAllItems];
    populate_from_gtk_menu(menu, self.gtkMenuShell);
}
@end

// Menu items from upstream Coot 0.9 that we suppress in the native NSMenu
// because Bandicoot has replaced their functionality. Matched against the
// underscore-stripped GTK label.
//   "Model/Fit/Refine..." — upstream's floating Glade dialog with rainbow
//     icons; superseded by Bandicoot's permanent Model Tools side window.
//   "All Molecule..." / "Modelling..." / "Modules..." / "NCS Tools..." / "PISA..."
//     — dead extensions.py submenus (built with the stubbed pygtk gtk.Menu(), so
//     they attach nothing and click to nowhere). Redundant/niche/doubly-dead —
//     see the v0.1.1.0 todo. ("Modelling..."'s useful ops were folded into the
//     "Other Modelling Tools..." dialog, so its item is suppressed too.)
static BOOL bandicoot_suppress_menu_item(NSString *title) {
    static NSArray<NSString *> *suppress = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        suppress = @[ @"Model/Fit/Refine...",
                      @"All Molecule...", @"Modelling...", @"Modules...",
                      @"NCS Tools...", @"PISA..." ];
    });
    for (NSString *s in suppress) {
        if ([title isEqualToString:s]) return YES;
    }
    return NO;
}

static NSMenuItem *build_item(GtkMenuItem *gtk_item) {
    if (GTK_IS_SEPARATOR_MENU_ITEM(gtk_item)) {
        return [NSMenuItem separatorItem];
    }
    NSString *title = string_from_gtk_label(gtk_item);
    if (bandicoot_suppress_menu_item(title)) return nil;
    NSMenuItem *ns_item = [[NSMenuItem alloc] initWithTitle:title
                                                     action:nil
                                              keyEquivalent:@""];

    GtkWidget *sub = gtk_menu_item_get_submenu(gtk_item);
    if (sub && GTK_IS_MENU(sub)) {
        NSMenu *ns_sub = [[NSMenu alloc] initWithTitle:title];
        [ns_sub setAutoenablesItems:NO];
        populate_from_gtk_menu(ns_sub, GTK_MENU_SHELL(sub));
        // v0.1.0.2: attach a mirror delegate so anything added to this
        // GTK submenu after startup (e.g. Sequence View's molecule list)
        // shows up on next open. NSMenu.delegate is a zeroing weak ref,
        // so we associate-retain the delegate on the menu to keep it alive.
        BandicootMenuMirror *mirror = [BandicootMenuMirror new];
        mirror.gtkMenuShell = GTK_MENU_SHELL(sub);
        [ns_sub setDelegate:mirror];
        objc_setAssociatedObject(ns_sub, "bandicoot.menu.mirror",
                                 mirror, OBJC_ASSOCIATION_RETAIN);
        [ns_item setSubmenu:ns_sub];
    } else {
        [ns_item setTarget:[BandicootMenuTarget shared]];
        [ns_item setAction:@selector(dispatch:)];
        if (!gtk_widget_is_sensitive(GTK_WIDGET(gtk_item))) {
            [ns_item setEnabled:NO];
        }
        objc_setAssociatedObject(ns_item, &kGtkMenuItemKey,
                                 [NSValue valueWithPointer:gtk_item],
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return ns_item;
}

static void populate_from_gtk_menu(NSMenu *ns_menu, GtkMenuShell *gtk_menu) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(gtk_menu));
    for (GList *l = children; l; l = l->next) {
        if (!GTK_IS_MENU_ITEM(l->data)) continue;
        NSMenuItem *ns_item = build_item(GTK_MENU_ITEM(l->data));
        if (ns_item) [ns_menu addItem:ns_item];
    }
    g_list_free(children);
}

extern "C" double bandicoot_get_backing_scale_factor(void) {
    NSScreen *screen = [NSScreen mainScreen];
    return screen ? (double)[screen backingScaleFactor] : 1.0;
}

// Backing scale of the screen the widget's window is ACTUALLY on. Prefer this
// over bandicoot_get_backing_scale_factor(): [NSScreen mainScreen] tracks the
// focused / menu-bar screen, which is the wrong scale on a mixed-DPI
// multi-monitor setup (e.g. a 2x Retina built-in + a 1x external). NSWindow's
// backingScaleFactor follows the screen the window currently sits on and
// updates as it is dragged between displays. Falls back to mainScreen, then 1.
extern "C" double bandicoot_get_backing_scale_factor_for_widget(GtkWidget *w) {
    if (w) {
        GtkWidget *top = gtk_widget_get_toplevel(w);
        if (top && top->window) {
            NSWindow *win = (NSWindow *) gdk_quartz_window_get_nswindow(top->window);
            if (win) return (double)[win backingScaleFactor];
        }
    }
    NSScreen *screen = [NSScreen mainScreen];
    return screen ? (double)[screen backingScaleFactor] : 1.0;
}

extern "C" void bandicoot_activate_app(void) {
    // Apps launched from a wrapper script start as background apps; their
    // windows can appear behind the launcher's own windows. Force foreground.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
}

// Disable macOS automatic window tabbing (NSWindow, 10.12+). Bandicoot is not
// a document-based tabbed app: its dialogs are independent GTK-Quartz NSWindows
// all left at the default tabbingMode == .automatic, so AppKit is free to merge
// each new one into the main window's tab group. That fires once the user has
// entered full-screen (the system "Prefer tabs when opening documents" default
// is "In Full Screen Only"), and the grouping persists after leaving full
// screen — so every dialog then opens as a tab inside the main window. Flipping
// the class-level switch off removes the automatic-tabbing mechanism entirely
// (no "Prefer tabs" value or full-screen state can re-trigger it) and strips the
// system tabbing menu items. Must run before any windows are created.
extern "C" void bandicoot_disable_window_tabbing(void) {
    if ([NSWindow respondsToSelector:@selector(setAllowsAutomaticWindowTabbing:)])
        [NSWindow setAllowsAutomaticWindowTabbing:NO];
}

extern "C" int bandicoot_shift_pressed(void) {
    return ([NSEvent modifierFlags] & NSEventModifierFlagShift) ? 1 : 0;
}

extern "C" int bandicoot_control_pressed(void) {
    NSEventModifierFlags f = [NSEvent modifierFlags];
    return (f & NSEventModifierFlagControl) ? 1 : 0;
}

extern "C" unsigned int bandicoot_make_text_texture(const char *text,
                                                    double point_size,
                                                    int *out_width,
                                                    int *out_height) {
    if (!text || !*text) return 0;

    // v0.1.0.5: this function runs once per atom label per draw frame; the
    // build doesn't use ARC so MRC rules apply. The original code leaked
    // NSBitmapImageRep on every call (alloc/init gives +1 retain, never
    // released) and accumulated autoreleased NSString/NSDictionary/NSGCtx
    // until the event-loop drained its pool -- often minutes during heavy
    // modeling. Result: 10-15 minute Jetsam OOM kill (SIGKILL), no Bandicoot
    // crash log. Wrap the whole body in @autoreleasepool to drain transients
    // per call, and explicitly release the bitmap rep.
    GLuint tex = 0;
    int w = 0, h = 0;
    @autoreleasepool {
        NSString *str = [NSString stringWithUTF8String:text];
        if (!str) return 0;

        // Use a monospaced face so atom labels (with their slash-separated
        // chain/residue fields) line up vertically when stacked. Menlo ships
        // with every macOS install. Fall back to the system fixed-pitch font
        // if Menlo is somehow missing.
        if (point_size < 8.0) point_size = 8.0;
        NSFont *font = [NSFont fontWithName:@"Menlo" size:point_size];
        if (!font) font = [NSFont userFixedPitchFontOfSize:point_size];
        if (!font) font = [NSFont systemFontOfSize:point_size];
        NSDictionary *attrs = @{
            NSFontAttributeName:            font,
            NSForegroundColorAttributeName: [NSColor whiteColor]
        };
        NSSize sz = [str sizeWithAttributes:attrs];
        w = (int)ceil(sz.width)  + 8;
        h = (int)ceil(sz.height) + 8;
        if (w <= 0 || h <= 0) return 0;

        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL
                          pixelsWide:w
                          pixelsHigh:h
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:w * 4
                        bitsPerPixel:32];
        if (!rep) return 0;

        NSGraphicsContext *gc =
            [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:gc];

        // Transparent background, white text. Caller can tint via glColor.
        [[NSColor clearColor] set];
        NSRectFill(NSMakeRect(0, 0, w, h));
        [str drawAtPoint:NSMakePoint(4, 4) withAttributes:attrs];

        [gc flushGraphics];
        [NSGraphicsContext restoreGraphicsState];

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, [rep bitmapData]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        [rep release];
    }

    if (out_width)  *out_width  = w;
    if (out_height) *out_height = h;
    return tex;
}

extern "C" void bandicoot_free_text_texture(unsigned int tex) {
    if (tex == 0) return;
    GLuint t = tex;
    glDeleteTextures(1, &t);
}

// Rename the app so the macOS application menu (the bold first menu) reads
// "Bandicoot" instead of the binary name "coot-bin". AppKit takes that title
// from the application name, which for this unbundled binary defaults to the
// process name (argv[0] basename). It is read and cached the first time
// NSApplication is instantiated (inside gtk_init on the Quartz backend), so
// this MUST run before gtk_init() — setting it later (e.g. at menu-install
// time) is too late, the cached "coot-bin" name has already been used.
extern "C" void bandicoot_set_application_name(void) {
    [[NSProcessInfo processInfo] setProcessName:@"Bandicoot"];
}

extern "C" void bandicoot_install_native_menubar(GtkWidget *menubar) {
    if (!menubar || !GTK_IS_MENU_BAR(menubar)) return;

    NSMenu *main_menu = [[NSMenu alloc] initWithTitle:@""];
    [main_menu setAutoenablesItems:NO];

    // First slot is the application menu (Bandicoot > About / Quit etc.).
    // Populate it with Bandicoot-specific items: macOS conventionally
    // places "Customize Toolbar…" under View, but Coot 0.9 has no View
    // menu so the app menu is the natural home (and where users tend to
    // look for app-specific settings on macOS anyway).
    NSMenuItem *app_item = [[NSMenuItem alloc] initWithTitle:@""
                                                      action:nil
                                               keyEquivalent:@""];
    NSMenu *app_sub = [[NSMenu alloc] initWithTitle:@"Bandicoot"];

    NSMenuItem *customize = [[NSMenuItem alloc]
                             initWithTitle:@"Customize Toolbar…"
                                    action:@selector(bandicootCustomizeToolbar:)
                             keyEquivalent:@""];
    [customize setTarget:[BandicootMenuTarget shared]];
    [app_sub addItem:customize];

    // "Exit Bandicoot" with a separator above it. Confirms via an
    // "Exit Bandicoot?" Yes/No alert before the clean exit (bandicootExit:).
    [app_sub addItem:[NSMenuItem separatorItem]];
    NSMenuItem *exit_item = [[NSMenuItem alloc]
                             initWithTitle:@"Exit Bandicoot"
                                    action:@selector(bandicootExit:)
                             keyEquivalent:@""];
    [exit_item setTarget:[BandicootMenuTarget shared]];
    [app_sub addItem:exit_item];

    [app_item setSubmenu:app_sub];
    [main_menu addItem:app_item];

    GList *children = gtk_container_get_children(GTK_CONTAINER(menubar));
    for (GList *l = children; l; l = l->next) {
        if (!GTK_IS_MENU_ITEM(l->data)) continue;
        [main_menu addItem:build_item(GTK_MENU_ITEM(l->data))];
    }
    g_list_free(children);

    [NSApp setMainMenu:main_menu];

    // Intercept the Dock icon's "Quit" (kAEQuitApplication). Registered here,
    // after gtk_init has finished launching NSApp, so this handler replaces
    // GDK-Quartz's default one (whose terminate path doesn't stop gtk_main).
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:[BandicootMenuTarget shared]
            andSelector:@selector(bandicootHandleQuitEvent:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEQuitApplication];

    gtk_widget_set_no_show_all(menubar, TRUE);
    gtk_widget_hide(menubar);
}

// Set the macOS Dock icon (and the app's icon image generally). For an
// unbundled binary the Dock otherwise shows the stock unix-executable
// (Terminal) icon. png_path should point to a square PNG; no-op if it can't
// be loaded.
extern "C" void bandicoot_set_dock_icon(const char *png_path) {
    if (!png_path || !*png_path) return;
    NSString *p = [NSString stringWithUTF8String:png_path];
    NSImage *img = [[NSImage alloc] initWithContentsOfFile:p];
    if (img) [NSApp setApplicationIconImage:img];
}

// ----- NSToolbar shim --------------------------------------------------------
//
// Bandicoot mirrors Coot's GtkToolbar (and selected items from the side
// model_toolbar) into a native NSToolbar attached to the main NSWindow.
// Customization is enabled — users can right-click the toolbar (or use
// View → Customize Toolbar…) to drag/drop items from a palette. macOS
// persists the user's layout in NSUserDefaults via the toolbar's
// "bandicoot.main" identifier, so the choice survives across launches.
//
// The "allowed items" palette contains every GtkToolButton from both the
// main toolbar and the sidebar's model toolbar, plus a few Bandicoot-
// specific extras (Auto-open MTZ, Sphere Refine, …). Items are keyed by
// their GtkToolButton widget name (e.g. "model_toolbar_refine_togglebutton")
// so identifiers are stable across builds.

// ---- Coot bridge declarations
extern "C" void on_auto_open_mtz_activate(GtkMenuItem *menuitem, gpointer user_data);

// ---- Bandicoot-specific actions (run as g_idle_add callbacks)

// Auto-open MTZ: forward to Coot's existing File-menu handler.
static gboolean bandicoot_action_auto_open_mtz(gpointer data) {
    on_auto_open_mtz_activate(NULL, NULL);
    return G_SOURCE_REMOVE;
}

// Quicksave: write the currently-focused molecule to
// <basename>-quicksave<ext> next to the file it was loaded from.
// Overwrites on each press, so a model can be checkpointed without
// filling a directory with N-numbered auto-saves. Bandicoot-only;
// doesn't exist in upstream Coot 0.9.
//
// Picking the molecule: Coot's Go-To-Atom dialog stores its currently-
// focused molecule and that's the most reliable signal of "the one the
// user is editing" — they navigated there to make edits. Fall back to
// first_coords_imol() if go-to-atom hasn't been used yet (which is also
// what the original Quicksave did, so behaviour for fresh sessions is
// unchanged).
static gboolean bandicoot_action_quicksave(gpointer data) {
    int imol = go_to_atom_molecule_number();
    if (imol < 0 || !is_valid_model_molecule(imol)) {
        imol = first_coords_imol();
    }
    if (imol < 0 || !is_valid_model_molecule(imol)) {
        fprintf(stdout, "[bandicoot] Quicksave: no model molecule loaded\n");
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }
    const char *name = molecule_name(imol);
    if (!name || !*name) {
        fprintf(stdout, "[bandicoot] Quicksave: imol %d has no filename\n", imol);
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    // Split off the extension. Use the last '.' in the BASENAME so a
    // dotted directory like /home/.coot/foo doesn't fool us.
    std::string n(name);
    size_t slash = n.find_last_of('/');
    size_t dot   = n.find_last_of('.');
    std::string base, ext;
    if (dot != std::string::npos &&
        (slash == std::string::npos || dot > slash)) {
        base = n.substr(0, dot);
        ext  = n.substr(dot);   // includes leading '.'
    } else {
        base = n;
        ext  = ".pdb";
    }
    // If base already ends with "-quicksave", strip it — otherwise each
    // press cascades the suffix (foo-quicksave-quicksave-quicksave.pdb).
    // save_coordinates() updates the molecule's name to the saved path,
    // so molecule_name() on the next press returns the quicksave path.
    static const std::string qs_suffix = "-quicksave";
    if (base.size() >= qs_suffix.size() &&
        base.compare(base.size() - qs_suffix.size(),
                     qs_suffix.size(), qs_suffix) == 0) {
        base.resize(base.size() - qs_suffix.size());
    }
    std::string qs = base + qs_suffix + ext;

    // save_coordinates() return value is unreliable on Coot 0.9 — the
    // header comment says 1=success/0=fail but in practice it returns 0
    // even on successful writes. stat the file after to determine truth.
    (void)save_coordinates(imol, qs.c_str());
    struct stat st;
    if (stat(qs.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        fprintf(stdout, "[bandicoot] Quicksave: wrote %s\n", qs.c_str());
    } else {
        fprintf(stdout, "[bandicoot] Quicksave: write failed for %s\n", qs.c_str());
    }
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

// ---- Toggle actions (5 of the simpler Python-replaced items)
//
// Each one wraps a Coot C-interface call with primitive args. State for
// the on/off toggles lives in C statics; each click flips and applies.
// Replaces the previously-shelved Python equivalents — these don't need
// Python or the SWIG layer.

// Toggle "apply" functions. Each takes desired state via the gpointer
// (1 = on, 0 = off) and applies it to Coot. The visual on/off state
// lives on the NSButton subview these are wired through — see
// BandicootToggleTarget below. Cleaner than keeping a parallel C
// static, since the button is the user-visible source of truth.

static gboolean bandicoot_action_backrub_apply(gpointer data) {
    // ROTAMERSEARCHLOWRES = 2 is "backrub"; ROTAMERSEARCHAUTOMATIC = 0
    // is Coot's default per-residue selection. See rotamer-search-modes.hh.
    int on = GPOINTER_TO_INT(data);
    set_rotamer_search_mode(on ? 2 : 0);
    fprintf(stdout, "[bandicoot] Backrub rotamers: %s\n", on ? "on" : "off");
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

static gboolean bandicoot_action_hydrogen_apply(gpointer data) {
    int on = GPOINTER_TO_INT(data);
    int n = graphics_n_molecules();
    for (int i = 0; i < n; ++i) {
        if (is_valid_model_molecule(i)) set_draw_hydrogens(i, on);
    }
    fprintf(stdout, "[bandicoot] Hydrogens: %s\n", on ? "on" : "off");
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

static gboolean bandicoot_action_full_screen_apply(gpointer data) {
    int on = GPOINTER_TO_INT(data);
    full_screen(on);
    fprintf(stdout, "[bandicoot] Full screen: %s\n", on ? "on" : "off");
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

// Interactive Dots: enable/disable Coot's pure-C++ probe-dots overlay
// across every refinement / rotamer-cycle / chi-edit interaction.
//
// Three flags work together:
//   - do_coot_probe_dots_during_refine_flag: live preview dots while
//     dragging atoms during real-space refine (rendered into
//     "Intermediate Atoms <type>" objects, vanish when intermediate
//     atoms are torn down on accept/reject).
//   - do_probe_dots_post_refine_flag: triggers Bandicoot's post-accept
//     hook (graphics-info.cc) which routes through setup_for_probe_
//     dots_on_chis_molprobity → do_probe_dots_on_rotamers_and_chis
//     (Bandicoot funnel) → bandicoot_render_local_post_refine_dots.
//     Persistent "Molecule N: <type>" dots scoped to the refined region.
//   - do_probe_dots_on_rotamers_and_chis_flag: same funnel, triggered
//     by rotamer cycling (Rotamers dialog) and chi-angle editing (Edit
//     Chi Angles or mouse-drag chi rotation).
//
// All paths use Coot's C++ atom_overlaps_container_t — NOT the guile-
// gtk-gated do_interactive_probe() (dead in this build).
static gboolean bandicoot_action_interactive_dots_apply(gpointer data) {
    int on = GPOINTER_TO_INT(data);
    set_do_coot_probe_dots_during_refine(on);
    set_do_probe_dots_post_refine(on);
    set_do_probe_dots_on_rotamers_and_chis(on);
    // Toggling OFF clears the currently-drawn dots (both Local Probe
    // Dots and Interactive Dots families). This is Art's longstanding
    // wishlist item — upstream Coot has no clear-dots UI, just toggles
    // that affect FUTURE refinements but leave current dots on screen.
    // Toggling ON does nothing extra; new dots arrive on the next
    // refinement / rotamer cycle.
    if (!on) {
        bandicoot_clear_probe_dot_objects();
        graphics_draw();
    }
    fprintf(stdout, "[bandicoot] Interactive Dots: %s (refine+rotamer+chi%s)\n",
            on ? "on" : "off", on ? "" : "; cleared existing dots");
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

// Cis ↔ Trans isn't really a toggle — it enters Coot's interactive
// peptide-flip pick mode. Calling with state=1 turns the pick mode on;
// the user then clicks an atom and Coot performs the conversion + exits
// the mode automatically. Each press of the toolbar button re-arms the
// mode for one more flip.
static gboolean bandicoot_action_cis_trans(gpointer data) {
    do_cis_trans_conversion_setup(1);
    fprintf(stdout, "[bandicoot] Cis↔Trans: click an atom to flip\n");
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

// ---- Dispatch idle-callbacks (run after AppKit's event-tracking loop)

static gboolean bandicoot_fire_tool_clicked(gpointer data) {
    GtkToolButton *tb = (GtkToolButton *)data;
    if (!tb || !GTK_IS_TOOL_BUTTON(tb)) return G_SOURCE_REMOVE;

    // GtkToggleToolButton subclasses (Real Space Refine Zone, Regularize
    // Zone, Auto Fit Rotamer, etc. — most of the sidebar refinement
    // tools) have their callbacks wired to the "toggled" signal, not
    // "clicked". Emitting "clicked" leaves the active state unchanged
    // and "toggled" never fires. Flip the active state instead — GTK
    // emits "toggled" as a side effect and the connected handler runs.
    //
    // GtkRadioToolButton is also a subclass of GtkToggleToolButton, so
    // this check covers both. GtkMenuToolButton is a regular
    // GtkToolButton subclass and responds correctly to "clicked".
    if (GTK_IS_TOGGLE_TOOL_BUTTON(tb)) {
        GtkToggleToolButton *ttb = GTK_TOGGLE_TOOL_BUTTON(tb);
        gboolean cur = gtk_toggle_tool_button_get_active(ttb);
        gtk_toggle_tool_button_set_active(ttb, !cur);
    } else {
        g_signal_emit_by_name(tb, "clicked");
    }
    return G_SOURCE_REMOVE;
}

// Python toolbar-item dispatch was attempted in v0.0.0.2 development
// and shelved. Background: Coot 0.9's C interface uses Python 2 API
// (PyString_FromString, etc.) and won't compile against Python 3
// without a port. Without USE_PYTHON defined, safe_python_command_*
// are no-ops, so any toolbar item that tried to dispatch a Python
// expression would do nothing. The Python-dispatched extras have
// been removed from BANDICOOT_EXTRAS until Coot's C interface gets
// a py3 port (a separate piece of work).
#if 0  // disabled — see comment above
static const char *bandicoot_py3_extras_src =
"# Bandicoot py3 replacements for the (broken-on-py3) Coot Python suite.\n"
"# Defined once on first Python dispatch from a Bandicoot toolbar item.\n"
"# Coot 0.9's `coot_load_modules.py` uses Python-2 syntax (execfile, etc.)\n"
"# and crashes on first run under py3, so the alias file\n"
"# `redefine_functions.py` that normally maps active_residue_py →\n"
"# active_residue (and similar) is never loaded. Call the *_py SWIG names\n"
"# directly here.\n"
"def _bc_aa_check(label):\n"
"    aa = active_residue_py()\n"
"    if not aa:\n"
"        print('[bandicoot] %s: no active residue; click an atom first' % label)\n"
"        return None\n"
"    return aa\n"
"\n"
"def _bc_sphere_generic(use_map, radius, expand):\n"
"    aa = _bc_aa_check('Sphere refine/regularize')\n"
"    if aa is None: return\n"
"    imol, ch, rn, ins, _, _ = aa\n"
"    central = [ch, rn, ins]\n"
"    near = residues_near_residue_py(imol, central, radius) or []\n"
"    seen = {(ch, rn, ins)}\n"
"    rs = [central]\n"
"    for r in near:\n"
"        t = tuple(r[:3])\n"
"        if t not in seen:\n"
"            seen.add(t); rs.append(r[:3])\n"
"    if expand:\n"
"        for dr in (-1, 1):\n"
"            nr = (ch, rn+dr, ins)\n"
"            if nr not in seen:\n"
"                seen.add(nr); rs.append([ch, rn+dr, ins])\n"
"    if use_map:\n"
"        refine_residues_py(imol, rs)\n"
"    else:\n"
"        regularize_residues_py(imol, rs)\n"
"    print('[bandicoot] %s refine/regularize: %d residues around %s/%d' % \\\n"
"          ('Map-restrained' if use_map else 'Geometry-only', len(rs), ch, rn))\n"
"\n"
"def sphere_refine(radius=4.5):        _bc_sphere_generic(True,  radius, False)\n"
"def sphere_refine_plus(radius=4.5):   _bc_sphere_generic(True,  radius, True)\n"
"def sphere_regularize(radius=4.5):    _bc_sphere_generic(False, radius, False)\n"
"def sphere_regularize_plus(radius=4.5): _bc_sphere_generic(False, radius, True)\n"
"\n"
"def refine_tandem_residues():\n"
"    aa = _bc_aa_check('Tandem Refine')\n"
"    if aa is None: return\n"
"    imol, ch, rn, ins, _, _ = aa\n"
"    rs = [[ch, rn+i, ins] for i in range(-3, 4)]\n"
"    refine_residues_py(imol, rs)\n"
"    print('[bandicoot] Tandem Refine: %s/%d-%d' % (ch, rn-3, rn+3))\n"
"\n"
"# Toggle state lives in module globals so each click flips it.\n"
"_bc_backrub_on = False\n"
"def toggle_backrub_rotamers():\n"
"    global _bc_backrub_on\n"
"    _bc_backrub_on = not _bc_backrub_on\n"
"    set_rotamer_search_mode(1 if _bc_backrub_on else 0)\n"
"    print('[bandicoot] Backrub rotamers:', 'on' if _bc_backrub_on else 'off')\n"
"\n"
"_bc_probe_on = False\n"
"def toggle_interactive_probe_dots():\n"
"    global _bc_probe_on\n"
"    _bc_probe_on = not _bc_probe_on\n"
"    s = 1 if _bc_probe_on else 0\n"
"    set_do_probe_dots_on_rotamers_and_chis(s)\n"
"    set_do_probe_dots_post_refine(s)\n"
"    print('[bandicoot] Interactive probe dots:', 'on' if _bc_probe_on else 'off')\n"
"\n"
"_bc_hydrogens_on = True\n"
"def toggle_hydrogen_display():\n"
"    global _bc_hydrogens_on\n"
"    _bc_hydrogens_on = not _bc_hydrogens_on\n"
"    s = 1 if _bc_hydrogens_on else 0\n"
"    for i in range(graphics_n_molecules()):\n"
"        if is_valid_model_molecule(i):\n"
"            set_draw_hydrogens(i, s)\n"
"    print('[bandicoot] Hydrogens:', 'on' if _bc_hydrogens_on else 'off')\n"
"\n"
"_bc_fullscreen_on = False\n"
"def toggle_full_screen():\n"
"    global _bc_fullscreen_on\n"
"    _bc_fullscreen_on = not _bc_fullscreen_on\n"
"    full_screen(1 if _bc_fullscreen_on else 0)\n";

static gboolean bandicoot_fire_python_command(gpointer data) {
    char *cmd = (char *)data;
    fprintf(stdout, "[bandicoot-dispatch] python cmd: %s\n",
            cmd ? cmd : "(null)");
    fflush(stdout);
    if (cmd) {
        // Trivial GIL sanity test — confirms PyRun executes Python at
        // all in this dispatch context. If we don't see "GIL TEST" in
        // the terminal, even the GIL-acquiring wrapper isn't enough
        // (Python interpreter dead, fd 1 redirected, etc).
        fprintf(stdout, "[bandicoot-dispatch] GIL sanity test\n");
        fflush(stdout);
        bandicoot_run_python(
            "import os; os.write(1, b'[bandicoot-py] GIL TEST OK\\n')");
        fprintf(stdout, "[bandicoot-dispatch] sanity test returned\n");
        fflush(stdout);

        // Lazy-load the py3 replacement module on first Python dispatch.
        static gboolean inited = FALSE;
        if (!inited) {
            fprintf(stdout, "[bandicoot-dispatch] running py3 extras init\n");
            fflush(stdout);
            bandicoot_run_python(bandicoot_py3_extras_src);
            fprintf(stdout, "[bandicoot-dispatch] init returned, probing\n");
            fflush(stdout);
            // Probe via os.write so output bypasses sys.stdout (which
            // Coot's embedded interpreter redirects somewhere unseen).
            bandicoot_run_python(
                "import os\n"
                "def _bcw(s):\n"
                "    try: os.write(1, (s + '\\n').encode('utf-8'))\n"
                "    except Exception: pass\n"
                "_bcw('[bandicoot-init] sphere_refine defined: ' + str('sphere_refine' in dir()))\n"
                "_bcw('[bandicoot-init] active_residue_py in scope: ' + str('active_residue_py' in dir()))\n"
                "_bcw('[bandicoot-init] refine_residues_py in scope: ' + str('refine_residues_py' in dir()))\n");
            fprintf(stdout, "[bandicoot-dispatch] probe returned\n");
            fflush(stdout);
            inited = TRUE;
        }
        // Wrap the user command so any exception traceback is captured
        // and written to fd 1 directly (bypassing sys.stderr too).
        size_t n = strlen(cmd) + 512;
        char *wrapped = (char *)g_malloc(n);
        snprintf(wrapped, n,
                 "import os, traceback\n"
                 "try:\n"
                 "    %s\n"
                 "    os.write(1, b'[bandicoot-dispatch] python cmd ok\\n')\n"
                 "except Exception:\n"
                 "    os.write(1, b'[bandicoot-dispatch] python cmd raised:\\n')\n"
                 "    os.write(1, traceback.format_exc().encode('utf-8'))\n",
                 cmd);
        fprintf(stdout, "[bandicoot-dispatch] running wrapped cmd\n");
        fflush(stdout);
        bandicoot_run_python(wrapped);
        fprintf(stdout, "[bandicoot-dispatch] wrapped cmd returned\n");
        fflush(stdout);
        g_free(wrapped);
        g_free(cmd);
    }
    return G_SOURCE_REMOVE;
}
#endif  // 0 — Python dispatch disabled until Coot's py3 port

// ---- Toolbar item dispatch target
//
// One target object handles every NSToolbarItem. Each item carries one of
// three associated-object slots that select its dispatch mechanism:
//   kCCallbackKey  → an NSValue wrapping a void(*)(gpointer) (g_idle_add fn)
//   kPythonCmdKey  → an NSString with a Python expression to eval
//   kGtkButtonKey  → an NSValue wrapping the GtkToolButton * to "click"

static char kGtkButtonKey;
static char kPythonCmdKey;
static char kCCallbackKey;
static char kToggleApplyKey;   // associated object on NSButton (toggle subview):
                               // NSValue wrapping a GSourceFunc that takes
                               // GPOINTER_TO_INT(state).

// Tint a push-on toggle button's bezel: dark grey when on (a calmer, more
// "recessed" look than the system-accent blue), default when off.
static void bandicoot_apply_toggle_bezel(NSButton *btn, int on) {
    if (!btn) return;
    btn.bezelColor = on ? [NSColor colorWithCalibratedWhite:0.42 alpha:1.0] : nil;
}

// ---- Toggle button target
//
// Used for NSToolbarItems whose `view` is an NSButton configured as
// NSButtonTypePushOnPushOff. AppKit auto-flips the button's `state` on
// click; this target reads the new state and dispatches it (as an int
// payload) to the registered C "apply" function via g_idle_add.
//
// One target instance handles every toggle button — discrimination is by
// the associated-object lookup on `sender`.

@interface BandicootToggleTarget : NSObject
+ (instancetype)shared;
- (void)toggle:(NSButton *)sender;
@end

@implementation BandicootToggleTarget
+ (instancetype)shared {
    static BandicootToggleTarget *s;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [BandicootToggleTarget new]; });
    return s;
}
- (void)toggle:(NSButton *)sender {
    NSValue *box = objc_getAssociatedObject(sender, &kToggleApplyKey);
    GSourceFunc fn = box ? (GSourceFunc)[box pointerValue] : NULL;
    if (!fn) return;
    int on = (sender.state == NSControlStateValueOn) ? 1 : 0;
    // Tint the "on" bezel dark grey instead of the garish system-accent blue.
    bandicoot_apply_toggle_bezel(sender, on);
    g_idle_add(fn, GINT_TO_POINTER(on));
}
@end

@interface BandicootToolbarTarget : NSObject
+ (instancetype)shared;
- (void)dispatch:(NSToolbarItem *)sender;
@end

@implementation BandicootToolbarTarget
+ (instancetype)shared {
    static BandicootToolbarTarget *s;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [BandicootToolbarTarget new]; });
    return s;
}
- (void)dispatch:(NSToolbarItem *)sender {
    // C-callback items (Bandicoot extras like Quicksave / Auto-open MTZ)
    NSValue *cb = objc_getAssociatedObject(sender, &kCCallbackKey);
    if (cb) {
        GSourceFunc fn = (GSourceFunc)[cb pointerValue];
        if (fn) g_idle_add(fn, NULL);
        return;
    }
    // Python-dispatched items are not currently supported (USE_PYTHON
    // disabled in build.sh — see comment there). kPythonCmdKey is left
    // in the dispatch table for when Coot's Python interface gets a
    // py3 port.
    // Default: forward to the wrapped GtkToolButton's "clicked" signal
    NSValue *boxed = objc_getAssociatedObject(sender, &kGtkButtonKey);
    GtkToolButton *tb = (GtkToolButton *)[boxed pointerValue];
    if (tb && GTK_IS_TOOL_BUTTON(tb)) {
        g_idle_add(bandicoot_fire_tool_clicked, tb);
    }
}
@end

// ---- NSToolbarDelegate
//
// Holds the full palette ("allowed"), the initial visible set ("default"),
// and a dictionary mapping each identifier to a fully-built NSToolbarItem.

@interface BandicootToolbarDelegate : NSObject <NSToolbarDelegate>
@property (nonatomic, strong) NSMutableArray<NSString *> *defaultIdentifiers;
@property (nonatomic, strong) NSMutableArray<NSString *> *allowedIdentifiers;
@property (nonatomic, strong) NSMutableDictionary<NSString *, NSToolbarItem *> *itemsById;
@end

@implementation BandicootToolbarDelegate
- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)tb {
    return _defaultIdentifiers;
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)tb {
    return _allowedIdentifiers;
}
- (NSToolbarItem *)toolbar:(NSToolbar *)tb
     itemForItemIdentifier:(NSToolbarItemIdentifier)ident
 willBeInsertedIntoToolbar:(BOOL)flag {
    return _itemsById[ident];
}
// Lock the first five default items so they can't be dragged out or
// reordered in the customize sheet. macOS 13+ (Sonoma) added this
// delegate method specifically for "non-removable" toolbar items.
- (NSSet<NSToolbarItemIdentifier> *)toolbarImmovableItemIdentifiers:(NSToolbar *)tb
    API_AVAILABLE(macos(13.0))
{
    return [NSSet setWithArray:@[
        @"bandicoot.main.0",                // Open Coords
        @"bandicoot.extra.auto_open_mtz",
        @"bandicoot.extra.quicksave",
        @"bandicoot.main.2",                // Display Manager
        @"bandicoot.main.3",                // Go to Atom
    ]];
}
@end

// Convert a GdkPixbuf into an NSImage. Caller takes ownership of returned NSImage.
// Returns nil on failure.
static NSImage *ns_image_from_pixbuf(GdkPixbuf *pixbuf) {
    if (!pixbuf) return nil;
    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    int rs = gdk_pixbuf_get_rowstride(pixbuf);
    gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    guint8 *pixels = gdk_pixbuf_get_pixels(pixbuf);

    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                      pixelsWide:w
                      pixelsHigh:h
                   bitsPerSample:8
                 samplesPerPixel:(has_alpha ? 4 : 3)
                        hasAlpha:has_alpha
                        isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace
                     bytesPerRow:rs
                    bitsPerPixel:0];
    if (!rep) return nil;
    memcpy([rep bitmapData], pixels, h * rs);

    NSImage *img = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
    [img addRepresentation:rep];
    return img;
}

// Try hard to extract a usable NSImage from a tool button's icon widget.
// gtk_image_get_pixbuf returns undefined data if the image's storage type
// isn't GTK_IMAGE_PIXBUF or GTK_IMAGE_EMPTY, so we must gate on storage type
// or we'll crash inside gdk_pixbuf_get_width.
static NSImage *image_for_tool_button(GtkToolButton *tb) {
    GtkWidget *icon = gtk_tool_button_get_icon_widget(tb);
    if (!icon || !GTK_IS_IMAGE(icon)) return nil;

    GtkImage *gimg = GTK_IMAGE(icon);
    GtkImageType st = gtk_image_get_storage_type(gimg);

    if (st == GTK_IMAGE_PIXBUF) {
        GdkPixbuf *pb = gtk_image_get_pixbuf(gimg);
        if (pb) return ns_image_from_pixbuf(pb);
        return nil;
    }
    if (st == GTK_IMAGE_STOCK) {
        const gchar *stock_id = NULL;
        gtk_image_get_stock(gimg, (gchar **)&stock_id, NULL);
        if (stock_id) {
            // Render the stock icon at large toolbar size through the
            // widget's style context so we get the correct theme rendering.
            GdkPixbuf *pb = gtk_widget_render_icon(GTK_WIDGET(tb), stock_id,
                                                  GTK_ICON_SIZE_LARGE_TOOLBAR,
                                                  NULL);
            if (pb) {
                NSImage *img = ns_image_from_pixbuf(pb);
                g_object_unref(pb);
                return img;
            }
        }
        return nil;
    }
    if (st == GTK_IMAGE_ICON_NAME) {
        const gchar *name = NULL;
        gtk_image_get_icon_name(gimg, &name, NULL);
        if (name) {
            GtkIconTheme *theme = gtk_icon_theme_get_default();
            GdkPixbuf *pb = gtk_icon_theme_load_icon(theme, name, 24,
                                                    GTK_ICON_LOOKUP_USE_BUILTIN,
                                                    NULL);
            if (pb) {
                NSImage *img = ns_image_from_pixbuf(pb);
                g_object_unref(pb);
                return img;
            }
        }
        return nil;
    }
    // Animation, gicon, etc. -- not handled.
    return nil;
}

// Build one NSToolbarItem from a GtkToolButton at a known position in its
// parent toolbar and add it to the delegate's palette catalog. The ident
// is `bandicoot.<prefix>.<index>` — the index comes from the child's
// position in the Glade-XML-defined toolbar, which is stable across
// builds. (gtk_widget_get_name() can't be used as the ID base: libglade
// stores Coot's widget names as g_object_data on the toplevel rather than
// calling gtk_widget_set_name, so gtk_widget_get_name() returns the class
// name "GtkToolButton" for every tool button — and we'd dedup them all.)
static NSString *catalog_gtk_tool_button(GtkToolButton *tb,
                                         NSString *ident_prefix,
                                         int index,
                                         BandicootToolbarDelegate *delegate) {
    if (!tb || !GTK_IS_TOOL_BUTTON(tb)) return nil;

    NSString *ident = [NSString stringWithFormat:@"bandicoot.%@.%d",
                       ident_prefix, index];

    const char *label_c = gtk_tool_button_get_label(tb);
    NSString *label = (label_c && *label_c)
                      ? [NSString stringWithUTF8String:label_c]
                      : ident;

    NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
    [item setLabel:label];
    [item setPaletteLabel:label];
    [item setTarget:[BandicootToolbarTarget shared]];
    [item setAction:@selector(dispatch:)];

    NSImage *img = image_for_tool_button(tb);
    if (img) [item setImage:img];

    if (!gtk_widget_is_sensitive(GTK_WIDGET(tb))) [item setEnabled:NO];

    objc_setAssociatedObject(item, &kGtkButtonKey,
                             [NSValue valueWithPointer:tb],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    [delegate.allowedIdentifiers addObject:ident];
    delegate.itemsById[ident] = item;
    return ident;
}

// Walk a GtkToolbar's children, cataloguing every GtkToolButton found.
// Returns an array of the identifiers in toolbar order so the caller can
// use it to construct the "default visible" set when needed.
static NSArray<NSString *> *catalog_gtk_toolbar(GtkWidget *gtk_toolbar,
                                                NSString *ident_prefix,
                                                BandicootToolbarDelegate *delegate) {
    NSMutableArray *order = [NSMutableArray new];
    if (!gtk_toolbar || !GTK_IS_TOOLBAR(gtk_toolbar)) return order;
    GList *children = gtk_container_get_children(GTK_CONTAINER(gtk_toolbar));
    int index = 0;
    for (GList *l = children; l; l = l->next) {
        GtkWidget *child = GTK_WIDGET(l->data);
        if (!GTK_IS_TOOL_BUTTON(child)) continue;
        NSString *ident = catalog_gtk_tool_button(GTK_TOOL_BUTTON(child),
                                                  ident_prefix, index++, delegate);
        if (ident) [order addObject:ident];
    }
    g_list_free(children);
    return order;
}

// Look up an icon file under $COOT_PIXMAPS_DIR / $COOT_DATA_DIR/pixmaps,
// fall back to nil if the file doesn't exist. Sets the NSImage's `size`
// to a toolbar-sized 24×24 so it lays out at the same scale as items
// rendered from GtkImage via GTK_ICON_SIZE_LARGE_TOOLBAR. (NSImage's
// underlying bitmap is unchanged — `size` is the intended-display size
// the toolbar uses for layout; AppKit scales at draw time.)
static NSImage *image_from_pixmaps_dir(const char *basename) {
    if (!basename || !*basename) return nil;
    const char *dir = getenv("COOT_PIXMAPS_DIR");
    char path[1024];
    if (dir && *dir) {
        snprintf(path, sizeof(path), "%s/%s", dir, basename);
    } else {
        const char *data = getenv("COOT_DATA_DIR");
        if (!data || !*data) return nil;
        snprintf(path, sizeof(path), "%s/pixmaps/%s", data, basename);
    }
    NSString *p = [NSString stringWithUTF8String:path];
    if (![[NSFileManager defaultManager] fileExistsAtPath:p]) return nil;
    NSImage *img = [[NSImage alloc] initWithContentsOfFile:p];
    if (img) [img setSize:NSMakeSize(24, 24)];
    return img;
}

// Table-driven catalog of Bandicoot-specific tool buttons. Each row is
// one toolbar item with exactly one dispatch mechanism: a C callback
// pointer OR a Python expression string. Most rows are sourced from
// python/coot_toolbuttons.py's list_of_toolbar_functions() — that lists
// everything Coot's old "Toolbar Selection" dialog could add. We
// hardcode the subset that's genuinely useful, since Python isn't
// initialised yet at toolbar-install time.
//
// Items with c_callback set bypass Python and run as a g_idle_add
// callback (matches the dispatch pattern of the rest of bandicoot_appkit).
struct bandicoot_extra {
    const char *ident_suffix;     // appended to "bandicoot.extra."
    const char *label;
    GSourceFunc c_callback;       // non-NULL → call directly; else python_cmd
    const char *python_cmd;       // ignored when c_callback != NULL
    const char *icon_basename;    // file under share/coot/pixmaps/
    // For toggle items: c_callback above goes UNUSED; instead the
    // NSToolbarItem gets a view=NSButton(PushOnPushOff) whose target
    // dispatches `toggle_apply` with the new state via GPOINTER_TO_INT.
    // is_toggle_initial is the button's starting state (0=off, 1=on)
    // — set to 1 for Toggle Hydrogens since Coot starts with hydrogens
    // visible.
    GSourceFunc toggle_apply;     // non-NULL → render as NSButton toggle
    int toggle_initial;
};

static const struct bandicoot_extra BANDICOOT_EXTRAS[] = {
    // File
    {"auto_open_mtz", "Auto-open MTZ", bandicoot_action_auto_open_mtz, NULL, NULL,             NULL, 0},
    {"quicksave",     "Quicksave",     bandicoot_action_quicksave,     NULL, "coot-save.png",  NULL, 0},

    // Refinement (C++ implementations live in bandicoot_refine.cc — they
    // use Coot's C++ active-atom / neighbor-finding / refine APIs)
    {"sphere_refine",         "Sphere Refine",         bandicoot_action_sphere_refine,         NULL, "refine-sphere-1.png",     NULL, 0},
    {"sphere_refine_plus",    "Sphere Refine +",       bandicoot_action_sphere_refine_plus,    NULL, "refine-sphere-1.png",     NULL, 0},
    {"refine_tandem",         "Tandem Refine",         bandicoot_action_refine_tandem,         NULL, "refine-tandem-1.png",     NULL, 0},
    {"sphere_regularize",     "Sphere Regularize",     bandicoot_action_sphere_regularize,     NULL, "regularize-sphere-1.png", NULL, 0},
    {"sphere_regularize_plus","Sphere Regularize +",   bandicoot_action_sphere_regularize_plus,NULL, "regularize-sphere-1.png", NULL, 0},
    {"cis_trans",             "Cis ↔ Trans",           bandicoot_action_cis_trans,             NULL, "flip-peptide.svg",  NULL, 0},
    {"local_probe_dots",      "Local Probe Dots",      bandicoot_action_local_probe_dots,      NULL, "probe-clash.svg",   NULL, 0},

    // Toggles — rendered as NSButton subviews so AppKit shows their
    // on/off state visually. c_callback unused; the NSButton's target
    // dispatches toggle_apply with the new state on each press.
    {"backrub_toggle",        "Backrub Rotamers",  NULL, NULL, "auto-fit-rotamer.svg", bandicoot_action_backrub_apply,           0},
    {"hydrogen_toggle",       "Toggle Hydrogens",  NULL, NULL, "delete.svg",           bandicoot_action_hydrogen_apply,          1},
    {"full_screen_toggle",    "Full Screen",       NULL, NULL, "reset-view-32.svg",    bandicoot_action_full_screen_apply,       0},
    {"interactive_dots_toggle","Interactive Dots", NULL, NULL, "probe-clash.svg",      bandicoot_action_interactive_dots_apply,  0},

    // Interactive Dots — uses Coot's INTERNAL probe (do_interactive_coot_
    //   probe / atom_overlaps_container_t), not the guile-gtk-gated
    //   do_interactive_probe() path. Refinement timeout already invokes
    //   it when the flag is on; we just toggle the flag.
};
static const size_t BANDICOOT_EXTRAS_COUNT =
    sizeof(BANDICOOT_EXTRAS) / sizeof(BANDICOOT_EXTRAS[0]);

// Add Bandicoot-specific extras to the catalog: items that aren't backed
// by an existing GtkToolButton in either toolbar. Each row in
// BANDICOOT_EXTRAS becomes one NSToolbarItem dispatched via either a
// C callback or a Python expression.
static void catalog_bandicoot_extras(BandicootToolbarDelegate *delegate,
                                     NSImage *fallback_icon) {
    for (size_t i = 0; i < BANDICOOT_EXTRAS_COUNT; ++i) {
        const struct bandicoot_extra *e = &BANDICOOT_EXTRAS[i];
        NSString *ident = [NSString stringWithFormat:@"bandicoot.extra.%s",
                           e->ident_suffix];
        NSString *label = [NSString stringWithUTF8String:e->label];
        NSImage *icon = image_from_pixmaps_dir(e->icon_basename);
        if (!icon) icon = fallback_icon;

        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
        [item setLabel:label];
        [item setPaletteLabel:label];

        if (e->toggle_apply) {
            // Toggle items: render as an NSButton subview so AppKit
            // shows the on/off state visually (NSButtonTypePushOnPushOff
            // gives the standard bordered "pressed" look when on). The
            // button's `state` is the truth — the apply function reads
            // it via BandicootToggleTarget.
            NSRect rect = NSMakeRect(0, 0, 40, 32);
            NSButton *btn = [[NSButton alloc] initWithFrame:rect];
            [btn setButtonType:NSButtonTypePushOnPushOff];
            [btn setBezelStyle:NSBezelStyleRegularSquare];
            [btn setBordered:YES];
            [btn setTitle:@""];
            if (icon) [btn setImage:icon];
            [btn setImagePosition:NSImageOnly];
            [btn setState:e->toggle_initial ? NSControlStateValueOn
                                            : NSControlStateValueOff];
            bandicoot_apply_toggle_bezel(btn, e->toggle_initial);
            [btn setTarget:[BandicootToggleTarget shared]];
            [btn setAction:@selector(toggle:)];
            // Stash the apply function so the toggle target can find it.
            objc_setAssociatedObject(btn, &kToggleApplyKey,
                                     [NSValue valueWithPointer:(void *)e->toggle_apply],
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            [item setView:btn];
            // Setting both view and image is harmless; the view wins
            // for display, but the image-only fallback in the palette
            // (item summary) still wants something.
            if (icon) [item setImage:icon];
        } else {
            // Regular click-once items: standard NSToolbarItem with
            // image + target/action dispatch through BandicootToolbarTarget.
            [item setTarget:[BandicootToolbarTarget shared]];
            [item setAction:@selector(dispatch:)];
            if (icon) [item setImage:icon];

            if (e->c_callback) {
                objc_setAssociatedObject(item, &kCCallbackKey,
                                         [NSValue valueWithPointer:(void *)e->c_callback],
                                         OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            } else if (e->python_cmd) {
                objc_setAssociatedObject(item, &kPythonCmdKey,
                                         [NSString stringWithUTF8String:e->python_cmd],
                                         OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            }
        }

        [delegate.allowedIdentifiers addObject:ident];
        delegate.itemsById[ident] = item;
    }
}

extern "C" void bandicoot_install_native_toolbar(GtkWidget *gtk_toolbar,
                                                 GtkWidget *model_toolbar) {
    if (!gtk_toolbar || !GTK_IS_TOOLBAR(gtk_toolbar)) return;

    GtkWidget *toplevel = gtk_widget_get_toplevel(gtk_toolbar);
    if (!toplevel || !toplevel->window) return;
    NSWindow *win = (NSWindow *)gdk_quartz_window_get_nswindow(toplevel->window);
    if (!win) return;

    BandicootToolbarDelegate *delegate = [BandicootToolbarDelegate new];
    delegate.allowedIdentifiers = [NSMutableArray new];
    delegate.defaultIdentifiers = [NSMutableArray new];
    delegate.itemsById = [NSMutableDictionary new];

    // --- 1) Catalog the main toolbar (these will form the initial visible set)
    NSArray<NSString *> *main_idents = catalog_gtk_toolbar(gtk_toolbar,
                                                            @"main",
                                                            delegate);

    // --- 2) Catalog the model (side) toolbar so its buttons are available
    //         in the customize palette. They aren't shown by default.
    if (model_toolbar) {
        catalog_gtk_toolbar(model_toolbar, @"model", delegate);
    }

    // --- 3) Catalog Bandicoot-specific extras (Auto-open MTZ, Sphere Refine).
    //         Use the first main item's icon as a fallback for ones lacking
    //         their own.
    NSImage *fallback_icon = nil;
    if (main_idents.count > 0) {
        fallback_icon = [delegate.itemsById[main_idents[0]] image];
    }
    catalog_bandicoot_extras(delegate, fallback_icon);

    // --- 4) Default visible set, in user-requested order. The first
    //         five are also locked via toolbarImmovableItemIdentifiers:
    //         in the delegate — keep this list and the locked set in
    //         sync if either changes.
    //
    //         Hard-coded identifiers used here:
    //           bandicoot.main.0 = Open Coords     (from main_toolbar)
    //           bandicoot.main.2 = Display Manager (from main_toolbar)
    //           bandicoot.main.3 = Go to Atom      (from main_toolbar)
    //         Indices match Coot 0.9's frozen main_toolbar Glade layout
    //         — verified in the diagnostic build's catalog dump.
    [delegate.defaultIdentifiers addObjectsFromArray:@[
        @"bandicoot.main.0",                  // 1. Open Coords         (locked)
        @"bandicoot.extra.auto_open_mtz",     // 2. Auto-open MTZ       (locked)
        @"bandicoot.extra.quicksave",         // 3. Quicksave           (locked)
        @"bandicoot.main.2",                  // 4. Display Manager     (locked)
        @"bandicoot.main.3",                  // 5. Go to Atom          (locked)
        @"bandicoot.extra.sphere_refine",     // 6. Sphere Refine
        @"bandicoot.extra.refine_tandem",     // 7. Tandem Refine
        @"bandicoot.extra.local_probe_dots",  // 8. Local Probe Dots
        @"bandicoot.extra.hydrogen_toggle",   // 9. Toggle Hydrogens
    ]];

    // --- 5) Always allow the standard system identifiers (spaces / customize).
    [delegate.allowedIdentifiers addObject:NSToolbarSpaceItemIdentifier];
    [delegate.allowedIdentifiers addObject:NSToolbarFlexibleSpaceItemIdentifier];

    // v2 identifier — the v1 (`bandicoot.main`) NSUserDefaults slot has
    // stale item IDs from earlier builds where catalog_gtk_tool_button
    // used the widget's class name as the key. Bumping the identifier
    // is the simplest way to make those installs read a fresh
    // configuration instead of getting a toolbar populated with stale
    // identifiers that no longer resolve.
    // ---- Schema-version migration ----
    //
    // Bandicoot's toolbar identifier scheme has churned during v0.0.0.2
    // development (widget-class-name keys → indexed keys, etc.). When the
    // identifier scheme changes, any saved NSToolbar configuration in
    // NSUserDefaults from a prior version becomes invalid: macOS may
    // restore a stale layout that references identifiers the new code
    // doesn't recognise, producing a toolbar with mostly-missing items.
    //
    // To migrate cleanly we stamp a schema-version key into defaults each
    // time install_native_toolbar runs. If the saved version doesn't
    // match BANDICOOT_TOOLBAR_SCHEMA below, we delete every
    // "NSToolbar Configuration bandicoot.*" entry so the next NSToolbar
    // alloc starts fresh and applies our defaults. Bump the schema
    // integer whenever the catalog's identifier format changes.
    static const int BANDICOOT_TOOLBAR_SCHEMA = 4;
    {
        NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
        NSInteger stored = [ud integerForKey:@"BandicootToolbarSchema"];

        FILE *log2 = fopen("/tmp/bandicoot-toolbar.log", "a");
        if (!log2) log2 = stderr;
        fprintf(log2, "[bandicoot-toolbar] schema stored=%ld current=%d\n",
                (long)stored, BANDICOOT_TOOLBAR_SCHEMA);

        if (stored != BANDICOOT_TOOLBAR_SCHEMA) {
            // Migration: wipe every NSToolbar Configuration entry that
            // points at the Bandicoot main toolbar (v1, v2, …).
            NSDictionary *all = [ud dictionaryRepresentation];
            for (NSString *k in [all allKeys]) {
                if ([k hasPrefix:@"NSToolbar Configuration bandicoot."]) {
                    fprintf(log2, "[bandicoot-toolbar] migration: removing '%s'\n",
                            [k UTF8String]);
                    [ud removeObjectForKey:k];
                }
            }
            [ud setInteger:BANDICOOT_TOOLBAR_SCHEMA forKey:@"BandicootToolbarSchema"];
            [ud synchronize];
        }
        fflush(log2);
        if (log2 != stderr) fclose(log2);
    }

    NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"bandicoot.main.v2"];
    toolbar.delegate = delegate;
    toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
    toolbar.sizeMode = NSToolbarSizeModeRegular;
    // Customization on. macOS persists the user's layout in NSUserDefaults
    // keyed on the toolbar's identifier ("bandicoot.main"), so their choice
    // survives across launches.
    toolbar.allowsUserCustomization = YES;
    toolbar.autosavesConfiguration = YES;

    // Retain the delegate for the toolbar's lifetime.
    objc_setAssociatedObject(toolbar, "bandicoot.toolbar.delegate",
                             delegate, OBJC_ASSOCIATION_RETAIN);

    // Expanded: toolbar lives in its own row below the title bar. This is
    // the classic Mac toolbar look and doesn't require FullSizeContentView,
    // which GTK-Quartz's NSWindow doesn't set. (Unified style needs that
    // mask or it silently fails to render.)
    if (@available(macOS 11.0, *)) {
        win.toolbarStyle = NSWindowToolbarStyleExpanded;
    }

    [win setToolbar:toolbar];
    [toolbar setVisible:YES];

    // v0.1.0.1: thin white outline around the GL canvas so the bottom-right
    // resize corner is findable against black-on-black (terminal windows
    // below the all-black GL viewport). Drawn by CoreAnimation on the
    // contentView's existing layer (which is already there because GtkGLExt
    // attaches the NSOpenGLContext as a backing layer), so it composites on
    // top of GL contents without going through the GL pipeline.
    NSView *cv = win.contentView;
    if (cv) {
        cv.wantsLayer = YES;
        cv.layer.borderWidth = 1.5;
        cv.layer.borderColor = [[NSColor whiteColor] CGColor];
    }

    // Prevent gtk_widget_show_all (called later by various Coot init paths)
    // from undoing our hide and resurrecting the in-window toolbar.
    gtk_widget_set_no_show_all(gtk_toolbar, TRUE);
    gtk_widget_hide(gtk_toolbar);
}

// Show the system Customize Toolbar… sheet on the main window's toolbar.
// Invoked from a menu item; safe to call from any thread because
// runCustomizationPalette: hops to the main thread internally.
extern "C" void bandicoot_run_toolbar_customization(void) {
    for (NSWindow *w in [NSApp windows]) {
        NSToolbar *tb = [w toolbar];
        if (tb && [[tb identifier] isEqualToString:@"bandicoot.main.v2"]) {
            [tb runCustomizationPalette:nil];
            return;
        }
    }
}

// ---- Right-click context menu on the toolbar
//
// NSToolbar normally pops a context menu on right-click (Icon Only /
// Text Only / Customize Toolbar…). On GTK-Quartz with Tahoe the contentView
// (GTK GL area) intercepts right-clicks before they reach NSToolbar's own
// handlers — so the user only gets the menu when right-clicking the title
// bar (which is above contentView). Restore it via an NSEvent local
// monitor that checks if the click landed in the toolbar strip (between
// contentLayoutRect.maxY and the start of the title bar) and pops our own
// menu there.

static id bandicoot_right_click_monitor = nil;

static void bandicoot_show_toolbar_menu(NSWindow *win, NSEvent *event) {
    NSToolbar *tb = win.toolbar;
    BandicootMenuTarget *target = [BandicootMenuTarget shared];
    NSToolbarDisplayMode mode = tb ? tb.displayMode : NSToolbarDisplayModeIconAndLabel;

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
    [menu setAutoenablesItems:NO];

    // Display-mode radio entries — match the standard NSToolbar context
    // menu (Icon and Text / Icon Only / Text Only). Checkmark reflects
    // the toolbar's current mode.
    NSMenuItem *m;
    m = [menu addItemWithTitle:@"Icon and Text"
                        action:@selector(bandicootSetToolbarDisplayModeIconAndLabel:)
                 keyEquivalent:@""];
    [m setTarget:target];
    [m setState:(mode == NSToolbarDisplayModeIconAndLabel) ? NSControlStateValueOn
                                                            : NSControlStateValueOff];

    m = [menu addItemWithTitle:@"Icon Only"
                        action:@selector(bandicootSetToolbarDisplayModeIconOnly:)
                 keyEquivalent:@""];
    [m setTarget:target];
    [m setState:(mode == NSToolbarDisplayModeIconOnly) ? NSControlStateValueOn
                                                        : NSControlStateValueOff];

    m = [menu addItemWithTitle:@"Text Only"
                        action:@selector(bandicootSetToolbarDisplayModeLabelOnly:)
                 keyEquivalent:@""];
    [m setTarget:target];
    [m setState:(mode == NSToolbarDisplayModeLabelOnly) ? NSControlStateValueOn
                                                         : NSControlStateValueOff];

    [menu addItem:[NSMenuItem separatorItem]];

    m = [menu addItemWithTitle:@"Customize Toolbar…"
                        action:@selector(bandicootCustomizeToolbar:)
                 keyEquivalent:@""];
    [m setTarget:target];

    [NSMenu popUpContextMenu:menu withEvent:event forView:win.contentView];
}

static BOOL bandicoot_click_is_in_toolbar(NSWindow *win, NSEvent *event) {
    if (![win.toolbar isVisible]) return NO;
    // contentLayoutRect is the area below the toolbar in window
    // coordinates (origin at bottom-left). Anything above its maxY and
    // below the title bar is the toolbar strip. Title-bar height varies
    // by macOS, but the NSWindowStyleMaskTitled portion is roughly 28pt
    // — and we only want to NOT trigger on title-bar clicks (which have
    // their own context menu we shouldn't override).
    NSPoint p = event.locationInWindow;
    NSRect contentRect = win.contentLayoutRect;
    CGFloat title_top = NSHeight(win.frame) - 28.0;  // approx title-bar bottom
    return (p.y >= NSMaxY(contentRect)) && (p.y < title_top);
}

extern "C" void bandicoot_install_right_click_handler(void) {
    if (bandicoot_right_click_monitor) return;  // idempotent
    bandicoot_right_click_monitor =
        [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskRightMouseDown
                                              handler:^NSEvent *(NSEvent *e) {
            NSWindow *w = e.window;
            if (w && bandicoot_click_is_in_toolbar(w, e)) {
                bandicoot_show_toolbar_menu(w, e);
                return nil;  // consume — don't let the click fall through to contentView
            }
            return e;
        }];
}

// ---- Model-toolbar sidebar: dock / undock --------------------------------
// The model toolbar floats in its own "Model Tools" window (created just below
// in bandicoot_float_widget_in_window). "Docked" pins that window flush to the
// RIGHT edge of the main window -- a child window that follows it on move/resize
// (the status-bar trick) -- and "undocked" is the free-floating default. A
// Dock/Undock button on the sidebar toggles between them.
static GtkWidget *bandicoot_sidebar_floater    = NULL; // the floating GTK window
static GtkWidget *bandicoot_sidebar_parent_gtk = NULL; // main window (GTK)
static GtkWidget *bandicoot_sidebar_dock_btn   = NULL; // the Dock/Undock menu item
static GtkWidget *bandicoot_sidebar_toolbar    = NULL; // the model GtkToolbar inside
static GtkWidget *bandicoot_sidebar_settings_bar = NULL; // hidden holder keeping the settings toolitem in-tree
static GtkWidget *bandicoot_sidebar_settings_btn = NULL; // full-width Settings button at the bottom
static GtkWidget *bandicoot_sidebar_settings_gear = NULL; // gear glyph, left edge (icons+text)
static GtkWidget *bandicoot_sidebar_settings_text = NULL; // "Settings" label, centered in the button
static GtkWidget *bandicoot_sidebar_settings_pad  = NULL; // right spacer matching the gear (balances center)
static GtkWidget *bandicoot_sidebar_settings_menu = NULL; // the settings popup (still attached to setting1)
static NSWindow  *bandicoot_sidebar_ns         = nil;  // floater's NSWindow
static NSWindow  *bandicoot_sidebar_parent_ns  = nil;  // main NSWindow
static BOOL       bandicoot_sidebar_docked     = NO;
static NSUInteger bandicoot_sidebar_orig_mask  = 0;    // styleMask before docking
static BOOL       bandicoot_sidebar_mask_saved = NO;

// Defined with the native status bar further below; lets the docked sidebar
// stop short of the bottom status strip (and its resize grip).
static NSWindow *bandicoot_get_status_window(void);
// Defined just below; lazily resolves the floater + main NSWindows.
static void bandicoot_resolve_sidebar_windows(void);
// Persisted dock state (defined in c-interface-preferences.cc): the sidebar dock
// choice survives restarts via ~/.coot-preferences/bandicoot-sidebar-docked.
extern "C" void bandicoot_save_sidebar_docked(int docked);
extern "C" int  bandicoot_load_sidebar_docked(void);
// Defined in the Accept/Reject bar section; re-fit the A/R bar when the sidebar
// docks/undocks (the bar shortens to stop at the sidebar's left edge).
static void bandicoot_ar_reposition(void);
// Defined in the status-bar section; the bottom strip likewise shortens to
// clear the docked sidebar's column.
static void bandicoot_reposition_status_bar(void);
// Sequence-view dock (own section further below): the A/R bar stacks just under
// a docked sequence view (needs its height), and the shared settle/observer
// paths reposition the seqview too.
static CGFloat bandicoot_sv_docked_height(void);
static void    bandicoot_sv_reposition(void);

// Natural size the sidebar wants for the *current* toolbar style: width = the
// widest item + padding, height = the sum of every item's height. Walking the
// children works in every style (icons-only / icons+text / text) and counts
// overflowed items too, so the figure is stable.
static void bandicoot_sidebar_compute_natural(int *out_w, int *out_h) {
    int natural_h = 0, max_child_w = 0;
    GtkWidget *tb = bandicoot_sidebar_toolbar;
    if (tb && GTK_IS_CONTAINER(tb)) {
        GList *kids = gtk_container_get_children(GTK_CONTAINER(tb));
        for (GList *l = kids; l; l = l->next) {
            GtkRequisition cr = {0, 0};
            gtk_widget_size_request(GTK_WIDGET(l->data), &cr);
            natural_h += cr.height;
            if (cr.width > max_child_w) max_child_w = cr.width;
        }
        g_list_free(kids);
    }
    // Hug the widest item (in icon-only mode that's the "R/RC" button) with
    // just a hair of padding so the column never clips. With the undocked
    // window's traffic lights gone there's no title-bar minimum width fighting
    // this, so icon-only collapses to its natural minimum.
    int w = max_child_w > 0 ? max_child_w + 8 : 340;
    if (w < 44) w = 44;
    // The full-width "Settings ▸" button below mustn't clip, so widen to fit it
    // (this gently widens icon-only mode so "Settings" stays readable).
    if (bandicoot_sidebar_settings_btn) {
        GtkRequisition sr = {0, 0};
        gtk_widget_size_request(bandicoot_sidebar_settings_btn, &sr);
        if (sr.width > w) w = sr.width;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = natural_h;
}

static void bandicoot_reposition_sidebar(void) {
    if (!bandicoot_sidebar_docked || !bandicoot_sidebar_ns || !bandicoot_sidebar_parent_ns) return;
    NSWindow *p = bandicoot_sidebar_parent_ns;
    // Usable region = the content area BELOW the toolbar. contentLayoutRect
    // excludes both the title bar and the toolbar (contentRectForFrameRect did
    // not, which is why the old top edge overlapped the toolbar's bevel).
    NSView *cv = p.contentView;
    NSRect usable = [p convertRectToScreen:[cv convertRect:p.contentLayoutRect
                                                    toView:nil]];
    // Stop short of the bottom status strip (full-width, but z-ordered BELOW
    // this sidebar so the settings popup isn't occluded by it). Leaving the
    // strip's lower-right corner clear keeps its passthrough resize grip usable.
    NSWindow *sw = bandicoot_get_status_window();
    CGFloat statusH = sw ? NSHeight(sw.frame) : 0.0;
    CGFloat top    = NSMaxY(usable);
    CGFloat bottom = NSMinY(usable) + statusH;
    CGFloat availH = top - bottom;
    if (availH < 0) availH = 0;
    // Fill the full available height: stretch when the window is taller than
    // the sidebar's natural height, and shrink (letting the toolbar overflow
    // chevron appear) when it is shorter. Flush to the content's right edge.
    NSRect sb = bandicoot_sidebar_ns.frame;
    NSRect sf = NSMakeRect(NSMaxX(usable) - sb.size.width, bottom,
                           sb.size.width, availH);
    [bandicoot_sidebar_ns setFrame:sf display:YES];
}

// Re-fit the sidebar after a toolbar-style change: width follows the new
// style (icons-only is narrow, icons+text wide, etc.). Undocked → resize the
// floater to the new natural size; docked → set the new width and let
// reposition refill the height. Run from an idle so GTK has settled the new
// item requisitions first.
// Height the bottom "Settings ▸" button wants (0 before it exists).
static int bandicoot_sidebar_settings_bar_height(void) {
    if (!bandicoot_sidebar_settings_btn) return 0;
    GtkRequisition r = {0, 0};
    gtk_widget_size_request(bandicoot_sidebar_settings_btn, &r);
    return r.height;
}

// Match the Settings button's content to the toolbar's current display style:
// just the gear in icon-only mode (keeps the column tight), gear + word in
// icons+text, word alone in text-only. (The Coot 0.9 iconset has no gear/wrench
// icon, so we use the ⚙ glyph.)
static void bandicoot_sidebar_update_settings_label(void) {
    if (!bandicoot_sidebar_settings_btn || !bandicoot_sidebar_toolbar) return;
    if (!GTK_IS_TOOLBAR(bandicoot_sidebar_toolbar)) return;
    if (!bandicoot_sidebar_settings_text) return;
    GtkToolbarStyle st = gtk_toolbar_get_style(GTK_TOOLBAR(bandicoot_sidebar_toolbar));
    GtkWidget *gear = bandicoot_sidebar_settings_gear;
    GtkWidget *text = bandicoot_sidebar_settings_text;
    GtkWidget *pad  = bandicoot_sidebar_settings_pad;
    const char *gear_markup = "<span size='xx-large'>⚙</span>";  // gear ~2x
    if (st == GTK_TOOLBAR_ICONS) {
        // Just the gear, centered (like the icon-only tool buttons).
        gtk_label_set_markup(GTK_LABEL(text), gear_markup);
        gtk_widget_hide(gear);
        gtk_widget_hide(pad);
    } else if (st == GTK_TOOLBAR_TEXT) {
        gtk_label_set_text(GTK_LABEL(text), "Settings");
        gtk_widget_hide(gear);
        gtk_widget_hide(pad);
    } else {
        // icons+text: gear hugs the left edge, "Settings" centered in the full
        // button — a right spacer matched to the gear width balances the center.
        gtk_label_set_markup(GTK_LABEL(gear), gear_markup);
        gtk_label_set_text(GTK_LABEL(text), "Settings");
        gtk_widget_show(gear);
        GtkRequisition gr = {0, 0};
        gtk_widget_size_request(gear, &gr);
        gtk_widget_set_size_request(pad, gr.width, -1);
        gtk_widget_show(pad);
    }
}

static void bandicoot_sidebar_relayout(void) {
    if (!bandicoot_sidebar_floater) return;
    bandicoot_sidebar_update_settings_label();  // before measuring (width depends on it)
    int w = 0, h = 0;
    bandicoot_sidebar_compute_natural(&w, &h);
    if (bandicoot_sidebar_docked) {
        bandicoot_resolve_sidebar_windows();
        if (bandicoot_sidebar_ns) {
            NSRect f = bandicoot_sidebar_ns.frame;
            f.size.width = w;
            [bandicoot_sidebar_ns setFrame:f display:YES];
        }
        bandicoot_reposition_sidebar();
        // The sidebar's width just changed (icon style switch), so re-fit the
        // A/R bar which stops at the sidebar's left edge — otherwise it leaves a
        // gap or overlaps until the next window move/resize.
        bandicoot_ar_reposition();
    } else if (h > 0) {
        // Undocked: natural toolbar height + the pinned settings strip +
        // a small buffer below the last item.
        gtk_window_resize(GTK_WINDOW(bandicoot_sidebar_floater),
                          w, h + bandicoot_sidebar_settings_bar_height() + 16);
    }
}

static gboolean bandicoot_sidebar_relayout_idle(gpointer u) {
    bandicoot_sidebar_relayout();
    return FALSE;  // one-shot
}

static void bandicoot_sidebar_style_changed(GtkToolbar *tb, gpointer u) {
    g_idle_add(bandicoot_sidebar_relayout_idle, NULL);
}

@interface BandicootSidebarObserver : NSObject
@end
@implementation BandicootSidebarObserver
- (void)parentGeometryChanged:(NSNotification *)note { bandicoot_reposition_sidebar(); }
@end
static BandicootSidebarObserver *bandicoot_sidebar_observer = nil;

static void bandicoot_resolve_sidebar_windows(void) {
    if (!bandicoot_sidebar_ns && bandicoot_sidebar_floater && bandicoot_sidebar_floater->window)
        bandicoot_sidebar_ns =
            (NSWindow *)gdk_quartz_window_get_nswindow(bandicoot_sidebar_floater->window);
    if (!bandicoot_sidebar_parent_ns && bandicoot_sidebar_parent_gtk &&
        bandicoot_sidebar_parent_gtk->window)
        bandicoot_sidebar_parent_ns =
            (NSWindow *)gdk_quartz_window_get_nswindow(bandicoot_sidebar_parent_gtk->window);
}

// The undocked sidebar must never be closed or minimized: there is no way to
// bring it back once it's gone, and the toolbar is essential. Strip Closable +
// Miniaturizable from the mask (kills the red/yellow buttons and Cmd-W) and
// hide all three traffic lights; keep Titled + Resizable so it can still be
// dragged and resized. Removing the buttons also drops the title bar's minimum
// width, which lets the icon-only sidebar shrink to its widest item.
static void bandicoot_sidebar_lock_undocked_chrome(void) {
    if (!bandicoot_sidebar_ns) return;
    NSUInteger m = bandicoot_sidebar_ns.styleMask;
    m &= ~(NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable);
    [bandicoot_sidebar_ns setStyleMask:m];
    [[bandicoot_sidebar_ns standardWindowButton:NSWindowCloseButton] setHidden:YES];
    [[bandicoot_sidebar_ns standardWindowButton:NSWindowMiniaturizeButton] setHidden:YES];
    [[bandicoot_sidebar_ns standardWindowButton:NSWindowZoomButton] setHidden:YES];
}

static void bandicoot_sidebar_set_docked(BOOL docked) {
    @autoreleasepool {
        bandicoot_resolve_sidebar_windows();
        if (!bandicoot_sidebar_ns || !bandicoot_sidebar_parent_ns) return;
        if (docked) {
            bandicoot_sidebar_docked = YES;
            // Strip the title bar while docked: a titled window lets the user
            // drag the sidebar off the edge, and it looks wrong pinned inside
            // the main window. Borderless removes the chrome (and the drag
            // handle); the original mask is restored on undock.
            if (!bandicoot_sidebar_mask_saved) {
                bandicoot_sidebar_orig_mask  = bandicoot_sidebar_ns.styleMask;
                bandicoot_sidebar_mask_saved = YES;
            }
            [bandicoot_sidebar_ns setStyleMask:NSWindowStyleMaskBorderless];
            if (![bandicoot_sidebar_ns parentWindow])
                [bandicoot_sidebar_parent_ns addChildWindow:bandicoot_sidebar_ns
                                                    ordered:NSWindowAbove];
            if (!bandicoot_sidebar_observer) {
                bandicoot_sidebar_observer = [BandicootSidebarObserver new];
                NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
                [nc addObserver:bandicoot_sidebar_observer selector:@selector(parentGeometryChanged:)
                           name:NSWindowDidResizeNotification object:bandicoot_sidebar_parent_ns];
                [nc addObserver:bandicoot_sidebar_observer selector:@selector(parentGeometryChanged:)
                           name:NSWindowDidMoveNotification object:bandicoot_sidebar_parent_ns];
            }
            bandicoot_reposition_sidebar();
        } else {
            bandicoot_sidebar_docked = NO;
            if ([bandicoot_sidebar_ns parentWindow])
                [bandicoot_sidebar_parent_ns removeChildWindow:bandicoot_sidebar_ns];
            // Give the title bar (and resize chrome) back, then re-lock it so
            // the restored window still has no close/minimize/zoom buttons.
            if (bandicoot_sidebar_mask_saved)
                [bandicoot_sidebar_ns setStyleMask:bandicoot_sidebar_orig_mask];
            bandicoot_sidebar_lock_undocked_chrome();
        }
        // The A/R bar and the bottom status strip both shorten to clear the
        // docked sidebar's column, so re-fit them when the sidebar toggles.
        bandicoot_ar_reposition();
        bandicoot_reposition_status_bar();
    }
}

static void bandicoot_sidebar_dock_clicked(GtkMenuItem *item, gpointer u) {
    BOOL want = ! bandicoot_sidebar_docked;
    bandicoot_sidebar_set_docked(want);
    bandicoot_save_sidebar_docked(want ? 1 : 0);   // persist the user's choice
    if (bandicoot_sidebar_dock_btn)
        gtk_menu_item_set_label(GTK_MENU_ITEM(bandicoot_sidebar_dock_btn),
                                want ? "Undock" : "Dock");
}

// Apply the saved sidebar dock state at startup (docked by default when the
// preference file is absent). Call once after the sidebar windows are realized
// (i.e. from pin_settings_item). Reads, does not write, the persisted choice.
extern "C" void bandicoot_sidebar_dock_default(void) {
    BOOL docked = bandicoot_load_sidebar_docked() ? YES : NO;
    bandicoot_sidebar_set_docked(docked);
    if (bandicoot_sidebar_dock_btn)
        gtk_menu_item_set_label(GTK_MENU_ITEM(bandicoot_sidebar_dock_btn),
                                docked ? "Undock" : "Dock");
}

// Get / set the sidebar dock state from outside (the "Dock Toolbar?" preference).
// Keeps the settings-popup Dock/Undock label in sync.
extern "C" int bandicoot_sidebar_is_docked(void) {
    return bandicoot_sidebar_docked ? 1 : 0;
}
extern "C" void bandicoot_sidebar_set_docked_ext(int docked) {
    BOOL want = docked ? YES : NO;
    if (want == bandicoot_sidebar_docked) return;
    bandicoot_sidebar_set_docked(want);
    bandicoot_save_sidebar_docked(want ? 1 : 0);   // persist the user's choice
    if (bandicoot_sidebar_dock_btn)
        gtk_menu_item_set_label(GTK_MENU_ITEM(bandicoot_sidebar_dock_btn),
                                want ? "Undock" : "Dock");
}

// Append a Dock/Undock toggle to the model toolbar's settings popup (the
// bottom triangle button). Replaces the old standalone Dock button row.
extern "C" void bandicoot_sidebar_add_dock_menu_item(GtkWidget *menu) {
    if (!menu || !GTK_IS_MENU_SHELL(menu)) return;
    GtkWidget *sep = gtk_separator_menu_item_new();
    gtk_widget_show(sep);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
    // Start label matches the startup dock state (undocked → offer "Dock").
    GtkWidget *item = gtk_menu_item_new_with_label(bandicoot_sidebar_docked
                                                       ? "Undock" : "Dock");
    g_signal_connect(item, "activate",
                     G_CALLBACK(bandicoot_sidebar_dock_clicked), NULL);
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    bandicoot_sidebar_dock_btn = item;
}

extern "C" void bandicoot_float_widget_in_window(GtkWidget *widget, const char *title) {
    if (!widget) return;

    GtkWidget *parent_toplevel = gtk_widget_get_toplevel(widget);

    GtkWidget *floater = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(floater), title ? title : "Tools");
    gtk_window_set_resizable(GTK_WINDOW(floater), TRUE);
    // Keep the sidebar floating above other Bandicoot windows so users
    // can visually "dock" it against the main window's edge without
    // losing it behind the main window on subsequent clicks. This is
    // separate from transient_for (which would also group window-manager
    // operations like move/minimize — we want stay-on-top only).
    gtk_window_set_keep_above(GTK_WINDOW(floater), TRUE);

    if (parent_toplevel && GTK_IS_WINDOW(parent_toplevel)) {
        // Deliberately NOT calling gtk_window_set_transient_for() here.
        // On GTK-Quartz, transient windows inherit the parent's window-
        // manager grouping, which makes the sidebar move/minimize in
        // lockstep with the main window. We want the sidebar to be a
        // fully-independent NSWindow that the user can position freely.
        // The widget-callback hookup below (via GladeParentKey) is
        // independent of transient_for and gives us what we need.

        // Coot's patched lookup_widget consults a "GladeParentKey" pointer
        // when a toplevel doesn't itself have the named hookup. Setting it
        // on the floater redirects lookups back to window1 so libglade
        // callbacks like on_model_toolbar_*_activate still resolve.
        g_object_set_data(G_OBJECT(floater), "GladeParentKey", parent_toplevel);

        // Tentative default size — width wide enough for icons+text
        // labels; height is set properly below from the toolbar's own
        // natural requisition after show. Position just to the right
        // of the parent window.
        int px = 0, py = 0, pw = 0;
        gtk_window_get_position(GTK_WINDOW(parent_toplevel), &px, &py);
        gtk_window_get_size(GTK_WINDOW(parent_toplevel), &pw, NULL);
        gtk_window_set_default_size(GTK_WINDOW(floater), 340, 600);
        gtk_window_move(GTK_WINDOW(floater), px + pw + 16, py);
    } else {
        gtk_window_set_default_size(GTK_WINDOW(floater), 340, 600);
    }

    // Layout: floater > vbox > [ model_toolbar (expands), settings strip ].
    // The model toolbar expands to fill, so its overflow chevron lands at its
    // bottom edge; the settings strip below it always stays visible (the
    // settings item is reparented into it by bandicoot_sidebar_pin_settings_item
    // from main.cc, after this runs). gtk_widget_reparent handles ref-counting.
    GtkWidget *vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(floater), vbox);
    gtk_widget_reparent(widget, vbox);
    gtk_box_set_child_packing(GTK_BOX(vbox), widget, TRUE, TRUE, 0, GTK_PACK_START);

    GtkWidget *settings_bar = gtk_toolbar_new();
    gtk_toolbar_set_orientation(GTK_TOOLBAR(settings_bar), GTK_ORIENTATION_VERTICAL);
    gtk_toolbar_set_style(GTK_TOOLBAR(settings_bar), GTK_TOOLBAR_ICONS);
    gtk_toolbar_set_show_arrow(GTK_TOOLBAR(settings_bar), FALSE);  // never overflow it
    // A few px below the strip keeps the settings triangle off the very bottom
    // edge so the window's bottom resize border stays grabbable.
    gtk_box_pack_end(GTK_BOX(vbox), settings_bar, FALSE, FALSE, 4);

    bandicoot_sidebar_floater      = floater;
    bandicoot_sidebar_parent_gtk   = parent_toplevel;
    bandicoot_sidebar_toolbar      = widget;
    bandicoot_sidebar_settings_bar = settings_bar;

    // Closing the floater hides rather than destroys it, so the widget tree
    // stays intact and the window can be brought back later.
    g_signal_connect(floater, "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    if (GTK_IS_TOOLBAR(widget)) {
        // Show the overflow chevron: when the docked sidebar is too short for
        // every item, GtkToolbar moves the spillover into the chevron's pop-up
        // menu (whose proxy items carry icon + label) — exactly the behaviour
        // of the main toolbar when the window is too narrow.
        gtk_toolbar_set_show_arrow(GTK_TOOLBAR(widget), TRUE);
        // Re-fit width/height whenever the user switches icons / icons+text /
        // text in the settings popup.
        g_signal_connect(widget, "style-changed",
                         G_CALLBACK(bandicoot_sidebar_style_changed), NULL);
    }

    gtk_widget_show_all(floater);
    // Final sizing happens in bandicoot_sidebar_pin_settings_item (called next
    // from main.cc), once the settings item has moved out of the toolbar.
}

// Position the settings popup just to the RIGHT of the Settings button (its
// top-right corner), so it appears alongside the window like the overflow
// chevron's menu — never popping down into the bottom status strip.
static void bandicoot_sidebar_settings_menu_pos(GtkMenu *menu, gint *x, gint *y,
                                                gboolean *push_in, gpointer u) {
    GtkWidget *btn = GTK_WIDGET(u);
    GtkWidget *top = gtk_widget_get_toplevel(btn);
    GtkAllocation a = {0, 0, 0, 0};
    gtk_widget_get_allocation(btn, &a);
    gint tx = 0, ty = 0;  // button's top-right corner, in the toplevel's coords
    gtk_widget_translate_coordinates(btn, top, a.width, 0, &tx, &ty);
    gint ox = 0, oy = 0;
    GdkWindow *tw = gtk_widget_get_window(top);
    if (tw) gdk_window_get_origin(tw, &ox, &oy);
    *x = ox + tx;
    *y = oy + ty;
    *push_in = FALSE;     // keep it to the side, don't shove it back under the button
}

static void bandicoot_sidebar_settings_clicked(GtkButton *b, gpointer u) {
    if (!bandicoot_sidebar_settings_menu) return;
    gtk_menu_popup(GTK_MENU(bandicoot_sidebar_settings_menu), NULL, NULL,
                   bandicoot_sidebar_settings_menu_pos, b, 0,
                   gtk_get_current_event_time());
}

// Replace the model toolbar's tiny settings triangle with a full-width
// "Settings ▸" button at the bottom of the sidebar whose menu pops to the SIDE.
// The original settings toolitem is moved into a HIDDEN holder (kept in-tree so
// its submenu stays attached and the item callbacks still resolve via
// lookup_widget). `settings_menu` is model_toolbar_setting1_menu. Called once at
// startup from main.cc right after bandicoot_float_widget_in_window.
extern "C" void bandicoot_sidebar_pin_settings_item(GtkWidget *item,
                                                    GtkWidget *settings_menu) {
    if (!item || !bandicoot_sidebar_settings_bar) return;
    // Move the original settings toolitem (menubar + triangle) into the hidden
    // holder so it stops cluttering the toolbar but stays in the widget tree.
    if (GTK_IS_TOOL_ITEM(item)) {
        g_object_ref(item);
        if (item->parent)
            gtk_container_remove(GTK_CONTAINER(item->parent), item);
        gtk_toolbar_insert(GTK_TOOLBAR(bandicoot_sidebar_settings_bar),
                           GTK_TOOL_ITEM(item), -1);
        g_object_unref(item);
    }
    gtk_widget_hide(bandicoot_sidebar_settings_bar);  // in-tree but invisible

    bandicoot_sidebar_settings_menu = settings_menu;

    // The visible, full-width Settings button. Its child is a 3-part row so the
    // gear can hug the left edge while "Settings" stays centered in the FULL
    // button width (a right spacer matched to the gear balances the center).
    // Content per toolbar style is set by bandicoot_sidebar_update_settings_label.
    GtkWidget *vbox = gtk_widget_get_parent(bandicoot_sidebar_settings_bar);
    GtkWidget *btn  = gtk_button_new();
    GtkWidget *brow = gtk_hbox_new(FALSE, 0);
    GtkWidget *gear = gtk_label_new(NULL);
    GtkWidget *text = gtk_label_new(NULL);
    GtkWidget *pad  = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(text), 0.5, 0.5);
    gtk_box_pack_start(GTK_BOX(brow), gear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(brow), text, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(brow), pad,  FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btn), brow);
    bandicoot_sidebar_settings_btn  = btn;
    bandicoot_sidebar_settings_gear = gear;
    bandicoot_sidebar_settings_text = text;
    bandicoot_sidebar_settings_pad  = pad;
    g_signal_connect(btn, "clicked",
                     G_CALLBACK(bandicoot_sidebar_settings_clicked), NULL);
    if (vbox) gtk_box_pack_end(GTK_BOX(vbox), btn, FALSE, TRUE, 4);
    gtk_widget_show_all(btn);

    // Strip the undocked window's traffic lights so it can never be closed and
    // lost — and so the title bar's button minimum-width stops fighting the
    // tight icon-only column. Do this BEFORE fitting so the resize isn't clamped.
    bandicoot_resolve_sidebar_windows();
    bandicoot_sidebar_lock_undocked_chrome();
    // Now that the toolbar's item set is final, fit the floater to it.
    bandicoot_sidebar_relayout();
    // Ship with the sidebar docked by default.
    bandicoot_sidebar_dock_default();
}

// --- Default placement for newly-realized top-level windows -----------------
//
// GTK-Quartz on Tahoe places windows with no explicit position in the
// lower-left of the active screen, which feels wrong: every dialog the
// user opens jumps to a corner instead of appearing near the click that
// spawned it. We install one emission hook on GtkWidget's "realize"
// signal at startup; it inspects each realized widget and, if it's a
// GtkWindow with no explicit position-policy already set, picks a sane
// one (center-on-parent for transient dialogs, near the mouse pointer
// for everything else). Windows that explicitly chose a placement (e.g.
// the splash uses GTK_WIN_POS_CENTER) are left alone.

static gboolean bandicoot_window_realize_hook(GSignalInvocationHint *hint,
                                              guint n_param_values,
                                              const GValue *param_values,
                                              gpointer data) {
    if (n_param_values < 1) return TRUE;
    GObject *obj = (GObject *)g_value_get_object(&param_values[0]);
    if (!GTK_IS_WINDOW(obj)) return TRUE;

    GtkWindow *win = GTK_WINDOW(obj);

    GtkWindowPosition pos = GTK_WIN_POS_NONE;
    g_object_get(win, "window-position", &pos, NULL);
    if (pos != GTK_WIN_POS_NONE) return TRUE;

    GtkWindow *transient_for = gtk_window_get_transient_for(win);
    gtk_window_set_position(win, transient_for ? GTK_WIN_POS_CENTER_ON_PARENT
                                               : GTK_WIN_POS_MOUSE);
    return TRUE;
}

extern "C" void bandicoot_setup_window_positioning(void) {
    guint realize_id = g_signal_lookup("realize", GTK_TYPE_WIDGET);
    if (realize_id) {
        g_signal_add_emission_hook(realize_id, 0,
                                   bandicoot_window_realize_hook,
                                   NULL, NULL);
    }
}

// --- Raise newly-mapped top-level windows to the front ---------------------
//
// Many Coot dialogs are cached: the first invocation creates the widget,
// subsequent invocations call gtk_widget_show() on the same hidden window.
// On GTK-Quartz/Tahoe, showing a previously-hidden window doesn't raise
// it — it pops back into whatever z-order slot it last had, which is
// usually behind the main window. Dialogs like "Accept Refinement?"
// then go invisible on every refinement after the first.
//
// gtk_window_present() is the cross-platform fix in theory, but GTK 2's
// Quartz backend doesn't reliably re-order the NSWindow either. We hook
// the "map" signal (fires every time a window goes from unmapped to
// mapped, including re-shows) and use AppKit directly to bring the
// NSWindow forward.

// Deferred raise: fires on the next main-loop iteration, after every
// "map" handler + GTK's own NSWindow setFrame/order chatter has
// settled. Holds a weak ref to the GtkWindow so a dialog that's been
// destroyed before the idle runs is safely skipped.
static gboolean bandicoot_raise_idle(gpointer data) {
    GtkWidget *widget = (GtkWidget *)data;
    if (widget && GTK_IS_WINDOW(widget)) {
        GdkWindow *gdk_win = gtk_widget_get_window(widget);
        if (gdk_win) {
            NSWindow *ns_win = gdk_quartz_window_get_nswindow(gdk_win);
            if (ns_win && [ns_win isVisible]) {
                // orderFrontRegardless: harder raise than orderFront:;
                // ignores the macOS app-active check that can leave a
                // window stuck behind another after a frame change.
                [ns_win orderFrontRegardless];
            }
        }
        g_object_unref(widget);
    }
    return G_SOURCE_REMOVE;
}

static gboolean bandicoot_window_map_hook(GSignalInvocationHint *hint,
                                          guint n_param_values,
                                          const GValue *param_values,
                                          gpointer data) {
    if (n_param_values < 1) return TRUE;
    GObject *obj = (GObject *)g_value_get_object(&param_values[0]);
    if (!GTK_IS_WINDOW(obj)) return TRUE;

    // Hold a ref so the widget can't be finalized before the idle
    // callback runs (rare, but harmless to defend against).
    g_object_ref(obj);
    g_idle_add(bandicoot_raise_idle, obj);

    return TRUE;
}

extern "C" void bandicoot_setup_window_raising(void) {
    guint map_id = g_signal_lookup("map", GTK_TYPE_WIDGET);
    if (map_id) {
        g_signal_add_emission_hook(map_id, 0,
                                   bandicoot_window_map_hook,
                                   NULL, NULL);
    }
}

// --- Persistent, always-on-top dialogs -----------------------------------
//
// Some Coot dialogs are rebuilt from scratch on every invocation — the
// residue-type chooser behind Simple Mutate / Mutate & Auto Fit is the worst
// offender. Two annoyances follow on GTK-Quartz/Tahoe: (1) the chooser opens
// in front, but because the user just clicked in the GL view to pick the
// residue the main window is key and immediately reclaims the top z-slot,
// burying the chooser behind it; (2) since the widget is a brand-new instance
// each time, it always reappears at the realize-hook's default placement
// (center-on-parent / mouse) rather than wherever the user last dragged it.
//
// Fix: register such a dialog under a stable `role` string. We pin its
// NSWindow to the floating level so it stays above the normal-level main
// window until the user dismisses it, and we remember its on-screen origin
// (captured on unmap, keyed by role) so the next incarnation reappears there.

static NSMutableDictionary *bandicoot_dialog_origins = nil;  // role -> NSValue(point)

static const char *bandicoot_dialog_role(GtkWidget *w) {
    return (const char *)g_object_get_data(G_OBJECT(w), "bandicoot-dialog-role");
}

static NSWindow *bandicoot_nswindow_for(GtkWidget *w) {
    GdkWindow *gw = gtk_widget_get_window(w);
    return gw ? (NSWindow *)gdk_quartz_window_get_nswindow(gw) : nil;
}

// On unmap (which fires before unrealize — including when the dialog is
// destroyed by an amino-acid button — while the NSWindow is still alive),
// stash the window's current origin so a freshly-built replacement can be put
// back in the same spot.
static void bandicoot_dialog_unmap_cb(GtkWidget *w, gpointer data) {
    const char *role = bandicoot_dialog_role(w);
    NSWindow *nw = bandicoot_nswindow_for(w);
    if (!role || !nw) return;
    if (!bandicoot_dialog_origins)
        bandicoot_dialog_origins = [[NSMutableDictionary alloc] init];
    NSRect f = [nw frame];
    [bandicoot_dialog_origins setObject:[NSValue valueWithPoint:f.origin]
                                 forKey:[NSString stringWithUTF8String:role]];
}

// Deferred to idle so GTK's own initial placement (the realize hook plus any
// queued AppKit windowDidMove) has settled before we override the origin and
// pin the level. Mirrors the bandicoot_raise_idle pattern.
static gboolean bandicoot_dialog_map_idle(gpointer data) {
    GtkWidget *w = (GtkWidget *)data;
    if (w && GTK_IS_WINDOW(w)) {
        NSWindow *nw = bandicoot_nswindow_for(w);
        if (nw) {
            [nw setLevel:NSFloatingWindowLevel];
            const char *role = bandicoot_dialog_role(w);
            if (role && bandicoot_dialog_origins) {
                NSValue *v = [bandicoot_dialog_origins
                                 objectForKey:[NSString stringWithUTF8String:role]];
                if (v) [nw setFrameOrigin:[v pointValue]];
            }
            [nw orderFrontRegardless];
        }
    }
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}

static void bandicoot_dialog_map_cb(GtkWidget *w, gpointer data) {
    g_object_ref(w);
    g_idle_add(bandicoot_dialog_map_idle, w);
}

extern "C" void bandicoot_register_persistent_dialog(GtkWidget *w, const char *role) {
    if (!w) return;
    if (role)
        g_object_set_data_full(G_OBJECT(w), "bandicoot-dialog-role",
                               g_strdup(role), g_free);
    g_signal_connect(w, "map",   G_CALLBACK(bandicoot_dialog_map_cb),   NULL);
    g_signal_connect(w, "unmap", G_CALLBACK(bandicoot_dialog_unmap_cb), NULL);
}

// ---- Native status bar (bottom child window) ----------------------------
//
// GTK's main_window_statusbar lives on the GL-owned contentView and is
// permanently occluded. We host the status text in a borderless child
// NSWindow pinned flush to the bottom edge of the main window, full content
// width. add_status_bar_text() funnels here via bandicoot_set_status_text().

static const CGFloat BANDICOOT_STATUS_BAR_HEIGHT = 26.0;

static NSWindow    *bandicoot_status_window = nil;  // the child strip (singleton)
static NSTextField *bandicoot_status_field  = nil;  // its label (owned by contentView)

// Forward-declared up in the sidebar section so the docked sidebar can avoid
// overlapping this strip; defined here once the static exists.
static NSWindow *bandicoot_get_status_window(void) { return bandicoot_status_window; }
static NSWindow    *bandicoot_status_parent = nil;  // the main NSWindow (unretained)

static void bandicoot_reposition_status_bar(void) {
    if (!bandicoot_status_window || !bandicoot_status_parent) return;
    // Pin to the bottom edge of the parent's *content* area, full width, so the
    // strip never overlaps the title bar / toolbar chrome at the top. The strip
    // is z-ordered BELOW the sidebar (see install), so the sidebar and its
    // settings popup draw over it cleanly where they share the bottom band.
    NSRect cf = [bandicoot_status_parent
                    contentRectForFrameRect:bandicoot_status_parent.frame];
    NSRect sf = NSMakeRect(cf.origin.x, cf.origin.y,
                           cf.size.width, BANDICOOT_STATUS_BAR_HEIGHT);
    [bandicoot_status_window setFrame:sf display:YES];
}

// Observes the parent window so the strip stays glued and full-width as the
// user moves/resizes the main window. addChildWindow: handles the move, but
// not the width, so we resync on every move/resize notification.
@interface BandicootStatusObserver : NSObject
@end
@implementation BandicootStatusObserver
- (void)parentGeometryChanged:(NSNotification *)note {
    bandicoot_reposition_status_bar();
}
@end
static BandicootStatusObserver *bandicoot_status_observer = nil;

// A cosmetic resize grip drawn in the bottom-right of the status strip. The
// strip's lower-right corner overlaps the main window's *functional* resize
// corner, and the strip ignoresMouseEvents so the drag passes straight through
// to the window -- this view is purely a visual hint that the corner resizes.
@interface BandicootResizeGrip : NSView
@end
@implementation BandicootResizeGrip
- (BOOL)isFlipped { return NO; }            // bottom-left origin
- (NSView *)hitTest:(NSPoint)point { return nil; }  // never intercept the resize
- (void)drawRect:(NSRect)dirty {
    CGFloat w = self.bounds.size.width;
    NSBezierPath *p = [NSBezierPath bezierPath];
    [p setLineWidth:1.0];
    [[NSColor tertiaryLabelColor] set];
    for (int i = 0; i < 3; i++) {           // three nested diagonal ticks
        CGFloat off = 3.0 + i * 3.5;
        [p moveToPoint:NSMakePoint(w - off, 2.0)];
        [p lineToPoint:NSMakePoint(w - 2.0, off)];
    }
    [p stroke];
}
@end

extern "C" void bandicoot_install_status_bar(GtkWidget *main_window) {
    @autoreleasepool {
        if (!main_window) return;
        if (bandicoot_status_window) return;  // already installed

        GtkWidget *toplevel = gtk_widget_get_toplevel(main_window);
        if (!toplevel || !toplevel->window) return;
        NSWindow *win = (NSWindow *)gdk_quartz_window_get_nswindow(toplevel->window);
        if (!win) return;

        bandicoot_status_parent = win;  // unretained: parent outlives the strip

        NSRect frame = NSMakeRect(0, 0, 400, BANDICOOT_STATUS_BAR_HEIGHT);
        NSWindow *sw = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:NSWindowStyleMaskBorderless
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
        sw.releasedWhenClosed = NO;
        // Match the GTK theme's window background (the gray of the sidebar / A-R
        // bar) instead of the stark white of windowBackgroundColor.
        NSColor *bg = [NSColor windowBackgroundColor];
        GtkStyle *style = gtk_widget_get_style(main_window);
        if (style) {
            GdkColor c = style->bg[GTK_STATE_NORMAL];
            bg = [NSColor colorWithCalibratedRed:c.red / 65535.0
                                           green:c.green / 65535.0
                                            blue:c.blue / 65535.0
                                           alpha:1.0];
        }
        sw.backgroundColor = bg;
        sw.opaque = YES;
        sw.hasShadow = NO;
        [sw setIgnoresMouseEvents:YES];  // never steal clicks from the GL canvas

        // Vertically center a ~17pt-tall label within the strip.
        CGFloat fld_h = 17.0;
        CGFloat fld_y = (BANDICOOT_STATUS_BAR_HEIGHT - fld_h) / 2.0;
        NSTextField *tf = [[NSTextField alloc] initWithFrame:
            NSMakeRect(8, fld_y, frame.size.width - 16 - 14, fld_h)];  // leave room for the grip
        tf.bezeled = NO;
        tf.editable = NO;
        tf.selectable = NO;
        tf.drawsBackground = NO;
        tf.font = [NSFont systemFontOfSize:13];
        tf.textColor = [NSColor labelColor];
        tf.lineBreakMode = NSLineBreakByTruncatingTail;
        tf.stringValue = @"";
        tf.autoresizingMask = NSViewWidthSizable;
        [[sw contentView] addSubview:tf];
        [tf release];                 // contentView now owns it (MRC: balance alloc)
        bandicoot_status_field = tf;  // unretained ref, valid for the window's life

        // Cosmetic resize grip, pinned to the bottom-right (the strip's lower-
        // right overlaps the window's functional resize corner; the strip
        // ignoresMouseEvents so the drag passes straight through to resize).
        const CGFloat grip = 14.0;
        BandicootResizeGrip *rg = [[BandicootResizeGrip alloc] initWithFrame:
            NSMakeRect(frame.size.width - grip, 0, grip, grip)];
        rg.autoresizingMask = NSViewMinXMargin;   // stay glued to the right edge
        [[sw contentView] addSubview:rg];
        [rg release];                 // contentView owns it (MRC: balance alloc)

        bandicoot_status_window = sw; // singleton, intentionally never released

        // Glue to the parent: follows it on move, stays just above it in z.
        [win addChildWindow:sw ordered:NSWindowAbove];

        bandicoot_status_observer = [BandicootStatusObserver new];
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserver:bandicoot_status_observer
               selector:@selector(parentGeometryChanged:)
                   name:NSWindowDidResizeNotification
                 object:win];
        [nc addObserver:bandicoot_status_observer
               selector:@selector(parentGeometryChanged:)
                   name:NSWindowDidMoveNotification
                 object:win];

        bandicoot_reposition_status_bar();
        [sw orderFront:nil];
        // (The settings-menu occlusion is handled by raising the menu above
        // this strip on map — see bandicoot_settings_menu_mapped — rather than
        // reordering the strip, which doesn't stick for a glued child window.)
    }
}

extern "C" void bandicoot_set_status_text(const char *text) {
    if (!bandicoot_status_field) return;
    @autoreleasepool {
        NSString *s = text ? [NSString stringWithUTF8String:text] : @"";
        if (!s) s = @"";  // invalid UTF-8 → stringWithUTF8String returns nil
        bandicoot_status_field.stringValue = s;
    }
}

// ---- Native Accept/Reject bar (top child window) -------------------------
//
// A self-contained native replacement for Coot's docked Accept/Reject dialog.
// We deliberately do NOT reuse Coot's DIALOG_DOCKED path (its in-window frame
// labels aren't ported in Bandicoot and it SIGSEGVs on geometry update). This
// bar is a borderless GTK floater docked as a child window to the TOP edge of
// the main window — the same mechanism as the model-toolbar sidebar — holding
// a row of geometry "lights" (rendered natively from the refinement results)
// plus Accept / Reject buttons. "Always shown" mode: it lives for the whole
// session; refinements just refresh the lights. Coot's stock UNDOCKED dialog
// is left completely untouched; do_accept_reject_dialog() routes here instead
// while this bar is active.

#define BANDICOOT_AR_MAX_LIGHTS 8

// Accept/Reject action bridges, implemented Coot-side (graphics-info-gui.cc)
// where the c-interface refinement calls are in scope.
extern "C" void bandicoot_ar_accept(void);
extern "C" void bandicoot_ar_reject(void);

static GtkWidget *bandicoot_ar_floater    = NULL;
static GtkWidget *bandicoot_ar_parent_gtk = NULL;
static GtkWidget *bandicoot_ar_lights_box = NULL;
static GtkWidget *bandicoot_ar_accept_btn = NULL;
static GtkWidget *bandicoot_ar_reject_btn = NULL;
static GtkWidget *bandicoot_ar_lights[BANDICOOT_AR_MAX_LIGHTS] = { NULL };
static NSWindow  *bandicoot_ar_ns        = nil;
static NSWindow  *bandicoot_ar_parent_ns = nil;
static BOOL       bandicoot_ar_pinned    = NO;   // window is a pinned child (always YES post-install)
static NSUInteger bandicoot_ar_orig_mask = 0;
static BOOL       bandicoot_ar_mask_saved = NO;
// Driven by the "Dock Accept/Reject Dialog?" preference (Coot's docked flags):
//  pref_active     = docked? (YES → our bar handles A/R, stock dialog suppressed)
//  pref_always_show= Always show (YES) vs Always hide (NO, bar only during refine)
static BOOL       bandicoot_ar_pref_active      = YES;
static BOOL       bandicoot_ar_pref_always_show = YES;

// Defined further below in this section; referenced before their definitions.
static void bandicoot_ar_resolve_windows(void);

// One geometry light = a colour-filled rectangle with the parameter name
// stacked over its value, both drawn inside. Colour + the two strings are
// stored on the widget; the full label is also the tooltip.
static gboolean bandicoot_ar_light_expose(GtkWidget *w, GdkEventExpose *e, gpointer u) {
    cairo_t *cr = gdk_cairo_create(w->window);
    double W = w->allocation.width, H = w->allocation.height;
    GdkColor *c = (GdkColor *)g_object_get_data(G_OBJECT(w), "bcoot-light-color");
    double r = c ? c->red / 65535.0   : 0.82;
    double g = c ? c->green / 65535.0 : 0.82;
    double b = c ? c->blue / 65535.0  : 0.82;
    cairo_set_source_rgb(cr, r, g, b);
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.30, 0.30, 0.30);       // thin border
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, W - 1, H - 1);
    cairo_stroke(cr);

    // Text colour for contrast against the fill (luminance threshold).
    double lum = 0.299 * r + 0.587 * g + 0.114 * b;
    if (lum < 0.5) cairo_set_source_rgb(cr, 1, 1, 1);
    else           cairo_set_source_rgb(cr, 0, 0, 0);
    const char *name = (const char *)g_object_get_data(G_OBJECT(w), "bcoot-light-name");
    const char *val  = (const char *)g_object_get_data(G_OBJECT(w), "bcoot-light-value");
    cairo_text_extents_t te;
    if (name && *name) {
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 8.5);
        cairo_text_extents(cr, name, &te);
        cairo_move_to(cr, (W - te.width) / 2 - te.x_bearing, H * 0.40);
        cairo_show_text(cr, name);
    }
    if (val && *val) {
        cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 13.0);
        cairo_text_extents(cr, val, &te);
        cairo_move_to(cr, (W - te.width) / 2 - te.x_bearing, H * 0.92);
        cairo_show_text(cr, val);
    }
    cairo_destroy(cr);
    return TRUE;
}

// Refresh the lights from refinement results. names[i]/values[i] are drawn
// inside each rectangle; tooltips[i] is the full label; colors[i] the
// distortion colour. n past the pool max is clamped, remaining lights hidden.
// (Button enable/disable is handled separately by present/dismiss, because a
// lightless dialog — e.g. Rigid Body Fit — still needs working buttons.)
extern "C" void bandicoot_ar_bar_set_lights(int n, const char *const *names,
                                            const char *const *values,
                                            const char *const *tooltips,
                                            const GdkColor *colors) {
    if (n < 0) n = 0;
    if (n > BANDICOOT_AR_MAX_LIGHTS) n = BANDICOOT_AR_MAX_LIGHTS;
    for (int i = 0; i < BANDICOOT_AR_MAX_LIGHTS; i++) {
        GtkWidget *w = bandicoot_ar_lights[i];
        if (!w) continue;
        if (i < n) {
            GdkColor *c = (GdkColor *)g_object_get_data(G_OBJECT(w), "bcoot-light-color");
            if (!c) {
                c = g_new0(GdkColor, 1);
                g_object_set_data_full(G_OBJECT(w), "bcoot-light-color", c, g_free);
            }
            *c = colors[i];
            g_object_set_data_full(G_OBJECT(w), "bcoot-light-name",
                                   g_strdup(names && names[i] ? names[i] : ""), g_free);
            g_object_set_data_full(G_OBJECT(w), "bcoot-light-value",
                                   g_strdup(values && values[i] ? values[i] : ""), g_free);
            gtk_widget_set_tooltip_text(w, tooltips && tooltips[i] ? tooltips[i] : "");
            gtk_widget_show(w);
            gtk_widget_queue_draw(w);
        } else {
            gtk_widget_hide(w);
        }
    }
}

// Show/hide the docked bar window (a pinned borderless child). Used by the
// "Always hide" preference: the bar only appears while a refinement is pending.
static void bandicoot_ar_bar_show_window(BOOL show) {
    bandicoot_ar_resolve_windows();
    if (!bandicoot_ar_ns) return;
    if (show) {
        if (![bandicoot_ar_ns parentWindow] && bandicoot_ar_parent_ns)
            [bandicoot_ar_parent_ns addChildWindow:bandicoot_ar_ns ordered:NSWindowAbove];
        bandicoot_ar_reposition();
        [bandicoot_ar_ns orderFront:nil];
    } else {
        [bandicoot_ar_ns orderOut:nil];
    }
}

// A refinement/fit is pending: enable Accept/Reject, and (in Always-hide mode)
// pop the bar into view. Called from do_accept_reject_dialog.
extern "C" void bandicoot_ar_bar_present(void) {
    if (bandicoot_ar_accept_btn) gtk_widget_set_sensitive(bandicoot_ar_accept_btn, TRUE);
    if (bandicoot_ar_reject_btn) gtk_widget_set_sensitive(bandicoot_ar_reject_btn, TRUE);
    if (!bandicoot_ar_pref_always_show)
        bandicoot_ar_bar_show_window(YES);
}

// The accept/reject decision was made (or no refinement is pending): clear the
// lights, disable the buttons, and (in Always-hide mode) hide the bar again.
static void bandicoot_ar_bar_dismiss(void) {
    bandicoot_ar_bar_set_lights(0, NULL, NULL, NULL, NULL);
    if (bandicoot_ar_accept_btn) gtk_widget_set_sensitive(bandicoot_ar_accept_btn, FALSE);
    if (bandicoot_ar_reject_btn) gtk_widget_set_sensitive(bandicoot_ar_reject_btn, FALSE);
    if (!bandicoot_ar_pref_always_show)
        bandicoot_ar_bar_show_window(NO);
}

static void bandicoot_ar_button_clicked(GtkButton *b, gpointer accept_ptr) {
    if (GPOINTER_TO_INT(accept_ptr)) bandicoot_ar_accept();
    else                            bandicoot_ar_reject();
    bandicoot_ar_bar_dismiss();
}

static void bandicoot_ar_resolve_windows(void) {
    if (!bandicoot_ar_ns && bandicoot_ar_floater && bandicoot_ar_floater->window)
        bandicoot_ar_ns = (NSWindow *)gdk_quartz_window_get_nswindow(bandicoot_ar_floater->window);
    if (!bandicoot_ar_parent_ns && bandicoot_ar_parent_gtk && bandicoot_ar_parent_gtk->window)
        bandicoot_ar_parent_ns =
            (NSWindow *)gdk_quartz_window_get_nswindow(bandicoot_ar_parent_gtk->window);
}

static void bandicoot_ar_reposition(void) {
    if (!bandicoot_ar_pinned || !bandicoot_ar_ns || !bandicoot_ar_parent_ns) return;
    NSWindow *p = bandicoot_ar_parent_ns;
    NSView *cv = p.contentView;
    NSRect usable = [p convertRectToScreen:[cv convertRect:p.contentLayoutRect toView:nil]];
    CGFloat barH = NSHeight(bandicoot_ar_ns.frame);
    // Span the content width, but stop at the docked sidebar's left edge.
    CGFloat sidebarW = (bandicoot_sidebar_docked && bandicoot_sidebar_ns)
                           ? NSWidth(bandicoot_sidebar_ns.frame) : 0.0;
    CGFloat w = NSWidth(usable) - sidebarW;
    if (w < 1) w = 1;
    // A docked sequence view occupies the very top; sit just beneath it.
    CGFloat svH = bandicoot_sv_docked_height();
    NSRect sf = NSMakeRect(NSMinX(usable), NSMaxY(usable) - barH - svH, w, barH);
    [bandicoot_ar_ns setFrame:sf display:YES];
}

static gboolean bandicoot_ar_reposition_idle(gpointer u) {
    bandicoot_ar_reposition();
    return FALSE;  // one-shot
}

// Re-snap every child bar into place. Used at startup: the bars are positioned
// before the main window's geometry is final, so they look misaligned until the
// first move/resize — this settles them without the user having to nudge.
static gboolean bandicoot_settle_all_bars_idle(gpointer u) {
    bandicoot_reposition_sidebar();
    bandicoot_sv_reposition();
    bandicoot_ar_reposition();
    bandicoot_reposition_status_bar();
    return FALSE;  // one-shot
}

@interface BandicootARObserver : NSObject
@end
@implementation BandicootARObserver
- (void)parentGeometryChanged:(NSNotification *)note { bandicoot_ar_reposition(); }
@end
static BandicootARObserver *bandicoot_ar_observer = nil;

// Pin the bar as a borderless child of the main window (done once at install;
// the bar stays pinned for the whole session — the Dock/Undock button toggles
// the MODE, not the window).
static void bandicoot_ar_pin(void) {
    @autoreleasepool {
        bandicoot_ar_resolve_windows();
        if (!bandicoot_ar_ns || !bandicoot_ar_parent_ns) return;
        bandicoot_ar_pinned = YES;
        if (!bandicoot_ar_mask_saved) {
            bandicoot_ar_orig_mask  = bandicoot_ar_ns.styleMask;
            bandicoot_ar_mask_saved = YES;
        }
        [bandicoot_ar_ns setStyleMask:NSWindowStyleMaskBorderless];
        if (![bandicoot_ar_ns parentWindow])
            [bandicoot_ar_parent_ns addChildWindow:bandicoot_ar_ns ordered:NSWindowAbove];
        if (!bandicoot_ar_observer) {
            bandicoot_ar_observer = [BandicootARObserver new];
            NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
            [nc addObserver:bandicoot_ar_observer selector:@selector(parentGeometryChanged:)
                       name:NSWindowDidResizeNotification object:bandicoot_ar_parent_ns];
            [nc addObserver:bandicoot_ar_observer selector:@selector(parentGeometryChanged:)
                       name:NSWindowDidMoveNotification object:bandicoot_ar_parent_ns];
        }
        bandicoot_ar_reposition();
    }
}

// Build the Accept/Reject bar and dock it to the top edge.
extern "C" void bandicoot_install_accept_reject_bar(GtkWidget *main_window) {
    GtkWidget *floater = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(floater), "Accept / Reject");
    gtk_window_set_resizable(GTK_WINDOW(floater), TRUE);
    gtk_window_set_keep_above(GTK_WINDOW(floater), TRUE);
    if (main_window && GTK_IS_WINDOW(main_window))
        g_object_set_data(G_OBJECT(floater), "GladeParentKey", main_window);

    GtkWidget *hbox = gtk_hbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 3);
    gtk_container_add(GTK_CONTAINER(floater), hbox);

    // Geometry lights row — rectangles big enough to hold name-over-value.
    GtkWidget *lights_box = gtk_hbox_new(TRUE, 2);
    for (int i = 0; i < BANDICOOT_AR_MAX_LIGHTS; i++) {
        GtkWidget *light = gtk_drawing_area_new();
        gtk_widget_set_size_request(light, 58, 38);
        g_signal_connect(light, "expose-event",
                         G_CALLBACK(bandicoot_ar_light_expose), NULL);
        gtk_box_pack_start(GTK_BOX(lights_box), light, FALSE, FALSE, 0);
        bandicoot_ar_lights[i] = light;   // shown on demand by set_lights
    }
    gtk_box_pack_start(GTK_BOX(hbox), lights_box, FALSE, FALSE, 4);

    GtkWidget *reject = gtk_button_new_with_label("Reject");
    g_signal_connect(reject, "clicked",
                     G_CALLBACK(bandicoot_ar_button_clicked), GINT_TO_POINTER(0));
    gtk_box_pack_start(GTK_BOX(hbox), reject, FALSE, FALSE, 0);

    GtkWidget *accept = gtk_button_new_with_label("Accept");
    g_signal_connect(accept, "clicked",
                     G_CALLBACK(bandicoot_ar_button_clicked), GINT_TO_POINTER(1));
    gtk_box_pack_start(GTK_BOX(hbox), accept, FALSE, FALSE, 0);

    g_signal_connect(floater, "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    bandicoot_ar_floater    = floater;
    bandicoot_ar_parent_gtk = main_window;
    bandicoot_ar_lights_box = lights_box;
    bandicoot_ar_accept_btn = accept;
    bandicoot_ar_reject_btn = reject;

    gtk_widget_show_all(floater);
    // Start idle: no lights, Accept/Reject disabled until a refinement runs.
    bandicoot_ar_bar_set_lights(0, NULL, NULL, NULL, NULL);
    if (bandicoot_ar_accept_btn) gtk_widget_set_sensitive(bandicoot_ar_accept_btn, FALSE);
    if (bandicoot_ar_reject_btn) gtk_widget_set_sensitive(bandicoot_ar_reject_btn, FALSE);
    // Pin to the top edge. Visibility is then governed by the docked preference
    // (applied via bandicoot_ar_bar_apply_prefs once Coot's flags are known).
    bandicoot_ar_resolve_windows();
    bandicoot_ar_pin();
    // Re-snap ALL bars (sidebar, A/R, status) once layout settles — on idle and
    // again after a short delay — so nothing looks misaligned on first paint.
    g_idle_add(bandicoot_settle_all_bars_idle, NULL);
    g_timeout_add(200, bandicoot_settle_all_bars_idle, NULL);
}

// Whether the native bar is currently handling Accept/Reject (docked == Yes), so
// do_accept_reject_dialog should route to it instead of Coot's stock dialog.
extern "C" int bandicoot_ar_bar_is_active(void) {
    return (bandicoot_ar_floater && bandicoot_ar_pref_active) ? 1 : 0;
}

// Apply the "Dock Accept/Reject Dialog?" preference. active = docked (Yes);
// always_show = "Always show" (vs "Always hide"). Updates bar visibility now:
// hidden when undocked (No) or Always-hide-and-idle; shown when Always-show.
extern "C" void bandicoot_ar_bar_apply_prefs(int active, int always_show) {
    bandicoot_ar_pref_active      = active ? YES : NO;
    bandicoot_ar_pref_always_show = always_show ? YES : NO;
    if (!bandicoot_ar_floater) return;
    if (bandicoot_ar_pref_active && bandicoot_ar_pref_always_show)
        bandicoot_ar_bar_show_window(YES);   // docked + always shown
    else
        bandicoot_ar_bar_show_window(NO);    // undocked, or wait for a refinement
}

// ---- Native docked Sequence View (top child window, above the A/R bar) ------
// The (top-level) nsv sequence-view dialog docks like the A/R bar: a borderless
// child window pinned flush to the top edge of the main window's content area,
// stacked ABOVE the A/R bar (which drops just beneath it). It stays its own
// NSWindow, so it composites correctly on gdk-quartz (a GTK strip docked inside
// the main window does not — that was the invisible-sequence-view bug). Driven
// by bandicoot_dock_sequence_view()/..._undock().
static GtkWidget *bandicoot_sv_floater    = NULL;  // the nsv top-level dialog
static NSWindow  *bandicoot_sv_ns         = nil;
static NSWindow  *bandicoot_sv_parent_ns  = nil;
static BOOL       bandicoot_sv_docked     = NO;
static NSUInteger bandicoot_sv_orig_mask  = 0;
static BOOL       bandicoot_sv_mask_saved = NO;
// The currently-active open sequence-view dialog (docked OR floating), so the
// "Dock Sequence View Dialog?" preference can dock/undock it live. nsv.cc notes
// it on open / NULL on destroy; dock and raise update it too.
static GtkWidget *bandicoot_sv_current    = NULL;

static void bandicoot_sv_resolve_windows(void) {
    if (!bandicoot_sv_ns && bandicoot_sv_floater && bandicoot_sv_floater->window)
        bandicoot_sv_ns = (NSWindow *)gdk_quartz_window_get_nswindow(bandicoot_sv_floater->window);
    // Parent = the same main window the A/R bar docks to.
    if (!bandicoot_sv_parent_ns) {
        bandicoot_ar_resolve_windows();
        bandicoot_sv_parent_ns = bandicoot_ar_parent_ns;
    }
}

// Height a docked sequence view occupies at the top (0 when not docked). The
// A/R bar reads this so it can sit just beneath the sequence view.
static CGFloat bandicoot_sv_docked_height(void) {
    if (!bandicoot_sv_docked || !bandicoot_sv_ns) return 0.0;
    return NSHeight(bandicoot_sv_ns.frame);
}

static void bandicoot_sv_reposition(void) {
    if (!bandicoot_sv_docked || !bandicoot_sv_ns || !bandicoot_sv_parent_ns) return;
    NSWindow *p = bandicoot_sv_parent_ns;
    NSView *cv = p.contentView;
    NSRect usable = [p convertRectToScreen:[cv convertRect:p.contentLayoutRect toView:nil]];
    CGFloat svH = NSHeight(bandicoot_sv_ns.frame);
    // Never occupy more than 1/3 of the content height; a many-chain sequence
    // then scrolls inside the strip (the scrolledwindow shows a vertical bar).
    CGFloat cap = NSHeight(usable) / 3.0;
    if (cap > 1.0 && svH > cap) svH = cap;
    // Span the content width, but stop at the docked sidebar's left edge.
    CGFloat sidebarW = (bandicoot_sidebar_docked && bandicoot_sidebar_ns)
                           ? NSWidth(bandicoot_sidebar_ns.frame) : 0.0;
    CGFloat w = NSWidth(usable) - sidebarW;
    if (w < 1) w = 1;
    // Flush to the top of the content area, directly under the native toolbar.
    NSRect sf = NSMakeRect(NSMinX(usable), NSMaxY(usable) - svH, w, svH);
    [bandicoot_sv_ns setFrame:sf display:YES];
}

// Re-apply the cap after GTK's post-dock layout settles. GTK can re-assert the
// window's requested (uncapped) height in a resize idle that runs AFTER
// bandicoot_dock_sequence_view()'s synchronous reposition, which is why a tall
// strip initially overshot the 1/3 cap and only snapped back on the next manual
// resize. Default idle priority runs after GTK's higher-priority resize idle.
static gboolean bandicoot_sv_reposition_idle(gpointer u) {
    bandicoot_sv_reposition();
    bandicoot_ar_reposition();
    return FALSE;   // one-shot
}

@interface BandicootSVObserver : NSObject
@end
@implementation BandicootSVObserver
- (void)parentGeometryChanged:(NSNotification *)note {
    bandicoot_sv_reposition();
    bandicoot_ar_reposition();   // the A/R bar follows the seqview's height
}
@end
static BandicootSVObserver *bandicoot_sv_observer = nil;

extern "C" void bandicoot_dock_sequence_view(GtkWidget *sv_dialog) {
    @autoreleasepool {
        if (!sv_dialog) return;
        // Only one docked sequence view at a time: if a DIFFERENT one is already
        // docked, close it first, so a shorter new strip doesn't leave the
        // previous one's bottom peeking out. (Floating seqviews stack freely and
        // are unaffected — they never come through here.) Destroying it fires
        // on_nsv_dialog_destroy, which undocks it and clears the globals below.
        if (bandicoot_sv_docked && bandicoot_sv_floater && bandicoot_sv_floater != sv_dialog)
            gtk_widget_destroy(bandicoot_sv_floater);
        bandicoot_sv_floater = sv_dialog;
        bandicoot_sv_ns = nil;           // re-resolve for this (possibly new) window
        bandicoot_sv_resolve_windows();
        if (!bandicoot_sv_ns || !bandicoot_sv_parent_ns) return;
        bandicoot_sv_docked = YES;
        if (!bandicoot_sv_mask_saved) {
            bandicoot_sv_orig_mask  = bandicoot_sv_ns.styleMask;
            bandicoot_sv_mask_saved = YES;
        }
        [bandicoot_sv_ns setStyleMask:NSWindowStyleMaskBorderless];
        if (![bandicoot_sv_ns parentWindow])
            [bandicoot_sv_parent_ns addChildWindow:bandicoot_sv_ns ordered:NSWindowAbove];
        if (!bandicoot_sv_observer) {
            bandicoot_sv_observer = [BandicootSVObserver new];
            NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
            [nc addObserver:bandicoot_sv_observer selector:@selector(parentGeometryChanged:)
                       name:NSWindowDidResizeNotification object:bandicoot_sv_parent_ns];
            [nc addObserver:bandicoot_sv_observer selector:@selector(parentGeometryChanged:)
                       name:NSWindowDidMoveNotification object:bandicoot_sv_parent_ns];
        }
        bandicoot_sv_reposition();
        bandicoot_ar_reposition();
        bandicoot_sv_current = sv_dialog;   // active window (closing a previous one cleared it)
        // Re-cap once GTK finishes sizing the freshly-docked window (see above).
        g_idle_add(bandicoot_sv_reposition_idle, NULL);
    }
}

extern "C" void bandicoot_undock_sequence_view(void) {
    @autoreleasepool {
        bandicoot_sv_docked = NO;
        if (bandicoot_sv_ns) {
            if (bandicoot_sv_mask_saved)
                [bandicoot_sv_ns setStyleMask:bandicoot_sv_orig_mask];
            if ([bandicoot_sv_ns parentWindow] && bandicoot_sv_parent_ns)
                [bandicoot_sv_parent_ns removeChildWindow:bandicoot_sv_ns];
        }
        bandicoot_sv_ns        = nil;
        bandicoot_sv_floater   = NULL;
        bandicoot_sv_mask_saved = NO;
        bandicoot_ar_reposition();       // the A/R bar reclaims the top edge
    }
}

extern "C" int bandicoot_sequence_view_is_docked(void) {
    return bandicoot_sv_docked ? 1 : 0;
}

extern "C" void bandicoot_note_sequence_view(GtkWidget *sv_dialog) {
    bandicoot_sv_current = sv_dialog;
}

// Apply the dock preference to the open sequence view immediately (live toggle).
// No-op if nothing is open, or if it is already in the requested state.
extern "C" void bandicoot_apply_sequence_view_dock_pref(int docked) {
    if (!bandicoot_sv_current) return;   // applies on next open instead
    if (docked) {
        if (!bandicoot_sv_docked) bandicoot_dock_sequence_view(bandicoot_sv_current);
    } else {
        if (bandicoot_sv_docked) bandicoot_undock_sequence_view();
    }
}

// Bring an already-open sequence view to the front. For a docked child window,
// gdk_window_raise() does not re-order it among the parent's child windows, so a
// second molecule's docked strip stays on top and re-selecting the first looks
// like a no-op. Re-stack it explicitly (and make it the active one).
extern "C" void bandicoot_raise_sequence_view(GtkWidget *sv_dialog) {
    @autoreleasepool {
        if (!sv_dialog || !sv_dialog->window) return;
        NSWindow *w = (NSWindow *)gdk_quartz_window_get_nswindow(sv_dialog->window);
        if (!w) return;
        NSWindow *p = [w parentWindow];
        if (p) {                       // docked child -> re-stack above its siblings
            [p removeChildWindow:w];
            [p addChildWindow:w ordered:NSWindowAbove];
        } else {
            [w orderFront:nil];
        }
        bandicoot_sv_current = sv_dialog;
    }
}

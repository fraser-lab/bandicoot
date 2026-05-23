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

#include <gtk/gtk.h>
#include <gdk/gdkquartz.h>

#include <string>   // for Quicksave's filename munging
#include <sys/stat.h>  // for stat() — verifying Quicksave wrote a file

#include "bandicoot_appkit.h"

// Forward-declare Coot's Python eval bridge so we can dispatch Bandicoot
// toolbar items (e.g. Sphere Refine) to Coot's Python layer without
// pulling cc-interface-scripting.hh into the Objective-C++ unit.
// Declared with C++ linkage to match the actual definition in
// src/c-interface.cc.
void safe_python_command_by_char_star(const char *python_command);

// Forward-declare Coot C-interface functions we need for the Quicksave
// toolbar action. These live inside BEGIN_C_DECLS in c-interface.h, so
// extern "C" matches the actual linkage.
extern "C" {
    int first_coords_imol(void);
    int go_to_atom_molecule_number(void);   // currently-focused molecule
    const char *molecule_name(int imol);
    int save_coordinates(int imol, const char *filename);
    short int is_valid_model_molecule(int imol);
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
@end

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

static NSMenuItem *build_item(GtkMenuItem *gtk_item) {
    if (GTK_IS_SEPARATOR_MENU_ITEM(gtk_item)) {
        return [NSMenuItem separatorItem];
    }
    NSString *title = string_from_gtk_label(gtk_item);
    NSMenuItem *ns_item = [[NSMenuItem alloc] initWithTitle:title
                                                     action:nil
                                              keyEquivalent:@""];

    GtkWidget *sub = gtk_menu_item_get_submenu(gtk_item);
    if (sub && GTK_IS_MENU(sub)) {
        NSMenu *ns_sub = [[NSMenu alloc] initWithTitle:title];
        [ns_sub setAutoenablesItems:NO];
        populate_from_gtk_menu(ns_sub, GTK_MENU_SHELL(sub));
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
        [ns_menu addItem:build_item(GTK_MENU_ITEM(l->data))];
    }
    g_list_free(children);
}

extern "C" double bandicoot_get_backing_scale_factor(void) {
    NSScreen *screen = [NSScreen mainScreen];
    return screen ? (double)[screen backingScaleFactor] : 1.0;
}

extern "C" void bandicoot_activate_app(void) {
    // Apps launched from a wrapper script start as background apps; their
    // windows can appear behind the launcher's own windows. Force foreground.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
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
    int w = (int)ceil(sz.width)  + 8;
    int h = (int)ceil(sz.height) + 8;
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

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, [rep bitmapData]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (out_width)  *out_width  = w;
    if (out_height) *out_height = h;
    return tex;
}

extern "C" void bandicoot_free_text_texture(unsigned int tex) {
    if (tex == 0) return;
    GLuint t = tex;
    glDeleteTextures(1, &t);
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

    [app_item setSubmenu:app_sub];
    [main_menu addItem:app_item];

    GList *children = gtk_container_get_children(GTK_CONTAINER(menubar));
    for (GList *l = children; l; l = l->next) {
        if (!GTK_IS_MENU_ITEM(l->data)) continue;
        [main_menu addItem:build_item(GTK_MENU_ITEM(l->data))];
    }
    g_list_free(children);

    [NSApp setMainMenu:main_menu];

    gtk_widget_set_no_show_all(menubar, TRUE);
    gtk_widget_hide(menubar);
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

// ---- Dispatch idle-callbacks (run after AppKit's event-tracking loop)

static gboolean bandicoot_fire_tool_clicked(gpointer data) {
    GtkToolButton *tb = (GtkToolButton *)data;
    // GtkToolButton inherits from GtkToolItem (not GtkButton). The "clicked"
    // signal lives on GtkToolButton itself; emitting it triggers Coot's
    // libglade-wired handlers exactly as a real user click would.
    if (tb && GTK_IS_TOOL_BUTTON(tb)) {
        g_signal_emit_by_name(tb, "clicked");
    }
    return G_SOURCE_REMOVE;
}

// Most of Coot 0.9's Python ecosystem is Python-2-syntax (`print "..."`
// without parens, `execfile()`, etc.) and fails to import under the
// Python 3 interpreter Bandicoot is built against. Of the 106 .py files
// shipped, only ~14 compile cleanly — fitting.py, coot_utils.py,
// coot_gui.py, generic_objects.py are all broken. Functions like
// sphere_refine(), toggle_backrub_rotamers() etc. are simply undefined
// in the running interpreter, so dispatching them via PyRun_SimpleString
// throws a silent NameError and the toolbar button does nothing.
//
// Rather than port the whole upstream Coot Python suite to py3, Bandicoot
// re-defines just the handful of functions its toolbar references — in
// py3-compatible form, using only the SWIG-exposed C bindings (which DO
// work). We lazy-load these on the first Python dispatch so the
// definitions are guaranteed to be in scope by the time the user's click
// reaches PyRun_SimpleString.
//
// Keep the block ASCII / no fancy quotes, and indent with spaces (Python
// is strict about this).
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
    if (cmd) {
        // Lazy-load the py3 replacement module on first Python dispatch.
        // safe_python_command_by_char_star routes through PyRun_SimpleString,
        // which prints any tracebacks to stderr but doesn't propagate them
        // — so failed function calls (NameError, AttributeError) silently
        // no-op. Defining the functions up front makes the dispatch work.
        static gboolean inited = FALSE;
        if (!inited) {
            safe_python_command_by_char_star(bandicoot_py3_extras_src);
            inited = TRUE;
        }
        safe_python_command_by_char_star(cmd);
        g_free(cmd);
    }
    return G_SOURCE_REMOVE;
}

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
    // Python-dispatched items (Bandicoot extras like Sphere Refine)
    NSString *py = objc_getAssociatedObject(sender, &kPythonCmdKey);
    if (py) {
        g_idle_add(bandicoot_fire_python_command,
                   g_strdup([py UTF8String]));
        return;
    }
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
};

static const struct bandicoot_extra BANDICOOT_EXTRAS[] = {
    // Bandicoot-specific actions implemented in C
    {"auto_open_mtz", "Auto-open MTZ", bandicoot_action_auto_open_mtz, NULL, NULL},
    {"quicksave",     "Quicksave",     bandicoot_action_quicksave,     NULL, "coot-save.png"},

    // Refinement extensions (python/fitting.py, python/coot_toolbuttons.py)
    {"sphere_refine",         "Sphere Refine",         NULL, "sphere_refine()",        "refine-1.svg"},
    {"sphere_refine_plus",    "Sphere Refine +",       NULL, "sphere_refine_plus()",   "refine-1.svg"},
    {"tandem_refine",         "Tandem Refine",         NULL, "refine_tandem_residues()", "refine-1.svg"},
    {"sphere_regularize",     "Sphere Regularize",     NULL, "sphere_regularize()",    "regularize-1.svg"},
    {"sphere_regularize_plus","Sphere Regularize +",   NULL, "sphere_regularize_plus()","regularize-1.svg"},
    {"refine_residue",        "Refine Residue",        NULL, "refine_active_residue()","refine-1.svg"},
    {"backrub_toggle",        "Backrub Rotamers",      NULL, "toggle_backrub_rotamers()","auto-fit-rotamer.svg"},
    {"repeat_refine_zone",    "Repeat Refine Zone",    NULL, "repeat_refine_zone()",   "rrz.svg"},
    {"cis_trans",             "Cis ↔ Trans",           NULL, "do_cis_trans_conversion_setup(1)","flip-peptide.svg"},

    // Validation
    {"update_atom_overlaps",  "Update Atom Overlaps",  NULL, "atom_overlaps_for_this_model()","auto-fit-rotamer.svg"},
    {"interactive_dots",      "Interactive Dots",      NULL, "toggle_interactive_probe_dots()","probe-clash.svg"},
    {"local_probe_dots",      "Local Probe Dots",      NULL, "probe_local_sphere_active_atom()","probe-clash.svg"},

    // Building
    {"undo_molecule_chooser", "Choose Undo Molecule",  NULL, "show_set_undo_molecule_chooser()","undo-1.svg"},
    {"find_waters",           "Find Waters",           NULL, "wrapped_create_find_waters_dialog()","add-water.svg"},
    {"split_water",           "Split Water",           NULL, "split_active_water()",   "add-water.svg"},
    {"build_na",              "Build NA",              NULL, "find_nucleic_acids_local(6.0)","dna.svg"},
    {"ligand_builder",        "Ligand Builder",        NULL, "start_ligand_builder_gui()","go-to-ligand.svg"},

    // Display
    {"full_screen_toggle",    "Full Screen",           NULL, "toggle_full_screen()",   "reset-view-32.svg"},
    {"hydrogen_toggle",       "Toggle Hydrogens",      NULL, "toggle_hydrogen_display()","delete.svg"},
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
        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
        [item setLabel:[NSString stringWithUTF8String:e->label]];
        [item setPaletteLabel:[NSString stringWithUTF8String:e->label]];
        [item setTarget:[BandicootToolbarTarget shared]];
        [item setAction:@selector(dispatch:)];

        NSImage *icon = image_from_pixmaps_dir(e->icon_basename);
        [item setImage:icon ? icon : fallback_icon];

        if (e->c_callback) {
            objc_setAssociatedObject(item, &kCCallbackKey,
                                     [NSValue valueWithPointer:(void *)e->c_callback],
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        } else if (e->python_cmd) {
            objc_setAssociatedObject(item, &kPythonCmdKey,
                                     [NSString stringWithUTF8String:e->python_cmd],
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
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

    // --- 4) Default visible set: the main toolbar items in their original
    //         order, with Auto-open MTZ inserted after Open Coords and
    //         Quicksave appended at the end (Bandicoot's two file-action
    //         additions). Persisted user customizations override this via
    //         macOS autosave.
    for (NSUInteger i = 0; i < main_idents.count; i++) {
        [delegate.defaultIdentifiers addObject:main_idents[i]];
        if (i == 0) {
            [delegate.defaultIdentifiers addObject:@"bandicoot.extra.auto_open_mtz"];
        }
    }
    [delegate.defaultIdentifiers addObject:@"bandicoot.extra.quicksave"];

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
    static const int BANDICOOT_TOOLBAR_SCHEMA = 1;
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

    // Reparent the widget into the new floating window. gtk_widget_reparent
    // handles ref-counting and removal-from-old-parent for us.
    gtk_widget_reparent(widget, floater);

    // Closing the floater hides rather than destroys it, so the widget tree
    // stays intact and the window can be brought back later.
    g_signal_connect(floater, "delete-event",
                     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    // Disable the toolbar's overflow chevron BEFORE size_request, so
    // GTK doesn't budget the toolbar's height around the chevron's
    // collapse behaviour. With show-arrow off, every item is always
    // visible — which is what we want for a dedicated tool window.
    if (GTK_IS_TOOLBAR(widget)) {
        gtk_toolbar_set_show_arrow(GTK_TOOLBAR(widget), FALSE);
    }

    gtk_widget_show_all(floater);

    // Sum the natural heights and find the max natural width across
    // the toolbar's individual children. gtk_widget_size_request() on
    // the GtkToolbar itself returns its *minimum* (just one item +
    // chevron) on GTK-Quartz — useless here. Walking the children
    // gives us the real dimensions each item wants.
    int natural_h = 0;
    int max_child_w = 0;
    if (GTK_IS_CONTAINER(widget)) {
        GList *kids = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList *l = kids; l; l = l->next) {
            GtkWidget *child = GTK_WIDGET(l->data);
            GtkRequisition cr = {0, 0};
            gtk_widget_size_request(child, &cr);
            natural_h += cr.height;
            if (cr.width > max_child_w) max_child_w = cr.width;
        }
        g_list_free(kids);
    }
    if (natural_h > 0) {
        // + 32 px buffer below the last item so it isn't flush with
        // the window chrome, and a small allowance for the title bar.
        // Width = widest item + 24 px for left/right padding around
        // the toolbar column, with an 80 px floor in case the items
        // somehow report 0.
        int win_w = max_child_w > 0 ? max_child_w + 24 : 340;
        if (win_w < 80) win_w = 80;
        gtk_window_resize(GTK_WINDOW(floater), win_w, natural_h + 48);
    }
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

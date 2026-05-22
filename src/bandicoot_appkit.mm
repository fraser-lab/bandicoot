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

#include "bandicoot_appkit.h"

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

@interface BandicootMenuTarget : NSObject
+ (instancetype)shared;
- (void)dispatch:(NSMenuItem *)sender;
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
    // Add a placeholder so the GTK-derived top-level items start at slot 1.
    NSMenuItem *app_item = [[NSMenuItem alloc] initWithTitle:@""
                                                      action:nil
                                               keyEquivalent:@""];
    NSMenu *app_sub = [[NSMenu alloc] initWithTitle:@"Bandicoot"];
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

// Coot's callbacks.c handler for File > Auto-open MTZ.
extern "C" void on_auto_open_mtz_activate(GtkMenuItem *menuitem, gpointer user_data);

static gboolean bandicoot_fire_auto_open_mtz(gpointer data) {
    on_auto_open_mtz_activate(NULL, NULL);
    return G_SOURCE_REMOVE;
}

@interface BandicootCustomToolbarTarget : NSObject
+ (instancetype)shared;
- (void)autoOpenMtz:(id)sender;
@end

@implementation BandicootCustomToolbarTarget
+ (instancetype)shared {
    static BandicootCustomToolbarTarget *s;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [BandicootCustomToolbarTarget new]; });
    return s;
}
- (void)autoOpenMtz:(id)sender {
    g_idle_add(bandicoot_fire_auto_open_mtz, NULL);
}
@end

static char kGtkButtonKey;

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
    NSValue *boxed = objc_getAssociatedObject(sender, &kGtkButtonKey);
    GtkToolButton *tb = (GtkToolButton *)[boxed pointerValue];
    if (tb && GTK_IS_TOOL_BUTTON(tb)) {
        g_idle_add(bandicoot_fire_tool_clicked, tb);
    }
}
@end

@interface BandicootToolbarDelegate : NSObject <NSToolbarDelegate>
@property (nonatomic, strong) NSMutableArray<NSString *> *itemIdentifiers;
@property (nonatomic, strong) NSMutableDictionary<NSString *, NSToolbarItem *> *itemsById;
@end

@implementation BandicootToolbarDelegate
- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)tb {
    return _itemIdentifiers;
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)tb {
    return _itemIdentifiers;
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

extern "C" void bandicoot_install_native_toolbar(GtkWidget *gtk_toolbar) {
    if (!gtk_toolbar || !GTK_IS_TOOLBAR(gtk_toolbar)) return;

    GtkWidget *toplevel = gtk_widget_get_toplevel(gtk_toolbar);
    if (!toplevel || !toplevel->window) return;
    NSWindow *win = (NSWindow *)gdk_quartz_window_get_nswindow(toplevel->window);
    if (!win) return;

    BandicootToolbarDelegate *delegate = [BandicootToolbarDelegate new];
    delegate.itemIdentifiers = [NSMutableArray new];
    delegate.itemsById = [NSMutableDictionary new];

    int idx = 0;
    int tool_btn_count = 0;
    GList *children = gtk_container_get_children(GTK_CONTAINER(gtk_toolbar));
    for (GList *l = children; l; l = l->next) {
        GtkWidget *child = GTK_WIDGET(l->data);

        // Separators -> NSToolbarSpaceItemIdentifier
        if (GTK_IS_SEPARATOR_TOOL_ITEM(child)) {
            [delegate.itemIdentifiers addObject:NSToolbarSpaceItemIdentifier];
            continue;
        }

        if (!GTK_IS_TOOL_BUTTON(child)) continue;
        GtkToolButton *tb = GTK_TOOL_BUTTON(child);

        NSString *ident = [NSString stringWithFormat:@"bandicoot.tool.%d", idx++];
        const char *label_c = gtk_tool_button_get_label(tb);
        NSString *label = (label_c && *label_c)
                          ? [NSString stringWithUTF8String:label_c]
                          : @"";

        NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:ident];
        [item setLabel:label];
        [item setPaletteLabel:label];
        [item setTarget:[BandicootToolbarTarget shared]];
        [item setAction:@selector(dispatch:)];

        NSImage *img = image_for_tool_button(tb);
        if (img) [item setImage:img];

        if (!gtk_widget_is_sensitive(GTK_WIDGET(tb))) [item setEnabled:NO];

        // GtkToolButton is itself a GtkButton subclass, so gtk_button_clicked()
        // works on it directly.
        objc_setAssociatedObject(item, &kGtkButtonKey,
                                 [NSValue valueWithPointer:tb],
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [delegate.itemIdentifiers addObject:ident];
        delegate.itemsById[ident] = item;
        ++tool_btn_count;

        // Insert the Bandicoot-specific Auto-open MTZ item immediately after
        // the first GtkToolButton (Open Coords...), reusing its icon so the
        // two file-open actions look alike.
        if (tool_btn_count == 1) {
            NSString *mtzId = @"bandicoot.tool.auto_open_mtz";
            NSToolbarItem *mtzItem = [[NSToolbarItem alloc] initWithItemIdentifier:mtzId];
            [mtzItem setLabel:@"Auto-open MTZ"];
            [mtzItem setPaletteLabel:@"Auto-open MTZ"];
            [mtzItem setTarget:[BandicootCustomToolbarTarget shared]];
            [mtzItem setAction:@selector(autoOpenMtz:)];
            if (img) [mtzItem setImage:img];
            [delegate.itemIdentifiers addObject:mtzId];
            delegate.itemsById[mtzId] = mtzItem;
        }
    }
    g_list_free(children);

    NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"bandicoot.main"];
    toolbar.delegate = delegate;
    toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
    toolbar.sizeMode = NSToolbarSizeModeRegular;
    toolbar.allowsUserCustomization = NO;
    toolbar.autosavesConfiguration = NO;

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

extern "C" void bandicoot_float_widget_in_window(GtkWidget *widget, const char *title) {
    if (!widget) return;

    GtkWidget *parent_toplevel = gtk_widget_get_toplevel(widget);

    GtkWidget *floater = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(floater), title ? title : "Tools");
    gtk_window_set_resizable(GTK_WINDOW(floater), TRUE);

    if (parent_toplevel && GTK_IS_WINDOW(parent_toplevel)) {
        gtk_window_set_transient_for(GTK_WINDOW(floater),
                                     GTK_WINDOW(parent_toplevel));

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

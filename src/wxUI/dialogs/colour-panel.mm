// src/wxUI/dialogs/colour-panel.mm

#include "wxUI/dialogs/colour-panel.hh"

#import <AppKit/AppKit.h>

// The panel's target while one use of it is open.
@interface BandicootColourTarget : NSObject {
@public
   std::function<void(const wxui::PanelColour &)> changed;
   std::function<void(bool)> done;
   bool active;
}
- (void)colourChanged:(id)sender;
- (void)ok:(id)sender;
- (void)cancel:(id)sender;
- (void)panelClosing:(NSNotification *)note;
- (void)finish:(bool)kept;
@end

@implementation BandicootColourTarget

- (void)colourChanged:(id)sender {
   if (! active || ! changed) return;
   NSColor *c = [[(NSColorPanel *) sender color] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
   if (! c) return;
   wxui::PanelColour pc;
   pc.r = c.redComponent; pc.g = c.greenComponent; pc.b = c.blueComponent;
   changed(pc);
}

- (void)ok:(id)sender {
   [self finish:true];
   [[NSColorPanel sharedColorPanel] orderOut:nil];
}

- (void)cancel:(id)sender {
   [self finish:false];
   [[NSColorPanel sharedColorPanel] orderOut:nil];
}

- (void)panelClosing:(NSNotification *)note {
   [self finish:true];
}

- (void)finish:(bool)kept {
   if (! active) return;
   active = false;
   NSColorPanel *panel = [NSColorPanel sharedColorPanel];
   [panel setTarget:nil];
   [panel setAction:nil];
   std::function<void(bool)> d = done;
   changed = nullptr;
   done = nullptr;
   if (d) d(kept);
}

@end

namespace wxui {

namespace {

BandicootColourTarget *target = nil;
NSView *buttons = nil;

NSButton *panel_button(NSString *title, SEL action, NSRect frame) {
   NSButton *b = [[NSButton alloc] initWithFrame:frame];
   [b setTitle:title];
   [b setBezelStyle:NSBezelStyleRounded];
   [b setTarget:target];
   [b setAction:action];
   return [b autorelease];
}

// Cancel and OK at the bottom right, under the colour wheel.
NSView *make_buttons() {
   NSView *v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 220, 44)];
   [v setAutoresizingMask:NSViewWidthSizable];
   NSButton *cancel = panel_button(@"Cancel", @selector(cancel:), NSMakeRect(20, 8, 96, 28));
   NSButton *ok = panel_button(@"OK", @selector(ok:), NSMakeRect(118, 8, 96, 28));
   [cancel setAutoresizingMask:NSViewMinXMargin];
   [ok setAutoresizingMask:NSViewMinXMargin];
   [ok setKeyEquivalent:@"\r"];
   [v addSubview:cancel];
   [v addSubview:ok];
   return v;
}

} // namespace

void show_colour_panel(const char *title, const PanelColour &initial,
                       std::function<void(const PanelColour &)> changed,
                       std::function<void(bool kept)> done) {
   NSColorPanel *panel = [NSColorPanel sharedColorPanel];
   if (! target) {
      target = [[BandicootColourTarget alloc] init];
      buttons = make_buttons();
      [[NSNotificationCenter defaultCenter] addObserver:target selector:@selector(panelClosing:)
                                                   name:NSWindowWillCloseNotification object:panel];
   }
   [target finish:true];

   // the starting colour is set before the target, so setting it is not a change
   [panel setShowsAlpha:NO];
   [panel setColor:[NSColor colorWithSRGBRed:initial.r green:initial.g blue:initial.b alpha:1.0]];
   target->changed = changed;
   target->done = done;
   target->active = true;
   [panel setTitle:[NSString stringWithUTF8String:title]];
   [panel setContinuous:YES];
   [panel setAccessoryView:buttons];
   [panel setTarget:target];
   [panel setAction:@selector(colourChanged:)];
   [panel makeKeyAndOrderFront:nil];
}

} // namespace wxui

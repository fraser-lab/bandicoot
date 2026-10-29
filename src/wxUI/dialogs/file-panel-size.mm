// src/wxUI/dialogs/file-panel-size.mm

#include "wxUI/dialogs/file-panel-size.hh"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <algorithm>

namespace wxui {

namespace {

NSRect screen_area() {
   NSScreen *s = [NSScreen mainScreen];
   return s ? s.visibleFrame : NSMakeRect(0, 0, 1440, 900);
}

// marks a panel already sized, so a user's own resize is kept
char sized_key;

} // namespace


void limit_file_panel_size() {
   static id observer = nil;
   if (observer) return;
   observer = [[NSNotificationCenter defaultCenter]
      addObserverForName:NSWindowDidBecomeKeyNotification object:nil queue:nil
      usingBlock:^(NSNotification *note) {
         NSWindow *w = note.object;
         if (! [w isKindOfClass:[NSSavePanel class]]) return;
         if (objc_getAssociatedObject(w, &sized_key)) return;
         objc_setAssociatedObject(w, &sized_key, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
         NSRect area = screen_area();
         NSRect f = w.frame;
         CGFloat width  = std::max(std::min(NSWidth(f), NSWidth(area) / 4.0), w.minSize.width);
         CGFloat height = std::max(std::min(NSHeight(f), NSHeight(area) / 3.0), w.minSize.height);
         if (width < NSWidth(f) || height < NSHeight(f)) {
            // shrunk about its centre
            NSRect g = NSMakeRect(NSMidX(f) - width / 2, NSMidY(f) - height / 2, width, height);
            [w setFrame:g display:YES animate:NO];
         }
      }];
}

} // namespace wxui

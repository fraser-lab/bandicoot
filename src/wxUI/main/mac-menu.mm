// src/wxUI/main/mac-menu.mm

#include "wxUI/main/mac-menu.hh"

#include <wx/menu.h>

#import <AppKit/AppKit.h>

namespace wxui {

void mac_strip_stock_edit_items(wxMenu *edit) {

   NSMenu *menu = (NSMenu *) edit->GetHMenu();
   if (! menu) return;

   // AppKit adds its items whenever the menu bar is installed, so remove them
   // as they arrive. Ours are the items present now plus anything wx adds later
   // (wx items are wxNSMenuItem; its separators are plain, so those are judged
   // by whether they were here at the start).
   NSHashTable *ours = [NSHashTable weakObjectsHashTable];
   for (NSMenuItem *i in menu.itemArray) [ours addObject:i];
   Class wx_item = NSClassFromString(@"wxNSMenuItem");

   __block bool pending = false;
   auto strip = ^{
      pending = false;
      for (NSMenuItem *i in [menu.itemArray copy]) {
         if ([ours containsObject:i]) continue;
         if (wx_item && [i isKindOfClass:wx_item]) continue;
         [menu removeItem:i];
      }
   };
   // Removal waits for the next main-loop pass: AppKit inserts several items
   // by index in one go, and removing any of them mid-way makes the next
   // insertion fail.
   [[NSNotificationCenter defaultCenter]
      addObserverForName:NSMenuDidAddItemNotification object:menu queue:nil
              usingBlock:^(NSNotification *) {
                 if (pending) return;
                 pending = true;
                 dispatch_async(dispatch_get_main_queue(), strip);
              }];
}

} // namespace wxui

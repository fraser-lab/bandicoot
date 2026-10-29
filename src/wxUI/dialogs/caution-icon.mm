// src/wxUI/dialogs/caution-icon.mm

#include "wxUI/dialogs/caution-icon.hh"

#include <wx/bitmap.h>

#import <AppKit/AppKit.h>

namespace wxui {

wxBitmapBundle caution_icon(int size) {
   NSImage *system = [NSImage imageNamed:NSImageNameCaution];
   if (! system) return wxBitmapBundle();
   // a copy, so the shared system image keeps its own size
   NSImage *sign = [[system copy] autorelease];
   [sign setSize:NSMakeSize(size, size)];
   wxBitmap bitmap(sign);
   if (! bitmap.IsOk()) return wxBitmapBundle();
   return wxBitmapBundle::FromBitmap(bitmap);
}

} // namespace wxui

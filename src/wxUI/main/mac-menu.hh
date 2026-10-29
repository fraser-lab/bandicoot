// src/wxUI/main/mac-menu.hh
//
// macOS adjustments to the native menu bar and toolbar that wx has no API for.

#ifndef BANDICOOT_WXUI_MAC_MENU_HH
#define BANDICOOT_WXUI_MAC_MENU_HH

#include <functional>

class wxFrame;
class wxMenu;

namespace wxui {

// Keep macOS from adding its text-editing items (AutoFill, Start Dictation,
// Emoji & Symbols) to the Edit menu: none of them applies to Bandicoot's Edit
// menu, which edits molecules, not text. Call once, before the menu bar is
// installed.
void mac_strip_stock_edit_items(wxMenu *edit);

// Call show_menu, once the click has been handled, when the toolbar area of
// frame's window (title bar and toolbar, above the content) is right-clicked
// or Control-clicked. The native toolbar does not report these to wx.
void mac_on_toolbar_right_click(wxFrame *frame, const std::function<void()> &show_menu);

// Put the window title on its own row above the toolbar (the "expanded"
// style), as the GTK build does, instead of in line with the tools.
void mac_title_above_toolbar(wxFrame *frame);

} // namespace wxui

#endif // BANDICOOT_WXUI_MAC_MENU_HH

// src/wxUI/main/mac-menu.hh
//
// macOS adjustments to the native menu bar that wx has no API for.

#ifndef BANDICOOT_WXUI_MAC_MENU_HH
#define BANDICOOT_WXUI_MAC_MENU_HH

class wxMenu;

namespace wxui {

// Keep macOS from adding its text-editing items (AutoFill, Start Dictation,
// Emoji & Symbols) to the Edit menu: none of them applies to Bandicoot's Edit
// menu, which edits molecules, not text. Call once, before the menu bar is
// installed.
void mac_strip_stock_edit_items(wxMenu *edit);

} // namespace wxui

#endif // BANDICOOT_WXUI_MAC_MENU_HH

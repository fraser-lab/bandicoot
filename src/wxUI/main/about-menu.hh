// src/wxUI/main/about-menu.hh
//
// The About menu's windows: Header/Remarks Browser, Hints, and About with
// its Credits, References and License windows.

#ifndef BANDICOOT_WXUI_ABOUT_MENU_HH
#define BANDICOOT_WXUI_ABOUT_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_about_action(const std::string &action);
// False if action is not one of these.
bool run_about_action(wxWindow *parent, const std::string &action);

} // namespace wxui

#endif // BANDICOOT_WXUI_ABOUT_MENU_HH

// src/wxUI/main/measures-menu.hh
//
// The Measures menu's windows: Distances & Angles, Environment Distances,
// Plane Distances and Pointer Distances. Residue Info is Edit's window.

#ifndef BANDICOOT_WXUI_MEASURES_MENU_HH
#define BANDICOOT_WXUI_MEASURES_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_measures_action(const std::string &action);
// False if action is not one of these.
bool run_measures_action(wxWindow *parent, const std::string &action);

// The core has finished a Distances & Angles pick: un-press its button.
void release_geometry_toggle(const std::string &kind);

} // namespace wxui

#endif // BANDICOOT_WXUI_MEASURES_MENU_HH

// src/wxUI/main/calculate-menu.hh
//
// The Calculate menu's actions and windows: Map Sharpening/Blurring, Map
// Skeleton, NCS Maps, Updating Maps, Move Molecule Here, LSQ and SSM
// Superpose, Align & Mutate, Mutate Residue Range, Fit Loop by Rama Search,
// Frames/Sec, Load Tutorial Model and Data, Run Script, the Python console and
// PanDDA Inspect.

#ifndef BANDICOOT_WXUI_CALCULATE_MENU_HH
#define BANDICOOT_WXUI_CALCULATE_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_calculate_action(const std::string &action);
// False if action is not one of these.
bool run_calculate_action(wxWindow *parent, const std::string &action);

} // namespace wxui

#endif // BANDICOOT_WXUI_CALCULATE_MENU_HH

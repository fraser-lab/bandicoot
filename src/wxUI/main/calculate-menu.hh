// src/wxUI/main/calculate-menu.hh
//
// The Calculate menu's actions and windows: Map Sharpening/Blurring, Map
// Skeleton, NCS Maps, Updating Maps, Move Molecule Here, LSQ and SSM
// Superpose, Align & Mutate, Mutate Residue Range, Fit Loop by Rama Search,
// Frames/Sec, Load Tutorial Model and Data, Run Script, the Python console and
// PanDDA Inspect.

#ifndef BANDICOOT_WXUI_CALCULATE_MENU_HH
#define BANDICOOT_WXUI_CALCULATE_MENU_HH

#include <functional>
#include <string>

#include <wx/string.h>

class wxWindow;

namespace wxui {

bool is_calculate_action(const std::string &action);
// False if action is not one of these.
bool run_calculate_action(wxWindow *parent, const std::string &action);

// Map Skeleton, with an optional note above it; after_ok runs once the
// choice is applied (null for none).
void show_map_skeleton(wxWindow *parent, const wxString &note, std::function<void()> after_ok);

} // namespace wxui

#endif // BANDICOOT_WXUI_CALCULATE_MENU_HH

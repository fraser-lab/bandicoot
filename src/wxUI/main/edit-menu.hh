// src/wxUI/main/edit-menu.hh
//
// The Edit menu's actions and windows: Change Chain IDs, Copy Molecule,
// Copy Fragment, Merge Molecules, Renumber Residues, Residue Info and the
// display settings (bond and map colours and parameters, skeleton, font,
// pointer and background).

#ifndef BANDICOOT_WXUI_EDIT_MENU_HH
#define BANDICOOT_WXUI_EDIT_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_edit_action(const std::string &action);
// False if action is not one of these.
bool run_edit_action(wxWindow *parent, const std::string &action);

// The colour panel for a map: the map follows it, Cancel puts the colour back.
void show_map_colour(wxWindow *parent, int imol);

// Lets the core open Residue Info once an atom is picked.
void install_edit_menu(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_EDIT_MENU_HH

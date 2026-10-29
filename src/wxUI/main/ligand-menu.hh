// src/wxUI/main/ligand-menu.hh
//
// The Ligand menu's windows: Find Ligands, SMILES -> simple 3D, Rename Atom
// to Reference, and Quick Ligand Validate's report. Its items that act at once
// go to the core directly (bandicoot_core_ligand_direct).

#ifndef BANDICOOT_WXUI_LIGAND_MENU_HH
#define BANDICOOT_WXUI_LIGAND_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_ligand_action(const std::string &action);
// False if action is not one of these.
bool run_ligand_action(wxWindow *parent, const std::string &action);

// Find Ligands (also opened from Other Modelling Tools).
void show_find_ligands(wxWindow *parent);
// Quick Ligand Validate's report: each test 0 passed, 1 failed, 2 not done.
void show_ligand_check(wxWindow *parent, const std::string &residue, int density, int geometry, int bumps);

} // namespace wxui

#endif // BANDICOOT_WXUI_LIGAND_MENU_HH

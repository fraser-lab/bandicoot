// src/wxUI/main/modelling-menu.hh
//
// The Modelling menu's windows: Add N-linked Glycan (Glyco), the model
// choosers (Arrange Waters ... Use SEGIDs), the chooser + entry forms
// (Monomer from Dictionary ... Fetch PDBe Ligand Description), the fitting
// and sequence forms (Rigid Body Fit Residue Range ... Cootaneer), Add Other
// Solvent Molecules, and Ligand Restraints, which coordinate loads open too. Its items that act at once go
// to the core directly (bandicoot_core_modelling_direct).

#ifndef BANDICOOT_WXUI_MODELLING_MENU_HH
#define BANDICOOT_WXUI_MODELLING_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_modelling_action(const std::string &action);
// False if action is not one of these.
bool run_modelling_action(wxWindow *parent, const std::string &action);

// Lets the core open Ligand Restraints after a coordinate load.
void install_modelling_menu(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_MODELLING_MENU_HH

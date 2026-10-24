// src/wxUI/main/other-tools.hh
//
// Other Modelling Tools (Modelling menu): one window of buttons, some of which
// wait for atom picks, plus the windows they open (secondary structure and
// nucleic acid building, Baton mode, Add OXT, Place Strand Here, Multi-Residue
// Torsion, Ligand from SMILES).

#ifndef BANDICOOT_WXUI_OTHER_TOOLS_HH
#define BANDICOOT_WXUI_OTHER_TOOLS_HH

#include <string>
#include <vector>

class wxWindow;

namespace wxui {

void show_other_tools(wxWindow *parent);

// The core is done with a pick: un-press its toggle (by Coot's widget name).
void release_tools_toggle(const std::string &widget);

// Multi-Residue Torsion's rotatable bonds, by label.
void show_multi_torsion(wxWindow *parent, const std::vector<std::string> &bonds);

// Asks for a SMILES string and a 3-letter code; false if cancelled.
bool ask_ligand_from_smiles(wxWindow *parent, std::string &smiles, std::string &code);

} // namespace wxui

#endif // BANDICOOT_WXUI_OTHER_TOOLS_HH

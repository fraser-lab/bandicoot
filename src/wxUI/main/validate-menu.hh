// src/wxUI/main/validate-menu.hh
//
// The Validate menu's checks that end in a list (Incorrect Chiral Volumes,
// Unmodelled blobs, Difference Map Peaks, Check/Delete Waters, GLN and ASN
// B-factor Outliers), and the way into its graphs and plots.

#ifndef BANDICOOT_WXUI_VALIDATE_MENU_HH
#define BANDICOOT_WXUI_VALIDATE_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_validate_action(const std::string &action);
// False if action is not one of these.
bool run_validate_action(wxWindow *parent, const std::string &action);

// The molecule submenus these checks and the residue graphs fill (by Coot's
// menu name), and running one on the molecule chosen from it.
bool is_validate_molecule_list(const std::string &list);
void run_validate_molecule(wxWindow *parent, const std::string &list, int imol);

// The . and , keys: step the open peak or questionable-water list by one row.
// False if neither is open.
bool step_validate_list(int step);

} // namespace wxui

#endif // BANDICOOT_WXUI_VALIDATE_MENU_HH

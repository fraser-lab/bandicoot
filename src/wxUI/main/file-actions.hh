// src/wxUI/main/file-actions.hh
//
// The File menu's actions (and the toolbar's Open Coords, Auto-open MTZ and
// Open Map, which are the same): file choosers that hand the chosen file to
// the core, and the windows in file-menu.hh.

#ifndef BANDICOOT_WXUI_FILE_ACTIONS_HH
#define BANDICOOT_WXUI_FILE_ACTIONS_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_file_action(const std::string &action);
// False if action is not one of these.
bool run_file_action(wxWindow *parent, const std::string &action);

// Lets the core open the save panel once a symmetry atom is picked.
void install_file_actions(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_FILE_ACTIONS_HH

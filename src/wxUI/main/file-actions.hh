// src/wxUI/main/file-actions.hh
//
// Actions that open a file chooser and hand the chosen file to the core:
// the toolbar's Open Coords, Auto-open MTZ and Open Map.

#ifndef BANDICOOT_WXUI_FILE_ACTIONS_HH
#define BANDICOOT_WXUI_FILE_ACTIONS_HH

#include <string>

class wxWindow;

namespace wxui {

// False if action is not one of these.
bool run_file_action(wxWindow *parent, const std::string &action);

} // namespace wxui

#endif // BANDICOOT_WXUI_FILE_ACTIONS_HH

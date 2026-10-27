// src/wxUI/main/draw-menu.hh
//
// The Draw menu's windows: Anisotropic Atoms, Additional Representation, Cell & Symmetry,
// Generic Display Objects, NCS Ghost Control and the screenshots. Its items that act at once go to
// the core directly (bandicoot_core_draw_direct).

#ifndef BANDICOOT_WXUI_DRAW_MENU_HH
#define BANDICOOT_WXUI_DRAW_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

bool is_draw_action(const std::string &action);
// False if action is not one of these.
bool run_draw_action(wxWindow *parent, const std::string &action);

} // namespace wxui

#endif // BANDICOOT_WXUI_DRAW_MENU_HH

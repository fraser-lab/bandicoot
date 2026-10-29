// src/wxUI/dialogs/colour-panel.hh
//
// The system colour panel, used the way Coot used its colour selector: the
// colour applies as it is picked, OK keeps it and Cancel puts the old one
// back. The panel does not block, so the graphics follow the choice.

#ifndef BANDICOOT_WXUI_COLOUR_PANEL_HH
#define BANDICOOT_WXUI_COLOUR_PANEL_HH

#include <functional>

namespace wxui {

struct PanelColour { float r = 0, g = 0, b = 0; };  // 0 to 1

// changed follows the colour as it is picked. done(true) on OK or when the
// panel is closed, done(false) on Cancel. Opening the panel again while it
// is in use finishes the earlier use as OK.
void show_colour_panel(const char *title, const PanelColour &initial,
                       std::function<void(const PanelColour &)> changed,
                       std::function<void(bool kept)> done);

} // namespace wxui

#endif // BANDICOOT_WXUI_COLOUR_PANEL_HH

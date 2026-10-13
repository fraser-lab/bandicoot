// src/wxUI/main/top-toolbar.hh
//
// The top toolbar, a native toolbar with Bandicoot's default set of buttons
// (as the GTK build shows it before any customisation). Like the main menu,
// each button carries an action name -- the Coot handler or Bandicoot action
// it stands for -- which is what its owner acts on.

#ifndef BANDICOOT_WXUI_TOP_TOOLBAR_HH
#define BANDICOOT_WXUI_TOP_TOOLBAR_HH

#include <functional>
#include <string>

class wxFrame;

namespace wxui {

// state: for a toggle, its new state; for a push button, true
typedef std::function<void(const std::string &action, const std::string &label,
                           bool state)> ToolbarHandler;

void build_top_toolbar(wxFrame *frame, const ToolbarHandler &on_select);

} // namespace wxui

#endif // BANDICOOT_WXUI_TOP_TOOLBAR_HH

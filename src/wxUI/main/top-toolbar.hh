// src/wxUI/main/top-toolbar.hh
//
// The top toolbar, a native toolbar. Five tools always lead it; after them
// come the tools the user has arranged (Bandicoot's default set until they
// do), saved with the display style. A right-click on it offers the style and
// Customize Toolbar..., which opens the drag-and-drop customizer. Like the
// main menu, each button carries an action name -- the Coot handler or
// Bandicoot action it stands for -- which is what its owner acts on.

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
// Customize Toolbar... (the right-click menu and the application menu)
void customize_top_toolbar();
// Set the toggle that stands for action to on or off, without acting on it --
// for a state changed some other way (e.g. leaving full screen with Escape).
void set_top_toolbar_toggle(const std::string &action, bool on);

} // namespace wxui

#endif // BANDICOOT_WXUI_TOP_TOOLBAR_HH

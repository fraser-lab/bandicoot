// src/wxUI/main/display-manager.hh
//
// Coot's Display Manager: a row per map (Display, Scroll, Properties, Delete)
// above a row per model (Display, Active, Render As, Delete), with "All" and
// "Last Only" controls for each list. One window, kept and reused; the core
// tells it when molecules come and go and when their state changes.

#ifndef BANDICOOT_WXUI_DISPLAY_MANAGER_HH
#define BANDICOOT_WXUI_DISPLAY_MANAGER_HH

class wxWindow;

namespace wxui {

// Creates the window and connects it to the core.
void install_display_manager(wxWindow *parent);
// Shows it (F7, the toolbar button).
void show_display_manager();

} // namespace wxui

#endif // BANDICOOT_WXUI_DISPLAY_MANAGER_HH

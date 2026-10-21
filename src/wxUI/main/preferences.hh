// src/wxUI/main/preferences.hh
//
// Edit > Preferences...: categories down the side, each a set of tabs, as in
// Coot. Settings apply as they are changed; OK also saves them.

#ifndef BANDICOOT_WXUI_PREFERENCES_HH
#define BANDICOOT_WXUI_PREFERENCES_HH

#include <functional>

class wxWindow;

namespace wxui {

// What the main window owns: where the sidebar is, how big its icons are,
// and the sequence views.
struct PreferencesHooks {
   std::function<bool()> sidebar_docked;
   std::function<void(bool docked)> set_sidebar_docked;
   std::function<int()> sidebar_side;               // 0 right, 1 left, 3 bottom
   std::function<void(int side)> set_sidebar_side;  // docks it there
   std::function<void(int points)> set_sidebar_icon_size;
   std::function<void(bool docked)> sequence_view_docking_changed;
   std::function<void()> sequence_view_rows_changed;
};

// One window; asking again brings it forward.
void show_preferences(wxWindow *parent, const PreferencesHooks &hooks);

} // namespace wxui

#endif // BANDICOOT_WXUI_PREFERENCES_HH

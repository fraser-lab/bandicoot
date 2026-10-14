// src/wxUI/main/sidebar.hh
//
// The sidebar (modelling toolbar): Coot's model toolbar as Bandicoot shows it
// -- R/RC and Map at the top, the modelling tools, Run Refmac, and the
// Settings button at the bottom. Built from the custom assets. Laid out
// vertically for the left or right edge, horizontally for the bottom.
//
// Each button carries an action name (the Coot handler it stands for), which
// is what the owner acts on. Which tools are shown is the core's to say (the
// Settings menu's Main / All Icons, and Preferences); the sidebar asks for it
// through the hooks.

#ifndef BANDICOOT_WXUI_SIDEBAR_HH
#define BANDICOOT_WXUI_SIDEBAR_HH

#include <wx/panel.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class wxBoxSizer;
class wxSizerItem;
class wxScrolledWindow;

namespace wxui {

class BevelButton;

class Sidebar : public wxPanel {
public:
   // Coot's model toolbar styles, in its numbering
   enum class Style { Icons = 1, IconsAndText = 2, Text = 3 };

   // a tool, by Coot's widget name, and whether it is shown now and by default
   struct IconState { std::string widget; bool shown; bool shown_by_default; };

   struct Hooks {
      // state: for a toggle, its new state; for a push button, true
      std::function<void(const std::string &action, const std::string &label, bool state)> on_select;
      std::function<std::vector<IconState>()> icon_states;
      std::function<void(bool main_icons_only)> choose_icon_set;
      // whether the sidebar is in the main window, and a request to move it
      // out of (or back into) it; unset: no Dock/Undock item
      std::function<bool()> is_docked;
      std::function<void()> toggle_docked;
      // the side it docks on (Coot's numbering: 0 right, 1 left, 3 bottom),
      // and a request to dock on a given side; unset: no side items
      std::function<int()> dock_side;
      std::function<void(int side)> dock_on;
   };

   Sidebar(wxWindow *parent, bool vertical, Style style, Hooks hooks);

   void set_style(Style style);
   Style get_style() const { return style; }

   // Show and hide tools as the core says.
   void refresh_icons();

   // The size that shows every tool without scrolling.
   wxSize natural_size() const;

private:
   bool vertical;
   Style style;
   Hooks hooks;
   int rot_trans_mode = 0; // 0 residue range, 1 chain, 2 molecule

   wxScrolledWindow *area = nullptr;
   wxBoxSizer *tools = nullptr;
   wxSizerItem *tools_item = nullptr;
   int scroll_room = 0;  // kept clear beside the tools for the scroll bar
   BevelButton *settings = nullptr;
   std::vector<std::pair<std::string, wxWindow *>> named; // Coot widget name -> control

   void populate();
   void relayout();
   void update_scroll_room();
   void show_settings_menu();
   void show_rot_trans_menu(wxWindow *anchor);
};

} // namespace wxui

#endif // BANDICOOT_WXUI_SIDEBAR_HH

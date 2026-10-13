// src/wxUI/main/main-menu.hh
//
// The main menu: Bandicoot's menu as it stands in the GTK build (Coot 0.9's
// menus plus Bandicoot's Modelling, Glyco and Ligand menus, without the items
// Bandicoot suppresses), held as data and built into a wxMenuBar -- the
// top-of-screen menu on macOS, an in-window one elsewhere.
//
// Each item carries an action name: the Coot handler or Bandicoot operation it
// stands for (see callbacks.c and c-interface-build-gui.cc), which is what the
// owner of the menu acts on.

#ifndef BANDICOOT_WXUI_MAIN_MENU_HH
#define BANDICOOT_WXUI_MAIN_MENU_HH

#include <functional>
#include <string>
#include <vector>

class wxMenuBar;

namespace wxui {

struct MenuEntry {
   enum class Kind { Item, Check, Radio, Separator, Submenu, MoleculeSubmenu };
   Kind kind = Kind::Item;
   std::string label;
   // an item: what it does; a molecule submenu: which list it shows
   std::string action;
   // a capability the core must have for the item to appear, e.g.
   // "enhanced_ligand_tools"; empty: always shown
   std::string needs;
   bool checked = false;
   std::vector<MenuEntry> items;
};

const std::vector<MenuEntry> &main_menu_entries();

struct MainMenuHooks {
   // path is the item's place in the menu, e.g. "File > Open Coordinates..."
   std::function<void(const std::string &action, const std::string &path)> on_select;
   // unset: every item is shown
   std::function<bool(const std::string &capability)> has_capability;
};

wxMenuBar *build_main_menu(const MainMenuHooks &hooks);

} // namespace wxui

#endif // BANDICOOT_WXUI_MAIN_MENU_HH

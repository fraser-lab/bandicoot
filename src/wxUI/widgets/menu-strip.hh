// src/wxUI/widgets/menu-strip.hh
//
// MenuStrip: a menu bar inside a window, as GTK draws one: flat titles that
// rise under the pointer, each opening its menu just below it. A window's own
// menu bar cannot be used, because on macOS it would replace the application's
// main menu while the window is in front.
//
// A title's mnemonic letter (marked with '&', e.g. "&File") is underlined, and
// Alt/Option with that letter opens the menu, once the owner passes its key
// presses to handle_key().

#ifndef BANDICOOT_WXUI_MENU_STRIP_HH
#define BANDICOOT_WXUI_MENU_STRIP_HH

#include <wx/panel.h>

#include <functional>
#include <vector>

class wxBoxSizer;
class wxMenu;
class wxKeyEvent;

namespace wxui {

class MenuTitle;

class MenuStrip : public wxPanel {
public:
   explicit MenuStrip(wxWindow *parent);

   // make() builds the menu afresh each time it opens, so it can show the
   // current state; the strip deletes it after use.
   void add(const wxString &label, std::function<wxMenu *()> make);

   // For the owner's wxEVT_CHAR_HOOK: true when the key opened a menu.
   bool handle_key(const wxKeyEvent &e);

private:
   std::vector<MenuTitle *> titles;
   wxBoxSizer *row;

   void on_paint(wxPaintEvent &e);
};

} // namespace wxui

#endif // BANDICOOT_WXUI_MENU_STRIP_HH

// src/wxUI/main/sidebar.hh
//
// The sidebar (modelling toolbar): Coot's model toolbar as Bandicoot shows it
// -- R/RC and Map at the top, the modelling tools, Run Refmac, and the
// Settings button at the bottom. Built from the custom assets. Laid out
// vertically for the left or right edge, horizontally for the bottom.
//
// Each button carries an action name (the Coot handler it stands for), which
// is what the owner acts on.

#ifndef BANDICOOT_WXUI_SIDEBAR_HH
#define BANDICOOT_WXUI_SIDEBAR_HH

#include <wx/panel.h>

#include <functional>
#include <string>

namespace wxui {

class Sidebar : public wxPanel {
public:
   // Coot's model toolbar styles, in its numbering
   enum class Style { Icons = 1, IconsAndText = 2, Text = 3 };

   // state: for a toggle, its new state; for a push button, true
   typedef std::function<void(const std::string &action, const std::string &label,
                              bool state)> Handler;

   Sidebar(wxWindow *parent, bool vertical, Style style, Handler on_select);

private:
   bool vertical;
   Style style;
   Handler on_select;
   int rot_trans_mode = 0; // 0 residue range, 1 chain, 2 molecule

   void populate(wxWindow *area, wxSizer *sizer);
   void show_rot_trans_menu(wxWindow *anchor);
};

} // namespace wxui

#endif // BANDICOOT_WXUI_SIDEBAR_HH

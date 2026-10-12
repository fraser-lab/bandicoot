// src/wxUI/assets/bevel-button.hh
//
// BevelButton: Coot 0.9's grey push button (e.g. the sidebar's R/RC and Map,
// the Accept/Reject bar). Raised when idle, lavender under the pointer, sunken
// while pressed; a toggle shows pink while down.

#ifndef BANDICOOT_WXUI_BEVEL_BUTTON_HH
#define BANDICOOT_WXUI_BEVEL_BUTTON_HH

#include "wxUI/assets/button-base.hh"

namespace wxui {

class BevelButton : public ButtonBase {
public:
   BevelButton(wxWindow *parent, wxWindowID id, const wxString &label);

protected:
   void draw(wxDC &dc, const wxRect &rect) override;
   wxSize DoGetBestSize() const override;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_BEVEL_BUTTON_HH

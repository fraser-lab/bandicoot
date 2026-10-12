// src/wxUI/assets/flat-button.hh
//
// FlatButton: the etched row button of Coot 0.9's click-through lists (e.g.
// Check Waters), optionally carrying a radio or check indicator. Lavender under
// the pointer. When on, the indicator is set and the label turns dark red with
// a dotted outline.
//
// A Radio row does not change its own state: whoever owns the group decides
// which row is on (set_on()) after each wxEVT_BUTTON. A Check row toggles itself
// and sends wxEVT_TOGGLEBUTTON.

#ifndef BANDICOOT_WXUI_FLAT_BUTTON_HH
#define BANDICOOT_WXUI_FLAT_BUTTON_HH

#include "wxUI/assets/button-base.hh"
#include "wxUI/assets/bevel.hh"

namespace wxui {

class FlatButton : public ButtonBase {
public:
   FlatButton(wxWindow *parent, wxWindowID id, const wxString &label,
              Indicator indicator = Indicator::None);

protected:
   void draw(wxDC &dc, const wxRect &rect) override;
   wxSize DoGetBestSize() const override;

private:
   Indicator indicator;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_FLAT_BUTTON_HH

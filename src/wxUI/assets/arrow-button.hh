// src/wxUI/assets/arrow-button.hh
//
// ArrowButton: a small bevelled button carrying a filled triangle pointing
// left or right, as a spinner's arrows are drawn. A click sends wxEVT_BUTTON.

#ifndef BANDICOOT_WXUI_ARROW_BUTTON_HH
#define BANDICOOT_WXUI_ARROW_BUTTON_HH

#include "wxUI/assets/button-base.hh"

namespace wxui {

class ArrowButton : public ButtonBase {
public:
   enum class Direction { Left, Right };

   ArrowButton(wxWindow *parent, wxWindowID id, Direction direction);

protected:
   void draw(wxDC &dc, const wxRect &rect) override;
   wxSize DoGetBestSize() const override;

private:
   Direction direction;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_ARROW_BUTTON_HH

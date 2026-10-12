// src/wxUI/assets/flat-button.cc

#include "wxUI/assets/flat-button.hh"
#include "wxUI/assets/palette.hh"

#include <wx/dcclient.h>

#include <algorithm>

namespace wxui {

namespace {
const int pad_x = 6, pad_y = 2, gap = 6; // points, inside the frame
}

FlatButton::FlatButton(wxWindow *parent, wxWindowID id, const wxString &label,
                       Indicator indicator_in)
   : ButtonBase(parent, id, label), indicator(indicator_in) {
   set_toggle(indicator == Indicator::Check);
   SetInitialSize();
}

void FlatButton::draw(wxDC &dc, const wxRect &rect) {

   wxColour face = (look() == Look::Idle) ? palette::face() : palette::face_hover_toggle();
   draw_frame(dc, rect, FrameStyle::Etched, face, palette::shadow_of(palette::face()));

   wxRect inner = frame_interior(rect);
   int x = inner.x + pad_x;
   if (indicator != Indicator::None) {
      draw_indicator(dc, wxRect(x, inner.y, indicator_size, inner.height), indicator, is_on());
      x += indicator_size + gap;
   }

   dc.SetFont(GetFont());
   dc.SetTextForeground(! IsEnabled() ? palette::text_disabled()
                        : is_on()     ? palette::text_on() : palette::text());
   wxSize ts = dc.GetTextExtent(GetLabel());
   int y = inner.y + (inner.height - ts.y) / 2;
   dc.DrawText(GetLabel(), x, y);
   if (is_on())
      draw_focus_outline(dc, wxRect(x - 2, y - 1, ts.x + 4, ts.y + 2), palette::text_on());
}

wxSize FlatButton::DoGetBestSize() const {
   wxClientDC dc(const_cast<FlatButton *>(this));
   dc.SetFont(GetFont());
   wxSize ts = dc.GetTextExtent(GetLabel());
   int w = 2 + pad_x + ts.x + pad_x + 2;
   if (indicator != Indicator::None) w += indicator_size + gap;
   int h = 2 + pad_y + std::max(ts.y, int(indicator_size)) + pad_y + 2;
   return wxSize(w, h);
}

} // namespace wxui

// src/wxUI/assets/bevel-button.cc

#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/dcclient.h>

#include <algorithm>

namespace wxui {

namespace {
const int pad_x = 6, pad_y = 2, min_height = 22; // points, inside the frame
}

BevelButton::BevelButton(wxWindow *parent, wxWindowID id, const wxString &label)
   : ButtonBase(parent, id, label) {
   SetInitialSize();
}

void BevelButton::draw(wxDC &dc, const wxRect &rect) {

   FrameStyle style = FrameStyle::Raised;
   wxColour face = palette::face();
   wxColour text = palette::text();

   Look l = look();
   if (is_on() || (l == Look::Pressed && is_toggle())) {
      style = FrameStyle::Sunken;
      face  = palette::face_on();
      text  = palette::text_on();
   } else if (l == Look::Pressed) {
      style = FrameStyle::Sunken;
      face  = palette::face_pressed();
   } else if (l == Look::Hover) {
      face = is_toggle() ? palette::face_hover_toggle() : palette::face_hover();
   }
   if (! IsEnabled())
      text = palette::text_disabled();

   draw_frame(dc, rect, style, face, palette::shadow_of(face));

   dc.SetFont(GetFont());
   dc.SetTextForeground(text);
   wxRect inner = frame_interior(rect);
   if (style == FrameStyle::Sunken)
      inner.Offset(1, 1); // the label sinks with the button, as in GTK
   dc.DrawLabel(GetLabel(), inner, wxALIGN_CENTRE);
}

wxSize BevelButton::DoGetBestSize() const {
   wxClientDC dc(const_cast<BevelButton *>(this));
   dc.SetFont(GetFont());
   wxSize t = dc.GetMultiLineTextExtent(GetLabel());
   return wxSize(t.x + 2 * (2 + pad_x), std::max(min_height, t.y + 2 * (2 + pad_y)));
}

} // namespace wxui

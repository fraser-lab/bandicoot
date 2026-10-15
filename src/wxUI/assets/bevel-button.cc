// src/wxUI/assets/bevel-button.cc

#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/dcclient.h>

#include <algorithm>

namespace wxui {

namespace {
const int pad_x = 6, pad_y = 2, min_height = 22; // points, inside the frame
const int icon_gap = 8;                          // points, icon to label
}

BevelButton::BevelButton(wxWindow *parent, wxWindowID id, const wxString &label)
   : ButtonBase(parent, id, label) {
   SetInitialSize();
}

void BevelButton::set_icon(const wxBitmapBundle &b) {
   icon = b;
   InvalidateBestSize();
   SetInitialSize();
   Refresh();
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
   if (! icon.IsOk()) {
      dc.DrawLabel(GetLabel(), inner, wxALIGN_CENTRE);
      return;
   }
   // icon and label centred together
   wxBitmap bmp = icon.GetBitmapFor(this);
   wxSize b = bmp.GetLogicalSize();
   wxSize t = dc.GetMultiLineTextExtent(GetLabel());
   int x = inner.x + (inner.width - (b.x + icon_gap + t.x)) / 2;
   dc.DrawBitmap(bmp, x, inner.y + (inner.height - b.y) / 2, true);
   dc.DrawText(GetLabel(), x + b.x + icon_gap, inner.y + (inner.height - t.y) / 2);
}

wxSize BevelButton::DoGetBestSize() const {
   wxClientDC dc(const_cast<BevelButton *>(this));
   dc.SetFont(GetFont());
   wxSize t = dc.GetMultiLineTextExtent(GetLabel());
   if (icon.IsOk()) {
      wxSize b = icon.GetDefaultSize();
      t.x += b.x + icon_gap;
      t.y = std::max(t.y, b.y);
   }
   return wxSize(t.x + 2 * (2 + pad_x), std::max(min_height, t.y + 2 * (2 + pad_y)));
}

} // namespace wxui

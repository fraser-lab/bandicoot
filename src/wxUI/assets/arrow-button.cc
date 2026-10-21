// src/wxUI/assets/arrow-button.cc

#include "wxUI/assets/arrow-button.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/brush.h>
#include <wx/pen.h>

namespace wxui {

namespace {
const int button_size = 18;   // points, square
const int arrow_half = 4;     // points, half the triangle's height
}

ArrowButton::ArrowButton(wxWindow *parent, wxWindowID id, Direction direction_in)
   : ButtonBase(parent, id, wxEmptyString), direction(direction_in) {
   SetInitialSize();
}

void ArrowButton::draw(wxDC &dc, const wxRect &rect) {

   FrameStyle style = FrameStyle::Raised;
   wxColour face = palette::face();
   Look l = look();
   if (l == Look::Pressed) {
      style = FrameStyle::Sunken;
      face  = palette::face_pressed();
   } else if (l == Look::Hover) {
      face = palette::face_hover();
   }
   draw_frame(dc, rect, style, face, palette::shadow_of(face));

   wxRect inner = frame_interior(rect);
   if (style == FrameStyle::Sunken)
      inner.Offset(1, 1); // the arrow sinks with the button, as a label does
   wxPoint c(inner.x + inner.width / 2, inner.y + inner.height / 2);
   // the tip is arrow_half from the centre, the base arrow_half the other way
   int tip = direction == Direction::Left ? -arrow_half : arrow_half;
   wxPoint triangle[3] = {
      wxPoint(c.x + tip, c.y),
      wxPoint(c.x - tip, c.y - arrow_half),
      wxPoint(c.x - tip, c.y + arrow_half)
   };
   wxColour fill = IsEnabled() ? palette::text() : palette::text_disabled();
   dc.SetPen(wxPen(fill));
   dc.SetBrush(wxBrush(fill));
   dc.DrawPolygon(3, triangle);
}

wxSize ArrowButton::DoGetBestSize() const {
   return wxSize(button_size, button_size);
}

} // namespace wxui

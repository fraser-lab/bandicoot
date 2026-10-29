// src/wxUI/assets/bevel.cc

#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/brush.h>
#include <wx/pen.h>

namespace wxui {

namespace {

// one-point lines as filled rectangles, which keeps their ends exact
void hline(wxDC &dc, int x0, int x1, int y, const wxColour &c) {
   if (x1 < x0) return;
   dc.SetPen(*wxTRANSPARENT_PEN);
   dc.SetBrush(wxBrush(c));
   dc.DrawRectangle(x0, y, x1 - x0 + 1, 1);
}

void vline(wxDC &dc, int x, int y0, int y1, const wxColour &c) {
   if (y1 < y0) return;
   dc.SetPen(*wxTRANSPARENT_PEN);
   dc.SetBrush(wxBrush(c));
   dc.DrawRectangle(x, y0, 1, y1 - y0 + 1);
}

} // namespace

void draw_frame(wxDC &dc, const wxRect &r, FrameStyle style,
                const wxColour &face, const wxColour &shadow) {

   dc.SetPen(*wxTRANSPARENT_PEN);
   dc.SetBrush(wxBrush(face));
   dc.DrawRectangle(r);
   if (r.width < 4 || r.height < 4) return;

   const int x0 = r.x, y0 = r.y, x1 = r.GetRight(), y1 = r.GetBottom();
   const wxColour light = palette::highlight();
   const wxColour black = palette::outer_shadow();

   switch (style) {
   case FrameStyle::Raised:
      hline(dc, x0, x1, y0, light);
      vline(dc, x0, y0, y1, light);
      hline(dc, x0 + 1, x1 - 1, y1 - 1, shadow);
      vline(dc, x1 - 1, y0 + 1, y1 - 1, shadow);
      hline(dc, x0, x1, y1, black);
      vline(dc, x1, y0, y1, black);
      break;
   case FrameStyle::Sunken:
      hline(dc, x0, x1, y0, shadow);
      vline(dc, x0, y0, y1, shadow);
      hline(dc, x0 + 1, x1 - 1, y0 + 1, black);
      vline(dc, x0 + 1, y0 + 1, y1 - 1, black);
      hline(dc, x0, x1, y1, light);
      vline(dc, x1, y0, y1, light);
      break;
   case FrameStyle::Etched:
      hline(dc, x0, x1, y0, shadow);
      vline(dc, x0, y0, y1, shadow);
      hline(dc, x0 + 1, x1 - 1, y0 + 1, light);
      vline(dc, x0 + 1, y0 + 1, y1 - 1, light);
      hline(dc, x0 + 1, x1 - 1, y1 - 1, shadow);
      vline(dc, x1 - 1, y0 + 1, y1 - 1, shadow);
      hline(dc, x0, x1, y1, light);
      vline(dc, x1, y0, y1, light);
      break;
   }
}

wxRect frame_interior(const wxRect &r) {
   return wxRect(r).Deflate(2);
}

void draw_indicator(wxDC &dc, const wxRect &box, Indicator kind, bool on) {

   if (kind == Indicator::None) return;
   const int s = indicator_size;
   wxRect b(box.x + (box.width - s) / 2, box.y + (box.height - s) / 2, s, s);

   dc.SetPen(wxPen(palette::indicator_ring(), 1));
   dc.SetBrush(wxBrush(palette::indicator_fill()));
   if (kind == Indicator::Radio) {
      dc.DrawEllipse(b);
      if (on) {
         const int d = 5;
         dc.SetPen(*wxTRANSPARENT_PEN);
         dc.SetBrush(wxBrush(palette::indicator_ring()));
         dc.DrawEllipse(b.x + (s - d) / 2, b.y + (s - d) / 2, d, d);
      }
   } else {
      dc.DrawRectangle(b);
      if (on) {
         dc.SetPen(wxPen(palette::indicator_ring(), 2));
         wxPoint tick[3] = { wxPoint(b.x + 3, b.y + 6), wxPoint(b.x + 5, b.y + 9),
                             wxPoint(b.x + 9, b.y + 3) };
         dc.DrawLines(3, tick);
      }
   }
}

void draw_focus_outline(wxDC &dc, const wxRect &rect, const wxColour &colour) {
   static const wxDash dots[] = { 1, 1 };
   wxPen pen(colour, 1, wxPENSTYLE_USER_DASH);
   pen.SetDashes(2, dots);
   dc.SetPen(pen);
   dc.SetBrush(*wxTRANSPARENT_BRUSH);
   dc.DrawRectangle(rect);
}

} // namespace wxui

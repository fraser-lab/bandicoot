// src/wxUI/assets/toolbar-button.cc

#include "wxUI/assets/toolbar-button.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/dcclient.h>
#include <wx/settings.h>

#include <algorithm>

namespace wxui {

namespace {
const int pad = 3, gap = 2; // points: inside the frame, between icon and label
}

ToolbarButton::ToolbarButton(wxWindow *parent, wxWindowID id, const wxString &label,
                             const wxBitmapBundle &icon_in, Content content_in)
   : ButtonBase(parent, id, label), icon(icon_in), content(content_in) {
   SetToolTip(label);
   SetInitialSize();
}

void ToolbarButton::set_content(Content c) {
   content = c;
   InvalidateBestSize();
   SetInitialSize();
   Refresh();
}

bool ToolbarButton::shows_icon() const {
   return content != Content::Text && icon.IsOk();
}

bool ToolbarButton::shows_text() const {
   return content != Content::Icon;
}

void ToolbarButton::draw(wxDC &dc, const wxRect &rect) {

   // idle: no frame, the parent's background shows through
   dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
   dc.Clear();

   Look l = look();
   bool sunken = is_on() || l == Look::Pressed;
   if (sunken) {
      wxColour face = (is_on() || is_toggle()) ? palette::face_on() : palette::face_pressed();
      draw_frame(dc, rect, FrameStyle::Sunken, face, palette::shadow_of(face));
   } else if (l == Look::Hover) {
      draw_frame(dc, rect, FrameStyle::Raised, palette::face_hover_toggle(),
                 palette::toolbar_hover_shadow());
   }

   wxRect inner = frame_interior(rect).Deflate(pad);
   if (sunken) inner.Offset(1, 1);

   wxBitmap bmp;
   if (shows_icon()) {
      bmp = icon.GetBitmapFor(this);
      if (! IsEnabled()) bmp = bmp.ConvertToDisabled();
   }
   wxSize bs = bmp.IsOk() ? bmp.GetLogicalSize() : wxSize(0, 0);

   // On its own face the text is Coot's colour; with no face it sits on the
   // window's background and takes the system's (which follows dark mode).
   bool flush = ! sunken && l != Look::Hover;
   dc.SetFont(GetFont());
   dc.SetTextForeground(! IsEnabled() ? palette::text_disabled()
                        : is_on()     ? palette::text_on()
                        : flush       ? wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)
                                      : palette::text());
   wxSize ts = shows_text() ? dc.GetTextExtent(GetLabel()) : wxSize(0, 0);

   if (bmp.IsOk() && shows_text() && content == Content::IconBesideText) {
      // left-aligned, so a column of these lines up as a list
      int x = inner.x;
      dc.DrawBitmap(bmp, x, inner.y + (inner.height - bs.y) / 2, true);
      dc.DrawText(GetLabel(), x + bs.x + gap, inner.y + (inner.height - ts.y) / 2);
   } else if (bmp.IsOk() && shows_text()) {
      int h = bs.y + gap + ts.y;
      int y = inner.y + (inner.height - h) / 2;
      dc.DrawBitmap(bmp, inner.x + (inner.width - bs.x) / 2, y, true);
      dc.DrawText(GetLabel(), inner.x + (inner.width - ts.x) / 2, y + bs.y + gap);
   } else if (bmp.IsOk()) {
      dc.DrawBitmap(bmp, inner.x + (inner.width - bs.x) / 2,
                    inner.y + (inner.height - bs.y) / 2, true);
   } else {
      dc.DrawLabel(GetLabel(), inner, wxALIGN_CENTRE);
   }
}

wxSize ToolbarButton::DoGetBestSize() const {

   wxSize bs = shows_icon() ? icon.GetPreferredLogicalSizeFor(this) : wxSize(0, 0);
   wxSize ts(0, 0);
   if (shows_text()) {
      wxClientDC dc(const_cast<ToolbarButton *>(this));
      dc.SetFont(GetFont());
      ts = dc.GetTextExtent(GetLabel());
   }
   wxSize content_size;
   if (shows_icon() && shows_text() && content == Content::IconBesideText)
      content_size = wxSize(bs.x + gap + ts.x, std::max(bs.y, ts.y));
   else if (shows_icon() && shows_text())
      content_size = wxSize(std::max(bs.x, ts.x), bs.y + gap + ts.y);
   else
      content_size = shows_icon() ? bs : ts;
   const int border = 2 + pad;
   return wxSize(content_size.x + 2 * border, content_size.y + 2 * border);
}

} // namespace wxui

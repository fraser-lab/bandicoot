// src/wxUI/main/validation-report.cc

#include "wxUI/main/validation-report.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/dialogs/click-through-dialog.hh"
#include "wxUI/assets/palette.hh"

#include <wx/dcbuffer.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include <memory>
#include <string>
#include <vector>

namespace wxui {

namespace {

// The percentile sliders, laid out as the wwPDB report draws them.
class PercentileChart : public wxPanel {
public:
   PercentileChart(wxWindow *parent, const bandicoot_validation_report &r)
      : wxPanel(parent), metrics(r.metrics), footnote(r.footnote) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      int n = int(metrics.size());
      SetMinSize(FromDIP(wxSize(width, top + n * row_step + (footnote.empty() ? 76 : 96))));
      Bind(wxEVT_PAINT, &PercentileChart::on_paint, this);
   }

private:
   static constexpr int width = 600, top = 40, row_step = 36;
   static constexpr int label_right = 180, bar_x = 192, bar_w = 320, bar_h = 14;
   static constexpr int marker_w = 6, marker_h = 22;

   std::vector<bandicoot_validation_report::metric> metrics;
   std::string footnote;

   int x_for(float percent) const { return FromDIP(bar_x) + int(0.01f * percent * FromDIP(bar_w)); }
   int bar_top(int row) const { return FromDIP(top + row * row_step); }

   void marker(wxDC &dc, float percent, int row, bool filled) const {
      int w = FromDIP(marker_w), h = FromDIP(marker_h);
      int y = bar_top(row) + (FromDIP(bar_h) - h) / 2;
      dc.SetPen(wxPen(palette::text(), FromDIP(2)));
      dc.SetBrush(filled ? wxBrush(palette::text()) : wxBrush(*wxWHITE));
      dc.DrawRectangle(x_for(percent) - w / 2, y, w, h);
   }

   void on_paint(wxPaintEvent &) {
      wxAutoBufferedPaintDC dc(this);
      dc.SetBackground(wxBrush(*wxWHITE));
      dc.Clear();
      dc.SetTextForeground(palette::text());
      wxFont base = GetFont();
      int bx = FromDIP(bar_x), bw = FromDIP(bar_w), bh = FromDIP(bar_h);

      wxFont heading = base.Larger().Larger();
      dc.SetFont(heading);
      auto centred = [&dc](const wxString &s, int cx, int y) {
         dc.DrawText(s, cx - dc.GetTextExtent(s).x / 2, y);
      };
      centred("Metric", FromDIP(label_right) / 2, FromDIP(6));
      centred("Percentile Ranks", bx + bw / 2, FromDIP(6));
      dc.DrawText("Value", bx + bw + FromDIP(10), FromDIP(6));

      dc.SetFont(base.Larger());
      for (size_t i = 0; i < metrics.size(); i++) {
         int y = bar_top(int(i));
         wxRect left(bx, y, bw / 2, bh), right(bx + bw / 2, y, bw - bw / 2, bh);
         wxColour middle(0xe6, 0xe6, 0xe6);
         dc.GradientFillLinear(left, wxColour(0xff, 0x00, 0x00), middle, wxEAST);
         dc.GradientFillLinear(right, middle, wxColour(0x00, 0x00, 0xff), wxEAST);
         dc.SetPen(wxPen(wxColour(0x88, 0x88, 0x88)));
         dc.SetBrush(*wxTRANSPARENT_BRUSH);
         dc.DrawRectangle(bx, y, bw, bh);

         wxString name = wxString::FromUTF8(metrics[i].name);
         wxSize ext = dc.GetTextExtent(name);
         int ty = y + (bh - ext.y) / 2;
         dc.DrawText(name, FromDIP(label_right) - ext.x, ty);
         dc.DrawText(wxString::FromUTF8(metrics[i].value), bx + bw + FromDIP(10), ty);
      }

      // the line through the markers for all structures, then the markers
      dc.SetPen(wxPen(palette::text(), FromDIP(2)));
      int previous = -1;
      for (size_t i = 0; i < metrics.size(); i++) {
         if (metrics[i].absolute < 0) continue;
         if (previous >= 0) {
            int h = FromDIP(marker_h), offset = (FromDIP(bar_h) - h) / 2;
            dc.DrawLine(x_for(metrics[previous].absolute), bar_top(previous) + offset + h,
                        x_for(metrics[i].absolute), bar_top(int(i)) + offset);
         }
         previous = int(i);
      }
      for (size_t i = 0; i < metrics.size(); i++) {
         if (metrics[i].relative >= 0) marker(dc, metrics[i].relative, int(i), false);
         if (metrics[i].absolute >= 0) marker(dc, metrics[i].absolute, int(i), true);
      }

      int y = bar_top(int(metrics.size())) - FromDIP(8);
      wxFont italic = base;
      italic.SetStyle(wxFONTSTYLE_ITALIC);
      dc.SetFont(italic);
      dc.DrawText("Worse", bx, y);
      dc.DrawText("Better", bx + bw - dc.GetTextExtent("Better").x, y);

      dc.SetFont(base);
      int ky = y + FromDIP(24), kw = FromDIP(marker_w), kh = FromDIP(14);
      dc.SetPen(wxPen(palette::text(), FromDIP(2)));
      dc.SetBrush(wxBrush(palette::text()));
      dc.DrawRectangle(bx, ky, kw, kh);
      dc.DrawText("Percentile relative to all X-ray structures", bx + kw + FromDIP(8), ky);
      ky += FromDIP(20);
      dc.SetBrush(wxBrush(*wxWHITE));
      dc.DrawRectangle(bx, ky, kw, kh);
      dc.DrawText("Percentile relative to X-ray structures of similar resolution", bx + kw + FromDIP(8), ky);
      if (! footnote.empty())
         dc.DrawText(wxString::FromUTF8(footnote), FromDIP(10), ky + FromDIP(24));
   }
};

} // namespace

void show_validation_report(wxWindow *parent, const bandicoot_validation_report &r) {
   ClickThroughDialog *d = new ClickThroughDialog(parent, wxString::FromUTF8(r.title),
                                                  ClickThroughList::RowType::Button);
   d->header_sizer()->Add(new PercentileChart(d->header(), r), 0, wxEXPAND | wxBOTTOM, 6);
   d->list()->set_labels_left(true);
   d->list()->allow_horizontal_scroll();
   if (r.residues.empty())
      d->list()->add_item("No problem residues listed");
   for (const auto &row : r.residues)
      d->list()->add_item(wxString::FromUTF8(row.label));
   auto rows = std::make_shared<std::vector<bandicoot_result_row>>(r.residues);
   d->list()->on_click = [rows](size_t i) {
      if (i < rows->size()) bandicoot_core_go_to_residue((*rows)[i]);
   };
   d->SetSize(d->FromDIP(wxSize(640, 720)));
   d->CentreOnParent();
   d->Show();
}

} // namespace wxui

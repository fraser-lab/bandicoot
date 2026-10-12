// src/wxUI/widgets/titled-panel.cc

#include "wxUI/widgets/titled-panel.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>

#include <cmath>

namespace wxui {

namespace {
const int border = 8;       // points between the frame and the content
const int title_inset = 8;  // points from the frame's left end to the title
}

// ---------------------------------------------------------------- TitledPanel

TitledPanel::TitledPanel(wxWindow *parent, const wxString &title)
   : wxPanel(parent) {

   SetLabel(title);
   SetBackgroundColour(palette::face());
   inner = new wxPanel(this);
   inner_sizer = new wxBoxSizer(wxVERTICAL);
   inner->SetSizer(inner_sizer);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->AddSpacer(title_height());
   outer->Add(inner, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, border);
   SetSizer(outer);
   Bind(wxEVT_PAINT, &TitledPanel::on_paint, this);
}

int TitledPanel::title_height() const {
   if (GetLabel().empty()) return border;
   return GetTextExtent(GetLabel()).y + border / 2;
}

void TitledPanel::on_paint(wxPaintEvent &) {

   wxPaintDC dc(this);
   wxSize s = GetClientSize();
   wxString title = GetLabel();
   int th = title.empty() ? 0 : dc.GetTextExtent(title).y;

   // the frame's top edge runs through the middle of the title
   wxRect frame(0, th / 2, s.x, s.y - th / 2);
   draw_frame(dc, frame, FrameStyle::Etched, GetBackgroundColour(),
              palette::shadow_of(palette::face()));
   if (! title.empty()) {
      wxSize ts = dc.GetTextExtent(title);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(wxBrush(GetBackgroundColour()));
      dc.DrawRectangle(title_inset - 2, 0, ts.x + 4, ts.y);
      dc.SetFont(GetFont());
      dc.SetTextForeground(IsEnabled() ? palette::text() : palette::text_disabled());
      dc.DrawText(title, title_inset, 0);
   }
}

// ---------------------------------------------------------------- TitledSlider

TitledSlider::TitledSlider(wxWindow *parent, const wxString &title,
                           double min_in, double max_in, double step_in, double value,
                           int decimals_in)
   : TitledPanel(parent, title), min(min_in), step(step_in > 0 ? step_in : 1.0),
     decimals(decimals_in) {

   int steps = int(std::lround((max_in - min_in) / step));
   slider = new wxSlider(content(), wxID_ANY, 0, 0, steps > 0 ? steps : 1);
   readout = new wxStaticText(content(), wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
   wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);
   h->Add(slider, 1, wxALIGN_CENTER_VERTICAL);
   h->Add(readout, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
   content_sizer()->Add(h, 0, wxEXPAND);

   // size the readout for the widest value it will show
   wxString widest = wxString::Format("%.*f", decimals, std::fabs(min_in) > std::fabs(max_in) ? min_in : max_in);
   readout->SetMinSize(wxSize(readout->GetTextExtent(widest + "0").x, -1));

   set_value(value);
   slider->Bind(wxEVT_SLIDER, [this](wxCommandEvent &) {
      show_value();
      if (on_change) on_change(get_value());
   });
}

double TitledSlider::get_value() const {
   return min + slider->GetValue() * step;
}

void TitledSlider::set_value(double value) {
   slider->SetValue(int(std::lround((value - min) / step)));
   show_value();
}

void TitledSlider::show_value() {
   readout->SetLabel(wxString::Format("%.*f", decimals, get_value()));
}

// ---------------------------------------------------------------- RadioGroup

RadioGroup::RadioGroup(wxWindow *parent, const wxString &title,
                       const std::vector<wxString> &options, int initial, bool horizontal)
   : TitledPanel(parent, title) {

   wxBoxSizer *box = new wxBoxSizer(horizontal ? wxHORIZONTAL : wxVERTICAL);
   for (size_t i = 0; i < options.size(); i++) {
      wxRadioButton *r = new wxRadioButton(content(), wxID_ANY, options[i], wxDefaultPosition,
                                           wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
      r->Bind(wxEVT_RADIOBUTTON, [this, i](wxCommandEvent &) {
         if (on_change) on_change(int(i));
      });
      box->Add(r, 0, horizontal ? (wxRIGHT | wxALIGN_CENTER_VERTICAL) : wxBOTTOM, 4);
      radios.push_back(r);
   }
   content_sizer()->Add(box, 0, wxEXPAND);
   set_selection(initial);
}

int RadioGroup::get_selection() const {
   for (size_t i = 0; i < radios.size(); i++)
      if (radios[i]->GetValue()) return int(i);
   return -1;
}

void RadioGroup::set_selection(int index) {
   if (index >= 0 && index < int(radios.size()))
      radios[index]->SetValue(true);
}

// ---------------------------------------------------------------- ButtonStack

ButtonStack::ButtonStack(wxWindow *parent, const wxString &title)
   : TitledPanel(parent, title) {}

wxButton *ButtonStack::add_button(const wxString &label, std::function<void()> action) {
   wxButton *b = new wxButton(content(), wxID_ANY, label);
   b->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { if (action) action(); });
   content_sizer()->Add(b, 0, wxEXPAND | wxBOTTOM, 2);
   InvalidateBestSize();
   return b;
}

} // namespace wxui

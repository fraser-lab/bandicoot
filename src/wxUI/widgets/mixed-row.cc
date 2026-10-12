// src/wxUI/widgets/mixed-row.cc

#include "wxUI/widgets/mixed-row.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace wxui {

namespace {
const int spacing = 6; // points between controls
}

MixedRow::MixedRow(wxWindow *parent, bool framed_in)
   : wxPanel(parent), framed(framed_in) {

   SetBackgroundColour(palette::face());
   row = new wxBoxSizer(wxHORIZONTAL);
   wxBoxSizer *outer = new wxBoxSizer(wxHORIZONTAL);
   outer->Add(row, 1, wxEXPAND | wxALL, framed ? 2 + 4 : 0);
   SetSizer(outer);
   if (framed)
      Bind(wxEVT_PAINT, &MixedRow::on_paint, this);
}

void MixedRow::add(wxWindow *w, int proportion) {
   row->Add(w, proportion, wxALIGN_CENTER_VERTICAL | wxRIGHT, spacing);
   InvalidateBestSize();
   Layout();
}

wxStaticText *MixedRow::add_label(const wxString &text) {
   wxStaticText *w = new wxStaticText(this, wxID_ANY, text);
   add(w);
   return w;
}

wxTextCtrl *MixedRow::add_text(const wxString &text, int width_chars, bool read_only) {
   wxTextCtrl *w = new wxTextCtrl(this, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                                  read_only ? wxTE_READONLY : 0);
   if (width_chars > 0)
      w->SetMinSize(wxSize(GetTextExtent(wxString('M', width_chars)).x, -1));
   add(w, width_chars > 0 ? 0 : 1);
   return w;
}

wxCheckBox *MixedRow::add_check(const wxString &label, bool state) {
   wxCheckBox *w = new wxCheckBox(this, wxID_ANY, label);
   w->SetValue(state);
   add(w);
   return w;
}

wxRadioButton *MixedRow::add_radio(const wxString &label, bool first_in_group) {
   wxRadioButton *w = new wxRadioButton(this, wxID_ANY, label, wxDefaultPosition,
                                        wxDefaultSize, first_in_group ? wxRB_GROUP : 0);
   add(w);
   return w;
}

wxButton *MixedRow::add_button(const wxString &label) {
   wxButton *w = new wxButton(this, wxID_ANY, label);
   add(w);
   return w;
}

wxChoice *MixedRow::add_choice(const std::vector<wxString> &items, int selection) {
   wxChoice *w = new wxChoice(this, wxID_ANY);
   for (const wxString &s : items) w->Append(s);
   if (selection >= 0 && selection < int(items.size())) w->SetSelection(selection);
   add(w);
   return w;
}

void MixedRow::add_stretch() {
   row->AddStretchSpacer();
}

void MixedRow::on_paint(wxPaintEvent &) {
   wxPaintDC dc(this);
   draw_frame(dc, wxRect(GetClientSize()), FrameStyle::Etched, GetBackgroundColour(),
              palette::shadow_of(palette::face()));
}

} // namespace wxui

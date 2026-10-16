// src/wxUI/widgets/entry-row.cc

#include "wxUI/widgets/entry-row.hh"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace wxui {

EntryRow::EntryRow(wxWindow *parent, const wxString &label, const wxString &initial,
                   const wxString &button_label)
   : wxPanel(parent) {

   wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);
   if (! label.empty())
      h->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   // drawn white with a dark edge: on the grey dialogs the native field
   // otherwise all but disappears
   text = new wxTextCtrl(this, wxID_ANY, initial, wxDefaultPosition, wxDefaultSize,
                         wxTE_PROCESS_ENTER | wxBORDER_SIMPLE);
   text->SetBackgroundColour(*wxWHITE);
   text->SetForegroundColour(*wxBLACK);
   h->Add(text, 1, wxALIGN_CENTER_VERTICAL);

   auto fire = [this]() { if (on_button) on_button(get_text()); };
   text->Bind(wxEVT_TEXT_ENTER, [fire](wxCommandEvent &) { fire(); });
   if (! button_label.empty()) {
      wxButton *b = new wxButton(this, wxID_ANY, button_label);
      b->Bind(wxEVT_BUTTON, [fire](wxCommandEvent &) { fire(); });
      h->Add(b, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
   }
   SetSizer(h);
}

wxString EntryRow::get_text() const {
   return text->GetValue();
}

void EntryRow::set_text(const wxString &s) {
   text->ChangeValue(s);
}

bool EntryRow::get_double(double &value) const {
   double v;
   if (! get_text().Strip(wxString::both).ToCDouble(&v)) return false;
   value = v;
   return true;
}

bool EntryRow::get_long(long &value) const {
   long v;
   if (! get_text().Strip(wxString::both).ToLong(&v)) return false;
   value = v;
   return true;
}

} // namespace wxui

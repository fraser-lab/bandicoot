// src/wxUI/dialogs/text-dialog.cc

#include "wxUI/dialogs/text-dialog.hh"

#include <wx/button.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

namespace wxui {

TextDialog::TextDialog(wxWindow *parent, const wxString &title, const wxString &text,
                       bool fixed_width)
   : wxFrame(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_FRAME_STYLE | (parent ? wxFRAME_FLOAT_ON_PARENT : 0)) {

   wxPanel *p = new wxPanel(this);
   wxTextCtrl *t = new wxTextCtrl(p, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                                  wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
   if (fixed_width)
      t->SetFont(wxFont(wxFontInfo(t->GetFont().GetPointSize()).Family(wxFONTFAMILY_TELETYPE)));
   wxButton *close = new wxButton(p, wxID_CLOSE, "Close");
   close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });

   wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);
   v->Add(t, 1, wxEXPAND | wxALL, 8);
   v->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   p->SetSizer(v);
   SetSize(FromDIP(wxSize(520, 420)));
}

TextDialog *show_text(wxWindow *parent, const wxString &title, const wxString &text,
                      bool fixed_width) {
   TextDialog *d = new TextDialog(parent, title, text, fixed_width);
   d->CentreOnParent();
   d->Show();
   return d;
}

} // namespace wxui

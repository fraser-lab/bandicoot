// src/wxUI/dialogs/message.cc

#include "wxUI/dialogs/message.hh"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <iostream>

namespace wxui {

void info(wxWindow *parent, const wxString &text) {

   std::cout << "INFO:: " << text.utf8_string() << std::endl;

   // wxMessageDialog is modal on macOS, so this one is built by hand
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Bandicoot", wxDefaultPosition, wxDefaultSize,
                              wxDEFAULT_DIALOG_STYLE);
   wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);
   wxStaticText *t = new wxStaticText(d, wxID_ANY, text);
   t->Wrap(d->FromDIP(420));
   v->Add(t, 1, wxEXPAND | wxALL, 16);
   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   ok->SetDefault();
   v->Add(ok, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 12);
   d->SetSizerAndFit(v);
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_OK);
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   d->CentreOnParent();
   d->Show();
}

void error(wxWindow *parent, const wxString &what_failed, const wxString &subject,
           const std::string &detail) {

   std::cout << "ERROR:: " << what_failed.utf8_string() << ": " << subject.utf8_string();
   if (! detail.empty()) std::cout << " -- " << detail;
   std::cout << std::endl;

   wxMessageDialog d(parent, subject, what_failed, wxOK | wxICON_ERROR);
   d.ShowModal();
}

bool confirm(wxWindow *parent, const wxString &question,
             const wxString &yes_label, const wxString &no_label,
             const wxString &consequence) {

   wxMessageDialog d(parent, question, "Bandicoot", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
   if (! consequence.empty())
      d.SetExtendedMessage(consequence);
   d.SetYesNoLabels(yes_label, no_label);
   return d.ShowModal() == wxID_YES;
}

} // namespace wxui

// src/wxUI/dialogs/message.cc

#include "wxUI/dialogs/message.hh"
#include "wxUI/dialogs/caution-icon.hh"

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

#include <iostream>

namespace wxui {

void info(wxWindow *parent, const wxString &text) {

   std::cout << "INFO:: " << text.utf8_string() << std::endl;

   // wxMessageDialog is modal on macOS, so this one is built by hand; it
   // floats so that it cannot be lost behind the main window
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Bandicoot", wxDefaultPosition, wxDefaultSize,
                              wxDEFAULT_DIALOG_STYLE | wxFRAME_FLOAT_ON_PARENT);
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

   // built by hand so the warning sign sits beside the text
   wxDialog d(parent, wxID_ANY, "Bandicoot");
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
#ifdef __APPLE__
   // wx's own warning art on macOS is the small monochrome symbol
   wxBitmapBundle sign = caution_icon(48);
#else
   wxBitmapBundle sign = wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MESSAGE_BOX, wxSize(48, 48));
#endif
   // without one the dialog is simply text and buttons
   if (sign.IsOk())
      row->Add(new wxStaticBitmap(&d, wxID_ANY, sign), 0, wxALIGN_TOP | wxRIGHT, 14);
   wxBoxSizer *text = new wxBoxSizer(wxVERTICAL);
   wxStaticText *q = new wxStaticText(&d, wxID_ANY, question);
   q->Wrap(d.FromDIP(420));
   text->Add(q, 0, wxEXPAND);
   if (! consequence.empty()) {
      wxStaticText *c = new wxStaticText(&d, wxID_ANY, consequence);
      c->Wrap(d.FromDIP(420));
      text->Add(c, 0, wxEXPAND | wxTOP, 10);
   }
   row->Add(text, 1, wxEXPAND);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   buttons->AddStretchSpacer();
   wxButton *no = new wxButton(&d, wxID_NO, no_label);
   wxButton *yes = new wxButton(&d, wxID_YES, yes_label);
   buttons->Add(no, 0, wxRIGHT, 8);
   buttons->Add(yes, 0);
   no->SetDefault();
   d.SetEscapeId(wxID_NO);
   d.SetAffirmativeId(wxID_YES);

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(row, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 16);
   col->Add(buttons, 0, wxEXPAND | wxALL, 12);
   d.SetSizerAndFit(col);
   d.Bind(wxEVT_BUTTON, [&d](wxCommandEvent &) { d.EndModal(wxID_YES); }, wxID_YES);
   d.Bind(wxEVT_BUTTON, [&d](wxCommandEvent &) { d.EndModal(wxID_NO); }, wxID_NO);
   d.CentreOnParent();
   return d.ShowModal() == wxID_YES;
}

} // namespace wxui

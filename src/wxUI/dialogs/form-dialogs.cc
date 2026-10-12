// src/wxUI/dialogs/form-dialogs.cc

#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/widgets/entry-row.hh"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace wxui {

namespace {

// prompt above, OK/Cancel below; the body goes in between
wxBoxSizer *frame_form(wxDialog *d, const wxString &prompt, wxSizer *body) {
   wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);
   if (! prompt.empty())
      v->Add(new wxStaticText(d, wxID_ANY, prompt), 0, wxALL, 10);
   v->Add(body, 1, wxEXPAND | wxLEFT | wxRIGHT | (prompt.empty() ? wxTOP : 0), 10);
   v->Add(d->CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
   return v;
}

} // namespace

// ---------------------------------------------------------------- ChooserDialog

ChooserDialog::ChooserDialog(wxWindow *parent, const wxString &title, const wxString &prompt,
                             const MoleculeChooser::molecule_list &molecules)
   : wxDialog(parent, wxID_ANY, title) {

   molecule_chooser = new MoleculeChooser(this, wxEmptyString, molecules);
   wxBoxSizer *body = new wxBoxSizer(wxVERTICAL);
   body->Add(molecule_chooser, 0, wxEXPAND);
   SetSizerAndFit(frame_form(this, prompt, body));

   Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      int imol = molecule_chooser->get_imol();
      if (imol >= 0 && on_ok) on_ok(imol);
      Destroy();
   }, wxID_OK);
   Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Destroy(); }, wxID_CANCEL);
   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
}

// ---------------------------------------------------------------- EntryDialog

EntryDialog::EntryDialog(wxWindow *parent, const wxString &title, const wxString &prompt,
                         const MoleculeChooser::molecule_list &molecules)
   : wxDialog(parent, wxID_ANY, title) {

   wxBoxSizer *body = new wxBoxSizer(wxVERTICAL);
   if (! molecules.empty()) {
      molecule_chooser = new MoleculeChooser(this, wxEmptyString, molecules);
      body->Add(molecule_chooser, 0, wxEXPAND | wxBOTTOM, 8);
   }
   fields_sizer = new wxBoxSizer(wxVERTICAL);
   body->Add(fields_sizer, 0, wxEXPAND);
   SetSizerAndFit(frame_form(this, prompt, body));

   Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      if (on_ok && ! on_ok(*this)) return;
      Destroy();
   }, wxID_OK);
   Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Destroy(); }, wxID_CANCEL);
   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
}

EntryRow *EntryDialog::add_field(const wxString &label, const wxString &initial) {
   EntryRow *r = new EntryRow(this, label, initial);
   // Return in any field acts as OK
   r->on_button = [this](const wxString &) {
      wxCommandEvent ok(wxEVT_BUTTON, wxID_OK);
      ok.SetEventObject(this);
      ProcessWindowEvent(ok);
   };
   fields_sizer->Add(r, 0, wxEXPAND | wxBOTTOM, 4);
   fields.push_back(r);
   Fit();
   return r;
}

int EntryDialog::get_imol() const {
   return molecule_chooser ? molecule_chooser->get_imol() : -1;
}

} // namespace wxui

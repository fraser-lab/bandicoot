// src/wxUI/widgets/path-box.cc

#include "wxUI/widgets/path-box.hh"

#include <wx/button.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace wxui {

PathBox::PathBox(wxWindow *parent, const wxString &label, Mode mode_in,
                 const wxString &wildcard_in)
   : wxPanel(parent), mode(mode_in),
     wildcard(wildcard_in.empty() ? wxString(wxFileSelectorDefaultWildcardStr) : wildcard_in) {

   wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);
   if (! label.empty())
      h->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   field = new wxTextCtrl(this, wxID_ANY);
   h->Add(field, 1, wxALIGN_CENTER_VERTICAL);
   wxButton *b = new wxButton(this, wxID_ANY, "Browse...");
   h->Add(b, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
   SetSizer(h);

   b->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { browse(); });
   field->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { if (on_change) on_change(get_path()); });
}

wxString PathBox::get_path() const {
   return field->GetValue();
}

void PathBox::set_path(const wxString &path) {
   field->ChangeValue(path);
}

void PathBox::browse() {

   // start where the current path points, if anywhere
   wxFileName current(get_path());
   wxString dir = current.GetPath();
   wxString name = current.GetFullName();

   wxString chosen;
   if (mode == Mode::Folder) {
      wxDirDialog d(this, "Choose a folder", get_path());
      if (d.ShowModal() == wxID_OK) chosen = d.GetPath();
   } else {
      long style = (mode == Mode::Save) ? (wxFD_SAVE | wxFD_OVERWRITE_PROMPT)
                                        : (wxFD_OPEN | wxFD_FILE_MUST_EXIST);
      wxFileDialog d(this, mode == Mode::Save ? "Save as" : "Choose a file",
                     dir, name, wildcard, style);
      if (d.ShowModal() == wxID_OK) chosen = d.GetPath();
   }
   if (chosen.empty()) return;
   set_path(chosen);
   if (on_change) on_change(chosen);
}

} // namespace wxui

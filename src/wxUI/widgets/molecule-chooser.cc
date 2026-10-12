// src/wxUI/widgets/molecule-chooser.cc

#include "wxUI/widgets/molecule-chooser.hh"

#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>

namespace wxui {

MoleculeChooser::MoleculeChooser(wxWindow *parent, const wxString &label,
                                 const molecule_list &molecules)
   : wxPanel(parent) {

   wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);
   if (! label.empty())
      h->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   choice = new wxChoice(this, wxID_ANY);
   h->Add(choice, 1, wxALIGN_CENTER_VERTICAL);
   SetSizer(h);

   choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
      if (on_change) on_change(get_imol());
   });
   set_molecules(molecules);
}

void MoleculeChooser::set_molecules(const molecule_list &molecules) {

   int previous = get_imol();
   choice->Clear();
   imols.clear();
   for (const auto &m : molecules) {
      choice->Append(wxString::Format("%d %s", m.first, wxString::FromUTF8(m.second)));
      imols.push_back(m.first);
   }
   if (imols.empty()) return;
   if (! set_imol(previous))
      choice->SetSelection(0);
   InvalidateBestSize();
   Layout();
}

int MoleculeChooser::get_imol() const {
   int i = choice->GetSelection();
   return (i == wxNOT_FOUND || i >= int(imols.size())) ? -1 : imols[i];
}

bool MoleculeChooser::set_imol(int imol) {
   auto it = std::find(imols.begin(), imols.end(), imol);
   if (it == imols.end()) return false;
   choice->SetSelection(int(it - imols.begin()));
   return true;
}

} // namespace wxui

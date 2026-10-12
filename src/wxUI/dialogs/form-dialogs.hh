// src/wxUI/dialogs/form-dialogs.hh
//
// The two small forms most of Coot's single-purpose dialogs reduce to:
//
//   ChooserDialog -- pick a molecule, then OK
//   EntryDialog   -- fill in one or more fields (optionally for a chosen
//                    molecule), then OK
//
// Neither blocks: OK runs on_ok and closes the dialog, Cancel just closes it.
// The caller supplies the molecules; see MoleculeChooser.

#ifndef BANDICOOT_WXUI_FORM_DIALOGS_HH
#define BANDICOOT_WXUI_FORM_DIALOGS_HH

#include "wxUI/widgets/molecule-chooser.hh"

#include <wx/dialog.h>

#include <functional>
#include <vector>

class wxBoxSizer;

namespace wxui {

class EntryRow;

class ChooserDialog : public wxDialog {
public:
   ChooserDialog(wxWindow *parent, const wxString &title, const wxString &prompt,
                 const MoleculeChooser::molecule_list &molecules);

   MoleculeChooser *chooser() const { return molecule_chooser; }
   std::function<void(int imol)> on_ok;

private:
   MoleculeChooser *molecule_chooser;
};

class EntryDialog : public wxDialog {
public:
   // molecules: an empty list means the dialog has no molecule chooser
   EntryDialog(wxWindow *parent, const wxString &title, const wxString &prompt,
               const MoleculeChooser::molecule_list &molecules = MoleculeChooser::molecule_list());

   EntryRow *add_field(const wxString &label, const wxString &initial = wxEmptyString);
   EntryRow *field(size_t index) const { return index < fields.size() ? fields[index] : nullptr; }
   int get_imol() const;  // -1 without a chooser

   // return false to keep the dialog open (e.g. after reporting a bad value)
   std::function<bool(EntryDialog &)> on_ok;

private:
   MoleculeChooser *molecule_chooser = nullptr;
   std::vector<EntryRow *> fields;
   wxBoxSizer *fields_sizer;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_FORM_DIALOGS_HH

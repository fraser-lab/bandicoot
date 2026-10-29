// src/wxUI/widgets/molecule-chooser.hh
//
// MoleculeChooser: a label and a drop-down of molecules, shown as Coot shows
// them ("2 1abc.pdb"). It is handed the molecules to offer -- models, maps or
// both, as the caller decides -- and knows nothing else about them.

#ifndef BANDICOOT_WXUI_MOLECULE_CHOOSER_HH
#define BANDICOOT_WXUI_MOLECULE_CHOOSER_HH

#include <wx/panel.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class wxChoice;
class wxStaticText;

namespace wxui {

class MoleculeChooser : public wxPanel {
public:
   typedef std::vector<std::pair<int, std::string>> molecule_list; // imol, name

   MoleculeChooser(wxWindow *parent, const wxString &label,
                   const molecule_list &molecules = molecule_list());

   // Keeps the current choice if it is still offered, else picks the first.
   void set_molecules(const molecule_list &molecules);
   int get_imol() const;        // -1 when there is nothing to choose
   bool set_imol(int imol);

   std::function<void(int imol)> on_change;

private:
   wxChoice *choice;
   std::vector<int> imols;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_MOLECULE_CHOOSER_HH

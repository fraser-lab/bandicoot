// src/wxUI/main/ligand-menu.cc

#include "wxUI/main/ligand-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/entry-row.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <functional>
#include <map>
#include <string>

namespace wxui {

namespace {

std::string field_text(EntryDialog &d, size_t i) {
   return d.field(i)->get_text().Trim().Trim(false).utf8_string();
}

void set_ok_label(wxDialog *d, const wxString &label) {
   if (wxButton *ok = wxDynamicCast(d->FindWindow(wxID_OK), wxButton)) ok->SetLabel(label);
   d->Fit();
}

void smiles_to_3d(wxWindow *parent) {
   EntryDialog *d = new EntryDialog(parent, "SMILES -> simple 3D", wxEmptyString);
   d->add_field("Residue name:", "LIG");
   d->add_field("SMILES string:");
   set_ok_label(d, "Import Molecule");
   d->on_ok = [](EntryDialog &dialog) {
      std::string comp_id = field_text(dialog, 0), smiles = field_text(dialog, 1);
      if (comp_id.empty() || smiles.empty()) { wxBell(); return false; }
      std::string why_not;
      if (! bandicoot_core_smiles_to_3d(smiles, comp_id, why_not)) {
         error(&dialog, "Could not import a molecule from", wxString::FromUTF8(smiles),
               "SMILES -> simple 3D: " + why_not);
         return false;
      }
      return true;
   };
   d->CentreOnParent();
   d->Show();
}

void rename_to_reference(wxWindow *parent) {
   // the residue it works on is the one centred when it opens, as in Coot
   int imol, resno;
   std::string chain_id, ins_code, res_name;
   if (! bandicoot_core_active_residue(imol, chain_id, resno, ins_code, res_name)) {
      info(parent, "No residue at the screen centre");
      return;
   }
   EntryDialog *d = new EntryDialog(parent, "Rename Atom to Reference", wxEmptyString);
   d->add_field("Reference Residue Type:", "ATP");
   d->add_field("New Residue Type:", "RXC");
   d->add_field("New Dictionary cif file name:", "new-dictionary-RXC.cif");
   d->on_ok = [imol, chain_id, resno, ins_code](EntryDialog &dialog) {
      std::string reference = field_text(dialog, 0), new_type = field_text(dialog, 1),
                  file_name = field_text(dialog, 2);
      if (reference.empty() || new_type.empty() || file_name.empty()) { wxBell(); return false; }
      std::string why_not;
      if (! bandicoot_core_rename_to_reference(imol, chain_id, resno, ins_code, reference,
                                               new_type, file_name, why_not)) {
         error(&dialog, "Could not write a dictionary for", wxString::FromUTF8(new_type),
               "Rename Atom to Reference: " + why_not);
         return false;
      }
      return true;
   };
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Find Ligands

class FindLigandsDialog : public wxDialog {
public:
   FindLigandsDialog(wxWindow *parent, const bandicoot_find_ligands &setup)
      : wxDialog(parent, wxID_ANY, "Find Ligands"), f(setup) {

      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      // the three lists, each scrolling when it is long
      wxScrolledWindow *lig = list_frame(col, "Select Ligands: (Search for things like this)");
      for (const auto &m : f.ligands) {
         wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
         wxCheckBox *flexible = new wxCheckBox(lig, wxID_ANY, "Flexible?");
         wxCheckBox *use = new wxCheckBox(lig, wxID_ANY, wxString::FromUTF8(m.label));
         row->Add(flexible, 0, wxRIGHT, 6);
         row->Add(use, 0);
         lig->GetSizer()->Add(row, 0, wxBOTTOM, 2);
         ligand_rows.push_back({ flexible, use });
      }
      wxScrolledWindow *maps = list_frame(col, "Select Map:");
      for (size_t i = 0; i < f.maps.size(); i++) {
         wxRadioButton *r = new wxRadioButton(maps, wxID_ANY, wxString::FromUTF8(f.maps[i].label),
                                              wxDefaultPosition, wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
         // a difference map suggests a higher level, as in Coot
         r->Bind(wxEVT_RADIOBUTTON, [this, i](wxCommandEvent &) {
            sigma->SetValue(f.maps[i].difference_map ? "3.0" : "1.0");
         });
         maps->GetSizer()->Add(r, 0, wxBOTTOM, 2);
         map_radios.push_back(r);
      }
      wxScrolledWindow *proteins = list_frame(col, "Select Protein: (Masks the Map):");
      for (size_t i = 0; i < f.proteins.size(); i++) {
         wxRadioButton *r = new wxRadioButton(proteins, wxID_ANY, wxString::FromUTF8(f.proteins[i].label),
                                              wxDefaultPosition, wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
         proteins->GetSizer()->Add(r, 0, wxBOTTOM, 2);
         protein_radios.push_back(r);
      }

      mask = new RadioGroup(this, "Mask Waters?", { "Treat waters like protein (mask) atoms",
                                                    "Ignore waters for mask" }, f.mask_waters ? 0 : 1);
      col->Add(mask, 0, wxEXPAND | wxBOTTOM, 6);
      where = new RadioGroup(this, "Where to Search?", { "Everywhere", "Right here" }, f.here ? 1 : 0);
      col->Add(where, 0, wxEXPAND | wxBOTTOM, 6);

      TitledPanel *sites = new TitledPanel(this, "Sites Options");
      n_sites = entry(sites, "Search Number of Top Sites:", std::to_string(f.n_top_sites));
      col->Add(sites, 0, wxEXPAND | wxBOTTOM, 6);
      // searching right here looks at one site only
      where->on_change = [sites](int i) { sites->Enable(i == 0); };
      sites->Enable(! f.here);

      TitledPanel *level = new TitledPanel(this, "r.m.s.d level");
      sigma = entry(level, wxEmptyString, wxString::Format("%.2f", f.sigma).utf8_string(), "rmsd");
      col->Add(level, 0, wxEXPAND | wxBOTTOM, 6);

      TitledPanel *conformers = new TitledPanel(this, "Conformer Options");
      n_conformers = entry(conformers, "Number of Conformers to Search:", std::to_string(f.n_conformers));
      col->Add(conformers, 0, wxEXPAND | wxBOTTOM, 6);

      TitledPanel *solutions = new TitledPanel(this, "Solution Display Option");
      multi = new wxCheckBox(solutions->content(), wxID_ANY, "Multi-Solutions");
      multi->SetValue(f.multi);
      solutions->content_sizer()->Add(multi, 0, wxBOTTOM, 4);
      frac_scoring = entry(solutions, "Fraction for Scoring", wxString::Format("%g", f.frac_scoring).utf8_string());
      frac_correl = entry(solutions, "Fraction for Correlation", wxString::Format("%g", f.frac_correl).utf8_string());
      col->Add(solutions, 0, wxEXPAND | wxBOTTOM, 6);

      TitledPanel *post = new TitledPanel(this, "Post-Fit Options");
      refine = new wxCheckBox(post->content(), wxID_ANY, "Real Space Refine Solutions?");
      refine->SetValue(f.refine);
      post->content_sizer()->Add(refine, 0);
      col->Add(post, 0, wxEXPAND | wxBOTTOM, 6);

      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      buttons->AddStretchSpacer();
      wxButton *cancel = new wxButton(this, wxID_CANCEL, "Cancel");
      wxButton *find = new wxButton(this, wxID_OK, "Find Ligands");
      find->SetDefault();
      buttons->Add(cancel, 0, wxRIGHT, 8);
      buttons->Add(find, 0);
      col->Add(buttons, 0, wxEXPAND | wxTOP, 4);

      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 10);
      SetSizerAndFit(outer);

      Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { search(); }, wxID_OK);
      Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Destroy(); }, wxID_CANCEL);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
   }

private:
   bandicoot_find_ligands f;
   std::vector<std::pair<wxCheckBox *, wxCheckBox *>> ligand_rows;   // flexible, use
   std::vector<wxRadioButton *> map_radios, protein_radios;
   RadioGroup *mask, *where;
   wxCheckBox *multi, *refine;
   wxTextCtrl *n_sites, *sigma, *n_conformers, *frac_scoring, *frac_correl;

   wxScrolledWindow *list_frame(wxBoxSizer *col, const wxString &title) {
      TitledPanel *frame = new TitledPanel(this, title);
      wxScrolledWindow *s = new wxScrolledWindow(frame->content(), wxID_ANY, wxDefaultPosition,
                                                 wxSize(FromDIP(380), FromDIP(96)));
      s->SetScrollRate(FromDIP(6), FromDIP(6));
      s->SetSizer(new wxBoxSizer(wxVERTICAL));
      frame->content_sizer()->Add(s, 1, wxEXPAND);
      col->Add(frame, 0, wxEXPAND | wxBOTTOM, 6);
      return s;
   }

   wxTextCtrl *entry(TitledPanel *frame, const wxString &label, const std::string &value,
                     const wxString &unit = wxEmptyString) {
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      if (! label.empty())
         row->Add(new wxStaticText(frame->content(), wxID_ANY, label), 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      wxTextCtrl *t = new wxTextCtrl(frame->content(), wxID_ANY, wxString::FromUTF8(value));
      row->Add(t, 1, wxALIGN_CENTER_VERTICAL);
      if (! unit.empty())
         row->Add(new wxStaticText(frame->content(), wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      frame->content_sizer()->Add(row, 0, wxEXPAND | wxBOTTOM, 2);
      return t;
   }

   // Out-of-range or unreadable numbers keep the value in use, as in Coot.
   static void read_int(wxTextCtrl *t, int &value) {
      long v;
      if (t->GetValue().Trim().Trim(false).ToLong(&v) && v > 0 && v < 1000000) value = int(v);
   }
   static void read_float(wxTextCtrl *t, float &value, double lo, double hi) {
      double v;
      if (t->GetValue().Trim().Trim(false).ToCDouble(&v) && v > lo && v < hi) value = float(v);
   }

   void search() {
      f.chosen.clear();
      int many = 0;
      for (size_t i = 0; i < ligand_rows.size(); i++) {
         if (! ligand_rows[i].second->GetValue()) continue;
         f.chosen.push_back({ f.ligands[i].imol, ligand_rows[i].first->GetValue() });
         if (f.ligands[i].n_atoms > f.many_atoms) many++;
      }
      if (f.chosen.empty()) {
         info(this, "No ligands were selected");
         return;
      }
      for (size_t i = 0; i < map_radios.size(); i++)
         if (map_radios[i]->GetValue()) f.map = f.maps[i].imol;
      for (size_t i = 0; i < protein_radios.size(); i++)
         if (protein_radios[i]->GetValue()) f.protein = f.proteins[i].imol;
      f.mask_waters = mask->get_selection() == 0;
      f.here = where->get_selection() == 1;
      read_int(n_sites, f.n_top_sites);
      read_float(sigma, f.sigma, 0.0, 1000.0);
      read_int(n_conformers, f.n_conformers);
      f.multi = multi->GetValue();
      read_float(frac_scoring, f.frac_scoring, 0.0, 1000.0);
      read_float(frac_correl, f.frac_correl, 0.0, 1000.0);
      f.refine = refine->GetValue();

      if (many > 0 &&
          ! confirm(this, "One or more of the selected ligands has unexpectedly many atoms.",
                    "Continue", "Cancel", "Do you want to continue?"))
         return;

      bandicoot_find_ligands chosen = f;
      wxWindow *parent = GetParent();
      Destroy();
      parent->CallAfter([chosen]() {
         wxBusyCursor busy;
         bandicoot_core_find_ligands(chosen);
      });
   }
};

// ------------------------------------------------------------ Ligand Check

void show_ligand_check_window(wxWindow *parent, const std::string &residue,
                              int density, int geometry, int bumps) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Ligand Check");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, wxString::FromUTF8(residue)), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

   // one row per test, the result beside its name
   const char *images[] = { "koppi-green-tick-64.png", "rygle-red-cross-64.png",
                            "forest51690-dwcheckno-incomplete-64.png" };
   const std::pair<const char *, int> tests[] = {
      { "Density Correlation", density }, { "Geometry Distortion", geometry },
      { "Ligand-Protein Bumps", bumps } };
   wxFlexGridSizer *grid = new wxFlexGridSizer(2, d->FromDIP(10), d->FromDIP(12));
   for (const auto &t : tests) {
      int s = (t.second >= 0 && t.second <= 2) ? t.second : 2;
      grid->Add(new wxStaticBitmap(d, wxID_ANY, icon(images[s], wxSize(42, 42))), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(new wxStaticText(d, wxID_ANY, t.first), 0, wxALIGN_CENTER_VERTICAL);
   }
   col->Add(grid, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 12);

   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   ok->SetDefault();
   col->Add(ok, 0, wxALIGN_RIGHT);
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 14);
   d->SetSizerAndFit(outer);
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_OK);
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   d->CentreOnParent();
   d->Show();
}

const std::map<std::string, std::function<void(wxWindow *)>> &ligand_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "BLIG_SMILES_3D",           smiles_to_3d },
      { "BLIG_RENAME_TO_REFERENCE", rename_to_reference },
      { "BLIG_FIND_LIGANDS",        show_find_ligands },
   };
   return actions;
}

} // namespace

void show_find_ligands(wxWindow *parent) {
   bandicoot_find_ligands setup = bandicoot_core_find_ligands_setup();
   // what Coot says when one of the three is missing
   if (setup.maps.empty()) { info(parent, "You must have a map to search for ligands."); return; }
   if (setup.proteins.empty()) { info(parent, "You must have a protein to mask the map."); return; }
   if (setup.ligands.empty()) {
      info(parent, wxString::Format("You must have at least one ligand to search for.\n"
                                    "Ligands have fewer than %d atoms.", setup.atom_limit));
      return;
   }
   FindLigandsDialog *d = new FindLigandsDialog(parent, setup);
   d->CentreOnParent();
   d->Show();
}

void show_ligand_check(wxWindow *parent, const std::string &residue, int density, int geometry, int bumps) {
   show_ligand_check_window(parent, residue, density, geometry, bumps);
}

bool is_ligand_action(const std::string &action) {
   return ligand_actions().count(action) > 0;
}

bool run_ligand_action(wxWindow *parent, const std::string &action) {
   auto it = ligand_actions().find(action);
   if (it == ligand_actions().end()) return false;
   it->second(parent);
   return true;
}

} // namespace wxui

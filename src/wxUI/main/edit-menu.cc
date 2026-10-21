// src/wxUI/main/edit-menu.cc

#include "wxUI/main/edit-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/colour-panel.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/entry-row.hh"
#include "wxUI/widgets/molecule-chooser.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/grid.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

// A dialog's face and its two buttons, the affirmative one relabelled.
wxStdDialogButtonSizer *dialog_buttons(wxDialog *d, const wxString &ok_label) {
   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
   buttons->GetAffirmativeButton()->SetLabel(ok_label);
   return buttons;
}

void destroy_on_close(wxDialog *d) {
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
}

wxTextCtrl *entry(wxWindow *parent, int width, const wxString &text = wxEmptyString) {
   return new wxTextCtrl(parent, wxID_ANY, text, wxDefaultPosition, wxSize(width, -1));
}

// A whole number from an entry; beeps and focuses it if there is none.
bool int_from(wxTextCtrl *t, int &value) {
   long v;
   if (! t->GetValue().Trim().Trim(false).ToLong(&v)) {
      wxBell();
      t->SetFocus();
      return false;
   }
   value = int(v);
   return true;
}

void fill_chains(wxChoice *c, int imol) {
   c->Clear();
   for (const std::string &chain : bandicoot_core_chain_ids(imol))
      c->Append(wxString::FromUTF8(chain));
   if (c->GetCount() > 0) c->SetSelection(0);
}

std::string chosen(const wxChoice *c) {
   int i = c->GetSelection();
   return i < 0 ? std::string() : c->GetString(i).utf8_string();
}

wxBoxSizer *row_of(std::initializer_list<std::pair<wxWindow *, int>> items) {
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   bool first = true;
   for (const auto &i : items) {
      row->Add(i.first, i.second, wxALIGN_CENTER_VERTICAL | (first ? 0 : wxLEFT), 6);
      first = false;
   }
   return row;
}

// ------------------------------------------------------------ Change Chain IDs

void show_change_chain_ids(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to change");
      return;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Change Chain ID");
   d->SetBackgroundColour(palette::face());
   TitledPanel *frame = new TitledPanel(d);
   wxWindow *c = frame->content();
   wxBoxSizer *col = frame->content_sizer();

   col->Add(new wxStaticText(c, wxID_ANY, "Change Chain ID in Molecule:"), 0, wxBOTTOM, 4);
   MoleculeChooser *molecule = new MoleculeChooser(c, wxEmptyString, models);
   col->Add(molecule, 0, wxEXPAND | wxBOTTOM, 6);

   col->Add(new wxStaticText(c, wxID_ANY, "From:"), 0, wxBOTTOM, 4);
   wxChoice *from_chain = new wxChoice(c, wxID_ANY);
   fill_chains(from_chain, molecule->get_imol());
   col->Add(row_of({ { new wxStaticText(c, wxID_ANY, "Chain ID:"), 0 }, { from_chain, 1 } }),
            0, wxEXPAND | wxLEFT | wxBOTTOM, 6);
   molecule->on_change = [from_chain](int imol) { fill_chains(from_chain, imol); };

   wxStaticBoxSizer *selection = new wxStaticBoxSizer(wxVERTICAL, c, "Using Residue Selection:");
   wxWindow *sb = selection->GetStaticBox();
   wxRadioButton *whole = new wxRadioButton(sb, wxID_ANY, "Whole Chain", wxDefaultPosition,
                                            wxDefaultSize, wxRB_GROUP);
   wxRadioButton *range = new wxRadioButton(sb, wxID_ANY, "Use Residue Range");
   wxStaticText *from_label = new wxStaticText(sb, wxID_ANY, "from");
   wxTextCtrl *from_resno = entry(sb, 80);
   wxStaticText *to_label = new wxStaticText(sb, wxID_ANY, "to");
   wxTextCtrl *to_resno = entry(sb, 80);
   whole->SetValue(true);
   selection->Add(whole, 0, wxBOTTOM, 4);
   selection->Add(row_of({ { range, 0 }, { from_label, 0 }, { from_resno, 1 },
                           { to_label, 0 }, { to_resno, 1 } }), 0, wxEXPAND);
   auto sync_range = [=]() {
      bool on = range->GetValue();
      for (wxWindow *w : std::vector<wxWindow *>{ from_label, from_resno, to_label, to_resno })
         w->Enable(on);
   };
   sync_range();
   whole->Bind(wxEVT_RADIOBUTTON, [sync_range](wxCommandEvent &) { sync_range(); });
   range->Bind(wxEVT_RADIOBUTTON, [sync_range](wxCommandEvent &) { sync_range(); });
   col->Add(selection, 0, wxEXPAND | wxBOTTOM, 6);

   col->Add(new wxStaticText(c, wxID_ANY, "To:"), 0, wxBOTTOM, 4);
   wxTextCtrl *to_chain = entry(c, 120);
   col->Add(row_of({ { new wxStaticText(c, wxID_ANY, "Chain ID:"), 0 }, { to_chain, 0 } }));

   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      int imol = molecule->get_imol();
      std::string from = chosen(from_chain);
      if (imol < 0 || from_chain->GetSelection() < 0) { wxBell(); return; }
      bool use_range = range->GetValue();
      int first = 0, last = 0;
      if (use_range && (! int_from(from_resno, first) || ! int_from(to_resno, last))) return;
      std::string to = to_chain->GetValue().utf8_string();
      std::string why_not;
      if (! bandicoot_core_change_chain_id(imol, from, to, use_range, first, last, why_not)) {
         std::cout << "INFO:: Change Chain ID: " << from << " -> " << to << ": " << why_not << std::endl;
         info(d, wxString::FromUTF8(why_not));
         return;
      }
      d->Destroy();
   }, wxID_OK);
   destroy_on_close(d);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(frame, 1, wxEXPAND | wxALL, 10);
   outer->Add(dialog_buttons(d, "Apply New Chain ID"), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Copy Molecule, Copy Fragment

void show_copy_molecule(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to copy");
      return;
   }
   ChooserDialog *d = new ChooserDialog(parent, "Copy Molecule", "Molecule to copy:", models);
   d->on_ok = [](int imol) { bandicoot_core_copy_molecule(imol); };
   d->CentreOnParent();
   d->Show();
}

void show_copy_fragment(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to copy from");
      return;
   }
   EntryDialog *d = new EntryDialog(parent, "Copy Fragment", "Copy a fragment from which molecule?",
                                    models);
   d->add_field(wxEmptyString, "//A/1-10");
   d->on_ok = [](EntryDialog &dialog) {
      std::string selection = dialog.field(0)->get_text().utf8_string();
      if (selection.empty()) { wxBell(); return false; }
      if (bandicoot_core_copy_fragment(dialog.get_imol(), selection) < 0) {
         error(&dialog, "Could not copy fragment", wxString::FromUTF8(selection),
               "Copy Fragment: no new molecule from selection " + selection);
         return false;
      }
      return true;
   };
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Replace Fragment, Replace Residue

// First the molecule to update, then the one holding the new fragment.
void show_replace_fragment(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to update");
      return;
   }
   ChooserDialog *d = new ChooserDialog(parent, "Replace Fragment", "Define the molecule that needs updating",
                                        models);
   d->on_ok = [parent, models](int imol_target) {
      EntryDialog *f = new EntryDialog(parent, "Replace Fragment", "Molecule that contains the new fragment:",
                                       models);
      f->add_field("Atom Selection", "//");
      f->on_ok = [imol_target](EntryDialog &dialog) {
         std::string selection = dialog.field(0)->get_text().utf8_string();
         if (selection.empty()) { wxBell(); return false; }
         std::string why_not;
         if (! bandicoot_core_replace_fragment(imol_target, dialog.get_imol(), selection, why_not)) {
            error(&dialog, "Could not replace fragment", wxString::FromUTF8(selection),
                  "Replace Fragment: " + why_not);
            return false;
         }
         return true;
      };
      f->CentreOnParent();
      f->Show();
   };
   d->CentreOnParent();
   d->Show();
}

// The active residue becomes the type typed in, as a monomer from the
// dictionary laid over it.
void show_replace_residue(wxWindow *parent) {
   if (bandicoot_core_goto_molecules().empty()) {
      info(parent, "No coordinates to change");
      return;
   }
   EntryDialog *d = new EntryDialog(parent, "Replace Residue", "Replace this residue with residue of type:");
   d->add_field(wxEmptyString, "ALA");
   if (wxWindow *ok = d->FindWindow(wxID_OK)) ok->SetLabel("Mutate");
   d->on_ok = [](EntryDialog &dialog) {
      std::string comp_id = dialog.field(0)->get_text().Upper().utf8_string();
      // a dictionary code: letters and digits only
      bool valid = ! comp_id.empty() && comp_id.size() <= 5;
      for (char c : comp_id) valid = valid && std::isalnum(static_cast<unsigned char>(c));
      if (! valid) { wxBell(); return false; }
      std::string why_not;
      if (! bandicoot_core_replace_active_residue(comp_id, why_not)) {
         error(&dialog, "Could not replace residue with", wxString::FromUTF8(comp_id),
               "Replace Residue: " + why_not);
         return false;
      }
      return true;
   };
   d->Fit();
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Merge Molecules

void show_merge_molecules(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to merge");
      return;
   }
   // the list names molecules as the Display Manager does
   std::map<int, std::string> short_names;
   for (const auto &m : bandicoot_core_models_and_maps()) short_names[m.first] = m.second;

   wxDialog *d = new wxDialog(parent, wxID_ANY, "Merge Molecules", wxDefaultPosition,
                              wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
   d->SetBackgroundColour(palette::face());
   TitledPanel *frame = new TitledPanel(d);
   wxWindow *c = frame->content();
   wxBoxSizer *col = frame->content_sizer();

   col->Add(new wxStaticText(c, wxID_ANY, "Append/Insert Molecule(s):"), 0,
            wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);
   wxScrolledWindow *list = new wxScrolledWindow(c, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                 wxVSCROLL | wxBORDER_THEME);
   wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
   std::vector<std::pair<int, wxCheckBox *>> boxes;
   for (const auto &m : models) {
      wxCheckBox *b = new wxCheckBox(list, wxID_ANY, wxString::FromUTF8(short_names[m.first]));
      rows->Add(b, 0, wxALL, 4);
      boxes.push_back({ m.first, b });
   }
   list->SetSizer(rows);
   list->SetScrollRate(0, 10);
   wxSize all = rows->CalcMin();
   list->SetMinSize(wxSize(std::max(all.x + 24, 320), std::max(std::min(all.y, 300), 160)));
   col->Add(list, 1, wxEXPAND | wxBOTTOM, 8);

   col->Add(new wxStaticText(c, wxID_ANY, "into Molecule:"), 0, wxALIGN_CENTER | wxBOTTOM, 6);
   MoleculeChooser *target = new MoleculeChooser(c, wxEmptyString, models);
   target->set_imol(bandicoot_core_merge_target_default());
   col->Add(target, 0, wxEXPAND);

   d->Bind(wxEVT_BUTTON, [d, boxes, target](wxCommandEvent &) {
      std::vector<int> add;
      for (const auto &b : boxes)
         if (b.second->IsChecked()) add.push_back(b.first);
      int into = target->get_imol();
      d->Destroy();
      bandicoot_core_merge_molecules(add, into);
   }, wxID_OK);
   destroy_on_close(d);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(frame, 1, wxEXPAND | wxALL, 10);
   outer->Add(dialog_buttons(d, "Merge"), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Renumber Residues

void show_renumber_residues(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to renumber");
      return;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Renumber Residue Range");
   d->SetBackgroundColour(palette::face());
   TitledPanel *frame = new TitledPanel(d);
   wxWindow *c = frame->content();
   wxBoxSizer *col = frame->content_sizer();

   col->Add(new wxStaticText(c, wxID_ANY, "Renumber Residue Range of Molecule:"), 0, wxBOTTOM, 4);
   MoleculeChooser *molecule = new MoleculeChooser(c, wxEmptyString, models);
   col->Add(molecule, 0, wxEXPAND | wxBOTTOM, 6);
   wxChoice *chain = new wxChoice(c, wxID_ANY);
   fill_chains(chain, molecule->get_imol());
   molecule->on_change = [chain](int imol) { fill_chains(chain, imol); };
   col->Add(row_of({ { new wxStaticText(c, wxID_ANY, "Chain ID:"), 0 }, { chain, 0 } }), 0, wxBOTTOM, 6);

   // the start entry holds the active residue's number, in case the range starts there
   wxStaticBoxSizer *start = new wxStaticBoxSizer(wxHORIZONTAL, c, "Start Residue");
   wxWindow *sb1 = start->GetStaticBox();
   wxRadioButton *n_term = new wxRadioButton(sb1, wxID_ANY, "N-terminus", wxDefaultPosition,
                                             wxDefaultSize, wxRB_GROUP);
   wxRadioButton *start_at = new wxRadioButton(sb1, wxID_ANY, "Residue");
   int active = 0;
   wxTextCtrl *start_resno = entry(sb1, 60, bandicoot_core_active_residue_number(active)
                                            ? wxString::Format("%d", active) : wxString());
   n_term->SetValue(true);
   start->Add(row_of({ { n_term, 0 }, { start_at, 0 }, { start_resno, 0 } }));

   wxStaticBoxSizer *end = new wxStaticBoxSizer(wxHORIZONTAL, c, "End Residue");
   wxWindow *sb2 = end->GetStaticBox();
   wxRadioButton *end_at = new wxRadioButton(sb2, wxID_ANY, "Residue", wxDefaultPosition,
                                             wxDefaultSize, wxRB_GROUP);
   wxTextCtrl *end_resno = entry(sb2, 60);
   wxRadioButton *c_term = new wxRadioButton(sb2, wxID_ANY, "C-terminus");
   c_term->SetValue(true);
   end->Add(row_of({ { end_at, 0 }, { end_resno, 0 }, { c_term, 0 } }));

   wxBoxSizer *ends = new wxBoxSizer(wxHORIZONTAL);
   ends->Add(start, 0, wxALIGN_CENTER_VERTICAL);
   ends->Add(new wxStaticText(c, wxID_ANY, "to"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 10);
   ends->Add(end, 0, wxALIGN_CENTER_VERTICAL);
   ends->Add(new wxStaticText(c, wxID_ANY, "(inclusive)"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
   col->Add(ends, 0, wxBOTTOM, 6);

   wxTextCtrl *offset = entry(c, 80);
   col->Add(row_of({ { new wxStaticText(c, wxID_ANY, "Apply Offset:"), 0 }, { offset, 0 } }));

   auto sync = [=]() {
      start_resno->Enable(start_at->GetValue());
      end_resno->Enable(end_at->GetValue());
   };
   sync();
   for (wxRadioButton *r : { n_term, start_at, end_at, c_term })
      r->Bind(wxEVT_RADIOBUTTON, [sync](wxCommandEvent &) { sync(); });

   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      bandicoot_renumber r;
      r.imol = molecule->get_imol();
      r.chain = chosen(chain);
      if (r.imol < 0 || chain->GetSelection() < 0) { wxBell(); return; }
      r.from_n_terminus = n_term->GetValue();
      r.to_c_terminus = c_term->GetValue();
      if (! r.from_n_terminus && ! int_from(start_resno, r.start)) return;
      if (! r.to_c_terminus && ! int_from(end_resno, r.end)) return;
      if (! int_from(offset, r.offset)) return;
      if (! bandicoot_core_renumber_residues(r)) {
         std::cout << "WARNING:: could not renumber residue range in molecule " << r.imol
                   << " chain " << r.chain << std::endl;
         info(d, "Could not renumber residue range.\n"
                 "Maybe your selection overlaps with existing residues.\n"
                 "Please revise your selection.");
         return;
      }
      d->Destroy();
   }, wxID_OK);
   destroy_on_close(d);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(frame, 1, wxEXPAND | wxALL, 10);
   outer->Add(dialog_buttons(d, "Apply"), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
   offset->SetFocus();
}

// ------------------------------------------------------------ Residue Info

wxString two_places(float v) {
   return wxString::Format("%.2f", v);
}

// One window per picked residue. Apply writes the edited occupancies,
// B-factors and alt confs, then shows the residue as it now is.
class ResidueInfoWindow : public wxDialog {
public:
   ResidueInfoWindow(wxWindow *parent, const bandicoot_residue_info &info)
      : wxDialog(parent, wxID_ANY, "Residue Info", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        info(info) {
      SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      molecule_label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(info.molecule_label));
      residue_label = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(info.residue_name));
      col->Add(molecule_label, 0, wxALIGN_CENTER | wxTOP, 10);
      col->Add(residue_label, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);
      col->Add(master_atom(), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

      atom_list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                       wxVSCROLL | wxBORDER_THEME);
      atom_list->SetScrollRate(0, 10);
      col->Add(atom_list, 1, wxEXPAND | wxALL, 10);

      chi_box = new wxStaticBoxSizer(wxVERTICAL, this, "Chi Angles");
      col->Add(chi_box, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

      wxStdDialogButtonSizer *buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
      buttons->GetAffirmativeButton()->SetLabel("Apply Occ and B Edits");
      buttons->GetCancelButton()->SetLabel("Close");
      col->Add(buttons, 0, wxEXPAND | wxALL, 10);

      Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply(); }, wxID_OK);
      Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Destroy(); }, wxID_CANCEL);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });

      fill();
      SetSizer(col);
      Fit();
   }

private:
   bandicoot_residue_info info;
   wxStaticText *molecule_label, *residue_label;
   wxCheckBox *occ_all, *occ_alt, *b_all, *rename;
   wxTextCtrl *occ_alt_conf, *rename_from, *rename_to, *master_occ, *master_b;
   wxScrolledWindow *atom_list;
   wxStaticBoxSizer *chi_box;
   struct Row { wxTextCtrl *occ, *b, *alt_conf; };
   std::vector<Row> rows;

   wxSizer *master_atom() {
      wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, this, "Master Atom");
      wxWindow *sb = box->GetStaticBox();
      occ_all = new wxCheckBox(sb, wxID_ANY, "Apply Occupancy to all atoms in residue?");
      occ_alt = new wxCheckBox(sb, wxID_ANY, "Apply Occupancy to atoms with alt. conf.:");
      occ_alt_conf = entry(sb, 40);
      b_all = new wxCheckBox(sb, wxID_ANY, "Apply Temperature Factor to all atoms in residue?");
      rename = new wxCheckBox(sb, wxID_ANY, "Rename Alt-conf from");
      rename_from = entry(sb, 30);
      rename_to = entry(sb, 30);
      master_occ = entry(sb, 120, "1.00");
      master_b = entry(sb, 120, two_places(info.default_b_factor));

      box->Add(occ_all, 0, wxBOTTOM, 4);
      box->Add(row_of({ { occ_alt, 0 }, { occ_alt_conf, 0 } }), 0, wxBOTTOM, 4);
      box->Add(b_all, 0, wxBOTTOM, 4);
      box->Add(row_of({ { rename, 0 }, { rename_from, 0 },
                        { new wxStaticText(sb, wxID_ANY, "to"), 0 }, { rename_to, 0 } }),
               0, wxBOTTOM, 6);
      wxFlexGridSizer *values = new wxFlexGridSizer(2, 2, 2, 12);
      values->Add(new wxStaticText(sb, wxID_ANY, "Occupancy"), 0, wxALIGN_CENTER);
      values->Add(new wxStaticText(sb, wxID_ANY, "Temperature Factor"), 0, wxALIGN_CENTER);
      values->Add(master_occ, 0, wxEXPAND);
      values->Add(master_b, 0, wxEXPAND);
      box->Add(values, 0, wxALIGN_RIGHT);

      for (wxCheckBox *b : { occ_all, occ_alt, b_all, rename })
         b->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { sync_master(); spread_occupancy(); spread_b_factor(); });
      master_occ->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { spread_occupancy(); });
      occ_alt_conf->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { spread_occupancy(); });
      master_b->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { spread_b_factor(); });
      sync_master();
      return box;
   }

   void sync_master() {
      master_occ->Enable(occ_all->IsChecked() || occ_alt->IsChecked());
      occ_alt_conf->Enable(occ_alt->IsChecked());
      master_b->Enable(b_all->IsChecked());
      rename_from->Enable(rename->IsChecked());
      rename_to->Enable(rename->IsChecked());
   }

   // The master values go into the atom rows as they are typed (or ticked).
   void spread_occupancy() {
      wxString v = master_occ->GetValue();
      if (occ_all->IsChecked()) {
         for (Row &r : rows) r.occ->ChangeValue(v);
      } else if (occ_alt->IsChecked()) {
         for (Row &r : rows)
            if (r.alt_conf->GetValue() == occ_alt_conf->GetValue()) r.occ->ChangeValue(v);
      }
   }

   void spread_b_factor() {
      if (! b_all->IsChecked()) return;
      for (Row &r : rows) r.b->ChangeValue(master_b->GetValue());
   }

   void fill() {
      molecule_label->SetLabel(wxString::FromUTF8(info.molecule_label));
      residue_label->SetLabel(wxString::FromUTF8(info.residue_name));

      atom_list->DestroyChildren();
      rows.clear();
      wxFlexGridSizer *grid = new wxFlexGridSizer(5, 4, 6);
      for (const bandicoot_residue_atom &a : info.atoms) {
         Row r;
         r.occ = entry(atom_list, 90, two_places(a.occupancy));
         r.b = entry(atom_list, 90, two_places(a.b_factor));
         r.alt_conf = entry(atom_list, 50, wxString::FromUTF8(a.alt_conf));
         grid->Add(new wxStaticText(atom_list, wxID_ANY, wxString::FromUTF8(a.label)), 0,
                   wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
         grid->Add(r.occ, 0, wxALIGN_CENTER_VERTICAL);
         grid->Add(r.b, 0, wxALIGN_CENTER_VERTICAL);
         grid->Add(new wxStaticText(atom_list, wxID_ANY, "Alt-conf:"), 0, wxALIGN_CENTER_VERTICAL);
         grid->Add(r.alt_conf, 0, wxALIGN_CENTER_VERTICAL);
         rows.push_back(r);
      }
      wxBoxSizer *pad = new wxBoxSizer(wxVERTICAL);
      pad->Add(grid, 0, wxALL, 6);
      atom_list->SetSizer(pad);
      // up to fifteen rows before the list scrolls, as Coot opened it
      wxSize all = pad->CalcMin();
      int row_height = rows.empty() ? 24 : rows.front().occ->GetBestSize().y + 6;
      atom_list->SetMinSize(wxSize(all.x + 24, std::min(all.y, 15 * row_height + 12)));
      atom_list->FitInside();

      chi_box->Clear(true);
      for (const std::string &line : info.chi_angles)
         chi_box->Add(new wxStaticText(chi_box->GetStaticBox(), wxID_ANY, wxString::FromUTF8(line)));
      chi_box->GetStaticBox()->Show(! info.chi_angles.empty());
      chi_box->ShowItems(! info.chi_angles.empty());
      Layout();
   }

   void apply() {
      // a bulk rename first, so the edits below pick it up
      if (rename->IsChecked()) {
         wxString from = rename_from->GetValue(), to = rename_to->GetValue();
         from.Replace(" ", ""); to.Replace(" ", "");
         for (Row &r : rows) {
            wxString cur = r.alt_conf->GetValue();
            cur.Replace(" ", "");
            if (cur == from) r.alt_conf->ChangeValue(to);
         }
      }
      // a value that does not read as a number is left as it was
      std::vector<bandicoot_residue_atom> after = info.atoms;
      for (size_t i = 0; i < rows.size() && i < after.size(); i++) {
         double v;
         if (rows[i].occ->GetValue().ToCDouble(&v)) after[i].occupancy = float(v);
         if (rows[i].b->GetValue().ToCDouble(&v)) after[i].b_factor = float(v);
         after[i].alt_conf = rows[i].alt_conf->GetValue().utf8_string();
      }
      bandicoot_core_apply_atom_edits(info.imol, info.atoms, after);

      // show the residue as it now is; it may have gone
      if (info.atoms.empty()) return;
      const bandicoot_residue_atom &a = info.atoms.front();
      bandicoot_residue_info now;
      if (! bandicoot_core_residue_info(info.imol, a.chain, a.resno, a.ins_code, now)) {
         Destroy();
         return;
      }
      info = now;
      fill();
   }
};

void show_residue_info(const bandicoot_residue_info &info) {
   ResidueInfoWindow *w = new ResidueInfoWindow(owner, info);
   w->CentreOnParent();
   w->Show();
}

// ------------------------------------------------------------ display settings

const char *angstrom = "\xC3\x85ngstr\xC3\xB6m";

wxString utf8(const char *s) { return wxString::FromUTF8(s); }

// A positive number below limit from an entry; beeps and focuses it if not.
bool positive_from(wxTextCtrl *t, double &value, double limit = 1.0e6) {
   double v;
   if (! t->GetValue().Trim().Trim(false).ToCDouble(&v) || v <= 0.0 || v >= limit) {
      wxBell();
      t->SetFocus();
      return false;
   }
   value = v;
   return true;
}

// A small window for one number: heading, "label [entry] unit", then OK
// (and Apply, when asked for) and Cancel. apply gets the number; the
// window stays open on Apply.
void show_value_window(wxWindow *parent, const wxString &title, const wxString &heading,
                       const wxString &label, const std::string &initial, const wxString &unit,
                       double limit, bool with_apply, std::function<void(double)> apply) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, heading), 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);
   wxTextCtrl *value = new wxTextCtrl(d, wxID_ANY, wxString::FromUTF8(initial), wxDefaultPosition,
                                      wxSize(160, -1), wxTE_PROCESS_ENTER);
   col->Add(row_of({ { new wxStaticText(d, wxID_ANY, label), 0 }, { value, 1 },
                     { new wxStaticText(d, wxID_ANY, unit), 0 } }), 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   buttons->Add(ok, 1, wxRIGHT, 6);
   if (with_apply) {
      wxButton *apply_button = new wxButton(d, wxID_APPLY, "Apply");
      buttons->Add(apply_button, 1, wxRIGHT, 6);
      apply_button->Bind(wxEVT_BUTTON, [value, apply, limit](wxCommandEvent &) {
         double v;
         if (positive_from(value, v, limit)) apply(v);
      });
   }
   buttons->Add(new wxButton(d, wxID_CANCEL, "Cancel"), 1);
   col->Add(buttons, 0, wxEXPAND | wxALL, 10);
   ok->SetDefault();

   auto finish = [d, value, apply, limit]() {
      double v;
      if (! positive_from(value, v, limit)) return;
      apply(v);
      d->Destroy();
   };
   ok->Bind(wxEVT_BUTTON, [finish](wxCommandEvent &) { finish(); });
   value->Bind(wxEVT_TEXT_ENTER, [finish](wxCommandEvent &) { finish(); });
   destroy_on_close(d);

   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
   value->SetFocus();
   value->SelectAll();
}

PanelColour panel_colour(const bandicoot_rgb &c) {
   PanelColour p;
   p.r = c.r; p.g = c.g; p.b = c.b;
   return p;
}

bandicoot_rgb core_colour(const PanelColour &p) {
   bandicoot_rgb c;
   c.r = p.r; c.g = p.g; c.b = p.b;
   return c;
}

void show_skeleton_colour(wxWindow *) {
   bandicoot_rgb before = bandicoot_core_skeleton_colour();
   show_colour_panel("Skeleton Colour Selection", panel_colour(before),
                     [](const PanelColour &c) { bandicoot_core_set_skeleton_colour(core_colour(c)); },
                     [before](bool kept) { if (! kept) bandicoot_core_set_skeleton_colour(before); });
}

void show_bond_colours(wxWindow *parent) {
   std::vector<bandicoot_bond_colour> models = bandicoot_core_bond_colours();
   if (models.empty()) {
      info(parent, "No molecules to colour");
      return;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Molecule Colours", wxDefaultPosition,
                              wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
   d->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Molecule Colours"), 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);

   wxScrolledWindow *list = new wxScrolledWindow(d, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                 wxVSCROLL | wxBORDER_THEME);
   list->SetBackgroundColour(palette::face());
   wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
   for (const bandicoot_bond_colour &m : models) {
      TitledSlider *s = new TitledSlider(list, wxString::FromUTF8(m.label), 0.0, 360.0, 1.0,
                                         m.rotation, 1);
      int imol = m.imol;
      s->on_change = [imol](double v) { bandicoot_core_set_bond_colour_rotation(imol, float(v)); };
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(s, 1, wxEXPAND);
      row->Add(new wxStaticText(list, wxID_ANY, "degrees"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
      rows->Add(row, 0, wxEXPAND | wxALL, 6);
   }
   list->SetSizer(rows);
   list->SetScrollRate(0, 10);
   wxSize all = rows->CalcMin();
   list->SetMinSize(wxSize(std::max(all.x + 24, 360), std::min(all.y, 420)));
   col->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

   wxCheckBox *carbons = new wxCheckBox(d, wxID_ANY, "Change colour for Carbons only");
   carbons->SetValue(bandicoot_core_bond_colour_carbons_only());
   carbons->Bind(wxEVT_CHECKBOX, [carbons](wxCommandEvent &) {
      bandicoot_core_set_bond_colour_carbons_only(carbons->IsChecked());
   });
   col->Add(carbons, 0, wxALL, 10);

   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   col->Add(ok, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);
   ok->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
   destroy_on_close(d);

   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
}

void show_bond_parameters(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to change");
      return;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Bond Parameters");
   d->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   MoleculeChooser *molecule = new MoleculeChooser(d, "Molecule:", models);
   molecule->set_imol(bandicoot_core_bond_parameters_molecule());
   col->Add(molecule, 0, wxEXPAND | wxALL, 10);

   wxStaticBoxSizer *width_box = new wxStaticBoxSizer(wxHORIZONTAL, d, "Molecule Bond Width:");
   wxChoice *width = new wxChoice(width_box->GetStaticBox(), wxID_ANY);
   for (int i = 1; i <= 20; i++) width->Append(wxString::Format("%d", i));
   width_box->Add(width, 0, wxALIGN_CENTER_VERTICAL);
   width_box->Add(new wxStaticText(width_box->GetStaticBox(), wxID_ANY, "pixels"), 0,
                  wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
   col->Add(width_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

   RadioGroup *hydrogens = new RadioGroup(d, "Draw Hydrogens?", { "Yes", "No" });
   col->Add(hydrogens, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

   // the window shows, and changes, one molecule at a time
   auto show = [width, hydrogens](int imol) {
      bandicoot_core_set_bond_parameters_molecule(imol);
      width->SetSelection(std::clamp(bandicoot_core_bond_width(imol), 1, 20) - 1);
      hydrogens->set_selection(bandicoot_core_draws_hydrogens(imol) ? 0 : 1);
   };
   show(molecule->get_imol());
   molecule->on_change = show;
   width->Bind(wxEVT_CHOICE, [molecule, width](wxCommandEvent &) {
      bandicoot_core_set_bond_width(molecule->get_imol(), width->GetSelection() + 1);
   });
   hydrogens->on_change = [molecule](int index) {
      bandicoot_core_set_draws_hydrogens(molecule->get_imol(), index == 0);
   };

   wxButton *close = new wxButton(d, wxID_CANCEL, "Close");
   col->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);
   destroy_on_close(d);

   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
}

void show_map_parameters(wxWindow *parent) {
   bandicoot_map_parameters p = bandicoot_core_map_parameters();
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Map Parameters");
   d->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Density Settings"), 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);

   wxFlexGridSizer *grid = new wxFlexGridSizer(4, 6, 8);
   grid->AddGrowableCol(1);
   auto add_row = [d, grid](const wxString &label, const std::string &text, const wxString &unit,
                            bool with_apply) {
      wxTextCtrl *t = new wxTextCtrl(d, wxID_ANY, wxString::FromUTF8(text), wxDefaultPosition,
                                     wxSize(160, -1), wxTE_PROCESS_ENTER);
      grid->Add(new wxStaticText(d, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(t, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
      grid->Add(new wxStaticText(d, wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL);
      wxButton *b = nullptr;
      if (with_apply) b = new wxButton(d, wxID_ANY, "Apply");
      if (b) grid->Add(b, 0, wxALIGN_CENTER_VERTICAL); else grid->AddSpacer(0);
      return std::make_pair(t, b);
   };
   auto xray = add_row("Map Radius x-ray:", p.radius_xray, utf8(angstrom), true);
   auto em = add_row("Map Radius EM:", p.radius_em, utf8(angstrom), true);
   wxTextCtrl *increment = add_row("Increment Size", p.increment, utf8("e/\xC3\x85^3"), false).first;
   wxTextCtrl *diff_increment = add_row("Diff Map Increment", p.diff_increment,
                                        utf8("e/\xC3\x85^3"), false).first;
   wxTextCtrl *sampling = add_row("Sampling Rate:", p.sampling_rate, wxEmptyString, false).first;
   col->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

   // a radius applies on its own from its button or Return, as in Coot
   for (auto r : { std::make_pair(xray, false), std::make_pair(em, true) }) {
      wxTextCtrl *t = r.first.first;
      bool is_em = r.second;
      auto apply = [t, is_em]() {
         double v;
         if (positive_from(t, v)) bandicoot_core_set_map_radius(is_em, v);
      };
      r.first.second->Bind(wxEVT_BUTTON, [apply](wxCommandEvent &) { apply(); });
      t->Bind(wxEVT_TEXT_ENTER, [apply](wxCommandEvent &) { apply(); });
   }

   // the dynamic options apply as they are ticked
   wxCheckBox *dynamic_sampling = new wxCheckBox(d, wxID_ANY, "Dynamic Map Sampling");
   wxCheckBox *dynamic_size = new wxCheckBox(d, wxID_ANY, "Dynamic Map Display Size");
   dynamic_sampling->SetValue(p.dynamic_sampling);
   dynamic_size->SetValue(p.dynamic_size);
   dynamic_sampling->Bind(wxEVT_CHECKBOX, [dynamic_sampling](wxCommandEvent &) {
      bandicoot_core_set_dynamic_map_sampling(dynamic_sampling->IsChecked());
   });
   dynamic_size->Bind(wxEVT_CHECKBOX, [dynamic_size](wxCommandEvent &) {
      bandicoot_core_set_dynamic_map_size(dynamic_size->IsChecked());
   });
   col->Add(dynamic_sampling, 0, wxLEFT | wxRIGHT | wxTOP, 10);
   col->Add(dynamic_size, 0, wxLEFT | wxRIGHT | wxTOP, 6);

   wxTextCtrl *radius_xray = xray.first, *radius_em = em.first;
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      double rx, re, inc, diff_inc, rate;
      if (! positive_from(radius_xray, rx) || ! positive_from(radius_em, re) ||
          ! positive_from(increment, inc) || ! positive_from(diff_increment, diff_inc) ||
          ! positive_from(sampling, rate))
         return;
      bandicoot_core_set_map_parameters(rx, re, inc, diff_inc, rate);
      d->Destroy();
   }, wxID_OK);
   destroy_on_close(d);
   col->Add(d->CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);

   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
}

void show_font_size(wxWindow *parent) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Font Size");
   d->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Atom Label Font Size"), 0, wxALIGN_CENTER | wxALL, 8);
   RadioGroup *size = new RadioGroup(d, wxEmptyString, { "Small", "Medium", "Large" },
                                     std::clamp(bandicoot_core_font_size(), 1, 3) - 1);
   col->Add(size, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   col->Add(ok, 0, wxEXPAND | wxALL, 10);
   ok->Bind(wxEVT_BUTTON, [d, size](wxCommandEvent &) {
      bandicoot_core_set_font_size(size->get_selection() + 1);
      d->Destroy();
   });
   destroy_on_close(d);
   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Restraints

// One monomer's restraints, a table to a tab, every cell editable (double
// click, Return or typing). Apply puts the edited restraints into the
// dictionary; the window stays open.
class RestraintsEditor : public wxDialog {
public:
   RestraintsEditor(wxWindow *parent, const std::string &comp_id,
                    const std::vector<bandicoot_restraint_table> &tables)
      : wxDialog(parent, wxID_ANY, "Restraints Editor", wxDefaultPosition, wxSize(820, 520),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        comp_id(comp_id), tables(tables) {
      SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      notebook = new wxNotebook(this, wxID_ANY);
      for (const bandicoot_restraint_table &t : tables) {
         wxGrid *grid = new wxGrid(notebook, wxID_ANY);
         grid->CreateGrid(int(t.rows.size()), int(t.columns.size()), wxGrid::wxGridSelectRows);
         grid->SetRowLabelSize(0);
         grid->EnableDragRowSize(false);
         for (size_t c = 0; c < t.columns.size(); c++)
            grid->SetColLabelValue(int(c), wxString::FromUTF8(t.columns[c]));
         for (size_t r = 0; r < t.rows.size(); r++)
            for (size_t c = 0; c < t.columns.size() && c < t.rows[r].size(); c++)
               grid->SetCellValue(int(r), int(c), wxString::FromUTF8(t.rows[r][c]));
         grid->AutoSizeColumns(false);
         notebook->AddPage(grid, wxString::FromUTF8(t.title));
         grids.push_back(grid);
      }
      col->Add(notebook, 1, wxEXPAND | wxALL, 8);

      wxBoxSizer *edit_row = new wxBoxSizer(wxHORIZONTAL);
      wxButton *add = new wxButton(this, wxID_ANY, "Add Restraint");
      wxButton *remove = new wxButton(this, wxID_ANY, "Delete Restraint");
      edit_row->Add(add, 0, wxRIGHT, 6);
      edit_row->Add(remove, 0);
      col->Add(edit_row, 0, wxLEFT | wxRIGHT, 8);

      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      wxButton *close = new wxButton(this, wxID_CANCEL, "Close");
      wxButton *save = new wxButton(this, wxID_ANY, "Save as mmCIF...");
      wxButton *apply = new wxButton(this, wxID_ANY, "Apply");
      buttons->AddStretchSpacer();
      buttons->Add(close, 0, wxRIGHT, 6);
      buttons->Add(save, 0, wxRIGHT, 6);
      buttons->Add(apply, 0);
      col->Add(buttons, 0, wxEXPAND | wxALL, 8);

      add->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { add_restraint(); });
      remove->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { delete_restraint(); });
      save->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { save_restraints(); });
      apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply_restraints(); });
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Destroy(); });
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });

      SetSizer(col);
   }

private:
   std::string comp_id;
   std::vector<bandicoot_restraint_table> tables;  // the titles and columns
   wxNotebook *notebook;
   std::vector<wxGrid *> grids;

   // The Info tab has the one row that names the monomer.
   wxGrid *editable_grid() {
      int page = notebook->GetSelection();
      if (page <= 0 || page >= int(grids.size())) { wxBell(); return nullptr; }
      return grids[page];
   }

   void add_restraint() {
      wxGrid *grid = editable_grid();
      if (! grid) return;
      grid->SaveEditControlValue();
      grid->AppendRows(1);
      int row = grid->GetNumberRows() - 1;
      grid->SetGridCursor(row, 0);
      grid->MakeCellVisible(row, 0);
      grid->SetFocus();
      grid->EnableCellEditControl();
   }

   // the row holding the cursor, or the selected one
   void delete_restraint() {
      wxGrid *grid = editable_grid();
      if (! grid) return;
      grid->SaveEditControlValue();
      grid->DisableCellEditControl();
      wxArrayInt selected = grid->GetSelectedRows();
      int row = selected.empty() ? grid->GetGridCursorRow() : selected[0];
      if (row < 0 || row >= grid->GetNumberRows()) { wxBell(); return; }
      grid->DeleteRows(row, 1);
   }

   std::vector<bandicoot_restraint_table> edited() {
      std::vector<bandicoot_restraint_table> out = tables;
      for (size_t t = 0; t < out.size() && t < grids.size(); t++) {
         wxGrid *grid = grids[t];
         // a cell still being typed in counts
         grid->SaveEditControlValue();
         out[t].rows.clear();
         for (int r = 0; r < grid->GetNumberRows(); r++) {
            std::vector<std::string> row;
            for (int c = 0; c < grid->GetNumberCols(); c++)
               row.push_back(grid->GetCellValue(r, c).utf8_string());
            out[t].rows.push_back(row);
         }
      }
      return out;
   }

   void apply_restraints() {
      std::string why_not;
      if (! bandicoot_core_apply_restraints(comp_id, edited(), why_not))
         error(this, "Could not apply restraints for", wxString::FromUTF8(comp_id),
               "Restraints Editor: " + comp_id + ": " + why_not);
   }

   void save_restraints() {
      std::vector<bandicoot_restraint_table> now = edited();
      FileRequest r;
      r.title = "Save Restraints";
      r.types = { FileType{ "mmCIF", { ".cif" } } };
      r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
      r.file_name = wxString::FromUTF8("monomer-" + comp_id + ".cif");
      wxString path = save_file(this, r);
      if (path.empty()) return;
      std::string why_not;
      if (! bandicoot_core_save_restraints(comp_id, now, path.utf8_string(), why_not))
         error(this, "Could not save restraints for", wxString::FromUTF8(comp_id),
               "Restraints Editor: " + comp_id + ": " + why_not);
   }
};

void show_restraints(wxWindow *parent) {
   std::vector<std::string> types = bandicoot_core_restraint_monomer_types();
   if (types.empty()) {
      info(parent, "No monomer restraints loaded");
      return;
   }
   wxDialog d(parent, wxID_ANY, "Choose Monomer Type");
   d.SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(&d, wxID_ANY, "Select Monomer Type for Editing"), 0, wxALL, 10);
   wxChoice *type = new wxChoice(&d, wxID_ANY);
   for (const std::string &t : types) type->Append(wxString::FromUTF8(t));
   // the last one, as Coot offered it
   type->SetSelection(int(types.size()) - 1);
   col->Add(type, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
   col->Add(d.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);
   d.SetSizerAndFit(col);
   d.CentreOnParent();
   if (d.ShowModal() != wxID_OK) return;

   std::string comp_id = chosen(type);
   std::vector<bandicoot_restraint_table> tables;
   if (! bandicoot_core_monomer_restraints(comp_id, tables)) {
      error(parent, "No restraints for", wxString::FromUTF8(comp_id),
            "Restraints Editor: no dictionary entry for " + comp_id);
      return;
   }
   RestraintsEditor *editor = new RestraintsEditor(parent, comp_id, tables);
   editor->CentreOnParent();
   editor->Show();
}

// The Edit menu's items, by the GTK build's handler names.
const std::map<std::string, std::function<void(wxWindow *)>> &edit_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "on_edit_change_chain_ids1_activate",  show_change_chain_ids },
      { "on_edit_copy_molecule1_activate",     show_copy_molecule },
      { "on_edit_copy_fragment1_activate",     show_copy_fragment },
      { "on_edit_replace_fragment1_activate",  show_replace_fragment },
      { "on_edit_replace_residue1_activate",   show_replace_residue },
      { "on_edit_merge_molecules1_activate",   show_merge_molecules },
      { "on_edit_renumber_residues1_activate", show_renumber_residues },
      { "on_residue_info2_activate", [](wxWindow *) { bandicoot_core_pick_residue_info(); } },
      { "on_bond_colours1_activate",           show_bond_colours },
      { "on_bond_parameters1_activate",        show_bond_parameters },
      { "on_density_size1_activate",           show_map_parameters },
      { "on_skeleton_colour1_activate",        show_skeleton_colour },
      { "on_skeleton_box_radius1_activate", [](wxWindow *p) {
           show_value_window(p, "Skeleton Box Size", "Skeleton Box Size", "Size",
                             bandicoot_core_skeleton_box_radius(), utf8(angstrom) + "s", 9999.9,
                             false, [](double v) { bandicoot_core_set_skeleton_box_radius(v); }); } },
      { "on_skeletonization_level1_activate", [](wxWindow *p) {
           show_value_window(p, "Skeletonization Level", "Skeletonization Level", "Level",
                             bandicoot_core_skeletonization_level(), "electrons/A^3", 9999.9,
                             true, [](double v) { bandicoot_core_set_skeletonization_level(v); }); } },
      { "on_font_size1_activate",              show_font_size },
      { "on_restraints1_activate",             show_restraints },
      { "on_pink_pointer_size1_activate", [](wxWindow *p) {
           show_value_window(p, "Pink Pointer Size", "Rotation Centre Cube Size", "Size:",
                             bandicoot_core_pointer_size(), utf8(angstrom), 1000.0,
                             false, [](double v) { bandicoot_core_set_pointer_size(v); }); } },
      { "on_background_black1_activate", [](wxWindow *) { bandicoot_core_set_background_black(true); } },
      { "on_background_white1_activate", [](wxWindow *) { bandicoot_core_set_background_black(false); } },
   };
   return actions;
}

} // namespace

void show_map_colour(wxWindow *, int imol) {
   bandicoot_core_remember_map_colour(imol);
   show_colour_panel("Map Colour Selection", panel_colour(bandicoot_core_map_colour(imol)),
                     [imol](const PanelColour &c) { bandicoot_core_set_map_colour(imol, core_colour(c)); },
                     [imol](bool kept) { if (! kept) bandicoot_core_restore_map_colour(imol); });
}

bool is_edit_action(const std::string &action) {
   return edit_actions().count(action) > 0;
}

bool run_edit_action(wxWindow *parent, const std::string &action) {
   auto it = edit_actions().find(action);
   if (it == edit_actions().end()) return false;
   it->second(parent);
   return true;
}

void install_edit_menu(wxWindow *parent) {
   owner = parent;
   bandicoot_residue_info_view v;
   // after the click that picked the atom has been handled
   v.show = [](const bandicoot_residue_info &info) {
      owner->CallAfter([info]() { show_residue_info(info); });
   };
   bandicoot_core_set_residue_info_view(v);
}

} // namespace wxui

// src/wxUI/main/modelling-menu.cc

#include "wxUI/main/modelling-menu.hh"
#include "wxUI/main/other-tools.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/titled-panel.hh"

#include "wxUI/widgets/entry-row.hh"
#include "wxUI/widgets/molecule-chooser.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace wxui {

namespace {

// ------------------------------------------------------------ Add N-linked Glycan

// The tree types, as bandicoot_glyco_button_states() names them.
const std::pair<const char *, const char *> tree_types[] = {
   { "High Mannose", "oligomannose" }, { "Hybrid (Mammal)", "hybrid-mammal" },
   { "Complex (Mammal)", "complex-mammal" }, { "Complex (Plant)", "complex-plant" },
   { "Expert User Mode", "expert-user-mode" } };

// Each button adds its sugar to the residue now centred; those that cannot be
// linked to it for the chosen tree type are greyed, and follow the view as it
// is recentred.
class GlycanWindow : public wxFrame {
public:
   explicit GlycanWindow(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "Add N-linked Glycan", wxDefaultPosition, wxSize(300, 600)) {
      SetBackgroundColour(palette::face());
      bandicoot_core_glycan_opened();
      wxPanel *p = new wxPanel(this);
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      std::vector<wxString> labels;
      for (const auto &t : tree_types) labels.push_back(t.first);
      tree = new RadioGroup(p, "Glycan tree type", labels, 0);
      tree->on_change = [this](int) { refresh(); };
      col->Add(tree, 0, wxEXPAND | wxBOTTOM, 4);

      BevelButton *update = new BevelButton(p, wxID_ANY, "Update for Current Residue");
      update->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { refresh(); });
      col->Add(update, 0, wxEXPAND | wxBOTTOM, 4);

      wxScrolledWindow *list = new wxScrolledWindow(p, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                    wxVSCROLL | wxBORDER_THEME);
      list->SetBackgroundColour(palette::face());
      list->SetScrollRate(0, 10);
      wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
      for (const bandicoot_glycan_link &link : bandicoot_core_glycan_links()) {
         BevelButton *b = new BevelButton(list, wxID_ANY, wxString::FromUTF8(link.label));
         b->Bind(wxEVT_BUTTON, [link](wxCommandEvent &) { bandicoot_core_add_glycan_link(link); });
         rows->Add(b, 0, wxEXPAND | wxALL, 2);
         links.push_back({ link.label, b });
      }
      list->SetSizer(rows);
      col->Add(list, 1, wxEXPAND | wxBOTTOM, 4);

      BevelButton *refine = new BevelButton(p, wxID_ANY, "Refine Tree");
      refine->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_refine_glycan_tree(); });
      col->Add(refine, 0, wxEXPAND | wxBOTTOM, 6);
      BevelButton *close = new BevelButton(p, wxID_ANY, "Close");
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      col->Add(close, 0, wxEXPAND);

      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 6);
      p->SetSizer(outer);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
      bandicoot_core_set_glycan_view([]() { if (shown) shown->refresh(); });
      refresh();
   }

   ~GlycanWindow() override {
      shown = nullptr;
      bandicoot_core_set_glycan_view(nullptr);
   }

   static GlycanWindow *shown;

   void refresh() {
      std::map<std::string, bool> on = bandicoot_core_glycan_link_states(tree_types[tree->get_selection()].second);
      for (auto &l : links) {
         auto it = on.find(l.first);
         l.second->Enable(it == on.end() || it->second);   // a sugar it does not judge stays usable
      }
   }

private:
   RadioGroup *tree;
   std::vector<std::pair<std::string, BevelButton *>> links;
};

GlycanWindow *GlycanWindow::shown = nullptr;

void show_glycan_window(wxWindow *parent) {
   if (! GlycanWindow::shown) {
      GlycanWindow::shown = new GlycanWindow(parent);
      GlycanWindow::shown->CentreOnParent();
      GlycanWindow::shown->Show();
   }
   GlycanWindow::shown->Raise();
}

// ------------------------------------------------------------ actions on a chosen model

void choose_model_for(wxWindow *parent, const std::string &id, const wxString &title, const wxString &prompt) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to work on");
      return;
   }
   ChooserDialog *d = new ChooserDialog(parent, title, prompt, models);
   d->on_ok = [id](int imol) { bandicoot_core_modelling_on_model(id, imol); };
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ chooser + entry forms

// A form with an optional model chooser and one field; act returns false,
// with the reason, to keep it open.
void entry_form(wxWindow *parent, const wxString &title, const wxString &prompt, bool with_model,
                const wxString &field, const wxString &initial,
                std::function<bool(int imol, const std::string &text, std::string &why_not)> act) {
   MoleculeChooser::molecule_list models;
   if (with_model) {
      models = bandicoot_core_goto_molecules();
      if (models.empty()) {
         info(parent, "No molecules to work on");
         return;
      }
   }
   EntryDialog *d = new EntryDialog(parent, title, prompt, models);
   d->add_field(field, initial);
   d->on_ok = [title, act](EntryDialog &dialog) {
      std::string text = dialog.field(0)->get_text().Trim().Trim(false).utf8_string();
      if (text.empty()) { wxBell(); return false; }
      std::string why_not;
      if (! act(dialog.get_imol(), text, why_not)) {
         error(&dialog, "Could not do " + title + " for", wxString::FromUTF8(text), title.utf8_string() + ": " + why_not);
         return false;
      }
      return true;
   };
   d->CentreOnParent();
   d->Show();
}

void monomer_from_dictionary(wxWindow *p) {
   entry_form(p, "Monomer from Dictionary", "Pull coordinates from CIF dictionary for 3-letter code:", false,
              wxEmptyString, wxEmptyString, [](int, const std::string &t, std::string &why_not) {
                 return bandicoot_core_monomer_from_dictionary(t, why_not);
              });
}

void new_molecule_by_sphere(wxWindow *p) {
   entry_form(p, "New Molecule by Sphere", "Choose a molecule to select a sphere of atoms from:", true,
              "Radius:", "10.0", [](int imol, const std::string &t, std::string &why_not) {
                 double r;
                 if (! wxString::FromUTF8(t).ToCDouble(&r) || r <= 0) { why_not = "the radius is not a positive number"; return false; }
                 return bandicoot_core_new_molecule_by_sphere(imol, float(r), why_not);
              });
}

void new_molecule_by_symop(wxWindow *p) {
   entry_form(p, "New Molecule from Symmetry Op", "Molecule to generate a symmetry copy from:", true,
              "SymOp:", "X,Y,Z", [](int imol, const std::string &t, std::string &why_not) {
                 return bandicoot_core_new_molecule_by_symop(imol, t, why_not);
              });
}

void residue_type_selection(wxWindow *p) {
   entry_form(p, "Residue Type Selection", "Choose a molecule to select residues from:", true,
              "Residue Type:", wxEmptyString, [](int imol, const std::string &t, std::string &why_not) {
                 return bandicoot_core_residue_type_selection(imol, t, why_not);
              });
}

void rename_residue(wxWindow *p) {
   entry_form(p, "Rename Residue", "New residue name for the centred residue:", false,
              wxEmptyString, wxEmptyString, [](int, const std::string &t, std::string &why_not) {
                 return bandicoot_core_rename_active_residue(t, why_not);
              });
}

void fetch_pdbe_ligand(wxWindow *p) {
   entry_form(p, "Fetch PDBe Ligand Description", "Fetch PDBe Ligand Description for comp-id:", false,
              wxEmptyString, wxEmptyString, [](int, const std::string &t, std::string &) {
                 bandicoot_core_fetch_pdbe_ligand(t);
                 return true;
              });
}

// ------------------------------------------------------------ fitting and sequence forms

wxDialog *new_dialog(wxWindow *parent, const wxString &title) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   return d;
}

void show(wxDialog *d, wxBoxSizer *col, const wxString &ok_label = "OK") {
   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
   buttons->GetAffirmativeButton()->SetLabel(ok_label);
   col->Add(buttons, 0, wxEXPAND | wxTOP, 10);
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
}

// A model's chains in a drop-down.
void fill_chains(wxChoice *c, int imol) {
   c->Clear();
   for (const std::string &chain : bandicoot_core_chain_ids(imol)) c->Append(wxString::FromUTF8(chain));
   if (c->GetCount() > 0) c->SetSelection(0);
}

std::string chain_of(wxChoice *c) {
   return c->GetSelection() == wxNOT_FOUND ? std::string() : c->GetStringSelection().utf8_string();
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

wxTextCtrl *number_entry(wxWindow *parent) {
   return new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1));
}

// "Chain: [..]" followed by labelled number entries, on one row.
std::tuple<wxChoice *, std::vector<wxTextCtrl *>>
chain_row(wxWindow *parent, wxSizer *into, MoleculeChooser *model, const std::vector<wxString> &number_labels) {
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   row->Add(new wxStaticText(parent, wxID_ANY, "Chain:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   wxChoice *chain = new wxChoice(parent, wxID_ANY);
   fill_chains(chain, model->get_imol());
   row->Add(chain, 0, wxRIGHT, 10);
   std::vector<wxTextCtrl *> numbers;
   for (const wxString &label : number_labels) {
      row->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      numbers.push_back(number_entry(parent));
      row->Add(numbers.back(), 0, wxRIGHT, 10);
   }
   into->Add(row, 0, wxALL, 4);
   model->on_change = [chain](int imol) { fill_chains(chain, imol); };
   return std::make_tuple(chain, numbers);
}

// A sequence file shown by name, with a button to choose it; none at first.
class SequenceFileRow : public wxPanel {
public:
   explicit SequenceFileRow(wxWindow *parent) : wxPanel(parent) {
      SetBackgroundColour(parent->GetBackgroundColour());
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      shown = new wxTextCtrl(this, wxID_ANY, "(None)", wxDefaultPosition, wxSize(260, -1), wxTE_READONLY);
      row->Add(shown, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      wxButton *choose = new wxButton(this, wxID_ANY, "Choose...");
      row->Add(choose, 0);
      SetSizer(row);
      choose->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
         FileRequest r;
         r.title = "Select sequence file";
         r.types = { { "Sequence files", { ".fasta", ".fa", ".pir", ".seq", ".txt" } } };
         std::vector<wxString> paths = open_files(this, r);
         if (paths.empty()) return;
         path = paths.front();
         shown->SetValue(path);
         shown->SetInsertionPointEnd();
      });
   }
   std::string get_path() const { return path.utf8_string(); }
private:
   wxTextCtrl *shown;
   wxString path;
};

void rigid_body_fit_range(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to fit");
      return;
   }
   wxDialog *d = new_dialog(parent, "Rigid Body Fit Residue Range");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Rigid-body fit a residue range (needs a refinement map):"), 0, wxBOTTOM, 6);
   MoleculeChooser *model = new MoleculeChooser(d, wxEmptyString, models);
   col->Add(model, 0, wxEXPAND | wxBOTTOM, 4);
   wxChoice *chain;
   std::vector<wxTextCtrl *> range;
   std::tie(chain, range) = chain_row(d, col, model, { "Res start:", "Res end:" });
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      int first, last;
      if (! int_from(range[0], first) || ! int_from(range[1], last)) return;
      std::string why_not;
      if (! bandicoot_core_rigid_body_fit_range(model->get_imol(), chain_of(chain), first, last, why_not)) {
         error(d, "Could not rigid-body fit", wxString::FromUTF8(chain_of(chain)) + wxString::Format(" %d-%d", first, last),
               "Rigid Body Fit Residue Range: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   show(d, col);
}

void superpose_ligands(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules with ligands");
      return;
   }
   wxDialog *d = new_dialog(parent, "Superpose Ligands");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Overlay the moving ligand onto the reference ligand:"), 0, wxBOTTOM, 8);
   auto ligand = [d, col, &models](const wxString &title) {
      wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, d, title);
      wxWindow *b = box->GetStaticBox();
      MoleculeChooser *model = new MoleculeChooser(b, wxEmptyString, models);
      box->Add(model, 0, wxEXPAND | wxALL, 4);
      wxChoice *chain;
      std::vector<wxTextCtrl *> resno;
      std::tie(chain, resno) = chain_row(b, box, model, { "Res no:" });
      col->Add(box, 0, wxEXPAND | wxBOTTOM, 8);
      return std::make_tuple(model, chain, resno[0]);
   };
   MoleculeChooser *ref_model, *mov_model;
   wxChoice *ref_chain, *mov_chain;
   wxTextCtrl *ref_resno, *mov_resno;
   std::tie(ref_model, ref_chain, ref_resno) = ligand("Reference ligand (stays put)");
   std::tie(mov_model, mov_chain, mov_resno) = ligand("Moving ligand (whole molecule moves)");
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      int ref, mov;
      if (! int_from(ref_resno, ref) || ! int_from(mov_resno, mov)) return;
      std::string why_not;
      if (! bandicoot_core_superpose_ligands(ref_model->get_imol(), chain_of(ref_chain), ref,
                                             mov_model->get_imol(), chain_of(mov_chain), mov, why_not)) {
         error(d, "Could not superpose", "the ligands", "Superpose Ligands: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   show(d, col, "Superpose");
}

void associate_sequence(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to associate a sequence with");
      return;
   }
   wxDialog *d = new_dialog(parent, "Associate Sequence to Chain");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Associate a sequence file with a chain:"), 0, wxBOTTOM, 6);
   MoleculeChooser *model = new MoleculeChooser(d, wxEmptyString, models);
   col->Add(model, 0, wxEXPAND | wxBOTTOM, 4);
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   row->Add(new wxStaticText(d, wxID_ANY, "Chain:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   wxChoice *chain = new wxChoice(d, wxID_ANY);
   fill_chains(chain, model->get_imol());
   row->Add(chain, 0, wxRIGHT, 16);
   wxRadioButton *fasta = new wxRadioButton(d, wxID_ANY, "FASTA", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
   wxRadioButton *pir = new wxRadioButton(d, wxID_ANY, "PIR");
   fasta->SetValue(true);
   row->Add(fasta, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
   row->Add(pir, 0, wxALIGN_CENTER_VERTICAL);
   col->Add(row, 0, wxALL, 4);
   SequenceFileRow *file = new SequenceFileRow(d);
   col->Add(file, 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
   wxCheckBox *all = new wxCheckBox(d, wxID_ANY, "Assign all protein chains (ignore chain above)");
   col->Add(all, 0);
   model->on_change = [chain](int imol) { fill_chains(chain, imol); };
   all->Bind(wxEVT_CHECKBOX, [chain, all](wxCommandEvent &) { chain->Enable(! all->IsChecked()); });
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      if (file->get_path().empty()) { wxBell(); return; }
      std::string why_not;
      if (! bandicoot_core_associate_sequence(model->get_imol(), chain_of(chain), file->get_path(), pir->GetValue(),
                                              all->IsChecked(), why_not)) {
         error(d, "Could not associate the sequence", wxString::FromUTF8(file->get_path()),
               "Associate Sequence to Chain: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   show(d, col);
}

void cootaneer(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to assign a sequence to");
      return;
   }
   wxDialog *d = new_dialog(parent, "Assign Sequence (Cootaneer)");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Assign sidechains from a sequence (needs a refinement map):"), 0, wxBOTTOM, 6);
   MoleculeChooser *model = new MoleculeChooser(d, wxEmptyString, models);
   col->Add(model, 0, wxEXPAND | wxBOTTOM, 6);
   SequenceFileRow *file = new SequenceFileRow(d);
   col->Add(file, 0, wxEXPAND | wxBOTTOM, 6);
   wxCheckBox *refine = new wxCheckBox(d, wxID_ANY, "Refine (fit protein) afterwards");
   col->Add(refine, 0);
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      std::string why_not;
      bool done;
      {
         wxBusyCursor busy;
         done = bandicoot_core_cootaneer(model->get_imol(), file->get_path(), refine->IsChecked(), why_not);
      }
      if (! done) {
         error(d, "Could not assign the sequence", "with Cootaneer", "Assign Sequence (Cootaneer): " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   show(d, col);
}

// Stays open: each button adds one more solvent molecule.
class SolventWindow : public wxFrame {
public:
   explicit SolventWindow(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "Add Solvent Molecules", wxDefaultPosition, wxSize(340, 600)) {
      SetBackgroundColour(palette::face());
      wxPanel *p = new wxPanel(this);
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(new wxStaticText(p, wxID_ANY, "Add a solvent molecule to the chosen molecule\n"
                                             "(jiggle-fits into the refinement map if one is set):"),
               0, wxBOTTOM, 6);
      model = new MoleculeChooser(p, wxEmptyString, bandicoot_core_goto_molecules());
      col->Add(model, 0, wxEXPAND | wxBOTTOM, 6);

      wxScrolledWindow *list = new wxScrolledWindow(p, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                    wxVSCROLL | wxBORDER_THEME);
      list->SetBackgroundColour(palette::face());
      list->SetScrollRate(0, 10);
      wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
      for (const std::string &comp_id : bandicoot_core_solvent_comp_ids()) {
         BevelButton *b = new BevelButton(list, wxID_ANY, wxString::FromUTF8(comp_id));
         b->Bind(wxEVT_BUTTON, [this, comp_id](wxCommandEvent &) { add(comp_id); });
         rows->Add(b, 0, wxEXPAND | wxALL, 2);
      }
      list->SetSizer(rows);
      col->Add(list, 1, wxEXPAND | wxBOTTOM, 6);
      col->Add(new wxStaticLine(p), 0, wxEXPAND | wxBOTTOM, 6);

      wxBoxSizer *other = new wxBoxSizer(wxHORIZONTAL);
      other->Add(new wxStaticText(p, wxID_ANY, "Other code:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      wxTextCtrl *code = new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1),
                                        wxTE_PROCESS_ENTER);
      other->Add(code, 0, wxRIGHT, 6);
      wxButton *add_other = new wxButton(p, wxID_ANY, "Add");
      other->Add(add_other, 0);
      col->Add(other, 0, wxBOTTOM, 8);
      auto add_code = [this, code](wxCommandEvent &) {
         std::string c = code->GetValue().Trim().Trim(false).Upper().utf8_string();
         if (c.empty()) { wxBell(); return; }
         add(c);
      };
      add_other->Bind(wxEVT_BUTTON, add_code);
      code->Bind(wxEVT_TEXT_ENTER, add_code);

      BevelButton *close = new BevelButton(p, wxID_ANY, "Close");
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      col->Add(close, 0, wxEXPAND);

      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 8);
      p->SetSizer(outer);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { shown = nullptr; Destroy(); });
      Bind(wxEVT_ACTIVATE, [this](wxActivateEvent &e) {
         if (e.GetActive()) model->set_molecules(bandicoot_core_goto_molecules());
         e.Skip();
      });
   }

   static SolventWindow *shown;

private:
   void add(const std::string &comp_id) {
      if (model->get_imol() < 0) { info(this, "No molecule to add solvent to"); return; }
      wxBusyCursor busy;
      bandicoot_core_add_solvent(model->get_imol(), comp_id);
   }

   MoleculeChooser *model;
};

SolventWindow *SolventWindow::shown = nullptr;

void add_solvent(wxWindow *parent) {
   if (bandicoot_core_goto_molecules().empty()) {
      info(parent, "No molecules to add solvent to");
      return;
   }
   if (! SolventWindow::shown) {
      SolventWindow::shown = new SolventWindow(parent);
      SolventWindow::shown->CentreOnParent();
      SolventWindow::shown->Show();
   }
   SolventWindow::shown->Raise();
}

// ------------------------------------------------------------ Ligand Restraints

// One window serves the Modelling menu (every ligand) and coordinate loads
// (only what lacks restraints). Its rows are re-derived whenever it is looked
// at; a row switched off stays off until a load brings it again.
class RestraintsWindow : public wxFrame {
public:
   explicit RestraintsWindow(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "Ligand Restraints", wxDefaultPosition, wxSize(460, 320)) {
      SetBackgroundColour(palette::face());
      wxPanel *p = new wxPanel(this);
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      header = new wxStaticText(p, wxID_ANY, idle_header);
      col->Add(header, 0, wxEXPAND | wxBOTTOM, 4);
      list = new wxScrolledWindow(p, wxID_ANY, wxDefaultPosition, wxSize(-1, 140), wxVSCROLL | wxBORDER_THEME);
      list->SetBackgroundColour(palette::face());
      list->SetScrollRate(0, 10);
      list->SetSizer(new wxBoxSizer(wxVERTICAL));
      col->Add(list, 1, wxEXPAND | wxBOTTOM, 6);
      col->Add(new wxStaticText(p, wxID_ANY,
                                "Refinement cannot restrain them until a dictionary is\n"
                                "read in (File -> Import CIF dictionary...) or generated\n"
                                "here from the model geometry."), 0, wxBOTTOM, 10);
      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      buttons->AddStretchSpacer();
      close = new wxButton(p, wxID_ANY, "Close");
      generate = new wxButton(p, wxID_ANY, "Generate Restraints");
      buttons->Add(close, 0, wxRIGHT, 8);
      buttons->Add(generate, 0);
      col->Add(buttons, 0, wxEXPAND);
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 10);
      p->SetSizer(outer);

      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      generate->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { run(); });
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &e) {
         if (running) { e.Veto(); return; }
         finish();
      });
      // re-validated when looked at: a molecule deleted or a dictionary read
      // since loses its row here rather than at Generate
      Bind(wxEVT_ACTIVATE, [this](wxActivateEvent &e) {
         if (e.GetActive() && ! running) CallAfter([this]() { if (rebuild() == 0) finish(); });
         e.Skip();
      });
   }

   ~RestraintsWindow() override { if (shown == this) shown = nullptr; }

   // Destroying is deferred: stop offering this window at once.
   void finish() {
      if (shown == this) shown = nullptr;
      Destroy();
   }

   static RestraintsWindow *shown;
   bool show_all = false;
   std::set<std::string> unchecked;

   // The number of rows now listed.
   int rebuild() {
      remember();
      list->DestroyChildren();
      boxes.clear();
      for (const bandicoot_restraint_row &r : bandicoot_core_restraint_rows(show_all)) {
         wxCheckBox *b = new wxCheckBox(list, wxID_ANY, wxString::FromUTF8(r.label));
         // ticked only when nothing describes it: ticking one that has
         // restraints is asking to replace them
         b->SetValue(r.needed && unchecked.count(r.comp_id) == 0);
         list->GetSizer()->Add(b, 0, wxALL, 3);
         boxes.push_back({ r.comp_id, b });
      }
      list->FitInside();
      list->Layout();
      return int(boxes.size());
   }

private:
   const char *idle_header = "These components loaded without restraints:";
   wxStaticText *header;
   wxScrolledWindow *list;
   wxButton *close, *generate;
   std::vector<std::pair<std::string, wxCheckBox *>> boxes;
   bool running = false;

   void remember() {
      for (auto &b : boxes) {
         if (b.second->GetValue()) unchecked.erase(b.first);
         else unchecked.insert(b.first);
      }
   }

   void run() {
      std::vector<std::string> wanted;
      for (auto &b : boxes) if (b.second->GetValue()) wanted.push_back(b.first);
      if (wanted.empty()) {
         info(this, "No components are selected.");
         return;
      }
      std::string report;
      bool ran;
      {
         // generation blocks, so refinement cannot start against a
         // half-written dictionary; the header says where it has got to
         running = true;
         wxWindowDisabler others(this);
         list->Enable(false);
         close->Enable(false);
         generate->Enable(false);
         wxBusyCursor busy;
         ran = bandicoot_core_generate_restraints(show_all, wanted, [this](const std::string &m) {
            header->SetLabel(wxString::FromUTF8(m));
            wxYield();
         }, report);
         header->SetLabel(idle_header);
         list->Enable(true);
         close->Enable(true);
         generate->Enable(true);
         running = false;
      }
      // success says nothing: the rows going is the report
      if (! report.empty()) info(this, wxString::FromUTF8(report));
      // listing everything, a finished run ends the exchange; listing what is
      // missing, an empty list does
      if ((show_all && ran) || rebuild() == 0) finish();
   }
};

RestraintsWindow *RestraintsWindow::shown = nullptr;

void open_restraints(wxWindow *parent, bool show_all, const std::vector<std::string> &fresh, bool announce_empty) {
   bool is_new = RestraintsWindow::shown == nullptr;
   if (is_new) RestraintsWindow::shown = new RestraintsWindow(parent);
   RestraintsWindow *w = RestraintsWindow::shown;
   if (show_all) w->show_all = true;   // a later load adds rows without changing it
   for (const std::string &c : fresh) w->unchecked.erase(c);
   if (w->rebuild() == 0) {
      bool all = w->show_all;
      w->finish();
      if (announce_empty)
         info(parent, all ? "No ligands found in the loaded molecules."
                          : "Every component in the loaded molecules has restraints.");
      return;
   }
   if (is_new) {
      w->CentreOnParent();
      w->Show();
   }
   w->Raise();
}

// The Modelling menu's windows, by their menu ids.
const std::map<std::string, std::function<void(wxWindow *)>> &modelling_actions() {
   static std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "BGLYCO_OPEN_DIALOG",              show_glycan_window },
      { "BMOD_MONOMER_FROM_DICT",          monomer_from_dictionary },
      { "BMOD_NEW_MOL_SPHERE",             new_molecule_by_sphere },
      { "BMOD_NEW_MOL_SYMOP",              new_molecule_by_symop },
      { "BMOD_RESIDUE_TYPE_SEL",           residue_type_selection },
      { "BMOD_FETCH_PDBE_LIGAND",          fetch_pdbe_ligand },
      { "BMOD_RENAME_RESIDUE",             rename_residue },
      { "BMOD_RIGID_BODY_RANGES",          rigid_body_fit_range },
      { "BMOD_ADD_SOLVENT",                add_solvent },
      { "BMOD_SUPERPOSE_LIGANDS",          superpose_ligands },
      { "BMOD_ASSOC_SEQUENCE",             associate_sequence },
      { "BMOD_COOTANEER",                  cootaneer },
      { "on_other_modelling_tools1_activate", show_other_tools },
      { "BMOD_GENERATE_LIGAND_RESTRAINTS", [](wxWindow *) { bandicoot_core_open_ligand_restraints(); } },
   };
   static bool filled = false;
   if (! filled) {
      const struct { const char *id, *title, *prompt; } choosers[] = {
         { "BMOD_ARRANGE_WATERS",     "Arrange Waters Around Protein", "Arrange waters in molecule:" },
         { "BMOD_MERGE_WATER_CHAINS", "Merge Water Chains",            "Merge water chains in molecule:" },
         { "BMOD_RENUMBER_WATERS",    "Renumber Waters",               "Renumber waters of molecule:" },
         { "BMOD_ASSIGN_HETATM_MOL",  "Assign HETATM to Molecule",     "Assign HETATMs as per PDB definition:" },
         { "BMOD_FIX_NOMENCLATURE",   "Fix Nomenclature Errors",       "Fix nomenclature errors in molecule:" },
         { "BMOD_REORDER_CHAINS",     "Reorder Chains",                "Sort chain IDs in molecule:" },
         { "BMOD_RIGID_BODY_FIT_MOL", "Rigid Body Fit Molecule",       "Rigid body fit whole molecule:" },
         { "BMOD_USE_SEGIDS",         "Use SEGIDs",                    "Exchange chain IDs for SEG IDs in molecule:" },
         { "BMOD_RES_ALT_CONFS",      "Residues with Alt Confs",       "Which molecule to check for alt confs?" },
         { "BMOD_RES_CIS_PEPTIDES",   "Residues with Cis Peptide Bonds", "Check for cis peptides in molecule:" },
         { "BMOD_RES_MISSING_ATOMS",  "Residues with Missing Atoms",   "Which molecule to check for missing atoms?" },
         { "BMOD_RES_ZERO_OCC",       "Atoms with Zero Occupancies",   "Which molecule to check for zero-occupancy atoms?" },
      };
      for (const auto &c : choosers) {
         std::string id = c.id;
         wxString title = c.title, prompt = c.prompt;
         actions[id] = [id, title, prompt](wxWindow *p) { choose_model_for(p, id, title, prompt); };
      }
      filled = true;
   }
   return actions;
}

} // namespace

bool is_modelling_action(const std::string &action) {
   return modelling_actions().count(action) > 0;
}

bool run_modelling_action(wxWindow *parent, const std::string &action) {
   auto it = modelling_actions().find(action);
   if (it == modelling_actions().end()) return false;
   it->second(parent);
   return true;
}

void install_modelling_menu(wxWindow *parent) {
   bandicoot_core_set_restraints_view([parent](bool show_all, const std::vector<std::string> &fresh,
                                               bool announce_empty) {
      open_restraints(parent, show_all, fresh, announce_empty);
   });
}

} // namespace wxui

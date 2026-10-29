// src/wxUI/main/other-tools.cc

#include "wxUI/main/other-tools.hh"
#include "wxUI/main/ligand-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/main/calculate-menu.hh"
#include "wxUI/main/delete-waters.hh"
#include "wxUI/widgets/molecule-chooser.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/utils.h>

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace wxui {

namespace {

wxDialog *new_dialog(wxWindow *parent, const wxString &title) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   return d;
}

void show(wxDialog *d, wxBoxSizer *col, const wxString &ok_label) {
   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
   buttons->GetAffirmativeButton()->SetLabel(ok_label);
   col->Add(buttons, 0, wxEXPAND | wxTOP, 10);
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
}

void closes_on_cancel(wxDialog *d) {
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
}

// ------------------------------------------------------------ Place Strand Here

void show_place_strand(wxWindow *parent) {
   wxDialog *d = new_dialog(parent, "Place Strand Here");
   closes_on_cancel(d);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Estimated number of residues in strand:"), 0, wxBOTTOM, 6);
   wxChoice *n = new wxChoice(d, wxID_ANY);
   for (int i = 4; i <= 12; i++) n->Append(wxString::Format("%d", i));
   n->SetSelection(3);   // 7
   col->Add(n, 0, wxEXPAND);
   d->Bind(wxEVT_BUTTON, [d, n](wxCommandEvent &) {
      std::string why_not;
      wxBusyCursor busy;
      if (! bandicoot_core_place_strand_here(n->GetSelection() + 4, why_not)) {
         error(d, "Could not place a strand", "here", "Place Strand Here: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   show(d, col, "Go");
}

// ------------------------------------------------------------ Find Secondary Structure

wxChoice *choice_of(wxWindow *parent, const std::vector<wxString> &items, int initial) {
   wxChoice *c = new wxChoice(parent, wxID_ANY);
   for (const wxString &i : items) c->Append(i);
   c->SetSelection(initial);
   return c;
}

void show_find_secondary_structure(wxWindow *parent) {
   wxDialog *d = new_dialog(parent, "Fast Secondary Structure Search");
   closes_on_cancel(d);
   wxFlexGridSizer *grid = new wxFlexGridSizer(5, 6, 6);
   const std::vector<wxString> templates = { "normal", "strict", "high resolution", "low resolution" };
   const std::vector<wxString> lengths = { "5", "7", "9", "11" };   // length = 2 * index + 5
   auto row = [d, grid, &templates, &lengths](const wxString &what, int initial_template, int initial_length) {
      wxCheckBox *use = new wxCheckBox(d, wxID_ANY, "find " + what + ", using");
      use->SetValue(true);
      wxChoice *t = choice_of(d, templates, initial_template);
      wxChoice *n = choice_of(d, lengths, initial_length);
      grid->Add(use, 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(t, 0, wxEXPAND);
      grid->Add(new wxStaticText(d, wxID_ANY, "template of"), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(n, 0);
      grid->Add(new wxStaticText(d, wxID_ANY, "amino acids."), 0, wxALIGN_CENTER_VERTICAL);
      return std::make_tuple(use, t, n);
   };
   wxCheckBox *helices, *strands;
   wxChoice *helix_template, *helix_length, *strand_template, *strand_length;
   std::tie(helices, helix_template, helix_length) = row("helices", 0, 1);
   std::tie(strands, strand_template, strand_length) = row("strands", 1, 0);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(grid, 0, wxBOTTOM, 8);
   wxBoxSizer *limit_row = new wxBoxSizer(wxHORIZONTAL);
   wxCheckBox *limit = new wxCheckBox(d, wxID_ANY, "limit search to a");
   wxChoice *radius = choice_of(d, { "10", "20", "30", "40", "50" }, 1);   // radius = 10 * (index + 1)
   limit_row->Add(limit, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   limit_row->Add(radius, 0, wxRIGHT, 6);
   limit_row->Add(new wxStaticText(d, wxID_ANY, "A sphere about the cursor."), 0, wxALIGN_CENTER_VERTICAL);
   col->Add(limit_row, 0);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   wxButton *reference = new wxButton(d, wxID_ANY, "Reference");
   buttons->Add(reference, 0, wxRIGHT, 8);
   buttons->AddStretchSpacer();
   buttons->Add(new wxButton(d, wxID_CANCEL, "Cancel"), 0, wxRIGHT, 8);
   buttons->Add(new wxButton(d, wxID_OK, "OK"), 0);
   col->Add(buttons, 0, wxEXPAND | wxTOP, 12);
   reference->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) {
      info(d, "If using the Coot sequencing tool (\"Cootaneer\") or Fast Secondary Structure Search, "
              "please cite:\n\nCowtan K (2008). Fitting molecular fragments into electron density. "
              "Acta Cryst. D64, 83-89.");
   });
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      std::string why_not;
      bool found;
      {
         wxBusyCursor busy;
         found = bandicoot_core_find_secondary_structure(
            helices->IsChecked(), helix_template->GetSelection(), 2 * helix_length->GetSelection() + 5,
            strands->IsChecked(), strand_template->GetSelection(), 2 * strand_length->GetSelection() + 5,
            limit->IsChecked() ? 10.0f * (radius->GetSelection() + 1) : 0.0f, why_not);
      }
      if (! found) {
         error(d, "Could not find secondary structure", "in the map", "Find Secondary Structure: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Build Nucleic Acid

void show_build_nucleic_acid(wxWindow *parent) {
   wxDialog *d = new_dialog(parent, "Automated Nucleic Acid building");
   closes_on_cancel(d);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   row->Add(new wxStaticText(d, wxID_ANY, "Search radius:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
   wxTextCtrl *radius = new wxTextCtrl(d, wxID_ANY, "6.0", wxDefaultPosition, wxSize(160, -1), wxTE_PROCESS_ENTER);
   row->Add(radius, 1);
   col->Add(row, 0, wxEXPAND);
   auto go = [d, radius]() {
      double r;
      if (! radius->GetValue().Trim().Trim(false).ToCDouble(&r) || r <= 0) {
         wxBell();
         radius->SetFocus();
         return;
      }
      std::string why_not;
      bool found;
      {
         wxBusyCursor busy;
         found = bandicoot_core_find_nucleic_acids(float(r), why_not);
      }
      if (! found) {
         error(d, "Could not build nucleic acid", "here", "Build Nucleic Acid: " + why_not);
         return;
      }
      d->Destroy();
   };
   d->Bind(wxEVT_BUTTON, [go](wxCommandEvent &) { go(); }, wxID_OK);
   radius->Bind(wxEVT_TEXT_ENTER, [go](wxCommandEvent &) { go(); });
   radius->SelectAll();
   show(d, col, "OK");
}

// ------------------------------------------------------------ Add OXT

void fill_chains(wxChoice *c, int imol) {
   c->Clear();
   for (const std::string &chain : bandicoot_core_chain_ids(imol)) c->Append(wxString::FromUTF8(chain));
   if (c->GetCount() > 0) c->SetSelection(0);
}

void show_add_oxt(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to add OXT to");
      return;
   }
   wxDialog *d = new_dialog(parent, "Add OXT to Chain");
   closes_on_cancel(d);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, d, "Add OXT to Chain:");
   wxWindow *b = box->GetStaticBox();
   wxFlexGridSizer *grid = new wxFlexGridSizer(2, 6, 6);
   grid->AddGrowableCol(1);
   MoleculeChooser *model = new MoleculeChooser(b, wxEmptyString, models);
   wxChoice *chain = new wxChoice(b, wxID_ANY);
   fill_chains(chain, model->get_imol());
   grid->Add(new wxStaticText(b, wxID_ANY, "Molecule:"), 0, wxALIGN_CENTER_VERTICAL);
   grid->Add(model, 1, wxEXPAND);
   grid->Add(new wxStaticText(b, wxID_ANY, "Chain:"), 0, wxALIGN_CENTER_VERTICAL);
   grid->Add(chain, 1, wxEXPAND);
   box->Add(grid, 0, wxEXPAND | wxALL, 4);
   wxRadioButton *terminus = new wxRadioButton(b, wxID_ANY, "At C terminus", wxDefaultPosition, wxDefaultSize,
                                               wxRB_GROUP);
   wxRadioButton *at_residue = new wxRadioButton(b, wxID_ANY, "At Residue:");
   wxTextCtrl *resno = new wxTextCtrl(b, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(120, -1));
   terminus->SetValue(true);
   resno->Enable(false);
   box->Add(terminus, 0, wxALL, 4);
   wxBoxSizer *residue_row = new wxBoxSizer(wxHORIZONTAL);
   residue_row->Add(at_residue, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   residue_row->Add(resno, 0);
   box->Add(residue_row, 0, wxALL, 4);
   col->Add(box, 0, wxEXPAND);
   model->on_change = [chain](int imol) { fill_chains(chain, imol); };
   auto by_residue = [resno, at_residue](wxCommandEvent &) { resno->Enable(at_residue->GetValue()); };
   terminus->Bind(wxEVT_RADIOBUTTON, by_residue);
   at_residue->Bind(wxEVT_RADIOBUTTON, by_residue);
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      long r = 0;
      bool c_terminus = terminus->GetValue();
      if (! c_terminus && ! resno->GetValue().Trim().Trim(false).ToLong(&r)) {
         wxBell();
         resno->SetFocus();
         return;
      }
      std::string ch = chain->GetSelection() == wxNOT_FOUND ? std::string() : chain->GetStringSelection().utf8_string();
      std::string why_not;
      if (! bandicoot_core_add_oxt(model->get_imol(), ch, c_terminus, int(r), why_not)) {
         error(d, "Could not add OXT to chain", wxString::FromUTF8(ch), "Add OXT: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   show(d, col, "Add it");
}

// ------------------------------------------------------------ DNA & RNA Models

void show_nucleotide_builder(wxWindow *parent) {
   wxDialog *d = new_dialog(parent, "Nucleotide Builder");
   closes_on_cancel(d);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Nucleotide Builder"), 0, wxALIGN_CENTER | wxBOTTOM, 8);
   auto boxed = [d, col](const wxString &title, wxWindow *(*make)(wxWindow *)) {
      wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, d, title);
      wxWindow *w = make(box->GetStaticBox());
      box->Add(w, 0, wxEXPAND | wxALL, 2);
      col->Add(box, 0, wxEXPAND | wxBOTTOM, 4);
      return w;
   };
   wxChoice *type = static_cast<wxChoice *>(boxed("Nucleotide Type:", [](wxWindow *p) -> wxWindow * {
      return choice_of(p, { "RNA", "DNA" }, 0); }));
   wxChoice *form = static_cast<wxChoice *>(boxed("Form:", [](wxWindow *p) -> wxWindow * {
      return choice_of(p, { "A", "B" }, 0); }));
   wxChoice *strands = static_cast<wxChoice *>(boxed("Strandedness:", [](wxWindow *p) -> wxWindow * {
      return choice_of(p, { "Single Stranded", "Double Stranded" }, 0); }));
   wxTextCtrl *sequence = static_cast<wxTextCtrl *>(boxed("Sequence:", [](wxWindow *p) -> wxWindow * {
      return new wxTextCtrl(p, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(220, -1)); }));
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      std::string seq = sequence->GetValue().Trim().Trim(false).utf8_string();
      if (seq.empty()) { wxBell(); sequence->SetFocus(); return; }
      std::string why_not;
      if (! bandicoot_core_ideal_nucleic_acid(type->GetSelection() == 1, form->GetSelection() == 1,
                                              strands->GetSelection() == 0, seq, why_not)) {
         error(d, "Could not build a nucleic acid model of", wxString::FromUTF8(seq), "DNA & RNA Models: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   show(d, col, "OK");
}

// ------------------------------------------------------------ C-alpha Baton Mode

void show_baton_build_params(wxWindow *parent) {
   wxDialog *d = new_dialog(parent, "Baton Build Parameters");
   closes_on_cancel(d);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   wxFlexGridSizer *grid = new wxFlexGridSizer(2, 6, 6);
   wxTextCtrl *resno = new wxTextCtrl(d, wxID_ANY, "1", wxDefaultPosition, wxSize(80, -1));
   wxTextCtrl *chain = new wxTextCtrl(d, wxID_ANY, "A", wxDefaultPosition, wxSize(80, -1));
   grid->Add(new wxStaticText(d, wxID_ANY, "Starting Residue Number:"), 0, wxALIGN_CENTER_VERTICAL);
   grid->Add(resno, 0);
   grid->Add(new wxStaticText(d, wxID_ANY, "Chain ID:"), 0, wxALIGN_CENTER_VERTICAL);
   grid->Add(chain, 0);
   col->Add(grid, 0, wxBOTTOM, 8);
   wxCheckBox *backwards = new wxCheckBox(d, wxID_ANY, "Build Backwards");
   col->Add(backwards, 0);
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      long r;
      if (! resno->GetValue().Trim().Trim(false).ToLong(&r)) { wxBell(); resno->SetFocus(); return; }
      bandicoot_core_baton_build_params(int(r), chain->GetValue().Trim().Trim(false).utf8_string(),
                                        backwards->IsChecked());
      d->Destroy();
   }, wxID_OK);
   show(d, col, "OK");
}

// The baton follows the mouse; the buttons place atoms and adjust it. Closing
// puts the baton away.
class BatonWindow : public wxFrame {
public:
   explicit BatonWindow(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "Baton", wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX)) {
      SetBackgroundColour(palette::face());
      wxPanel *p = new wxPanel(this);
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      auto heading = [p, col](const wxString &text) {
         wxStaticText *t = new wxStaticText(p, wxID_ANY, text);
         t->SetFont(t->GetFont().Bold());
         col->Add(t, 0, wxTOP | wxBOTTOM, 4);
      };
      auto op = [p, col](const wxString &label, std::function<void()> action) {
         BevelButton *b = new BevelButton(p, wxID_ANY, label);
         b->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
         col->Add(b, 0, wxEXPAND | wxLEFT | wxBOTTOM, 4);
      };
      heading("Baton Position");
      op("Accept", []() { bandicoot_core_baton_op("accept"); });
      op("Try Another", []() { bandicoot_core_baton_op("try another"); });
      op("Previous Tip Position", []() { bandicoot_core_baton_op("previous"); });
      op("Undo", []() { bandicoot_core_baton_op("undo"); });
      heading("Baton Length");
      op("Lengthen", []() { bandicoot_core_baton_op("lengthen"); });
      op("Shorten", []() { bandicoot_core_baton_op("shorten"); });
      heading("Build Parameters");
      op("Baton-build params...", [this]() { show_baton_build_params(this); });
      BevelButton *dismiss = new BevelButton(p, wxID_ANY, "Dismiss");
      dismiss->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      col->Add(dismiss, 0, wxALIGN_RIGHT | wxTOP, 12);
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 10);
      p->SetSizer(outer);
      outer->SetSizeHints(this);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
         bandicoot_core_baton_stop();
         shown = nullptr;
         Destroy();
      });
   }

   static BatonWindow *shown;
};

BatonWindow *BatonWindow::shown = nullptr;

void open_baton_window(wxWindow *parent) {
   if (! BatonWindow::shown) {
      BatonWindow::shown = new BatonWindow(parent);
      BatonWindow::shown->CentreOnParent();
      BatonWindow::shown->Show();
   }
   BatonWindow::shown->Raise();
}

// Without a skeleton, Map Skeleton comes first; the baton starts once it has one.
void start_baton_mode(wxWindow *parent) {
   if (bandicoot_core_baton_start()) {
      open_baton_window(parent);
      return;
   }
   show_map_skeleton(parent, "CA-Baton mode needs a skeleton", [parent]() {
      if (bandicoot_core_baton_start()) open_baton_window(parent);
   });
}

// ------------------------------------------------------------ Multi-Residue Torsion

// Waits for the residues to twist; Apply offers their bonds.
void pick_torsion_residues(wxWindow *parent) {
   bandicoot_core_multi_torsion_start();
   wxDialog *d = new_dialog(parent, "Pick Residues");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Pick (atoms in) residues..."), 0, wxALIGN_CENTER);
   auto cancel = [d]() {
      bandicoot_core_multi_torsion_finish(false);
      d->Destroy();
   };
   d->Bind(wxEVT_BUTTON, [cancel](wxCommandEvent &) { cancel(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [cancel](wxCloseEvent &) { cancel(); });
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) {
      d->Destroy();
      bandicoot_core_multi_torsion_picked();
   }, wxID_OK);
   show(d, col, "Apply");
}

// A bond button makes it the one dragging turns; OK keeps the result.
class TorsionWindow : public wxDialog {
public:
   TorsionWindow(wxWindow *parent, const std::vector<std::string> &bonds)
      : wxDialog(parent, wxID_ANY, "Multi-Residue Torsion", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
      SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(new wxStaticText(this, wxID_ANY, "Torsions"), 0, wxALIGN_CENTER | wxBOTTOM, 4);
      wxScrolledWindow *list = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(300, 260),
                                                    wxVSCROLL | wxBORDER_THEME);
      list->SetBackgroundColour(palette::face());
      list->SetScrollRate(0, 10);
      wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
      reverse = new wxCheckBox(this, wxID_ANY, "Reverse");
      for (size_t i = 0; i < bonds.size(); i++) {
         BevelButton *b = new BevelButton(list, wxID_ANY, wxString::FromUTF8(bonds[i]));
         b->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent &) {
            bandicoot_core_multi_torsion_select(int(i), reverse->IsChecked());
         });
         rows->Add(b, 0, wxEXPAND | wxALL, 2);
      }
      list->SetSizer(rows);
      col->Add(list, 1, wxEXPAND | wxBOTTOM, 6);
      col->Add(reverse, 0, wxBOTTOM, 6);
      reverse->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
         bandicoot_core_multi_torsion_reverse(reverse->IsChecked());
      });
      col->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 10);
      SetSizerAndFit(outer);
      Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { finish(true); }, wxID_OK);
      Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { finish(false); }, wxID_CANCEL);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { finish(false); });
   }

private:
   wxCheckBox *reverse;

   void finish(bool accept) {
      bandicoot_core_multi_torsion_finish(accept);
      Destroy();
   }
};

// ------------------------------------------------------------ the window

// Coot's widget names for the tools that wait for a pick.
const char *const cis_trans    = "cis_trans_conversion_toggle_button";
const char *const ca_zone      = "model_refine_dialog_db_main_togglebutton";
const char *const reverse_dir  = "reverse_fragment_direction_togglebutton";
const char *const base_pair    = "other_tools_base_pair_toggle_button";

class OtherToolsWindow : public wxFrame {
public:
   explicit OtherToolsWindow(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "Other Modelling Tools", wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX)) {
      SetBackgroundColour(palette::face());
      p = new wxPanel(this);
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      wxStaticBoxSizer *tools = new wxStaticBoxSizer(wxVERTICAL, p, wxEmptyString);
      wxWindow *tb = tools->GetStaticBox();
      add(tools, tb, "Find Waters...", "add-water.svg", [this]() { show_find_waters(GetParent()); });
      add(tools, tb, "Find Ligands...", "go-to-ligand.svg", [this]() { show_find_ligands(GetParent()); });
      add(tools, tb, "Find Secondary Structure...", "secondary-structure.svg", [this]() { show_find_secondary_structure(this); });
      add(tools, tb, "Build Nucleic Acid...", "dna.svg", [this]() { show_build_nucleic_acid(this); });
      add_pick(tools, tb, "Cis <-> Trans", cis_trans);
      add(tools, tb, "C-alpha Baton Mode...", "", [this]() { start_baton_mode(GetParent()); });
      add_pick(tools, tb, "Ca Zone -> Mainchain", ca_zone);
      add(tools, tb, "Add OXT to Residue...", "", [this]() { show_add_oxt(this); });
      add(tools, tb, "Multi-Residue Torsion", "", [this]() { pick_torsion_residues(GetParent()); });
      add_pick(tools, tb, "Reverse Direction...", reverse_dir);
      add(tools, tb, "Place Helix Here", "", [this]() {
         std::string why_not;
         wxBusyCursor busy;
         if (! bandicoot_core_place_helix_here(why_not))
            error(this, "Could not place a helix", "here", "Place Helix Here: " + why_not);
      });
      add(tools, tb, "Place Strand Here", "", [this]() { show_place_strand(this); });
      add(tools, tb, "DNA & RNA Models...", "", [this]() { show_nucleotide_builder(this); });
      add_pick(tools, tb, "Base Pair...", base_pair);
      add(tools, tb, "Choose \"Undo\" Molecule...", "", [this]() { choose_undo_molecule(); });
      add(tools, tb, "Make Link (click 2 atoms)...", "", []() { bandicoot_core_make_link_pick(); });
      col->Add(tools, 0, wxEXPAND | wxBOTTOM, 8);

      wxStaticBoxSizer *active = new wxStaticBoxSizer(wxVERTICAL, p, "Modelling Tools (active molecule/chain)");
      wxWindow *ab = active->GetStaticBox();
      for (const char *tool : { "Fill Partial Residues", "Delete Hydrogen Atoms",
                                "Delete Side-chains for Active Chain" }) {
         std::string t = tool;
         add(active, ab, t, "", [this, t]() {
            std::string why_not;
            if (! bandicoot_core_active_molecule_tool(t, why_not))
               error(this, "Could not " + wxString::FromUTF8(t), "(no active molecule)", t + ": " + why_not);
         });
      }
      add(active, ab, "Ligand from SMILES...", "", [this]() { ligand_from_smiles(); });
      col->Add(active, 0, wxEXPAND | wxBOTTOM, 10);

      BevelButton *close = new BevelButton(p, wxID_ANY, "Close");
      close->set_icon(icon("stock-menu-close.png", wxSize(16, 16)));
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      col->Add(close, 0, wxALIGN_RIGHT);

      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 8);
      p->SetSizer(outer);
      outer->SetSizeHints(this);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
         release_all(nullptr);
         shown = nullptr;
         Destroy();
      });
   }

   static OtherToolsWindow *shown;

   void release(const std::string &widget) {
      auto it = picks.find(widget);
      if (it != picks.end()) it->second->set_on(false);
   }

private:
   wxPanel *p;
   std::map<std::string, BevelButton *> picks;

   // A full-width button; no action leaves it greyed (not yet in wx).
   void add(wxSizer *into, wxWindow *box, const std::string &label, const std::string &icon_file,
            std::function<void()> action) {
      BevelButton *b = new BevelButton(box, wxID_ANY, wxString::FromUTF8(label));
      if (! icon_file.empty()) b->set_icon(icon(icon_file, wxSize(16, 16)));
      if (action)
         b->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
      else
         b->Enable(false);
      into->Add(b, 0, wxEXPAND | wxALL, 2);
   }

   // A toggle that stays down while the core waits for its pick. Only one
   // waits at a time.
   void add_pick(wxSizer *into, wxWindow *box, const std::string &label, const std::string &widget) {
      BevelButton *b = new BevelButton(box, wxID_ANY, wxString::FromUTF8(label));
      b->set_toggle(true);
      b->Bind(wxEVT_TOGGLEBUTTON, [this, b, widget](wxCommandEvent &e) {
         bool on = e.GetInt() != 0;
         if (on) release_all(b);
         bandicoot_core_tools_pick(widget, on);
      });
      into->Add(b, 0, wxEXPAND | wxALL, 2);
      picks[widget] = b;
   }

   // Stops every pick but keep's.
   void release_all(BevelButton *keep) {
      for (auto &pick : picks) {
         if (pick.second == keep || ! pick.second->is_on()) continue;
         pick.second->set_on(false);
         bandicoot_core_tools_pick(pick.first, false);
      }
   }

   void choose_undo_molecule() {
      MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
      if (models.empty()) {
         info(this, "No molecules to undo in");
         return;
      }
      ChooserDialog *d = new ChooserDialog(this, "Choose \"Undo\" Molecule", "Molecule for Undo operations:", models);
      d->on_ok = [](int imol) { bandicoot_core_set_undo_molecule(imol); };
      d->CentreOnParent();
      d->Show();
   }

   void ligand_from_smiles() {
      std::string smiles, code;
      if (! ask_ligand_from_smiles(this, smiles, code)) return;
      std::string message;
      bool made;
      {
         wxBusyCursor busy;
         made = bandicoot_core_ligand_from_smiles(smiles, code, message);
      }
      if (! made) error(this, "Could not build a ligand from", wxString::FromUTF8(smiles), message);
   }
};

OtherToolsWindow *OtherToolsWindow::shown = nullptr;

} // namespace

void show_other_tools(wxWindow *parent) {
   if (! OtherToolsWindow::shown) {
      OtherToolsWindow::shown = new OtherToolsWindow(parent);
      OtherToolsWindow::shown->CentreOnParent();
      OtherToolsWindow::shown->Show();
   }
   OtherToolsWindow::shown->Raise();
}

void release_tools_toggle(const std::string &widget) {
   if (OtherToolsWindow::shown) OtherToolsWindow::shown->release(widget);
}

void show_multi_torsion(wxWindow *parent, const std::vector<std::string> &bonds) {
   if (bonds.empty()) {
      bandicoot_core_multi_torsion_finish(false);
      info(parent, "No rotatable bonds in the picked residues");
      return;
   }
   TorsionWindow *w = new TorsionWindow(parent, bonds);
   w->CentreOnParent();
   w->Show();
}

bool ask_ligand_from_smiles(wxWindow *parent, std::string &smiles, std::string &code) {
   wxDialog d(parent, wxID_ANY, "Ligand from SMILES");
   d.SetBackgroundColour(palette::face());
   wxFlexGridSizer *grid = new wxFlexGridSizer(2, 6, 6);
   grid->AddGrowableCol(1);
   wxTextCtrl *code_entry = new wxTextCtrl(&d, wxID_ANY, "LIG");
   code_entry->SetMaxLength(3);
   wxTextCtrl *smiles_entry = new wxTextCtrl(&d, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(320, -1),
                                             wxTE_PROCESS_ENTER);
   grid->Add(new wxStaticText(&d, wxID_ANY, "3-letter code:"), 0, wxALIGN_CENTER_VERTICAL);
   grid->Add(code_entry, 1, wxEXPAND);
   grid->Add(new wxStaticText(&d, wxID_ANY, "SMILES:"), 0, wxALIGN_CENTER_VERTICAL);
   grid->Add(smiles_entry, 1, wxEXPAND);
   smiles_entry->Bind(wxEVT_TEXT_ENTER, [&d](wxCommandEvent &) { d.EndModal(wxID_OK); });
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(grid, 0, wxEXPAND | wxBOTTOM, 10);
   col->Add(d.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d.SetSizerAndFit(outer);
   d.CentreOnParent();
   smiles_entry->SetFocus();
   if (d.ShowModal() != wxID_OK) return false;
   smiles = smiles_entry->GetValue().Trim().Trim(false).utf8_string();
   code = code_entry->GetValue().Trim().Trim(false).utf8_string();
   return ! smiles.empty();
}

} // namespace wxui

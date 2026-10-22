// src/wxUI/main/calculate-menu.cc

#include "wxUI/main/calculate-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/main/pandda-inspect.hh"
#include "wxUI/main/python-console.hh"
#include "wxUI/main/refine-dialogs.hh"
#include "wxUI/widgets/molecule-chooser.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace wxui {

namespace {

// A dialog's two buttons, the affirmative one relabelled.
wxStdDialogButtonSizer *dialog_buttons(wxDialog *d, const wxString &ok_label = "OK") {
   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
   buttons->GetAffirmativeButton()->SetLabel(ok_label);
   return buttons;
}

wxDialog *new_dialog(wxWindow *parent, const wxString &title) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   return d;
}

void show(wxDialog *d, wxBoxSizer *col) {
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
}

// The map to start on: the refinement map if it is offered, else the first.
void choose_start(MoleculeChooser *chooser, int preferred) {
   if (preferred < 0 || ! chooser->set_imol(preferred))
      chooser->set_imol(bandicoot_core_refinement_map());
}

// ------------------------------------------------------------ Map Sharpening/Blurring

// The map follows the slider; OK closes. A changed map shows its own B-factor.
void show_map_sharpening(wxWindow *parent) {
   MoleculeChooser::molecule_list maps = bandicoot_core_maps_for_fitting();
   if (maps.empty()) {
      info(parent, "No maps to sharpen or blur");
      return;
   }
   wxDialog *d = new_dialog(parent, "Map Sharpening/Blurring");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Map to Sharpen/Blur:"), 0, wxALIGN_CENTER | wxBOTTOM, 6);
   MoleculeChooser *chooser = new MoleculeChooser(d, wxEmptyString, maps);
   choose_start(chooser, -1);
   col->Add(chooser, 0, wxEXPAND | wxBOTTOM, 8);

   float limit = bandicoot_core_map_sharpening_limit();
   StepSlider *slider = new StepSlider(d, -limit, limit, 1.0, bandicoot_core_map_sharpening(chooser->get_imol()), 1);
   col->Add(slider, 0, wxEXPAND);
   wxBoxSizer *ends = new wxBoxSizer(wxHORIZONTAL);
   ends->Add(new wxStaticText(d, wxID_ANY, "Sharpen"), 0);
   ends->AddStretchSpacer();
   ends->Add(new wxStaticText(d, wxID_ANY, "Blur"), 0);
   col->Add(ends, 0, wxEXPAND | wxBOTTOM, 10);

   wxBoxSizer *tools = new wxBoxSizer(wxHORIZONTAL);
   wxButton *optimize = new wxButton(d, wxID_ANY, "Optimize B");
   wxButton *reset = new wxButton(d, wxID_ANY, "Reset");
   tools->Add(optimize, 0, wxRIGHT, 8);
   tools->Add(reset, 0);
   col->Add(tools, 0, wxBOTTOM, 10);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   buttons->AddStretchSpacer();
   buttons->Add(new wxButton(d, wxID_OK, "OK"), 0);
   col->Add(buttons, 0, wxEXPAND);

   slider->on_change = [chooser](double b) { bandicoot_core_sharpen_map(chooser->get_imol(), float(b)); };
   chooser->on_change = [slider](int imol) { slider->set_value(bandicoot_core_map_sharpening(imol)); };
   auto apply = [chooser, slider](float b) {
      slider->set_value(b);
      bandicoot_core_sharpen_map(chooser->get_imol(), float(slider->get_value()));
   };
   optimize->Bind(wxEVT_BUTTON, [d, chooser, apply](wxCommandEvent &) {
      bool converged = true;
      wxBusyCursor busy;
      float b = bandicoot_core_optimal_sharpening(chooser->get_imol(), converged);
      if (! converged) info(d, "Optimisation did not converge. The value may be bogus.");
      apply(b);
   });
   reset->Bind(wxEVT_BUTTON, [apply](wxCommandEvent &) { apply(0.0f); });
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_OK);
   d->SetMinSize(wxSize(420, -1));
   show(d, col);
}

// ------------------------------------------------------------ Map Skeleton

// On / Off shows the chosen map's skeleton as it is; OK applies the choice.
void show_map_skeleton(wxWindow *parent) {
   MoleculeChooser::molecule_list maps = bandicoot_core_maps_for_fitting();
   if (maps.empty()) {
      info(parent, "No maps to skeletonize");
      return;
   }
   wxDialog *d = new_dialog(parent, "Map Skeleton");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Skeletonize Map:"), 0, wxALIGN_CENTER | wxBOTTOM, 6);
   MoleculeChooser *chooser = new MoleculeChooser(d, wxEmptyString, maps);
   choose_start(chooser, bandicoot_core_skeleton_map());
   col->Add(chooser, 0, wxEXPAND | wxBOTTOM, 8);
   RadioGroup *on_off = new RadioGroup(d, wxEmptyString, { "On", "Off" },
                                       bandicoot_core_map_skeletonized(chooser->get_imol()) ? 0 : 1);
   col->Add(on_off, 0, wxEXPAND | wxBOTTOM, 8);
   wxCheckBox *prune = new wxCheckBox(d, wxID_ANY, "Prune sidechains");
   col->Add(prune, 0, wxBOTTOM, 10);
   col->Add(dialog_buttons(d), 0, wxEXPAND);

   chooser->on_change = [on_off](int imol) { on_off->set_selection(bandicoot_core_map_skeletonized(imol) ? 0 : 1); };
   d->Bind(wxEVT_BUTTON, [d, chooser, on_off, prune](wxCommandEvent &) {
      bandicoot_core_set_map_skeleton(chooser->get_imol(), on_off->get_selection() == 0, prune->IsChecked());
      d->Destroy();
   }, wxID_OK);
   show(d, col);
}

// ------------------------------------------------------------ NCS Maps

void show_ncs_maps(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_ncs_models();
   MoleculeChooser::molecule_list maps = bandicoot_core_maps_for_fitting();
   if (models.empty()) {
      info(parent, "No models with NCS");
      return;
   }
   if (maps.empty()) {
      info(parent, "No maps for NCS maps");
      return;
   }
   wxDialog *d = new_dialog(parent, "NCS Maps");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   auto labels = [](const MoleculeChooser::molecule_list &l) {
      std::vector<wxString> v;
      for (const auto &m : l) v.push_back(wxString::FromUTF8(m.second));
      return v;
   };
   RadioGroup *model = new RadioGroup(d, "Select Model", labels(models), 0);
   RadioGroup *map = new RadioGroup(d, "Select Map", labels(maps), 0);
   col->Add(model, 0, wxEXPAND | wxBOTTOM, 8);
   col->Add(map, 0, wxEXPAND | wxBOTTOM, 10);
   col->Add(dialog_buttons(d), 0, wxEXPAND);
   d->Bind(wxEVT_BUTTON, [d, model, map, models, maps](wxCommandEvent &) {
      bandicoot_core_make_ncs_maps(models[model->get_selection()].first, maps[map->get_selection()].first);
      d->Destroy();
   }, wxID_OK);
   show(d, col);
}

// ------------------------------------------------------------ Updating Maps

void show_updating_maps(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   MoleculeChooser::molecule_list maps = bandicoot_core_maps_for_fitting();
   MoleculeChooser::molecule_list differences = bandicoot_core_difference_maps();
   if (models.empty()) {
      info(parent, "No models to update maps from");
      return;
   }
   if (maps.empty()) {
      info(parent, "No maps with data to update from");
      return;
   }
   if (differences.empty()) {
      info(parent, "No difference map to update");
      return;
   }
   wxDialog *d = new_dialog(parent, "Updating Maps");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   auto row = [d, col](const wxString &label, const MoleculeChooser::molecule_list &l) {
      col->Add(new wxStaticText(d, wxID_ANY, label), 0, wxBOTTOM, 4);
      MoleculeChooser *c = new MoleculeChooser(d, wxEmptyString, l);
      col->Add(c, 0, wxEXPAND | wxBOTTOM, 8);
      return c;
   };
   MoleculeChooser *model = row("Model:", models);
   MoleculeChooser *map = row("Map (with data attached):", maps);
   choose_start(map, -1);
   MoleculeChooser *difference = row("Difference map to update:", differences);
   wxCheckBox *automatic = new wxCheckBox(d, wxID_ANY, "Auto-update as the model changes");
   automatic->SetValue(true);
   col->Add(automatic, 0, wxBOTTOM, 10);
   col->Add(dialog_buttons(d), 0, wxEXPAND);
   d->Bind(wxEVT_BUTTON, [d, model, map, difference, automatic](wxCommandEvent &) {
      bandicoot_core_update_maps(model->get_imol(), map->get_imol(), difference->get_imol(),
                                 automatic->IsChecked());
      d->Destroy();
   }, wxID_OK);
   d->SetMinSize(wxSize(420, -1));
   show(d, col);
}

// ------------------------------------------------------------ superpose and move

// A model's chains in a drop-down, keeping the one asked for if it is there.
void fill_chains(wxChoice *c, int imol, const std::string &keep = std::string()) {
   c->Clear();
   for (const std::string &chain : bandicoot_core_chain_ids(imol)) c->Append(wxString::FromUTF8(chain));
   if (c->GetCount() == 0) return;
   int i = keep.empty() ? wxNOT_FOUND : c->FindString(wxString::FromUTF8(keep), true);
   c->SetSelection(i == wxNOT_FOUND ? 0 : i);
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

void show_move_molecule_here(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to move");
      return;
   }
   ChooserDialog *d = new ChooserDialog(parent, "Move Molecule Here", "Move Molecule Here:", models);
   if (wxWindow *ok = d->FindWindow(wxID_OK)) ok->SetLabel("Move It");
   d->on_ok = [](int imol) { bandicoot_core_move_molecule_here(imol); };
   d->CentreOnParent();
   d->Show();
}

// Fit applies and keeps the window for another try; Close remembers the
// values for next time.
void show_lsq_superpose(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to superpose");
      return;
   }
   bandicoot_lsq remembered = bandicoot_core_lsq_remembered();
   wxDialog *d = new wxDialog(parent, wxID_ANY, "LSQ Superpose");
   d->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   wxStaticBoxSizer *molecules = new wxStaticBoxSizer(wxVERTICAL, d, wxEmptyString);
   wxWindow *mb = molecules->GetStaticBox();
   molecules->Add(new wxStaticText(mb, wxID_ANY, "Reference Molecule:"), 0, wxLEFT | wxTOP, 4);
   MoleculeChooser *reference = new MoleculeChooser(mb, wxEmptyString, models);
   reference->set_imol(remembered.imol_reference);
   molecules->Add(reference, 0, wxEXPAND | wxALL, 4);
   molecules->Add(new wxStaticText(mb, wxID_ANY, "Moving Molecule:"), 0, wxLEFT, 4);
   MoleculeChooser *moving = new MoleculeChooser(mb, wxEmptyString, models);
   if (! moving->set_imol(remembered.imol_moving) && models.size() > 1) moving->set_imol(models[1].first);
   molecules->Add(moving, 0, wxEXPAND | wxALL, 4);
   col->Add(molecules, 0, wxEXPAND | wxBOTTOM, 8);

   wxStaticBoxSizer *ranges = new wxStaticBoxSizer(wxVERTICAL, d, wxEmptyString);
   wxWindow *rb = ranges->GetStaticBox();
   wxFlexGridSizer *grid = new wxFlexGridSizer(6, 6, 6);
   auto range_row = [rb, grid](const wxString &label, int start, int end) {
      wxTextCtrl *from = new wxTextCtrl(rb, wxID_ANY, wxString::Format("%d", start), wxDefaultPosition, wxSize(80, -1));
      wxTextCtrl *to = new wxTextCtrl(rb, wxID_ANY, wxString::Format("%d", end), wxDefaultPosition, wxSize(80, -1));
      wxChoice *chain = new wxChoice(rb, wxID_ANY);
      grid->Add(new wxStaticText(rb, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(from, 0);
      grid->Add(new wxStaticText(rb, wxID_ANY, "to"), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(to, 0);
      grid->Add(new wxStaticText(rb, wxID_ANY, "Chain"), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(chain, 0);
      return std::make_tuple(from, to, chain);
   };
   wxTextCtrl *ref_from, *ref_to, *mov_from, *mov_to;
   wxChoice *ref_chain, *mov_chain;
   std::tie(ref_from, ref_to, ref_chain) = range_row("Reference Residue Range", remembered.reference_start,
                                                     remembered.reference_end);
   std::tie(mov_from, mov_to, mov_chain) = range_row("Moving Residue Range", remembered.moving_start,
                                                     remembered.moving_end);
   fill_chains(ref_chain, reference->get_imol(), remembered.reference_chain);
   fill_chains(mov_chain, moving->get_imol(), remembered.moving_chain);
   ranges->Add(grid, 0, wxALL, 4);
   col->Add(ranges, 0, wxEXPAND | wxBOTTOM, 8);

   RadioGroup *match = new RadioGroup(d, "Match Atoms", { "All Atoms", "Mainchain", "C-alphas" }, 0);
   col->Add(match, 0, wxEXPAND | wxBOTTOM, 8);
   wxCheckBox *copy = new wxCheckBox(d, wxID_ANY, "Move copy of Moving Structure");
   col->Add(copy, 0, wxBOTTOM, 10);
   col->Add(dialog_buttons(d, "Fit"), 0, wxEXPAND);
   if (wxWindow *close = d->FindWindow(wxID_CANCEL)) close->SetLabel("Close");

   reference->on_change = [ref_chain](int imol) { fill_chains(ref_chain, imol); };
   moving->on_change = [mov_chain](int imol) { fill_chains(mov_chain, imol); };

   // the window's values; false (after a beep) if a range is not a number
   auto read = [=](bandicoot_lsq &l) {
      l.imol_reference = reference->get_imol();
      l.imol_moving = moving->get_imol();
      if (! int_from(ref_from, l.reference_start) || ! int_from(ref_to, l.reference_end) ||
          ! int_from(mov_from, l.moving_start) || ! int_from(mov_to, l.moving_end))
         return false;
      l.reference_chain = chain_of(ref_chain);
      l.moving_chain = chain_of(mov_chain);
      l.match = match->get_selection();
      l.move_copy = copy->IsChecked();
      return true;
   };
   d->Bind(wxEVT_BUTTON, [d, read](wxCommandEvent &) {
      bandicoot_lsq l;
      if (! read(l)) return;
      std::string why_not;
      if (! bandicoot_core_lsq_fit(l, why_not))
         error(d, "Could not superpose", "by least squares", "LSQ Superpose: " + why_not);
   }, wxID_OK);
   auto close = [d, read]() {
      bandicoot_lsq l;
      if (read(l)) bandicoot_core_remember_lsq(l);
      d->Destroy();
   };
   d->Bind(wxEVT_BUTTON, [close](wxCommandEvent &) { close(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [close](wxCloseEvent &) { close(); });
   show(d, col);
}

void show_ssm_superpose(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to superpose");
      return;
   }
   wxDialog *d = new_dialog(parent, "SSM Superpose");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Superpose Protein Molecules using\nSecondary Structure Matching",
                             wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL),
            0, wxALIGN_CENTER | wxBOTTOM, 10);

   // a structure, and the chain to use if one is asked for
   auto structure = [d, col, &models](const wxString &title) {
      wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, d, title);
      wxWindow *b = box->GetStaticBox();
      MoleculeChooser *chooser = new MoleculeChooser(b, wxEmptyString, models);
      wxCheckBox *use_chain = new wxCheckBox(b, wxID_ANY, "Use Specific Chain");
      wxChoice *chain = new wxChoice(b, wxID_ANY);
      fill_chains(chain, chooser->get_imol());
      chain->Enable(false);
      box->Add(chooser, 0, wxEXPAND | wxALL, 4);
      box->Add(use_chain, 0, wxALL, 4);
      box->Add(chain, 0, wxEXPAND | wxALL, 4);
      col->Add(box, 0, wxEXPAND | wxBOTTOM, 10);
      chooser->on_change = [chain](int imol) { fill_chains(chain, imol); };
      use_chain->Bind(wxEVT_CHECKBOX, [chain, use_chain](wxCommandEvent &) { chain->Enable(use_chain->IsChecked()); });
      return std::make_tuple(chooser, use_chain, chain);
   };
   MoleculeChooser *reference, *moving;
   wxCheckBox *ref_use, *mov_use;
   wxChoice *ref_chain, *mov_chain;
   std::tie(reference, ref_use, ref_chain) = structure("Reference Structure");
   std::tie(moving, mov_use, mov_chain) = structure("Moving Structure");
   wxCheckBox *copy = new wxCheckBox(d, wxID_ANY, "Move copy of Moving Structure");
   col->Add(copy, 0, wxBOTTOM, 10);
   col->Add(dialog_buttons(d, "Apply"), 0, wxEXPAND);

   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      bandicoot_core_ssm_superpose(reference->get_imol(), moving->get_imol(),
                                   ref_use->IsChecked() ? chain_of(ref_chain) : std::string(),
                                   mov_use->IsChecked() ? chain_of(mov_chain) : std::string(),
                                   copy->IsChecked());
      d->Destroy();
   }, wxID_OK);
   d->SetMinSize(wxSize(380, -1));
   show(d, col);
}

void show_align_and_mutate(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to mutate");
      return;
   }
   wxDialog *d = new_dialog(parent, "Align & Mutate");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Align and Mutate Molecule:"), 0, wxBOTTOM, 4);
   MoleculeChooser *chooser = new MoleculeChooser(d, wxEmptyString, models);
   col->Add(chooser, 0, wxEXPAND | wxBOTTOM, 6);
   wxBoxSizer *chain_row = new wxBoxSizer(wxHORIZONTAL);
   wxChoice *chain = new wxChoice(d, wxID_ANY);
   fill_chains(chain, chooser->get_imol());
   chain_row->Add(new wxStaticText(d, wxID_ANY, "Chain ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   chain_row->Add(chain, 1);
   col->Add(chain_row, 0, wxEXPAND | wxBOTTOM, 8);
   col->Add(new wxStaticText(d, wxID_ANY, "to (single letter) sequence:"), 0, wxBOTTOM, 4);
   wxTextCtrl *sequence = new wxTextCtrl(d, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 100),
                                         wxTE_MULTILINE);
   col->Add(sequence, 1, wxEXPAND | wxBOTTOM, 8);
   wxCheckBox *autofit = new wxCheckBox(d, wxID_ANY, "Autofit chain after alignment and mutations?");
   col->Add(autofit, 0, wxBOTTOM, 10);
   col->Add(dialog_buttons(d, "Apply Sequence to Model"), 0, wxEXPAND);

   chooser->on_change = [chain](int imol) { fill_chains(chain, imol); };
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      std::string text = sequence->GetValue().utf8_string();
      if (text.find_first_not_of(" \t\r\n") == std::string::npos) { wxBell(); sequence->SetFocus(); return; }
      std::string why_not;
      if (! bandicoot_core_align_and_mutate(chooser->get_imol(), chain_of(chain), text, autofit->IsChecked(),
                                            why_not)) {
         error(d, "Could not align and mutate", wxString::FromUTF8(chain_of(chain)), "Align & Mutate: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   d->SetMinSize(wxSize(400, -1));
   show(d, col);
}

// ------------------------------------------------------------ Mutate Residue Range, Fit Loop

// A round light: green when on, red when off.
class MatchLight : public wxPanel {
public:
   explicit MatchLight(wxWindow *parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(18, 18)) {
      SetBackgroundColour(parent->GetBackgroundColour());
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
         wxPaintDC dc(this);
         dc.SetPen(wxPen(wxColour(0x50, 0x50, 0x50)));
         dc.SetBrush(wxBrush(on ? wxColour(0x2e, 0xb8, 0x3c) : wxColour(0xc8, 0x1e, 0x1e)));
         dc.DrawCircle(9, 9, 7);
      });
   }
   void set(bool state) { if (state != on) { on = state; Refresh(); } }
private:
   bool on = false;
};

// The two share one window: Fit Loop says what it fits, shows the method,
// and offers Ramachandran restraints in place of autofit.
void show_range_sequence(wxWindow *parent, bool fit_loop) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, fit_loop ? "No molecules to fit a loop in" : "No molecules to mutate");
      return;
   }
   wxDialog *d = new_dialog(parent, fit_loop ? "Fit Loop by Rama Search" : "Mutate Residue Range");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, fit_loop ? "Fit loop in Molecule:" : "Mutate Molecule:"), 0, wxBOTTOM, 4);
   MoleculeChooser *chooser = new MoleculeChooser(d, wxEmptyString, models);
   col->Add(chooser, 0, wxEXPAND | wxBOTTOM, 6);

   wxBoxSizer *chain_row = new wxBoxSizer(wxHORIZONTAL);
   wxChoice *chain = new wxChoice(d, wxID_ANY);
   fill_chains(chain, chooser->get_imol());
   chain_row->Add(new wxStaticText(d, wxID_ANY, "Chain ID:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   chain_row->Add(chain, 0);
   col->Add(chain_row, 0, wxBOTTOM, 6);

   wxBoxSizer *range_row = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *first = new wxTextCtrl(d, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1));
   wxTextCtrl *last = new wxTextCtrl(d, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1));
   range_row->Add(new wxStaticText(d, wxID_ANY, "Residue numbers"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   range_row->Add(first, 0);
   range_row->Add(new wxStaticText(d, wxID_ANY, "to"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);
   range_row->Add(last, 0);
   range_row->Add(new wxStaticText(d, wxID_ANY, "(inclusive)"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
   col->Add(range_row, 0, wxBOTTOM, 8);

   if (fit_loop) {
      wxStaticBoxSizer *method = new wxStaticBoxSizer(wxVERTICAL, d, "Using Method:");
      method->Add(new wxStaticText(method->GetStaticBox(), wxID_ANY,
                                   "Successive Peptides by Ramachandran\nsearch (this may not work well)",
                                   wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL),
                  0, wxALIGN_CENTER | wxALL, 4);
      col->Add(method, 0, wxEXPAND | wxBOTTOM, 8);
   }

   col->Add(new wxStaticText(d, wxID_ANY, "to (single letter) sequence:"), 0, wxBOTTOM, 4);
   wxTextCtrl *sequence = new wxTextCtrl(d, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 90),
                                         wxTE_MULTILINE);
   col->Add(sequence, 1, wxEXPAND | wxBOTTOM, 4);

   wxBoxSizer *match_row = new wxBoxSizer(wxHORIZONTAL);
   MatchLight *light = new MatchLight(d);
   wxStaticText *counts = new wxStaticText(d, wxID_ANY, "Counts: Residues - Sequence: -");
   match_row->Add(new wxStaticText(d, wxID_ANY, "Match:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   match_row->Add(light, 0, wxALIGN_CENTER_VERTICAL);
   match_row->AddStretchSpacer();
   match_row->Add(counts, 0, wxALIGN_CENTER_VERTICAL);
   col->Add(match_row, 0, wxEXPAND | wxBOTTOM, 8);

   wxCheckBox *option = new wxCheckBox(d, wxID_ANY, fit_loop ? "Use Ramachandran restraints to fit the loop?"
                                                              : "Autofit the mutated residues?");
   option->SetValue(fit_loop);
   col->Add(option, 0, wxBOTTOM, 10);
   col->Add(dialog_buttons(d, fit_loop ? "Fit Loop" : "Mutate"), 0, wxEXPAND);

   // the residue and sequence counts, and the light when they agree
   auto recount = [first, last, sequence, light, counts]() {
      long a, b;
      int residues = 0;
      if (first->GetValue().ToLong(&a) && last->GetValue().ToLong(&b) && b >= a) residues = int(b - a + 1);
      int letters = int(bandicoot_core_plain_sequence(sequence->GetValue().utf8_string()).size());
      wxString text = "Counts: Residues " + (residues > 0 ? wxString::Format("%d", residues) : wxString("-")) +
                      " Sequence: " + (letters > 0 ? wxString::Format("%d", letters) : wxString("-"));
      bool match = residues > 0 && residues == letters;
      if (match) text += " counts match";
      counts->SetLabel(text);
      light->set(match);
      counts->GetParent()->Layout();
   };
   for (wxTextCtrl *t : { first, last, sequence })
      t->Bind(wxEVT_TEXT, [recount](wxCommandEvent &) { recount(); });
   chooser->on_change = [chain](int imol) { fill_chains(chain, imol); };

   // autofit needs a refinement map: ask for one, as Coot did
   if (! fit_loop) {
      option->Bind(wxEVT_CHECKBOX, [d, option](wxCommandEvent &) {
         if (! option->IsChecked() || bandicoot_core_refinement_map() != -1) return;
         select_refinement_map(d);
         if (bandicoot_core_refinement_map() == -1) {
            option->SetValue(false);
            info(d, "A map has not yet been assigned for Refinement/Fitting");
         }
      });
   }

   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      bandicoot_range_sequence r;
      r.imol = chooser->get_imol();
      r.chain = chain_of(chain);
      if (! int_from(first, r.first) || ! int_from(last, r.last)) return;
      r.sequence = sequence->GetValue().utf8_string();
      std::string why_not, warning;
      if (fit_loop) {
         if (! bandicoot_core_fit_loop(r, option->IsChecked(), why_not, warning)) {
            error(d, "Could not fit loop in chain", wxString::FromUTF8(r.chain), "Fit Loop: " + why_not);
            return;
         }
         if (! warning.empty()) info(parent, wxString::FromUTF8(warning));
      } else if (! bandicoot_core_mutate_residue_range(r, option->IsChecked(), why_not)) {
         error(d, "Could not mutate chain", wxString::FromUTF8(r.chain), "Mutate Residue Range: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   d->SetMinSize(wxSize(460, -1));
   show(d, col);
}

// ------------------------------------------------------------ direct items and scripts

// Yes / No applies on OK, as in Coot.
void show_frames_per_second(wxWindow *parent) {
   wxDialog *d = new_dialog(parent, "Frames/Sec");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Show Frames Per Second?"), 0, wxALIGN_CENTER | wxBOTTOM, 8);
   RadioGroup *show_it = new RadioGroup(d, wxEmptyString, { "Yes", "No" }, bandicoot_core_fps_shown() ? 0 : 1);
   col->Add(show_it, 0, wxEXPAND | wxBOTTOM, 10);
   col->Add(dialog_buttons(d), 0, wxEXPAND);
   d->Bind(wxEVT_BUTTON, [d, show_it](wxCommandEvent &) {
      bandicoot_core_show_fps(show_it->get_selection() == 0);
      d->Destroy();
   }, wxID_OK);
   show(d, col);
}

void load_tutorial(wxWindow *parent) {
   std::string missing;
   if (! bandicoot_core_load_tutorial(missing))
      error(parent, "Could not load the tutorial data", "(not installed)",
            "Load Tutorial Model and Data: missing " + missing);
}

void run_script_file(wxWindow *parent) {
   FileRequest request;
   request.title = "Run Script";
   request.types = { { "Python scripts", { ".py" } } };
   std::vector<wxString> paths = open_files(parent, request);
   if (! paths.empty()) bandicoot_core_run_script(paths.front().utf8_string());
}

// The basis size, then the picks; the candidate loops follow in their own
// window (python/bandicoot_db_loop.py).
void show_fit_loop_by_database(wxWindow *parent) {
   if (bandicoot_core_goto_molecules().empty()) {
      info(parent, "No molecules to fit a loop in");
      return;
   }
   wxDialog *d = new_dialog(parent, "Fit Loop by Database Search");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Number of residues for basis"), 0, wxBOTTOM, 6);
   wxChoice *n = new wxChoice(d, wxID_ANY);
   for (int i = 2; i <= 9; i++) n->Append(wxString::Format("%d", i));
   n->SetSelection(2);   // 4
   col->Add(n, 0, wxEXPAND | wxBOTTOM, 10);
   col->Add(dialog_buttons(d, "Pick Atoms..."), 0, wxEXPAND);
   d->Bind(wxEVT_BUTTON, [d, n](wxCommandEvent &) {
      bandicoot_core_run_python("bandicoot_db_loop_pick(" + std::to_string(n->GetSelection() + 2) + ")");
      d->Destroy();
   }, wxID_OK);
   d->SetMinSize(wxSize(300, -1));
   show(d, col);
}

// The Calculate menu's items, by the GTK build's handler names.
const std::map<std::string, std::function<void(wxWindow *)>> &calculate_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "on_map_sharpening1_activate",          show_map_sharpening },
      { "on_skeleton1_activate",                show_map_skeleton },
      { "on_ncs_maps1_activate",                show_ncs_maps },
      { "on_calculate_updating_maps1_activate", show_updating_maps },
      { "on_move_molecule_here1_activate",      show_move_molecule_here },
      { "on_lsq_superpose1_activate",           show_lsq_superpose },
      { "on_ssm_superposition1_activate",       show_ssm_superpose },
      { "on_align_and_mutate1_activate",        show_align_and_mutate },
      { "on_mutate_molecule1_activate",         [](wxWindow *p) { show_range_sequence(p, false); } },
      { "on_fit_loop_by_rama_search1_activate", [](wxWindow *p) { show_range_sequence(p, true); } },
      { "on_fps1_activate",                     show_frames_per_second },
      { "on_calculate_load_tutorial_model_and_data1_activate", load_tutorial },
      { "on_run_script1_activate",              run_script_file },
      { "on_scripting_python1_activate",        show_python_console },
      { "on_pandda_inspect1_activate",          [](wxWindow *p) { show_pandda_inspect(p); } },
      { "on_fit_loop_by_database_search1_activate", show_fit_loop_by_database },
   };
   return actions;
}

} // namespace

bool is_calculate_action(const std::string &action) {
   return calculate_actions().count(action) > 0;
}

bool run_calculate_action(wxWindow *parent, const std::string &action) {
   auto it = calculate_actions().find(action);
   if (it == calculate_actions().end()) return false;
   it->second(parent);
   return true;
}

} // namespace wxui

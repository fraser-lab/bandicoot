// src/wxUI/main/refine-dialogs.cc

#include "wxUI/main/refine-dialogs.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace wxui {

namespace {

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   b->set_icon(icon(icon_file, wxSize(16, 16)));
   return b;
}

wxStaticText *bold_label(wxWindow *parent, const wxString &text) {
   wxStaticText *t = new wxStaticText(parent, wxID_ANY, text);
   t->SetFont(t->GetFont().Bold());
   return t;
}

std::string two_places(double v) {
   char b[32];
   snprintf(b, sizeof b, "%.2f", v);
   return b;
}

bool same_value(double a, double b) {
   return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::fabs(b));
}

// A drop-down of preset values with the one in use chosen; a value in use
// that is not a preset is added to the list in its place, so opening the
// window shows the settings as they are without changing any.
wxChoice *preset_choice(wxWindow *parent, const std::vector<const char *> &presets,
                        double in_use) {
   std::vector<std::string> items;
   int chosen = -1;
   for (const char *p : presets) {
      double v = atof(p);
      if (chosen < 0 && ! same_value(v, in_use) && v > in_use) {
         char b[32];
         snprintf(b, sizeof b, "%g", in_use);
         chosen = int(items.size());
         items.push_back(b);
      }
      if (same_value(v, in_use)) chosen = int(items.size());
      items.push_back(p);
   }
   if (chosen < 0) {
      char b[32];
      snprintf(b, sizeof b, "%g", in_use);
      chosen = int(items.size());
      items.push_back(b);
   }
   wxChoice *c = new wxChoice(parent, wxID_ANY);
   for (const std::string &s : items) c->Append(wxString::FromUTF8(s));
   c->SetSelection(chosen);
   return c;
}

// ------------------------------------------------------------ R/RC

class RefineParams : public wxFrame {
public:
   explicit RefineParams(wxWindow *parent);
private:
   wxTextCtrl *weight_entry;
   TitledPanel *more;
   wxPanel *panel;
   void fit() { panel->Layout(); panel->GetSizer()->Fit(panel); Fit(); }
};

RefineParams *refine_params = nullptr;

RefineParams::RefineParams(wxWindow *parent)
   : wxFrame(parent, wxID_ANY, "Refinement and Regularization Parameters",
             wxDefaultPosition, wxDefaultSize,
             (wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER)) | wxFRAME_FLOAT_ON_PARENT) {

   bandicoot_refine_params p = bandicoot_core_refine_params();

   panel = new wxPanel(this);
   panel->SetBackgroundColour(palette::face());
   TitledPanel *body = new TitledPanel(panel);
   wxWindow *b = body->content();
   wxBoxSizer *col = body->content_sizer();

   col->Add(bold_label(b, "For Regularization and Refinement:"), 0, wxBOTTOM, 6);
   struct Check { const char *label; bool value; void (*set)(bool); };
   const Check checks[] = {
      { "Use Torsion Restraints",        p.torsions,        bandicoot_core_set_torsion_restraints },
      { "Use Planar Peptide Restraints", p.planar_peptides, bandicoot_core_set_planar_peptide_restraints },
      { "Use Trans Peptide Restraints",  p.trans_peptides,  bandicoot_core_set_trans_peptide_restraints },
      { "Ramachandran Restraints",       p.rama,            bandicoot_core_set_rama_restraints }
   };
   for (const Check &c : checks) {
      wxCheckBox *box = new wxCheckBox(b, wxID_ANY, c.label);
      box->SetValue(c.value);
      void (*set)(bool) = c.set;
      box->Bind(wxEVT_CHECKBOX, [set](wxCommandEvent &e) { set(e.IsChecked()); });
      col->Add(box, 0, wxBOTTOM, 4);
   }

   TitledPanel *mainchain = new TitledPanel(b, "Mainchain Restraints");
   const char *ss[] = { "No Secondary Structure Restraints", "Alpha Helix Restraints",
                        "Beta Strand Restraints" };
   for (int i = 0; i < 3; i++) {
      wxRadioButton *r = new wxRadioButton(mainchain->content(), wxID_ANY, ss[i],
                                           wxDefaultPosition, wxDefaultSize,
                                           i == 0 ? wxRB_GROUP : 0);
      r->SetValue(p.secondary_structure == i);
      r->Bind(wxEVT_RADIOBUTTON, [i](wxCommandEvent &) {
         bandicoot_core_set_secondary_structure_restraints(i);
      });
      mainchain->content_sizer()->Add(r, 0, wxLEFT | wxBOTTOM, 4);
   }
   col->Add(mainchain, 0, wxEXPAND | wxTOP | wxBOTTOM, 6);

   TitledPanel *weights = new TitledPanel(b, "Weight Matrix");
   wxBoxSizer *wrow = new wxBoxSizer(wxHORIZONTAL);
   wrow->Add(new wxStaticText(weights->content(), wxID_ANY, "Refinement Weight"), 0,
             wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 8);
   weight_entry = new wxTextCtrl(weights->content(), wxID_ANY, two_places(p.weight),
                                 wxDefaultPosition, wxSize(80, -1));
   // each edit that reads as a number is the weight, as in Coot
   weight_entry->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
      double v;
      if (weight_entry->GetValue().ToCDouble(&v)) bandicoot_core_set_refinement_weight(float(v));
   });
   wrow->Add(weight_entry, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   BevelButton *estimate = new BevelButton(weights->content(), wxID_ANY, "Estimate");
   estimate->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      float w;
      if (bandicoot_core_estimate_refinement_weight(w))
         weight_entry->ChangeValue(two_places(w));
   });
   wrow->Add(estimate, 0, wxALIGN_CENTER_VERTICAL);
   weights->content_sizer()->Add(wrow);
   col->Add(weights, 0, wxEXPAND | wxBOTTOM, 6);

   BevelButton *more_button = new BevelButton(b, wxID_ANY, "More Control...");
   more_button->set_toggle(true);
   more_button->set_on(p.more_control_shown);
   col->Add(more_button, 0, wxBOTTOM, 6);

   more = new TitledPanel(b);
   wxFlexGridSizer *grid = new wxFlexGridSizer(2, 6, 12);
   grid->AddGrowableCol(0);
   wxWindow *m = more->content();
   // the overall weight's list is built around the weight now in use
   std::vector<std::string> overall_items;
   for (double f : { 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 4.0, 10.0, 20.0 })
      overall_items.push_back(two_places(p.weight * f));
   std::vector<const char *> overall;
   for (const std::string &s : overall_items) overall.push_back(s.c_str());
   struct Control { const char *label; std::vector<const char *> presets; double in_use; };
   const Control controls[] = {
      { "Overall weight",         overall, atof(two_places(p.weight).c_str()) },
      { "Lennard-Jones epsilon",  { "0.1", "0.2", "0.5", "1.0", "2.0", "5.0", "10.0" }, p.lj_epsilon },
      { "Rama Restraints weight", { "0.1", "0.2", "0.5", "1.0", "2.0", "5.0", "10.0", "20.0", "50.0", "100.0" },
                                  p.rama_weight },
      { "Geman-McClure alpha",    { "0.0", "0.001", "0.003", "0.01", "0.03", "0.1", "0.3", "1.0", "3.0", "10.0" },
                                  p.gm_alpha },
      { "Torsions weight",        { "0.1", "0.3", "1.0", "3.0", "10.0", "30.0" }, p.torsion_weight }
   };
   for (const Control &c : controls) {
      grid->Add(new wxStaticText(m, wxID_ANY, c.label), 0, wxALIGN_CENTER_VERTICAL);
      wxChoice *choice = preset_choice(m, c.presets, c.in_use);
      std::string which = c.label;
      choice->Bind(wxEVT_CHOICE, [this, which](wxCommandEvent &e) {
         std::string value = e.GetString().utf8_string();
         bandicoot_core_set_refine_control(which, value);
         // the overall weight is the Refinement Weight above, so it shows there
         if (which == "Overall weight") weight_entry->ChangeValue(wxString::FromUTF8(value));
      });
      grid->Add(choice, 0, wxEXPAND);
   }
   more->content_sizer()->Add(grid, 0, wxEXPAND | wxALL, 4);
   more->Show(p.more_control_shown);
   col->Add(more, 0, wxEXPAND);
   more_button->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
      bool shown = e.GetInt() != 0;
      bandicoot_core_set_more_control_shown(shown);
      more->Show(shown);
      fit();
   });

   BevelButton *ok = icon_button(panel, "OK", "stock-button-ok.png");
   ok->SetMinSize(wxSize(110, 28));
   ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(body, 1, wxEXPAND | wxALL, 6);
   outer->Add(ok, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   panel->SetSizer(outer);

   // OK and the close box both apply the weight as typed, as Coot's OK did
   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
      double v;
      if (weight_entry->GetValue().ToCDouble(&v)) bandicoot_core_set_refinement_weight(float(v));
      refine_params = nullptr;
      Destroy();
   });
   fit();
}

// ------------------------------------------------------------ Map

void run_select_map(wxWindow *parent) {

   wxDialog d(parent, wxID_ANY, "Select Map for Fitting");
   d.SetBackgroundColour(palette::face());

   // Coot's choice when none is set: the first map, so OK keeps it
   int before = bandicoot_core_refinement_map();
   std::vector<std::pair<int, std::string>> maps = bandicoot_core_maps_for_fitting();
   if (before < 0 && ! maps.empty()) bandicoot_core_set_refinement_map(maps.front().first);
   int now = bandicoot_core_refinement_map();

   TitledPanel *body = new TitledPanel(&d);
   wxWindow *b = body->content();
   body->content_sizer()->Add(new wxStaticText(b, wxID_ANY, "Choose a Map:"), 0,
                              wxALIGN_CENTER | wxBOTTOM, 4);
   wxChoice *choice = new wxChoice(b, wxID_ANY);
   std::vector<int> imols;
   for (const auto &mp : maps) {
      imols.push_back(mp.first);
      choice->Append(wxString::FromUTF8(mp.second));
      if (mp.first == now) choice->SetSelection(int(imols.size()) - 1);
   }
   // the choice is the refinement map at once, as in Coot
   choice->Bind(wxEVT_CHOICE, [&imols](wxCommandEvent &e) {
      int i = e.GetSelection();
      if (i >= 0 && i < int(imols.size())) bandicoot_core_set_refinement_map(imols[i]);
   });
   body->content_sizer()->Add(choice, 0, wxEXPAND | wxBOTTOM, 6);

   wxBoxSizer *wrow = new wxBoxSizer(wxHORIZONTAL);
   wrow->Add(new wxStaticText(b, wxID_ANY, "Weight:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   wxTextCtrl *weight = new wxTextCtrl(b, wxID_ANY,
                                       std::to_string(bandicoot_core_refine_params().weight),
                                       wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
   wrow->Add(weight, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   BevelButton *estimate = new BevelButton(b, wxID_ANY, "Estimate");
   estimate->Bind(wxEVT_BUTTON, [weight](wxCommandEvent &) {
      float w;
      if (bandicoot_core_estimate_refinement_weight(w)) weight->ChangeValue(two_places(w));
   });
   wrow->Add(estimate, 0, wxALIGN_CENTER_VERTICAL);
   body->content_sizer()->Add(wrow, 0, wxEXPAND);

   BevelButton *ok = icon_button(&d, "OK", "stock-button-ok.png");
   ok->SetMinSize(wxSize(110, 28));
   ok->Bind(wxEVT_BUTTON, [&d](wxCommandEvent &) { d.EndModal(wxID_OK); });
   weight->Bind(wxEVT_TEXT_ENTER, [&d](wxCommandEvent &) { d.EndModal(wxID_OK); });

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(body, 1, wxEXPAND | wxALL, 6);
   outer->Add(ok, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d.SetSizerAndFit(outer);
   d.SetSize(wxSize(std::max(d.GetSize().x, 440), d.GetSize().y));
   d.CentreOnParent();

   if (d.ShowModal() == wxID_OK) {
      double v;
      if (weight->GetValue().ToCDouble(&v)) bandicoot_core_set_refinement_weight(float(v));
   } else if (before < 0) {
      // closed without choosing: no map was set before, so none is now
      bandicoot_core_set_refinement_map(-1);
   }
}

// ------------------------------------------------------------ Run Refmac

void run_refmac_dialog(wxWindow *parent) {

   wxDialog d(parent, wxID_ANY, "Run Refmac");
   d.SetBackgroundColour(palette::face());

   TitledPanel *model_frame = new TitledPanel(&d, "Model Molecule");
   wxChoice *models = new wxChoice(model_frame->content(), wxID_ANY);
   std::vector<int> imols;
   for (const auto &m : bandicoot_core_refmac_models()) {
      imols.push_back(m.first);
      models->Append(wxString::FromUTF8(m.second));
   }
   if (! imols.empty()) models->SetSelection(0);
   model_frame->content_sizer()->Add(models, 0, wxEXPAND);

   TitledPanel *mtz_frame = new TitledPanel(&d, "MTZ File");
   wxWindow *mf = mtz_frame->content();
   wxBoxSizer *mrow = new wxBoxSizer(wxHORIZONTAL);
   wxChoice *mtz = new wxChoice(mf, wxID_ANY);
   std::string last = bandicoot_core_refmac_mtz();
   if (! last.empty()) {
      mtz->Append(wxString::FromUTF8(last));
      mtz->SetSelection(0);
   }
   mrow->Add(mtz, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   BevelButton *file = icon_button(mf, "File...", "stock-menu-open.png");
   mrow->Add(file, 0, wxALIGN_CENTER_VERTICAL);
   mtz_frame->content_sizer()->Add(mrow, 0, wxEXPAND);

   BevelButton *cancel = icon_button(&d, "Cancel", "stock-button-cancel.png");
   BevelButton *ok = icon_button(&d, "OK", "stock-button-ok.png");
   for (BevelButton *b : { cancel, ok }) b->SetMinSize(wxSize(110, 28));
   auto update_ok = [&]() { ok->Enable(models->GetSelection() >= 0 && mtz->GetSelection() >= 0); };
   update_ok();

   file->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
      FileRequest r;
      r.title = "Select MTZ File";
      r.types = { { "MTZ", { ".mtz" } } };
      r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
      std::vector<wxString> paths = open_files(&d, r);
      if (paths.empty()) return;
      bandicoot_core_save_chooser_directory(r.directory.utf8_string());
      mtz->Clear();
      mtz->Append(paths.front());
      mtz->SetSelection(0);
      update_ok();
   });
   cancel->Bind(wxEVT_BUTTON, [&d](wxCommandEvent &) { d.EndModal(wxID_CANCEL); });
   ok->Bind(wxEVT_BUTTON, [&d](wxCommandEvent &) { d.EndModal(wxID_OK); });

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   buttons->Add(cancel, 0, wxRIGHT, 12);
   buttons->Add(ok);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(model_frame, 0, wxEXPAND | wxALL, 6);
   outer->Add(mtz_frame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
   outer->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d.SetSizerAndFit(outer);
   d.SetSize(wxSize(std::max(d.GetSize().x, 480), d.GetSize().y));
   d.CentreOnParent();

   if (d.ShowModal() != wxID_OK) return;
   int i = models->GetSelection();
   if (i < 0 || i >= int(imols.size()) || mtz->GetSelection() < 0) return;
   bandicoot_core_run_refmac(imols[i], mtz->GetString(mtz->GetSelection()).utf8_string());
}

// ------------------------------------------------------------ Fixed Atoms

class FixedAtoms : public wxFrame {
public:
   explicit FixedAtoms(wxWindow *parent);
   void release();
private:
   BevelButton *fix, *unfix;
   void pick(BevelButton *pressed, BevelButton *other, int mode, bool on);
};

FixedAtoms *fixed_atoms = nullptr;

FixedAtoms::FixedAtoms(wxWindow *parent)
   : wxFrame(parent, wxID_ANY, "Fixed Atoms for Refinement", wxDefaultPosition, wxDefaultSize,
             (wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER)) | wxFRAME_FLOAT_ON_PARENT) {

   wxPanel *panel = new wxPanel(this);
   panel->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   fix   = new BevelButton(panel, wxID_ANY, "Fix Atom");
   unfix = new BevelButton(panel, wxID_ANY, "Unfix Atom");
   for (BevelButton *t : { fix, unfix }) t->set_toggle(true);
   fix->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) { pick(fix, unfix, 1, e.GetInt() != 0); });
   unfix->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) { pick(unfix, fix, 2, e.GetInt() != 0); });
   BevelButton *clear = new BevelButton(panel, wxID_ANY, "Clear Fixed Atoms");
   clear->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_clear_fixed_atoms(); });
   col->Add(fix,   0, wxEXPAND | wxBOTTOM, 4);
   col->Add(unfix, 0, wxEXPAND | wxBOTTOM, 4);
   col->Add(clear, 0, wxEXPAND | wxBOTTOM, 12);

   BevelButton *close = icon_button(panel, "Close", "stock-menu-close.png");
   close->SetMinSize(wxSize(110, 28));
   close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
   col->Add(close, 0, wxALIGN_RIGHT);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 8);
   panel->SetSizer(outer);
   outer->SetSizeHints(this);
   SetSize(wxSize(std::max(GetSize().x, 260), GetSize().y));

   // closing stops a pick that is waiting
   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
      if (fix->is_on() || unfix->is_on()) bandicoot_core_fixed_atom_pick(0);
      fix->set_on(false);
      unfix->set_on(false);
      Hide();
   });
}

// Fix and Unfix wait for one pick each; pressing one lets go of the other.
void FixedAtoms::pick(BevelButton *, BevelButton *other, int mode, bool on) {
   if (on) {
      other->set_on(false);
      bandicoot_core_fixed_atom_pick(mode);
   } else {
      bandicoot_core_fixed_atom_pick(0);
   }
}

void FixedAtoms::release() {
   fix->set_on(false);
   unfix->set_on(false);
}

} // namespace

void show_refine_params(wxWindow *parent) {
   if (refine_params) {
      refine_params->Raise();
      return;
   }
   refine_params = new RefineParams(parent);
   refine_params->CentreOnParent();
   refine_params->Show();
}

void select_refinement_map(wxWindow *parent) {
   run_select_map(parent);
}

void run_refmac(wxWindow *parent) {
   run_refmac_dialog(parent);
}

void show_fixed_atoms(wxWindow *parent) {
   if (! fixed_atoms) {
      fixed_atoms = new FixedAtoms(parent);
      fixed_atoms->CentreOnParent();
   }
   fixed_atoms->Show();
   fixed_atoms->Raise();
}

void release_fixed_atom_toggles() {
   if (fixed_atoms) fixed_atoms->release();
}

} // namespace wxui

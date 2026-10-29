// src/wxUI/main/draw-menu.cc

#include "wxUI/main/draw-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/colour-panel.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/entry-row.hh"
#include "wxUI/widgets/molecule-chooser.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxDialog *new_dialog(wxWindow *parent, const wxString &title) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   return d;
}

void show(wxDialog *d, wxBoxSizer *col, int min_width = -1) {
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);
   if (min_width > 0) {
      d->SetMinSize(wxSize(min_width, -1));
      d->Fit();
   }
   d->CentreOnParent();
   d->Show();
}

// A window of per-molecule panels that scrolls when there are many.
struct ScrollingWindow {
   wxFrame *frame;
   wxScrolledWindow *scroller;
   wxBoxSizer *rows;
};

ScrollingWindow scrolling_window(wxWindow *parent, const wxString &title, const wxSize &size) {
   ScrollingWindow w;
   w.frame = new wxFrame(parent, wxID_ANY, title, wxDefaultPosition, size,
                         wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT);
   wxPanel *panel = new wxPanel(w.frame);
   panel->SetBackgroundColour(palette::face());
   w.scroller = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                     wxVSCROLL | wxBORDER_SUNKEN);
   w.scroller->SetScrollRate(0, 10);
   w.rows = new wxBoxSizer(wxVERTICAL);
   w.scroller->SetSizer(w.rows);
   wxButton *ok = new wxButton(panel, wxID_OK, "OK");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(w.scroller, 1, wxEXPAND | wxALL, 8);
   col->Add(ok, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 8);
   panel->SetSizer(col);
   wxFrame *f = w.frame;
   ok->Bind(wxEVT_BUTTON, [f](wxCommandEvent &) { f->Close(); });
   return w;
}

// ---------------------------------------------------- Anisotropic Atoms

// Apply sets the choices; the probability applies as the slider moves, as in Coot.
void anisotropic_atoms(wxWindow *parent) {
   bandicoot_aniso a = bandicoot_core_aniso();
   wxDialog *d = new_dialog(parent, "Anisotropic Atoms");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   RadioGroup *show_aniso = new RadioGroup(d, "Show Anisotropic Atoms?", { "Yes", "No" }, a.show ? 0 : 1);
   col->Add(show_aniso, 0, wxEXPAND | wxBOTTOM, 10);

   TitledPanel *limit_panel = new TitledPanel(d, "Limit Display Radius of Anisotropic Atoms?");
   wxPanel *lp = limit_panel->content();
   RadioGroup *limit = new RadioGroup(lp, wxEmptyString, { "Yes", "No" }, a.limit ? 0 : 1);
   wxBoxSizer *radius_row = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *radius = new wxTextCtrl(lp, wxID_ANY, wxString::Format("%.1f", a.radius),
                                       wxDefaultPosition, wxSize(160, -1));
   radius_row->Add(new wxStaticText(lp, wxID_ANY, "Display Limit Radius:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
   radius_row->Add(radius, 1, wxALIGN_CENTER_VERTICAL);
   radius_row->Add(new wxStaticText(lp, wxID_ANY, wxString::FromUTF8("\xc3\x85")), 0,
                   wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
   limit_panel->content_sizer()->Add(limit, 0, wxEXPAND | wxBOTTOM, 6);
   limit_panel->content_sizer()->Add(radius_row, 0, wxEXPAND | wxALL, 4);
   col->Add(limit_panel, 0, wxEXPAND | wxBOTTOM, 10);
   radius->Enable(a.limit);
   limit->on_change = [radius](int i) { radius->Enable(i == 0); };

   TitledSlider *probability = new TitledSlider(d, "Probability (%)", 0.0, 100.0, 0.1, a.probability, 1);
   probability->on_change = [](double v) { bandicoot_core_set_aniso_probability(float(v)); };
   col->Add(probability, 0, wxEXPAND | wxBOTTOM, 10);

   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxAPPLY | wxCLOSE);
   col->Add(buttons, 0, wxEXPAND);

   d->Bind(wxEVT_BUTTON, [show_aniso, limit, radius](wxCommandEvent &) {
      bandicoot_aniso now = bandicoot_core_aniso();
      now.show = show_aniso->get_selection() == 0;
      now.limit = limit->get_selection() == 0;
      if (now.limit) {
         double r;
         if (! radius->GetValue().Trim().Trim(false).ToCDouble(&r) || r < 0) { wxBell(); return; }
         now.radius = float(r);
      }
      bandicoot_core_set_aniso(now);
   }, wxID_APPLY);
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CLOSE);
   show(d, col, 360);
}

// ------------------------------------------- Additional Representation

void additional_representation(wxWindow *parent) {
   // the chooser puts the molecule number before each name itself
   MoleculeChooser::molecule_list models;
   for (const bandicoot_dm_molecule &m : bandicoot_core_dm_models()) models.push_back({ m.imol, m.name });
   if (models.empty()) {
      info(parent, "No models to add a representation to");
      return;
   }
   wxDialog *d = new_dialog(parent, "Additional Representation");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   MoleculeChooser *molecule = new MoleculeChooser(d, "Molecule:", models);
   molecule->set_imol(bandicoot_core_add_rep_molecule());
   col->Add(molecule, 0, wxEXPAND | wxBOTTOM, 10);

   // each choice in a box of its own, its entries indented beneath it
   wxStaticBoxSizer *selection = new wxStaticBoxSizer(wxVERTICAL, d, "Representation Selection");
   wxWindow *sb = selection->GetStaticBox();
   wxRadioButton *by_position = new wxRadioButton(sb, wxID_ANY, "By Position (this residue)",
                                                  wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
   wxRadioButton *by_molecule = new wxRadioButton(sb, wxID_ANY, "Entire Molecule");
   wxRadioButton *by_residues = new wxRadioButton(sb, wxID_ANY, "By Residue Number Specification");
   wxRadioButton *by_string = new wxRadioButton(sb, wxID_ANY, "By Selection String");
   auto entry = [sb](int width) {
      return new wxTextCtrl(sb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(width, -1));
   };
   wxTextCtrl *chain = entry(50), *from = entry(70), *to = entry(70), *ins = entry(50);
   wxTextCtrl *atoms = entry(240);
   wxCheckBox *whole_chain = new wxCheckBox(sb, wxID_ANY, "Entire Chain");
   wxFlexGridSizer *residues = new wxFlexGridSizer(4, 6, 8);
   auto label = [sb](const wxString &t) { return new wxStaticText(sb, wxID_ANY, t); };
   residues->Add(label("Chain ID:"), 0, wxALIGN_CENTER_VERTICAL);
   wxBoxSizer *chain_row = new wxBoxSizer(wxHORIZONTAL);
   chain_row->Add(chain, 0, wxALIGN_CENTER_VERTICAL);
   chain_row->Add(whole_chain, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
   residues->Add(chain_row);
   residues->AddSpacer(0);
   residues->AddSpacer(0);
   residues->Add(label("From Residue Number:"), 0, wxALIGN_CENTER_VERTICAL);
   residues->Add(from);
   residues->Add(label("To Residue Number:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
   residues->Add(to);
   residues->Add(label("With Insertion Code:"), 0, wxALIGN_CENTER_VERTICAL);
   residues->Add(ins);
   wxBoxSizer *atoms_row = new wxBoxSizer(wxHORIZONTAL);
   atoms_row->Add(label("Atom Selection:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   atoms_row->Add(atoms, 1);
   selection->Add(by_position, 0, wxALL, 4);
   selection->Add(by_molecule, 0, wxALL, 4);
   selection->Add(by_residues, 0, wxLEFT | wxRIGHT | wxTOP, 4);
   selection->Add(residues, 0, wxLEFT | wxBOTTOM, 28);
   selection->Add(by_string, 0, wxLEFT | wxRIGHT | wxTOP, 4);
   selection->Add(atoms_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
   selection->GetItem(atoms_row)->SetBorder(28);
   selection->GetItem(atoms_row)->SetFlag(wxEXPAND | wxLEFT | wxBOTTOM);
   col->Add(selection, 0, wxEXPAND | wxBOTTOM, 10);

   wxStaticBoxSizer *style = new wxStaticBoxSizer(wxVERTICAL, d, "Representation Style");
   wxWindow *stb = style->GetStaticBox();
   wxRadioButton *fat = new wxRadioButton(stb, wxID_ANY, "Fat Bonds", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
   wxRadioButton *balls = new wxRadioButton(stb, wxID_ANY, "Ball & Stick");
   wxChoice *width = new wxChoice(stb, wxID_ANY);
   for (int w = 1; w <= 20; w++) width->Append(wxString::Format("%d", w));
   width->SetSelection(7);   // 8, Coot's
   wxBoxSizer *width_row = new wxBoxSizer(wxHORIZONTAL);
   width_row->Add(new wxStaticText(stb, wxID_ANY, "Bond Width:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   width_row->Add(width, 0, wxALIGN_CENTER_VERTICAL);
   style->Add(fat, 0, wxALL, 4);
   style->Add(width_row, 0, wxLEFT | wxBOTTOM, 28);
   style->Add(balls, 0, wxALL, 4);
   col->Add(style, 0, wxEXPAND | wxBOTTOM, 10);

   col->Add(new wxStaticText(d, wxID_ANY, "Additional representations are shown and hidden "
                                          "in the Display Manager."), 0, wxBOTTOM, 10);
   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
   buttons->GetAffirmativeButton()->SetLabel("Add Representation");
   col->Add(buttons, 0, wxEXPAND);

   // Entire Chain fills in the chain's residue range and holds it there
   auto fill_chain = [=]() {
      if (! whole_chain->GetValue()) return;
      int first, last;
      if (bandicoot_core_chain_residue_range(molecule->get_imol(), chain->GetValue().Trim().Trim(false).utf8_string(),
                                             first, last)) {
         from->ChangeValue(wxString::Format("%d", first));
         to->ChangeValue(wxString::Format("%d", last));
      } else {
         from->ChangeValue(wxEmptyString);
         to->ChangeValue(wxEmptyString);
      }
   };
   // the entries of a choice not taken are greyed
   auto sync = [=]() {
      bool residues_on = by_residues->GetValue();
      for (wxTextCtrl *t : { chain, ins }) t->Enable(residues_on);
      whole_chain->Enable(residues_on);
      for (wxTextCtrl *t : { from, to }) t->Enable(residues_on && ! whole_chain->GetValue());
      atoms->Enable(by_string->GetValue());
      width->Enable(fat->GetValue());
      molecule->Enable(! by_position->GetValue());
   };
   for (wxRadioButton *r : { by_position, by_molecule, by_residues, by_string, fat, balls })
      r->Bind(wxEVT_RADIOBUTTON, [sync](wxCommandEvent &) { sync(); });
   whole_chain->Bind(wxEVT_CHECKBOX, [fill_chain, sync](wxCommandEvent &) { fill_chain(); sync(); });
   chain->Bind(wxEVT_TEXT, [fill_chain](wxCommandEvent &) { fill_chain(); });
   molecule->on_change = [fill_chain](int) { fill_chain(); };
   sync();

   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      bandicoot_add_rep_request r;
      r.imol = molecule->get_imol();
      r.by = by_position->GetValue() ? 0 : by_residues->GetValue() ? 1 : by_string->GetValue() ? 2 : 3;
      if (r.by == 1) {
         long a, b;
         if (! from->GetValue().Trim().Trim(false).ToLong(&a) ||
             ! to->GetValue().Trim().Trim(false).ToLong(&b)) {
            wxBell();
            return;
         }
         r.chain_id = chain->GetValue().Trim().Trim(false).utf8_string();
         r.ins_code = ins->GetValue().Trim().Trim(false).utf8_string();
         r.resno_start = int(a);
         r.resno_end = int(b);
      }
      r.selection = atoms->GetValue().Trim().Trim(false).utf8_string();
      r.ball_and_stick = balls->GetValue();
      r.bond_width = float(width->GetSelection() + 1);
      std::string why_not;
      if (! bandicoot_core_add_representation(r, why_not)) {
         error(d, "Could not add the representation", wxEmptyString,
               "Additional Representation: " + why_not);
         return;
      }
      d->Destroy();
   }, wxID_OK);
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
   show(d, col);
}

// ------------------------------------------------- Symmetry by Molecule

wxFrame *symmetry_by_molecule_window = nullptr;

void symmetry_by_molecule(wxWindow *parent) {
   if (symmetry_by_molecule_window) { symmetry_by_molecule_window->Raise(); return; }
   std::vector<bandicoot_molecule_symmetry> mols = bandicoot_core_molecule_symmetries();
   if (mols.empty()) {
      info(parent, "No models to show symmetry for");
      return;
   }
   ScrollingWindow w = scrolling_window(parent, "Symmetry by Molecule", wxSize(460, 520));
   symmetry_by_molecule_window = w.frame;
   w.frame->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent &e) {
      symmetry_by_molecule_window = nullptr;
      e.Skip();
   });
   for (const bandicoot_molecule_symmetry &m : mols) {
      wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, w.scroller, wxString::FromUTF8(m.label));
      wxWindow *b = box->GetStaticBox();
      wxCheckBox *shown = new wxCheckBox(b, wxID_ANY, "Show Symmetry?");
      shown->SetValue(m.shown);
      box->Add(shown, 0, wxALL, 4);
      wxStaticBoxSizer *options = new wxStaticBoxSizer(wxHORIZONTAL, b, "Display Options");
      wxWindow *ob = options->GetStaticBox();
      const char *displays[] = { "Display Sphere", "Display Near Chains", "Display as CAs" };
      const char *colourings[] = { "Standard Colouring", "Colour by Symop", "Colour by Molecule" };
      wxBoxSizer *left = new wxBoxSizer(wxVERTICAL), *right = new wxBoxSizer(wxVERTICAL);
      int imol = m.imol;
      for (int i = 0; i < 3; i++) {
         wxRadioButton *r = new wxRadioButton(ob, wxID_ANY, displays[i], wxDefaultPosition, wxDefaultSize,
                                              i == 0 ? wxRB_GROUP : 0);
         r->SetValue(m.display == i);
         r->Bind(wxEVT_RADIOBUTTON, [imol, i](wxCommandEvent &) { bandicoot_core_set_molecule_symmetry_display(imol, i); });
         left->Add(r, 0, wxALL, 3);
      }
      for (int i = 0; i < 3; i++) {
         wxRadioButton *r = new wxRadioButton(ob, wxID_ANY, colourings[i], wxDefaultPosition, wxDefaultSize,
                                              i == 0 ? wxRB_GROUP : 0);
         r->SetValue(m.colouring == i);
         r->Bind(wxEVT_RADIOBUTTON, [imol, i](wxCommandEvent &) { bandicoot_core_set_molecule_symmetry_colouring(imol, i); });
         right->Add(r, 0, wxALL, 3);
      }
      options->Add(left, 0, wxRIGHT, 12);
      options->Add(right, 0);
      box->Add(options, 0, wxEXPAND | wxALL, 4);
      shown->Bind(wxEVT_CHECKBOX, [imol](wxCommandEvent &e) {
         bandicoot_core_set_molecule_symmetry_shown(imol, e.IsChecked());
      });
      w.rows->Add(box, 0, wxEXPAND | wxALL, 6);
   }
   w.scroller->FitInside();
   w.frame->CentreOnParent();
   w.frame->Show();
}

// ------------------------------------------------------ Cell & Symmetry

wxDialog *cell_and_symmetry_window = nullptr;

wxBitmap swatch(const bandicoot_rgb &c) {
   wxBitmap bm(36, 16);
   wxMemoryDC dc(bm);
   dc.SetBackground(wxBrush(wxColour(int(std::lround(c.r * 255)), int(std::lround(c.g * 255)),
                                     int(std::lround(c.b * 255)))));
   dc.Clear();
   dc.SetPen(*wxBLACK_PEN);
   dc.SetBrush(*wxTRANSPARENT_BRUSH);
   dc.DrawRectangle(0, 0, 36, 16);
   dc.SelectObject(wxNullBitmap);
   return bm;
}

// Every choice applies as it is made; the radius applies on Return and on OK.
void cell_and_symmetry(wxWindow *parent) {
   if (cell_and_symmetry_window) { cell_and_symmetry_window->Raise(); return; }
   bandicoot_symmetry s = bandicoot_core_symmetry();
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Cell & Symmetry");
   d->SetBackgroundColour(palette::face());
   cell_and_symmetry_window = d;
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   wxStaticBoxSizer *symm = new wxStaticBoxSizer(wxVERTICAL, d, "Symmetry");
   wxWindow *sb = symm->GetStaticBox();

   wxStaticBoxSizer *master = new wxStaticBoxSizer(wxVERTICAL, sb, "Master Switch");
   wxWindow *mb = master->GetStaticBox();
   wxRadioButton *off = new wxRadioButton(mb, wxID_ANY, "Symmetry Off", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
   wxRadioButton *on = new wxRadioButton(mb, wxID_ANY, "Symmetry On");
   wxCheckBox *always = new wxCheckBox(mb, wxID_ANY, "Always On");
   (s.shown ? on : off)->SetValue(true);
   master->Add(off, 0, wxALL, 3);
   master->Add(on, 0, wxALL, 3);
   master->Add(always, 0, wxALL, 3);
   symm->Add(master, 0, wxEXPAND | wxALL, 4);

   wxButton *by_molecule = new wxButton(sb, wxID_ANY, "Symmetry by Molecule...");
   symm->Add(by_molecule, 0, wxEXPAND | wxALL, 4);

   wxBoxSizer *radius_row = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *radius = new wxTextCtrl(sb, wxID_ANY, wxString::Format("%.1f", s.radius), wxDefaultPosition,
                                       wxSize(120, -1), wxTE_PROCESS_ENTER);
   radius_row->Add(new wxStaticText(sb, wxID_ANY, "Symmetry Atom Display Radius:"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   radius_row->Add(radius, 0, wxALIGN_CENTER_VERTICAL);
   radius_row->Add(new wxStaticText(sb, wxID_ANY, wxString::FromUTF8("\xc3\x85")), 0,
                   wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
   symm->Add(radius_row, 0, wxALL, 4);

   auto colour = std::make_shared<bandicoot_rgb>(s.colour);
   wxBitmapButton *pick = new wxBitmapButton(sb, wxID_ANY, swatch(s.colour));
   wxBoxSizer *colour_row = new wxBoxSizer(wxHORIZONTAL);
   colour_row->Add(new wxStaticText(sb, wxID_ANY, "Symmetry Colour:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   colour_row->Add(pick, 0, wxALIGN_CENTER_VERTICAL);
   symm->Add(colour_row, 0, wxALL, 4);
   wxBoxSizer *merge_row = new wxBoxSizer(wxHORIZONTAL);
   StepSlider *merge = new StepSlider(sb, 0.0, 1.0, 0.02, s.colour_merge, 2);
   merge->on_change = [](double v) { bandicoot_core_set_symmetry_colour_merge(float(v)); };
   merge_row->Add(new wxStaticText(sb, wxID_ANY, "Colour Merge:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   merge_row->Add(merge, 1, wxALIGN_CENTER_VERTICAL);
   symm->Add(merge_row, 0, wxEXPAND | wxALL, 4);

   wxCheckBox *expanded = new wxCheckBox(sb, wxID_ANY, "Expanded Symmetry Atom Labels?");
   expanded->SetValue(s.expanded_labels);
   symm->Add(expanded, 0, wxALL, 4);
   col->Add(symm, 0, wxEXPAND | wxBOTTOM, 10);

   RadioGroup *cells = new RadioGroup(d, "Show Unit Cells?", { "Yes", "No" }, s.unit_cells ? 0 : 1);
   col->Add(cells, 0, wxEXPAND | wxBOTTOM, 10);

   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK);
   col->Add(buttons, 0, wxEXPAND);

   off->Bind(wxEVT_RADIOBUTTON, [](wxCommandEvent &) { bandicoot_core_set_symmetry_shown(false); });
   on->Bind(wxEVT_RADIOBUTTON, [](wxCommandEvent &) { bandicoot_core_set_symmetry_shown(true); });
   // as in Coot, ticking it saves symmetry on; unticking leaves the saved setting
   always->Bind(wxEVT_CHECKBOX, [on](wxCommandEvent &e) {
      if (! e.IsChecked()) return;
      bandicoot_core_set_symmetry_always_on();
      on->SetValue(true);
   });
   by_molecule->Bind(wxEVT_BUTTON, [parent](wxCommandEvent &) { symmetry_by_molecule(parent); });
   auto apply_radius = [radius]() {
      double r;
      if (! radius->GetValue().Trim().Trim(false).ToCDouble(&r) || ! bandicoot_core_set_symmetry_radius(float(r))) {
         wxBell();
         return false;
      }
      return true;
   };
   radius->Bind(wxEVT_TEXT_ENTER, [apply_radius](wxCommandEvent &) { apply_radius(); });
   pick->Bind(wxEVT_BUTTON, [pick, colour](wxCommandEvent &) {
      bandicoot_rgb before = *colour;
      PanelColour start;
      start.r = colour->r; start.g = colour->g; start.b = colour->b;
      // the panel can outlast the window, so the swatch is only touched while it is up
      show_colour_panel("Symmetry Colour", start,
                        [colour](const PanelColour &p) {
                           colour->r = p.r; colour->g = p.g; colour->b = p.b;
                           bandicoot_core_set_symmetry_colour(*colour);
                           if (cell_and_symmetry_window) {
                              wxWindow *b = cell_and_symmetry_window->FindWindow("symmetry_swatch");
                              if (wxBitmapButton *s = wxDynamicCast(b, wxBitmapButton)) s->SetBitmap(swatch(*colour));
                           }
                        },
                        [colour, before](bool kept) {
                           if (kept) return;
                           *colour = before;
                           bandicoot_core_set_symmetry_colour(before);
                           if (cell_and_symmetry_window) {
                              wxWindow *b = cell_and_symmetry_window->FindWindow("symmetry_swatch");
                              if (wxBitmapButton *s = wxDynamicCast(b, wxBitmapButton)) s->SetBitmap(swatch(before));
                           }
                        });
   });
   pick->SetName("symmetry_swatch");
   expanded->Bind(wxEVT_CHECKBOX, [](wxCommandEvent &e) {
      bandicoot_core_set_symmetry_expanded_labels(e.IsChecked());
   });
   cells->on_change = [](int i) { bandicoot_core_set_unit_cells_shown(i == 0); };

   auto close = [d]() {
      cell_and_symmetry_window = nullptr;
      d->Destroy();
   };
   d->Bind(wxEVT_BUTTON, [apply_radius, close](wxCommandEvent &) {
      if (apply_radius()) close();
   }, wxID_OK);
   d->Bind(wxEVT_CLOSE_WINDOW, [close](wxCloseEvent &) { close(); });
   show(d, col, 340);
}

// ---------------------------------------------- Generic Display Objects

// The list follows the objects as they come and go: they are made by many
// parts of Coot and by scripts, with no notice to a window.
class GenericObjectsWindow : public wxFrame {
public:
   explicit GenericObjectsWindow(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "Generic Display Objects", wxDefaultPosition, wxSize(420, 420),
                wxDEFAULT_FRAME_STYLE) {
      wxPanel *panel = new wxPanel(this);
      panel->SetBackgroundColour(palette::face());
      BevelButton *all = new BevelButton(panel, wxID_ANY, "Display/Undisplay All");
      all->set_toggle(true);
      list = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxBORDER_SUNKEN);
      list->SetScrollRate(0, 10);
      grid = new wxFlexGridSizer(2, 4, 16);
      grid->AddGrowableCol(0);
      wxBoxSizer *pad = new wxBoxSizer(wxVERTICAL);
      pad->Add(grid, 0, wxEXPAND | wxALL, 8);
      list->SetSizer(pad);
      BevelButton *delete_all = new BevelButton(panel, wxID_ANY, "Delete All Generic Display Objects");
      delete_all->set_icon(icon("stock-delete.png", wxSize(16, 16)));
      wxButton *close = new wxButton(panel, wxID_CLOSE, "Close");

      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(all, 0, wxALL, 8);
      col->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
      col->Add(delete_all, 0, wxALL, 8);
      col->Add(close, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 8);
      panel->SetSizer(col);

      all->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
         bandicoot_core_set_all_generic_objects_displayed(e.GetInt() != 0);
         refresh();
      });
      delete_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
         bandicoot_core_close_all_generic_objects();
         refresh();
      });
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent &e) { shown = nullptr; e.Skip(); });
      timer.Bind(wxEVT_TIMER, [this](wxTimerEvent &) { refresh(); });
      refresh();
      timer.Start(500);
   }

   ~GenericObjectsWindow() override { timer.Stop(); }

   static GenericObjectsWindow *shown;

private:
   wxScrolledWindow *list;
   wxFlexGridSizer *grid;
   wxTimer timer;
   std::vector<bandicoot_generic_object> objects;
   std::vector<wxCheckBox *> checks;

   void refresh() {
      std::vector<bandicoot_generic_object> now = bandicoot_core_generic_objects();
      bool same = now.size() == objects.size();
      for (size_t i = 0; same && i < now.size(); i++)
         same = now[i].index == objects[i].index && now[i].name == objects[i].name;
      if (same) {
         for (size_t i = 0; i < now.size(); i++)
            if (checks[i]->GetValue() != now[i].displayed) checks[i]->SetValue(now[i].displayed);
         objects = now;
         return;
      }
      objects = now;
      checks.clear();
      grid->Clear(true);
      if (objects.empty()) grid->Add(new wxStaticText(list, wxID_ANY, "No generic display objects"));
      for (const bandicoot_generic_object &o : objects) {
         wxCheckBox *c = new wxCheckBox(list, wxID_ANY, "Display");
         c->SetValue(o.displayed);
         int index = o.index;
         c->Bind(wxEVT_CHECKBOX, [index](wxCommandEvent &e) {
            bandicoot_core_set_generic_object_displayed(index, e.IsChecked());
         });
         grid->Add(new wxStaticText(list, wxID_ANY, wxString::FromUTF8(o.name)), 0, wxALIGN_CENTER_VERTICAL);
         grid->Add(c, 0, wxALIGN_CENTER_VERTICAL);
         checks.push_back(c);
      }
      list->FitInside();
      list->Layout();
   }
};

GenericObjectsWindow *GenericObjectsWindow::shown = nullptr;

void generic_display_objects(wxWindow *parent) {
   if (GenericObjectsWindow::shown) { GenericObjectsWindow::shown->Raise(); return; }
   GenericObjectsWindow::shown = new GenericObjectsWindow(parent);
   GenericObjectsWindow::shown->CentreOnParent();
   GenericObjectsWindow::shown->Show();
}

// -------------------------------------------------- NCS Ghost Control

wxFrame *ncs_window = nullptr;

void ncs_ghost_control(wxWindow *parent) {
   if (ncs_window) { ncs_window->Raise(); return; }
   std::vector<bandicoot_ncs_molecule> mols = bandicoot_core_ncs_molecules();
   if (mols.empty()) {
      info(parent, "No models have NCS");
      return;
   }
   ScrollingWindow w = scrolling_window(parent, "NCS Ghost Control", wxSize(460, 560));
   ncs_window = w.frame;
   w.frame->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent &e) { ncs_window = nullptr; e.Skip(); });
   for (const bandicoot_ncs_molecule &m : mols) {
      wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, w.scroller, wxString::FromUTF8(m.label));
      wxWindow *b = box->GetStaticBox();
      wxCheckBox *ghosts = new wxCheckBox(b, wxID_ANY, "Display Non-crystallographic Ghosts");
      ghosts->SetValue(m.ghosts_shown);
      box->Add(ghosts, 0, wxALL, 4);
      box->Add(new wxStaticLine(b), 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
      wxFlexGridSizer *grid = new wxFlexGridSizer(2, 2, 24);
      grid->Add(new wxStaticText(b, wxID_ANY, "Displayed Chains"));
      grid->Add(new wxStaticText(b, wxID_ANY, "NCS Master Chain"));
      int imol = m.imol;
      auto chain_checks = std::make_shared<std::vector<wxCheckBox *>>();
      for (size_t ich = 0; ich < m.chains.size(); ich++) {
         wxString chain = "Chain " + wxString::FromUTF8(m.chains[ich]);
         wxCheckBox *c = new wxCheckBox(b, wxID_ANY, chain);
         c->SetValue(m.chain_shown[ich]);
         c->Enable(int(ich) != m.master);
         c->SetToolTip("Show this chain as a ghost chain?");
         int i = int(ich);
         c->Bind(wxEVT_CHECKBOX, [imol, i](wxCommandEvent &e) {
            bandicoot_core_set_ncs_ghost_chain_shown(imol, i, e.IsChecked());
         });
         wxRadioButton *r = new wxRadioButton(b, wxID_ANY, chain, wxDefaultPosition, wxDefaultSize,
                                              ich == 0 ? wxRB_GROUP : 0);
         r->SetValue(int(ich) == m.master);
         r->SetToolTip("The chain to which operators and ghosts are generated from other chains");
         r->Bind(wxEVT_RADIOBUTTON, [imol, i, chain_checks](wxCommandEvent &) {
            bandicoot_core_set_ncs_master(imol, i);
            for (size_t j = 0; j < chain_checks->size(); j++) {
               (*chain_checks)[j]->Enable(int(j) != i);
               (*chain_checks)[j]->SetValue(int(j) != i);
            }
         });
         chain_checks->push_back(c);
         grid->Add(c);
         grid->Add(r);
      }
      box->Add(grid, 0, wxALL, 6);
      ghosts->Bind(wxEVT_CHECKBOX, [imol](wxCommandEvent &e) {
         bandicoot_core_set_ncs_ghosts_shown(imol, e.IsChecked());
      });
      w.rows->Add(box, 0, wxEXPAND | wxALL, 6);
   }
   w.scroller->FitInside();
   w.frame->CentreOnParent();
   w.frame->Show();
}

// -------------------------------------------------------- Screenshots

enum class Shot { Simple, Povray, Raster3D };

void screenshot(wxWindow *parent, Shot kind) {
   if (kind == Shot::Simple) {
      std::string warning = bandicoot_core_dark_density_warning();
      if (! warning.empty()) info(parent, wxString::FromUTF8(warning));
   }
   FileRequest r;
   r.title = kind == Shot::Simple ? "Screenshot" : kind == Shot::Povray ? "Povray Image" : "Raster3D Image";
   r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
   r.file_name = kind == Shot::Povray ? "coot-povray" : "coot.png";
   wxString path = save_file(parent, r);
   if (path.empty()) return;
   std::string p = path.utf8_string();
   // the panel is gone before the graphics are read or rendered
   parent->CallAfter([parent, kind, p]() {
      wxBusyCursor busy;
      if (kind == Shot::Simple) {
         if (! bandicoot_core_screenshot(p))
            error(parent, "Could not save the screenshot", wxString::FromUTF8(p),
                  "Screenshot: could not write " + p);
         return;
      }
      std::string image;
      bool found = false;
      bool ok = kind == Shot::Povray ? bandicoot_core_povray_image(p, image, found)
                                     : bandicoot_core_raster3d_image(p, image, found);
      if (ok) return;
      std::string program = kind == Shot::Povray ? "Povray" : "Raster3D";
      // the scene file is written either way
      if (! found)
         error(parent, program + " was not found", kind == Shot::Povray ? "povray" : "render",
               program + ": renderer not found; scene file written beside " + image);
      else
         error(parent, program + " did not render", wxString::FromUTF8(image),
               program + ": no image written to " + image);
   });
}

// ------------------------------------------------ Representation Tools

MoleculeChooser::molecule_list model_names() {
   MoleculeChooser::molecule_list models;
   for (const bandicoot_dm_molecule &m : bandicoot_core_dm_models()) models.push_back({ m.imol, m.name });
   return models;
}

std::string entry_text(EntryDialog &d, size_t i) {
   return d.field(i)->get_text().Trim().Trim(false).utf8_string();
}

// A molecule chooser and entry fields; ok returns false to keep it open.
void entry_dialog(wxWindow *parent, const wxString &title, bool with_molecule,
                  const std::vector<std::pair<wxString, wxString>> &fields,
                  const wxString &ok_label, std::function<bool(EntryDialog &)> ok) {
   MoleculeChooser::molecule_list models;
   if (with_molecule) {
      models = model_names();
      if (models.empty()) { info(parent, "No models loaded"); return; }
   }
   EntryDialog *d = new EntryDialog(parent, title, wxEmptyString, models);
   for (const auto &f : fields) d->add_field(f.first, f.second);
   if (wxButton *b = wxDynamicCast(d->FindWindow(wxID_OK), wxButton)) b->SetLabel(ok_label);
   d->Fit();
   d->on_ok = ok;
   d->CentreOnParent();
   d->Show();
}

void choose_model(wxWindow *parent, const wxString &title, const wxString &prompt,
                  std::function<void(int)> ok) {
   MoleculeChooser::molecule_list models = model_names();
   if (models.empty()) { info(parent, "No models loaded"); return; }
   ChooserDialog *d = new ChooserDialog(parent, title, prompt, models);
   d->on_ok = ok;
   d->CentreOnParent();
   d->Show();
}

void ball_and_stick(wxWindow *parent) {
   entry_dialog(parent, "Ball & Stick", true, { { "Atom Selection:", "//A/1-2" } }, "Apply",
                [](EntryDialog &d) {
                   std::string sel = entry_text(d, 0);
                   if (sel.empty()) { wxBell(); return false; }
                   if (bandicoot_core_ball_and_stick(d.get_imol(), sel) < 0) {
                      error(&d, "Could not draw ball and stick for", wxString::FromUTF8(sel),
                            "Ball & Stick: no handle for " + sel);
                      return false;
                   }
                   return true;
                });
}

void clear_ball_and_stick(wxWindow *parent) {
   choose_model(parent, "Clear Ball & Stick", "Clear ball and stick objects from:",
                [](int imol) { bandicoot_core_clear_ball_and_stick(imol); });
}

void dotted_surface(wxWindow *parent) {
   entry_dialog(parent, "Dotted Surface", true, { { "Atom Selection:", "//A/1-2" } }, "Apply",
                [](EntryDialog &d) {
                   std::string sel = entry_text(d, 0);
                   if (sel.empty()) { wxBell(); return false; }
                   if (bandicoot_core_dotted_surface(d.get_imol(), sel) < 0) {
                      error(&d, "No atoms match", wxString::FromUTF8(sel),
                            "Dotted Surface: no atoms match " + sel);
                      return false;
                   }
                   return true;
                });
}

void clear_dots(wxWindow *parent) {
   entry_dialog(parent, "Clear Surface Dots", true, { { "Dots Handle Number:", "0" } }, "Clear",
                [](EntryDialog &d) {
                   long n;
                   if (! wxString::FromUTF8(entry_text(d, 0)).ToLong(&n) || n < 0) { wxBell(); return false; }
                   bandicoot_core_clear_dots(d.get_imol(), int(n));
                   return true;
                });
}

void limit_radius(wxWindow *parent) {
   entry_dialog(parent, "Limit Model Display Radius", false,
                { { "Display Radius Limit (0 for no limit):",
                    wxString::Format("%.1f", bandicoot_core_map_radius()) } }, "Set",
                [](EntryDialog &d) {
                   double r;
                   if (! wxString::FromUTF8(entry_text(d, 0)).ToCDouble(&r) || r < 0) { wxBell(); return false; }
                   bandicoot_core_limit_model_display_radius(float(r));
                   return true;
                });
}

void label_cas(wxWindow *parent) {
   choose_model(parent, "Label All CAs", "Label the CAs of:",
                [](int imol) { bandicoot_core_label_all_cas(imol); });
}

// The pore runs between two points taken from the screen centre.
void hole_window(wxWindow *parent) {
   MoleculeChooser::molecule_list models = model_names();
   if (models.empty()) { info(parent, "No models loaded"); return; }
   wxDialog *d = new_dialog(parent, "HOLE");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   MoleculeChooser *molecule = new MoleculeChooser(d, "HOLE-ify molecule:", models);
   col->Add(molecule, 0, wxEXPAND | wxBOTTOM, 10);
   auto start = std::make_shared<std::pair<bool, bandicoot_xyz>>(false, bandicoot_xyz());
   auto end = std::make_shared<std::pair<bool, bandicoot_xyz>>(false, bandicoot_xyz());
   wxButton *set_start = new wxButton(d, wxID_ANY, "Set Start Point");
   wxButton *set_end = new wxButton(d, wxID_ANY, "Set End Point");
   wxStaticText *start_at = new wxStaticText(d, wxID_ANY, "not set");
   wxStaticText *end_at = new wxStaticText(d, wxID_ANY, "not set");
   wxFlexGridSizer *points = new wxFlexGridSizer(2, 6, 10);
   points->Add(set_start);
   points->Add(start_at, 0, wxALIGN_CENTER_VERTICAL);
   points->Add(set_end);
   points->Add(end_at, 0, wxALIGN_CENTER_VERTICAL);
   col->Add(points, 0, wxBOTTOM, 10);
   wxBoxSizer *file_row = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *file = new wxTextCtrl(d, wxID_ANY, "hole_surface_dots.dat", wxDefaultPosition, wxSize(220, -1));
   file_row->Add(new wxStaticText(d, wxID_ANY, "Export surface dots to file:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   file_row->Add(file, 1);
   col->Add(file_row, 0, wxEXPAND | wxBOTTOM, 10);
   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
   buttons->GetAffirmativeButton()->SetLabel("Calculate");
   col->Add(buttons, 0, wxEXPAND);

   auto set_point = [d](std::shared_ptr<std::pair<bool, bandicoot_xyz>> p, wxStaticText *label) {
      p->first = true;
      p->second = bandicoot_core_rotation_centre();
      label->SetLabel(wxString::Format("(%.2f, %.2f, %.2f)", p->second.x, p->second.y, p->second.z));
      d->Layout();
      d->Fit();
   };
   set_start->Bind(wxEVT_BUTTON, [set_point, start, start_at](wxCommandEvent &) { set_point(start, start_at); });
   set_end->Bind(wxEVT_BUTTON, [set_point, end, end_at](wxCommandEvent &) { set_point(end, end_at); });
   d->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      if (! start->first || ! end->first) { wxBell(); return; }
      std::string f = file->GetValue().Trim().Trim(false).utf8_string();
      wxBusyCursor busy;
      bandicoot_core_hole(molecule->get_imol(), start->second, end->second, f);
      d->Destroy();
   }, wxID_OK);
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
   show(d, col, 380);
}

void add_view(wxWindow *parent) {
   entry_dialog(parent, "Add View", false, { { "View Name:", wxString::FromUTF8(bandicoot_core_next_view_name()) } },
                "Add View", [](EntryDialog &d) {
                   std::string name = entry_text(d, 0);
                   if (name.empty()) { wxBell(); return false; }
                   bandicoot_core_add_view(name);
                   return true;
                });
}

void add_spin_view(wxWindow *parent) {
   entry_dialog(parent, "Add a Spin View", false,
                { { "Number of Steps:", "3600" }, { "Number of Degrees (total):", "360" } }, "Add Spin",
                [](EntryDialog &d) {
                   long steps;
                   double degrees;
                   if (! wxString::FromUTF8(entry_text(d, 0)).ToLong(&steps) || steps < 1 ||
                       ! wxString::FromUTF8(entry_text(d, 1)).ToCDouble(&degrees)) {
                      wxBell();
                      return false;
                   }
                   bandicoot_core_add_spin_view(int(steps), float(degrees));
                   return true;
                });
}

void views_speed(wxWindow *parent) {
   entry_dialog(parent, "Set Views Play Speed", false,
                { { "Play Speed:", wxString::Format("%g", bandicoot_core_views_play_speed()) } }, "Set",
                [](EntryDialog &d) {
                   double v;
                   if (! wxString::FromUTF8(entry_text(d, 0)).ToCDouble(&v) || v <= 0) { wxBell(); return false; }
                   bandicoot_core_set_views_play_speed(float(v));
                   return true;
                });
}

// A button per view, and Play Views when there is more than one.
void views_panel(wxWindow *parent) {
   std::vector<std::string> views = bandicoot_core_views();
   if (views.empty()) { info(parent, "No views have been added"); return; }
   wxDialog *d = new_dialog(parent, "Views");
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   if (views.size() > 1) {
      wxButton *play = new wxButton(d, wxID_ANY, "Play Views");
      play->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
         std::string why_not;
         bandicoot_core_representation_direct("BREP_PLAY_VIEWS", why_not);
      });
      col->Add(play, 0, wxEXPAND | wxBOTTOM, 10);
   }
   for (size_t i = 0; i < views.size(); i++) {
      wxButton *b = new wxButton(d, wxID_ANY, wxString::FromUTF8(views[i]));
      int index = int(i);
      b->Bind(wxEVT_BUTTON, [index](wxCommandEvent &) { bandicoot_core_go_to_view(index); });
      col->Add(b, 0, wxEXPAND | wxBOTTOM, 4);
   }
   wxButton *close = new wxButton(d, wxID_CLOSE, "Close");
   close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
   col->Add(close, 0, wxALIGN_RIGHT | wxTOP, 8);
   show(d, col, 220);
}

// A file to save to, or to read, through the system panels.
wxString pick_file(wxWindow *parent, const wxString &title, const wxString &name, bool save) {
   FileRequest r;
   r.title = title;
   r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
   r.file_name = name;
   r.types = { FileType{ "Python scripts", { ".py" } } };
   if (save) return save_file(parent, r);
   std::vector<wxString> paths = open_files(parent, r);
   return paths.empty() ? wxString() : paths[0];
}

void save_views(wxWindow *parent) {
   wxString path = pick_file(parent, "Save Views", "coot-views.py", true);
   if (! path.empty()) bandicoot_core_save_views(path.utf8_string());
}

void annotate(wxWindow *parent) {
   entry_dialog(parent, "Annotate Position", false, { { "Annotation:", wxEmptyString } }, "Make Annotation",
                [](EntryDialog &d) {
                   std::string text = entry_text(d, 0);
                   if (text.empty()) { wxBell(); return false; }
                   bandicoot_core_add_annotation(text);
                   return true;
                });
}

void save_annotations(wxWindow *parent) {
   wxString path = pick_file(parent, "Save Annotations", "coot_annotations.py", true);
   if (! path.empty()) bandicoot_core_save_annotations(path.utf8_string());
}

void load_annotations(wxWindow *parent) {
   wxString path = pick_file(parent, "Load Annotations", "coot_annotations.py", false);
   if (! path.empty()) bandicoot_core_load_annotations(path.utf8_string());
}

const std::map<std::string, std::function<void(wxWindow *)>> &draw_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "on_anisotropic_atoms1_activate",        anisotropic_atoms },
      { "on_additional_representation1_activate", additional_representation },
      { "on_show_symmetry1_activate",            cell_and_symmetry },
      { "on_generic_display_objects1_activate",  generic_display_objects },
      { "on_ncs_ghost_control1_activate",        ncs_ghost_control },
      { "on_simple1_activate",   [](wxWindow *p) { screenshot(p, Shot::Simple); } },
      { "on_povray1_activate",   [](wxWindow *p) { screenshot(p, Shot::Povray); } },
      { "on_raster3d1_activate", [](wxWindow *p) { screenshot(p, Shot::Raster3D); } },
      { "BREP_BALL_AND_STICK",       ball_and_stick },
      { "BREP_CLEAR_BALL_AND_STICK", clear_ball_and_stick },
      { "BREP_DOTTED_SURFACE",       dotted_surface },
      { "BREP_CLEAR_DOTS",           clear_dots },
      { "BREP_LIMIT_RADIUS",         limit_radius },
      { "BREP_HOLE",                 hole_window },
      { "BREP_LABEL_CAS",            label_cas },
      { "BREP_ADD_VIEW",             add_view },
      { "BREP_ADD_SPIN_VIEW",        add_spin_view },
      { "BREP_VIEWS_PANEL",          views_panel },
      { "BREP_VIEWS_SPEED",          views_speed },
      { "BREP_SAVE_VIEWS",           save_views },
      { "BREP_ANNOTATE",             annotate },
      { "BREP_SAVE_ANNOTATIONS",     save_annotations },
      { "BREP_LOAD_ANNOTATIONS",     load_annotations },
   };
   return actions;
}

} // namespace

bool is_draw_action(const std::string &action) {
   return draw_actions().count(action) > 0;
}

bool run_draw_action(wxWindow *parent, const std::string &action) {
   auto it = draw_actions().find(action);
   if (it == draw_actions().end()) return false;
   it->second(parent);
   return true;
}

} // namespace wxui

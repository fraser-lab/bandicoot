// src/wxUI/main/validate-menu.cc

#include "wxUI/main/validate-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/click-through-dialog.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/main/delete-waters.hh"
#include "wxUI/main/rama-plot.hh"
#include "wxUI/main/refine-dialogs.hh"
#include "wxUI/widgets/molecule-chooser.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wxui {

namespace {

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   b->set_icon(icon(icon_file, wxSize(16, 16)));
   b->SetMinSize(wxSize(110, 28));
   return b;
}

// One of each options window at a time: opening it again raises the one there is.
wxDialog *single_dialog(wxWindow *parent, wxDialog *&shown, const wxString &title) {
   if (shown) {
      shown->Raise();
      return nullptr;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   wxDialog **slot = &shown;
   d->Bind(wxEVT_CLOSE_WINDOW, [d, slot](wxCloseEvent &) {
      *slot = nullptr;
      d->Destroy();
   });
   shown = d;
   return d;
}

// OK and Cancel at the bottom right; Cancel closes.
BevelButton *ok_cancel(wxDialog *d, wxBoxSizer *col, const wxString &ok_label, const char *ok_icon) {
   BevelButton *ok = icon_button(d, ok_label, ok_icon);
   BevelButton *cancel = icon_button(d, "Cancel", "stock-button-cancel.png");
   cancel->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });
   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   buttons->Add(ok, 0, wxRIGHT, 12);
   buttons->Add(cancel);
   col->Add(buttons, 0, wxALIGN_RIGHT | wxTOP, 10);
   return ok;
}

void show(wxDialog *d, wxBoxSizer *col) {
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
}

bool read_positive(wxTextCtrl *entry, float &value, float below = 1e10f) {
   double v;
   if (! entry->GetValue().Trim().Trim(false).ToCDouble(&v) || v <= 0 || v >= below) {
      wxBell();
      entry->SetFocus();
      return false;
   }
   value = float(v);
   return true;
}

// The peak or questionable-water list the . and , keys step through: the
// last of them opened, while it is open.
ClickThroughDialog *steppable = nullptr;

void make_steppable(ClickThroughDialog *d) {
   steppable = d;
   d->Bind(wxEVT_DESTROY, [d](wxWindowDestroyEvent &e) {
      if (e.GetEventObject() == d && steppable == d) steppable = nullptr;
      e.Skip();
   });
}

// A list of atoms to go to, one radio or button per atom.
ClickThroughDialog *atom_list(wxWindow *parent, const wxString &title, const std::vector<bandicoot_result_row> &rows,
                              ClickThroughList::RowType type, const wxString &action_label = wxEmptyString) {
   ClickThroughDialog *d = new ClickThroughDialog(parent, title, type);
   for (const bandicoot_result_row &r : rows)
      d->list()->add_item(wxString::FromUTF8(r.label), action_label);
   d->list()->on_click = [rows](size_t i) {
      if (i < rows.size()) bandicoot_core_go_to_result(rows[i]);
   };
   d->CentreOnParent();
   d->Show();
   return d;
}

// ---------------------------------------------- Incorrect Chiral Volumes

wxDialog *chirals_shown = nullptr;

void chiral_volumes(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No models to check");
      return;
   }
   wxDialog *d = single_dialog(parent, chirals_shown, "Check Chiral Volumes");
   if (! d) return;
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   TitledPanel *panel = new TitledPanel(d);
   panel->content_sizer()->Add(new wxStaticText(panel->content(), wxID_ANY, "Check Chiral Volumes on molecule:"),
                               0, wxALIGN_CENTER | wxALL, 4);
   MoleculeChooser *molecule = new MoleculeChooser(panel->content(), wxEmptyString, models);
   panel->content_sizer()->Add(molecule, 0, wxEXPAND | wxALL, 4);
   col->Add(panel, 0, wxEXPAND);
   BevelButton *apply = ok_cancel(d, col, "Apply", "stock-button-apply.png");
   apply->Bind(wxEVT_BUTTON, [d, parent, molecule](wxCommandEvent &) {
      int imol = molecule->get_imol();
      d->Close();
      bandicoot_bad_chirals b = bandicoot_core_bad_chirals(imol);
      if (! b.unknown_types.empty()) {
         wxString s = "Problem finding restraints for the following residues:\n\n";
         for (size_t i = 0; i < b.unknown_types.size(); i++)
            s += wxString::FromUTF8(b.unknown_types[i]) + ((i + 1) % 10 == 0 ? "\n" : "  ");
         info(parent, s);
      }
      if (b.atoms.empty())
         info(parent, "Coot detected no bad chiral volumes\nin this molecule.");
      else
         atom_list(parent, "Chiral Volume Baddies:", b.atoms, ClickThroughList::RowType::Button);
   });
   show(d, col);
}

// -------------------------------------------------- Difference Map Peaks

// one radio per molecule, the first chosen; the molecule's number for each
std::vector<std::pair<wxRadioButton *, int>>
radio_list(TitledPanel *frame, const std::vector<std::pair<int, std::string>> &molecules) {
   std::vector<std::pair<wxRadioButton *, int>> v;
   for (size_t i = 0; i < molecules.size(); i++) {
      wxRadioButton *r = new wxRadioButton(frame->content(), wxID_ANY, wxString::FromUTF8(molecules[i].second),
                                           wxDefaultPosition, wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
      r->SetValue(i == 0);
      frame->content_sizer()->Add(r, 0, wxALL, 3);
      v.push_back({ r, molecules[i].first });
   }
   return v;
}

int chosen(const std::vector<std::pair<wxRadioButton *, int>> &radios) {
   for (const auto &r : radios)
      if (r.first->GetValue()) return r.second;
   return -1;
}

// Opening it centres on the first peak, as Coot's did.
void show_peaks(wxWindow *parent, int imol_map, const std::vector<bandicoot_peak> &peaks) {
   std::vector<bandicoot_result_row> rows;
   ClickThroughDialog *d = new ClickThroughDialog(parent,
                                                  wxString::Format("Difference Map Peaks Dialog From Map No. %d", imol_map),
                                                  ClickThroughList::RowType::Radio);
   for (const bandicoot_peak &p : peaks) d->list()->add_item(wxString::FromUTF8(p.label));
   d->list()->on_click = [peaks](size_t i) {
      if (i < peaks.size()) bandicoot_core_go_to_peak(peaks[i], peaks[i].label);
   };
   d->list()->select(0);
   make_steppable(d);
   d->CentreOnParent();
   d->Show();
   bandicoot_core_go_to_peak(peaks[0], "1 of " + std::to_string(peaks.size()) + " (dialog opened) " +
                                        wxString::Format("%.2f", peaks[0].rmsd).utf8_string() + " rmsd");
}

wxDialog *peaks_shown = nullptr;

void difference_map_peaks(wxWindow *parent) {
   std::vector<std::pair<int, std::string>> maps = bandicoot_core_difference_maps_full_names();
   if (maps.empty()) {
      info(parent, "No difference maps to search for peaks");
      return;
   }
   wxDialog *d = single_dialog(parent, peaks_shown, "Difference Map Peak Analysis");
   if (! d) return;
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   TitledPanel *map_panel = new TitledPanel(d, "Select Difference Map");
   auto map_radios = radio_list(map_panel, maps);
   col->Add(map_panel, 0, wxEXPAND | wxBOTTOM, 8);
   TitledPanel *model_panel = new TitledPanel(d, "Select Model");
   auto model_radios = radio_list(model_panel, bandicoot_core_find_waters_models());
   col->Add(model_panel, 0, wxEXPAND | wxBOTTOM, 8);

   TitledPanel *level = new TitledPanel(d, "r.m.s.d. level");
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *rmsd = new wxTextCtrl(level->content(), wxID_ANY,
                                     wxString::Format("%.2f", bandicoot_core_difference_map_peaks_level()),
                                     wxDefaultPosition, wxSize(140, -1));
   row->Add(new wxStaticText(level->content(), wxID_ANY, "Find Peaks above"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   row->Add(rmsd, 0, wxRIGHT, 8);
   row->Add(new wxStaticText(level->content(), wxID_ANY, "r.m.s.d."), 0, wxALIGN_CENTER_VERTICAL);
   level->content_sizer()->Add(row, 0, wxALL, 3);
   col->Add(level, 0, wxEXPAND | wxBOTTOM, 8);

   TitledPanel *which = new TitledPanel(d, "Which Peaks?");
   wxCheckBox *positive = new wxCheckBox(which->content(), wxID_ANY, "Find Positive Peaks?");
   wxCheckBox *negative = new wxCheckBox(which->content(), wxID_ANY, "Find Negative Peaks Also?");
   wxCheckBox *around_model = new wxCheckBox(which->content(), wxID_ANY, "Around Model?");
   positive->SetValue(true);
   negative->SetValue(true);
   for (wxCheckBox *c : { positive, negative, around_model }) which->content_sizer()->Add(c, 0, wxALL, 3);
   col->Add(which, 0, wxEXPAND);

   BevelButton *find = ok_cancel(d, col, "Find Peaks", "stock-button-ok.png");
   find->Bind(wxEVT_BUTTON, [d, parent, map_radios, model_radios, rmsd, positive, negative,
                             around_model](wxCommandEvent &) {
      float level;
      if (! read_positive(rmsd, level, 1000)) return;
      int imol_map = chosen(map_radios);
      d->Close();
      std::vector<bandicoot_peak> peaks =
         bandicoot_core_difference_map_peaks(imol_map, chosen(model_radios), level, positive->GetValue(),
                                             negative->GetValue(), around_model->GetValue());
      if (peaks.empty())
         info(parent, wxString::Format("No difference map peaks\nat %.2f sigma", level));
      else
         show_peaks(parent, imol_map, peaks);
   });
   show(d, col);
}

// --------------------------------------------------- Check/Delete Waters

wxDialog *waters_shown = nullptr;

void check_waters(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No models to check");
      return;
   }
   wxDialog *d = single_dialog(parent, waters_shown, "Checking Waters:");
   if (! d) return;
   bandicoot_check_waters c = bandicoot_core_check_waters_values();
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   MoleculeChooser *molecule = new MoleculeChooser(d, "Molecule Number:", models);
   molecule->set_imol(c.imol);
   col->Add(molecule, 0, wxEXPAND | wxBOTTOM, 8);
   wxBoxSizer *action_row = new wxBoxSizer(wxHORIZONTAL);
   wxChoice *action = new wxChoice(d, wxID_ANY);
   action->Append("Check");
   action->Append("Delete");
   action->SetSelection(0);
   action_row->Add(new wxStaticText(d, wxID_ANY, "Action:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
   action_row->Add(action, 1);
   col->Add(action_row, 0, wxEXPAND | wxBOTTOM, 8);

   TitledPanel *criteria = new TitledPanel(d);
   wxWindow *cp = criteria->content();
   wxFlexGridSizer *grid = new wxFlexGridSizer(4, 6, 8);
   grid->AddGrowableCol(2);
   auto criterion = [cp, grid](const wxString &label, float value, const wxString &unit, wxCheckBox *&active) {
      wxTextCtrl *entry = new wxTextCtrl(cp, wxID_ANY, wxString::Format("%.2f", value), wxDefaultPosition,
                                         wxSize(80, -1));
      active = new wxCheckBox(cp, wxID_ANY, "Active");
      active->SetValue(true);
      grid->Add(new wxStaticText(cp, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(entry, 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(new wxStaticText(cp, wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(active, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
      return entry;
   };
   wxCheckBox *use_b, *use_map, *use_min, *use_max;
   wxTextCtrl *b_factor = criterion("with B factor greater than:", c.b_factor, "A^2", use_b);
   wxTextCtrl *map_rmsd = criterion("with map r.m.s.d. level less than:", c.map_rmsd, "electrons/A3", use_map);
   wxTextCtrl *min_d = criterion("with closest contact less than:", c.min_distance, "A", use_min);
   wxTextCtrl *max_d = criterion("with closest contact more than:", c.max_distance, "A", use_max);
   criteria->content_sizer()->Add(grid, 0, wxEXPAND | wxALL, 4);

   // the difference-map test, off at first and greyed while off
   std::vector<std::pair<int, std::string>> diff_maps = bandicoot_core_difference_maps();
   wxStaticText *diff_label = new wxStaticText(cp, wxID_ANY, "Difference Map Variance Analysis using Map");
   MoleculeChooser *diff_map = new MoleculeChooser(cp, wxEmptyString, diff_maps);
   wxCheckBox *use_diff = new wxCheckBox(cp, wxID_ANY, "Active");
   wxBoxSizer *diff_row = new wxBoxSizer(wxHORIZONTAL);
   diff_row->Add(diff_map, 1, wxLEFT, 40);
   diff_row->Add(use_diff, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
   criteria->content_sizer()->Add(diff_label, 0, wxLEFT | wxTOP, 4);
   criteria->content_sizer()->Add(diff_row, 0, wxEXPAND | wxALL, 4);
   diff_label->Enable(false);
   diff_map->Enable(false);
   use_diff->Enable(! diff_maps.empty());
   use_diff->Bind(wxEVT_CHECKBOX, [diff_label, diff_map](wxCommandEvent &e) {
      diff_label->Enable(e.IsChecked());
      diff_map->Enable(e.IsChecked());
   });

   wxCheckBox *partial = new wxCheckBox(cp, wxID_ANY, "Ignore Partial Occupancy close contacts");
   wxCheckBox *zero_occ = new wxCheckBox(cp, wxID_ANY, "Ignore Waters with Zero Occupancy");
   criteria->content_sizer()->Add(partial, 0, wxALL, 4);
   criteria->content_sizer()->Add(zero_occ, 0, wxALL, 4);
   col->Add(criteria, 0, wxEXPAND | wxBOTTOM, 8);

   RadioGroup *match = new RadioGroup(d, "For Waters that match:", { "Any Criterion", "All Criteria" }, 0);
   col->Add(match, 0, wxEXPAND);

   BevelButton *ok = ok_cancel(d, col, "OK", "stock-button-ok.png");
   ok->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      bandicoot_check_waters now;
      now.imol = molecule->get_imol();
      if (! read_positive(b_factor, now.b_factor) || ! read_positive(map_rmsd, now.map_rmsd) ||
          ! read_positive(min_d, now.min_distance) || ! read_positive(max_d, now.max_distance))
         return;
      now.use_b_factor = use_b->GetValue();
      now.use_map_rmsd = use_map->GetValue();
      now.use_min_distance = use_min->GetValue();
      now.use_max_distance = use_max->GetValue();
      now.ignore_partial_occupancy_contacts = partial->GetValue();
      now.ignore_zero_occupancy = zero_occ->GetValue();
      now.any = match->get_selection() == 0;
      bool check = action->GetSelection() == 0;
      int imol_diff = use_diff->GetValue() ? diff_map->get_imol() : -1;

      // the density test needs the refinement map; ask once more, as Coot did
      if (bandicoot_core_refinement_map() < 0) select_refinement_map(parent);
      if (bandicoot_core_refinement_map() < 0) {
         error(d, check ? "Cannot check waters" : "Cannot delete waters", "No refinement map is set.");
         return;
      }
      d->Close();
      if (imol_diff >= 0) {
         std::vector<bandicoot_result_row> odd = bandicoot_core_waters_by_difference_map(now.imol, imol_diff);
         if (odd.empty())
            info(parent, "There were no strange/anomalous waters\n(in relation to the difference map).");
         else
            atom_list(parent, "Interesting/Strange Waters:", odd, ClickThroughList::RowType::Radio);
      }
      if (check) {
         std::vector<bandicoot_result_row> rows;
         bandicoot_core_check_waters(now, rows);
         if (rows.empty()) {
            info(parent, "There were no suspicious waters\nmatching those criteria in\nMolecule " +
                         wxString::FromUTF8(bandicoot_core_short_name(now.imol)));
         } else {
            ClickThroughDialog *list = atom_list(parent, "Questionable Waters", rows, ClickThroughList::RowType::Radio);
            list->list()->select(0);
            make_steppable(list);
         }
      } else {
         int n = 0;
         bandicoot_core_delete_checked_waters(now, n);
         info(parent, wxString::Format("Deleted %d waters", n));
      }
   });
   show(d, col);
   // asked after the window is up, so the chooser comes on top of it
   if (bandicoot_core_refinement_map() < 0) parent->CallAfter([parent]() { select_refinement_map(parent); });
}

// ------------------------------------------------------- Kleywegt Plot

// Two models, each optionally narrowed to one chain; both or neither.
wxDialog *kleywegt_shown = nullptr;

struct KleywegtSide {
   MoleculeChooser *molecule;
   wxCheckBox *use_chain;
   wxChoice *chain;
};

KleywegtSide kleywegt_side(wxDialog *d, wxBoxSizer *col, const wxString &title,
                           const MoleculeChooser::molecule_list &models, int initial) {
   TitledPanel *panel = new TitledPanel(d, title);
   KleywegtSide side;
   side.molecule = new MoleculeChooser(panel->content(), wxEmptyString, models);
   if (initial < int(models.size())) side.molecule->set_imol(models[initial].first);
   side.use_chain = new wxCheckBox(panel->content(), wxID_ANY, "Use Specific Chain");
   side.chain = new wxChoice(panel->content(), wxID_ANY);
   side.chain->Enable(false);
   panel->content_sizer()->Add(side.molecule, 0, wxEXPAND | wxALL, 3);
   panel->content_sizer()->Add(side.use_chain, 0, wxALL, 3);
   panel->content_sizer()->Add(side.chain, 0, wxEXPAND | wxALL, 3);
   col->Add(panel, 0, wxEXPAND | wxBOTTOM, 10);
   wxChoice *chain = side.chain;
   MoleculeChooser *molecule = side.molecule;
   auto fill = [chain, molecule]() {
      chain->Clear();
      for (const std::string &id : bandicoot_core_chain_ids(molecule->get_imol())) chain->Append(wxString::FromUTF8(id));
      if (chain->GetCount() > 0) chain->SetSelection(0);
   };
   fill();
   side.molecule->on_change = [fill](int) { fill(); };
   side.use_chain->Bind(wxEVT_CHECKBOX, [chain](wxCommandEvent &e) { chain->Enable(e.IsChecked()); });
   return side;
}

void kleywegt_plot(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No models to compare");
      return;
   }
   wxDialog *d = single_dialog(parent, kleywegt_shown, "Ramachandran Plot Differences");
   if (! d) return;
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Ramachandran Plot Differences"), 0, wxALIGN_CENTER | wxBOTTOM, 10);
   KleywegtSide first = kleywegt_side(d, col, "First Structure", models, 0);
   KleywegtSide second = kleywegt_side(d, col, "Second Structure", models, models.size() > 1 ? 1 : 0);
   BevelButton *ok = ok_cancel(d, col, "OK", "stock-button-ok.png");
   ok->Bind(wxEVT_BUTTON, [d, parent, first, second](wxCommandEvent &) {
      bool by_chain = first.use_chain->GetValue();
      if (by_chain != second.use_chain->GetValue()) {
         info(d, "Can't make sense of chain selection.  Try again?");
         return;
      }
      if (by_chain && (first.chain->GetSelection() < 0 || second.chain->GetSelection() < 0)) { wxBell(); return; }
      int imol1 = first.molecule->get_imol(), imol2 = second.molecule->get_imol();
      std::string chain1 = by_chain ? first.chain->GetStringSelection().utf8_string() : std::string();
      std::string chain2 = by_chain ? second.chain->GetStringSelection().utf8_string() : std::string();
      d->Close();
      show_kleywegt_plot(parent, imol1, imol2, by_chain, chain1, chain2);
   });
   show(d, col);
}

// ------------------------------------------ GLN and ASN B-factor Outliers

// Fix flips the amide 180 degrees, swapping its O and N.
void gln_asn_outliers(wxWindow *parent, int imol) {
   std::vector<bandicoot_result_row> rows = bandicoot_core_gln_asn_outliers(imol);
   if (rows.empty()) {
      info(parent, "No GLN or ASN B-factor outliers found");
      return;
   }
   ClickThroughDialog *d = atom_list(parent, "GLN and ASN B-factor Outliers", rows,
                                     ClickThroughList::RowType::Button, "Fix");
   d->list()->on_action = [rows](size_t i) {
      if (i < rows.size()) bandicoot_core_flip_side_chain(rows[i]);
   };
}

const std::map<std::string, std::function<void(wxWindow *)>> &validate_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "on_incorrect_chiral_volumes1_activate", chiral_volumes },
      { "on_unmodelled_blobs1_activate",         show_unmodelled_blobs },
      { "on_difference_map_peaks1_activate",     difference_map_peaks },
      { "on_check_waters1_activate",             check_waters },
      { "on_ramachandran_differences_plot1_activate", kleywegt_plot },
   };
   return actions;
}

} // namespace

bool is_validate_action(const std::string &action) {
   return validate_actions().count(action) > 0;
}

bool run_validate_action(wxWindow *parent, const std::string &action) {
   auto it = validate_actions().find(action);
   if (it == validate_actions().end()) return false;
   it->second(parent);
   return true;
}

// the residue graphs, by Coot's menu names
const std::vector<std::string> graph_lists = {
   "geometry_analysis1", "peptide_omega_analysis1", "temp_fact_variance_analysis1", "temp_fact_analysis1",
   "rotamer_analysis1", "density_fit_analysis1", "ncs_differences1",
};

bool is_validate_molecule_list(const std::string &list) {
   return list == "gln_and_asn_b_factor_outliers1" || list == "ramachandran_plot1" || list == "probe_clashes1" ||
          std::find(graph_lists.begin(), graph_lists.end(), list) != graph_lists.end();
}

void run_validate_molecule(wxWindow *parent, const std::string &list, int imol) {
   if (list == "gln_and_asn_b_factor_outliers1") {
      gln_asn_outliers(parent, imol);
      return;
   }
   if (list == "ramachandran_plot1") {
      show_rama_plot(parent, imol);
      return;
   }
   if (list == "probe_clashes1") {
      std::string why_not;
      if (! bandicoot_core_probe_clashes(imol, why_not))
         error(parent, "Cannot run Probe clashes", wxString::FromUTF8(why_not));
      return;
   }
   std::string why_not;
   if (! bandicoot_core_validation_graph(list, imol, why_not) && ! why_not.empty())
      info(parent, "No graph: " + wxString::FromUTF8(why_not));
}

bool step_validate_list(int step) {
   if (! steppable) return false;
   ClickThroughList *list = steppable->list();
   int n = int(list->count());
   if (n == 0) return false;
   int i = list->selected();
   // wraps at either end, as Coot's keys did
   i = (i < 0) ? 0 : ((i + step) % n + n) % n;
   list->select(i);
   list->scroll_to(size_t(i));
   if (list->on_click) list->on_click(size_t(i));
   return true;
}

} // namespace wxui

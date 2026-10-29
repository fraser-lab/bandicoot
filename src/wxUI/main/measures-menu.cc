// src/wxUI/main/measures-menu.cc

#include "wxUI/main/measures-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/utils.h>

#include <functional>
#include <map>
#include <string>

namespace wxui {

namespace {

const wxString angstrom = wxString::FromUTF8("\xc3\x85ngstr\xc3\xb6m");

// One of each window at a time: opening it again raises the one there is.
// on_destroy runs as it goes, however it is closed.
wxDialog *single_dialog(wxWindow *parent, wxDialog *&shown, const wxString &title,
                        std::function<void()> on_destroy = nullptr) {
   if (shown) {
      shown->Raise();
      return nullptr;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   wxDialog **slot = &shown;
   d->Bind(wxEVT_DESTROY, [d, slot, on_destroy](wxWindowDestroyEvent &e) {
      if (e.GetEventObject() == d) {
         *slot = nullptr;
         if (on_destroy) on_destroy();
      }
      e.Skip();
   });
   shown = d;
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

bool read_distance(wxTextCtrl *entry, float &value) {
   double v;
   if (! entry->GetValue().Trim().Trim(false).ToCDouble(&v)) return false;
   value = float(v);
   return true;
}

// A Min and a Max Distance row, each in Angstroms.
void distance_rows(wxWindow *parent, wxSizer *into, float min_distance, float max_distance, int decimals,
                   wxTextCtrl *&min_entry, wxTextCtrl *&max_entry) {
   wxFlexGridSizer *grid = new wxFlexGridSizer(3, 6, 8);
   grid->AddGrowableCol(1);
   auto row = [parent, grid, decimals](const wxString &label, float value) {
      wxTextCtrl *entry = new wxTextCtrl(parent, wxID_ANY, wxString::Format("%.*f", decimals, value),
                                         wxDefaultPosition, wxSize(140, -1), wxTE_PROCESS_ENTER);
      grid->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
      grid->Add(entry, 1, wxEXPAND);
      grid->Add(new wxStaticText(parent, wxID_ANY, angstrom), 0, wxALIGN_CENTER_VERTICAL);
      return entry;
   };
   min_entry = row("Min Distance", min_distance);
   max_entry = row("Max Distance", max_distance);
   into->Add(grid, 0, wxEXPAND | wxALL, 4);
}

// --------------------------------------------------- Distances & Angles

// Distance, Angle and Torsion stay down while the core waits for their atoms;
// Dynamic Distance stays down until its distance is made. One waits at a time.
class GeometryWindow {
public:
   static wxDialog *shown;
   static std::map<std::string, BevelButton *> picks;

   static void open(wxWindow *parent) {
      wxDialog *d = single_dialog(parent, shown, "Geometry", []() {
         // closing stops a pick still waiting
         for (auto &pick : picks)
            if (pick.second->is_on()) bandicoot_core_geometry_pick(pick.first, false);
         picks.clear();
      });
      if (! d) return;
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      add_pick(d, col, "Distance", "distance");
      add_pick(d, col, "Angle", "angle");
      add_pick(d, col, "Torsion", "torsion");
      add_pick(d, col, "Dynamic Distance", "dynamic_distance");
      col->Add(new wxStaticLine(d), 0, wxEXPAND | wxTOP | wxBOTTOM, 4);
      add_button(d, col, "Clear Last Distance", bandicoot_core_clear_last_distance);
      add_button(d, col, "Clear All Distances", bandicoot_core_clear_all_distances);
      add_button(d, col, "Clear All Atom Labels", bandicoot_core_clear_atom_labels);
      wxButton *close = new wxButton(d, wxID_CLOSE, "Close");
      close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
      col->Add(close, 0, wxALIGN_RIGHT | wxTOP, 8);
      show(d, col, 220);
   }

   static void release(const std::string &kind) {
      auto it = picks.find(kind);
      if (it != picks.end()) it->second->set_on(false);
   }

private:
   static void add_pick(wxDialog *d, wxSizer *col, const wxString &label, const std::string &kind) {
      BevelButton *b = new BevelButton(d, wxID_ANY, label);
      b->set_toggle(true);
      b->Bind(wxEVT_TOGGLEBUTTON, [b, kind](wxCommandEvent &e) {
         bool on = e.GetInt() != 0;
         if (on) {
            for (auto &pick : picks) {
               if (pick.second == b || ! pick.second->is_on()) continue;
               pick.second->set_on(false);
               bandicoot_core_geometry_pick(pick.first, false);
            }
         }
         bandicoot_core_geometry_pick(kind, on);
      });
      col->Add(b, 0, wxEXPAND | wxALL, 2);
      picks[kind] = b;
   }

   static void add_button(wxDialog *d, wxSizer *col, const wxString &label, void (*action)()) {
      BevelButton *b = new BevelButton(d, wxID_ANY, label);
      b->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
      col->Add(b, 0, wxEXPAND | wxALL, 2);
   }
};

wxDialog *GeometryWindow::shown = nullptr;
std::map<std::string, BevelButton *> GeometryWindow::picks;

// ------------------------------------------------ Environment Distances

// The check boxes act at once; the limits on Return or OK.
wxDialog *environment_shown = nullptr;

void environment_distances(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, environment_shown, "Environment Distances");
   if (! d) return;
   bandicoot_environment e = bandicoot_core_environment();
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   TitledPanel *show_panel = new TitledPanel(d);
   wxCheckBox *shown = new wxCheckBox(show_panel->content(), wxID_ANY, "Show Residue Environment?");
   shown->SetValue(e.shown);
   show_panel->content_sizer()->Add(shown, 0, wxALL, 4);
   col->Add(show_panel, 0, wxEXPAND | wxBOTTOM, 8);

   TitledPanel *types = new TitledPanel(d, "Environment Distance Type Selection");
   wxCheckBox *h_bonds = new wxCheckBox(types->content(), wxID_ANY, "Hydrogen/Other Bonds");
   wxCheckBox *bumps = new wxCheckBox(types->content(), wxID_ANY, "Bumps");
   h_bonds->SetValue(e.h_bonds);
   bumps->SetValue(e.bumps);
   types->content_sizer()->Add(h_bonds, 0, wxALL, 4);
   types->content_sizer()->Add(bumps, 0, wxALL, 4);
   col->Add(types, 0, wxEXPAND | wxBOTTOM, 8);

   TitledPanel *limits = new TitledPanel(d, "Cut-off Distances");
   wxTextCtrl *min_entry, *max_entry;
   distance_rows(limits->content(), limits->content_sizer(), e.min_distance, e.max_distance, 1,
                 min_entry, max_entry);
   col->Add(limits, 0, wxEXPAND | wxBOTTOM, 8);

   wxCheckBox *label_atom = new wxCheckBox(d, wxID_ANY, "Label Atom?");
   label_atom->SetValue(e.label_atom);
   col->Add(label_atom, 0, wxALL, 4);

   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   col->Add(ok, 0, wxALIGN_RIGHT | wxTOP, 8);

   auto follow_shown = [types, limits, label_atom](bool on) {
      types->Enable(on);
      limits->Enable(on);
      label_atom->Enable(on);
   };
   follow_shown(e.shown);
   shown->Bind(wxEVT_CHECKBOX, [follow_shown](wxCommandEvent &ev) {
      bandicoot_core_set_environment_shown(ev.IsChecked());
      follow_shown(ev.IsChecked());
   });
   h_bonds->Bind(wxEVT_CHECKBOX, [](wxCommandEvent &ev) { bandicoot_core_set_environment_h_bonds(ev.IsChecked()); });
   bumps->Bind(wxEVT_CHECKBOX, [](wxCommandEvent &ev) { bandicoot_core_set_environment_bumps(ev.IsChecked()); });
   label_atom->Bind(wxEVT_CHECKBOX, [](wxCommandEvent &ev) {
      bandicoot_core_set_environment_label_atom(ev.IsChecked());
   });

   auto apply = [min_entry, max_entry]() {
      float lo, hi;
      if (! read_distance(min_entry, lo) || ! read_distance(max_entry, hi) ||
          ! bandicoot_core_set_environment_limits(lo, hi)) {
         wxBell();
         return false;
      }
      bandicoot_environment now = bandicoot_core_environment();
      min_entry->ChangeValue(wxString::Format("%.1f", now.min_distance));
      max_entry->ChangeValue(wxString::Format("%.1f", now.max_distance));
      return true;
   };
   min_entry->Bind(wxEVT_TEXT_ENTER, [apply](wxCommandEvent &) { apply(); });
   max_entry->Bind(wxEVT_TEXT_ENTER, [apply](wxCommandEvent &) { apply(); });
   ok->Bind(wxEVT_BUTTON, [d, apply](wxCommandEvent &) {
      if (apply()) d->Destroy();
   });
   show(d, col, 320);
}

// ------------------------------------------------------ Plane Distances

// Open, picks add atoms to the plane or measure deviations from it (both
// reported in the status bar); closing ends the picks and clears the plane.
wxDialog *plane_shown = nullptr;

void plane_distances(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, plane_shown, "LSQ Plane", []() { bandicoot_core_plane_end(); });
   if (! d) return;
   bandicoot_core_plane_begin();
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   TitledPanel *panel = new TitledPanel(d, "Lsq Plane");
   wxPanel *p = panel->content();
   wxRadioButton *add_atom = new wxRadioButton(p, wxID_ANY, "Add Atom to Plane", wxDefaultPosition,
                                               wxDefaultSize, wxRB_GROUP);
   wxButton *remove_last = new wxButton(p, wxID_ANY, "Delete Last Plane Atom");
   wxRadioButton *deviation = new wxRadioButton(p, wxID_ANY, "Deviation of Atom");
   add_atom->SetValue(true);
   panel->content_sizer()->Add(add_atom, 0, wxALL, 4);
   panel->content_sizer()->Add(remove_last, 0, wxLEFT | wxBOTTOM, 28);
   panel->content_sizer()->Add(deviation, 0, wxALL, 4);
   col->Add(panel, 0, wxEXPAND | wxBOTTOM, 8);

   add_atom->Bind(wxEVT_RADIOBUTTON, [](wxCommandEvent &) { bandicoot_core_plane_mode(false); });
   deviation->Bind(wxEVT_RADIOBUTTON, [](wxCommandEvent &) { bandicoot_core_plane_mode(true); });
   remove_last->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_plane_remove_last_atom(); });

   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   ok->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
   col->Add(ok, 0, wxALIGN_RIGHT | wxTOP, 8);
   show(d, col, 280);
}

// ---------------------------------------------------- Pointer Distances

// The check box acts at once; the limits on Return or OK.
wxDialog *pointer_shown = nullptr;

void pointer_distances(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, pointer_shown, "Pointer Distances");
   if (! d) return;
   bandicoot_pointer_distances pd = bandicoot_core_pointer_distances();
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   TitledPanel *outer = new TitledPanel(d);
   wxCheckBox *shown = new wxCheckBox(outer->content(), wxID_ANY, "Show Pointer Distances");
   shown->SetValue(pd.shown);
   outer->content_sizer()->Add(shown, 0, wxALL, 4);
   TitledPanel *limits = new TitledPanel(outer->content());
   wxTextCtrl *min_entry, *max_entry;
   distance_rows(limits->content(), limits->content_sizer(), pd.min_distance, pd.max_distance, 2,
                 min_entry, max_entry);
   outer->content_sizer()->Add(limits, 0, wxEXPAND | wxALL, 4);
   col->Add(outer, 0, wxEXPAND | wxBOTTOM, 8);

   wxButton *ok = new wxButton(d, wxID_OK, "OK");
   col->Add(ok, 0, wxALIGN_RIGHT | wxTOP, 8);

   limits->Enable(pd.shown);
   shown->Bind(wxEVT_CHECKBOX, [limits](wxCommandEvent &ev) {
      bandicoot_core_set_pointer_distances_shown(ev.IsChecked());
      limits->Enable(ev.IsChecked());
   });
   auto apply = [min_entry, max_entry]() {
      float lo, hi;
      if (! read_distance(min_entry, lo) || ! read_distance(max_entry, hi) ||
          ! bandicoot_core_set_pointer_distance_limits(lo, hi)) {
         wxBell();
         return false;
      }
      return true;
   };
   min_entry->Bind(wxEVT_TEXT_ENTER, [apply](wxCommandEvent &) { apply(); });
   max_entry->Bind(wxEVT_TEXT_ENTER, [apply](wxCommandEvent &) { apply(); });
   ok->Bind(wxEVT_BUTTON, [d, apply](wxCommandEvent &) {
      if (apply()) d->Destroy();
   });
   show(d, col, 360);
}

const std::map<std::string, std::function<void(wxWindow *)>> &measures_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "on_residue_info1_activate",           [](wxWindow *) { bandicoot_core_pick_residue_info(); } },
      { "on_distances_and_angles1_activate",   GeometryWindow::open },
      { "on_environment_distances1_activate",  environment_distances },
      { "on_plane_distances1_activate",        plane_distances },
      { "on_pointer_distances1_activate",      pointer_distances },
      { "on_clear_atom_labels1_activate",      [](wxWindow *) { bandicoot_core_clear_atom_labels(); } },
   };
   return actions;
}

} // namespace

bool is_measures_action(const std::string &action) {
   return measures_actions().count(action) > 0;
}

bool run_measures_action(wxWindow *parent, const std::string &action) {
   auto it = measures_actions().find(action);
   if (it == measures_actions().end()) return false;
   it->second(parent);
   return true;
}

void release_geometry_toggle(const std::string &kind) {
   GeometryWindow::release(kind);
}

} // namespace wxui

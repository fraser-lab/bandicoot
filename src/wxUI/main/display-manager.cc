// src/wxUI/main/display-manager.cc

#include "wxUI/main/display-manager.hh"
#include "wxUI/main/map-properties.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/widgets/mixed-row.hh"
#include "wxUI/widgets/row-list.hh"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>

#include <vector>

namespace wxui {

namespace {

const int map_name_chars   = 34;  // the name fields' widths, as the GTK window has them
const int model_name_chars = 28;

// a checked toggle's label takes Coot's "on" colour
void show_check_state(wxCheckBox *c) {
   c->SetForegroundColour(c->GetValue() ? palette::text_on() : palette::text());
   c->Refresh();
}

wxCheckBox *coloured_check(MixedRow *row, const wxString &label, bool state) {
   wxCheckBox *c = row->add_check(label, state);
   show_check_state(c);
   return c;
}

struct MapRow {
   int imol;
   wxTextCtrl *name;
   wxCheckBox *display;
   wxRadioButton *scroll;
};

struct ModelRow {
   int imol;
   wxTextCtrl *name;
   wxCheckBox *display, *active;
   wxChoice *render_as;
   std::vector<wxCheckBox *> add_reps;   // its additional representations
};

class DisplayManager : public wxFrame {
public:
   explicit DisplayManager(wxWindow *parent);

   void open();
   // from the core; both wait for the core to finish what it is doing
   void molecules_changed();
   void state_changed();

private:
   wxSplitterWindow *splitter;
   RowList *map_list, *model_list;
   std::vector<MapRow> map_rows;
   std::vector<ModelRow> model_rows;
   bool rebuild_pending = false, refresh_pending = false;

   wxPanel *pane(wxWindow *parent, const wxString &title, RowList *&list,
                 wxBoxSizer *&header);
   void rebuild();
   void refresh();
   void add_map_row(const bandicoot_dm_molecule &m, int scroll_map);
   void add_model_row(const bandicoot_dm_molecule &m, const std::vector<wxString> &render_as);
   wxWindow *add_reps_row(const bandicoot_dm_molecule &m, ModelRow &r);
   void close();
};

DisplayManager::DisplayManager(wxWindow *parent)
   : wxFrame(parent, wxID_ANY, "Display Manager", wxDefaultPosition, wxSize(900, 480),
             wxDEFAULT_FRAME_STYLE) {

   wxPanel *panel = new wxPanel(this);
   panel->SetBackgroundColour(palette::face());

   splitter = new wxSplitterWindow(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxSP_LIVE_UPDATE | wxSP_3DSASH);
   splitter->SetMinimumPaneSize(60);
   splitter->SetSashGravity(0.5);

   wxBoxSizer *maps_header, *models_header;
   wxPanel *maps_pane   = pane(splitter, "Maps", map_list, maps_header);
   wxPanel *models_pane = pane(splitter, "Molecules", model_list, models_header);

   BevelButton *all_maps = new BevelButton(maps_pane, wxID_ANY, "All");
   all_maps->set_toggle(true);
   all_maps->set_on(true);
   all_maps->Bind(wxEVT_TOGGLEBUTTON, [](wxCommandEvent &e) {
      bandicoot_core_set_all_maps_displayed(e.GetInt() != 0);
   });
   maps_header->Add(all_maps, 0, wxALIGN_CENTER_VERTICAL);

   BevelButton *all_models = new BevelButton(models_pane, wxID_ANY, "All");
   all_models->set_toggle(true);
   all_models->set_on(true);
   all_models->Bind(wxEVT_TOGGLEBUTTON, [](wxCommandEvent &e) {
      bandicoot_core_set_all_models_displayed(e.GetInt() != 0);
   });
   BevelButton *last_only = new BevelButton(models_pane, wxID_ANY, "Last Only");
   last_only->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_display_last_model_only(); });
   models_header->Add(all_models, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
   models_header->Add(last_only, 0, wxALIGN_CENTER_VERTICAL);

   splitter->SplitHorizontally(maps_pane, models_pane);

   BevelButton *close_button = new BevelButton(panel, wxID_ANY, "Close");
   close_button->set_icon(icon("stock-menu-close.png", wxSize(16, 16)));
   close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { close(); });
   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { close(); });

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(splitter, 1, wxEXPAND | wxALL, 6);
   col->Add(close_button, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
   panel->SetSizer(col);

   // a representation changed from the keyboard or a script has no notice;
   // coming back to the window catches it
   Bind(wxEVT_ACTIVATE, [this](wxActivateEvent &e) {
      if (e.GetActive()) state_changed();
      e.Skip();
   });
}

// A titled header row over a scrolling list of rows.
wxPanel *DisplayManager::pane(wxWindow *parent, const wxString &title, RowList *&list,
                              wxBoxSizer *&header) {
   wxPanel *p = new wxPanel(parent);
   p->SetBackgroundColour(palette::face());
   header = new wxBoxSizer(wxHORIZONTAL);
   wxStaticText *label = new wxStaticText(p, wxID_ANY, title);
   label->SetMinSize(wxSize(80, -1));
   header->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
   list = new RowList(p);
   list->SetWindowStyleFlag(list->GetWindowStyleFlag() | wxBORDER_SUNKEN);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(header, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);
   col->Add(list, 1, wxEXPAND);
   p->SetSizer(col);
   return p;
}

void DisplayManager::open() {
   if (! IsShown()) {
      bandicoot_dm_geometry g;
      if (bandicoot_core_dm_geometry(g)) {
         if (g.width > 0 && g.height > 0) SetSize(g.width, g.height);
         Move(g.x, g.y);
      } else {
         CentreOnParent();
      }
      Show();
      if (g.split > 0) splitter->SetSashPosition(g.split);
      rebuild();
   }
   Raise();
}

void DisplayManager::molecules_changed() {
   if (! IsShown() || rebuild_pending) return;
   rebuild_pending = true;
   CallAfter([this]() { rebuild_pending = false; rebuild(); });
}

void DisplayManager::state_changed() {
   if (! IsShown() || refresh_pending) return;
   refresh_pending = true;
   CallAfter([this]() { refresh_pending = false; refresh(); });
}

void DisplayManager::rebuild() {
   map_list->clear();
   model_list->clear();
   map_rows.clear();
   model_rows.clear();
   int scroll_map = bandicoot_core_scroll_map();
   for (const bandicoot_dm_molecule &m : bandicoot_core_dm_maps())
      add_map_row(m, scroll_map);
   std::vector<wxString> render_as;
   for (const std::string &s : bandicoot_core_render_as_labels())
      render_as.push_back(wxString::FromUTF8(s));
   for (const bandicoot_dm_molecule &m : bandicoot_core_dm_models())
      add_model_row(m, render_as);
}

// Brings every row's controls up to date without rebuilding them.
void DisplayManager::refresh() {
   std::vector<bandicoot_dm_molecule> maps = bandicoot_core_dm_maps();
   std::vector<bandicoot_dm_molecule> models = bandicoot_core_dm_models();
   bool same = maps.size() == map_rows.size() && models.size() == model_rows.size();
   for (size_t i = 0; same && i < models.size(); i++)
      same = models[i].add_reps.size() == model_rows[i].add_reps.size();
   if (! same) {
      rebuild();
      return;
   }
   int scroll_map = bandicoot_core_scroll_map();
   for (size_t i = 0; i < maps.size(); i++) {
      MapRow &r = map_rows[i];
      r.name->ChangeValue(wxString::FromUTF8(maps[i].name));
      r.display->SetValue(maps[i].displayed);
      show_check_state(r.display);
      r.scroll->SetValue(maps[i].imol == scroll_map);
   }
   for (size_t i = 0; i < models.size(); i++) {
      ModelRow &r = model_rows[i];
      r.name->ChangeValue(wxString::FromUTF8(models[i].name));
      r.display->SetValue(models[i].displayed);
      r.active->SetValue(models[i].active);
      show_check_state(r.display);
      show_check_state(r.active);
      r.render_as->SetSelection(models[i].render_as >= 0 ? models[i].render_as : wxNOT_FOUND);
      for (size_t j = 0; j < r.add_reps.size(); j++) {
         r.add_reps[j]->SetValue(models[i].add_reps[j].second);
         show_check_state(r.add_reps[j]);
      }
   }
}

void DisplayManager::add_map_row(const bandicoot_dm_molecule &m, int scroll_map) {
   MixedRow *row = new MixedRow(map_list, true);
   row->add_label(wxString::Format("%d", m.imol));
   MapRow r;
   r.imol = m.imol;
   r.name = row->add_text(wxString::FromUTF8(m.name), map_name_chars);
   row->add_stretch();
   r.display = coloured_check(row, "Display", m.displayed);
   // each row's button is alone in its group; the window keeps one chosen
   r.scroll = row->add_radio("Scroll", true);
   r.scroll->SetValue(m.imol == scroll_map);
   BevelButton *properties = row->add_button("Properties");
   BevelButton *remove = row->add_button("Delete Map");

   int imol = m.imol;
   r.display->Bind(wxEVT_CHECKBOX, [imol](wxCommandEvent &e) {
      show_check_state(static_cast<wxCheckBox *>(e.GetEventObject()));
      bandicoot_core_set_map_displayed(imol, e.IsChecked());
   });
   // one Scroll map at a time, across the rows
   r.scroll->Bind(wxEVT_RADIOBUTTON, [this, imol](wxCommandEvent &) {
      for (MapRow &other : map_rows) other.scroll->SetValue(other.imol == imol);
      bandicoot_core_set_scroll_map(imol);
   });
   properties->Bind(wxEVT_BUTTON, [this, imol](wxCommandEvent &) { show_map_properties(this, imol); });
   remove->Bind(wxEVT_BUTTON, [imol](wxCommandEvent &) { bandicoot_core_close_molecule(imol); });

   map_list->add_row(row);
   map_rows.push_back(r);
}

void DisplayManager::add_model_row(const bandicoot_dm_molecule &m,
                                   const std::vector<wxString> &render_as) {
   MixedRow *row = new MixedRow(model_list, true);
   row->add_label(wxString::Format("%d", m.imol));
   ModelRow r;
   r.imol = m.imol;
   r.name = row->add_text(wxString::FromUTF8(m.name), model_name_chars);
   row->add_stretch();
   r.display = coloured_check(row, "Display", m.displayed);
   r.active  = coloured_check(row, "Active", m.active);
   r.render_as = row->add_choice(render_as, m.render_as);
   if (m.render_as < 0) r.render_as->SetSelection(wxNOT_FOUND);
   BevelButton *remove = row->add_button("Delete Model");

   int imol = m.imol;
   wxCheckBox *active = r.active;
   r.display->Bind(wxEVT_CHECKBOX, [imol, active](wxCommandEvent &e) {
      show_check_state(static_cast<wxCheckBox *>(e.GetEventObject()));
      // a hidden model is made inactive too, as the GTK window did
      active->SetValue(e.IsChecked());
      show_check_state(active);
      bandicoot_core_set_model_displayed(imol, e.IsChecked());
   });
   r.active->Bind(wxEVT_CHECKBOX, [imol](wxCommandEvent &e) {
      show_check_state(static_cast<wxCheckBox *>(e.GetEventObject()));
      bandicoot_core_set_model_active(imol, e.IsChecked());
   });
   r.render_as->Bind(wxEVT_CHOICE, [imol](wxCommandEvent &e) {
      bandicoot_core_set_render_as(imol, e.GetSelection());
   });
   remove->Bind(wxEVT_BUTTON, [imol](wxCommandEvent &) { bandicoot_core_close_molecule(imol); });

   model_list->add_row(row);
   if (! m.add_reps.empty()) model_list->add_row(add_reps_row(m, r));
   model_rows.push_back(r);
}

// Under a model with additional representations: one switch for them all,
// and one for each, as the GTK window has them.
wxWindow *DisplayManager::add_reps_row(const bandicoot_dm_molecule &m, ModelRow &r) {
   wxPanel *p = new wxPanel(model_list);
   p->SetBackgroundColour(palette::face());
   bool any_shown = false;
   for (const auto &rep : m.add_reps) any_shown = any_shown || rep.second;
   wxCheckBox *all = new wxCheckBox(p, wxID_ANY, "Show Additional Representations");
   all->SetValue(any_shown);
   show_check_state(all);
   wxStaticBoxSizer *box = new wxStaticBoxSizer(wxVERTICAL, p, "Additional Representations");
   int imol = m.imol;
   for (size_t i = 0; i < m.add_reps.size(); i++) {
      wxCheckBox *c = new wxCheckBox(box->GetStaticBox(), wxID_ANY, wxString::FromUTF8(m.add_reps[i].first));
      c->SetValue(m.add_reps[i].second);
      show_check_state(c);
      int index = int(i);
      c->Bind(wxEVT_CHECKBOX, [imol, index](wxCommandEvent &e) {
         show_check_state(static_cast<wxCheckBox *>(e.GetEventObject()));
         bandicoot_core_set_additional_representation_shown(imol, index, e.IsChecked());
      });
      box->Add(c, 0, wxALL, 2);
      r.add_reps.push_back(c);
   }
   all->Bind(wxEVT_CHECKBOX, [this, imol](wxCommandEvent &e) {
      show_check_state(static_cast<wxCheckBox *>(e.GetEventObject()));
      bandicoot_core_set_all_additional_representations_shown(imol, e.IsChecked());
      state_changed();
   });
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(all, 0, wxLEFT | wxTOP, 8);
   col->Add(box, 0, wxEXPAND | wxALL, 6);
   p->SetSizer(col);
   return p;
}

void DisplayManager::close() {
   bandicoot_dm_geometry g;
   wxPoint p = GetPosition();
   wxSize s = GetSize();
   g.x = p.x;
   g.y = p.y;
   g.width = s.x;
   g.height = s.y;
   g.split = splitter->GetSashPosition();
   bandicoot_core_save_dm_geometry(g);
   Hide();
}

DisplayManager *window = nullptr;

} // namespace

void install_display_manager(wxWindow *parent) {
   window = new DisplayManager(parent);
   bandicoot_dm_view v;
   v.open = []() { window->open(); return true; };
   v.molecules_changed = []() { window->molecules_changed(); };
   v.state_changed = []() { window->state_changed(); };
   bandicoot_core_set_dm_view(v);
}

void show_display_manager() {
   if (window) window->open();
}

} // namespace wxui

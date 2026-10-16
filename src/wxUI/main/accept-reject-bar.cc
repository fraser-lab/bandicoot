// src/wxUI/main/accept-reject-bar.cc

#include "wxUI/main/accept-reject-bar.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"

#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <cstdio>
#include <utility>

namespace wxui {

// One light: a box filled with its colour, the name small over the value.
class LightTile : public wxPanel {
public:
   explicit LightTile(wxWindow *parent)
      : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(58, 38)) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) { paint(); });
   }
   void set(const wxString &n, const wxString &v, const wxColour &c, const wxString &tip) {
      name = n;
      value = v;
      colour = c;
      SetToolTip(tip);
      Refresh();
   }
private:
   wxString name, value;
   wxColour colour = palette::face();

   void paint() {
      wxPaintDC dc(this);
      wxRect r = GetClientRect();
      dc.SetPen(wxPen(wxColour(0x4d, 0x4d, 0x4d)));
      dc.SetBrush(wxBrush(colour));
      dc.DrawRectangle(r);
      // dark text on light colours, light text on dark ones
      double lum = (0.299 * colour.Red() + 0.587 * colour.Green() + 0.114 * colour.Blue()) / 255.0;
      dc.SetTextForeground(lum < 0.5 ? *wxWHITE : *wxBLACK);
      wxFont small = GetFont();
      small.SetPointSize(9);
      dc.SetFont(small);
      wxSize ns = dc.GetTextExtent(name);
      dc.DrawText(name, (r.width - ns.x) / 2, r.height / 2 - ns.y - 1);
      wxFont big = GetFont().Bold();
      big.SetPointSize(14);
      dc.SetFont(big);
      wxSize vs = dc.GetTextExtent(value);
      dc.DrawText(value, (r.width - vs.x) / 2, r.height / 2 - 1);
   }
};

namespace {

const int max_lights = 8;
const wxColour warning_red(0xff, 0x00, 0x00);

enum { ID_ALWAYS_SHOW = wxID_HIGHEST + 1, ID_ALWAYS_HIDE, ID_UNDOCK,
       ID_PULL_CLEAR, ID_PULL_AUTO_CLEAR, ID_ACTIVE_TOOL };

// the floating window's Active Refinement tools
const std::pair<const char *, const char *> active_tools[] = {
   { "Flip This Peptide",           "on_accept_reject_flip_this_peptide_button_clicked" },
   { "Flip Next Peptide",           "on_accept_reject_flip_next_peptide_button_clicked" },
   { "Backrub Rotamer Fit",         "on_accept_reject_backrub_rotamer_button_clicked" },
   { "Side-chain Flip 180\xc2\xb0", "on_accept_reject_side_chain_180_flip_clicked" },  // degree sign, UTF-8
   { "Crankshaft Peptide Optimise", "on_accept_reject_crankshaft_button_clicked" }
};

} // namespace

AcceptRejectBar::AcceptRejectBar(wxWindow *parent, const Hooks &h)
   : wxPanel(parent), hooks(h) {

   SetBackgroundColour(palette::face());
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);

   for (int i = 0; i < max_lights; i++) {
      LightTile *t = new LightTile(this);
      lights.push_back(t);
      row->Add(t, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
   }

   reject_button = new BevelButton(this, wxID_ANY, "Reject");
   accept_button = new BevelButton(this, wxID_ANY, "Accept");
   reject_button->set_icon(icon("stock-button-cancel.png", wxSize(16, 16)));
   accept_button->set_icon(icon("stock-button-ok.png", wxSize(16, 16)));
   for (BevelButton *b : { reject_button, accept_button })
      b->SetMinSize(wxSize(b->GetBestSize().x, 38));
   reject_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reject(); });
   accept_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { accept(); });
   row->Add(reject_button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);
   row->Add(accept_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

   status_label = new wxStaticText(this, wxID_ANY, wxEmptyString);
   row->Add(status_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

   chiral_tile = new LightTile(this);
   cis_tile = new LightTile(this);
   row->Add(chiral_tile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
   row->Add(cis_tile, 0, wxALIGN_CENTER_VERTICAL);

   row->AddStretchSpacer();
   pull_button = new BevelButton(this, wxID_ANY, "Pull Restraint");
   pull_button->SetToolTip("Atom Pull Restraint");
   pull_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_pull_menu(); });
   active_button = new BevelButton(this, wxID_ANY, "Active Ref");
   active_button->SetToolTip("Active Refinement");
   active_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_active_menu(); });
   settings_button = new BevelButton(this, wxID_ANY, wxString::FromUTF8("\342\232\231")); // U+2699 gear
   settings_button->SetToolTip("Accept/Reject bar settings");
   settings_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_settings_menu(); });
   for (BevelButton *b : { pull_button, active_button, settings_button })
      b->SetMinSize(wxSize(b->GetBestSize().x, 38));
   row->Add(pull_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   row->Add(active_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   row->Add(settings_button, 0, wxALIGN_CENTER_VERTICAL);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(row, 1, wxEXPAND | wxALL, 4);
   SetSizer(outer);
   stand_down();
}

void AcceptRejectBar::present(bool auto_clear_atom_pull) {
   is_pending = true;
   auto_clear = auto_clear_atom_pull;
   for (BevelButton *b : { reject_button, accept_button, pull_button, active_button })
      b->Enable();
}

void AcceptRejectBar::set_status(const wxString &status) {
   status_label->SetLabel(status);
   Layout();
}

void AcceptRejectBar::set_lights(const std::vector<Light> &v) {
   if (v.empty()) return;
   for (size_t i = 0; i < lights.size(); i++) {
      if (i < v.size()) {
         char buf[32];
         snprintf(buf, sizeof buf, "%.2f", v[i].value);
         lights[i]->set(v[i].name, buf, v[i].colour, v[i].label);
         lights[i]->Show();
      } else {
         lights[i]->Hide();
      }
   }
   Layout();
}

// Coot's text puts the chiral-volume part first and starts the cis-peptide
// part at its first WARNING.
void AcceptRejectBar::set_warnings(const wxString &text) {
   wxString chirals, cis;
   if (text.Find("cis-peptide") != wxNOT_FOUND) {
      int w = text.Find("WARNING");
      cis = text.Mid(w == wxNOT_FOUND ? 0 : w).Strip(wxString::both);
      chirals = text.Left(w == wxNOT_FOUND ? 0 : w);
   } else {
      chirals = text;
   }
   chirals.Trim(true).Trim(false);
   if (chirals.Find("chiral") == wxNOT_FOUND) chirals.clear();
   chiral_tile->set("Warning", "Chiral", warning_red, chirals);
   cis_tile->set("Warning", "Cis", warning_red, cis);
   chiral_tile->Show(! chirals.empty());
   cis_tile->Show(! cis.empty());
   Layout();
}

void AcceptRejectBar::stand_down() {
   is_pending = false;
   for (LightTile *t : lights) t->Hide();
   set_status(wxEmptyString);
   set_warnings(wxEmptyString);
   for (BevelButton *b : { reject_button, accept_button, pull_button, active_button })
      b->Disable();
   Layout();
}

void AcceptRejectBar::accept() {
   finish("on_accept_reject_refinement_accept_button_clicked");
}

void AcceptRejectBar::reject() {
   finish("on_accept_reject_refinement_reject_button_clicked");
}

// Idle before the action runs: accepting can start work that presents a
// new fit.
void AcceptRejectBar::finish(const std::string &action) {
   if (! is_pending) return;
   stand_down();
   if (hooks.on_idle) hooks.on_idle();
   if (hooks.on_action) hooks.on_action(action, true);
}

void AcceptRejectBar::show_pull_menu() {
   wxMenu menu;
   menu.Append(ID_PULL_CLEAR, "Clear Atom Pull Restraint");
   menu.AppendCheckItem(ID_PULL_AUTO_CLEAR, "Auto-clear")->Check(auto_clear);
   menu.Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
      if (! hooks.on_action) return;
      if (e.GetId() == ID_PULL_CLEAR) {
         hooks.on_action("on_accept_reject_atom_pull_clear_button_clicked", true);
      } else if (e.GetId() == ID_PULL_AUTO_CLEAR) {
         auto_clear = e.IsChecked();
         hooks.on_action("on_accept_reject_refinement_atom_pull_autoclear_checkbutton_toggled",
                         auto_clear);
      }
   });
   pull_button->PopupMenu(&menu, wxPoint(0, pull_button->GetSize().y));
}

void AcceptRejectBar::show_active_menu() {
   wxMenu menu;
   int id = ID_ACTIVE_TOOL;
   for (const auto &t : active_tools)
      menu.Append(id++, wxString::FromUTF8(t.first));
   menu.Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
      int i = e.GetId() - ID_ACTIVE_TOOL;
      if (hooks.on_action && i >= 0 && i < int(sizeof active_tools / sizeof active_tools[0]))
         hooks.on_action(active_tools[i].second, true);
   });
   active_button->PopupMenu(&menu, wxPoint(0, active_button->GetSize().y));
}

void AcceptRejectBar::show_settings_menu() {
   wxMenu menu;
   bool always = hooks.always_shown ? hooks.always_shown() : true;
   menu.AppendRadioItem(ID_ALWAYS_SHOW, "Always Show")->Check(always);
   menu.AppendRadioItem(ID_ALWAYS_HIDE, "Always Hide")->Check(! always);
   menu.AppendSeparator();
   menu.Append(ID_UNDOCK, "Undock");
   menu.Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
      int id = e.GetId();
      // run once the menu has closed: these can hide the bar
      CallAfter([this, id]() {
         if (id == ID_UNDOCK) { if (hooks.undock) hooks.undock(); }
         else if (hooks.set_always_shown) hooks.set_always_shown(id == ID_ALWAYS_SHOW);
      });
   });
   settings_button->PopupMenu(&menu, wxPoint(0, settings_button->GetSize().y));
}

} // namespace wxui

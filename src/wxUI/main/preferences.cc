// src/wxUI/main/preferences.cc

#include "wxUI/main/preferences.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/assets/toolbar-button.hh"
#include "wxUI/dialogs/colour-panel.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcmemory.h>
#include <wx/dialog.h>
#include <wx/dirdlg.h>
#include <wx/notebook.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wxui {

namespace {

// A labelled frame of choices; the box is the parent of its contents.
wxStaticBoxSizer *frame(wxWindow *parent, const wxString &title) {
   return new wxStaticBoxSizer(wxVERTICAL, parent, title);
}

// A heading centred above a tab's content, as Coot's tabs have.
void heading(wxWindow *page, wxBoxSizer *col, const wxString &text) {
   col->Add(new wxStaticText(page, wxID_ANY, text), 0, wxALIGN_CENTER | wxBOTTOM, 10);
}

wxPanel *new_page(wxWindow *parent, wxBoxSizer *&col) {
   wxPanel *page = new wxPanel(parent);
   col = new wxBoxSizer(wxVERTICAL);
   wxBoxSizer *pad = new wxBoxSizer(wxVERTICAL);
   pad->Add(col, 1, wxEXPAND | wxALL, 12);
   page->SetSizer(pad);
   return page;
}

void on_choose(wxRadioButton *r, std::function<void()> f) {
   r->Bind(wxEVT_RADIOBUTTON, [f](wxCommandEvent &) { f(); });
}

// An angle slider in tenths of a degree, its value shown as Coot shows it.
class DegreeSlider {
public:
   DegreeSlider(wxWindow *parent, wxSizer *into, const wxString &label, double max_degrees,
                std::function<void(float)> changed) {
      slider = new StepSlider(parent, 0.0, max_degrees, 0.1, 0.0, 1);
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(parent, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      row->Add(slider, 1, wxALIGN_CENTER_VERTICAL);
      row->Add(new wxStaticText(parent, wxID_ANY, "degrees"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
      into->Add(row, 0, wxEXPAND | wxALL, 4);
      slider->on_change = [changed](double v) { changed(float(v)); };
   }

   void set(float degrees) { slider->set_value(degrees); }

private:
   StepSlider *slider;
};

// A framed Yes / No choice; set shows the setting, changed applies one.
struct YesNo {
   wxRadioButton *yes, *no;
   void set(bool on) { yes->SetValue(on); no->SetValue(! on); }
};

// The frame is returned through box_out for a caller that greys it.
YesNo yes_no(wxWindow *page, wxBoxSizer *col, const wxString &title, std::function<void(bool)> changed,
             const wxString &yes_label = "Yes", const wxString &no_label = "No",
             bool side_by_side = false, wxStaticBox **box_out = nullptr) {
   wxStaticBoxSizer *box = frame(page, title);
   wxWindow *b = box->GetStaticBox();
   YesNo yn;
   yn.yes = new wxRadioButton(b, wxID_ANY, yes_label, wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
   yn.no = new wxRadioButton(b, wxID_ANY, no_label);
   wxBoxSizer *choices = new wxBoxSizer(side_by_side ? wxHORIZONTAL : wxVERTICAL);
   choices->Add(yn.yes, 0, wxALL, 4);
   if (side_by_side) choices->AddSpacer(12);
   choices->Add(yn.no, 0, wxALL, 4);
   box->Add(choices, 0);
   col->Add(box, 0, wxEXPAND | wxBOTTOM, 10);
   on_choose(yn.yes, [changed]() { changed(true); });
   on_choose(yn.no, [changed]() { changed(false); });
   if (box_out) *box_out = box->GetStaticBox();
   return yn;
}

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

// The swatch beside an "Own colour" choice, holding that colour. Clicking it
// chooses own and opens the colour panel, which applies the colour as it
// moves; Cancel puts back the colour the panel opened on. The panel can
// outlast the page, so the page is only touched while the swatch exists.
wxBitmapButton *own_colour_swatch(wxWindow *parent, wxRadioButton *own, const char *title,
                                  std::shared_ptr<bandicoot_rgb> own_colour,
                                  std::function<bandicoot_rgb()> current,
                                  std::function<void(const bandicoot_rgb &)> apply,
                                  std::function<void()> refresh) {
   wxBitmapButton *pick = new wxBitmapButton(parent, wxID_ANY, swatch(*own_colour));
   auto alive = std::make_shared<bool>(true);
   pick->Bind(wxEVT_DESTROY, [alive](wxWindowDestroyEvent &e) { *alive = false; e.Skip(); });
   pick->Bind(wxEVT_BUTTON, [=](wxCommandEvent &) {
      bandicoot_rgb before = current();
      own->SetValue(true);
      apply(*own_colour);
      PanelColour start;
      start.r = own_colour->r; start.g = own_colour->g; start.b = own_colour->b;
      show_colour_panel(title, start,
                        [pick, own_colour, apply, alive](const PanelColour &p) {
                           own_colour->r = p.r; own_colour->g = p.g; own_colour->b = p.b;
                           if (*alive) pick->SetBitmap(swatch(*own_colour));
                           apply(*own_colour);
                        },
                        [before, apply, refresh, alive](bool kept) {
                           if (! kept) apply(before);
                           if (*alive) refresh();
                        });
   });
   return pick;
}

// A number entry that applies its value whenever the text reads as one in range.
wxTextCtrl *number_entry(wxWindow *parent, std::function<void(double)> apply, long style = 0) {
   wxTextCtrl *t = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(160, -1), style);
   t->Bind(wxEVT_TEXT, [t, apply](wxCommandEvent &) {
      double v;
      if (t->GetValue().ToCDouble(&v)) apply(v);
   });
   return t;
}

class PreferencesWindow : public wxDialog {
public:
   PreferencesWindow(wxWindow *parent, const PreferencesHooks &hooks)
      : wxDialog(parent, wxID_ANY, "Preferences", wxDefaultPosition, wxSize(940, 600),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        hooks(hooks) {
      SetBackgroundColour(palette::face());

      wxBoxSizer *body = new wxBoxSizer(wxHORIZONTAL);
      wxPanel *categories = new wxPanel(this);
      wxBoxSizer *cat_col = new wxBoxSizer(wxVERTICAL);
      categories->SetSizer(cat_col);
      body->Add(categories, 0, wxEXPAND | wxALL, 8);

      notebook = new wxNotebook(this, wxID_ANY);
      body->Add(notebook, 1, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, 8);

      // the categories whose tabs are not here yet stay greyed
      struct Category { const char *label, *icon; bool ready; };
      const Category cats[] = {
         { "General",  "preferences-general.svg", true },
         { "Bonds",    "preferences-bonds.svg",   true },
         { "Maps",     "preferences-maps.svg",    true },
         { "Colours",  "preferences-colours.svg", true },
         { "Others",   "preferences-others.svg",  true },
      };
      for (const Category &c : cats) {
         ToolbarButton *b = new ToolbarButton(categories, wxID_ANY, c.label,
                                              *c.icon ? icon(c.icon, wxSize(24, 24)) : wxBitmapBundle(),
                                              ToolbarButton::Content::IconAboveText);
         b->Enable(c.ready);
         cat_col->Add(b, 0, wxEXPAND | wxBOTTOM, 4);
         int index = int(category_buttons.size());
         b->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent &) { show_category(index); });
         category_buttons.push_back(b);
      }

      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      wxButton *reset = new wxButton(this, wxID_ANY, "Reset Defaults");
      wxButton *ok = new wxButton(this, wxID_OK, "OK");
      buttons->AddStretchSpacer();
      buttons->Add(reset, 0, wxRIGHT, 8);
      buttons->Add(ok, 0);

      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(body, 1, wxEXPAND);
      col->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
      SetSizer(col);

      reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reset_defaults(); });
      // OK saves; closing the window keeps the changes for this session only, as in Coot
      ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
         bandicoot_core_save_preferences();
         Destroy();
      });
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });

      show_category(0);
   }

   ~PreferencesWindow() override { shown = nullptr; }

   static PreferencesWindow *shown;

   // Every control shows the setting as it now is.
   void refresh() {
      for (auto &f : refreshers) f();
      Layout();
   }

private:
   PreferencesHooks hooks;
   wxNotebook *notebook;
   std::vector<ToolbarButton *> category_buttons;
   std::vector<std::function<void()>> refreshers;

   // A category's tabs are built afresh each time it is chosen, since Bond
   // Colours and Map Colours appear under two categories.
   void show_category(int index) {
      for (size_t i = 0; i < category_buttons.size(); i++) category_buttons[i]->set_on(int(i) == index);
      notebook->DeleteAllPages();
      refreshers.clear();
      std::vector<std::pair<wxPanel *, wxString>> pages;
      switch (index) {
      case 0:
         pages = { dialogs_page(), toolbar_page(), recentring_page(), smooth_page(), hid_page() };
         break;
      case 1: pages = { bond_parameters_page(), bond_colours_page(), cis_peptides_page() }; break;
      case 2: pages = { map_parameters_page(), map_colours_page(), map_drag_page() }; break;
      case 3: pages = { bond_colours_page(), map_colours_page(), background_page() }; break;
      case 4:
         // Coot's Tips tab is left out: no tips window opens in Bandicoot
         pages = { antialias_page(), fonts_page(), console_page(), pointer_page(), speed_page(),
                   pick_atom_page(), pandda_page(), ligands_page() };
         break;
      default: break;
      }
      for (auto &p : pages) notebook->AddPage(p.first, p.second);
      refresh();
   }

   // ---- General > Dialogs

   std::pair<wxPanel *, wxString> dialogs_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      wxStaticBoxSizer *ar = frame(page, "Dock Accept/Reject Dialog?");
      wxWindow *arb = ar->GetStaticBox();
      // created yes, no, then the pair under yes, so the two groups stay apart
      wxRadioButton *ar_yes = new wxRadioButton(arb, wxID_ANY, "Yes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *ar_no = new wxRadioButton(arb, wxID_ANY, "No");
      wxRadioButton *ar_show = new wxRadioButton(arb, wxID_ANY, "Always show?", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *ar_hide = new wxRadioButton(arb, wxID_ANY, "Always hide?");
      // nested pair in its own column so the indent stays horizontal only
      wxBoxSizer *ar_sub = new wxBoxSizer(wxVERTICAL);
      ar_sub->Add(ar_show, 0, wxALL, 4);
      ar_sub->Add(ar_hide, 0, wxALL, 4);
      ar->Add(ar_yes, 0, wxALL, 4);
      ar->Add(ar_sub, 0, wxLEFT, 28);
      ar->Add(ar_no, 0, wxALL, 4);
      col->Add(ar, 0, wxEXPAND | wxBOTTOM, 10);

      wxStaticBoxSizer *sv = frame(page, "Dock Sequence View Dialog?");
      wxWindow *svb = sv->GetStaticBox();
      wxRadioButton *sv_yes = new wxRadioButton(svb, wxID_ANY, "Yes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *sv_no = new wxRadioButton(svb, wxID_ANY, "No");
      sv->Add(sv_yes, 0, wxALL, 4);
      sv->Add(sv_no, 0, wxALL, 4);
      col->Add(sv, 0, wxEXPAND | wxBOTTOM, 10);

      wxStaticBoxSizer *rows = frame(page, "Max. sequences shown (docked)");
      wxWindow *rb = rows->GetStaticBox();
      wxSpinCtrl *max_rows = new wxSpinCtrl(rb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1),
                                            wxSP_ARROW_KEYS, 1, 30, 5);
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(max_rows, 0, wxALIGN_CENTER_VERTICAL);
      row->Add(new wxStaticText(rb, wxID_ANY, "sequences"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
      rows->Add(row, 0, wxALL, 4);
      col->Add(rows, 0, wxEXPAND);

      auto sync_ar = [this, ar, ar_sub, ar_yes]() {
         ar->Show(ar_sub, ar_yes->GetValue());
         Layout();
      };
      on_choose(ar_yes, [sync_ar]() { bandicoot_core_set_ar_docked(true); sync_ar(); });
      on_choose(ar_no, [sync_ar]() {
         bandicoot_core_set_ar_docked(false);
         bandicoot_core_set_ar_always_shown(false);
         sync_ar();
      });
      on_choose(ar_show, []() { bandicoot_core_set_ar_always_shown(true); });
      on_choose(ar_hide, []() { bandicoot_core_set_ar_always_shown(false); });
      on_choose(sv_yes, [this]() {
         bandicoot_core_save_sequence_view_docked(true);
         if (hooks.sequence_view_docking_changed) hooks.sequence_view_docking_changed(true);
      });
      on_choose(sv_no, [this]() {
         bandicoot_core_save_sequence_view_docked(false);
         if (hooks.sequence_view_docking_changed) hooks.sequence_view_docking_changed(false);
      });
      max_rows->Bind(wxEVT_SPINCTRL, [this, max_rows](wxSpinEvent &) {
         bandicoot_core_save_sequence_view_max_chains(max_rows->GetValue());
         if (hooks.sequence_view_rows_changed) hooks.sequence_view_rows_changed();
      });

      refreshers.push_back([=, this]() {
         bool docked = bandicoot_core_ar_docked();
         ar_yes->SetValue(docked);
         ar_no->SetValue(! docked);
         bool always = bandicoot_core_ar_always_shown();
         ar_show->SetValue(always);
         ar_hide->SetValue(! always);
         sync_ar();
         bool sv_docked = bandicoot_core_sequence_view_docked();
         sv_yes->SetValue(sv_docked);
         sv_no->SetValue(! sv_docked);
         max_rows->SetValue(bandicoot_core_sequence_view_max_chains());
      });
      return { page, "Dialogs" };
   }

   // ---- General > Refinement Toolbar (the sidebar)

   std::pair<wxPanel *, wxString> toolbar_page() {
      wxPanel *page = new wxPanel(notebook);
      wxBoxSizer *cols = new wxBoxSizer(wxHORIZONTAL);
      wxBoxSizer *left = new wxBoxSizer(wxVERTICAL);
      wxBoxSizer *right = new wxBoxSizer(wxVERTICAL);
      cols->Add(left, 1, wxEXPAND | wxALL, 12);
      cols->Add(right, 1, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, 12);
      page->SetSizer(cols);

      // Yes opens the choice of side; No floats the sidebar in a window
      wxStaticBoxSizer *dock = frame(page, "Dock Toolbar?");
      wxWindow *db = dock->GetStaticBox();
      wxRadioButton *dock_yes = new wxRadioButton(db, wxID_ANY, "Yes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *dock_no = new wxRadioButton(db, wxID_ANY, "No");
      wxRadioButton *side_right = new wxRadioButton(db, wxID_ANY, "Right", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *side_left = new wxRadioButton(db, wxID_ANY, "Left");
      wxRadioButton *side_bottom = new wxRadioButton(db, wxID_ANY, "Bottom");
      wxBoxSizer *side_col = new wxBoxSizer(wxVERTICAL);
      for (wxRadioButton *r : { side_right, side_left, side_bottom }) side_col->Add(r, 0, wxALL, 4);
      dock->Add(dock_yes, 0, wxALL, 4);
      dock->Add(side_col, 0, wxLEFT, 28);
      dock->Add(dock_no, 0, wxALL, 4);
      left->Add(dock, 0, wxEXPAND | wxBOTTOM, 10);

      wxStaticBoxSizer *style = frame(page, "Toolbar Style");
      wxWindow *sb = style->GetStaticBox();
      wxRadioButton *style_icons = new wxRadioButton(sb, wxID_ANY, "Icons only (default)", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *style_both = new wxRadioButton(sb, wxID_ANY, "Icons and Text");
      wxRadioButton *style_text = new wxRadioButton(sb, wxID_ANY, "Text only");
      for (wxRadioButton *r : { style_icons, style_both, style_text }) style->Add(r, 0, wxALL, 4);
      left->Add(style, 0, wxEXPAND | wxBOTTOM, 10);

      wxStaticBoxSizer *size = frame(page, "Icon Size");
      wxWindow *zb = size->GetStaticBox();
      const int sizes[] = { 24, 20, 16, 12 };
      std::vector<wxRadioButton *> size_radios;
      for (int i = 0; i < 4; i++) {
         wxString label = wxString::Format("%d pt", sizes[i]);
         if (sizes[i] == 12) label += " (default)";
         wxRadioButton *r = new wxRadioButton(zb, wxID_ANY, label, wxDefaultPosition, wxDefaultSize,
                                              i == 0 ? wxRB_GROUP : 0);
         size->Add(r, 0, wxALL, 4);
         size_radios.push_back(r);
         int points = sizes[i];
         on_choose(r, [this, points]() {
            if (hooks.set_sidebar_icon_size) hooks.set_sidebar_icon_size(points);
         });
      }
      left->Add(size, 0, wxEXPAND);

      // which tools the sidebar shows
      wxStaticBoxSizer *content = frame(page, "Toolbar Content");
      wxWindow *cb = content->GetStaticBox();
      wxScrolledWindow *list = new wxScrolledWindow(cb, wxID_ANY, wxDefaultPosition, wxSize(-1, 260),
                                                    wxVSCROLL | wxBORDER_THEME);
      list->SetBackgroundColour(*wxWHITE);
      list->SetScrollRate(0, 10);
      wxBoxSizer *items = new wxBoxSizer(wxVERTICAL);
      std::vector<wxCheckBox *> boxes;
      std::vector<bandicoot_sidebar_icon> icons = bandicoot_core_sidebar_icons();
      for (size_t i = 0; i < icons.size(); i++) {
         wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
         wxCheckBox *box = new wxCheckBox(list, wxID_ANY, wxEmptyString);
         row->Add(box, 0, wxALIGN_CENTER_VERTICAL);
         wxBitmapBundle b = icons[i].icon_file.empty() ? wxBitmapBundle()
                                                       : icon(wxString::FromUTF8(icons[i].icon_file), wxSize(20, 20));
         if (b.IsOk())
            row->Add(new wxStaticBitmap(list, wxID_ANY, b), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
         else
            row->AddSpacer(26);
         row->Add(new wxStaticText(list, wxID_ANY, wxString::FromUTF8(icons[i].label)), 0,
                  wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
         items->Add(row, 0, wxALL, 4);
         int pos = int(i);
         box->Bind(wxEVT_CHECKBOX, [box, pos](wxCommandEvent &) {
            bandicoot_core_set_sidebar_icon_shown(pos, box->IsChecked());
         });
         boxes.push_back(box);
      }
      list->SetSizer(items);
      content->Add(list, 1, wxEXPAND | wxBOTTOM, 6);
      content->Add(new wxStaticText(cb, wxID_ANY, "Shortcut a selection?"), 0, wxALIGN_CENTER | wxBOTTOM, 4);
      wxButton *show_default = new wxButton(cb, wxID_ANY, "Show default icons (BKP setting)");
      wxButton *show_all = new wxButton(cb, wxID_ANY, "Show all icons");
      content->Add(show_default, 0, wxEXPAND | wxBOTTOM, 4);
      content->Add(show_all, 0, wxEXPAND);
      right->Add(content, 1, wxEXPAND);

      auto refresh_icons = [boxes]() {
         std::vector<bandicoot_sidebar_icon> now = bandicoot_core_sidebar_icons();
         for (size_t i = 0; i < boxes.size() && i < now.size(); i++) boxes[i]->SetValue(now[i].shown);
      };
      show_default->Bind(wxEVT_BUTTON, [refresh_icons](wxCommandEvent &) {
         bandicoot_core_sidebar_show_icon_set(true);
         refresh_icons();
      });
      show_all->Bind(wxEVT_BUTTON, [refresh_icons](wxCommandEvent &) {
         bandicoot_core_sidebar_show_icon_set(false);
         refresh_icons();
      });

      auto sync_dock = [this, dock, side_col, dock_yes]() {
         dock->Show(side_col, dock_yes->GetValue());
         Layout();
      };
      on_choose(dock_yes, [this, sync_dock]() {
         if (hooks.set_sidebar_docked) hooks.set_sidebar_docked(true);
         sync_dock();
      });
      on_choose(dock_no, [this, sync_dock]() {
         if (hooks.set_sidebar_docked) hooks.set_sidebar_docked(false);
         sync_dock();
      });
      const std::pair<wxRadioButton *, int> sides[] = { { side_right, 0 }, { side_left, 1 }, { side_bottom, 3 } };
      for (const auto &s : sides) {
         int side = s.second;
         on_choose(s.first, [this, side]() { if (hooks.set_sidebar_side) hooks.set_sidebar_side(side); });
      }
      on_choose(style_icons, []() { bandicoot_core_set_sidebar_style(1); });
      on_choose(style_both, []() { bandicoot_core_set_sidebar_style(2); });
      on_choose(style_text, []() { bandicoot_core_set_sidebar_style(3); });

      refreshers.push_back([=, this]() {
         bool docked = hooks.sidebar_docked ? hooks.sidebar_docked() : true;
         dock_yes->SetValue(docked);
         dock_no->SetValue(! docked);
         int side = hooks.sidebar_side ? hooks.sidebar_side() : 0;
         side_right->SetValue(side == 0);
         side_left->SetValue(side == 1);
         side_bottom->SetValue(side == 3);
         sync_dock();
         int st = bandicoot_core_sidebar_style();
         style_icons->SetValue(st <= 1);
         style_both->SetValue(st == 2);
         style_text->SetValue(st == 3);
         int points = bandicoot_core_sidebar_icon_size();
         for (int i = 0; i < 4; i++) size_radios[i]->SetValue(sizes[i] == points);
         refresh_icons();
      });
      return { page, "Refinement Toolbar" };
   }

   // ---- General > Coordinates Recentering

   std::pair<wxPanel *, wxString> recentring_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Recentre on read new PDB?");
      wxStaticBoxSizer *box = frame(page, wxEmptyString);
      wxWindow *b = box->GetStaticBox();
      wxRadioButton *yes = new wxRadioButton(b, wxID_ANY, "Yes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *no = new wxRadioButton(b, wxID_ANY, "No");
      box->Add(yes, 0, wxALL, 4);
      box->Add(no, 0, wxALL, 4);
      col->Add(box, 0, wxEXPAND);
      on_choose(yes, []() { bandicoot_core_set_recentre_on_read(true); });
      on_choose(no, []() { bandicoot_core_set_recentre_on_read(false); });
      refreshers.push_back([yes, no]() {
         bool on = bandicoot_core_recentre_on_read();
         yes->SetValue(on);
         no->SetValue(! on);
      });
      return { page, "Coordinates Recentering" };
   }

   // ---- General > Smooth Recentering

   std::pair<wxPanel *, wxString> smooth_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Recentering Smooth Scrolling");
      wxStaticBoxSizer *box = frame(page, wxEmptyString);
      wxWindow *b = box->GetStaticBox();
      wxRadioButton *yes = new wxRadioButton(b, wxID_ANY, "Yes", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *no = new wxRadioButton(b, wxID_ANY, "No");
      box->Add(yes, 0, wxALL, 4);
      box->Add(no, 0, wxALL, 4);
      col->Add(box, 0, wxEXPAND | wxBOTTOM, 10);

      wxFlexGridSizer *grid = new wxFlexGridSizer(3, 8, 8);
      wxTextCtrl *steps = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(160, -1));
      wxTextCtrl *limit = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(160, -1));
      grid->Add(new wxStaticText(page, wxID_ANY, "Number of Steps:"), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(steps, 0);
      grid->AddSpacer(0);
      grid->Add(new wxStaticText(page, wxID_ANY, "Do Not Scroll for\ndistances more than:"), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(limit, 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(new wxStaticText(page, wxID_ANY, "A"), 0, wxALIGN_CENTER_VERTICAL);
      col->Add(grid, 0);

      on_choose(yes, []() { bandicoot_core_set_smooth_scroll(true); });
      on_choose(no, []() { bandicoot_core_set_smooth_scroll(false); });
      // a value applies once it reads as one, as in Coot
      steps->Bind(wxEVT_TEXT, [steps](wxCommandEvent &) {
         long v;
         if (steps->GetValue().ToLong(&v) && v > 0 && v < 10000000) bandicoot_core_set_smooth_scroll_steps(int(v));
      });
      limit->Bind(wxEVT_TEXT, [limit](wxCommandEvent &) {
         double v;
         if (limit->GetValue().ToCDouble(&v) && v > 0 && v < 1000) bandicoot_core_set_smooth_scroll_limit(float(v));
      });
      refreshers.push_back([yes, no, steps, limit]() {
         bool on = bandicoot_core_smooth_scroll();
         yes->SetValue(on);
         no->SetValue(! on);
         steps->ChangeValue(wxString::Format("%d", bandicoot_core_smooth_scroll_steps()));
         limit->ChangeValue(wxString::Format("%.2f", bandicoot_core_smooth_scroll_limit()));
      });
      return { page, "Smooth Recentering" };
   }

   // ---- General > HID

   std::pair<wxPanel *, wxString> hid_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Virtual trackball");
      wxStaticBoxSizer *box = frame(page, wxEmptyString);
      wxWindow *b = box->GetStaticBox();
      wxRadioButton *sphere = new wxRadioButton(b, wxID_ANY, "Spherical Surface", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *flat = new wxRadioButton(b, wxID_ANY, "Turntable Mode (Flat)");
      box->Add(sphere, 0, wxALL, 4);
      box->Add(flat, 0, wxALL, 4);
      col->Add(box, 0, wxEXPAND);
      on_choose(sphere, []() { bandicoot_core_set_flat_trackball(false); });
      on_choose(flat, []() { bandicoot_core_set_flat_trackball(true); });
      refreshers.push_back([sphere, flat]() {
         bool f = bandicoot_core_flat_trackball();
         sphere->SetValue(! f);
         flat->SetValue(f);
      });
      return { page, "HID" };
   }

   // ---- Bonds > Bond Parameters

   std::pair<wxPanel *, wxString> bond_parameters_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      // a molecule takes this width when it is read; Edit > Bond Parameters
      // changes one already loaded
      wxStaticBoxSizer *width = frame(page, "Bond Width for New Molecules:");
      wxWindow *wb = width->GetStaticBox();
      wxChoice *pixels = new wxChoice(wb, wxID_ANY);
      for (int i = 1; i <= 20; i++) pixels->Append(wxString::Format("%d", i));
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(pixels, 0, wxALIGN_CENTER_VERTICAL);
      row->Add(new wxStaticText(wb, wxID_ANY, "pixels"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      width->Add(row, 0, wxALL, 4);
      col->Add(width, 0, wxEXPAND | wxBOTTOM, 10);
      pixels->Bind(wxEVT_CHOICE, [pixels](wxCommandEvent &) {
         bandicoot_core_set_default_bond_width(pixels->GetSelection() + 1);
      });

      YesNo occupancy = yes_no(page, col, "Scale Bond Width to Occupancy",
                               [](bool on) { bandicoot_core_set_scale_bond_width_to_occupancy(on); });

      refreshers.push_back([pixels, occupancy]() mutable {
         bandicoot_display_preferences p = bandicoot_core_display_preferences();
         pixels->SetSelection(std::clamp(p.bond_width, 1, 20) - 1);
         occupancy.set(p.scale_bond_width_to_occupancy);
      });
      return { page, "Bond Parameters" };
   }

   // ---- Bonds (and Colours) > Bond Colours

   std::pair<wxPanel *, wxString> bond_colours_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      wxStaticBoxSizer *scheme = frame(page, "Default Bond Display Scheme");
      wxWindow *sb = scheme->GetStaticBox();
      wxChoice *schemes = new wxChoice(sb, wxID_ANY);
      for (const std::string &s : bandicoot_core_bond_scheme_labels()) schemes->Append(wxString::FromUTF8(s));
      scheme->Add(schemes, 0, wxALL, 4);
      col->Add(scheme, 0, wxEXPAND | wxBOTTOM, 10);
      schemes->Bind(wxEVT_CHOICE, [schemes](wxCommandEvent &) {
         bandicoot_core_set_default_bond_scheme(schemes->GetSelection());
      });

      wxStaticBoxSizer *rotation = frame(page, "Colour Map Rotation [Molecule Independent]");
      wxWindow *rb = rotation->GetStaticBox();
      rotation->Add(new wxStaticText(rb, wxID_ANY, "New molecule rotates colour map"), 0, wxALIGN_CENTER | wxTOP, 4);
      auto turn = std::make_shared<DegreeSlider>(rb, rotation, "by", 360.0,
                                                 [](float d) { bandicoot_core_set_bond_colour_rotation_on_read(d); });
      col->Add(rotation, 0, wxEXPAND | wxBOTTOM, 10);

      wxStaticBoxSizer *carbons_box = frame(page, wxEmptyString);
      wxCheckBox *carbons = new wxCheckBox(carbons_box->GetStaticBox(), wxID_ANY, "Change colour for Carbons only");
      carbons_box->Add(carbons, 0, wxALL, 4);
      col->Add(carbons_box, 0, wxEXPAND | wxBOTTOM, 10);
      carbons->Bind(wxEVT_CHECKBOX, [carbons](wxCommandEvent &) {
         bandicoot_core_set_carbons_only_preference(carbons->IsChecked());
      });

      wxStaticBoxSizer *altloc = frame(page, "Alt. Conf. Colour Scheme");
      wxWindow *ab = altloc->GetStaticBox();
      auto offset = std::make_shared<DegreeSlider>(ab, altloc, "Conf. A colour offset", 180.0,
                                                   [](float d) { bandicoot_core_set_altloc_conf_a_offset(d); });
      auto difference = std::make_shared<DegreeSlider>(ab, altloc, "Alt. Conf. colour difference", 180.0,
                                                       [](float d) { bandicoot_core_set_altloc_colour_difference(d); });
      col->Add(altloc, 0, wxEXPAND);

      refreshers.push_back([schemes, turn, carbons, offset, difference]() {
         bandicoot_display_preferences p = bandicoot_core_display_preferences();
         schemes->SetSelection(p.bond_scheme);   // -1 shows no choice
         turn->set(p.bond_colour_rotation);
         carbons->SetValue(p.carbons_only);
         offset->set(p.altloc_conf_a_offset);
         difference->set(p.altloc_colour_difference);
      });
      return { page, "Bond Colours" };
   }

   // ---- Maps > Map Parameters

   std::pair<wxPanel *, wxString> map_parameters_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Density Settings");

      // one framed row per setting, as Coot lays them out
      auto entry_row = [page, col](const wxString &label, const wxString &unit,
                                   std::function<void(double)> apply, long style = 0) {
         wxStaticBoxSizer *box = frame(page, wxEmptyString);
         wxWindow *b = box->GetStaticBox();
         wxTextCtrl *t = number_entry(b, apply, style);
         wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
         row->Add(new wxStaticText(b, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
         row->Add(t, 0, wxALIGN_CENTER_VERTICAL);
         if (! unit.empty())
            row->Add(new wxStaticText(b, wxID_ANY, unit), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
         box->Add(row, 0, wxALL, 6);
         col->Add(box, 0, wxEXPAND | wxBOTTOM, 8);
         return t;
      };
      // the limits are Coot's: a radius typed in up to 200 A applies at
      // once; Return takes one up to 1000 A
      wxTextCtrl *radius = entry_row("Map Radius:", wxString::FromUTF8("\xc3\x85ngstr\xc3\xb6m"), [](double v) {
         if (v > 0 && v < 200) bandicoot_core_set_map_radius_preference(float(v));
      }, wxTE_PROCESS_ENTER);
      radius->Bind(wxEVT_TEXT_ENTER, [radius](wxCommandEvent &) {
         double v;
         if (radius->GetValue().ToCDouble(&v) && v > 0 && v < 1000) bandicoot_core_set_map_radius_preference(float(v));
      });
      wxTextCtrl *increment = entry_row("Increment Size", "e/A^3", [](double v) {
         if (v > 0) bandicoot_core_set_map_increment_preference(float(v));
      });
      wxTextCtrl *diff_increment = entry_row("Diff Map Increment", "e/A^3", [](double v) {
         if (v > 0) bandicoot_core_set_diff_map_increment_preference(float(v));
      });
      wxTextCtrl *sampling = entry_row("Sampling Rate:", wxEmptyString, [](double v) {
         if (v > 1 && v < 100) bandicoot_core_set_map_sampling_rate_preference(float(v));
      });

      wxStaticBoxSizer *dynamic = frame(page, wxEmptyString);
      wxWindow *db = dynamic->GetStaticBox();
      wxCheckBox *dyn_sampling = new wxCheckBox(db, wxID_ANY, "Dynamic Map Sampling");
      wxCheckBox *dyn_size = new wxCheckBox(db, wxID_ANY, "Dynamic Map Display Size");
      dynamic->Add(dyn_sampling, 0, wxALL, 4);
      dynamic->Add(dyn_size, 0, wxALL, 4);
      col->Add(dynamic, 0, wxEXPAND);
      dyn_sampling->Bind(wxEVT_CHECKBOX, [dyn_sampling](wxCommandEvent &) {
         bandicoot_core_set_dynamic_map_sampling_preference(dyn_sampling->IsChecked());
      });
      dyn_size->Bind(wxEVT_CHECKBOX, [dyn_size](wxCommandEvent &) {
         bandicoot_core_set_dynamic_map_size_preference(dyn_size->IsChecked());
      });

      refreshers.push_back([=]() {
         bandicoot_display_preferences p = bandicoot_core_display_preferences();
         radius->ChangeValue(wxString::Format("%.2f", p.map_radius));
         increment->ChangeValue(wxString::Format("%.4f", p.map_increment));
         diff_increment->ChangeValue(wxString::Format("%.4f", p.diff_map_increment));
         sampling->ChangeValue(wxString::Format("%.4f", p.map_sampling_rate));
         dyn_sampling->SetValue(p.dynamic_map_sampling);
         dyn_size->SetValue(p.dynamic_map_size);
      });
      return { page, "Map Parameters" };
   }

   // ---- Maps (and Colours) > Map Colours

   std::pair<wxPanel *, wxString> map_colours_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      YesNo swap = yes_no(page, col, "Swap difference map colours?",
                          [](bool coot) { bandicoot_core_set_o_difference_map_colours(! coot); },
                          "Coot colours", "O colours");

      wxStaticBoxSizer *rotation = frame(page, "New map rotates colour map [map independent]");
      auto turn = std::make_shared<DegreeSlider>(rotation->GetStaticBox(), rotation, "by", 360.0,
                                                 [](float d) { bandicoot_core_set_map_colour_rotation_on_read(d); });
      col->Add(rotation, 0, wxEXPAND);

      refreshers.push_back([swap, turn]() mutable {
         bandicoot_display_preferences p = bandicoot_core_display_preferences();
         swap.set(! p.o_difference_map_colours);
         turn->set(p.map_colour_rotation);
      });
      return { page, "Map Colours" };
   }

   // ---- Maps > Dragged Map

   std::pair<wxPanel *, wxString> map_drag_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Active Map on Dragging?");
      YesNo drag = yes_no(page, col, wxEmptyString, [](bool on) { bandicoot_core_set_active_map_drag(on); });
      refreshers.push_back([drag]() mutable { drag.set(bandicoot_core_display_preferences().active_map_drag); });
      return { page, "Dragged Map" };
   }

   // ---- Bonds > Cis-Peptides

   std::pair<wxPanel *, wxString> cis_peptides_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Mark Cis-Peptides as Bad in Geometry Graph?");
      YesNo bad = yes_no(page, col, wxEmptyString, [](bool on) { bandicoot_core_set_mark_cis_peptides_bad(on); });
      refreshers.push_back([bad]() mutable { bad.set(bandicoot_core_display_preferences().mark_cis_peptides_bad); });
      return { page, "Cis-Peptides" };
   }

   // ---- Colours > Background colour

   std::pair<wxPanel *, wxString> background_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Background colour?");
      wxStaticBoxSizer *box = frame(page, wxEmptyString);
      wxWindow *b = box->GetStaticBox();
      wxRadioButton *black = new wxRadioButton(b, wxID_ANY, "Black", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *white = new wxRadioButton(b, wxID_ANY, "White");
      wxRadioButton *own = new wxRadioButton(b, wxID_ANY, "Own colour");

      // the swatch holds the own colour; choosing Own colour applies it
      auto own_colour = std::make_shared<bandicoot_rgb>();
      auto apply = [](const bandicoot_rgb &c) { bandicoot_core_set_background_preference(c); };
      auto show = std::make_shared<std::function<void()>>();
      wxBitmapButton *pick = own_colour_swatch(b, own, "Background Colour", own_colour,
                                               []() { return bandicoot_core_display_preferences().background; },
                                               apply, [show]() { (*show)(); });
      wxBoxSizer *own_row = new wxBoxSizer(wxHORIZONTAL);
      own_row->Add(own, 0, wxALIGN_CENTER_VERTICAL);
      own_row->Add(pick, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      box->Add(black, 0, wxALL, 4);
      box->Add(white, 0, wxALL, 4);
      box->Add(own_row, 0, wxALL, 4);
      col->Add(box, 0, wxEXPAND);

      on_choose(black, [apply]() { apply(bandicoot_rgb()); });
      on_choose(white, [apply]() { bandicoot_rgb w; w.r = w.g = w.b = 1; apply(w); });
      on_choose(own, [apply, own_colour]() { apply(*own_colour); });

      *show = [black, white, own, pick, own_colour]() {
         bandicoot_rgb c = bandicoot_core_display_preferences().background;
         bool is_black = c.r < 0.01f && c.g < 0.01f && c.b < 0.01f;
         bool is_white = c.r > 0.99f && c.g > 0.99f && c.b > 0.99f;
         black->SetValue(is_black);
         white->SetValue(is_white);
         own->SetValue(! is_black && ! is_white);
         *own_colour = c;
         pick->SetBitmap(swatch(c));
      };
      refreshers.push_back([show]() { (*show)(); });
      return { page, "Background colour" };
   }

   // ---- Others > Antialiasing

   std::pair<wxPanel *, wxString> antialias_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Antialias Lines?");
      YesNo aa = yes_no(page, col, wxEmptyString, [](bool on) { bandicoot_core_set_antialias(on); });
      refreshers.push_back([aa]() mutable { aa.set(bandicoot_core_other_preferences().antialias); });
      return { page, "Antialiasing" };
   }

   // ---- Others > Fonts

   std::pair<wxPanel *, wxString> fonts_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      wxStaticBoxSizer *size = frame(page, "Atom Label Font Size?");
      wxWindow *sb = size->GetStaticBox();
      wxRadioButton *small = new wxRadioButton(sb, wxID_ANY, "Small", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *medium = new wxRadioButton(sb, wxID_ANY, "Medium");
      wxRadioButton *large = new wxRadioButton(sb, wxID_ANY, "Large");
      wxRadioButton *others = new wxRadioButton(sb, wxID_ANY, "Others");
      wxChoice *fonts = new wxChoice(sb, wxID_ANY);
      for (const std::string &f : bandicoot_core_other_font_labels()) fonts->Append(wxString::FromUTF8(f));
      wxBoxSizer *others_row = new wxBoxSizer(wxHORIZONTAL);
      others_row->Add(others, 0, wxALIGN_CENTER_VERTICAL);
      others_row->Add(fonts, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      for (wxRadioButton *r : { small, medium, large }) size->Add(r, 0, wxALL, 4);
      size->Add(others_row, 0, wxALL, 4);
      col->Add(size, 0, wxEXPAND | wxBOTTOM, 10);

      on_choose(small, []() { bandicoot_core_set_font_size_preference(1); });
      on_choose(medium, []() { bandicoot_core_set_font_size_preference(2); });
      on_choose(large, []() { bandicoot_core_set_font_size_preference(3); });
      // the other fonts are sizes 4 on; with none picked yet, the first
      auto apply_other = [fonts]() {
         if (fonts->GetSelection() < 0) fonts->SetSelection(0);
         bandicoot_core_set_font_size_preference(fonts->GetSelection() + 4);
      };
      on_choose(others, apply_other);
      fonts->Bind(wxEVT_CHOICE, [others, apply_other](wxCommandEvent &) {
         others->SetValue(true);
         apply_other();
      });

      wxStaticBoxSizer *colour = frame(page, "Atom Label Colour");
      wxWindow *cb = colour->GetStaticBox();
      wxRadioButton *standard = new wxRadioButton(cb, wxID_ANY, "Default", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *own = new wxRadioButton(cb, wxID_ANY, "Own label colour");
      auto own_colour = std::make_shared<bandicoot_rgb>(bandicoot_core_default_font_colour());
      auto apply = [](const bandicoot_rgb &c) { bandicoot_core_set_font_colour_preference(c); };
      auto show_colour = std::make_shared<std::function<void()>>();
      wxBitmapButton *pick = own_colour_swatch(cb, own, "Atom Label Colour", own_colour,
                                               []() { return bandicoot_core_other_preferences().font_colour; },
                                               apply, [show_colour]() { (*show_colour)(); });
      wxBoxSizer *own_row = new wxBoxSizer(wxHORIZONTAL);
      own_row->Add(own, 0, wxALIGN_CENTER_VERTICAL);
      own_row->Add(pick, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      colour->Add(standard, 0, wxALL, 4);
      colour->Add(own_row, 0, wxALL, 4);
      col->Add(colour, 0, wxEXPAND);
      on_choose(standard, [apply]() { apply(bandicoot_core_default_font_colour()); });
      on_choose(own, [apply, own_colour]() { apply(*own_colour); });

      // Coot's default label colour shows as Default, any other as own
      *show_colour = [standard, own, pick, own_colour]() {
         bandicoot_rgb c = bandicoot_core_other_preferences().font_colour;
         bandicoot_rgb d = bandicoot_core_default_font_colour();
         bool is_default = std::fabs(c.r - d.r) < 0.002f && std::fabs(c.g - d.g) < 0.002f &&
                           std::fabs(c.b - d.b) < 0.002f;
         standard->SetValue(is_default);
         own->SetValue(! is_default);
         *own_colour = c;
         pick->SetBitmap(swatch(c));
      };
      refreshers.push_back([=]() {
         int s = bandicoot_core_other_preferences().font_size;
         small->SetValue(s < 2);
         medium->SetValue(s == 2);
         large->SetValue(s == 3);
         others->SetValue(s > 3);
         fonts->SetSelection(s > 3 && s - 4 < int(fonts->GetCount()) ? s - 4 : wxNOT_FOUND);
         (*show_colour)();
      });
      return { page, "Fonts" };
   }

   // ---- Others > Console

   std::pair<wxPanel *, wxString> console_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      YesNo show = yes_no(page, col, "Display state commands in console?",
                          [](bool on) { bandicoot_core_set_console_commands(on); });
      refreshers.push_back([show]() mutable { show.set(bandicoot_core_other_preferences().console_commands); });
      return { page, "Console" };
   }

   // ---- Others > Pink Pointer Size

   std::pair<wxPanel *, wxString> pointer_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      heading(page, col, "Rotation Centre Cube Size");
      wxStaticBoxSizer *box = frame(page, wxEmptyString);
      wxWindow *b = box->GetStaticBox();
      wxTextCtrl *size = number_entry(b, [](double v) {
         if (v >= 0 && v <= 1000) bandicoot_core_set_pointer_size_preference(float(v));
      });
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(b, wxID_ANY, "Size:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      row->Add(size, 0, wxALIGN_CENTER_VERTICAL);
      row->Add(new wxStaticText(b, wxID_ANY, wxString::FromUTF8("\xc3\x85ngstr\xc3\xb6m")), 0,
               wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      box->Add(row, 0, wxALL, 6);
      col->Add(box, 0, wxEXPAND);
      refreshers.push_back([size]() {
         size->ChangeValue(wxString::Format("%.2f", bandicoot_core_other_preferences().pointer_size));
      });
      return { page, "Pink Pointer Size" };
   }

   // ---- Others > Speed Settings

   std::pair<wxPanel *, wxString> speed_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      // Coot's three named speeds, in steps per frame; anything else is own
      wxStaticBoxSizer *refine = frame(page, "Refinement Speed?");
      wxWindow *rb = refine->GetStaticBox();
      wxRadioButton *molasses = new wxRadioButton(rb, wxID_ANY, "Molasses Refinement mode", wxDefaultPosition,
                                                  wxDefaultSize, wxRB_GROUP);
      wxRadioButton *crock = new wxRadioButton(rb, wxID_ANY, "Crock Refinement mode");
      wxRadioButton *standard = new wxRadioButton(rb, wxID_ANY, "Default Refinement mode");
      wxRadioButton *own = new wxRadioButton(rb, wxID_ANY, "Set own speed");
      wxTextCtrl *own_steps = new wxTextCtrl(rb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(160, -1));
      wxBoxSizer *own_row = new wxBoxSizer(wxHORIZONTAL);
      own_row->Add(own, 0, wxALIGN_CENTER_VERTICAL);
      own_row->Add(own_steps, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      for (wxRadioButton *r : { molasses, crock, standard }) refine->Add(r, 0, wxALL, 4);
      refine->Add(own_row, 0, wxALL, 4);
      col->Add(refine, 0, wxEXPAND | wxBOTTOM, 10);

      on_choose(molasses, []() { bandicoot_core_set_refinement_speed(4); });
      on_choose(crock, []() { bandicoot_core_set_refinement_speed(120); });
      on_choose(standard, []() { bandicoot_core_set_refinement_speed(80); });
      auto apply_own = [own_steps]() {
         long v;
         if (own_steps->GetValue().ToLong(&v) && v >= 1 && v <= 10000) bandicoot_core_set_refinement_speed(int(v));
      };
      on_choose(own, apply_own);
      // typing a speed chooses own, as in Coot
      own_steps->Bind(wxEVT_TEXT, [own, apply_own](wxCommandEvent &) {
         own->SetValue(true);
         apply_own();
      });

      wxStaticBoxSizer *spin = frame(page, "Spin Speed?");
      wxWindow *pb = spin->GetStaticBox();
      wxTextCtrl *degrees = number_entry(pb, [](double v) {
         if (v >= 0 && v <= 360) bandicoot_core_set_spin_speed(float(v));
      });
      wxBoxSizer *spin_row = new wxBoxSizer(wxHORIZONTAL);
      spin_row->Add(new wxStaticText(pb, wxID_ANY, "Set Spin Speed"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      spin_row->Add(degrees, 0, wxALIGN_CENTER_VERTICAL);
      spin->Add(spin_row, 0, wxALL, 4);
      col->Add(spin, 0, wxEXPAND);

      refreshers.push_back([=]() {
         bandicoot_other_preferences p = bandicoot_core_other_preferences();
         int s = p.refinement_speed;
         molasses->SetValue(s == 4);
         crock->SetValue(s == 120);
         standard->SetValue(s == 80);
         own->SetValue(s != 4 && s != 120 && s != 80);
         own_steps->ChangeValue(wxString::Format("%d", s));
         degrees->ChangeValue(wxString::Format("%.2f", p.spin_speed));
      });
      return { page, "Speed Settings" };
   }

   // ---- Others > Pick Atom

   // A radius from Bandicoot's presets or typed in; the entry is live only for Other.
   void radius_frame(wxWindow *page, wxBoxSizer *col, const wxString &title,
                     std::function<float()> current, std::function<void(float)> set) {
      const wxString A = wxString::FromUTF8("\xc3\x85");
      const float presets[] = { 0.4f, 0.6f, 0.8f, 1.0f };
      wxStaticBoxSizer *box = frame(page, title);
      wxWindow *b = box->GetStaticBox();
      std::vector<wxRadioButton *> radios;
      for (int i = 0; i < 4; i++) {
         wxRadioButton *r = new wxRadioButton(b, wxID_ANY, wxString::Format("%.1f ", presets[i]) + A,
                                              wxDefaultPosition, wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
         box->Add(r, 0, wxALL, 4);
         radios.push_back(r);
      }
      wxRadioButton *other = new wxRadioButton(b, wxID_ANY, "Other:");
      wxTextCtrl *entry = new wxTextCtrl(b, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1));
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(other, 0, wxALIGN_CENTER_VERTICAL);
      row->Add(entry, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      row->Add(new wxStaticText(b, wxID_ANY, A), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
      box->Add(row, 0, wxALL, 4);
      col->Add(box, 0, wxEXPAND | wxBOTTOM, 10);

      auto apply_entry = [entry, set]() {
         double v;
         if (entry->GetValue().ToCDouble(&v) && v > 0) set(float(v));
      };
      for (int i = 0; i < 4; i++) {
         float r = presets[i];
         on_choose(radios[i], [entry, set, r]() { entry->Enable(false); set(r); });
      }
      on_choose(other, [entry, apply_entry]() { entry->Enable(true); entry->SetFocus(); apply_entry(); });
      entry->Bind(wxEVT_TEXT, [apply_entry](wxCommandEvent &) { apply_entry(); });

      refreshers.push_back([=]() {
         float v = current();
         bool preset = false;
         for (int i = 0; i < 4; i++) {
            bool on = std::fabs(v - presets[i]) < 1e-4f;
            radios[i]->SetValue(on);
            preset = preset || on;
         }
         other->SetValue(! preset);
         entry->ChangeValue(wxString::Format("%g", v));
         entry->Enable(! preset);
      });
   }

   std::pair<wxPanel *, wxString> pick_atom_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);
      radius_frame(page, col, "Atom Pick Radius",
                   []() { return bandicoot_core_other_preferences().pick_radius; },
                   [](float v) { bandicoot_core_set_pick_radius(v); });
      radius_frame(page, col, "Symmetry Atom Pick Radius",
                   []() { return bandicoot_core_other_preferences().symmetry_pick_radius; },
                   [](float v) { bandicoot_core_set_symmetry_pick_radius(v); });

      // the depth-weighted pair: far (back) and near (front) tolerances
      const wxString A = wxString::FromUTF8("\xc3\x85");
      const float far_presets[] = { 0.04f, 0.06f, 0.08f };
      const float near_presets[] = { 0.8f, 1.0f, 1.2f };
      wxStaticBoxSizer *box = frame(page, "Intermediate Atom Pick Radius");
      wxWindow *b = box->GetStaticBox();
      std::vector<wxRadioButton *> radios;
      for (int i = 0; i < 3; i++) {
         wxString label = wxString::Format("%.2f ", far_presets[i]) + A + wxString::Format(" - %.1f ", near_presets[i]) + A;
         wxRadioButton *r = new wxRadioButton(b, wxID_ANY, label, wxDefaultPosition, wxDefaultSize,
                                              i == 0 ? wxRB_GROUP : 0);
         box->Add(r, 0, wxALL, 4);
         radios.push_back(r);
      }
      wxRadioButton *other = new wxRadioButton(b, wxID_ANY, "Other:");
      wxTextCtrl *far_entry = new wxTextCtrl(b, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1));
      wxTextCtrl *near_entry = new wxTextCtrl(b, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1));
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(other, 0, wxALIGN_CENTER_VERTICAL);
      row->Add(far_entry, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      row->Add(new wxStaticText(b, wxID_ANY, A + " -"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 6);
      row->Add(near_entry, 0, wxALIGN_CENTER_VERTICAL);
      row->Add(new wxStaticText(b, wxID_ANY, A), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
      box->Add(row, 0, wxALL, 4);
      col->Add(box, 0, wxEXPAND);

      auto apply_entries = [far_entry, near_entry]() {
         double lo, hi;
         if (far_entry->GetValue().ToCDouble(&lo) && near_entry->GetValue().ToCDouble(&hi) && lo > 0 && hi > 0)
            bandicoot_core_set_intermediate_pick_radii(float(lo), float(hi));
      };
      for (int i = 0; i < 3; i++) {
         float lo = far_presets[i], hi = near_presets[i];
         on_choose(radios[i], [far_entry, near_entry, lo, hi]() {
            far_entry->Enable(false);
            near_entry->Enable(false);
            bandicoot_core_set_intermediate_pick_radii(lo, hi);
         });
      }
      on_choose(other, [far_entry, near_entry, apply_entries]() {
         far_entry->Enable(true);
         near_entry->Enable(true);
         far_entry->SetFocus();
         apply_entries();
      });
      far_entry->Bind(wxEVT_TEXT, [apply_entries](wxCommandEvent &) { apply_entries(); });
      near_entry->Bind(wxEVT_TEXT, [apply_entries](wxCommandEvent &) { apply_entries(); });

      std::vector<float> fars(far_presets, far_presets + 3), nears(near_presets, near_presets + 3);
      refreshers.push_back([=]() {
         bandicoot_other_preferences p = bandicoot_core_other_preferences();
         bool preset = false;
         for (int i = 0; i < 3; i++) {
            bool on = std::fabs(p.intermediate_pick_far - fars[i]) < 1e-4f &&
                      std::fabs(p.intermediate_pick_near - nears[i]) < 1e-4f;
            radios[i]->SetValue(on);
            preset = preset || on;
         }
         other->SetValue(! preset);
         far_entry->ChangeValue(wxString::Format("%g", p.intermediate_pick_far));
         near_entry->ChangeValue(wxString::Format("%g", p.intermediate_pick_near));
         far_entry->Enable(! preset);
         near_entry->Enable(! preset);
      });
      return { page, "Pick Atom" };
   }

   // ---- Others > PanDDA

   std::pair<wxPanel *, wxString> pandda_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      wxStaticBoxSizer *sources = frame(page, "PanDDA ligand sources");
      wxWindow *sb = sources->GetStaticBox();
      wxTextCtrl *cifs = new wxTextCtrl(sb, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                        wxTE_PROCESS_ENTER);
      wxButton *browse = new wxButton(sb, wxID_ANY, "Browse...");
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(sb, wxID_ANY, "Ligand CIFs directory:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      row->Add(cifs, 1, wxALIGN_CENTER_VERTICAL);
      row->Add(browse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
      sources->Add(row, 0, wxEXPAND | wxALL, 4);
      col->Add(sources, 0, wxEXPAND | wxBOTTOM, 10);

      // the directory is saved once it is finished with, as in Bandicoot's GTK tab
      auto save = [cifs]() {
         bandicoot_core_set_pandda_config(cifs->GetValue().utf8_string(),
                                          bandicoot_core_other_preferences().pandda_split);
      };
      cifs->Bind(wxEVT_TEXT_ENTER, [save](wxCommandEvent &) { save(); });
      cifs->Bind(wxEVT_KILL_FOCUS, [cifs, save](wxFocusEvent &e) {
         if (! cifs->IsBeingDeleted()) save();
         e.Skip();
      });
      browse->Bind(wxEVT_BUTTON, [this, cifs, save](wxCommandEvent &) {
         wxDirDialog d(this, "Select ligand CIFs directory", cifs->GetValue());
         if (d.ShowModal() != wxID_OK) return;
         cifs->ChangeValue(d.GetPath());
         save();
      });

      YesNo split = yes_no(page, col, "Ligand Splitting on Fitted-Model Load", [](bool on) {
         bandicoot_core_set_pandda_config(bandicoot_core_other_preferences().pandda_cifs_dir, on);
      }, "Yes", "No", true);
      YesNo mode = yes_no(page, col, "UI Mode", [](bool basic) { bandicoot_core_set_pandda_basic(basic); },
                          "Basic", "Full", true);

      refreshers.push_back([cifs, split, mode]() mutable {
         bandicoot_other_preferences p = bandicoot_core_other_preferences();
         cifs->ChangeValue(wxString::FromUTF8(p.pandda_cifs_dir));
         split.set(p.pandda_split);
         mode.set(p.pandda_basic);
      });
      return { page, "PanDDA" };
   }

   // ---- Others > Ligands

   std::pair<wxPanel *, wxString> ligands_page() {
      wxBoxSizer *col;
      wxPanel *page = new_page(notebook, col);

      // every answer is saved with the rest; an "ask" question that cannot
      // apply is greyed, not hidden, so the tab does not jump about
      auto answers = std::make_shared<std::vector<YesNo>>();
      auto boxes = std::make_shared<std::vector<wxStaticBox *>>(5, nullptr);
      auto sync = [answers, boxes]() {
         const std::vector<YesNo> &a = *answers;
         bandicoot_core_set_ligand_behaviour(a[0].yes->GetValue(), a[1].yes->GetValue(), a[2].yes->GetValue(),
                                             a[3].yes->GetValue(), a[4].yes->GetValue());
         (*boxes)[1]->Enable(! a[0].yes->GetValue());
         (*boxes)[3]->Enable(! a[2].yes->GetValue());
      };
      const char *questions[] = {
         "Rename LIG / DRG / INH automatically?",
         "Ask before renaming?",
         "Generate missing ligand restraints automatically?",
         "Ask before generating restraints?",
         "Apply Loaded mmCIF Restraints to All Molecules?"
      };
      for (int i = 0; i < 5; i++)
         answers->push_back(yes_no(page, col, questions[i], [sync](bool) { sync(); }, "Yes", "No", true,
                                   &(*boxes)[i]));
      col->Add(new wxStaticText(page, wxID_ANY,
                                "Answering No to both questions in a pair loads the coordinates and\n"
                                "leaves the ligands alone. Rename by hand with Modelling -> Rename\n"
                                "Residue, and generate with Modelling -> Generate Ligand Restraints."),
               0, wxTOP, 6);

      refreshers.push_back([answers, boxes]() {
         bandicoot_other_preferences p = bandicoot_core_other_preferences();
         std::vector<YesNo> &a = *answers;
         a[0].set(p.auto_rename);
         a[1].set(p.ask_rename);
         a[2].set(p.auto_generate);
         a[3].set(p.ask_generate);
         a[4].set(p.apply_restraints_to_all);
         (*boxes)[1]->Enable(! p.auto_rename);
         (*boxes)[3]->Enable(! p.auto_generate);
      });
      return { page, "Ligands" };
   }

   // Every setting this window shows, back to its default.
   void reset_defaults() {
      bandicoot_general_preferences d = bandicoot_core_general_preference_defaults();
      bandicoot_core_set_ar_docked(d.ar_docked);
      bandicoot_core_set_ar_always_shown(d.ar_always_shown);
      bandicoot_core_save_sequence_view_docked(d.sequence_view_docked);
      if (hooks.sequence_view_docking_changed) hooks.sequence_view_docking_changed(d.sequence_view_docked);
      bandicoot_core_save_sequence_view_max_chains(d.sequence_view_max_chains);
      if (hooks.sequence_view_rows_changed) hooks.sequence_view_rows_changed();
      if (hooks.set_sidebar_side) hooks.set_sidebar_side(d.sidebar_position);
      if (hooks.set_sidebar_docked) hooks.set_sidebar_docked(d.sidebar_docked);
      bandicoot_core_set_sidebar_style(d.sidebar_style);
      if (hooks.set_sidebar_icon_size) hooks.set_sidebar_icon_size(d.sidebar_icon_size);
      bandicoot_core_sidebar_show_icon_set(true);
      bandicoot_core_set_recentre_on_read(d.recentre_on_read);
      bandicoot_core_set_smooth_scroll(d.smooth_scroll);
      bandicoot_core_set_smooth_scroll_steps(d.smooth_scroll_steps);
      bandicoot_core_set_smooth_scroll_limit(d.smooth_scroll_limit);
      bandicoot_core_set_flat_trackball(d.flat_trackball);

      // as in Coot, every category goes back, not only the one shown
      bandicoot_display_preferences p = bandicoot_core_display_preference_defaults();
      bandicoot_core_set_default_bond_width(p.bond_width);
      bandicoot_core_set_scale_bond_width_to_occupancy(p.scale_bond_width_to_occupancy);
      if (p.bond_scheme >= 0) bandicoot_core_set_default_bond_scheme(p.bond_scheme);
      bandicoot_core_set_bond_colour_rotation_on_read(p.bond_colour_rotation);
      bandicoot_core_set_carbons_only_preference(p.carbons_only);
      bandicoot_core_set_altloc_conf_a_offset(p.altloc_conf_a_offset);
      bandicoot_core_set_altloc_colour_difference(p.altloc_colour_difference);
      bandicoot_core_set_map_radius_preference(p.map_radius);
      bandicoot_core_set_map_increment_preference(p.map_increment);
      bandicoot_core_set_diff_map_increment_preference(p.diff_map_increment);
      bandicoot_core_set_map_sampling_rate_preference(p.map_sampling_rate);
      bandicoot_core_set_dynamic_map_sampling_preference(p.dynamic_map_sampling);
      bandicoot_core_set_dynamic_map_size_preference(p.dynamic_map_size);
      bandicoot_core_set_o_difference_map_colours(p.o_difference_map_colours);
      bandicoot_core_set_map_colour_rotation_on_read(p.map_colour_rotation);
      bandicoot_core_set_active_map_drag(p.active_map_drag);
      bandicoot_core_set_mark_cis_peptides_bad(p.mark_cis_peptides_bad);
      bandicoot_core_set_background_preference(p.background);

      // the PanDDA ligand directory is a project path, not a setting, so it stays
      bandicoot_other_preferences o = bandicoot_core_other_preference_defaults();
      bandicoot_core_set_antialias(o.antialias);
      bandicoot_core_set_font_size_preference(o.font_size);
      bandicoot_core_set_font_colour_preference(o.font_colour);
      bandicoot_core_set_console_commands(o.console_commands);
      bandicoot_core_set_pointer_size_preference(o.pointer_size);
      bandicoot_core_set_refinement_speed(o.refinement_speed);
      bandicoot_core_set_spin_speed(o.spin_speed);
      bandicoot_core_set_pick_radius(o.pick_radius);
      bandicoot_core_set_symmetry_pick_radius(o.symmetry_pick_radius);
      bandicoot_core_set_intermediate_pick_radii(o.intermediate_pick_far, o.intermediate_pick_near);
      bandicoot_core_set_pandda_config(o.pandda_cifs_dir, o.pandda_split);
      bandicoot_core_set_pandda_basic(o.pandda_basic);
      bandicoot_core_set_ligand_behaviour(o.auto_rename, o.ask_rename, o.auto_generate, o.ask_generate,
                                          o.apply_restraints_to_all);
      refresh();
   }
};

PreferencesWindow *PreferencesWindow::shown = nullptr;

} // namespace

void show_preferences(wxWindow *parent, const PreferencesHooks &hooks) {
   if (PreferencesWindow::shown) {
      PreferencesWindow::shown->refresh();
      PreferencesWindow::shown->Raise();
      return;
   }
   PreferencesWindow *w = new PreferencesWindow(parent, hooks);
   PreferencesWindow::shown = w;
   w->CentreOnParent();
   w->Show();
}

} // namespace wxui

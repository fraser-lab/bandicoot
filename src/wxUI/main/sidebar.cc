// src/wxUI/main/sidebar.cc

#include "wxUI/main/sidebar.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/assets/toolbar-button.hh"

#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/app.h>
#include <wx/sizer.h>
#include <wx/tglbtn.h>

#include <algorithm>
#include <vector>

namespace wxui {

namespace {

struct SidebarEntry {
   enum Kind { Push, Tool, ToggleTool, RotateTranslate, Separator };
   Kind kind;
   const char *label;
   const char *icon;    // file in the pixmaps directory
   const char *action;
   const char *widget;  // Coot's name for it, by which the core shows or hides it
   bool important = false; // labelled in the horizontal icons-and-text layout, as Coot marks it
};

typedef SidebarEntry E;

// Coot's model toolbar, in its order.
const std::vector<SidebarEntry> entries = {
   { E::Push,       "R/RC",                    nullptr,                "on_model_toolbar_refine_control_button_clicked", "model_toolbar_refine_control_button" },
   { E::Push,       "Map",                     nullptr,                "on_model_toolbar_select_map_button_clicked", "model_toolbar_select_map_button" },
   { E::ToggleTool, "Real Space Refine Zone",  "refine-1.svg",         "on_model_toolbar_refine_togglebutton_toggled", "model_toolbar_refine_togglebutton", true },
   { E::ToggleTool, "Regularize Zone",         "regularize-1.svg",     "on_model_toolbar_regularize_togglebutton_toggled", "model_toolbar_regularize_togglebutton", true },
   { E::Tool,       "Fixed Atoms...",          "anchor.svg",           "on_model_toolbar_fixed_atoms_button_clicked", "model_toolbar_fixed_atoms_button" },
   { E::ToggleTool, "Rigid Body Fit Zone",     "rigid-body.svg",       "on_model_toolbar_rigid_body_fit_togglebutton_toggled", "model_toolbar_rigid_body_fit_togglebutton" },
   { E::RotateTranslate, "Rotate Translate",   "rtz.svg",              "on_model_toolbar_rot_trans_toolbutton_clicked", "model_toolbar_rot_trans_toolbutton" },
   { E::ToggleTool, "Auto Fit Rotamer",        "auto-fit-rotamer.svg", "on_model_toolbar_auto_fit_rotamer_togglebutton_toggled", "model_toolbar_auto_fit_rotamer_togglebutton" },
   { E::ToggleTool, "Rotamers...",             "rotamers.svg",         "on_model_toolbar_rotamers_togglebutton_toggled", "model_toolbar_rotamers_togglebutton" },
   { E::ToggleTool, "Edit Chi Angles",         "edit-chi.svg",         "on_model_toolbar_edit_chi_angles_togglebutton_toggled", "model_toolbar_edit_chi_angles_togglebutton" },
   { E::ToggleTool, "Torsion General",         "torsion-general.svg",  "on_model_toolbar_torsion_general_toggletoolbutton_toggled", "model_toolbar_torsion_general_toggletoolbutton" },
   { E::ToggleTool, "Flip Peptide",            "flip-peptide.svg",     "on_model_toolbar_flip_peptide_togglebutton_toggled", "model_toolbar_flip_peptide_togglebutton" },
   { E::ToggleTool, "Sidechain 180\302\260 Flip", "side-chain-180.svg", "on_model_toolbar_sidechain_180_togglebutton_toggled", "model_toolbar_sidechain_180_togglebutton" },
   { E::ToggleTool, "Edit Backbone Torsions",  "edit-backbone.svg",    "on_model_toolbar_edit_backbone_torsions_toggletoolbutton_toggled", "model_toolbar_edit_backbone_torsions_toggletoolbutton" },
   { E::Separator,  nullptr, nullptr, nullptr, "model_toolbar_hsep_toolitem" },
   { E::ToggleTool, "Mutate & Auto Fit...",    "mutate-auto-fit.svg",  "on_model_toolbar_mutate_and_autofit_togglebutton_toggled", "model_toolbar_mutate_and_autofit_togglebutton" },
   { E::ToggleTool, "Simple Mutate...",        "mutate.svg",           "on_model_toolbar_simple_mutate_togglebutton_toggled", "model_toolbar_simple_mutate_togglebutton" },
   { E::Tool,       "Find Waters...",          "add-water.svg",        "on_model_toolbar_find_water_button_clicked", "model_toolbar_find_water_button", true },
   { E::ToggleTool, "Add Terminal Residue...", "add-peptide-1.svg",    "on_model_toolbar_add_terminal_residue_togglebutton_toggled", "model_toolbar_add_terminal_residue_togglebutton" },
   { E::Tool,       "Add Alt Conf...",         "add-alt-conf.svg",     "on_model_toolbar_add_alt_conf_toolbutton_clicked", "model_toolbar_add_alt_conf_toolbutton", true },
   { E::Tool,       "Place Atom At Pointer",   "atom-at-pointer.svg",  "on_model_toolbar_add_atom_button_clicked", "model_toolbar_add_atom_button" },
   { E::Tool,       "Clear Pending Picks",     "stock-clear.png",      "on_model_toolbar_clear_pending_picks_button_clicked", "model_toolbar_clear_pending_picks_button" },
   { E::Tool,       "Delete...",               "stock-delete.png",     "on_model_toolbar_delete_button_clicked", "model_toolbar_delete_button" },
   { E::Tool,       "Undo",                    "stock-undo.png",       "on_model_toolbar_undo_button_clicked", "model_toolbar_undo_button" },
   { E::Tool,       "Redo",                    "stock-redo.png",       "on_model_toolbar_redo_button_clicked", "model_toolbar_redo_button" },
   { E::Separator,  nullptr, nullptr, nullptr, "model_toolbar_hsep_toolitem2" },
   { E::Tool,       "Run Refmac...",           "azerbaijan.svg",       "on_model_toolbar_refmac_button_clicked", "model_toolbar_refmac_button" },
   { E::Separator,  nullptr, nullptr, nullptr, nullptr }
};

const char *rot_trans_modes[] = { "By Residue Range...", "By Chain...", "By Molecule..." };

// GTK's etched separator line: a shadow line beside a highlight line.
class SeparatorLine : public wxWindow {
public:
   SeparatorLine(wxWindow *parent, bool across_a_column)
      : wxWindow(parent, wxID_ANY), horizontal(across_a_column) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      SetMinSize(horizontal ? wxSize(-1, 6) : wxSize(6, -1));
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
         wxPaintDC dc(this);
         wxSize s = GetClientSize();
         dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
         dc.Clear();
         dc.SetPen(*wxTRANSPARENT_PEN);
         // shadow and highlight taken from the background, so the groove
         // reads in light and dark appearance alike
         wxColour bg = GetParent()->GetBackgroundColour();
         wxBrush shadow(bg.ChangeLightness(70)), light(bg.ChangeLightness(130));
         if (horizontal) {
            int y = s.y / 2 - 1;
            dc.SetBrush(shadow); dc.DrawRectangle(2, y, s.x - 4, 1);
            dc.SetBrush(light);  dc.DrawRectangle(2, y + 1, s.x - 4, 1);
         } else {
            int x = s.x / 2 - 1;
            dc.SetBrush(shadow); dc.DrawRectangle(x, 2, 1, s.y - 4);
            dc.SetBrush(light);  dc.DrawRectangle(x + 1, 2, 1, s.y - 4);
         }
      });
   }
private:
   bool horizontal;
};

// Docked at the bottom in the icons-and-text style, only the tools Coot marks
// important carry text, as in Coot, which keeps the strip short. A column
// labels every tool.
ToolbarButton::Content content_for(Sidebar::Style style, bool vertical, bool important) {
   switch (style) {
   case Sidebar::Style::Icons: return ToolbarButton::Content::Icon;
   case Sidebar::Style::Text:  return ToolbarButton::Content::Text;
   default:
      if (vertical) return ToolbarButton::Content::IconBesideText;
      return important ? ToolbarButton::Content::IconAboveText
                       : ToolbarButton::Content::Icon;
   }
}

wxString settings_label(Sidebar::Style style) {
   const wxString gear = wxString::FromUTF8("\342\232\231"); // U+2699
   switch (style) {
   case Sidebar::Style::Icons: return gear;
   case Sidebar::Style::Text:  return "Settings";
   default:                    return gear + " Settings";
   }
}

// R/RC and Map spell themselves out unless the sidebar shows icons only.
wxString push_label(const SidebarEntry &e, Sidebar::Style style) {
   std::string action = e.action;
   if (style != Sidebar::Style::Icons) {
      if (action == "on_model_toolbar_refine_control_button_clicked") return "Refine/Regularize Control...";
      if (action == "on_model_toolbar_select_map_button_clicked")     return "Select Map...";
   }
   return wxString::FromUTF8(e.label);
}

enum { ID_STYLE = 2000, ID_MAIN_ICONS = 2010, ID_ALL_ICONS, ID_USER_ICONS, ID_DOCK = 2020, ID_DOCK_SIDE = 2030 };

void refresh_all(wxWindow *w) {
   w->Refresh();
   for (wxWindow *c : w->GetChildren()) refresh_all(c);
}

} // namespace

Sidebar::Sidebar(wxWindow *parent, bool vertical_in, Style style_in, int icon_points_in,
                 Hooks hooks_in)
   : wxPanel(parent), vertical(vertical_in), style(style_in), icon_points(icon_points_in),
     hooks(std::move(hooks_in)) {

   // No colour of its own: the sidebar shows the window's background, which
   // follows the system appearance.

   // the tools scroll if the window is too short (or narrow) for them; the
   // Settings button stays in view
   area = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                               vertical ? wxVSCROLL : wxHSCROLL);
   area->SetScrollRate(vertical ? 0 : 10, vertical ? 10 : 0);
   tools = new wxBoxSizer(vertical ? wxVERTICAL : wxHORIZONTAL);
   // the border beside the tools is kept clear for the scroll bar when one shows
   wxBoxSizer *wrapper = new wxBoxSizer(vertical ? wxHORIZONTAL : wxVERTICAL);
   tools_item = wrapper->Add(tools, 1, wxEXPAND | (vertical ? wxRIGHT : wxBOTTOM), 0);
   area->SetSizer(wrapper);
   area->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
      e.Skip();
      CallAfter([this]() { update_scroll_room(); });
   });
   Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent &e) {
      e.Skip();
      refresh_all(this);
   });

   settings = new BevelButton(this, wxID_ANY, settings_label(style));
   settings->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_settings_menu(); });

   wxBoxSizer *outer = new wxBoxSizer(vertical ? wxVERTICAL : wxHORIZONTAL);
   outer->Add(area, 1, wxEXPAND);
   outer->Add(settings, 0, wxEXPAND | wxALL, 2);
   SetSizer(outer);

   populate();
   refresh_icons();
}

void Sidebar::set_style(Style style_in) {
   if (style_in == style) return;
   style = style_in;
   tools->Clear(true);
   named.clear();
   populate();
   settings->SetLabel(settings_label(style));
   settings->InvalidateBestSize();
   refresh_icons();
}

void Sidebar::refresh_icons() {
   if (hooks.icon_states) {
      for (const IconState &s : hooks.icon_states())
         for (auto &n : named)
            if (n.first == s.widget) n.second->Show(s.shown);
   }
   relayout();
}

wxSize Sidebar::natural_size() const {
   wxSize t = tools->GetMinSize();
   wxSize s = settings->GetBestSize();
   const int margin = 2 * 2; // around the Settings button
   if (vertical) return wxSize(std::max(t.x, s.x + margin), t.y + s.y + margin);
   return wxSize(t.x + s.x + margin, std::max(t.y, s.y + margin));
}

void Sidebar::update_scroll_room() {
   // macOS draws its scroll bar over the content, so while the tools overflow
   // the sidebar widens by the bar's width and keeps the tools out from under it
   wxSize client = area->GetClientSize();
   wxSize need = tools->GetMinSize();
   bool overflow = vertical ? need.y > client.y : need.x > client.x;
   int bar = wxSystemSettings::GetMetric(vertical ? wxSYS_VSCROLL_X : wxSYS_HSCROLL_Y, area);
   int room = overflow ? (bar > 0 ? bar : 15) : 0;
   if (room == scroll_room) return;
   scroll_room = room;
   tools_item->SetBorder(room);
   relayout();
}

void Sidebar::relayout() {
   // as wide (or tall) as the widest tool, plus any room kept for the scroll
   // bar, so nothing is clipped or covered
   area->SetMinSize(wxDefaultSize);
   tools->Layout();
   area->FitInside();
   wxSize need = tools->GetMinSize();
   if (vertical) area->SetMinSize(wxSize(need.x + scroll_room, -1));
   else          area->SetMinSize(wxSize(-1, need.y + scroll_room));
   InvalidateBestSize();
   // the slot holding the sidebar, and the window, take up the new size
   for (wxWindow *w = this; w; w = w->GetParent()) {
      w->InvalidateBestSize();
      if (w->IsTopLevel() && w != wxTheApp->GetTopWindow() && w->GetSizer()) {
         // undocked, in a window of its own: that window fits the sidebar
         wxSize min = w->GetSizer()->GetMinSize();
         wxSize cur = w->GetClientSize();
         if (vertical) w->SetMinClientSize(wxSize(min.x, 120));
         else          w->SetMinClientSize(wxSize(120, min.y));
         w->SetClientSize(vertical ? min.x : cur.x, vertical ? cur.y : min.y);
      }
      w->Layout();
      if (w->IsTopLevel()) break;
   }
   // what the tools need may have changed without the area changing size
   CallAfter([this]() { update_scroll_room(); });
}

void Sidebar::populate() {

   const int flags = wxEXPAND | (vertical ? (wxLEFT | wxRIGHT) : (wxTOP | wxBOTTOM));

   for (const SidebarEntry &e : entries) {
      std::string action = e.action ? e.action : "";
      std::string label  = e.label ? e.label : "";
      wxWindow *made = nullptr;
      switch (e.kind) {
      case E::Separator:
         made = new SeparatorLine(area, vertical);
         tools->Add(made, 0, flags, 1);
         break;
      case E::Push: {
         BevelButton *b = new BevelButton(area, wxID_ANY, push_label(e, style));
         b->Bind(wxEVT_BUTTON, [this, action, label](wxCommandEvent &) {
            if (hooks.on_select) hooks.on_select(action, label, true);
         });
         tools->Add(b, 0, flags, 1);
         made = b;
         break;
      }
      case E::Tool:
      case E::ToggleTool:
      case E::RotateTranslate: {
         ToolbarButton *b = new ToolbarButton(area, wxID_ANY, wxString::FromUTF8(e.label),
                                              icon(e.icon, wxSize(icon_points, icon_points)),
                                              content_for(style, vertical, e.important));
         b->set_toggle(e.kind == E::ToggleTool);
         b->Bind(wxEVT_BUTTON, [this, action, label](wxCommandEvent &) {
            if (hooks.on_select) hooks.on_select(action, label, true);
         });
         b->Bind(wxEVT_TOGGLEBUTTON, [this, action, label](wxCommandEvent &ev) {
            if (hooks.on_select) hooks.on_select(action, label, ev.GetInt() != 0);
         });
         tools->Add(b, 0, flags, 1);
         made = b;
         if (e.kind == E::RotateTranslate) {
            // the drop-down part: which kind of selection Rotate Translate acts on
            ToolbarButton *arrow = new ToolbarButton(area, wxID_ANY,
                                                     wxString::FromUTF8("\342\226\270"), // U+25B8
                                                     wxBitmapBundle(), ToolbarButton::Content::Text);
            arrow->SetToolTip("Rotate Translate by...");
            arrow->Bind(wxEVT_BUTTON, [this, arrow](wxCommandEvent &) { show_rot_trans_menu(arrow); });
            tools->Add(arrow, 0, flags, 1);
            // shown and hidden with its button
            if (e.widget) named.emplace_back(e.widget, arrow);
         }
         break;
      }
      }
      if (made && e.widget) named.emplace_back(e.widget, made);
   }
}

void Sidebar::show_settings_menu() {

   wxMenu menu;
   const char *styles[] = { "Icons", "Icons and text", "Text" };
   for (int i = 0; i < 3; i++)
      menu.AppendRadioItem(ID_STYLE + i, styles[i])->Check(int(style) == i + 1);
   menu.AppendSeparator();

   // which set is showing, worked out as Coot does: nothing hidden is All
   // Icons, only the tools hidden by default is Main Icons, anything else is
   // the user's own choice (from Preferences)
   int set = ID_ALL_ICONS;
   if (hooks.icon_states) {
      for (const IconState &s : hooks.icon_states()) {
         if (s.shown) continue;
         if (s.shown_by_default) { set = ID_USER_ICONS; break; }
         set = ID_MAIN_ICONS;
      }
   }
   menu.AppendRadioItem(ID_MAIN_ICONS, "Main Icons")->Check(set == ID_MAIN_ICONS);
   menu.AppendRadioItem(ID_ALL_ICONS,  "All Icons")->Check(set == ID_ALL_ICONS);
   menu.AppendRadioItem(ID_USER_ICONS, "User defined")->Check(set == ID_USER_ICONS);
   bool docked = hooks.is_docked ? hooks.is_docked() : true;
   if (hooks.dock_on) {
      menu.AppendSeparator();
      int side = hooks.dock_side ? hooks.dock_side() : 0;
      const struct { int side; const char *label; } sides[] = {
         { 1, "Dock Left" }, { 0, "Dock Right" }, { 3, "Dock Bottom" } };
      for (const auto &s : sides)
         menu.AppendCheckItem(ID_DOCK_SIDE + s.side, s.label)->Check(docked && side == s.side);
   }
   if (hooks.toggle_docked) {
      if (! hooks.dock_on) menu.AppendSeparator();
      menu.Append(ID_DOCK, docked ? "Undock" : "Dock");
   }

   menu.Bind(wxEVT_MENU, [this](wxCommandEvent &ev) {
      int id = ev.GetId();
      if (id >= ID_STYLE && id < ID_STYLE + 3) {
         set_style(Style(id - ID_STYLE + 1));
      } else if (id == ID_MAIN_ICONS || id == ID_ALL_ICONS) {
         if (hooks.choose_icon_set) hooks.choose_icon_set(id == ID_MAIN_ICONS);
         refresh_icons();
      } else if (id == ID_DOCK) {
         if (hooks.toggle_docked) hooks.toggle_docked();
      } else if (id >= ID_DOCK_SIDE && id <= ID_DOCK_SIDE + 3) {
         if (hooks.dock_on) hooks.dock_on(id - ID_DOCK_SIDE);
      }
      // "User defined" only reports a choice made in Preferences, as in Coot
   });
   settings->PopupMenu(&menu, wxPoint(0, 0));
}

void Sidebar::show_rot_trans_menu(wxWindow *anchor) {
   wxMenu menu;
   for (int i = 0; i < 3; i++)
      menu.AppendRadioItem(1000 + i, rot_trans_modes[i])->Check(i == rot_trans_mode);
   menu.Bind(wxEVT_MENU, [this](wxCommandEvent &ev) {
      rot_trans_mode = ev.GetId() - 1000;
      if (hooks.on_select)
         hooks.on_select("rot_trans_by", rot_trans_modes[rot_trans_mode], true);
   });
   wxPoint at = vertical ? wxPoint(anchor->GetSize().x, 0) : wxPoint(0, anchor->GetSize().y);
   anchor->PopupMenu(&menu, at);
}

} // namespace wxui

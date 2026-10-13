// src/wxUI/main/sidebar.cc

#include "wxUI/main/sidebar.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/assets/toolbar-button.hh"

#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/tglbtn.h>

#include <vector>

namespace wxui {

namespace {

struct SidebarEntry {
   enum Kind { Push, Tool, ToggleTool, RotateTranslate, Separator };
   Kind kind;
   const char *label;
   const char *icon;    // file in the pixmaps directory
   const char *action;
};

typedef SidebarEntry E;

// Coot's model toolbar, in its order.
const std::vector<SidebarEntry> entries = {
   { E::Push,       "R/RC",                    nullptr,                "on_model_toolbar_refine_control_button_clicked" },
   { E::Push,       "Map",                     nullptr,                "on_model_toolbar_select_map_button_clicked" },
   { E::ToggleTool, "Real Space Refine Zone",  "refine-1.svg",         "on_model_toolbar_refine_togglebutton_toggled" },
   { E::ToggleTool, "Regularize Zone",         "regularize-1.svg",     "on_model_toolbar_regularize_togglebutton_toggled" },
   { E::Tool,       "Fixed Atoms...",          "anchor.svg",           "on_model_toolbar_fixed_atoms_button_clicked" },
   { E::ToggleTool, "Rigid Body Fit Zone",     "rigid-body.svg",       "on_model_toolbar_rigid_body_fit_togglebutton_toggled" },
   { E::RotateTranslate, "Rotate Translate",   "rtz.svg",              "on_model_toolbar_rot_trans_toolbutton_clicked" },
   { E::ToggleTool, "Auto Fit Rotamer",        "auto-fit-rotamer.svg", "on_model_toolbar_auto_fit_rotamer_togglebutton_toggled" },
   { E::ToggleTool, "Rotamers...",             "rotamers.svg",         "on_model_toolbar_rotamers_togglebutton_toggled" },
   { E::ToggleTool, "Edit Chi Angles",         "edit-chi.svg",         "on_model_toolbar_edit_chi_angles_togglebutton_toggled" },
   { E::ToggleTool, "Torsion General",         "torsion-general.svg",  "on_model_toolbar_torsion_general_toggletoolbutton_toggled" },
   { E::ToggleTool, "Flip Peptide",            "flip-peptide.svg",     "on_model_toolbar_flip_peptide_togglebutton_toggled" },
   { E::ToggleTool, "Sidechain 180\302\260 Flip", "side-chain-180.svg", "on_model_toolbar_sidechain_180_togglebutton_toggled" },
   { E::ToggleTool, "Edit Backbone Torsions",  "edit-backbone.svg",    "on_model_toolbar_edit_backbone_torsions_toggletoolbutton_toggled" },
   { E::Separator,  nullptr, nullptr, nullptr },
   { E::ToggleTool, "Mutate & Auto Fit...",    "mutate-auto-fit.svg",  "on_model_toolbar_mutate_and_autofit_togglebutton_toggled" },
   { E::ToggleTool, "Simple Mutate...",        "mutate.svg",           "on_model_toolbar_simple_mutate_togglebutton_toggled" },
   { E::Tool,       "Find Waters...",          "add-water.svg",        "on_model_toolbar_find_water_button_clicked" },
   { E::ToggleTool, "Add Terminal Residue...", "add-peptide-1.svg",    "on_model_toolbar_add_terminal_residue_togglebutton_toggled" },
   { E::Tool,       "Add Alt Conf...",         "add-alt-conf.svg",     "on_model_toolbar_add_alt_conf_toolbutton_clicked" },
   { E::Tool,       "Place Atom At Pointer",   "atom-at-pointer.svg",  "on_model_toolbar_add_atom_button_clicked" },
   { E::Tool,       "Clear Pending Picks",     "stock-clear.png",      "on_model_toolbar_clear_pending_picks_button_clicked" },
   { E::Tool,       "Delete...",               "stock-delete.png",     "on_model_toolbar_delete_button_clicked" },
   { E::Tool,       "Undo",                    "stock-undo.png",       "on_model_toolbar_undo_button_clicked" },
   { E::Tool,       "Redo",                    "stock-redo.png",       "on_model_toolbar_redo_button_clicked" },
   { E::Separator,  nullptr, nullptr, nullptr },
   { E::Tool,       "Run Refmac...",           "azerbaijan.svg",       "on_model_toolbar_refmac_button_clicked" },
   { E::Separator,  nullptr, nullptr, nullptr }
};

const char *rot_trans_modes[] = { "By Residue Range...", "By Chain...", "By Molecule..." };

const wxSize icon_size(24, 24); // points, as GTK's toolbar sizes them

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
         if (horizontal) {
            int y = s.y / 2 - 1;
            dc.SetBrush(wxBrush(palette::shadow_of(palette::face())));
            dc.DrawRectangle(2, y, s.x - 4, 1);
            dc.SetBrush(wxBrush(palette::highlight()));
            dc.DrawRectangle(2, y + 1, s.x - 4, 1);
         } else {
            int x = s.x / 2 - 1;
            dc.SetBrush(wxBrush(palette::shadow_of(palette::face())));
            dc.DrawRectangle(x, 2, 1, s.y - 4);
            dc.SetBrush(wxBrush(palette::highlight()));
            dc.DrawRectangle(x + 1, 2, 1, s.y - 4);
         }
      });
   }
private:
   bool horizontal;
};

ToolbarButton::Content content_for(Sidebar::Style style, bool vertical) {
   switch (style) {
   case Sidebar::Style::Icons: return ToolbarButton::Content::Icon;
   case Sidebar::Style::Text:  return ToolbarButton::Content::Text;
   default:
      return vertical ? ToolbarButton::Content::IconBesideText
                      : ToolbarButton::Content::IconAboveText;
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

} // namespace

Sidebar::Sidebar(wxWindow *parent, bool vertical_in, Style style_in, Handler handler)
   : wxPanel(parent), vertical(vertical_in), style(style_in), on_select(std::move(handler)) {

   SetBackgroundColour(palette::sidebar_face());

   // the tools scroll if the window is too short (or narrow) for them; the
   // Settings button stays in view
   wxScrolledWindow *area = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                                 wxDefaultSize,
                                                 vertical ? wxVSCROLL : wxHSCROLL);
   area->SetBackgroundColour(palette::sidebar_face());
   area->SetScrollRate(vertical ? 0 : 10, vertical ? 10 : 0);
   wxBoxSizer *tools = new wxBoxSizer(vertical ? wxVERTICAL : wxHORIZONTAL);
   populate(area, tools);
   area->SetSizer(tools);

   BevelButton *settings = new BevelButton(this, wxID_ANY, settings_label(style));
   settings->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      if (on_select) on_select("sidebar_settings", "Settings", true);
   });

   wxBoxSizer *outer = new wxBoxSizer(vertical ? wxVERTICAL : wxHORIZONTAL);
   outer->Add(area, 1, wxEXPAND);
   outer->Add(settings, 0, wxEXPAND | wxALL, 2);
   SetSizer(outer);

   // as wide (or tall) as the widest tool, so nothing is clipped
   area->FitInside();
   wxSize need = tools->GetMinSize();
   if (vertical) area->SetMinSize(wxSize(need.x, -1));
   else          area->SetMinSize(wxSize(-1, need.y));
}

void Sidebar::populate(wxWindow *area, wxSizer *sizer) {

   const int flags = wxEXPAND | (vertical ? (wxLEFT | wxRIGHT) : (wxTOP | wxBOTTOM));
   ToolbarButton::Content content = content_for(style, vertical);

   for (const SidebarEntry &e : entries) {
      std::string action = e.action ? e.action : "";
      std::string label  = e.label ? e.label : "";
      switch (e.kind) {
      case E::Separator:
         sizer->Add(new SeparatorLine(area, vertical), 0, flags, 1);
         break;
      case E::Push: {
         BevelButton *b = new BevelButton(area, wxID_ANY, wxString::FromUTF8(e.label));
         b->Bind(wxEVT_BUTTON, [this, action, label](wxCommandEvent &) {
            if (on_select) on_select(action, label, true);
         });
         sizer->Add(b, 0, flags, 1);
         break;
      }
      case E::Tool:
      case E::ToggleTool:
      case E::RotateTranslate: {
         ToolbarButton *b = new ToolbarButton(area, wxID_ANY, wxString::FromUTF8(e.label),
                                              icon(e.icon, icon_size), content);
         b->set_toggle(e.kind == E::ToggleTool);
         b->Bind(wxEVT_BUTTON, [this, action, label](wxCommandEvent &) {
            if (on_select) on_select(action, label, true);
         });
         b->Bind(wxEVT_TOGGLEBUTTON, [this, action, label](wxCommandEvent &ev) {
            if (on_select) on_select(action, label, ev.GetInt() != 0);
         });
         sizer->Add(b, 0, flags, 1);
         if (e.kind == E::RotateTranslate) {
            // the drop-down part: which kind of selection Rotate Translate acts on
            ToolbarButton *arrow = new ToolbarButton(area, wxID_ANY,
                                                     wxString::FromUTF8("\342\226\270"), // U+25B8
                                                     wxBitmapBundle(), ToolbarButton::Content::Text);
            arrow->SetToolTip("Rotate Translate by...");
            arrow->Bind(wxEVT_BUTTON, [this, arrow](wxCommandEvent &) { show_rot_trans_menu(arrow); });
            sizer->Add(arrow, 0, flags, 1);
         }
         break;
      }
      }
   }
}

void Sidebar::show_rot_trans_menu(wxWindow *anchor) {
   wxMenu menu;
   for (int i = 0; i < 3; i++)
      menu.AppendRadioItem(1000 + i, rot_trans_modes[i])->Check(i == rot_trans_mode);
   menu.Bind(wxEVT_MENU, [this](wxCommandEvent &ev) {
      rot_trans_mode = ev.GetId() - 1000;
      if (on_select)
         on_select("rot_trans_by", rot_trans_modes[rot_trans_mode], true);
   });
   wxPoint at = vertical ? wxPoint(anchor->GetSize().x, 0) : wxPoint(0, anchor->GetSize().y);
   anchor->PopupMenu(&menu, at);
}

} // namespace wxui

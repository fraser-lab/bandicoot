// src/wxUI/main/top-toolbar.cc

#include "wxUI/main/top-toolbar.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/dialogs/toolbar-customizer.hh"
#ifdef __WXOSX__
#include "wxUI/main/mac-menu.hh"
#endif

#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/toolbar.h>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace wxui {

namespace {

struct ToolbarEntry {
   const char *id;       // as the saved toolbar names it
   const char *label;
   const char *icon;     // file in the pixmaps directory
   const char *action;
   bool toggle;
   bool initially_on;
   const char *icon_on = nullptr;  // a toggle's icon while it is on, if it has one
};

// The tools that always lead the toolbar, in order.
const std::vector<ToolbarEntry> fixed_entries = {
   { "open_coords",     "Open Coords...",   "coords-open.svg",     "on_coords_toolbutton_clicked",          false, false },
   { "auto_open_mtz",   "Auto-open MTZ",    "mtz-open.svg",        "bandicoot_action_auto_open_mtz",        false, false },
   { "open_map",        "Open Map",         "map-open.svg",        "bandicoot_action_open_map",             false, false },
   { "display_manager", "Display Manager",  "display-manager.svg", "on_display_manager_toolbutton_clicked", false, false },
   { "go_to_atom",      "Go To Atom...",    "go-to-atom.svg",      "on_go_to_atom_toolbutton_clicked",      false, false }
};

// The tools the user can add, in the customizer's order.
const std::vector<ToolbarEntry> optional_entries = {
   { "reset_view",             "Reset View",          "stock-zoom-fit.png",      "on_reset_view_toolbutton_clicked",        false, false },
   { "go_to_ligand",           "Go To Ligand",        "go-to-ligand.svg",        "on_go_to_ligand_toolbutton_clicked",      false, false },
   { "quicksave",              "Quicksave",           "coot-save.png",           "bandicoot_action_quicksave",              false, false },
   { "sphere_refine",          "Sphere Refine",       "refine-sphere-1.svg",     "bandicoot_action_sphere_refine",          false, false },
   { "sphere_refine_plus",     "Sphere Refine +",     "refine-sphere-plus-1.svg",     "bandicoot_action_sphere_refine_plus",     false, false },
   { "refine_tandem",          "Tandem Refine",       "refine-tandem-1.svg",     "bandicoot_action_refine_tandem",          false, false },
   { "sphere_regularize",      "Sphere Regularize",   "regularize-sphere-1.svg", "bandicoot_action_sphere_regularize",      false, false },
   { "sphere_regularize_plus", "Sphere Regularize +", "regularize-sphere-plus-1.svg", "bandicoot_action_sphere_regularize_plus", false, false },
   { "cis_trans",              "Cis \xe2\x86\x94 Trans", "flip-peptide.svg",     "bandicoot_action_cis_trans",              false, false },  // double arrow, UTF-8
   { "local_probe_dots",       "Local Probe Dots",    "probe-clash-local.svg",         "bandicoot_action_local_probe_dots",       false, false },
   { "backrub_toggle",         "Backrub Rotamers",    "auto-fit-rotamer-backrub.svg",    "bandicoot_action_backrub_apply",          true,  false, "auto-fit-rotamer-backrub-on.svg" },
   { "hydrogen_toggle",        "Toggle Hydrogens",    "hydrogen-toggle.svg",              "bandicoot_action_hydrogen_apply",         true,  true, "hydrogen-toggle-on.svg" },
   { "full_screen_toggle",     "Full Screen",         "reset-view-32.svg",       "bandicoot_action_full_screen_apply",      true,  false },
   { "interactive_dots_toggle","Interactive Dots",    "probe-clash.svg",         "bandicoot_action_interactive_dots_apply", true,  false, "probe-clash-on.svg" }
};

const std::vector<std::string> default_layout =
   { "sphere_refine", "refine_tandem", "local_probe_dots", "hydrogen_toggle" };

const int default_style = 2;     // icons and text
const wxSize icon_size(24, 24);  // points, as the GTK build sizes them

const ToolbarEntry *find_optional(const std::string &id) {
   for (const ToolbarEntry &e : optional_entries)
      if (id == e.id) return &e;
   return nullptr;
}

long style_flags(int style) {
   long flags = wxTB_HORIZONTAL | wxTB_FLAT;
   if (style != 1) flags |= wxTB_TEXT;
   if (style == 3) flags |= wxTB_NOICONS;
   return flags;
}

// A toggle with an "on" icon shows its state by its icon.
wxBitmapBundle state_icon(const ToolbarEntry &e, bool on) {
   return icon(on && e.icon_on ? e.icon_on : e.icon, icon_size);
}

class TopToolbar {
public:
   TopToolbar(wxFrame *f, const ToolbarHandler &h) : frame(f), on_select(h) {
      std::vector<std::string> saved;
      if (bandicoot_core_top_toolbar(style, saved)) {
         // ids this build does not know (an older or newer saved toolbar) are dropped
         for (const std::string &id : saved)
            if (find_optional(id)) layout.push_back(id);
      } else {
         style = default_style;
         layout = default_layout;
      }
      for (const ToolbarEntry &e : optional_entries)
         if (e.toggle) toggled[e.id] = e.initially_on;
      build();
#ifdef __WXOSX__
      // the native toolbar reports no right-clicks; AppKit is asked instead
      mac_on_toolbar_right_click(frame, [this]() { show_menu(); });
      mac_title_above_toolbar(frame);
#endif
   }

   void customize();
   void show_menu();
   void set_toggle(const std::string &action, bool on);

private:
   wxFrame *frame;
   ToolbarHandler on_select;
   int style = default_style;
   std::vector<std::string> layout;     // after the fixed tools
   std::map<std::string, bool> toggled; // toggles keep their state across rebuilds
   std::map<int, const ToolbarEntry *> ids;

   void build();
   void add(wxToolBar *tb, const ToolbarEntry &e);
   void set_style(int s);
   void set_layout(const std::vector<std::string> &l);
   void save() const { bandicoot_core_save_top_toolbar(style, layout); }
};

// The toolbar is made again for any change: its style is fixed when it is
// created.
void TopToolbar::build() {
   if (wxToolBar *old = frame->GetToolBar()) {
      frame->SetToolBar(nullptr);
      old->Destroy();
   }
   ids.clear();
   wxToolBar *tb = frame->CreateToolBar(style_flags(style));
   tb->SetToolBitmapSize(icon_size);
   for (const ToolbarEntry &e : fixed_entries) add(tb, e);
   for (const std::string &id : layout)
      if (const ToolbarEntry *e = find_optional(id)) add(tb, *e);
   tb->Realize();

   tb->Bind(wxEVT_TOOL, [this](wxCommandEvent &ev) {
      auto it = ids.find(ev.GetId());
      if (it == ids.end()) { ev.Skip(); return; }
      const ToolbarEntry *e = it->second;
      if (e->toggle) {
         toggled[e->id] = ev.IsChecked();
         if (e->icon_on)
            frame->GetToolBar()->SetToolNormalBitmap(ev.GetId(), state_icon(*e, ev.IsChecked()));
      }
      if (on_select) on_select(e->action, e->label, e->toggle ? ev.IsChecked() : true);
   });
   // where the toolkit reports right-clicks on tools (not the macOS toolbar)
   tb->Bind(wxEVT_TOOL_RCLICKED, [this](wxCommandEvent &) { show_menu(); });
   frame->Layout();
}

void TopToolbar::add(wxToolBar *tb, const ToolbarEntry &e) {
   wxToolBarToolBase *tool =
      tb->AddTool(wxID_ANY, e.label, state_icon(e, e.toggle && toggled[e.id]), e.label,
                  e.toggle ? wxITEM_CHECK : wxITEM_NORMAL);
   if (e.toggle) tb->ToggleTool(tool->GetId(), toggled[e.id]);
   ids[tool->GetId()] = &e;
}

void TopToolbar::set_style(int s) {
   if (s == style) return;
   style = s;
   save();
   build();
}

void TopToolbar::set_layout(const std::vector<std::string> &l) {
   layout = l;
   save();
   build();
}

void TopToolbar::show_menu() {
   wxMenu menu;
   const std::pair<int, const char *> styles[] = {
      { 2, "Icon and Text" }, { 1, "Icon Only" }, { 3, "Text Only" }
   };
   for (const auto &s : styles) {
      wxMenuItem *mi = menu.AppendCheckItem(wxID_ANY, s.second);
      mi->Check(style == s.first);
      int value = s.first;
      menu.Bind(wxEVT_MENU, [this, value](wxCommandEvent &) {
         // after the menu has closed
         frame->CallAfter([this, value]() { set_style(value); });
      }, mi->GetId());
   }
   menu.AppendSeparator();
   wxMenuItem *c = menu.Append(wxID_ANY, "Customize Toolbar...");
   menu.Bind(wxEVT_MENU, [this](wxCommandEvent &) {
      frame->CallAfter([this]() { customize(); });
   }, c->GetId());
   frame->PopupMenu(&menu);
}

ToolbarCustomizer::Item customizer_item(const ToolbarEntry &e) {
   ToolbarCustomizer::Item i;
   i.id = e.id;
   i.label = wxString::FromUTF8(e.label);
   i.icon = icon(e.icon, icon_size);
   return i;
}

void TopToolbar::customize() {
   std::vector<ToolbarCustomizer::Item> fixed, palette;
   for (const ToolbarEntry &e : fixed_entries) fixed.push_back(customizer_item(e));
   for (const ToolbarEntry &e : optional_entries) palette.push_back(customizer_item(e));

   // applied once it closes: the toolbar is not remade under an open dialog
   ToolbarCustomizer d(frame, fixed, palette, layout, default_layout);
   d.ShowModal();
   if (d.arrangement() != layout) set_layout(d.arrangement());
}

void TopToolbar::set_toggle(const std::string &action, bool on) {
   wxToolBar *tb = frame->GetToolBar();
   for (const auto &i : ids) {
      const ToolbarEntry *e = i.second;
      if (! e->toggle || action != e->action) continue;
      toggled[e->id] = on;
      if (tb) {
         tb->ToggleTool(i.first, on);
         if (e->icon_on) tb->SetToolNormalBitmap(i.first, state_icon(*e, on));
      }
      return;
   }
   // not on the toolbar: remembered for when it is added
   for (const ToolbarEntry &e : optional_entries)
      if (e.toggle && action == e.action) toggled[e.id] = on;
}

TopToolbar *toolbar = nullptr;

} // namespace

void build_top_toolbar(wxFrame *frame, const ToolbarHandler &on_select) {
   toolbar = new TopToolbar(frame, on_select);
}

void customize_top_toolbar() {
   if (toolbar) toolbar->customize();
}

void set_top_toolbar_toggle(const std::string &action, bool on) {
   if (toolbar) toolbar->set_toggle(action, on);
}

} // namespace wxui

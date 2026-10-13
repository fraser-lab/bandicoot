// src/wxUI/main/top-toolbar.cc

#include "wxUI/main/top-toolbar.hh"
#include "wxUI/assets/icons.hh"

#include <wx/frame.h>
#include <wx/toolbar.h>

#include <map>
#include <memory>
#include <vector>

namespace wxui {

namespace {

struct ToolbarEntry {
   const char *label;
   const char *icon;     // file in the pixmaps directory
   const char *action;
   bool toggle;
   bool initially_on;
};

// Bandicoot's default toolbar, in its order. Auto-open MTZ has no icon of its
// own and shows Open Coords'.
const std::vector<ToolbarEntry> entries = {
   { "Open Coords...",   "stock-open.png",          "on_coords_toolbutton_clicked",          false, false },
   { "Auto-open MTZ",    "stock-open.png",          "bandicoot_action_auto_open_mtz",        false, false },
   { "Open Map",         "map.svg",                 "bandicoot_action_open_map",             false, false },
   { "Quicksave",        "coot-save.png",           "bandicoot_action_quicksave",            false, false },
   { "Display Manager",  "display-manager.svg",     "on_display_manager_toolbutton_clicked", false, false },
   { "Go To Atom...",    "go-to-atom.svg",          "on_go_to_atom_toolbutton_clicked",      false, false },
   { "Sphere Refine",    "refine-sphere-1.png",     "bandicoot_action_sphere_refine",        false, false },
   { "Tandem Refine",    "refine-tandem-1.png",     "bandicoot_action_refine_tandem",        false, false },
   { "Local Probe Dots", "probe-clash.svg",         "bandicoot_action_local_probe_dots",     false, false },
   { "Toggle Hydrogens", "delete.svg",              "bandicoot_action_hydrogen_apply",       true,  true  }
};

const wxSize icon_size(24, 24); // points, as the GTK build sizes them

} // namespace

void build_top_toolbar(wxFrame *frame, const ToolbarHandler &on_select) {

   wxToolBar *tb = frame->CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_FLAT);
   tb->SetToolBitmapSize(icon_size);

   auto ids = std::make_shared<std::map<int, const ToolbarEntry *>>();
   for (const ToolbarEntry &e : entries) {
      wxToolBarToolBase *tool =
         tb->AddTool(wxID_ANY, e.label, icon(e.icon, icon_size), e.label,
                     e.toggle ? wxITEM_CHECK : wxITEM_NORMAL);
      if (e.toggle) tb->ToggleTool(tool->GetId(), e.initially_on);
      (*ids)[tool->GetId()] = &e;
   }
   tb->Realize();

   tb->Bind(wxEVT_TOOL, [ids, on_select](wxCommandEvent &ev) {
      auto it = ids->find(ev.GetId());
      if (it == ids->end()) { ev.Skip(); return; }
      const ToolbarEntry *e = it->second;
      if (on_select) on_select(e->action, e->label, e->toggle ? ev.IsChecked() : true);
   });
}

} // namespace wxui

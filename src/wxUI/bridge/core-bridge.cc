// src/wxUI/bridge/core-bridge.cc
//
// Core side of the wx main window: adapts wx's GL surface and input to the
// existing GTK-era renderer and event handlers. See core-bridge.hh.
//
// During the toolkit changeover the core still reaches the GL surface through
// graphics_info_t::glarea, both as a GtkWidget (for its allocation) and as a
// "main window exists" token. A wx build therefore keeps a hidden GTK widget
// tree and a stand-in glarea whose allocation tracks the wx surface; the GL
// context itself is wx's.

#if defined (USE_PYTHON)
#include "Python.h"  // before system includes to stop "POSIX_C_SOURCE" redefined problems
#endif

#include <cstring>
#include <iostream>
#include <string>

#include <gtk/gtk.h>
#ifdef HAVE_GSL
#include <gsl/gsl_errno.h>
#endif

#include "graphics-info.h"
#include "globjects.h"
#include "c-interface.h"
#include "c-interface-gtk-widgets.h"
#include "c-interface-refine.h"
#include "cc-interface.hh"
#include "drag-and-drop.hh"
#include "c-interface-ligands-swig.hh"
#include "coot-utils/coot-package-paths.hh"

#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/main/surface.hh"

static_assert(int(BANDICOOT_MOD_SHIFT) == int(GDK_SHIFT_MASK),   "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_CONTROL) == int(GDK_CONTROL_MASK), "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_ALT) == int(GDK_MOD1_MASK),    "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_BUTTON1) == int(GDK_BUTTON1_MASK), "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_BUTTON2) == int(GDK_BUTTON2_MASK), "modifier bits must match GDK");
static_assert(int(BANDICOOT_MOD_BUTTON3) == int(GDK_BUTTON3_MASK), "modifier bits must match GDK");

void bandicoot_core_gl_init() {
   if (graphics_info_t::glarea)
      init(graphics_info_t::glarea);
}

void bandicoot_core_gl_resize(int surface_width, int surface_height,
                              int window_width, int window_height) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GtkAllocation allocation;
   allocation.x = 0;
   allocation.y = 0;
   allocation.width  = surface_width;
   allocation.height = surface_height;
   gtk_widget_size_allocate(glarea, &allocation);

   if (graphics_info_t::make_current_gl_context(glarea)) {
      // the viewport is in backing-store pixels, the allocation in points
      double s = bandicoot_wx_surface_backing_scale();
      glViewport(0, 0, int(surface_width * s), int(surface_height * s));
   }
   graphics_info_t::graphics_x_size = window_width;
   graphics_info_t::graphics_y_size = window_height;
}

void bandicoot_core_gl_draw() {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventExpose event;
   memset(&event, 0, sizeof(event));
   event.type  = GDK_EXPOSE;
   event.count = 0;
   event.area.width  = glarea->allocation.width;
   event.area.height = glarea->allocation.height;
   draw(glarea, &event);
}

static GdkEventButton make_button_event(GdkEventType type, int button,
                                        double x, double y, unsigned int mods) {
   GdkEventButton event;
   memset(&event, 0, sizeof(event));
   event.type   = type;
   event.time   = GDK_CURRENT_TIME;
   event.x      = x;
   event.y      = y;
   event.x_root = x;
   event.y_root = y;
   event.state  = mods;
   event.button = button;
   return event;
}

void bandicoot_core_button_press(int button, int click_count,
                                 double x, double y, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   // GTK reports the second press of a double click twice, as a plain press
   // and then as a 2BUTTON_PRESS; the handler expects both.
   GdkEventButton event = make_button_event(GDK_BUTTON_PRESS, button, x, y, mods);
   glarea_button_press(glarea, &event);
   if (click_count == 2) {
      GdkEventButton event_2 = make_button_event(GDK_2BUTTON_PRESS, button, x, y, mods);
      glarea_button_press(glarea, &event_2);
   }
}

void bandicoot_core_button_release(int button, double x, double y, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;
   GdkEventButton event = make_button_event(GDK_BUTTON_RELEASE, button, x, y, mods);
   glarea_button_release(glarea, &event);
}

void bandicoot_core_motion(double x, double y, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventMotion event;
   memset(&event, 0, sizeof(event));
   event.type    = GDK_MOTION_NOTIFY;
   event.time    = GDK_CURRENT_TIME;
   event.x       = x;
   event.y       = y;
   event.x_root  = x;
   event.y_root  = y;
   event.state   = mods;
   event.is_hint = 0; // the handler then takes position and state from the event
   glarea_motion_notify(glarea, &event);
}

void bandicoot_core_scroll(bool up, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventScroll event;
   memset(&event, 0, sizeof(event));
   event.type      = GDK_SCROLL;
   event.time      = GDK_CURRENT_TIME;
   event.state     = mods;
   event.direction = up ? GDK_SCROLL_UP : GDK_SCROLL_DOWN;
   glarea_scroll_event(glarea, &event);
}

void bandicoot_core_key(bool press, unsigned int keyval, unsigned int mods) {

   GtkWidget *glarea = graphics_info_t::glarea;
   if (! glarea) return;

   GdkEventKey event;
   memset(&event, 0, sizeof(event));
   event.type   = press ? GDK_KEY_PRESS : GDK_KEY_RELEASE;
   event.time   = GDK_CURRENT_TIME;
   event.state  = mods;
   event.keyval = keyval;
   event.string = const_cast<gchar *>("");
   if (press)
      key_press_event(glarea, &event);
   else
      key_release_event(glarea, &event);
}

void bandicoot_core_drop_file(const std::string &file_name) {
   handle_drag_and_drop_single_item(file_name);
}

void bandicoot_core_drop_text(const std::string &text) {
   handle_drag_and_drop_string(text);
}

std::string bandicoot_core_pixmaps_dir() {
   return coot::package_pixmaps_dir();
}

int bandicoot_core_sidebar_style() {
   return model_toolbar_style_state();
}

std::vector<bandicoot_sidebar_icon> bandicoot_core_sidebar_icons() {
   std::vector<bandicoot_sidebar_icon> v;
   if (graphics_info_t::model_toolbar_icons)
      for (const coot::preferences_icon_info_t &i : *graphics_info_t::model_toolbar_icons)
         v.push_back({ i.icon_widget, i.show_hide_flag == 1, i.default_show_flag == 1 });
   return v;
}

void bandicoot_core_sidebar_show_icon_set(bool main_icons_only) {
   if (main_icons_only)
      show_model_toolbar_main_icons();
   else
      show_model_toolbar_all_icons();
}

extern "C" void bandicoot_save_sidebar_docked(int docked);
extern "C" int  bandicoot_load_sidebar_docked(void);

bool bandicoot_core_sidebar_docked() {
   return bandicoot_load_sidebar_docked() != 0;
}

void bandicoot_core_save_sidebar_docked(bool docked) {
   bandicoot_save_sidebar_docked(docked ? 1 : 0);
}

extern "C" void bandicoot_save_sidebar_position(int position);
extern "C" int  bandicoot_load_sidebar_position(void);

int bandicoot_core_sidebar_position() {
   return bandicoot_load_sidebar_position();
}

void bandicoot_core_save_sidebar_position(int position) {
   bandicoot_save_sidebar_position(position);
}

extern "C" void bandicoot_save_sidebar_icon_size(int points);
extern "C" int  bandicoot_load_sidebar_icon_size(void);

int bandicoot_core_sidebar_icon_size() {
   return bandicoot_load_sidebar_icon_size();
}

void bandicoot_core_save_sidebar_icon_size(int points) {
   bandicoot_save_sidebar_icon_size(points);
}

bool bandicoot_load_top_toolbar(int &style, std::vector<std::string> &ids);
void bandicoot_save_top_toolbar(int style, const std::vector<std::string> &ids);

bool bandicoot_core_top_toolbar(int &style, std::vector<std::string> &ids) {
   return bandicoot_load_top_toolbar(style, ids);
}

void bandicoot_core_save_top_toolbar(int style, const std::vector<std::string> &ids) {
   bandicoot_save_top_toolbar(style, ids);
}

bool bandicoot_core_has_capability(const std::string &capability) {
   if (capability == "enhanced_ligand_tools")
      return enhanced_ligand_coot_p();
   return false;
}

static std::vector<std::string> extensions(const std::vector<std::string> *v) {
   return v ? *v : std::vector<std::string>();
}

std::vector<std::string> bandicoot_core_coordinates_extensions() {
   return extensions(graphics_info_t::coordinates_glob_extensions);
}

std::vector<std::string> bandicoot_core_data_extensions() {
   return extensions(graphics_info_t::data_glob_extensions);
}

std::vector<std::string> bandicoot_core_map_extensions() {
   return extensions(graphics_info_t::map_glob_extensions);
}

std::string bandicoot_core_chooser_directory() {
   graphics_info_t g;
   return g.get_directory_for_filechooser();
}

void bandicoot_core_save_chooser_directory(const std::string &directory) {
   graphics_info_t g;
   g.set_directory_for_filechooser_string(directory);
}

bool bandicoot_core_recentre_on_read() {
   return graphics_info_t::recentre_on_read_pdb;
}

void bandicoot_core_read_coordinates(const std::string &file_name, int recentre) {
   if (recentre == 2)
      handle_read_draw_molecule_and_move_molecule_here(file_name.c_str());
   else
      handle_read_draw_molecule_with_recentre(file_name.c_str(), recentre == 0 ? 1 : 0);
}

void bandicoot_core_read_map(const std::string &file_name, bool is_difference_map) {
   handle_read_ccp4_map_internal(file_name.c_str(), is_difference_map ? 1 : 0);
}

bool bandicoot_core_auto_open_dataset(const std::string &file_name, std::string &why_not) {

   // as the GTK build's dataset chooser does for Auto-open MTZ
   const char *f = file_name.c_str();
   bool is_mtz = is_mtz_file_p(f);
   bool has_phases = is_mtz ? mtz_file_has_phases_p(f) : cns_file_has_phases_p(f);
   if (has_phases) {
      wrapped_auto_read_make_and_draw_maps(f);
      return true;
   }
   std::cout << "INFO:: This file is not a map coefficient file. Coot can auto-read\n"
             << "INFO::  - MTZ files from refmac, phenix.refine, phaser, parrot, dm.\n"
             << "INFO::  - CNS files (new 2009 format only) with cell, symops, F1, F2."
             << std::endl;
   if (is_mtz) {
      calc_phases_generic(f);
      return true;
   }
   // The GTK build offers its column selector here; in the wx build that
   // dialog belongs to File > Open MTZ, mmCIF, fcf or phs...
   why_not = "not an MTZ or CNS file with map coefficients";
   return false;
}

// ---- Accept/Reject

static bandicoot_ar_view ar_view;

void bandicoot_core_set_ar_view(const bandicoot_ar_view &view) {
   ar_view = view;
}

static bandicoot_ar_results ar_results(const coot::refinement_results_t &rr) {

   // the stock dialog's wording
   bandicoot_ar_results r;
#ifdef HAVE_GSL
   if (rr.progress == GSL_ENOPROG)       r.status = "Failed - Error: No Progress";
   else if (rr.progress == GSL_CONTINUE) r.status = "Running...";
   else if (rr.progress == GSL_FAILURE)  r.status = "Failure";
   else if (rr.progress == GSL_SUCCESS)  r.status = "Success";
#endif
   for (const coot::refinement_lights_info_t &li : rr.lights) {
      GdkColor c = li.name == "Rama" ? colour_by_rama_plot_distortion(li.value, li.rama_type)
                                     : colour_by_distortion(li.value);
      r.lights.push_back({ li.name, li.label, (unsigned char) (c.red >> 8),
                           (unsigned char) (c.green >> 8), (unsigned char) (c.blue >> 8) });
   }
   return r;
}

bool bandicoot_wx_ar_present(const std::string &fit_type, const coot::refinement_results_t &rr) {
   if (! ar_view.present) return false;
   ar_view.present(fit_type, ar_results(rr), fit_type == "Torsion General",
                   get_auto_clear_atom_pull_restraint_state() != 0);
   return true;
}

bool bandicoot_wx_ar_update(const coot::refinement_results_t &rr) {
   if (! ar_view.update) return false;
   ar_view.update(ar_results(rr));
   return true;
}

bool bandicoot_wx_ar_warnings(const std::string &text) {
   if (! ar_view.warnings) return false;
   ar_view.warnings(text);
   return true;
}

bool bandicoot_core_ar_position(int &x, int &y) {
   x = graphics_info_t::accept_reject_dialog_x_position;
   y = graphics_info_t::accept_reject_dialog_y_position;
   return x > -100 && y > -100;  // the GTK dialog's test
}

void bandicoot_core_save_ar_position(int x, int y) {
   graphics_info_t::accept_reject_dialog_x_position = x;
   graphics_info_t::accept_reject_dialog_y_position = y;
}

// ---- Go To Atom

static bandicoot_goto_view goto_view;

void bandicoot_core_set_goto_view(const bandicoot_goto_view &view) {
   goto_view = view;
}

bool bandicoot_wx_goto_open() {
   return goto_view.open && goto_view.open();
}

void bandicoot_wx_goto_target_changed() {
   if (goto_view.target_changed) goto_view.target_changed();
}

void bandicoot_wx_goto_model_changed(int imol) {
   if (goto_view.model_changed) goto_view.model_changed(imol);
}

void bandicoot_wx_goto_molecules_changed(int imol) {
   if (goto_view.molecules_changed) goto_view.molecules_changed(imol);
}

// ---- Display Manager

static bandicoot_dm_view dm_view;

void bandicoot_core_set_dm_view(const bandicoot_dm_view &view) {
   dm_view = view;
}

bool bandicoot_wx_display_manager_open() {
   return dm_view.open && dm_view.open();
}

void bandicoot_wx_molecules_changed() {
   bandicoot_wx_goto_molecules_changed(-1);
   if (dm_view.molecules_changed) dm_view.molecules_changed();
}

void bandicoot_wx_display_changed() {
   if (dm_view.state_changed) dm_view.state_changed();
}

// The Render As menu: label, the bonds_box_type it shows, how to choose it.
// The same list, in the same order, is in gtk-manual.cc (the GTK menu) and
// c-interface-preferences.cc (bandicoot_bond_schemes[]); keep all three in step.
namespace {
struct render_as_t {
   const char *label;
   int bonds_box_type;
   void (*apply)(int imol);
};
const render_as_t render_as_items[] = {
   { "Bonds (Colour by Atom)",       coot::NORMAL_BONDS,             graphics_to_bonds_representation },
   { "Bonds (Colour by Molecule)",   coot::COLOUR_BY_MOLECULE_BONDS, set_colour_by_molecule },
   { "Bonds (Colour by Chain)",      coot::COLOUR_BY_CHAIN_BONDS,    set_colour_by_chain },
   { "Bonds (Colour by Sec. Str.)",  coot::BONDS_SEC_STRUCT_COLOUR,  graphics_to_sec_struct_bonds_representation },
   { "Bonds (Colour by Alt. Conf.)", coot::COLOUR_BY_ALTLOC_BONDS,   graphics_to_colour_by_altloc_representation },
   { "C-alphas/Backbone",            coot::CA_BONDS,                 graphics_to_ca_representation },
   { "CAs + Ligands",                coot::CA_BONDS_PLUS_LIGANDS,    graphics_to_ca_plus_ligands_representation },
   { "CAs+Ligs SecStr Col",          coot::CA_BONDS_PLUS_LIGANDS_SEC_STRUCT_COLOUR,
                                     graphics_to_ca_plus_ligands_sec_struct_representation },
   { "Jones' Rainbow",               coot::COLOUR_BY_RAINBOW_BONDS,  graphics_to_rainbow_representation },
   { "Colour by Atom - No Waters",   coot::BONDS_NO_WATERS,          graphics_to_bonds_no_waters_representation },
   { "Colour by B-factors - CAs",    coot::CA_BONDS_PLUS_LIGANDS_B_FACTOR_COLOUR,
                                     graphics_to_b_factor_cas_representation },
   { "Colour by B-factors - All",    coot::COLOUR_BY_B_FACTOR_BONDS, graphics_to_b_factor_representation },
   { "Colour by Occupancy",          coot::COLOUR_BY_OCCUPANCY_BONDS, graphics_to_occupancy_representation }
};
const int n_render_as = sizeof(render_as_items) / sizeof(render_as_items[0]);
}

std::vector<std::string> bandicoot_core_render_as_labels() {
   std::vector<std::string> v;
   for (const render_as_t &r : render_as_items) v.push_back(r.label);
   return v;
}

void bandicoot_core_set_render_as(int imol, int index) {
   if (is_valid_model_molecule(imol) && index >= 0 && index < n_render_as)
      render_as_items[index].apply(imol);
}

std::vector<bandicoot_dm_molecule> bandicoot_core_dm_maps() {
   std::vector<bandicoot_dm_molecule> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_map_molecule(imol))
         v.push_back({ imol, graphics_info_t::molecules[imol].name_for_display_manager(),
                       map_is_displayed(imol) != 0, false, -1 });
   return v;
}

std::vector<bandicoot_dm_molecule> bandicoot_core_dm_models() {
   std::vector<bandicoot_dm_molecule> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (! is_valid_model_molecule(imol)) continue;
      const molecule_class_info_t &m = graphics_info_t::molecules[imol];
      int render_as = -1;
      for (int i=0; i<n_render_as; i++)
         if (render_as_items[i].bonds_box_type == m.Bonds_box_type()) render_as = i;
      v.push_back({ imol, m.name_for_display_manager(), mol_is_displayed(imol) != 0,
                    mol_is_active(imol) != 0, render_as });
   }
   return v;
}

int bandicoot_core_scroll_map() {
   return graphics_info_t::scroll_wheel_map;
}

void bandicoot_core_set_map_displayed(int imol, bool state) {
   set_map_displayed(imol, state ? 1 : 0);
}

void bandicoot_core_set_model_displayed(int imol, bool state) {
   set_mol_displayed(imol, state ? 1 : 0);
   set_mol_active(imol, state ? 1 : 0);
}

void bandicoot_core_set_model_active(int imol, bool state) {
   set_mol_active(imol, state ? 1 : 0);
}

void bandicoot_core_set_scroll_map(int imol) {
   set_scrollable_map(imol);
}

void bandicoot_core_close_molecule(int imol) {
   close_molecule(imol);
}

void bandicoot_core_set_all_maps_displayed(bool state) {
   // map by map: set_all_maps_displayed() changes the maps' active flag, not
   // their display, and relied on the GTK window's buttons to do the rest.
   // One redraw at the end.
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_map_molecule(imol))
         graphics_info_t::molecules[imol].set_map_is_displayed(state ? 1 : 0);
   graphics_draw();
}

void bandicoot_core_set_all_models_displayed(bool state) {
   set_all_models_displayed_and_active(state ? 1 : 0);
}

void bandicoot_core_display_last_model_only() {
   set_only_last_model_molecule_displayed();
}

bool bandicoot_core_dm_geometry(bandicoot_dm_geometry &g) {
   g.x      = graphics_info_t::display_manager_x_position;
   g.y      = graphics_info_t::display_manager_y_position;
   g.width  = graphics_info_t::display_manager_x_size;
   g.height = graphics_info_t::display_manager_y_size;
   g.split  = graphics_info_t::display_manager_paned_position;
   return g.x != -1;
}

void bandicoot_core_save_dm_geometry(const bandicoot_dm_geometry &g) {
   graphics_info_t::display_manager_x_position = g.x;
   graphics_info_t::display_manager_y_position = g.y;
   graphics_info_t::display_manager_x_size     = g.width;
   graphics_info_t::display_manager_y_size     = g.height;
   graphics_info_t::display_manager_paned_position = g.split;
}

bandicoot_goto_target bandicoot_core_goto_target() {
   graphics_info_t g;
   bandicoot_goto_target t;
   t.resno = g.go_to_atom_residue(); // first: it fills an unset target in
   t.imol = g.go_to_atom_molecule();
   t.chain = g.go_to_atom_chain();
   t.ins_code = g.go_to_atom_ins_code();
   t.atom_name = g.go_to_atom_atom_name();
   t.alt_conf = g.go_to_atom_alt_conf();
   if (t.alt_conf == "empty") t.alt_conf.clear();
   return t;
}

std::vector<std::pair<int, std::string>> bandicoot_core_goto_molecules() {

   // as the GTK menu labels them: number and name, the name cut to its
   // last characters
   std::vector<std::pair<int, std::string>> v;
   const unsigned int n_max = graphics_info_t::go_to_atom_menu_label_n_chars_max;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (! graphics_info_t::molecules[imol].has_model()) continue;
      std::string name = graphics_info_t::molecules[imol].name_;
      if (name.length() > n_max)
         name = "..." + name.substr(name.length() - n_max);
      v.push_back({ imol, std::to_string(imol) + "  " + name });
   }
   return v;
}

void bandicoot_core_set_goto_molecule(int imol) {
   graphics_info_t g;
   g.set_go_to_atom_molecule(imol);
}

std::vector<bandicoot_goto_chain> bandicoot_core_goto_residues(int imol) {
   std::vector<bandicoot_goto_chain> v;
   if (! is_valid_model_molecule(imol)) return v;
   for (const coot::model_view_atom_tree_chain_t &c :
           graphics_info_t::molecules[imol].model_view_residue_tree_labels()) {
      bandicoot_goto_chain chain;
      chain.label = c.chain_id;
      for (const coot::model_view_atom_tree_item_info_t &r : c.tree_residue)
         chain.residues.push_back({ r.button_label, r.residue_spec.chain_id,
                                    r.residue_spec.ins_code, r.residue_spec.res_no });
      v.push_back(chain);
   }
   return v;
}

std::vector<bandicoot_goto_atom> bandicoot_core_goto_atoms(int imol, const std::string &chain,
                                                           int resno, const std::string &ins_code) {
   std::vector<bandicoot_goto_atom> v;
   if (! is_valid_model_molecule(imol)) return v;
   for (const coot::model_view_atom_button_info_t &a :
           graphics_info_t::molecules[imol].model_view_atom_button_labels(chain, resno, ins_code))
      if (a.atom)
         v.push_back({ a.button_label, a.atom->name, a.atom->altLoc });
   return v;
}

static void goto_centre() {
   graphics_info_t g;
   if (g.try_centre_from_new_go_to_atom())
      g.update_things_on_move_and_redraw();
}

void bandicoot_core_goto_residue(int imol, const bandicoot_goto_residue &residue, bool centre) {
   if (! is_valid_model_molecule(imol)) return;
   coot::residue_spec_t spec(residue.chain, residue.resno, residue.ins_code);
   mmdb::Residue *r = graphics_info_t::molecules[imol].get_residue(spec);
   mmdb::Atom *at = graphics_info_t::molecules[imol].intelligent_this_residue_mmdb_atom(r);
   if (! at) return;
   graphics_info_t g;
   g.set_go_to_atom_molecule(imol);
   g.set_go_to_atom_chain_residue_atom_name(at->GetChainID(), at->GetSeqNum(), at->GetInsCode(),
                                            at->name, at->altLoc);
   bandicoot_wx_goto_target_changed();
   if (centre) goto_centre();
}

void bandicoot_core_goto_atom(int imol, const bandicoot_goto_residue &residue,
                              const bandicoot_goto_atom &atom, bool centre) {
   if (! is_valid_model_molecule(imol)) return;
   graphics_info_t g;
   g.set_go_to_atom_molecule(imol);
   g.set_go_to_atom_chain_residue_atom_name(residue.chain.c_str(), residue.resno,
                                            residue.ins_code.c_str(), atom.name.c_str(),
                                            atom.alt_conf.c_str());
   bandicoot_wx_goto_target_changed();
   if (centre) goto_centre();
}

void bandicoot_core_goto_fields(const std::string &chain, const std::string &residue,
                                const std::string &atom) {
   // as the GTK window's Apply: "CA,A" and "42B" split into their parts
   std::pair<std::string, std::string> name_alt = graphics_info_t::split_atom_name(atom);
   std::pair<std::string, std::string> resno_ins = graphics_info_t::split_resno_inscode(residue);
   graphics_info_t g;
   g.set_go_to_atom_chain_residue_atom_name(chain.c_str(), atoi(resno_ins.first.c_str()),
                                            resno_ins.second.c_str(), name_alt.first.c_str(),
                                            name_alt.second.c_str());
   goto_centre();
}

void bandicoot_core_goto_next_residue(bool next) {
   graphics_info_t g;
   if (next)
      g.intelligent_next_atom_centring(nullptr);
   else
      g.intelligent_previous_atom_centring(nullptr);
}

void bandicoot_core_goto_from_current_position() {
   update_go_to_atom_from_current_position();
}

bool bandicoot_core_goto_position(int &x, int &y) {
   x = graphics_info_t::go_to_atom_window_x_position;
   y = graphics_info_t::go_to_atom_window_y_position;
   return x > -1;  // the GTK window's test
}

void bandicoot_core_save_goto_position(int x, int y) {
   graphics_info_t::go_to_atom_window_x_position = x;
   graphics_info_t::go_to_atom_window_y_position = y;
}

// ---- actions

extern "C" {
   gboolean bandicoot_action_quicksave(gpointer data);
   gboolean bandicoot_action_hydrogen_apply(gpointer data);
   gboolean bandicoot_action_local_probe_dots(gpointer data);
   gboolean bandicoot_action_sphere_refine(gpointer data);
   gboolean bandicoot_action_sphere_refine_plus(gpointer data);
   gboolean bandicoot_action_sphere_regularize(gpointer data);
   gboolean bandicoot_action_sphere_regularize_plus(gpointer data);
   gboolean bandicoot_action_refine_tandem(gpointer data);
   gboolean bandicoot_action_cis_trans(gpointer data);
   gboolean bandicoot_action_backrub_apply(gpointer data);
   gboolean bandicoot_action_interactive_dots_apply(gpointer data);
}

// what the Accept/Reject window's buttons did in the GTK build
static bool run_accept_reject_action(const std::string &action, bool state) {

   if (action == "on_accept_reject_refinement_accept_button_clicked") {
      stop_refinement_internal();
      c_accept_moving_atoms();
   } else if (action == "on_accept_reject_refinement_reject_button_clicked") {
      stop_refinement_internal();
      clear_up_moving_atoms();
   } else if (action == "on_accept_reject_reverse_button_clicked") {
      toggle_torsion_general_reverse();
   } else if (action == "on_accept_reject_refinement_atom_pull_autoclear_checkbutton_toggled") {
      set_auto_clear_atom_pull_restraint(state ? 1 : 0);
   } else if (action == "on_accept_reject_atom_pull_clear_button_clicked") {
      clear_all_atom_pull_restraints();
   } else if (action == "on_accept_reject_flip_this_peptide_button_clicked") {
      pepflip_intermediate_atoms();
   } else if (action == "on_accept_reject_flip_next_peptide_button_clicked") {
      pepflip_intermediate_atoms_other_peptide();
   } else if (action == "on_accept_reject_backrub_rotamer_button_clicked") {
      backrub_rotamer_intermediate_atoms();
   } else if (action == "on_accept_reject_side_chain_180_flip_clicked") {
      side_chain_flip_180_intermediate_atoms();
   } else if (action == "on_accept_reject_crankshaft_button_clicked") {
      crankshaft_peptide_rotation_optimization_intermediate_atoms();
   } else {
      return false;
   }
   return true;
}

bool bandicoot_core_run_action(const std::string &action, bool state) {

   if (run_accept_reject_action(action, state))
      return true;

   if (action == "bandicoot_action_sphere_refine")
      bandicoot_action_sphere_refine(nullptr);
   else if (action == "bandicoot_action_sphere_refine_plus")
      bandicoot_action_sphere_refine_plus(nullptr);
   else if (action == "bandicoot_action_sphere_regularize")
      bandicoot_action_sphere_regularize(nullptr);
   else if (action == "bandicoot_action_sphere_regularize_plus")
      bandicoot_action_sphere_regularize_plus(nullptr);
   else if (action == "on_reset_view_toolbutton_clicked")
      reset_view();
   else if (action == "on_go_to_ligand_toolbutton_clicked")
      go_to_ligand();
   else if (action == "bandicoot_action_cis_trans")
      bandicoot_action_cis_trans(nullptr);
   else if (action == "bandicoot_action_backrub_apply")
      bandicoot_action_backrub_apply(GINT_TO_POINTER(state ? 1 : 0));
   else if (action == "bandicoot_action_interactive_dots_apply")
      bandicoot_action_interactive_dots_apply(GINT_TO_POINTER(state ? 1 : 0));
   else if (action == "bandicoot_action_refine_tandem")
      bandicoot_action_refine_tandem(nullptr);
   else if (action == "bandicoot_action_quicksave")
      bandicoot_action_quicksave(nullptr);
   else if (action == "bandicoot_action_hydrogen_apply")
      bandicoot_action_hydrogen_apply(GINT_TO_POINTER(state ? 1 : 0));
   else if (action == "bandicoot_action_local_probe_dots")
      bandicoot_action_local_probe_dots(nullptr);
   else
      return false;
   return true;
}

std::vector<std::string> bandicoot_core_unsaved_molecules() {

   std::vector<std::string> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (graphics_info_t::molecules[imol].Have_unsaved_changes_p())
         v.push_back(std::to_string(imol) + "  " + graphics_info_t::molecules[imol].name_);
   }
   return v;
}

void bandicoot_core_quit() {
   // Not coot_checked_exit(): its unsaved-changes question is a GTK dialog,
   // which cannot take input while wx runs the event loop, so the wx side asks
   // instead. This is the path Ctrl-C takes; it saves the state and history.
   coot_real_exit(0);
}

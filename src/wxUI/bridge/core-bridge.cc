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

#include <cctype>
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
#include "c-interface-preferences.h"
#include "coot-preferences.h"
#include "cmtz-interface.hh"
#include "cc-interface-scripting.hh"
#include "get-monomer.hh"
#include "read-cif.h"
#include "cc-interface.hh"
#include "drag-and-drop.hh"
#include "c-interface-ligands-swig.hh"
#include "coot-utils/coot-package-paths.hh"
#include "ligand/richardson-rotamer.hh"
#include "coot-utils/coot-coord-utils.hh"

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

// ---- File menu: datasets

bandicoot_dataset_step bandicoot_core_open_dataset(const std::string &file_name) {
   // as the GTK build's dataset chooser and column selector decide
   const char *f = file_name.c_str();
   if (is_mtz_file_p(f)) {
      if (mtz_file_has_phases_p(f))
         return bandicoot_dataset_step::columns;
      calc_phases_generic(f);
      return bandicoot_dataset_step::done;
   }
   std::string ext = coot::util::file_name_extension(file_name);
   if (ext == ".phs" || ext == ".pha") {
      graphics_store_phs_filename(f);
      return bandicoot_dataset_step::phs;
   }
   if (ext == ".fcf" || ext == ".cif") {
      int imol = try_read_cif_file(f);
      if (ext == ".fcf" && is_valid_map_molecule(imol)) {
         graphics_info_t g;
         g.scroll_wheel_map = imol;
      }
      return bandicoot_dataset_step::done;
   }
   return bandicoot_dataset_step::unreadable;
}

static std::vector<std::string> column_labels(const std::vector<coot::mtz_type_label> &v) {
   std::vector<std::string> labels;
   for (const coot::mtz_type_label &l : v) labels.push_back(l.column_label);
   return labels;
}

bool bandicoot_core_mtz_columns(const std::string &file_name, bandicoot_mtz_columns &c) {
   coot::mtz_column_types_info_t info = coot::get_mtz_columns(file_name);
   if (! info.read_success) return false;
   // the amplitudes Coot's expert mode lists: F, then anomalous differences
   c.amplitudes = column_labels(info.f_cols);
   for (const std::string &d : column_labels(info.d_cols)) c.amplitudes.push_back(d);
   c.phases  = column_labels(info.phi_cols);
   c.weights = column_labels(info.weight_cols);
   c.fobs    = column_labels(info.f_cols);
   c.sigfobs = column_labels(info.sigf_cols);
   c.r_free  = column_labels(info.r_free_cols);
   c.preferred_amplitude = info.get_prefered_f_col_idx();
   c.preferred_phase     = info.get_prefered_phi_col_idx();
   return true;
}

int bandicoot_core_make_map_from_mtz(const bandicoot_map_from_mtz &r) {
   return make_and_draw_map_with_reso_with_refmac_params(
      r.file_name.c_str(), r.amplitudes.c_str(), r.phases.c_str(),
      r.use_weights ? r.weights.c_str() : "",
      r.use_weights, r.is_difference_map, r.refmac_labels,
      r.fobs.c_str(), r.sigfobs.c_str(), r.r_free.c_str(), ! r.r_free.empty(),
      0, r.use_resolution_limits, r.low_resolution, r.high_resolution);
}

void bandicoot_core_read_phs(const std::string &file_name, int imol) {
   read_phs_and_make_map_using_cell_symm_from_mol(file_name.c_str(), imol);
}

// ---- File menu: dictionaries

std::vector<std::string> bandicoot_core_dictionary_extensions() {
   return extensions(graphics_info_t::dictionary_glob_extensions);
}

bool bandicoot_core_dictionary_generates_molecule() {
   return graphics_info_t::cif_dictionary_file_selector_create_molecule_flag;
}

void bandicoot_core_import_dictionary(const std::string &file_name, int molecule, bool generate) {
   int imol_enc = molecule;
   if (molecule == -2) imol_enc = coot::protein_geometry::IMOL_ENC_AUTO;
   if (molecule == -1) imol_enc = coot::protein_geometry::IMOL_ENC_ANY;
   if (molecule == -3) imol_enc = coot::protein_geometry::IMOL_ENC_UNSET;
   handle_cif_dictionary_for_molecule(file_name.c_str(), imol_enc, generate ? 1 : 0);
}

// ---- File menu: monomers

int bandicoot_core_get_monomer(const std::string &comp_id) {
   return get_monomer(comp_id);
}

int bandicoot_core_get_monomer_from_network(const std::string &comp_id) {
   return get_monomer_molecule_by_network_and_dict_gen(comp_id);
}

int bandicoot_core_get_sbase_monomer(const std::string &comp_id) {
   return get_sbase_monomer(comp_id.c_str());
}

std::vector<std::pair<std::string, std::string>>
bandicoot_core_search_monomers(const std::string &text, bool minimal, bool sbase) {
   if (sbase) {
      graphics_info_t g;
      return g.Geom_p()->matching_ccp4srs_residues_names(text);
   }
   return monomer_lib_3_letter_codes_matching(text, minimal ? 1 : 0);
}

std::string comp_id_depiction_png(const std::string &comp_id, int imol, unsigned int image_size);

std::string bandicoot_core_monomer_depiction(const std::string &comp_id, unsigned int size) {
   return comp_id_depiction_png(comp_id, 0, size);
}

// ---- File menu: fetching

void bandicoot_core_fetch_by_accession(bandicoot_accession_kind kind, const std::string &code_in) {
   // as the GTK window's Get it: trailing space trimmed, otherwise as typed
   std::string code = coot::util::remove_trailing_whitespace(code_in);
   switch (kind) {
   case bandicoot_accession_kind::pdb:
      get_coords_for_accession_code(code);
      break;
   case bandicoot_accession_kind::pdb_and_map_eds:
      safe_python_command("get_eds_pdb_and_mtz(" + single_quote(code) + ")");
      break;
   case bandicoot_accession_kind::pdb_and_map_pdb_redo:
      safe_python_command("get_pdb_redo(" + single_quote(code) + ")");
      break;
   case bandicoot_accession_kind::alphafold_uniprot:
      fetch_alphafold_model_for_uniprot_id(code);
      break;
   }
}

void bandicoot_core_fetch_and_superpose_alphafold() {
   fetch_and_superpose_alphafold_models_using_active_molecule();
}

// ---- Accept/Reject

static bandicoot_ar_view ar_view;

void bandicoot_core_set_ar_view(const bandicoot_ar_view &view) {
   ar_view = view;
}

static bandicoot_ar_results ar_results(const coot::refinement_results_t &rr) {

   // the stock dialog's wording
   bandicoot_ar_results r;
   // Fits that run no refinement (Torsion General, Rigid Body Fit, ...) hand
   // over empty results, whose default "no progress" is no error of theirs.
   if (! rr.found_restraints_flag) return r;
#ifdef HAVE_GSL
   if (rr.progress == GSL_ENOPROG)       r.status = "Failed - Error: No Progress";
   else if (rr.progress == GSL_CONTINUE) r.status = "Running...";
   else if (rr.progress == GSL_FAILURE)  r.status = "Failure";
   else if (rr.progress == GSL_SUCCESS)  r.status = "Success";
#endif
   for (const coot::refinement_lights_info_t &li : rr.lights) {
      GdkColor c = li.name == "Rama" ? colour_by_rama_plot_distortion(li.value, li.rama_type)
                                     : colour_by_distortion(li.value);
      r.lights.push_back({ li.name, li.label, li.value, (unsigned char) (c.red >> 8),
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

void bandicoot_wx_ar_dismiss() {
   if (ar_view.dismiss) ar_view.dismiss();
}

void bandicoot_wx_ar_settings_changed() {
   if (ar_view.settings_changed) ar_view.settings_changed();
}

bool bandicoot_core_ar_docked() {
   return graphics_info_t::accept_reject_dialog_docked_flag == coot::DIALOG_DOCKED;
}

bool bandicoot_core_ar_always_shown() {
   return graphics_info_t::accept_reject_dialog_docked_show_flag == coot::DIALOG_DOCKED_SHOW;
}

void bandicoot_core_set_ar_docked(bool docked) {
   int state = docked ? coot::DIALOG_DOCKED : coot::DIALOG;
   set_accept_reject_dialog_docked(state);
   preferences_internal_change_value_int(PREFERENCES_ACCEPT_DIALOG_DOCKED, state);
   save_preferences();
}

void bandicoot_core_set_ar_always_shown(bool always) {
   int state = always ? coot::DIALOG_DOCKED_SHOW : coot::DIALOG_DOCKED_HIDE;
   set_accept_reject_dialog_docked_show(state);
   preferences_internal_change_value_int(PREFERENCES_ACCEPT_DIALOG_DOCKED_SHOW, state);
   save_preferences();
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

// ---- the window as a whole

static bandicoot_ui_view ui_view;

void bandicoot_core_set_ui_view(const bandicoot_ui_view &view) {
   ui_view = view;
}

// Coot's text, which may carry Pango markup, as plain text.
static std::string without_markup(const std::string &s) {
   std::string out;
   bool in_tag = false;
   for (char c : s) {
      if (c == '<') in_tag = true;
      else if (c == '>') in_tag = false;
      else if (! in_tag) out += c;
   }
   return out;
}

bool bandicoot_wx_info(const std::string &text, bool markup) {
   if (! ui_view.info) return false;
   ui_view.info(markup ? without_markup(text) : text);
   return true;
}

bool bandicoot_wx_results(const std::string &title, const std::vector<bandicoot_result_row> &rows) {
   if (! ui_view.results) return false;
   ui_view.results(title, rows);
   return true;
}

void bandicoot_core_go_to_result(const bandicoot_result_row &row) {
   // as the GTK list's buttons, recorded before the move
   bandicoot_record_navigation("results-list", row.label.c_str());
   if (row.at_point) {
      set_rotation_centre(row.x, row.y, row.z);
   } else {
      set_go_to_atom_molecule(row.imol);
      set_go_to_atom_chain_residue_atom_name(row.chain.c_str(), row.resno, row.atom.c_str());
   }
}

bool bandicoot_wx_select_map() {
   if (! ui_view.select_map) return false;
   ui_view.select_map();
   return true;
}

int bandicoot_wx_timeout_add(int interval_ms, std::function<bool()> callback) {
   static int last_id = 0;
   if (! ui_view.timeout_add) return 0;
   ui_view.timeout_add(interval_ms, callback);
   return ++last_id;
}

void bandicoot_wx_release_fixed_atom_toggles() {
   if (ui_view.release_fixed_atom_toggles) ui_view.release_fixed_atom_toggles();
}

// ---- refinement parameters, the refinement map, fixed atoms

bandicoot_refine_params bandicoot_core_refine_params() {
   bandicoot_refine_params p;
   p.torsions        = graphics_info_t::do_torsion_restraints;
   p.planar_peptides = planar_peptide_restraints_state();
   p.trans_peptides  = graphics_info_t::do_trans_peptide_restraints;
   p.rama            = graphics_info_t::do_rama_restraints;
   p.secondary_structure = secondary_structure_restraints_type();
   p.weight          = graphics_info_t::geometry_vs_map_weight;
   p.lj_epsilon      = graphics_info_t::lennard_jones_epsilon;
   p.gm_alpha        = graphics_info_t::geman_mcclure_alpha;
   p.torsion_weight  = graphics_info_t::torsion_restraints_weight;
   p.rama_weight     = graphics_info_t::rama_restraints_weight;
   p.more_control_shown = graphics_info_t::refine_params_dialog_extra_control_frame_is_visible;
   return p;
}

void bandicoot_core_set_torsion_restraints(bool on) {
   graphics_info_t::do_torsion_restraints = on ? 1 : 0;
}

void bandicoot_core_set_planar_peptide_restraints(bool on) {
   if (on) add_planar_peptide_restraints(); else remove_planar_peptide_restraints();
}

void bandicoot_core_set_trans_peptide_restraints(bool on) {
   set_use_trans_peptide_restraints(on ? 1 : 0);
}

void bandicoot_core_set_rama_restraints(bool on) {
   set_refine_ramachandran_angles(on ? 1 : 0);
}

void bandicoot_core_set_secondary_structure_restraints(int type) {
   set_secondary_structure_restraints_type(type);
}

void bandicoot_core_set_refinement_weight(float w) {
   set_matrix(w);
}

bool bandicoot_core_estimate_refinement_weight(float &w) {
   graphics_info_t g;
   int imol_map = g.Imol_Refinement_Map();
   if (! is_valid_map_molecule(imol_map)) return false;
   w = g.get_estimated_map_weight(imol_map);
   set_matrix(w);
   return true;
}

void bandicoot_core_set_refine_control(const std::string &which, const std::string &value) {
   // the GTK combo positions are stored too; -1, as these lists are not GTK's
   const char *t = value.c_str();
   if (which == "Overall weight")              set_refinement_overall_weight_from_text(t);
   else if (which == "Lennard-Jones epsilon")  set_refinement_lennard_jones_epsilon_from_text(-1, t);
   else if (which == "Rama Restraints weight") set_refinement_ramachandran_restraints_weight_from_text(-1, t);
   else if (which == "Geman-McClure alpha")    set_refinement_geman_mcclure_alpha_from_text(-1, t);
   else if (which == "Torsions weight")        set_refinement_torsion_weight_from_text(-1, t);
}

void bandicoot_core_set_more_control_shown(bool shown) {
   set_refine_params_dialog_more_control_frame_is_active(shown ? 1 : 0);
}

// A molecule as the GTK choosers label it: number, then the name cut to its
// last characters.
static std::string chooser_label(int imol) {
   const unsigned int n_max = graphics_info_t::go_to_atom_menu_label_n_chars_max;
   std::string name = graphics_info_t::molecules[imol].name_;
   if (name.length() > n_max)
      name = "..." + name.substr(name.length() - n_max);
   return std::to_string(imol) + "  " + name;
}

std::vector<std::pair<int, std::string>> bandicoot_core_maps_for_fitting() {
   std::vector<std::pair<int, std::string>> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_map_molecule(imol))
         v.push_back({ imol, chooser_label(imol) });
   return v;
}

int bandicoot_core_refinement_map() {
   graphics_info_t g;
   return g.Imol_Refinement_Map();
}

void bandicoot_core_set_refinement_map(int imol) {
   graphics_info_t g;
   g.set_refinement_map(imol);
}

void bandicoot_core_fixed_atom_pick(int mode) {
   if (mode == 0) setup_fixed_atom_pick(0, 0);
   else           setup_fixed_atom_pick(1, mode == 2 ? 1 : 0);
}

void bandicoot_core_clear_fixed_atoms() {
   clear_fixed_atoms_all();
}

void bandicoot_wx_release_sidebar_toggle(const std::string &widget) {
   if (ui_view.release_sidebar_toggle) ui_view.release_sidebar_toggle(widget);
}

// ---- windows that edit the moving atoms

static bandicoot_moving_atoms_view ma_view;

void bandicoot_core_set_moving_atoms_view(const bandicoot_moving_atoms_view &view) {
   ma_view = view;
}

bool bandicoot_wx_moving_atoms_window_open() {
   return ma_view.open_windows && ma_view.open_windows();
}

// The core's slider handlers take a GTK adjustment and read its value; a
// bare adjustment (no widget) carries the wx slider's value to them.
template <typename F>
static void with_adjustment(double v, F f) {
   GtkObject *adj = gtk_adjustment_new(v, -1.0e6, 1.0e6, 0.1, 1.0, 0.0);
   g_object_ref_sink(adj);
   f(GTK_ADJUSTMENT(adj));
   g_object_unref(adj);
}

bool bandicoot_wx_rot_trans_present() {
   if (! ma_view.rot_trans) return false;
   ma_view.rot_trans();
   return true;
}

void bandicoot_core_rot_trans_slider(int i, double v) {
   with_adjustment(v, [i](GtkAdjustment *a) {
      graphics_info_t::rot_trans_adjustment_changed(a, GINT_TO_POINTER(i));
   });
}

void bandicoot_core_rot_trans_finish(bool accept) {
   // as the GTK dialog's OK and Cancel
   rot_trans_reset_previous();
   if (accept) {
      accept_regularizement();
      clear_up_moving_atoms();
   } else {
      clear_up_moving_atoms();
      normal_cursor();
   }
}

// The rotamer list as Coot labels it: number, name, probability, chi 1.
static std::vector<std::string> rotamer_labels(int imol, int atom_index) {
   std::vector<std::string> v;
   graphics_info_t g;
   mmdb::Atom *at = graphics_info_t::molecules[imol].atom_sel.atom_selection[atom_index];
   coot::richardson_rotamer d(at->residue, at->altLoc, graphics_info_t::molecules[imol].atom_sel.mol,
                              g.rotamer_lowest_probability, 0);
   std::vector<float> p = d.probabilities();
   for (unsigned int i=0; i<p.size(); i++)
      v.push_back(coot::util::int_to_string(i+1) + ":  " + d.rotamer_name(i) + "  "
                  + coot::util::float_to_string(p[i]) + "% Chi_1 = "
                  + coot::util::float_to_string(d.Chi1(i)));
   return v;
}

static int rotamer_imol = -1;
static bool rotamer_alt_conf = false;

bool bandicoot_wx_rotamers_present(int imol, int atom_index, bool alt_conf) {
   if (! ma_view.rotamers) return false;
   graphics_info_t g;
   rotamer_imol = imol;
   rotamer_alt_conf = alt_conf;
   std::vector<std::string> labels = rotamer_labels(imol, atom_index);
   // the first rotamer is shown at once; none for GLY, ALA and the like
   if (g.generate_moving_atoms_from_rotamer(0))
      ma_view.rotamers(labels, alt_conf, g.add_alt_conf_new_atoms_occupancy);
   return true;
}

void bandicoot_core_rotamer_show(int i) {
   graphics_info_t g;
   g.generate_moving_atoms_from_rotamer(i);
}

void bandicoot_core_rotamer_occupancy(float v) {
   with_adjustment(v, [](GtkAdjustment *a) {
      graphics_info_t::new_alt_conf_occ_adjustment_changed(a, nullptr);
   });
}

extern "C" void bandicoot_clear_probe_dot_objects(void);

void bandicoot_core_rotamer_finish(bool accept) {
   // as the GTK dialog's OK and Cancel
   if (accept) {
      accept_regularizement();
      clear_moving_atoms_object();
   } else {
      clear_up_moving_atoms();
      bandicoot_clear_probe_dot_objects();
      graphics_draw();
      if (rotamer_alt_conf) {
         // the new alt conf goes again
         set_undo_molecule(rotamer_imol);
         apply_undo();
      }
   }
}

// The rotatable torsions as Coot lists them, in the order the chi indexing
// expects (non-const torsions of the monomer's restraints).
static std::string chi_res_type;
static int chi_mode = 0;

static std::vector<std::string> chi_labels(const std::string &res_type) {
   std::vector<std::string> v;
   graphics_info_t g;
   std::pair<short int, coot::dictionary_residue_restraints_t> p =
      g.Geom_p()->get_monomer_restraints(res_type, 0);
   if (! p.first) return v;
   for (const coot::dict_torsion_restraint_t &t :
           p.second.get_non_const_torsions(graphics_info_t::find_hydrogen_torsions_flag)) {
      if (t.is_const()) continue;
      v.push_back("  " + t.id() + "  " + t.atom_id_2_4c() + " <--> " + t.atom_id_3_4c()
                  + "  ref: " + coot::util::float_to_string(t.angle())
                  + "  per: " + coot::util::int_to_string(t.periodicity()));
   }
   return v;
}

int bandicoot_wx_edit_chi_present(const std::string &res_type, int mode) {
   if (! ma_view.edit_chi) return -2;
   chi_res_type = res_type;
   chi_mode = mode;
   graphics_info_t g;
   std::pair<short int, coot::dictionary_residue_restraints_t> p =
      g.Geom_p()->get_monomer_restraints(res_type, 0);
   if (! p.first) return -1;  // as GTK: no restraints, no torsions
   std::vector<std::string> labels = chi_labels(res_type);
   ma_view.edit_chi(labels, mode, graphics_info_t::find_hydrogen_torsions_flag);
   return int(labels.size());
}

void bandicoot_core_chi_choose(int i) {
   graphics_info_t g;
   g.edit_chi_current_chi = i + 1;
   g.in_edit_chi_mode_flag = 1;
   if (chi_mode == static_cast<int>(graphics_info_t::RESIDUE_PARTIAL_ALT_LOCS)) {
      g.residue_partial_alt_locs_split_residue(i, false);
      graphics_draw();
   }
}

void bandicoot_core_chi_flash(int i) {
   graphics_info_t g;
   if (chi_mode == static_cast<int>(graphics_info_t::RESIDUE_PARTIAL_ALT_LOCS))
      g.setup_flash_bond(graphics_info_t::imol_residue_partial_alt_locs,
                         graphics_info_t::residue_partial_alt_locs_spec, i);
   else
      g.setup_flash_bond_using_moving_atom_internal(i);
}

std::vector<std::string> bandicoot_core_chi_hydrogens(bool on) {
   set_find_hydrogen_torsions(on ? 1 : 0);
   return chi_labels(chi_res_type);
}

void bandicoot_core_chi_reverse(bool on) {
   set_edit_chi_angles_reverse_fragment_state(on ? 1 : 0);
}

void bandicoot_core_chi_view_rotation() {
   set_graphics_edit_current_chi(0);
}

void bandicoot_core_chi_finish(bool accept) {
   // as the GTK dialog's Apply and Cancel, then its closing
   if (accept) {
      accept_regularizement();
   } else {
      clear_up_moving_atoms();
      bandicoot_clear_probe_dot_objects();
      graphics_draw();
   }
   unset_moving_atom_move_chis();
   set_show_chi_angle_bond(0);
}

bool bandicoot_wx_edit_backbone_present() {
   if (! ma_view.edit_backbone) return false;
   ma_view.edit_backbone();
   return true;
}

void bandicoot_core_backbone_peptide(double degrees) {
   with_adjustment(degrees, [](GtkAdjustment *a) {
      graphics_info_t::edit_backbone_peptide_changed_func(a, nullptr);
   });
}

void bandicoot_core_backbone_carbonyl(double degrees) {
   with_adjustment(degrees, [](GtkAdjustment *a) {
      graphics_info_t::edit_backbone_carbonyl_changed_func(a, nullptr);
   });
}

void bandicoot_core_backbone_finish(bool accept) {
   // as the GTK dialog's OK and Cancel, then its closing
   if (accept) accept_regularizement();
   destroy_edit_backbone_rama_plot();
   clear_up_moving_atoms();
}

// ---- choosers

static bandicoot_chooser_view chooser_view;

void bandicoot_core_set_chooser_view(const bandicoot_chooser_view &view) {
   chooser_view = view;
}

bool bandicoot_wx_residue_chooser(bool show_stub) {
   if (! chooser_view.residue_type) return false;
   chooser_view.residue_type(show_stub);
   return true;
}

bool bandicoot_wx_base_chooser() {
   if (! chooser_view.base) return false;
   chooser_view.base();
   return true;
}

bool bandicoot_wx_pointer_atom_type() {
   if (! chooser_view.pointer_atom_type) return false;
   // the molecule last chosen, else the first model, as Coot's menu shows
   int imol = graphics_info_t::user_pointer_atom_molecule;
   if (! is_valid_model_molecule(imol)) imol = first_coords_imol();
   chooser_view.pointer_atom_type(bandicoot_core_goto_molecules(), imol);
   return true;
}

bool bandicoot_wx_add_alt_conf(int split_type) {
   if (! chooser_view.add_alt_conf) return false;
   chooser_view.add_alt_conf(split_type);
   return true;
}

void bandicoot_wx_add_alt_conf_done() {
   if (chooser_view.add_alt_conf_done) chooser_view.add_alt_conf_done();
}

void bandicoot_core_mutate(const std::string &res_type, bool stub) {
   do_mutation(res_type.c_str(), stub ? 1 : 0);
}

bool bandicoot_core_mutate_by_letter(char code, bool stub) {
   std::string res_type = coot::util::single_letter_to_3_letter_code(toupper(code));
   if (! coot::util::is_standard_amino_acid_name(res_type)) return false;
   bandicoot_core_mutate(res_type, stub);
   return true;
}

void bandicoot_core_mutate_base(const std::string &base) {
   do_base_mutation(base.c_str());
}

void bandicoot_core_base_chooser_closed() {
   clear_pending_picks();
}

void bandicoot_core_place_atom(int imol, const std::string &type) {
   // the molecule shown is the one added to
   graphics_info_t::user_pointer_atom_molecule = imol;
   place_typed_atom_at_pointer(type.c_str());
}

void bandicoot_core_set_alt_conf_split_type(int split_type) {
   set_add_alt_conf_split_type_number(split_type);
}

void bandicoot_core_cancel_add_alt_conf() {
   unset_add_alt_conf_define();
   graphics_info_t::pick_pending_flag = 0;
}

// ---- Delete item, Find Waters

static bandicoot_delete_view delete_view;

void bandicoot_core_set_delete_view(const bandicoot_delete_view &view) {
   delete_view = view;
}

bool bandicoot_wx_delete_open() {
   return delete_view.is_open && delete_view.is_open();
}

bool bandicoot_wx_delete_keep_active() {
   return delete_view.keep_active && delete_view.keep_active();
}

// closed by the core after a pick: as Coot's dialog going, which ended the pick
void bandicoot_wx_delete_close() {
   if (delete_view.close) delete_view.close(true);
}

// closed because refine or regularize was chosen: their pick is already armed
void bandicoot_wx_delete_close_quietly() {
   if (delete_view.close) delete_view.close(false);
}

bool bandicoot_wx_delete_present() {
   if (! delete_view.open) return false;
   delete_view.open(bandicoot_core_delete_begin());
   return true;
}

int bandicoot_core_delete_begin() {
   // the kind last used, as Coot's dialog chose its first radio
   int kind = 0;
   if (delete_item_mode_is_atom_p())                { set_delete_atom_mode(); kind = 2; }
   else if (delete_item_mode_is_water_p())          { set_delete_water_mode(); kind = 1; }
   else if (delete_item_mode_is_sidechain_p())      { set_delete_sidechain_mode(); kind = 3; }
   else if (delete_item_mode_is_chain_p())          { set_delete_chain_mode(); kind = 5; }
   else if (delete_item_mode_is_sidechain_range_p()) { set_delete_sidechain_range_mode(); kind = 4; }
   else                                             { set_delete_residue_mode(); kind = 0; }
   graphics_info_t::delete_item_water = (kind == 1);  // see bandicoot_core_delete_kind
   graphics_info_t::pick_pending_flag = 1;
   pick_cursor_maybe();
   return kind;
}

void bandicoot_core_delete_kind(int kind) {
   switch (kind) {
   case 0: set_delete_residue_mode(); break;
   case 1: set_delete_water_mode(); break;
   case 2: set_delete_atom_mode(); break;
   case 3: set_delete_sidechain_mode(); break;
   case 4: set_delete_sidechain_range_mode(); break;
   case 5: set_delete_chain_mode(); break;
   case 6: set_delete_residue_hydrogens_mode(); break;
   case 7: set_delete_residue_zone_mode(); break;
   }
   // set_delete_atom_mode() leaves the water flag as it was, so one click
   // could delete as both; only the chosen kind is on
   if (kind != 1) graphics_info_t::delete_item_water = 0;
}

void bandicoot_core_delete_closed() {
   // as the GTK dialog's Close and its going
   clear_pending_delete_item();
   clear_pending_picks();
   normal_cursor();
}

static std::vector<std::pair<int, std::string>> full_name_list(bool maps) {
   // as Coot's Find Waters lists them: number and full name
   std::vector<std::pair<int, std::string>> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      const molecule_class_info_t &m = graphics_info_t::molecules[imol];
      bool offered = maps ? m.has_xmap() : m.atom_sel.n_selected_atoms > 0;
      if (offered) v.push_back({ imol, std::to_string(imol) + " " + m.name_ });
   }
   return v;
}

std::vector<std::pair<int, std::string>> bandicoot_core_find_waters_maps() {
   return full_name_list(true);
}

std::vector<std::pair<int, std::string>> bandicoot_core_find_waters_models() {
   return full_name_list(false);
}

// ---- Run Refmac

// defined with Coot's Run Refmac dialog
void simple_refmac_run(int imol_coords, const std::string &mtz_in_filename);

std::vector<std::pair<int, std::string>> bandicoot_core_refmac_models() {
   std::vector<std::pair<int, std::string>> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_model_molecule(imol))
         v.push_back({ imol, chooser_label(imol) });
   return v;
}

std::string bandicoot_core_refmac_mtz() {
   return graphics_info_t::mtz_file_for_refmac;
}

void bandicoot_core_run_refmac(int imol, const std::string &mtz_file_name) {
   simple_refmac_run(imol, mtz_file_name);
}

bandicoot_find_waters_values bandicoot_core_find_waters_values() {
   return { graphics_info_t::find_waters_sigma_cut_off,
            graphics_info_t::ligand_water_to_protein_distance_lim_min,
            graphics_info_t::ligand_water_to_protein_distance_lim_max };
}

void bandicoot_core_find_waters(int imol_map, int imol_model, bool new_molecule,
                                const bandicoot_find_waters_values &values) {
   // kept for next time, as Coot's dialog did
   set_value_for_find_waters_sigma_cut_off(values.rmsd);
   set_ligand_water_to_protein_distance_limits(values.min_distance, values.max_distance);
   execute_find_waters_real(imol_map, imol_model, new_molecule ? 1 : 0, values.rmsd);
   graphics_draw();
}

bool bandicoot_wx_blobs(const std::vector<std::vector<float>> &blobs) {
   if (! delete_view.blobs) return false;
   delete_view.blobs(blobs);
   return true;
}

void bandicoot_core_centre_on(float x, float y, float z) {
   set_rotation_centre(x, y, z);
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

// ---- Sequence View

static bandicoot_sequence_view_hooks seq_hooks;

void bandicoot_core_set_sequence_view_hooks(const bandicoot_sequence_view_hooks &hooks) {
   seq_hooks = hooks;
}

void bandicoot_wx_sequence_view_changed(int imol) {
   if (seq_hooks.model_changed) seq_hooks.model_changed(imol);
}

void bandicoot_wx_sequence_view_highlight(int imol, mmdb::Atom *at) {
   if (! at || ! seq_hooks.highlight) return;
   seq_hooks.highlight(imol, at->GetChainID(), at->GetSeqNum(), at->GetInsCode());
}

// Coot's colours by secondary structure (X11 names in Coot's view).
static void secondary_structure_colour(int sse, bandicoot_seq_residue &r) {
   unsigned int rgb = 0x000000;
   switch (sse) {
   case mmdb::SSE_Strand: rgb = 0xcd2626; break;  // firebrick3
   case mmdb::SSE_Bulge:  rgb = 0xff3030; break;  // firebrick1
   case mmdb::SSE_3Turn:  rgb = 0x0000cd; break;  // MediumBlue
   case mmdb::SSE_4Turn:  rgb = 0x36648b; break;  // SteelBlue4
   case mmdb::SSE_5Turn:  rgb = 0x104e8b; break;  // DodgerBlue4
   case mmdb::SSE_Helix:  rgb = 0x000080; break;  // navy
   default: break;
   }
   r.red = (rgb >> 16) & 0xff;
   r.green = (rgb >> 8) & 0xff;
   r.blue = rgb & 0xff;
}

bandicoot_sequence bandicoot_core_sequence(int imol) {
   bandicoot_sequence seq;
   if (! is_valid_model_molecule(imol)) return seq;
   molecule_class_info_t &m = graphics_info_t::molecules[imol];
   seq.title = "Molecule Number " + std::to_string(imol) + ": " + m.name_for_display_manager();
   mmdb::Manager *mol = m.atom_sel.mol;
   mmdb::Model *model_p = mol ? mol->GetModel(1) : nullptr;
   if (! model_p) return seq;
   model_p->CalcSecStructure(1);
   int nchains = model_p->GetNumberOfChains();
   for (int ich=0; ich<nchains; ich++) {
      mmdb::Chain *chain_p = model_p->GetChain(ich);
      bandicoot_seq_chain c;
      c.chain_id = chain_p->GetChainID();
      int nres = chain_p->GetNumberOfResidues();
      for (int ir=0; ir<nres; ir++) {
         mmdb::Residue *r = chain_p->GetResidue(ir);
         if (! r) continue;
         bandicoot_seq_residue sr;
         sr.resno = r->GetSeqNum();
         sr.ins_code = r->GetInsCode();
         sr.res_name = r->GetResName();
         std::string one = coot::util::three_letter_to_one_letter_with_specials(sr.res_name);
         sr.letter = one.empty() ? 'X' : one[0];
         secondary_structure_colour(r->SSE, sr);
         c.residues.push_back(sr);
      }
      seq.chains.push_back(c);
   }
   return seq;
}

void bandicoot_core_sequence_view_centre(int imol, const std::string &chain, int resno,
                                         const std::string &ins_code) {
   bandicoot_core_goto_residue(imol, { std::string(), chain, ins_code, resno }, true);
}

extern "C" void bandicoot_save_sequence_view_docked(int docked);
extern "C" int  bandicoot_load_sequence_view_docked(void);
extern "C" int  bandicoot_load_sequence_view_max_chains(void);

bool bandicoot_core_sequence_view_docked() {
   return bandicoot_load_sequence_view_docked() != 0;
}

void bandicoot_core_save_sequence_view_docked(bool docked) {
   bandicoot_save_sequence_view_docked(docked ? 1 : 0);
}

int bandicoot_core_sequence_view_max_chains() {
   return bandicoot_load_sequence_view_max_chains();
}

void bandicoot_wx_goto_model_changed(int imol) {
   if (goto_view.model_changed) goto_view.model_changed(imol);
   bandicoot_wx_sequence_view_changed(imol);
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
   if (seq_hooks.molecules_changed) seq_hooks.molecules_changed();
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
   std::vector<std::pair<int, std::string>> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (graphics_info_t::molecules[imol].has_model())
         v.push_back({ imol, chooser_label(imol) });
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

// Centres on the Go To target, which becomes the sequence views' current
// residue, as centring by the pointer or the keyboard makes it.
static void goto_centre() {
   graphics_info_t g;
   if (! g.try_centre_from_new_go_to_atom()) return;
   g.update_things_on_move_and_redraw();
   bandicoot_goto_target t = bandicoot_core_goto_target();
   if (seq_hooks.highlight && t.imol >= 0)
      seq_hooks.highlight(t.imol, t.chain, t.resno, t.ins_code);
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

// The sidebar's tools that need no dialog of their own, as the GTK model
// toolbar's callbacks run them. A toggle's state arms or disarms its pick.
static bool run_sidebar_action(const std::string &action, bool state) {

   int s = state ? 1 : 0;
   if (action == "on_model_toolbar_refine_togglebutton_toggled")
      do_refine(s);
   else if (action == "on_model_toolbar_regularize_togglebutton_toggled")
      do_regularize(s);
   else if (action == "on_model_toolbar_rigid_body_fit_togglebutton_toggled")
      do_rigid_body_refine(s);
   else if (action == "on_model_toolbar_auto_fit_rotamer_togglebutton_toggled")
      setup_auto_fit_rotamer(s);
   else if (action == "on_model_toolbar_torsion_general_toggletoolbutton_toggled")
      setup_torsion_general(s);
   else if (action == "on_model_toolbar_flip_peptide_togglebutton_toggled")
      do_pepflip(s);
   else if (action == "on_model_toolbar_sidechain_180_togglebutton_toggled")
      setup_180_degree_flip(s);
   else if (action == "on_model_toolbar_add_terminal_residue_togglebutton_toggled")
      do_add_terminal_residue(s);
   else if (action == "on_model_toolbar_clear_pending_picks_button_clicked")
      clear_pending_picks();
   else if (action == "on_model_toolbar_undo_button_clicked")
      apply_undo();
   else if (action == "on_model_toolbar_redo_button_clicked")
      apply_redo();
   else if (action == "on_model_toolbar_select_map_button_clicked")
      show_select_map_dialog();  // the wx chooser, through the core's hook
   else if (action == "on_model_toolbar_rot_trans_toolbutton_clicked")
      do_rot_trans_setup(1);
   else if (action == "on_model_toolbar_rotamers_togglebutton_toggled")
      setup_rotamers(s);
   else if (action == "on_model_toolbar_edit_chi_angles_togglebutton_toggled") {
      setup_edit_chi_angles(s);
      if (! s) set_show_chi_angle_bond(0);
   }
   else if (action == "on_model_toolbar_edit_backbone_torsions_toggletoolbutton_toggled")
      setup_backbone_torsion_edit(s);
   else if (action == "on_model_toolbar_mutate_and_autofit_togglebutton_toggled")
      setup_mutate_auto_fit(s);
   else if (action == "on_model_toolbar_simple_mutate_togglebutton_toggled")
      setup_mutate(s);
   else if (action == "on_model_toolbar_add_alt_conf_toolbutton_clicked")
      altconf();
   else if (action == "on_model_toolbar_add_atom_button_clicked")
      place_atom_at_pointer();
   else if (action == "on_model_toolbar_delete_button_clicked")
      post_delete_item_dialog();  // the wx window, through the core's hook
   else
      return false;
   return true;
}

void bandicoot_core_rot_trans_by(int which) {
   // Coot's ROT_TRANS_TYPE_ZONE, _CHAIN, _MOLECULE are 12, 13, 14
   set_rot_trans_object_type(12 + which);
   do_rot_trans_setup(1);
}

bool bandicoot_core_run_action(const std::string &action, bool state) {

   if (run_accept_reject_action(action, state))
      return true;
   if (run_sidebar_action(action, state))
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

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

#include <algorithm>
#include <map>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <tuple>
#include <sys/stat.h>
#include <unistd.h>

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
#include "callbacks.h"   // the Modelling menu's op ids
#include "coot-preferences.h"
#include "cmtz-interface.hh"
#include "cc-interface-scripting.hh"
#include "get-monomer.hh"
#include "read-cif.h"
#include "cc-interface.hh"
#include "drag-and-drop.hh"
#include "save-coords-options.hh"
#include "ligand/primitive-chi-angles.hh"
#include "c-interface-ligands-swig.hh"
#include "c-interface-generic-objects.h"
#include "coot-utils/coot-package-paths.hh"
#include "ligand/richardson-rotamer.hh"
#include "ligand/ligand.hh"
#include "coot-utils/peak-search.hh"
#include "coot-utils/coot-coord-utils.hh"
#include "coot-utils/mmcif-header-view.hh"
#include "coords/mmdb.h"
#include "utils/coot-utils.hh"

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
         v.push_back({ i.icon_widget, i.show_hide_flag == 1, i.default_show_flag == 1,
                       i.icon_text, i.icon_filename });
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

// ---- File menu: saving coordinates

// The save panel enforces a filter's extensions, so only the plain ones are
// offered: no compressed variants, no case duplicates.
static std::vector<bandicoot_coords_format> coords_formats(bool symmetry) {
   std::vector<bandicoot_coords_format> v;
   for (const auto &e : coot::save_coords_options_t::menu_entries(symmetry)) {
      bandicoot_coords_format f;
      f.label = e.second;
      for (const std::string &p : coot::save_coords_options_t::filename_patterns_for(e.first)) {
         std::string ext = p.substr(1);  // "*.pdb" -> ".pdb"
         if (ext.find('.', 1) != std::string::npos) continue;
         if (coot::util::downcase(ext) != ext) continue;
         f.extensions.push_back(ext);
      }
      v.push_back(f);
   }
   return v;
}

static int format_index(coot::coord_file_format_t format, bool symmetry) {
   const auto entries = coot::save_coords_options_t::menu_entries(symmetry);
   for (unsigned int i = 0; i < entries.size(); i++)
      if (entries[i].first == format) return int(i);
   return 0;
}

// Where coordinates were last saved; the working directory until then, as
// in Coot. Only the GTK choosers read the core's own record of it.
static std::string last_save_directory;

static std::string save_directory() {
   return last_save_directory.empty() ? coot::util::current_working_dir() : last_save_directory;
}

// A model is saved in the format it was read in, unless the user picks another.
static coot::coord_file_format_t own_format(int imol) {
   return graphics_info_t::molecules[imol].get_input_molecule_was_in_mmcif_state()
      ? coot::coord_file_format_t::MMCIF : coot::coord_file_format_t::PDB;
}

static bandicoot_save_coords save_request(int imol, bool symmetry, coot::coord_file_format_t format,
                                          const std::string &file_name) {
   bandicoot_save_coords r;
   r.imol = imol;
   r.symmetry = symmetry;
   r.formats = coords_formats(symmetry);
   r.format = format_index(format, symmetry);
   r.file_name = coot::save_coords_options_t::apply_format_to_filename(file_name, format);
   r.directory = save_directory();
   return r;
}

int bandicoot_core_save_coords_default_molecule() {
   int imol = first_unsaved_coords_imol();
   return imol >= 0 ? imol : first_coords_imol();
}

bandicoot_save_coords bandicoot_core_save_coords_request(int imol) {
   if (! is_valid_model_molecule(imol)) return bandicoot_save_coords();
   graphics_info_t::save_imol = imol;
   return save_request(imol, false, own_format(imol),
                       graphics_info_t::molecules[imol].stripped_save_name_suggestion());
}

// The symmetry mate last picked: its operator and shifts.
static coot::save_coords_options_t picked_symmetry;

bool bandicoot_core_save_coords(const bandicoot_save_coords &request, const std::string &path) {
   coot::save_coords_options_t opts;
   if (request.symmetry) opts = picked_symmetry;
   opts.imol = request.imol;
   opts.is_symmetry = request.symmetry;
   const auto entries = coot::save_coords_options_t::menu_entries(request.symmetry);
   if (request.format >= 0 && request.format < int(entries.size()))
      opts.format = entries[request.format].first;
   opts.hydrogens = request.hydrogens;
   opts.aniso = request.aniso;
   opts.conect = graphics_info_t::write_conect_records_flag;

   std::string file_name = coot::save_coords_options_t::apply_format_to_filename(path, opts.format);
   if (file_name != path)
      std::cout << "INFO:: adjusting " << path << " to " << file_name
                << " to match the chosen format" << std::endl;
   last_save_directory = coot::util::file_name_directory(file_name);
   return opts.execute(file_name) == 0;
}

static bandicoot_save_view save_view;

void bandicoot_core_set_save_view(const bandicoot_save_view &view) {
   save_view = view;
}

void bandicoot_core_pick_symmetry_save() {
   setup_save_symmetry_coords();
}

bool bandicoot_wx_save_symmetry_coords(const coot::save_coords_options_t &opts,
                                       const std::string &file_name) {
   if (! save_view.symmetry_picked) return false;
   picked_symmetry = opts;
   save_view.symmetry_picked(save_request(opts.imol, true, opts.format, file_name));
   return true;
}

// ---- File menu: state, recovery, maps, deleting

std::string bandicoot_core_save_state_name() {
   return graphics_info_t::save_state_file_name;
}

void bandicoot_core_save_state(const std::string &path) {
   save_state_file(path.c_str());
   set_save_state_file_name(path.c_str());
}

std::vector<bandicoot_backup> bandicoot_core_recoverable_backups() {
   std::vector<bandicoot_backup> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (! graphics_info_t::molecules[imol].has_model()) continue;
      coot::backup_file_info info = graphics_info_t::molecules[imol].recent_backup_file_info();
      if (! info.status) continue;
      bandicoot_backup b;
      b.imol = imol;
      b.name = info.name;
      b.backup_file_name = info.backup_file_name;
      v.push_back(b);
   }
   return v;
}

void bandicoot_core_recover_from_backup(const bandicoot_backup &backup) {
   if (! is_valid_model_molecule(backup.imol)) return;
   graphics_info_t::molecules[backup.imol].execute_restore_from_recent_backup(
      backup.backup_file_name, coot::util::current_working_dir());
   graphics_draw();
}

int bandicoot_core_export_map_default() {
   return imol_refinement_map();
}

bool bandicoot_core_export_map(int imol, const std::string &path) {
   return export_map(imol, path.c_str()) == 1;
}

bool bandicoot_core_export_map_fragment(int imol, float radius, const std::string &path) {
   graphics_info_t g;
   coot::Cartesian rc = g.RotationCentre();
   return export_map_fragment(imol, rc.x(), rc.y(), rc.z(), radius, path.c_str()) == 1;
}

static bool has_model_or_map(int imol) {
   const molecule_class_info_t &m = graphics_info_t::molecules[imol];
   return m.has_model() || m.has_xmap() || m.has_nxmap();
}

std::vector<std::pair<int, std::string>> bandicoot_core_models_and_maps() {
   std::vector<std::pair<int, std::string>> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (has_model_or_map(imol))
         v.push_back({ imol, std::to_string(imol) + "  " +
                             graphics_info_t::molecules[imol].name_for_display_manager() });
   return v;
}

// ---- Edit menu: model edits

std::vector<std::string> bandicoot_core_chain_ids(int imol) {
   std::vector<std::string> v;
   if (! is_valid_model_molecule(imol)) return v;
   mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
   mmdb::Model *model = mol ? mol->GetModel(1) : nullptr;
   if (! model) return v;
   for (int i=0; i<model->GetNumberOfChains(); i++)
      if (mmdb::Chain *chain = model->GetChain(i))
         v.push_back(chain->GetChainID());
   return v;
}

bool bandicoot_core_change_chain_id(int imol, const std::string &from_chain,
                                    const std::string &to_chain, bool use_range,
                                    int from_resno, int to_resno, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) return false;
   std::pair<int, std::string> r =
      graphics_info_t::molecules[imol].change_chain_id(from_chain, to_chain, use_range,
                                                       from_resno, to_resno);
   if (r.first != 1) {
      why_not = r.second;
      return false;
   }
   update_go_to_atom_window_on_changed_mol(imol);
   graphics_draw();
   return true;
}

int bandicoot_core_copy_molecule(int imol) {
   return is_valid_model_molecule(imol) ? copy_molecule(imol) : -1;
}

int bandicoot_core_copy_fragment(int imol, const std::string &atom_selection) {
   return is_valid_model_molecule(imol)
      ? new_molecule_by_atom_selection(imol, atom_selection.c_str()) : -1;
}

int bandicoot_core_merge_target_default() {
   int imol = graphics_info_t::merge_molecules_master_molecule;
   return is_valid_model_molecule(imol) ? imol : first_coords_imol();
}

void bandicoot_core_merge_molecules(const std::vector<int> &add, int into) {
   if (add.empty() || ! is_valid_model_molecule(into)) return;
   graphics_info_t::merge_molecules_master_molecule = into;
   if (merge_molecules_by_vector(add, into).first)
      graphics_draw();
}

bool bandicoot_core_replace_fragment(int imol_target, int imol_fragment,
                                     const std::string &atom_selection, std::string &why_not) {
   if (! is_valid_model_molecule(imol_target) || ! is_valid_model_molecule(imol_fragment)) {
      why_not = "not a model molecule";
      return false;
   }
   // the core reports success even when the selection is empty
   mmdb::Manager *mol = graphics_info_t::molecules[imol_fragment].atom_sel.mol;
   int handle = mol->NewSelection();
   mol->Select(handle, mmdb::STYPE_ATOM, atom_selection.c_str(), mmdb::SKEY_OR);
   int n_selected = mol->GetSelLength(handle);
   mol->DeleteSelection(handle);
   if (n_selected == 0) {
      why_not = "no atoms in selection " + atom_selection;
      return false;
   }
   replace_fragment(imol_target, imol_fragment, atom_selection.c_str());
   return true;
}

bool bandicoot_core_replace_active_residue(const std::string &comp_id, std::string &why_not) {
   std::pair<bool, std::pair<int, coot::atom_spec_t>> aa = active_atom_spec();
   if (! aa.first) {
      why_not = "no active residue";
      return false;
   }
   int imol = aa.second.first;
   const coot::atom_spec_t &spec = aa.second.second;
   // mutate_by_overlap reports its failures only on the console, so success
   // is judged by the residue's new type
   safe_python_command("mutate_by_overlap(" + std::to_string(imol) + ", " + single_quote(spec.chain_id) +
                       ", " + std::to_string(spec.res_no) + ", " + single_quote(comp_id) + ")");
   std::string now = residue_name(imol, spec.chain_id.c_str(), spec.res_no, "");
   if (now != comp_id) {
      why_not = spec.chain_id + " " + std::to_string(spec.res_no) + " is still " + now;
      return false;
   }
   return true;
}

bool bandicoot_core_active_residue_number(int &resno) {
   std::pair<bool, std::pair<int, coot::atom_spec_t>> aa = active_atom_spec();
   if (! aa.first) return false;
   resno = aa.second.second.res_no;
   return true;
}

bool bandicoot_core_renumber_residues(const bandicoot_renumber &r) {
   if (! is_valid_model_molecule(r.imol)) return false;
   mmdb::Chain *chain_p = graphics_info_t::molecules[r.imol].get_chain(r.chain);
   if (! chain_p) return false;
   int start = r.start, end = r.end;
   if (r.from_n_terminus) {
      std::pair<bool, int> n = coot::util::min_resno_in_chain(chain_p);
      if (! n.first) return false;
      start = n.second;
   }
   if (r.to_c_terminus) {
      std::pair<bool, int> c = coot::util::max_resno_in_chain(chain_p);
      if (! c.first) return false;
      end = c.second;
   }
   return renumber_residue_range(r.imol, r.chain.c_str(), start, end, r.offset) != 0;
}

// ---- Edit menu: display settings

std::vector<bandicoot_bond_colour> bandicoot_core_bond_colours() {
   std::vector<bandicoot_bond_colour> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (! graphics_info_t::molecules[imol].has_model()) continue;
      bandicoot_bond_colour c;
      c.imol = imol;
      c.label = "Molecule " + std::to_string(imol) + " " +
                graphics_info_t::molecules[imol].name_for_display_manager();
      c.rotation = graphics_info_t::molecules[imol].bonds_colour_map_rotation;
      v.push_back(c);
   }
   return v;
}

void bandicoot_core_set_bond_colour_rotation(int imol, float degrees) {
   if (! is_valid_model_molecule(imol)) return;
   graphics_info_t::molecules[imol].bonds_colour_map_rotation = degrees;
   graphics_draw();
}

bool bandicoot_core_bond_colour_carbons_only() {
   return graphics_info_t::rotate_colour_map_on_read_pdb_c_only_flag;
}

void bandicoot_core_set_bond_colour_carbons_only(bool on) {
   set_colour_map_rotation_on_read_pdb_c_only_flag(on ? 1 : 0);
}

int bandicoot_core_bond_parameters_molecule() {
   int imol = graphics_info_t::bond_parameters_molecule;
   return is_valid_model_molecule(imol) ? imol : first_coords_imol();
}

void bandicoot_core_set_bond_parameters_molecule(int imol) {
   graphics_info_t::bond_parameters_molecule = imol;
}

int bandicoot_core_bond_width(int imol) {
   return is_valid_model_molecule(imol) ? graphics_info_t::molecules[imol].bond_thickness() : 3;
}

void bandicoot_core_set_bond_width(int imol, int width) {
   if (! is_valid_model_molecule(imol)) return;
   set_bond_thickness(imol, width);
   // intermediate atoms stay fatter than the bonds they would hide behind
   if (graphics_info_t::bond_thickness_intermediate_atoms < width + 2)
      graphics_info_t::bond_thickness_intermediate_atoms = width + 2;
}

bool bandicoot_core_draws_hydrogens(int imol) {
   return is_valid_model_molecule(imol) && graphics_info_t::molecules[imol].draw_hydrogens();
}

void bandicoot_core_set_draws_hydrogens(int imol, bool on) {
   if (! is_valid_model_molecule(imol)) return;
   set_draw_hydrogens(imol, on ? 1 : 0);
   graphics_info_t g;
   g.update_environment_distances_by_rotation_centre_maybe(imol);
   graphics_draw();
}

bandicoot_rgb bandicoot_core_map_colour(int imol) {
   bandicoot_rgb c;
   if (! is_valid_map_molecule(imol)) return c;
   double **col = graphics_info_t::molecules[imol].map_colour;
   if (! col || ! col[0]) return c;
   c.r = col[0][0]; c.g = col[0][1]; c.b = col[0][2];
   return c;
}

void bandicoot_core_set_map_colour(int imol, const bandicoot_rgb &colour) {
   if (! is_valid_map_molecule(imol)) return;
   gdouble col[4] = { colour.r, colour.g, colour.b, 1.0 };
   handle_map_colour_change(imol, col);
}

void bandicoot_core_remember_map_colour(int imol) {
   if (is_valid_map_molecule(imol)) save_previous_map_colour(imol);
}

void bandicoot_core_restore_map_colour(int imol) {
   if (is_valid_map_molecule(imol)) restore_previous_map_colour(imol);
}

bandicoot_rgb bandicoot_core_skeleton_colour() {
   bandicoot_rgb c;
   c.r = graphics_info_t::skeleton_colour[0];
   c.g = graphics_info_t::skeleton_colour[1];
   c.b = graphics_info_t::skeleton_colour[2];
   return c;
}

void bandicoot_core_set_skeleton_colour(const bandicoot_rgb &colour) {
   graphics_info_t::skeleton_colour[0] = colour.r;
   graphics_info_t::skeleton_colour[1] = colour.g;
   graphics_info_t::skeleton_colour[2] = colour.b;
   graphics_draw();
}

// The core's own text for an entry, which it allocates with malloc.
static std::string core_text(char *text) {
   std::string s = text ? coot::util::remove_trailing_whitespace(text) : std::string();
   free(text);
   return s;
}

static std::string number_text(double v) {
   char text[64];
   snprintf(text, sizeof(text), "%g", v);
   return text;
}

bandicoot_map_parameters bandicoot_core_map_parameters() {
   bandicoot_map_parameters p;
   p.radius_xray = core_text(get_text_for_density_size_widget());
   p.radius_em = core_text(get_text_for_density_size_em_widget());
   p.increment = core_text(get_text_for_iso_level_increment_entry(0));
   p.diff_increment = core_text(get_text_for_diff_map_iso_level_increment_entry(0));
   p.sampling_rate = core_text(get_text_for_map_sampling_rate_text());
   p.dynamic_sampling = graphics_info_t::dynamic_map_resampling;
   p.dynamic_size = graphics_info_t::dynamic_map_size_display;
   return p;
}

void bandicoot_core_set_map_radius(bool em, double radius) {
   std::string t = number_text(radius);
   if (em) set_density_size_em_from_widget(t.c_str());
   else    set_density_size_from_widget(t.c_str());
}

void bandicoot_core_set_map_parameters(double radius_xray, double radius_em, double increment,
                                       double diff_increment, double sampling_rate) {
   bandicoot_core_set_map_radius(false, radius_xray);
   bandicoot_core_set_map_radius(true, radius_em);
   // the increments apply to every map
   set_iso_level_increment_from_text(number_text(increment).c_str(), 0);
   set_diff_map_iso_level_increment_from_text(number_text(diff_increment).c_str(), 0);
   set_map_sampling_rate_text(number_text(sampling_rate).c_str());
}

void bandicoot_core_set_dynamic_map_sampling(bool on) {
   if (bool(graphics_info_t::dynamic_map_resampling) != on) toggle_dynamic_map_sampling();
}

void bandicoot_core_set_dynamic_map_size(bool on) {
   if (bool(graphics_info_t::dynamic_map_size_display) != on) toggle_dynamic_map_display_size();
}

std::string bandicoot_core_skeleton_box_radius() {
   return core_text(get_text_for_skeleton_box_size_entry());
}

void bandicoot_core_set_skeleton_box_radius(double radius) {
   set_skeleton_box_size_from_widget(number_text(radius).c_str());
}

std::string bandicoot_core_skeletonization_level() {
   return core_text(get_text_for_skeletonization_level_entry());
}

void bandicoot_core_set_skeletonization_level(double level) {
   set_skeletonization_level_from_widget(number_text(level).c_str());
}

int bandicoot_core_font_size() {
   return get_font_size();
}

void bandicoot_core_set_font_size(int size) {
   set_font_size(size);
}

std::string bandicoot_core_pointer_size() {
   return core_text(get_text_for_rotation_centre_cube_size());
}

void bandicoot_core_set_pointer_size(double size) {
   set_rotation_centre_size(float(size));
}

bool bandicoot_core_background_is_black() {
   return background_is_black_p();
}

void bandicoot_core_set_background_black(bool black) {
   double v = black ? 0.0 : 1.0;
   set_background_colour(v, v, v);
}

// ---- Edit menu: Restraints

std::vector<std::string> bandicoot_core_restraint_monomer_types() {
   graphics_info_t g;
   return g.Geom_p()->monomer_types();
}

static std::string float_text(double v) {
   char text[64];
   snprintf(text, sizeof(text), "%f", v);
   return text;
}

static bandicoot_restraint_table restraint_table(const std::string &title,
                                                 const std::vector<std::string> &columns) {
   bandicoot_restraint_table t;
   t.title = title;
   t.columns = columns;
   return t;
}

bool bandicoot_core_monomer_restraints(const std::string &comp_id,
                                       std::vector<bandicoot_restraint_table> &tables) {
   graphics_info_t g;
   std::pair<bool, coot::dictionary_residue_restraints_t> p =
      g.Geom_p()->get_monomer_restraints(comp_id, coot::protein_geometry::IMOL_ENC_ANY);
   if (! p.first) return false;
   const coot::dictionary_residue_restraints_t &r = p.second;
   tables.clear();

   // the atom counts are all atoms, then the non-hydrogens, as the dictionary holds them
   bandicoot_restraint_table info = restraint_table("Info",
      { "Comp ID", "3LetCode", "Name", "Group", "# Atoms", "# Non-H Atoms", "Desc Lev" });
   std::string tlc = r.residue_info.three_letter_code;
   if (tlc.empty()) tlc = r.residue_info.comp_id.substr(0, 3);
   info.rows.push_back({ r.residue_info.comp_id, tlc, r.residue_info.name, r.residue_info.group,
                         std::to_string(r.residue_info.number_atoms_all),
                         std::to_string(r.residue_info.number_atoms_nh),
                         r.residue_info.description_level });
   tables.push_back(info);

   bandicoot_restraint_table atoms = restraint_table("Atoms",
      { "Atom Name", "Element", "Energy Type", "Formal Charge" });
   for (const coot::dict_atom &a : r.atom_info)
      atoms.rows.push_back({ a.atom_id_4c, a.type_symbol, a.type_energy,
                             std::to_string(a.formal_charge.first ? a.formal_charge.second : 0) });
   tables.push_back(atoms);

   bandicoot_restraint_table bonds = restraint_table("Bonds",
      { "Atom Name 1", "Atom Name 2", "Type", "Bond Length", "ESD" });
   for (const coot::dict_bond_restraint_t &b : r.bond_restraint) {
      double dist = 0.0, esd = 0.0;
      try { dist = b.value_dist(); esd = b.value_esd(); }
      catch (const std::runtime_error &) { }  // a bond without a target length
      bonds.rows.push_back({ b.atom_id_1_4c(), b.atom_id_2_4c(), b.type(),
                             float_text(dist), float_text(esd) });
   }
   tables.push_back(bonds);

   bandicoot_restraint_table angles = restraint_table("Angles",
      { "Atom Name 1", "Atom Name 2", "Atom Name 3", "Angle", "ESD" });
   for (const coot::dict_angle_restraint_t &a : r.angle_restraint)
      angles.rows.push_back({ a.atom_id_1_4c(), a.atom_id_2_4c(), a.atom_id_3_4c(),
                              float_text(a.angle()), float_text(a.esd()) });
   tables.push_back(angles);

   bandicoot_restraint_table torsions = restraint_table("Torsions",
      { "Tors ID", "Atom Name 1", "Atom Name 2", "Atom Name 3", "Atom Name 4",
        "Torsion", "ESD", "Period" });
   for (const coot::dict_torsion_restraint_t &t : r.torsion_restraint)
      torsions.rows.push_back({ t.id(), t.atom_id_1_4c(), t.atom_id_2_4c(), t.atom_id_3_4c(),
                                t.atom_id_4_4c(), float_text(t.angle()), float_text(t.esd()),
                                std::to_string(t.periodicity()) });
   tables.push_back(torsions);

   bandicoot_restraint_table chirals = restraint_table("Chirals",
      { "Chiral ID", "Centre Atom", "Atom 1", "Atom 2", "Atom 3", "Sign" });
   for (const coot::dict_chiral_restraint_t &c : r.chiral_restraint)
      chirals.rows.push_back({ c.Chiral_Id(), c.atom_id_c_4c(), c.atom_id_1_4c(), c.atom_id_2_4c(),
                               c.atom_id_3_4c(),
                               coot::protein_geometry::make_chiral_volume_string(c.volume_sign) });
   tables.push_back(chirals);

   // as many atom columns as the largest plane has
   int max_atoms = 0;
   for (const coot::dict_plane_restraint_t &pl : r.plane_restraint)
      max_atoms = std::max(max_atoms, pl.n_atoms());
   std::vector<std::string> plane_columns = { "Plane ID" };
   for (int i = 1; i <= max_atoms; i++) plane_columns.push_back("Atom " + std::to_string(i));
   plane_columns.push_back("ESD");
   bandicoot_restraint_table planes = restraint_table("Planes", plane_columns);
   for (const coot::dict_plane_restraint_t &pl : r.plane_restraint) {
      std::vector<std::string> row = { pl.plane_id };
      for (int i = 0; i < max_atoms; i++) row.push_back(i < pl.n_atoms() ? pl[i].first : std::string());
      row.push_back(float_text(pl.n_atoms() > 0 ? pl.dist_esd(0) : 0.02));
      planes.rows.push_back(row);
   }
   tables.push_back(planes);
   return true;
}

// The cell, or "" past the end of a short row.
static std::string cell(const std::vector<std::string> &row, size_t i) {
   return i < row.size() ? row[i] : std::string();
}

static bool cell_number(const std::vector<std::string> &row, size_t i, double &v) {
   std::string t = coot::util::remove_whitespace(cell(row, i));
   if (t.empty()) return false;
   char *end = nullptr;
   v = strtod(t.c_str(), &end);
   return end && *end == '\0';
}

// The restraints the tables describe, starting from the dictionary's own so
// that what the tables do not show is kept. Rows are checked as Coot checked
// them; one that fails is left out.
static bool restraints_from_tables(const std::string &comp_id,
                                   const std::vector<bandicoot_restraint_table> &tables,
                                   coot::dictionary_residue_restraints_t &r, std::string &why_not) {
   if (tables.size() != 7) { why_not = "the editor's tables are incomplete"; return false; }
   graphics_info_t g;
   std::pair<bool, coot::dictionary_residue_restraints_t> p =
      g.Geom_p()->get_monomer_restraints(comp_id, coot::protein_geometry::IMOL_ENC_ANY);
   r = p.first ? p.second : coot::dictionary_residue_restraints_t(comp_id, coot::protein_geometry::IMOL_ENC_ANY);
   int skipped = 0;

   // Info
   if (tables[0].rows.empty()) { why_not = "the Info row is missing"; return false; }
   const std::vector<std::string> &info = tables[0].rows.front();
   double n_all, n_nh;
   std::string new_comp_id = coot::util::remove_whitespace(cell(info, 0));
   std::string tlc = cell(info, 1);
   if (coot::util::remove_whitespace(tlc).empty()) tlc = new_comp_id;
   if (new_comp_id.empty() || ! cell_number(info, 4, n_all) || n_all <= 0 ||
       ! cell_number(info, 5, n_nh) || n_nh <= 0 || cell(info, 6).empty()) {
      why_not = "the Info row needs a Comp ID, both atom counts and a description level";
      return false;
   }
   r.residue_info.comp_id = new_comp_id;
   r.residue_info.three_letter_code = tlc;
   r.residue_info.name = cell(info, 2);
   r.residue_info.group = cell(info, 3);
   r.residue_info.number_atoms_all = int(n_all);
   r.residue_info.number_atoms_nh = int(n_nh);
   r.residue_info.description_level = cell(info, 6);

   // Atoms: an atom still named as before keeps its coordinates and the rest
   std::vector<coot::dict_atom> atoms;
   for (const auto &row : tables[1].rows) {
      std::string name = cell(row, 0), element = cell(row, 1), energy = cell(row, 2);
      double charge = 0;
      if (name.empty() || element.empty() || energy.empty()) { skipped++; continue; }
      if (! cell(row, 3).empty() && ! cell_number(row, 3, charge)) { skipped++; continue; }
      coot::dict_atom a(name, name, element, energy, std::pair<bool, float>(false, 0.0f));
      for (const coot::dict_atom &old : r.atom_info)
         if (old.atom_id_4c == name) { a = old; break; }
      a.type_symbol = element;
      a.type_energy = energy;
      a.formal_charge = std::pair<bool, int>(true, int(charge));
      atoms.push_back(a);
   }
   r.atom_info = atoms;

   r.bond_restraint.clear();
   for (const auto &row : tables[2].rows) {
      double dist, esd;
      if (cell(row, 0).empty() || cell(row, 1).empty() || cell(row, 2).empty() ||
          ! cell_number(row, 3, dist) || dist <= 0 || ! cell_number(row, 4, esd) || esd <= 0) {
         skipped++; continue;
      }
      r.bond_restraint.push_back(coot::dict_bond_restraint_t(cell(row, 0), cell(row, 1), cell(row, 2),
                                                             dist, esd, 0.0, 0.0, false));
   }

   r.angle_restraint.clear();
   for (const auto &row : tables[3].rows) {
      double angle, esd;
      if (cell(row, 0).empty() || cell(row, 1).empty() || cell(row, 2).empty() ||
          ! cell_number(row, 3, angle) || angle <= 0 || ! cell_number(row, 4, esd) || esd <= 0) {
         skipped++; continue;
      }
      r.angle_restraint.push_back(coot::dict_angle_restraint_t(cell(row, 0), cell(row, 1), cell(row, 2),
                                                               angle, esd));
   }

   r.torsion_restraint.clear();
   for (const auto &row : tables[4].rows) {
      double torsion, esd, period;
      if (cell(row, 0).empty() || cell(row, 1).empty() || cell(row, 2).empty() ||
          cell(row, 3).empty() || cell(row, 4).empty() || ! cell_number(row, 5, torsion) ||
          ! cell_number(row, 6, esd) || esd < 0 || ! cell_number(row, 7, period) || period < 0) {
         skipped++; continue;
      }
      r.torsion_restraint.push_back(coot::dict_torsion_restraint_t(cell(row, 0), cell(row, 1),
                                       cell(row, 2), cell(row, 3), cell(row, 4), torsion, esd,
                                       int(period)));
   }

   r.chiral_restraint.clear();
   for (const auto &row : tables[5].rows) {
      bool complete = true;
      for (size_t i = 0; i < 5; i++) if (cell(row, i).empty()) complete = false;
      int sign = coot::protein_geometry::chiral_volume_string_to_chiral_sign(cell(row, 5));
      if (! complete || sign <= -999) { skipped++; continue; }
      r.chiral_restraint.push_back(coot::dict_chiral_restraint_t(cell(row, 0), cell(row, 1),
                                       cell(row, 2), cell(row, 3), cell(row, 4), sign));
   }
   r.assign_chiral_volume_targets();

   r.plane_restraint.clear();
   size_t esd_column = tables[6].columns.empty() ? 0 : tables[6].columns.size() - 1;
   for (const auto &row : tables[6].rows) {
      std::vector<std::string> plane_atoms;
      for (size_t i = 1; i < esd_column; i++)
         if (! coot::util::remove_whitespace(cell(row, i)).empty()) plane_atoms.push_back(cell(row, i));
      double esd;
      if (cell(row, 0).empty() || plane_atoms.empty() || ! cell_number(row, esd_column, esd) || esd <= 0) {
         skipped++; continue;
      }
      r.plane_restraint.push_back(coot::dict_plane_restraint_t(cell(row, 0), plane_atoms, esd));
   }

   if (skipped > 0)
      std::cout << "INFO:: restraints for " << comp_id << ": " << skipped
                << " incomplete row(s) left out" << std::endl;
   return true;
}

bool bandicoot_core_apply_restraints(const std::string &comp_id,
                                     const std::vector<bandicoot_restraint_table> &tables,
                                     std::string &why_not) {
   coot::dictionary_residue_restraints_t r(comp_id, coot::protein_geometry::IMOL_ENC_ANY);
   if (! restraints_from_tables(comp_id, tables, r, why_not)) return false;
   graphics_info_t g;
   const std::string &type = r.residue_info.comp_id;
   bool replaced = g.Geom_p()->replace_monomer_restraints(type, coot::protein_geometry::IMOL_ENC_ANY, r);
   g.redraw_molecules_with_residue(type);
   std::cout << "INFO:: restraints for \"" << type << "\" were "
             << (replaced ? "replaced" : "added") << std::endl;
   return true;
}

bool bandicoot_core_save_restraints(const std::string &comp_id,
                                    const std::vector<bandicoot_restraint_table> &tables,
                                    const std::string &path, std::string &why_not) {
   coot::dictionary_residue_restraints_t r(comp_id, coot::protein_geometry::IMOL_ENC_ANY);
   if (! restraints_from_tables(comp_id, tables, r, why_not)) return false;
   r.write_cif(path);
   return true;
}

// ---- Edit menu: Preferences

void bandicoot_core_save_preferences() {
   save_preferences();
}

// Coot's default for a preference, or fallback if it has none.
static const coot::preference_info_t *preference_default(int type) {
   for (const coot::preference_info_t &p : graphics_info_t::preferences_internal_default)
      if (p.preference_type == type) return &p;
   return nullptr;
}

static int default_int(int type, int fallback) {
   const coot::preference_info_t *p = preference_default(type);
   return p ? p->ivalue1 : fallback;
}

static float default_float(int type, float fallback) {
   const coot::preference_info_t *p = preference_default(type);
   return p ? p->fvalue1 : fallback;
}

bandicoot_general_preferences bandicoot_core_general_preference_defaults() {
   bandicoot_general_preferences d;
   d.ar_docked = default_int(PREFERENCES_ACCEPT_DIALOG_DOCKED, coot::DIALOG_DOCKED) == coot::DIALOG_DOCKED;
   d.ar_always_shown = default_int(PREFERENCES_ACCEPT_DIALOG_DOCKED_SHOW, coot::DIALOG_DOCKED_HIDE)
                       == coot::DIALOG_DOCKED_SHOW;
   d.sidebar_style = default_int(PREFERENCES_MODEL_TOOLBAR_STYLE, 1);
   d.recentre_on_read = default_int(PREFERENCES_RECENTRE_PDB, 1) != 0;
   d.smooth_scroll = default_int(PREFERENCES_SMOOTH_SCROLL, 1) != 0;
   d.smooth_scroll_steps = default_int(PREFERENCES_SMOOTH_SCROLL_STEPS, 40);
   d.smooth_scroll_limit = default_float(PREFERENCES_SMOOTH_SCROLL_LIMIT, 10.0f);
   d.flat_trackball = default_int(PREFERENCES_VT_SURFACE, 2) == 1;
   return d;
}

extern "C" void bandicoot_save_sequence_view_max_chains(int n);

void bandicoot_core_save_sequence_view_max_chains(int n) {
   bandicoot_save_sequence_view_max_chains(n);
}

void bandicoot_core_set_sidebar_style(int style) {
   preferences_internal_change_value_int(PREFERENCES_MODEL_TOOLBAR_STYLE, style);
   set_model_toolbar_style(style);
}

void bandicoot_core_set_sidebar_icon_shown(int pos, bool shown) {
   if (shown) show_model_toolbar_icon(pos);
   else       hide_model_toolbar_icon(pos);
}

void bandicoot_core_set_recentre_on_read(bool on) {
   preferences_internal_change_value_int(PREFERENCES_RECENTRE_PDB, on ? 1 : 0);
   set_recentre_on_read_pdb(on ? 1 : 0);
}

bool bandicoot_core_smooth_scroll() {
   return graphics_info_t::smooth_scroll == 1;
}

int bandicoot_core_smooth_scroll_steps() {
   return graphics_info_t::smooth_scroll_steps;
}

float bandicoot_core_smooth_scroll_limit() {
   return graphics_info_t::smooth_scroll_limit;
}

void bandicoot_core_set_smooth_scroll(bool on) {
   preferences_internal_change_value_int(PREFERENCES_SMOOTH_SCROLL, on ? 1 : 0);
   set_smooth_scroll_flag(on ? 1 : 0);
}

void bandicoot_core_set_smooth_scroll_steps(int steps) {
   preferences_internal_change_value_int(PREFERENCES_SMOOTH_SCROLL_STEPS, steps);
   set_smooth_scroll_steps(steps);
}

void bandicoot_core_set_smooth_scroll_limit(float angstroms) {
   preferences_internal_change_value_float(PREFERENCES_SMOOTH_SCROLL_LIMIT, angstroms);
   graphics_info_t::smooth_scroll_limit = angstroms;
}

bool bandicoot_core_flat_trackball() {
   return vt_surface_status() == 1;
}

void bandicoot_core_set_flat_trackball(bool flat) {
   int mode = flat ? 1 : 2;
   preferences_internal_change_value_int(PREFERENCES_VT_SURFACE, mode);
   vt_surface(mode);
}

// in c-interface-preferences.cc, which holds the scheme table
std::vector<std::string> bandicoot_bond_scheme_labels();
int bandicoot_bond_scheme_row(int bonds_box_type);

bandicoot_display_preferences bandicoot_core_display_preferences() {
   bandicoot_display_preferences p;
   p.bond_width = graphics_info_t::default_bond_width;
   p.scale_bond_width_to_occupancy = graphics_info_t::scale_bond_width_to_occupancy;
   p.bond_scheme = bandicoot_bond_scheme_row(graphics_info_t::default_bonds_box_type);
   p.bond_colour_rotation = graphics_info_t::rotate_colour_map_on_read_pdb;
   p.carbons_only = graphics_info_t::rotate_colour_map_on_read_pdb_c_only_flag;
   p.altloc_conf_a_offset = graphics_info_t::altloc_conf_a_colour_offset;
   p.altloc_colour_difference = graphics_info_t::altloc_colour_difference;
   p.map_radius = graphics_info_t::box_radius_xray;
   p.map_increment = graphics_info_t::iso_level_increment;
   p.diff_map_increment = graphics_info_t::diff_map_iso_level_increment;
   p.map_sampling_rate = graphics_info_t::map_sampling_rate;
   p.dynamic_map_sampling = graphics_info_t::dynamic_map_resampling;
   p.dynamic_map_size = graphics_info_t::dynamic_map_size_display;
   p.o_difference_map_colours = graphics_info_t::swap_difference_map_colours;
   p.map_colour_rotation = graphics_info_t::rotate_colour_map_for_map;
   p.active_map_drag = get_active_map_drag_flag();
   p.mark_cis_peptides_bad = graphics_info_t::mark_cis_peptides_as_bad_flag;
   p.background.r = graphics_info_t::background_colour[0];
   p.background.g = graphics_info_t::background_colour[1];
   p.background.b = graphics_info_t::background_colour[2];
   return p;
}

bandicoot_display_preferences bandicoot_core_display_preference_defaults() {
   bandicoot_display_preferences now = bandicoot_core_display_preferences();
   bandicoot_display_preferences d;
   d.bond_width = default_int(PREFERENCES_BONDS_THICKNESS, now.bond_width);
   d.scale_bond_width_to_occupancy = default_int(PREFERENCES_SCALE_BOND_WIDTH_TO_OCCUPANCY,
                                                 now.scale_bond_width_to_occupancy) != 0;
   const coot::preference_info_t *scheme = preference_default(PREFERENCES_DEFAULT_BOND_DISPLAY_SCHEME);
   d.bond_scheme = scheme ? bandicoot_bond_scheme_row(scheme->ivalue1) : now.bond_scheme;
   d.bond_colour_rotation = default_float(PREFERENCES_BOND_COLOURS_MAP_ROTATION, now.bond_colour_rotation);
   d.carbons_only = default_int(PREFERENCES_BOND_COLOUR_ROTATION_C_ONLY, now.carbons_only) != 0;
   d.altloc_conf_a_offset = default_float(PREFERENCES_ALTLOC_CONF_A_COLOUR_OFFSET, now.altloc_conf_a_offset);
   d.altloc_colour_difference = default_float(PREFERENCES_ALTLOC_COLOUR_DIFFERENCE,
                                              now.altloc_colour_difference);
   d.map_radius = default_float(PREFERENCES_MAP_RADIUS, now.map_radius);
   d.map_increment = default_float(PREFERENCES_MAP_ISOLEVEL_INCREMENT, now.map_increment);
   d.diff_map_increment = default_float(PREFERENCES_DIFF_MAP_ISOLEVEL_INCREMENT, now.diff_map_increment);
   d.map_sampling_rate = default_float(PREFERENCES_MAP_SAMPLING_RATE, now.map_sampling_rate);
   d.dynamic_map_sampling = default_int(PREFERENCES_DYNAMIC_MAP_SAMPLING, now.dynamic_map_sampling) != 0;
   d.dynamic_map_size = default_int(PREFERENCES_DYNAMIC_MAP_SIZE_DISPLAY, now.dynamic_map_size) != 0;
   d.o_difference_map_colours = default_int(PREFERENCES_SWAP_DIFF_MAP_COLOURS,
                                            now.o_difference_map_colours) != 0;
   d.map_colour_rotation = default_float(PREFERENCES_MAP_COLOURS_MAP_ROTATION, now.map_colour_rotation);
   d.active_map_drag = default_int(PREFERENCES_MAP_DRAG, now.active_map_drag) != 0;
   d.mark_cis_peptides_bad = default_int(PREFERENCES_MARK_CIS_BAD, now.mark_cis_peptides_bad) != 0;
   const coot::preference_info_t *bg = preference_default(PREFERENCES_BG_COLOUR);
   if (bg) {
      d.background.r = bg->fvalue1;
      d.background.g = bg->fvalue2;
      d.background.b = bg->fvalue3;
   } else {
      d.background = now.background;
   }
   return d;
}

std::vector<std::string> bandicoot_core_bond_scheme_labels() {
   return bandicoot_bond_scheme_labels();
}

void bandicoot_core_set_default_bond_width(int pixels) {
   preferences_internal_change_value_int(PREFERENCES_BONDS_THICKNESS, pixels);
   set_default_bond_thickness(pixels);
}

void bandicoot_core_set_scale_bond_width_to_occupancy(bool on) {
   preferences_internal_change_value_int(PREFERENCES_SCALE_BOND_WIDTH_TO_OCCUPANCY, on ? 1 : 0);
   set_scale_bond_width_to_occupancy(on ? 1 : 0);
}

void bandicoot_core_set_default_bond_scheme(int index) {
   bandicoot_set_default_bond_scheme_from_combo_index(index);
}

void bandicoot_core_set_bond_colour_rotation_on_read(float degrees) {
   preferences_internal_change_value_float(PREFERENCES_BOND_COLOURS_MAP_ROTATION, degrees);
   set_colour_map_rotation_on_read_pdb(degrees);
}

void bandicoot_core_set_carbons_only_preference(bool on) {
   preferences_internal_change_value_int(PREFERENCES_BOND_COLOUR_ROTATION_C_ONLY, on ? 1 : 0);
   set_colour_map_rotation_on_read_pdb_c_only_flag(on ? 1 : 0);
}

void bandicoot_core_set_altloc_conf_a_offset(float degrees) {
   preferences_internal_change_value_float(PREFERENCES_ALTLOC_CONF_A_COLOUR_OFFSET, degrees);
   set_altloc_conf_a_colour_offset(degrees);
}

void bandicoot_core_set_altloc_colour_difference(float degrees) {
   preferences_internal_change_value_float(PREFERENCES_ALTLOC_COLOUR_DIFFERENCE, degrees);
   set_altloc_colour_difference(degrees);
}

void bandicoot_core_set_map_radius_preference(float angstroms) {
   preferences_internal_change_value_float(PREFERENCES_MAP_RADIUS, angstroms);
   set_map_radius(angstroms);
}

void bandicoot_core_set_map_increment_preference(float increment) {
   preferences_internal_change_value_float(PREFERENCES_MAP_ISOLEVEL_INCREMENT, increment);
   set_iso_level_increment(increment);
}

void bandicoot_core_set_diff_map_increment_preference(float increment) {
   preferences_internal_change_value_float(PREFERENCES_DIFF_MAP_ISOLEVEL_INCREMENT, increment);
   set_diff_map_iso_level_increment(increment);
}

void bandicoot_core_set_map_sampling_rate_preference(float rate) {
   preferences_internal_change_value_float(PREFERENCES_MAP_SAMPLING_RATE, rate);
   set_map_sampling_rate(rate);
}

void bandicoot_core_set_dynamic_map_sampling_preference(bool on) {
   preferences_internal_change_value_int(PREFERENCES_DYNAMIC_MAP_SAMPLING, on ? 1 : 0);
   if (on) set_dynamic_map_sampling_on();
   else    set_dynamic_map_sampling_off();
}

void bandicoot_core_set_dynamic_map_size_preference(bool on) {
   preferences_internal_change_value_int(PREFERENCES_DYNAMIC_MAP_SIZE_DISPLAY, on ? 1 : 0);
   if (on) set_dynamic_map_size_display_on();
   else    set_dynamic_map_size_display_off();
}

void bandicoot_core_set_o_difference_map_colours(bool on) {
   preferences_internal_change_value_int(PREFERENCES_SWAP_DIFF_MAP_COLOURS, on ? 1 : 0);
   set_swap_difference_map_colours(on ? 1 : 0);
}

void bandicoot_core_set_map_colour_rotation_on_read(float degrees) {
   preferences_internal_change_value_float(PREFERENCES_MAP_COLOURS_MAP_ROTATION, degrees);
   set_colour_map_rotation_for_map(degrees);
}

void bandicoot_core_set_active_map_drag(bool on) {
   preferences_internal_change_value_int(PREFERENCES_MAP_DRAG, on ? 1 : 0);
   set_active_map_drag_flag(on ? 1 : 0);
}

void bandicoot_core_set_mark_cis_peptides_bad(bool on) {
   preferences_internal_change_value_int(PREFERENCES_MARK_CIS_BAD, on ? 1 : 0);
   set_mark_cis_peptides_as_bad(on ? 1 : 0);
}

void bandicoot_core_set_background_preference(const bandicoot_rgb &colour) {
   preferences_internal_change_value_float3(PREFERENCES_BG_COLOUR, colour.r, colour.g, colour.b);
   set_background_colour(colour.r, colour.g, colour.b);
}

// in c-interface-preferences.cc, with the files they keep
void bandicoot_read_pandda_config(std::string &cifs_dir, bool &split);
void bandicoot_write_pandda_config(const std::string &cifs_dir, bool split);
int bandicoot_load_pandda_uimode();
void bandicoot_save_pandda_uimode(int mode);
void bandicoot_load_ligand_behaviour(int *auto_rename, int *show_rename,
                                     int *auto_generate, int *show_generate, int *apply_all);
void bandicoot_write_ligand_behaviour(int auto_rename, int show_rename,
                                      int auto_generate, int show_generate, int apply_all);

bandicoot_rgb bandicoot_core_default_font_colour() {
   bandicoot_rgb c;
   c.r = 1.0f; c.g = 0.8f; c.b = 0.8f;
   return c;
}

bandicoot_other_preferences bandicoot_core_other_preferences() {
   bandicoot_other_preferences p;
   p.antialias = graphics_info_t::do_anti_aliasing_flag;
   p.font_size = graphics_info_t::atom_label_font_size;
   p.font_colour.r = graphics_info_t::font_colour.red;
   p.font_colour.g = graphics_info_t::font_colour.green;
   p.font_colour.b = graphics_info_t::font_colour.blue;
   p.console_commands = graphics_info_t::console_display_commands.display_commands_flag;
   p.pointer_size = graphics_info_t::rotation_centre_cube_size;
   p.refinement_speed = graphics_info_t::dragged_refinement_steps_per_frame;
   p.spin_speed = graphics_info_t::idle_function_rotate_angle;
   p.pick_radius = graphics_info_t::pick_atom_dist_cutoff;
   p.symmetry_pick_radius = graphics_info_t::symm_pick_atom_dist_cutoff;
   p.intermediate_pick_far = graphics_info_t::intermediate_pick_far_cutoff;
   p.intermediate_pick_near = graphics_info_t::intermediate_pick_near_cutoff;
   bandicoot_read_pandda_config(p.pandda_cifs_dir, p.pandda_split);
   p.pandda_basic = bandicoot_load_pandda_uimode() == 1;
   int auto_rename = 0, show_rename = 1, auto_generate = 0, show_generate = 1, apply_all = 0;
   bandicoot_load_ligand_behaviour(&auto_rename, &show_rename, &auto_generate, &show_generate, &apply_all);
   p.auto_rename = auto_rename;
   p.ask_rename = show_rename;
   p.auto_generate = auto_generate;
   p.ask_generate = show_generate;
   p.apply_restraints_to_all = apply_all;
   p.clipping = graphics_info_t::clipping_front;
   p.crosshairs = graphics_info_t::draw_crosshairs_flag != 0;
   p.label_on_recentre = graphics_info_t::label_atom_on_recentre_flag != 0;
   return p;
}

bandicoot_other_preferences bandicoot_core_other_preference_defaults() {
   bandicoot_other_preferences now = bandicoot_core_other_preferences();
   bandicoot_other_preferences d;
   d.antialias = default_int(PREFERENCES_ANTIALIAS, now.antialias) != 0;
   d.font_size = default_int(PREFERENCES_FONT_SIZE, now.font_size);
   const coot::preference_info_t *fc = preference_default(PREFERENCES_FONT_COLOUR);
   if (fc) {
      d.font_colour.r = fc->fvalue1;
      d.font_colour.g = fc->fvalue2;
      d.font_colour.b = fc->fvalue3;
   } else {
      d.font_colour = bandicoot_core_default_font_colour();
   }
   d.console_commands = default_int(PREFERENCES_CONSOLE_COMMANDS, now.console_commands) != 0;
   d.pointer_size = default_float(PREFERENCES_PINK_POINTER, now.pointer_size);
   d.refinement_speed = default_int(PREFERENCES_REFINEMENT_SPEED, now.refinement_speed);
   d.spin_speed = default_float(PREFERENCES_SPIN_SPEED, now.spin_speed);
   // Bandicoot's own settings: the values it starts with when nothing is saved
   d.pick_radius = 0.6f;
   d.symmetry_pick_radius = 0.4f;
   d.intermediate_pick_far = 0.04f;
   d.intermediate_pick_near = 0.8f;
   d.pandda_cifs_dir = now.pandda_cifs_dir;
   d.pandda_split = false;
   d.pandda_basic = false;
   d.auto_rename = false;
   d.ask_rename = true;
   d.auto_generate = false;
   d.ask_generate = true;
   d.apply_restraints_to_all = false;
   d.clipping = default_float(PREFERENCES_CLIPPING, 0.0f);
   d.crosshairs = default_int(PREFERENCES_CROSSHAIRS, 0) != 0;
   d.label_on_recentre = default_int(PREFERENCES_LABEL_ON_RECENTRE, 1) != 0;
   return d;
}

std::vector<std::string> bandicoot_core_other_font_labels() {
   // the names Coot gives sizes 4 to 7
   return { "Times Roman 10", "Times Roman 24", "Fixed 8/13", "Fixed 9/15" };
}

void bandicoot_core_set_clipping(float clipping) {
   preferences_internal_change_value_float(PREFERENCES_CLIPPING, clipping);
   set_clipping_front(clipping);
   set_clipping_back(clipping);
}

void bandicoot_core_set_crosshairs(bool on) {
   preferences_internal_change_value_int(PREFERENCES_CROSSHAIRS, on ? 1 : 0);
   set_draw_crosshairs(on ? 1 : 0);
   // Coot redraws only when they come on
   graphics_draw();
}

void bandicoot_core_set_label_on_recentre(bool on) {
   preferences_internal_change_value_int(PREFERENCES_LABEL_ON_RECENTRE, on ? 1 : 0);
   set_label_on_recentre_flag(on ? 1 : 0);
}

void bandicoot_core_set_antialias(bool on) {
   preferences_internal_change_value_int(PREFERENCES_ANTIALIAS, on ? 1 : 0);
   set_do_anti_aliasing(on ? 1 : 0);
}

void bandicoot_core_set_font_size_preference(int size) {
   preferences_internal_change_value_int(PREFERENCES_FONT_SIZE, size);
   set_font_size(size);
}

void bandicoot_core_set_font_colour_preference(const bandicoot_rgb &colour) {
   preferences_internal_change_value_float3(PREFERENCES_FONT_COLOUR, colour.r, colour.g, colour.b);
   set_font_colour(colour.r, colour.g, colour.b);
}

void bandicoot_core_set_console_commands(bool on) {
   preferences_internal_change_value_int(PREFERENCES_CONSOLE_COMMANDS, on ? 1 : 0);
   set_console_display_commands_state(on ? 1 : 0);
}

void bandicoot_core_set_pointer_size_preference(float angstroms) {
   preferences_internal_change_value_float(PREFERENCES_PINK_POINTER, angstroms);
   set_rotation_centre_size(angstroms);
}

void bandicoot_core_set_refinement_speed(int steps_per_frame) {
   preferences_internal_change_value_int(PREFERENCES_REFINEMENT_SPEED, steps_per_frame);
   set_dragged_refinement_steps_per_frame(steps_per_frame);
}

void bandicoot_core_set_spin_speed(float degrees) {
   preferences_internal_change_value_float(PREFERENCES_SPIN_SPEED, degrees);
   set_idle_function_rotate_angle(degrees);
}

void bandicoot_core_set_pick_radius(float angstroms) {
   set_pick_atom_distance_cutoff(angstroms);
}

void bandicoot_core_set_symmetry_pick_radius(float angstroms) {
   set_symmetry_pick_atom_distance_cutoff(angstroms);
}

void bandicoot_core_set_intermediate_pick_radii(float far_angstroms, float near_angstroms) {
   set_intermediate_pick_distance_cutoffs(far_angstroms, near_angstroms);
}

void bandicoot_core_set_pandda_config(const std::string &cifs_dir, bool split) {
   bandicoot_write_pandda_config(cifs_dir, split);
}

void bandicoot_core_set_pandda_basic(bool basic) {
   bandicoot_save_pandda_uimode(basic ? 1 : 0);
}

void bandicoot_core_set_ligand_behaviour(bool auto_rename, bool ask_rename, bool auto_generate,
                                         bool ask_generate, bool apply_restraints_to_all) {
   bandicoot_write_ligand_behaviour(auto_rename, ask_rename, auto_generate, ask_generate,
                                    apply_restraints_to_all);
}

// ---- Edit menu: Residue Info

static bandicoot_residue_info_view residue_info_view;

void bandicoot_core_set_residue_info_view(const bandicoot_residue_info_view &view) {
   residue_info_view = view;
}

void bandicoot_core_pick_residue_info() {
   do_residue_info_dialog();
}

// As Coot labels an atom row: chain/resno[ins] resname/name[ segid]
static std::string residue_atom_label(mmdb::Atom *at) {
   std::string label = std::string(at->GetChainID()) + "/" + std::to_string(at->GetSeqNum());
   label += at->GetInsCode();
   label += " " + std::string(at->GetResName()) + "/" + at->name;
   if (std::string(at->segID) != "") label += " " + std::string(at->segID);
   return label;
}

static void fill_residue_info(int imol, mmdb::Residue *residue, bandicoot_residue_info &info) {
   info = bandicoot_residue_info();
   info.imol = imol;
   info.molecule_label = "Molecule: " + std::to_string(imol) + " " +
                         graphics_info_t::molecules[imol].name_;
   graphics_info_t g;
   std::pair<bool, std::string> name = g.Geom_p()->get_monomer_name(residue->GetResName(), imol);
   if (name.first) info.residue_name = name.second;
   info.default_b_factor = graphics_info_t::default_new_atoms_b_factor;

   mmdb::PPAtom atoms = nullptr;
   int n_atoms = 0;
   residue->GetAtomTable(atoms, n_atoms);
   for (int i=0; i<n_atoms; i++) {
      mmdb::Atom *at = atoms[i];
      bandicoot_residue_atom a;
      a.label = residue_atom_label(at);
      a.chain = at->GetChainID();
      a.resno = at->GetSeqNum();
      a.ins_code = at->GetInsCode();
      a.atom_name = at->name;
      a.alt_conf = at->altLoc;
      a.occupancy = at->occupancy;
      a.b_factor = at->tempFactor;
      info.atoms.push_back(a);
   }

   // the first alt conf's chis, as Coot shows them
   try {
      coot::primitive_chi_angles chi_angles(residue);
      std::vector<coot::alt_confed_chi_angles> chis = chi_angles.get_chi_angles();
      if (! chis.empty()) {
         for (const auto &chi : chis[0].chi_angles) {
            std::string line = "Chi " + std::to_string(chi.first) + ":  ";
            if (! chis[0].alt_conf.empty()) line += " alt conf: " + chis[0].alt_conf + " ";
            line += coot::util::float_to_string(chi.second) + " degrees";
            info.chi_angles.push_back(line);
         }
      }
   }
   catch (const std::runtime_error &e) {
      std::cout << e.what() << std::endl;
   }
}

bool bandicoot_wx_residue_info(int imol, int atom_index) {
   if (! residue_info_view.show) return false;
   if (imol < 0 || imol >= graphics_info_t::n_molecules()) return true;
   molecule_class_info_t &m = graphics_info_t::molecules[imol];
   if (! m.has_model() || atom_index < 0 || atom_index >= m.atom_sel.n_selected_atoms) return true;
   output_residue_info_as_text(atom_index, imol);
   bandicoot_residue_info info;
   fill_residue_info(imol, m.atom_sel.atom_selection[atom_index]->residue, info);
   residue_info_view.show(info);
   return true;
}

bool bandicoot_core_residue_info(int imol, const std::string &chain, int resno,
                                 const std::string &ins_code, bandicoot_residue_info &info) {
   if (! is_valid_model_molecule(imol)) return false;
   mmdb::Residue *residue =
      graphics_info_t::molecules[imol].get_residue(chain, resno, ins_code);
   if (! residue) return false;
   fill_residue_info(imol, residue, info);
   return true;
}

void bandicoot_core_apply_atom_edits(int imol, const std::vector<bandicoot_residue_atom> &before,
                                     const std::vector<bandicoot_residue_atom> &after) {
   if (! is_valid_model_molecule(imol) || before.size() != after.size()) return;
   std::vector<coot::select_atom_info> edits;
   for (size_t i=0; i<before.size(); i++) {
      const bandicoot_residue_atom &b = before[i], &a = after[i];
      coot::select_atom_info e(int(i), imol, b.chain, b.resno, b.ins_code, b.atom_name, b.alt_conf);
      bool edited = false;
      // as Coot: changes below its two-decimal display are not edits
      if (std::abs(a.occupancy - b.occupancy) > 0.009) { e.add_occ_edit(a.occupancy); edited = true; }
      if (std::abs(a.b_factor - b.b_factor) > 0.009) { e.add_b_factor_edit(a.b_factor); edited = true; }
      if (a.alt_conf != b.alt_conf) { e.add_altloc_edit(a.alt_conf); edited = true; }
      if (edited) edits.push_back(e);
   }
   if (edits.empty()) return;
   graphics_info_t::molecules[imol].apply_atom_edits(edits);
   graphics_draw();
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

int bandicoot_wx_question(const std::string &text) {
   if (! ui_view.question) return -1;
   return ui_view.question(text) ? 1 : 0;
}

// Restraints read from one file fit several loaded molecules: which get them.
bool bandicoot_wx_choose_molecules(const std::string &file_name, const std::vector<int> &targets,
                                   std::vector<int> &chosen) {
   if (! ui_view.choose_molecules) return false;
   std::vector<std::pair<int, std::string>> molecules;
   for (int imol : targets)
      if (is_valid_model_molecule(imol))
         molecules.push_back({ imol, std::to_string(imol) + "  " +
                                     graphics_info_t::molecules[imol].name_for_display_manager() });
   chosen = ui_view.choose_molecules("Apply Restraints",
                                     "More than one molecule compatible with these restraints found:\n" +
                                     coot::util::file_name_non_directory(file_name), molecules);
   return true;
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

bool bandicoot_wx_radio_choice(const std::string &title,
                               const std::vector<std::pair<std::string, std::string>> &choices,
                               int selected,
                               const std::string &accept_label, const std::string &accept_command,
                               const std::string &reject_label, const std::string &reject_command) {
   if (! ui_view.radio_choice || choices.empty()) return false;
   bandicoot_radio_choice c;
   c.title = title;
   c.choices = choices;
   c.selected = (selected >= 0 && selected < int(choices.size())) ? selected : 0;
   c.accept_label = accept_label;
   c.accept_command = accept_command;
   c.reject_label = reject_label;
   c.reject_command = reject_command;
   ui_view.radio_choice(c);
   return true;
}

bool bandicoot_wx_button_choice_filtered(const std::string &title,
                                         const std::vector<std::pair<std::string, std::string>> &buttons,
                                         const std::string &check_label,
                                         const std::vector<std::pair<std::string, std::string>> &checked_buttons,
                                         const std::string &close_label, const std::string &close_command) {
   // an empty list is still shown, as Coot's window was
   if (! ui_view.button_choice) return false;
   bandicoot_button_choice c;
   c.title = title;
   c.buttons = buttons;
   c.check_label = check_label;
   c.checked_buttons = checked_buttons;
   c.close_label = close_label;
   c.close_command = close_command;
   ui_view.button_choice(c);
   return true;
}

bool bandicoot_wx_generic_objects() {
   if (! ui_view.generic_objects) return false;
   ui_view.generic_objects();
   return true;
}

bool bandicoot_wx_button_choice(const std::string &title,
                                const std::vector<std::pair<std::string, std::string>> &buttons,
                                const std::string &close_label, const std::string &close_command) {
   if (! ui_view.button_choice || buttons.empty()) return false;
   bandicoot_button_choice c;
   c.title = title;
   c.buttons = buttons;
   c.close_label = close_label;
   c.close_command = close_command;
   ui_view.button_choice(c);
   return true;
}

void bandicoot_core_run_python(const std::string &command) {
   if (! command.empty()) safe_python_command(command);
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

// ---- Calculate menu: maps

float bandicoot_core_map_sharpening_limit() {
   return graphics_info_t::map_sharpening_scale_limit;
}

float bandicoot_core_map_sharpening(int imol) {
   return is_valid_map_molecule(imol) ? graphics_info_t::molecules[imol].sharpen_b_factor() : 0.0f;
}

void bandicoot_core_sharpen_map(int imol, float b_factor) {
   if (! is_valid_map_molecule(imol)) return;
   graphics_info_t::imol_map_sharpening = imol;
   sharpen(imol, b_factor);
}

float bandicoot_core_optimal_sharpening(int imol, bool &converged) {
   float b = optimal_B_kurtosis(imol);
   // the search ends at the scale's limit when it finds no optimum
   converged = std::fabs(b - graphics_info_t::map_sharpening_scale_limit) > 0.1f;
   return b;
}

int bandicoot_core_skeleton_map() {
   graphics_info_t::set_initial_map_for_skeletonize();
   return is_valid_map_molecule(graphics_info_t::map_for_skeletonize) ? graphics_info_t::map_for_skeletonize : -1;
}

bool bandicoot_core_map_skeletonized(int imol) {
   return is_valid_map_molecule(imol) && graphics_info_t::molecules[imol].xskel_is_filled;
}

void bandicoot_core_set_map_skeleton(int imol, bool on, bool prune) {
   if (! is_valid_map_molecule(imol)) return;
   graphics_info_t::map_for_skeletonize = imol;
   if (on) graphics_info_t::skeletonize_map(imol, prune ? 1 : 0);
   else    graphics_info_t::unskeletonize_map(imol);
}

std::vector<std::pair<int, std::string>> bandicoot_core_ncs_models() {
   std::vector<std::pair<int, std::string>> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_model_molecule(imol) && graphics_info_t::molecules[imol].has_ncs_p())
         v.push_back({ imol, chooser_label(imol) });
   return v;
}

void bandicoot_core_make_ncs_maps(int imol_model, int imol_map) {
   make_dynamically_transformed_ncs_maps(imol_model, imol_map, 0);
}

std::vector<std::pair<int, std::string>> bandicoot_core_difference_maps() {
   std::vector<std::pair<int, std::string>> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_map_molecule(imol) && map_is_difference_map(imol))
         v.push_back({ imol, chooser_label(imol) });
   return v;
}

void bandicoot_core_update_maps(int imol_model, int imol_map, int imol_difference_map, bool automatic) {
   if (! is_valid_model_molecule(imol_model) || ! is_valid_map_molecule(imol_map)) return;
   if (automatic) set_auto_updating_sfcalc_genmap(imol_model, imol_map, imol_difference_map);
   else           sfcalc_genmap(imol_model, imol_map, imol_difference_map);
}

// ---- Calculate menu: superpose and move

void bandicoot_core_move_molecule_here(int imol) {
   if (! is_valid_model_molecule(imol)) return;
   move_molecule_to_screen_centre_internal(imol);
   add_to_history({ "move-molecule-here", clipper::String(imol) });
}

bandicoot_lsq bandicoot_core_lsq_remembered() {
   const lsq_dialog_values_t &v = graphics_info_t::lsq_dialog_values;
   bandicoot_lsq l;
   l.imol_reference = v.reference_molecule_number;
   l.imol_moving = v.moving_molecule_number;
   l.reference_start = v.ref_res_range_start;
   l.reference_end = v.ref_res_range_end;
   l.moving_start = v.mov_res_range_start;
   l.moving_end = v.mov_res_range_end;
   l.reference_chain = v.chain_id_ref;
   l.moving_chain = v.chain_id_mov;
   return l;
}

void bandicoot_core_remember_lsq(const bandicoot_lsq &l) {
   lsq_dialog_values_t &v = graphics_info_t::lsq_dialog_values;
   v.reference_molecule_number = l.imol_reference;
   v.moving_molecule_number = l.imol_moving;
   v.ref_res_range_start = l.reference_start;
   v.ref_res_range_end = l.reference_end;
   v.mov_res_range_start = l.moving_start;
   v.mov_res_range_end = l.moving_end;
   v.chain_id_ref = l.reference_chain;
   v.chain_id_mov = l.moving_chain;
}

bool bandicoot_core_lsq_fit(const bandicoot_lsq &l, std::string &why_not) {
   if (! is_valid_model_molecule(l.imol_reference) || ! is_valid_model_molecule(l.imol_moving)) {
      why_not = "not a model molecule";
      return false;
   }
   bandicoot_core_remember_lsq(l);
   int imol_moving = l.imol_moving;
   if (l.move_copy) {
      imol_moving = copy_molecule(imol_moving);
      graphics_info_t::lsq_mov_imol = imol_moving;
   }
   clear_lsq_matches();
   add_lsq_match(l.reference_start, l.reference_end, l.reference_chain.c_str(),
                 l.moving_start, l.moving_end, l.moving_chain.c_str(), l.match);
   if (! apply_lsq_matches_simple(l.imol_reference, imol_moving)) {
      why_not = "no atoms matched in those ranges";
      return false;
   }
   return true;
}

void bandicoot_core_ssm_superpose(int imol_reference, int imol_moving, const std::string &reference_chain,
                                  const std::string &moving_chain, bool move_copy) {
   if (! is_valid_model_molecule(imol_reference) || ! is_valid_model_molecule(imol_moving)) return;
   bool by_reference_chain = ! reference_chain.empty();
   bool by_moving_chain = ! moving_chain.empty();
   // a model is never moved onto itself: the copy moves instead, as in Coot
   if (imol_reference == imol_moving) {
      if (! by_reference_chain || ! by_moving_chain || reference_chain == moving_chain)
         move_copy = true;
   }
   superpose_with_chain_selection(imol_reference, imol_moving,
                                  by_reference_chain ? reference_chain.c_str() : "empty",
                                  by_moving_chain ? moving_chain.c_str() : "empty",
                                  by_reference_chain, by_moving_chain, move_copy);
}

bool bandicoot_core_align_and_mutate(int imol, const std::string &chain, const std::string &sequence,
                                     bool autofit, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) {
      why_not = "not a model molecule";
      return false;
   }
   graphics_info_t g;
   if (autofit && g.Imol_Refinement_Map() == -1) {
      why_not = "autofit needs a refinement map";
      return false;
   }
   g.mutate_chain(imol, chain, sequence, autofit, false);
   g.update_geometry_graphs(g.molecules[imol].atom_sel, imol);
   graphics_draw();
   return true;
}

// ---- Calculate menu: direct items and scripts

bool bandicoot_core_fps_shown() {
   return graphics_info_t::GetFPSFlag() != 0;
}

void bandicoot_core_show_fps(bool on) {
   set_fps_flag(on ? 1 : 0);
}

bool bandicoot_core_load_tutorial(std::string &why_not) {
   std::string data = coot::util::append_dir_dir(coot::package_data_dir(), "data");
   for (const char *f : { "tutorial-modern.pdb", "rnasa-1.8-all_refmac1.mtz" }) {
      std::string path = coot::util::append_dir_file(data, f);
      if (! coot::file_exists(path)) {
         why_not = path;
         return false;
      }
   }
   load_tutorial_model_and_data();
   return true;
}

void bandicoot_core_run_script(const std::string &path) {
   if (! path.empty()) run_script(path.c_str());
}

// The console's engine is _bandicoot_console_eval (python/bandicoot_console.py),
// in __main__ so that what a run defines lasts to the next one.
std::string bandicoot_core_console_run(const std::string &code, bool &had_error) {
   std::string output;
   had_error = false;
   PyGILState_STATE gstate = PyGILState_Ensure();
   PyObject *main_module = PyImport_AddModule("__main__");                       // borrowed
   PyObject *func = main_module ? PyDict_GetItemString(PyModule_GetDict(main_module),
                                                       "_bandicoot_console_eval") // borrowed
                                : nullptr;
   if (! func || ! PyCallable_Check(func)) {
      had_error = true;
      output = "Bandicoot: the console engine did not load (python/bandicoot_console.py).\n";
   } else {
      PyObject *rv = PyObject_CallFunction(func, "s", code.c_str());               // new
      if (rv && PyTuple_Check(rv) && PyTuple_Size(rv) == 2) {
         had_error = PyObject_IsTrue(PyTuple_GetItem(rv, 0)) == 1;
         PyObject *text = PyTuple_GetItem(rv, 1);
         if (text && PyUnicode_Check(text)) output = PyUnicode_AsUTF8(text);
      } else {
         PyErr_Print();
         had_error = true;
         output = "Bandicoot: error calling the console engine (see the terminal).\n";
      }
      Py_XDECREF(rv);
   }
   PyGILState_Release(gstate);
   return output;
}

// ---- Modelling menu

// The GTK build's Modelling menu sends these through the same dispatcher.
static const std::map<std::string, int> &modelling_direct_ops() {
   static const std::map<std::string, int> ops = {
      { "BMOD_ADD_HYDROGENS",          BMOD_ADD_HYDROGENS },
      { "BMOD_ADD_HYDROGENS_REFMAC",   BMOD_ADD_HYDROGENS_REFMAC },
      { "BMOD_ASSIGN_HETATMS_RESIDUE", BMOD_ASSIGN_HETATMS_RESIDUE },
      { "BMOD_INVERT_CHIRAL",          BMOD_INVERT_CHIRAL },
      { "BMOD_PHOSPHORYLATE",          BMOD_PHOSPHORYLATE },
      { "BMOD_WHATS_THIS",             BMOD_WHATS_THIS },
      { "BMOD_MORPH_FIT_7",            BMOD_MORPH_FIT_7 },
      { "BMOD_MORPH_FIT_11",           BMOD_MORPH_FIT_11 },
      { "BMOD_MORPH_FIT_SS",           BMOD_MORPH_FIT_SS },
      { "BMOD_FIND_HELICES",           BMOD_FIND_HELICES },
      { "BMOD_FIND_STRANDS",           BMOD_FIND_STRANDS },
      { "BMOD_BACKRUB_ON",             BMOD_BACKRUB_ON },
      { "BMOD_BACKRUB_OFF",            BMOD_BACKRUB_OFF },
      { "BMOD_DUPLICATE_RANGE",        BMOD_DUPLICATE_RANGE },
      { "BMOD_SYMM_SHIFT_REF_CHAIN",   BMOD_SYMM_SHIFT_REF_CHAIN },
      { "BMOD_FETCH_PDBE_THIS_LIGAND", BMOD_FETCH_PDBE_THIS_LIGAND },
   };
   return ops;
}

static const std::map<std::string, int> &glyco_direct_ops();

bool bandicoot_core_is_modelling_direct(const std::string &id) {
   return modelling_direct_ops().count(id) > 0 || glyco_direct_ops().count(id) > 0;
}

static const std::map<std::string, int> &glyco_direct_ops() {
   static const std::map<std::string, int> ops = {
      { "BGLYCO_SET_DEFAULT_B",          BGLYCO_SET_DEFAULT_B },
      { "BGLYCO_NLINK_NAG_NAG_BMA",      BGLYCO_NLINK_NAG_NAG_BMA },
      { "BGLYCO_ADD_HIGH_MANNOSE",       BGLYCO_ADD_HIGH_MANNOSE },
      { "BGLYCO_ADD_HYBRID_MAMMAL",      BGLYCO_ADD_HYBRID_MAMMAL },
      { "BGLYCO_ADD_COMPLEX_MAMMAL",     BGLYCO_ADD_COMPLEX_MAMMAL },
      { "BGLYCO_ADD_COMPLEX_PLANT",      BGLYCO_ADD_COMPLEX_PLANT },
      { "BGLYCO_DELETE_ALL",             BGLYCO_DELETE_ALL },
      { "BGLYCO_TORSION_FIT",            BGLYCO_TORSION_FIT },
      { "BGLYCO_TORSION_FIT_REFINE",     BGLYCO_TORSION_FIT_REFINE },
      { "BGLYCO_SYNTH_PYRANOSE_PLANES",  BGLYCO_SYNTH_PYRANOSE_PLANES },
      { "BGLYCO_UNIMODAL_RING_TORSIONS", BGLYCO_UNIMODAL_RING_TORSIONS },
      { "BGLYCO_DISPLAY_EXTRA_REST",     BGLYCO_DISPLAY_EXTRA_REST },
      { "BGLYCO_UNDISPLAY_EXTRA_REST",   BGLYCO_UNDISPLAY_EXTRA_REST },
      { "BGLYCO_EXTRACT_TREE",           BGLYCO_EXTRACT_TREE },
   };
   return ops;
}

bool bandicoot_core_modelling_direct(const std::string &id) {
   auto it = modelling_direct_ops().find(id);
   if (it != modelling_direct_ops().end()) {
      bandicoot_modelling_dispatch(it->second);
      return true;
   }
   auto g = glyco_direct_ops().find(id);
   if (g != glyco_direct_ops().end()) {
      bandicoot_glyco_dispatch(g->second);
      return true;
   }
   return false;
}

int import_rdkit_mol_from_smiles(const std::string &smiles, const std::string &comp_id); // sdf-interface.hh

static const std::map<std::string, int> &ligand_direct_ops() {
   static const std::map<std::string, int> ops = {
      { "BLIG_MOLPROBITY_DOTS",      BLIG_MOLPROBITY_DOTS },
      { "BLIG_COOT_LIGAND_DOTS",     BLIG_COOT_LIGAND_DOTS },
      { "BLIG_ALL_ATOM_DOTS",        BLIG_ALL_ATOM_DOTS },
      { "BLIG_JIGGLE_FIT",           BLIG_JIGGLE_FIT },
      { "BLIG_HYDROGENATE",          BLIG_HYDROGENATE },
      { "BLIG_CONTACT_DOTS_LIGAND",  BLIG_CONTACT_DOTS_LIGAND },
      { "BLIG_TOGGLE_FLEV",          BLIG_TOGGLE_FLEV },
      { "BLIG_SOLID_OBJECTS",        BLIG_SOLID_OBJECTS },
      { "BLIG_UNSOLID_OBJECTS",      BLIG_UNSOLID_OBJECTS },
      { "BLIG_CHEMICAL_FEATURES",    BLIG_CHEMICAL_FEATURES },
      { "BLIG_TABULATE_DISTORTIONS", BLIG_TABULATE_DISTORTIONS },
      { "BLIG_DISPLAY_DISTORTIONS",  BLIG_DISPLAY_DISTORTIONS },
      { "BLIG_QUICK_VALIDATE",       BLIG_QUICK_VALIDATE },
   };
   return ops;
}

bool bandicoot_core_is_ligand_direct(const std::string &id) {
   return ligand_direct_ops().count(id) > 0;
}

bool bandicoot_core_ligand_direct(const std::string &id) {
   auto it = ligand_direct_ops().find(id);
   if (it == ligand_direct_ops().end()) return false;
   bandicoot_ligand_dispatch(it->second);
   return true;
}

static const std::map<std::string, std::function<void()>> &draw_direct_ops() {
   static const std::map<std::string, std::function<void()>> ops = {
      { "on_display_only_active1_activate", []() {
           display_only_active();
           bandicoot_wx_molecules_changed();
        } },
      { "on_label_atoms_in_residue1_activate", label_atoms_in_residue },
      { "on_label_neighbours1_activate",       label_neighbours },
      { "on_undo_last_navigation1_activate",   undo_last_move },
      { "on_rock_view_on_off1_activate",       toggle_idle_rock_function },
      { "on_spin_view_on_off1_activate",       toggle_idle_spin_function },
   };
   return ops;
}

bool bandicoot_core_is_draw_direct(const std::string &id) {
   return draw_direct_ops().count(id) > 0;
}

bool bandicoot_core_draw_direct(const std::string &id) {
   auto it = draw_direct_ops().find(id);
   if (it == draw_direct_ops().end()) return false;
   it->second();
   return true;
}

bandicoot_aniso bandicoot_core_aniso() {
   bandicoot_aniso a;
   a.show = get_show_aniso() == 1;
   a.limit = get_show_limit_aniso() == 1;
   a.radius = graphics_info_t::show_aniso_atoms_radius;
   a.probability = get_aniso_probability();
   return a;
}

void bandicoot_core_set_aniso(const bandicoot_aniso &a) {
   set_limit_aniso(a.limit ? 1 : 0);
   // set_aniso_limit_size_from_widget's range, without its fallback to 10 A
   if (a.radius >= 0.0f && a.radius < 99999.9f) graphics_info_t::show_aniso_atoms_radius = a.radius;
   set_show_aniso(a.show ? 1 : 0);
}

void bandicoot_core_set_aniso_probability(float percent) {
   set_aniso_probability(percent);
}

// ---- Draw > Additional Representation

// every atom; the Display Manager names it rather than showing the string
static const std::string entire_molecule_selection = "/*/*/*";

bool bandicoot_core_add_representation(const bandicoot_add_rep_request &r, std::string &why_not) {
   int imol = r.imol;
   coot::atom_selection_info_t asi;
   if (r.by == 0) {
      std::pair<bool, std::pair<int, coot::atom_spec_t> > aa = active_atom_spec();
      if (! aa.first) { why_not = "no residue at the centre"; return false; }
      imol = aa.second.first;
      const coot::atom_spec_t &spec = aa.second.second;
      asi = coot::atom_selection_info_t(spec.chain_id, spec.res_no, spec.res_no, spec.ins_code);
   } else if (r.by == 1) {
      asi = coot::atom_selection_info_t(r.chain_id, r.resno_start, r.resno_end, r.ins_code);
   } else if (r.by == 2) {
      if (r.selection.empty()) { why_not = "no atom selection given"; return false; }
      asi = coot::atom_selection_info_t(r.selection);
   } else {
      asi = coot::atom_selection_info_t(entire_molecule_selection);
   }
   if (! is_valid_model_molecule(imol)) { why_not = "no model chosen"; return false; }
   if (r.by != 0) graphics_info_t::add_reps_molecule_combobox_molecule = imol;
   graphics_info_t g;
   // the representation types and widths Coot's window uses
   int type = r.ball_and_stick ? coot::BALL_AND_STICK : coot::SIMPLE_LINES;
   float width = r.ball_and_stick ? 0.15f : r.bond_width;
   gl_context_info_t glci(graphics_info_t::glarea, graphics_info_t::glarea_2);
   g.molecules[imol].add_additional_representation(type, coot::NORMAL_BONDS, width, true, asi,
                                                   g.display_control_window(), glci, g.Geom_p());
   graphics_draw();
   bandicoot_wx_molecules_changed();
   return true;
}

int bandicoot_core_add_rep_molecule() {
   return graphics_info_t::add_reps_molecule_combobox_molecule;
}

bool bandicoot_core_chain_residue_range(int imol, const std::string &chain_id, int &first, int &last) {
   if (! is_valid_model_molecule(imol)) return false;
   mmdb::Model *model = graphics_info_t::molecules[imol].atom_sel.mol->GetModel(1);
   if (! model) return false;
   for (int ich=0; ich<model->GetNumberOfChains(); ich++) {
      mmdb::Chain *chain = model->GetChain(ich);
      if (chain && chain_id == chain->GetChainID() && chain->GetNumberOfResidues() > 0) {
         std::pair<int, int> r = coot::util::min_and_max_residues(chain);
         first = r.first;
         last = r.second;
         return true;
      }
   }
   return false;
}

void bandicoot_core_set_additional_representation_shown(int imol, int index, bool shown) {
   set_show_additional_representation(imol, index, shown ? 1 : 0);
}

void bandicoot_core_set_all_additional_representations_shown(int imol, bool shown) {
   set_show_all_additional_representations(imol, shown ? 1 : 0);
}

// ---- Draw > Cell & Symmetry

static int first_molecule() {
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_model_molecule(imol) || is_valid_map_molecule(imol)) return imol;
   return -1;
}

bandicoot_symmetry bandicoot_core_symmetry() {
   bandicoot_symmetry s;
   s.shown = get_show_symmetry() == 1;
   s.radius = graphics_info_t::symmetry_search_radius;
   s.colour.r = graphics_info_t::symmetry_colour[0];
   s.colour.g = graphics_info_t::symmetry_colour[1];
   s.colour.b = graphics_info_t::symmetry_colour[2];
   s.colour_merge = graphics_info_t::symmetry_colour_merge_weight;
   s.expanded_labels = graphics_info_t::symmetry_atom_labels_expanded_flag;
   int imol = first_molecule();
   s.unit_cells = imol >= 0 && get_show_unit_cell(imol) == 1;
   return s;
}

void bandicoot_core_set_symmetry_shown(bool shown) {
   set_show_symmetry_master(shown ? 1 : 0);
}

void bandicoot_core_set_symmetry_always_on() {
   add_symmetry_on_to_preferences_and_apply();
}

bool bandicoot_core_set_symmetry_radius(float radius) {
   // set_symmetry_size_from_widget's range, without its fallback to 10 A
   if (! (radius > 0.0f && radius < 9999.9f)) return false;
   set_symmetry_size_from_widget(std::to_string(radius).c_str());
   return true;
}

void bandicoot_core_set_symmetry_colour(const bandicoot_rgb &colour) {
   graphics_info_t::symmetry_colour[0] = colour.r;
   graphics_info_t::symmetry_colour[1] = colour.g;
   graphics_info_t::symmetry_colour[2] = colour.b;
   graphics_draw();
}

void bandicoot_core_set_symmetry_colour_merge(float merge) {
   set_symmetry_colour_merge(merge);
}

void bandicoot_core_set_symmetry_expanded_labels(bool expanded) {
   set_symmetry_atom_labels_expanded(expanded ? 1 : 0);
}

void bandicoot_core_set_unit_cells_shown(bool shown) {
   set_show_unit_cells_all(shown ? 1 : 0);
}

std::vector<bandicoot_molecule_symmetry> bandicoot_core_molecule_symmetries() {
   std::vector<bandicoot_molecule_symmetry> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (! is_valid_model_molecule(imol)) continue;
      const molecule_class_info_t &m = graphics_info_t::molecules[imol];
      bandicoot_molecule_symmetry s;
      s.imol = imol;
      s.label = "Molecule " + std::to_string(imol) + " " + m.name_for_display_manager();
      s.shown = m.show_symmetry;
      s.display = m.symmetry_as_calphas ? 2 : m.symmetry_whole_chain_flag ? 1 : 0;
      s.colouring = m.symmetry_colour_by_symop_flag ? 1 : m.symmetry_rotate_colour_map_flag ? 2 : 0;
      v.push_back(s);
   }
   return v;
}

void bandicoot_core_set_molecule_symmetry_shown(int imol, bool shown) {
   set_show_symmetry_molecule(imol, shown ? 1 : 0);
}

// the pairs of calls Coot's window makes for each choice
void bandicoot_core_set_molecule_symmetry_display(int imol, int display) {
   if (display == 2) {
      symmetry_as_calphas(imol, 1);
   } else {
      symmetry_as_calphas(imol, 0);
      set_symmetry_whole_chain(imol, display == 1 ? 1 : 0);
   }
}

void bandicoot_core_set_molecule_symmetry_colouring(int imol, int colouring) {
   set_symmetry_colour_by_symop(imol, colouring == 1 ? 1 : 0);
   set_symmetry_molecule_rotate_colour_map(imol, colouring == 0 ? 0 : 1);
}

// ---- Draw > Generic Display Objects

std::vector<bandicoot_generic_object> bandicoot_core_generic_objects() {
   std::vector<bandicoot_generic_object> v;
   const std::vector<coot::generic_display_object_t> &objs = *graphics_info_t::generic_objects_p;
   for (unsigned int i=0; i<objs.size(); i++)
      if (! objs[i].is_closed_flag)
         v.push_back({ int(i), objs[i].name, objs[i].is_displayed_flag != 0 });
   return v;
}

void bandicoot_core_set_generic_object_displayed(int index, bool displayed) {
   set_display_generic_object(index, displayed ? 1 : 0);
}

void bandicoot_core_set_all_generic_objects_displayed(bool displayed) {
   set_display_all_generic_objects(displayed ? 1 : 0);
}

void bandicoot_core_close_all_generic_objects() {
   close_all_generic_objects();
}

// ---- Draw > NCS Ghost Control

std::vector<bandicoot_ncs_molecule> bandicoot_core_ncs_molecules() {
   std::vector<bandicoot_ncs_molecule> v;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (! is_valid_model_molecule(imol)) continue;
      const molecule_class_info_t &m = graphics_info_t::molecules[imol];
      std::vector<coot::ghost_molecule_display_t> ghosts = m.NCS_ghosts();
      if (ghosts.empty()) continue;
      bandicoot_ncs_molecule n;
      n.imol = imol;
      n.label = "Molecule " + std::to_string(imol) + " " + m.name_for_display_manager();
      n.ghosts_shown = m.draw_ncs_ghosts_p();
      n.chains = coot::util::chains_in_molecule(m.atom_sel.mol);
      const std::string &master = ghosts[0].target_chain_id;
      for (unsigned int ich=0; ich<n.chains.size(); ich++) {
         if (n.chains[ich] == master) n.master = ich;
         bool shown = false;
         for (const coot::ghost_molecule_display_t &gh : ghosts)
            if (gh.chain_id == n.chains[ich]) shown = gh.display_it_flag;
         n.chain_shown.push_back(shown && n.chains[ich] != master);
      }
      v.push_back(n);
   }
   return v;
}

void bandicoot_core_set_ncs_ghosts_shown(int imol, bool shown) {
   set_draw_ncs_ghosts(imol, shown ? 1 : 0);
}

void bandicoot_core_set_ncs_ghost_chain_shown(int imol, int ichain, bool shown) {
   ncs_control_display_chain(imol, ichain, shown ? 1 : 0);
}

void bandicoot_core_set_ncs_master(int imol, int ichain) {
   ncs_control_change_ncs_master_to_chain(imol, ichain);
   if (! is_valid_model_molecule(imol)) return;
   std::vector<std::string> chains =
      coot::util::chains_in_molecule(graphics_info_t::molecules[imol].atom_sel.mol);
   for (int ich=0; ich<int(chains.size()); ich++)
      if (ich != ichain) ncs_control_display_chain(imol, ich, 1);
}

// ---- Draw > Screenshot

bool bandicoot_core_screenshot(const std::string &path) {
   bool ok = false;
   bandicoot_wx_surface_read_back([&ok, path]() { ok = graphics_info_t::screendump_image(path) != 0; });
   struct stat st;
   return ok && stat(path.c_str(), &st) == 0;
}

static bool image_written(const std::string &image_path, time_t since) {
   struct stat st;
   return stat(image_path.c_str(), &st) == 0 && st.st_mtime >= since;
}

// Where raytrace looks for a renderer: the directory named by env_dir, if
// given, then PATH.
static bool program_found(const std::string &name, const char *env_dir) {
   std::vector<std::string> dirs;
   if (env_dir && getenv(env_dir)) dirs.push_back(getenv(env_dir));
   if (const char *path = getenv("PATH")) {
      std::string p(path);
      size_t start = 0;
      while (start <= p.size()) {
         size_t end = p.find(':', start);
         if (end == std::string::npos) end = p.size();
         dirs.push_back(p.substr(start, end - start));
         start = end + 1;
      }
   }
   for (const std::string &d : dirs) {
      if (d.empty()) continue;
      std::string f = d + "/" + name;
      if (access(f.c_str(), X_OK) == 0) return true;
   }
   std::cout << "INFO:: " << name << " not found" << (env_dir ? std::string(" in $") + env_dir + " or" : "")
             << " on PATH " << (getenv("PATH") ? getenv("PATH") : "") << std::endl;
   return false;
}

// find_exe's places, as Coot's probe script looks: PATH, $CBIN, $CCP4_BIN
bool bandicoot_core_probe_clashes(int imol, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "no such model"; return false; }
   for (const char *program : { "reduce", "probe" }) {
      if (! program_found(program, "CBIN") && ! program_found(program, "CCP4_BIN")) {
         why_not = std::string(program) + " was not found";
         return false;
      }
   }
   safe_python_command("probe(" + std::to_string(imol) + ")");
   return true;
}

bool bandicoot_core_povray_image(const std::string &path, std::string &image_path, bool &renderer_found) {
   renderer_found = program_found("povray", nullptr);
   time_t start = time(nullptr);
   make_image_povray(path.c_str());
   image_path = path + ".png";   // the name raytrace gives it
   return image_written(image_path, start);
}

bool bandicoot_core_raster3d_image(const std::string &path, std::string &image_path, bool &renderer_found) {
   renderer_found = program_found("render", "CCP4_BIN");
   time_t start = time(nullptr);
   make_image_raster3d(path.c_str());
   image_path = path;
   return image_written(image_path, start);
}

// ---- Draw > Representation Tools

// A Python string literal holding s.
static std::string py_quote(const std::string &s) {
   std::string q = "'";
   for (char c : s) {
      if (c == '\\' || c == '\'') q += '\\';
      if (c == '\n') { q += "\\n"; continue; }
      q += c;
   }
   return q + "'";
}

static void run_python(const std::string &code) {
   bool had_error = false;
   std::string out = bandicoot_core_console_run(code, had_error);
   if (! out.empty()) std::cout << out << (out.back() == '\n' ? "" : "\n");
}

// Coot's Highlight Interesting Site: the residue in ball-and-stick in a
// copy of its own colour, its neighbours in ball-and-stick, distances shown.
static bool highlight_site(std::string &why_not) {
   std::pair<bool, std::pair<int, coot::atom_spec_t> > aa = active_atom_spec();
   if (! aa.first) { why_not = "No residue at the centre"; return false; }
   int imol = aa.second.first;
   coot::residue_spec_t centre(aa.second.second);
   std::vector<coot::residue_spec_t> others =
      graphics_info_t::molecules[imol].residues_near_residue(centre, 4.0);
   std::string sel = "//" + centre.chain_id + "/" + std::to_string(centre.res_no);
   if (! centre.ins_code.empty()) sel += "." + centre.ins_code;
   int imol_new = new_molecule_by_atom_selection(imol, sel.c_str());
   int draw_h = draw_hydrogens_state(imol);
   if (is_valid_model_molecule(imol_new)) {
      set_mol_active(imol_new, 0);
      set_show_environment_distances(1);
      set_molecule_bonds_colour_map_rotation(imol_new, 230);
      additional_representation_by_attributes(imol_new, centre.chain_id.c_str(), centre.res_no, centre.res_no,
                                              centre.ins_code.c_str(), coot::BALL_AND_STICK, 1, 0.14, draw_h);
   }
   for (const coot::residue_spec_t &r : others)
      additional_representation_by_attributes(imol, r.chain_id.c_str(), r.res_no, r.res_no,
                                              r.ins_code.c_str(), coot::BALL_AND_STICK, 1, 0.14, draw_h);
   bandicoot_wx_molecules_changed();
   return true;
}

static bool grey_carbons(bool grey, std::string &why_not) {
   std::pair<bool, std::pair<int, coot::atom_spec_t> > aa = active_atom_spec();
   if (! aa.first) { why_not = "No molecule at the centre"; return false; }
   set_use_grey_carbons_for_molecule(aa.second.first, grey ? 1 : 0);
   return true;
}

static void stick_balls(bool on) {
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
      if (is_valid_model_molecule(imol)) set_draw_stick_mode_atoms(imol, on ? 1 : 0);
}

static bool ccp4mg(std::string &why_not) {
   // Coot's picture description, beside the files it reads
   std::string pd = coot::util::current_working_dir() + "/1.mgpic.py";
   write_ccp4mg_picture_description(pd.c_str());
   std::cout << "INFO:: wrote " << pd << std::endl;
   if (! program_found("ccp4mg", nullptr)) { why_not = "CCP4MG was not found"; return false; }
   std::string cmd = "ccp4mg -pict '" + pd + "' &";
   if (system(cmd.c_str()) != 0) { why_not = "CCP4MG could not be started"; return false; }
   return true;
}

static const std::map<std::string, std::function<bool(std::string &)>> &representation_ops() {
   static const std::map<std::string, std::function<bool(std::string &)>> ops = {
      { "BREP_UNDO_SYMMETRY_VIEW", [](std::string &why) {
           if (undo_symmetry_view()) return true;
           why = "No symmetry copy at the centre";
           return false;
        } },
      { "BREP_BALLS_ON",  [](std::string &) { stick_balls(true);  return true; } },
      { "BREP_BALLS_OFF", [](std::string &) { stick_balls(false); return true; } },
      { "BREP_GREY_CARBONS",     [](std::string &why) { return grey_carbons(true, why); } },
      { "BREP_COLOURED_CARBONS", [](std::string &why) { return grey_carbons(false, why); } },
      { "BREP_HIGHLIGHT_SITE", highlight_site },
      { "BREP_PLAY_VIEWS", [](std::string &why) {
           if (n_views() == 0) { why = "No views have been added"; return false; }
           go_to_first_view(1);
           play_views();
           return true;
        } },
      { "BREP_REMOVE_ANNOTATION_HERE",  [](std::string &) { run_python("remove_annotation_here()"); return true; } },
      { "BREP_REMOVE_ANNOTATION_CLICK", [](std::string &) {
           add_status_bar_text("Click near the annotation to remove");
           run_python("remove_annotation_at_click()");
           return true;
        } },
      { "BREP_CCP4MG", ccp4mg },
   };
   return ops;
}

bool bandicoot_core_is_representation_direct(const std::string &id) {
   return representation_ops().count(id) > 0;
}

bool bandicoot_core_representation_direct(const std::string &id, std::string &why_not) {
   auto it = representation_ops().find(id);
   if (it == representation_ops().end()) return false;
   return it->second(why_not);
}

int bandicoot_core_ball_and_stick(int imol, const std::string &selection) {
   // the sizes Coot's menu item used
   int handle = make_ball_and_stick(imol, selection.c_str(), 0.18, 0.3, 1);
   std::cout << "INFO:: ball and stick handle " << handle << std::endl;
   return handle;
}

void bandicoot_core_clear_ball_and_stick(int imol) {
   clear_ball_and_stick(imol);
}

int bandicoot_core_dotted_surface(int imol, const std::string &selection) {
   if (! is_valid_model_molecule(imol)) return -1;
   // nothing matched would make an empty, invisible object
   mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
   int sel = mol->NewSelection();
   mol->Select(sel, mmdb::STYPE_ATOM, selection.c_str(), mmdb::SKEY_NEW);
   int n = mol->GetSelLength(sel);
   mol->DeleteSelection(sel);
   if (n == 0) return -1;
   // one colour reads better than colour by atom
   set_dots_colour(imol, 0.5, 0.5, 0.5);
   int handle = dots(imol, selection.c_str(), selection.c_str(), 2, 1);
   std::string s = "Dotted surface handle " + std::to_string(handle);
   std::cout << "INFO:: " << s << std::endl;
   add_status_bar_text(s.c_str());
   return handle;
}

void bandicoot_core_clear_dots(int imol, int handle) {
   clear_dots(imol, handle);
}

float bandicoot_core_map_radius() {
   return get_map_radius();
}

void bandicoot_core_limit_model_display_radius(float radius) {
   if (radius < 0.1f) set_model_display_radius(0, 10);
   else set_model_display_radius(1, radius);
}

void bandicoot_core_label_all_cas(int imol) {
   if (! is_valid_model_molecule(imol)) return;
   mmdb::Model *model = graphics_info_t::molecules[imol].atom_sel.mol->GetModel(1);
   if (! model) return;
   for (int ich=0; ich<model->GetNumberOfChains(); ich++) {
      mmdb::Chain *chain = model->GetChain(ich);
      if (! chain || is_solvent_chain_p(imol, chain->GetChainID()) == 1) continue;
      for (int ir=0; ir<chain->GetNumberOfResidues(); ir++) {
         mmdb::Residue *r = chain->GetResidue(ir);
         if (r) add_atom_label(imol, const_cast<char *>(chain->GetChainID()), r->GetSeqNum(), const_cast<char *>(" CA "));
      }
   }
   graphics_draw();
}

bandicoot_xyz bandicoot_core_rotation_centre() {
   bandicoot_xyz c;
   c.x = graphics_info_t::RotationCentre_x();
   c.y = graphics_info_t::RotationCentre_y();
   c.z = graphics_info_t::RotationCentre_z();
   return c;
}

void bandicoot_core_hole(int imol, const bandicoot_xyz &start, const bandicoot_xyz &end,
                         const std::string &dots_file) {
   // Coot's colouring; its probe-radius graph is a GTK window, so it is not drawn
   hole(imol, start.x, start.y, start.z, end.x, end.y, end.z, 1, 0, 1, false, dots_file);
}

std::string bandicoot_core_next_view_name() {
   // "View", then "View-1", "View-2" ... as Coot names them
   std::vector<std::string> names = bandicoot_core_views();
   for (int n=0; ; n++) {
      std::string name = n == 0 ? "View" : "View-" + std::to_string(n);
      if (std::find(names.begin(), names.end(), name) == names.end()) return name;
   }
}

void bandicoot_core_add_view(const std::string &name) {
   add_view_here(name.c_str());
}

void bandicoot_core_add_spin_view(int steps, float degrees) {
   add_spin_view("Spin", steps, degrees);
}

std::vector<std::string> bandicoot_core_views() {
   std::vector<std::string> v;
   for (const coot::view_info_t &view : graphics_info_t::views) v.push_back(view.view_name);
   return v;
}

void bandicoot_core_go_to_view(int index) {
   go_to_view_number(index, 0);
}

float bandicoot_core_views_play_speed() {
   return views_play_speed();
}

void bandicoot_core_set_views_play_speed(float speed) {
   set_views_play_speed(speed);
}

void bandicoot_core_save_views(const std::string &path) {
   save_views(path.c_str());
}

void bandicoot_core_add_annotation(const std::string &text) {
   run_python("add_annotation_here(" + py_quote(text) + ")");
}

void bandicoot_core_save_annotations(const std::string &path) {
   run_python("save_annotations(" + py_quote(path) + ")");
}

void bandicoot_core_load_annotations(const std::string &path) {
   run_python("load_annotations(" + py_quote(path) + ")");
}

std::string bandicoot_core_dark_density_warning() {
   for (int i=0; i<graphics_info_t::n_molecules(); i++) {
      const molecule_class_info_t &m = graphics_info_t::molecules[i];
      if (m.has_xmap() && m.is_displayed_p() && background_is_black_p() && m.map_is_too_blue_p())
         return "Consider making the map brighter:\nblue projects badly.";
   }
   return std::string();
}

bandicoot_find_ligands bandicoot_core_find_ligands_setup() {
   graphics_info_t g;
   bandicoot_find_ligands f;
   f.atom_limit = graphics_info_t::find_ligand_ligand_atom_limit;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      std::string label = std::to_string(imol) + " " + g.molecules[imol].name_;
      if (g.molecules[imol].has_xmap())
         f.maps.push_back({ imol, label, 0, map_is_difference_map(imol) == 1 });
      int n = g.molecules[imol].atom_sel.n_selected_atoms;
      if (n > 0)
         f.proteins.push_back({ imol, label, n, false });
      if (g.molecules[imol].has_model() && n < f.atom_limit)
         f.ligands.push_back({ imol, label, n, false });
   }
   f.mask_waters  = graphics_info_t::find_ligand_mask_waters_flag;
   f.here         = graphics_info_t::find_ligand_here_cluster_flag;
   f.sigma        = graphics_info_t::ligand_cluster_sigma_level;
   f.n_top_sites  = graphics_info_t::find_ligand_n_top_ligands;
   f.n_conformers = graphics_info_t::ligand_wiggly_ligand_n_samples;
   f.multi        = graphics_info_t::find_ligand_multiple_solutions_per_cluster_flag;
   // as the GTK window: the stored fractions show only with multi-solutions on
   if (f.multi) {
      f.frac_scoring = graphics_info_t::find_ligand_score_by_correl_frac_limit;
      f.frac_correl  = graphics_info_t::find_ligand_score_correl_frac_interesting_limit;
   }
   f.refine = g.find_ligand_do_real_space_refine_state();
   return f;
}

void bandicoot_core_find_ligands(const bandicoot_find_ligands &f) {
   graphics_info_t g;
   graphics_info_t::find_ligand_mask_waters_flag = f.mask_waters;
   graphics_info_t::find_ligand_here_cluster_flag = f.here;
   graphics_info_t::ligand_cluster_sigma_level = f.sigma;
   graphics_info_t::find_ligand_n_top_ligands = f.n_top_sites;
   graphics_info_t::ligand_wiggly_ligand_n_samples = f.n_conformers;
   graphics_info_t::find_ligand_multiple_solutions_per_cluster_flag = f.multi;
   graphics_info_t::find_ligand_score_by_correl_frac_limit = f.frac_scoring;
   graphics_info_t::find_ligand_score_correl_frac_interesting_limit = f.frac_correl;
   g.set_find_ligand_do_real_space_refine_state(f.refine);
   g.set_find_ligands_mols(f.map, f.protein, f.chosen);
   execute_ligand_search();
}

bool bandicoot_wx_text_window(const std::string &title, const std::string &text) {
   if (! ui_view.text_window) return false;
   ui_view.text_window(title, text);
   return true;
}

bool bandicoot_wx_ligand_check(const std::string &residue, int density, int geometry, int bumps) {
   if (! ui_view.ligand_check) return false;
   ui_view.ligand_check(residue, density, geometry, bumps);
   return true;
}

bool bandicoot_core_active_residue(int &imol, std::string &chain_id, int &resno,
                                   std::string &ins_code, std::string &res_name) {
   std::pair<bool, std::pair<int, coot::atom_spec_t>> aa = active_atom_spec();
   if (! aa.first) return false;
   imol = aa.second.first;
   const coot::atom_spec_t &spec = aa.second.second;
   chain_id = spec.chain_id; resno = spec.res_no; ins_code = spec.ins_code;
   res_name = residue_name(imol, chain_id.c_str(), resno, ins_code.c_str());
   return true;
}

bool bandicoot_core_smiles_to_3d(const std::string &smiles, const std::string &comp_id,
                                 std::string &why_not) {
   if (! enhanced_ligand_coot_p()) { why_not = "this build has no RDKit"; return false; }
   int imol = import_rdkit_mol_from_smiles(smiles, comp_id);
   if (! is_valid_model_molecule(imol)) {
      why_not = "RDKit could not build a molecule from " + smiles;
      return false;
   }
   graphics_draw();
   return true;
}

bool bandicoot_core_rename_to_reference(int imol, const std::string &chain_id, int resno,
                                        const std::string &ins_code,
                                        const std::string &reference_comp_id,
                                        const std::string &new_comp_id,
                                        const std::string &cif_file_name, std::string &why_not) {
   // the core says nothing on failure except to the terminal, so success is
   // the dictionary file being written
   time_t start = time(nullptr);
   match_this_residue_and_dictionary(imol, chain_id, resno, ins_code, cif_file_name,
                                     reference_comp_id, new_comp_id);
   struct stat after;
   if (stat(cif_file_name.c_str(), &after) != 0 || after.st_mtime < start) {
      why_not = "no dictionary was written (no restraints for this residue or for "
                + reference_comp_id + ", or no atoms matched)";
      return false;
   }
   std::cout << "INFO:: wrote " << cif_file_name << std::endl;
   return true;
}

bool bandicoot_core_rename_active_residue(const std::string &name, std::string &why_not) {
   std::pair<bool, std::pair<int, coot::atom_spec_t>> aa = active_atom_spec();
   if (! aa.first) { why_not = "no residue at the screen centre"; return false; }
   const coot::atom_spec_t &spec = aa.second.second;
   set_residue_name(aa.second.first, spec.chain_id.c_str(), spec.res_no, spec.ins_code.c_str(), name.c_str());
   return true;
}

bool bandicoot_core_monomer_from_dictionary(const std::string &comp_id, std::string &why_not) {
   int imol = get_monomer_from_dictionary(comp_id, 0);   // the dictionary's own coordinates
   if (! is_valid_model_molecule(imol)) imol = get_monomer(comp_id);
   if (! is_valid_model_molecule(imol)) {
      why_not = "no dictionary entry or monomer for " + comp_id;
      return false;
   }
   return true;
}

bool bandicoot_core_new_molecule_by_sphere(int imol, float radius, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "not a model molecule"; return false; }
   int made = new_molecule_by_sphere_selection(imol, rotation_centre_position(0), rotation_centre_position(1),
                                               rotation_centre_position(2), radius, 0);
   if (! is_valid_model_molecule(made)) { why_not = "no atoms within that radius"; return false; }
   return true;
}

bool bandicoot_core_new_molecule_by_symop(int imol, const std::string &symop, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "not a model molecule"; return false; }
   try {
      // the same shift to the origin Coot applies before a symmetry copy
      clipper::Coord_frac cf = coot::util::shift_to_origin(graphics_info_t::molecules[imol].atom_sel.mol);
      int made = new_molecule_by_symop(imol, symop.c_str(), int(lround(cf.u())), int(lround(cf.v())),
                                       int(lround(cf.w())));
      if (! is_valid_model_molecule(made)) { why_not = "could not apply " + symop; return false; }
   } catch (const std::runtime_error &rte) {
      why_not = rte.what();
      return false;
   }
   return true;
}

bool bandicoot_core_residue_type_selection(int imol, const std::string &residue_type, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "not a model molecule"; return false; }
   int made = new_molecule_by_residue_type_selection(imol, residue_type.c_str());
   if (! is_valid_model_molecule(made)) { why_not = "no " + residue_type + " residues"; return false; }
   update_go_to_atom_window_on_new_mol();
   return true;
}

void bandicoot_core_fetch_pdbe_ligand(const std::string &comp_id) {
   // the name and SMILES come from the PDBe dictionary it reads
   safe_python_command("get_SMILES_for_comp_id_from_pdbe(" + single_quote(comp_id) + ")");
   get_monomer(comp_id.c_str());
   graphics_info_t g;
   std::pair<bool, std::string> name = g.Geom_p()->get_monomer_name(comp_id, coot::protein_geometry::IMOL_ENC_ANY);
   std::string smiles = SMILES_for_comp_id(comp_id);
   std::cout << "INFO:: PDBe " << comp_id << "   name: " << (name.first ? name.second : std::string("(none found)"))
             << "   SMILES: " << (smiles.empty() ? std::string("(none found)") : smiles) << std::endl;
   std::string s = comp_id + ":  " + (name.first && ! name.second.empty() ? name.second
                                                                           : std::string("<no PDBe description found>"));
   if (! smiles.empty()) s += "   [SMILES in console]";
   add_status_bar_text(s.c_str());
}

// in restraints-gui.cc, with the rows' rules
void bandicoot_restraint_rows_for_wx(bool show_all, std::vector<std::string> &comp_ids,
                                     std::vector<std::string> &labels, std::vector<bool> &needed);
bool bandicoot_generate_restraints_for_wx(bool show_all, const std::vector<std::string> &wanted,
                                          std::function<void(const std::string &)> progress,
                                          std::string &report);
void bandicoot_restraints_dialog();

std::vector<bandicoot_restraint_row> bandicoot_core_restraint_rows(bool show_all) {
   std::vector<std::string> ids, labels;
   std::vector<bool> needed;
   bandicoot_restraint_rows_for_wx(show_all, ids, labels, needed);
   std::vector<bandicoot_restraint_row> rows;
   for (size_t i = 0; i < ids.size(); i++) rows.push_back({ ids[i], labels[i], needed[i] });
   return rows;
}

bool bandicoot_core_generate_restraints(bool show_all, const std::vector<std::string> &comp_ids,
                                        std::function<void(const std::string &)> progress,
                                        std::string &report) {
   return bandicoot_generate_restraints_for_wx(show_all, comp_ids, progress, report);
}

static std::function<void(bool, const std::vector<std::string> &, bool)> restraints_view;

void bandicoot_core_set_restraints_view(
   std::function<void(bool show_all, const std::vector<std::string> &fresh, bool announce_empty)> open) {
   restraints_view = std::move(open);
}

bool bandicoot_wx_restraints(bool show_all, const std::vector<std::string> &fresh, bool announce_empty) {
   if (! restraints_view) return false;
   restraints_view(show_all, fresh, announce_empty);
   return true;
}

void bandicoot_core_open_ligand_restraints() {
   bandicoot_restraints_dialog();
}

bool bandicoot_core_modelling_on_model(const std::string &id, int imol) {
   static const std::map<std::string, std::function<void(int)>> ops = {
      { "BMOD_ARRANGE_WATERS",     [](int i) { move_waters_to_around_protein(i); } },
      { "BMOD_MERGE_WATER_CHAINS", [](int i) { safe_python_command("merge_solvent_chains(" + std::to_string(i) + ")"); } },
      { "BMOD_RENUMBER_WATERS",    [](int i) { renumber_waters(i); } },
      { "BMOD_ASSIGN_HETATM_MOL",  [](int i) { assign_hetatms(i); } },
      { "BMOD_FIX_NOMENCLATURE",   [](int i) { fix_nomenclature_errors(i); } },
      { "BMOD_REORDER_CHAINS",     [](int i) { sort_chains(i); } },
      { "BMOD_RIGID_BODY_FIT_MOL", [](int i) { rigid_body_refine_by_atom_selection(i, "//"); } },
      { "BMOD_USE_SEGIDS",         [](int i) { exchange_chain_ids_for_seg_ids(i); } },
      // the checks list what they find in the results window
      { "BMOD_RES_ALT_CONFS",      [](int i) { safe_python_command("alt_confs_gui(" + std::to_string(i) + ")"); } },
      { "BMOD_RES_CIS_PEPTIDES",   [](int i) { safe_python_command("cis_peptides_gui(" + std::to_string(i) + ")"); } },
      { "BMOD_RES_MISSING_ATOMS",  [](int i) { safe_python_command("missing_atoms_gui(" + std::to_string(i) + ")"); } },
      { "BMOD_RES_ZERO_OCC",       [](int i) { safe_python_command("zero_occ_atoms_gui(" + std::to_string(i) + ")"); } },
   };
   auto it = ops.find(id);
   if (it == ops.end() || ! is_valid_model_molecule(imol)) return false;
   it->second(imol);
   graphics_draw();
   return true;
}

// A Python string literal for any text, file names included.
static std::string python_literal(const std::string &s) {
   std::string r("'");
   for (char c : s) {
      if (c == '\\' || c == '\'') r += '\\';
      r += c;
   }
   return r + "'";
}

static bool residue_exists(int imol, const std::string &chain, int resno) {
   std::string c = chain, ins;
   return does_residue_exist_p(imol, &c[0], resno, &ins[0]) == 1;
}

bool bandicoot_core_rigid_body_fit_range(int imol, const std::string &chain, int first, int last,
                                         std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "not a model molecule"; return false; }
   if (! is_valid_map_molecule(imol_refinement_map())) { why_not = "no refinement map is set"; return false; }
   if (! residue_exists(imol, chain, first) || ! residue_exists(imol, chain, last)) {
      why_not = "no such residues in chain " + chain;
      return false;
   }
   safe_python_command("rigid_body_refine_by_residue_ranges(" + std::to_string(imol) + ", [[" +
                       python_literal(chain) + ", " + std::to_string(first) + ", " + std::to_string(last) + "]])");
   return true;
}

bool bandicoot_core_superpose_ligands(int imol_reference, const std::string &chain_reference, int resno_reference,
                                      int imol_moving, const std::string &chain_moving, int resno_moving,
                                      std::string &why_not) {
   if (! residue_exists(imol_reference, chain_reference, resno_reference)) {
      why_not = "no reference ligand " + chain_reference + " " + std::to_string(resno_reference);
      return false;
   }
   if (! residue_exists(imol_moving, chain_moving, resno_moving)) {
      why_not = "no moving ligand " + chain_moving + " " + std::to_string(resno_moving);
      return false;
   }
   safe_python_command("overlay_my_ligands(" + std::to_string(imol_moving) + ", " + python_literal(chain_moving) +
                       ", " + std::to_string(resno_moving) + ", " + std::to_string(imol_reference) + ", " +
                       python_literal(chain_reference) + ", " + std::to_string(resno_reference) + ")");
   graphics_draw();
   return true;
}

bool bandicoot_core_associate_sequence(int imol, const std::string &chain, const std::string &file, bool pir,
                                       bool all_protein_chains, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "not a model molecule"; return false; }
   if (! coot::file_exists(file)) { why_not = "no file " + file; return false; }
   safe_python_command("bandicoot_associate_sequence(" + std::to_string(imol) + ", " + python_literal(chain) + ", " +
                       python_literal(file) + ", " + (pir ? "'PIR'" : "'FASTA'") + ", " +
                       (all_protein_chains ? "True" : "False") + ")");
   return true;
}

bool bandicoot_core_cootaneer(int imol, const std::string &file, bool fit_protein_after, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "not a model molecule"; return false; }
   if (! is_valid_map_molecule(imol_refinement_map())) { why_not = "no refinement map is set"; return false; }
   if (! file.empty() && ! coot::file_exists(file)) { why_not = "no file " + file; return false; }
   safe_python_command("bandicoot_cootaneer(" + std::to_string(imol) + ", " + python_literal(file) + ", " +
                       (fit_protein_after ? "True" : "False") + ")");
   graphics_draw();
   return true;
}

std::vector<std::string> bandicoot_core_solvent_comp_ids() {
   return { "EDO", "GOL", "DMS", "ACT", "MPD", "CIT", "SO4", "PO4",
            "TRS", "TAM", "PEG", "PG4", "PE8", "EBE", "BTB" };
}

void bandicoot_core_add_solvent(int imol, const std::string &comp_id) {
   if (! is_valid_model_molecule(imol) || comp_id.empty()) return;
   safe_python_command("bandicoot_add_solvent_ligand(" + std::to_string(imol) + ", " + python_literal(comp_id) + ")");
   graphics_draw();
}

void bandicoot_core_tools_pick(const std::string &widget, bool on) {
   if (widget == "cis_trans_conversion_toggle_button")
      do_cis_trans_conversion_setup(on ? 1 : 0);
   else if (widget == "model_refine_dialog_db_main_togglebutton")
      do_db_main(on ? 1 : 0);
   else if (widget == "reverse_fragment_direction_togglebutton")
      setup_reverse_direction(on ? 1 : 0);
   else if (widget == "other_tools_base_pair_toggle_button")
      setup_base_pairing(on ? 1 : 0);
}

void bandicoot_core_make_link_pick() {
   bandicoot_make_link_interactive();
}

bool bandicoot_core_place_helix_here(std::string &why_not) {
   if (! is_valid_map_molecule(imol_refinement_map())) { why_not = "no refinement map is set"; return false; }
   if (! is_valid_model_molecule(place_helix_here())) { why_not = "no helix fits the density here"; return false; }
   graphics_draw();
   return true;
}

bool bandicoot_core_place_strand_here(int n_residues, std::string &why_not) {
   if (! is_valid_map_molecule(imol_refinement_map())) { why_not = "no refinement map is set"; return false; }
   if (! is_valid_model_molecule(place_strand_here(n_residues, 15))) {
      why_not = "no strand fits the density here";
      return false;
   }
   graphics_draw();
   return true;
}

bool bandicoot_core_active_molecule_tool(const std::string &tool, std::string &why_not) {
   std::pair<bool, std::pair<int, coot::atom_spec_t>> aa = active_atom_spec();
   if (! aa.first) { why_not = "no molecule at the screen centre"; return false; }
   int imol = aa.second.first;
   if (tool == "Fill Partial Residues")
      fill_partial_residues(imol);
   else if (tool == "Delete Hydrogen Atoms")
      delete_hydrogens(imol);
   else if (tool == "Delete Side-chains for Active Chain")
      delete_sidechains_for_chain(imol, aa.second.second.chain_id.c_str());
   else
      return false;
   graphics_draw();
   return true;
}

void bandicoot_core_set_undo_molecule(int imol) {
   set_undo_molecule(imol);
}

bool bandicoot_core_find_secondary_structure(bool helices, int helix_template, int helix_length,
                                             bool strands, int strand_template, int strand_length,
                                             float radius, std::string &why_not) {
   if (! is_valid_map_molecule(imol_refinement_map())) { why_not = "no refinement map is set"; return false; }
   if (! helices && ! strands) { why_not = "neither helices nor strands are asked for"; return false; }
   int imol = find_secondary_structure_local(helices, helix_length, helix_template,
                                             strands, strand_length, strand_template, radius);
   if (! is_valid_model_molecule(imol)) { why_not = "no secondary structure found"; return false; }
   graphics_draw();
   return true;
}

bool bandicoot_core_find_nucleic_acids(float radius, std::string &why_not) {
   if (! is_valid_map_molecule(imol_refinement_map())) { why_not = "no refinement map is set"; return false; }
   if (! is_valid_model_molecule(find_nucleic_acids_local(radius))) {
      why_not = "no nucleic acid found";
      return false;
   }
   graphics_draw();
   return true;
}

bool bandicoot_core_add_oxt(int imol, const std::string &chain, bool c_terminus, int resno,
                            std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "not a model molecule"; return false; }
   if (c_terminus) {
      std::pair<bool, int> last = graphics_info_t::molecules[imol].last_protein_residue_in_chain(chain);
      if (! last.first) { why_not = "no protein residues in chain " + chain; return false; }
      resno = last.second;
   }
   if (! add_OXT_to_residue(imol, chain.c_str(), resno, "")) {
      why_not = "could not add OXT to " + chain + " " + std::to_string(resno);
      return false;
   }
   graphics_draw();
   return true;
}

bool bandicoot_core_ideal_nucleic_acid(bool dna, bool form_b, bool single_stranded, const std::string &sequence,
                                       std::string &why_not) {
   int imol = ideal_nucleic_acid(dna ? "DNA" : "RNA", form_b ? "B" : "A", single_stranded ? 1 : 0, sequence.c_str());
   if (! is_valid_model_molecule(imol)) { why_not = "could not build a model of " + sequence; return false; }
   graphics_draw();
   return true;
}

bool bandicoot_core_baton_start() {
   // the core asks for a skeleton in a GTK window when it finds none
   int skeleton = -1;
   if (bandicoot_core_map_skeletonized(graphics_info_t::map_for_skeletonize))
      skeleton = graphics_info_t::map_for_skeletonize;
   for (int imol = 0; skeleton == -1 && imol < graphics_info_t::n_molecules(); imol++)
      if (bandicoot_core_map_skeletonized(imol)) skeleton = imol;
   if (skeleton == -1) return false;
   graphics_info_t::map_for_skeletonize = skeleton;
   return try_set_draw_baton(1) != 0;
}

void bandicoot_core_baton_op(const std::string &op) {
   if      (op == "accept")      accept_baton_position();
   else if (op == "try another") baton_tip_try_another();
   else if (op == "previous")    baton_tip_previous();
   else if (op == "undo")        baton_build_delete_last_residue();
   else if (op == "lengthen")    lengthen_baton();
   else if (op == "shorten")     shorten_baton();
}

void bandicoot_core_baton_stop() {
   try_set_draw_baton(0);
   set_baton_mode(0);
}

void bandicoot_core_baton_build_params(int start_resno, const std::string &chain, bool backwards) {
   set_baton_build_params(start_resno, chain.c_str(), backwards ? "backwards" : "forwards");
}

bool bandicoot_core_ligand_from_smiles(const std::string &smiles, const std::string &code, std::string &message) {
   int n_before = graphics_info_t::n_molecules();
   // needs no PanDDA session; the module may not have been loaded yet
   safe_python_command("import bandicoot_pandda");
   message = bandicoot_core_pandda_call("ligand_from_smiles_standalone(" + python_literal(smiles) + ", " +
                                        python_literal(code) + ")");
   if (! message.empty()) add_status_bar_text(message.c_str());
   return graphics_info_t::n_molecules() > n_before;
}

void bandicoot_core_glycan_opened() {
   // sugars added or refined keep a proper chair
   safe_python_command("add_synthetic_pyranose_planes()");
   safe_python_command("use_unimodal_pyranose_ring_torsions()");
}

std::vector<bandicoot_glycan_link> bandicoot_core_glycan_links() {
   // the labels are the keys of bandicoot_glyco_button_states() (gui_add_linked_cho.py)
   return {
      { "Add a NAG-ASN NAG",   "NAG", "NAG-ASN"  }, { "Add a BETA1-4 NAG",   "NAG", "BETA1-4"  },
      { "Add a BETA1-4 BMA",   "BMA", "BETA1-4"  }, { "Add an ALPHA1-2 MAN", "MAN", "ALPHA1-2" },
      { "Add an ALPHA1-3 MAN", "MAN", "ALPHA1-3" }, { "Add an ALPHA2-3 MAN", "MAN", "ALPHA2-3" },
      { "Add an ALPHA2-3 GAL", "GAL", "ALPHA2-3" }, { "Add an ALPHA1-6 MAN", "MAN", "ALPHA1-6" },
      { "Add a BETA1-2 NAG",   "NAG", "BETA1-2"  }, { "Add a BETA1-4 GAL",   "GAL", "BETA1-4"  },
      { "Add an ALPHA1-2 FUC", "FUC", "ALPHA1-2" }, { "Add an ALPHA1-3 FUC", "FUC", "ALPHA1-3" },
      { "Add an ALPHA1-6 FUC", "FUC", "ALPHA1-6" }, { "Add a BETA1-6 FUL",   "FUL", "BETA1-6"  },
      { "Add an XYP-BMA XYP",  "XYP", "XYP-BMA"  }, { "Add an ALPHA2-3 SIA", "SIA", "ALPHA2-3" },
      { "Add an ALPHA2-6 SIA", "SIA", "ALPHA2-6" }
   };
}

std::map<std::string, bool> bandicoot_core_glycan_link_states(const std::string &tree_type) {
   std::map<std::string, bool> states;
   PyObject *r = safe_python_command_with_return("bandicoot_glyco_button_states(" + single_quote(tree_type) + ")");
   PyGILState_STATE gstate = PyGILState_Ensure();
   if (r && PyDict_Check(r)) {
      PyObject *key, *value;
      Py_ssize_t pos = 0;
      while (PyDict_Next(r, &pos, &key, &value))
         if (PyUnicode_Check(key)) states[PyUnicode_AsUTF8(key)] = PyObject_IsTrue(value) == 1;
   }
   PyGILState_Release(gstate);
   return states;
}

void bandicoot_core_add_glycan_link(const bandicoot_glycan_link &link) {
   safe_python_command("add_linked_residue_with_extra_restraints_to_active_residue(" +
                       single_quote(link.residue) + ", " + single_quote(link.link) + ")");
}

void bandicoot_core_refine_glycan_tree() {
   safe_python_command("glyco_refine_tree()");
}

static std::function<void()> glycan_view;

void bandicoot_core_set_glycan_view(std::function<void()> refresh) {
   glycan_view = std::move(refresh);
}

bool bandicoot_wx_glycan_refresh() {
   if (! glycan_view) return false;
   glycan_view();
   return true;
}

// ---- Measures menu

void bandicoot_core_geometry_pick(const std::string &kind, bool on) {
   if (kind == "dynamic_distance") {
      setup_dynamic_distances(on ? 1 : 0);
      if (! on) normal_cursor();
      return;
   }
   if (on) {
      if (kind == "distance") do_distance_define();
      else if (kind == "angle") do_angle_define();
      else if (kind == "torsion") do_torsion_define();
      return;
   }
   if (kind == "distance") graphics_info_t::in_distance_define = 0;
   else if (kind == "angle") graphics_info_t::in_angle_define = 0;
   else if (kind == "torsion") graphics_info_t::in_torsion_define = 0;
   else return;
   graphics_info_t::pick_pending_flag = 0;
   normal_cursor();
}

void bandicoot_core_clear_last_distance() { clear_last_simple_distance(); }
void bandicoot_core_clear_all_distances() { clear_simple_distances(); }
void bandicoot_core_clear_atom_labels() { remove_all_atom_labels(); }

void bandicoot_wx_release_geometry_toggle(const std::string &kind) {
   if (ui_view.release_geometry_toggle) ui_view.release_geometry_toggle(kind);
}

bandicoot_environment bandicoot_core_environment() {
   bandicoot_environment e;
   e.shown = graphics_info_t::environment_show_distances;
   e.h_bonds = graphics_info_t::environment_distances_show_h_bonds;
   e.bumps = graphics_info_t::environment_distances_show_bumps;
   e.label_atom = graphics_info_t::environment_distance_label_atom != 0;
   e.min_distance = graphics_info_t::environment_min_distance;
   e.max_distance = graphics_info_t::environment_max_distance;
   return e;
}

void bandicoot_core_set_environment_shown(bool shown) { set_show_environment_distances(shown ? 1 : 0); }
void bandicoot_core_set_environment_h_bonds(bool shown) { set_show_environment_distances_h_bonds(shown ? 1 : 0); }
void bandicoot_core_set_environment_bumps(bool shown) { set_show_environment_distances_bumps(shown ? 1 : 0); }
void bandicoot_core_set_environment_label_atom(bool label) { set_environment_distances_label_atom(label ? 1 : 0); }

// execute_environment_settings' range, without its fallback values
bool bandicoot_core_set_environment_limits(float min_distance, float max_distance) {
   if (min_distance < 0 || min_distance > 1000 || max_distance < 0 || max_distance > 1000) return false;
   if (max_distance < min_distance) std::swap(min_distance, max_distance);
   graphics_info_t::environment_min_distance = min_distance;
   graphics_info_t::environment_max_distance = max_distance;
   if (graphics_info_t::environment_show_distances) set_show_environment_distances(1);
   return true;
}

bandicoot_pointer_distances bandicoot_core_pointer_distances() {
   bandicoot_pointer_distances p;
   p.shown = graphics_info_t::show_pointer_distances_flag != 0;
   p.min_distance = graphics_info_t::pointer_min_dist;
   p.max_distance = graphics_info_t::pointer_max_dist;
   return p;
}

void bandicoot_core_set_pointer_distances_shown(bool shown) { set_show_pointer_distances(shown ? 1 : 0); }

// execute_pointer_distances_settings' range, without its fallback to 0
bool bandicoot_core_set_pointer_distance_limits(float min_distance, float max_distance) {
   if (min_distance < 0 || min_distance >= 999.9f || max_distance < 0 || max_distance >= 999.9f) return false;
   graphics_info_t::pointer_min_dist = min_distance;
   graphics_info_t::pointer_max_dist = max_distance;
   if (graphics_info_t::show_pointer_distances_flag) {
      graphics_info_t g;
      g.make_pointer_distance_objects();
      graphics_draw();
   }
   return true;
}

void bandicoot_core_plane_begin() {
   pick_cursor_maybe();
   bandicoot_core_plane_mode(false);
}

void bandicoot_core_plane_mode(bool deviation) {
   setup_lsq_deviation(deviation ? 1 : 0);
   setup_lsq_plane_define(deviation ? 0 : 1);
}

void bandicoot_core_plane_remove_last_atom() { remove_last_lsq_plane_atom(); }

void bandicoot_core_plane_end() {
   unset_lsq_plane_dialog();
   normal_cursor();
}

// ---- Validate menu

static std::vector<std::pair<int, std::string>> full_name_list(bool maps);

// An atom row, labelled as Coot's lists label them: " A 38 CA " and so on.
static bandicoot_result_row atom_row(int imol, const coot::atom_spec_t &spec, const std::string &extra = "") {
   bandicoot_result_row r;
   r.imol = imol;
   r.chain = spec.chain_id;
   r.resno = spec.res_no;
   r.atom = spec.atom_name;
   r.ins_code = spec.ins_code;
   r.alt_conf = spec.alt_conf;
   r.label = spec.chain_id + " " + std::to_string(spec.res_no) + " " + spec.atom_name;
   if (! spec.alt_conf.empty()) r.label += " " + spec.alt_conf;
   r.label += extra;
   return r;
}

bandicoot_bad_chirals bandicoot_core_bad_chirals(int imol) {
   bandicoot_bad_chirals b;
   if (! is_valid_model_molecule(imol)) return b;
   std::pair<std::vector<std::string>, std::vector<coot::atom_spec_t>> v =
      graphics_info_t::molecules[imol].bad_chiral_volumes();
   b.unknown_types = v.first;
   std::cout << "There were " << v.second.size() << " bad chiral volumes" << std::endl;
   for (const coot::atom_spec_t &spec : v.second) b.atoms.push_back(atom_row(imol, spec));
   return b;
}

// execute_find_blobs, handing back the blobs instead of showing them
std::vector<bandicoot_xyz> bandicoot_core_find_blobs(int imol_model, int imol_map, float rmsd) {
   std::vector<bandicoot_xyz> blobs;
   if (! is_valid_model_molecule(imol_model) || ! is_valid_map_molecule(imol_map)) return blobs;
   graphics_info_t g;
   coot::ligand lig;
   lig.import_map_from(g.molecules[imol_map].xmap, g.molecules[imol_map].map_sigma());
   lig.set_map_atom_mask_radius(1.9);
   lig.mask_map(g.molecules[imol_model].atom_sel.mol, g.find_ligand_mask_waters_flag);
   std::cout << "using sigma cut off " << rmsd << std::endl;
   lig.water_fit(rmsd, 1);
   for (const auto &blob : lig.big_blobs()) {
      bandicoot_xyz p;
      p.x = blob.first.x(); p.y = blob.first.y(); p.z = blob.first.z();
      blobs.push_back(p);
   }
   if (blobs.empty()) std::cout << "Coot found no blobs" << std::endl;
   return blobs;
}

std::vector<std::pair<int, std::string>> bandicoot_core_difference_maps_full_names() {
   std::vector<std::pair<int, std::string>> v;
   for (const auto &m : full_name_list(true))
      if (graphics_info_t::molecules[m.first].is_difference_map_p()) v.push_back(m);
   return v;
}

float bandicoot_core_difference_map_peaks_level() {
   return graphics_info_t::difference_map_peaks_sigma_level;
}

// difference_map_peaks, handing back the peaks instead of showing them
std::vector<bandicoot_peak> bandicoot_core_difference_map_peaks(int imol_map, int imol_model, float rmsd,
                                                                bool positive, bool negative,
                                                                bool around_model_only) {
   std::vector<bandicoot_peak> peaks;
   if (! is_valid_map_molecule(imol_map)) return peaks;
   molecule_class_info_t &m = graphics_info_t::molecules[imol_map];
   if (! m.is_difference_map_p()) return peaks;
   graphics_info_t::difference_map_peaks_sigma_level = rmsd;
   coot::peak_search ps(m.xmap);
   ps.set_max_closeness(graphics_info_t::difference_map_peaks_max_closeness);
   std::vector<std::pair<clipper::Coord_orth, float>> centres;
   if (is_valid_model_molecule(imol_model))
      centres = ps.get_peaks(m.xmap, graphics_info_t::molecules[imol_model].atom_sel.mol,
                             rmsd, positive, negative, around_model_only);
   else
      centres = ps.get_peaks(m.xmap, rmsd, positive, negative);
   float map_sigma = m.map_sigma();
   std::cout << "\n   Found these peak positions:\n";
   for (unsigned int i = 0; i < centres.size(); i++) {
      bandicoot_peak p;
      p.at.x = centres[i].first.x(); p.at.y = centres[i].first.y(); p.at.z = centres[i].first.z();
      p.value = centres[i].second;
      p.rmsd = centres[i].second / map_sigma;
      p.label = std::to_string(i + 1) + " " + graphics_info_t::float_to_string(p.value) + " (" +
                graphics_info_t::float_to_string(p.rmsd) + " rmsd) at " + centres[i].first.format();
      peaks.push_back(p);
      std::cout << "   " << i << " dv: " << p.value << " n-rmsd: " << p.rmsd << " "
                << centres[i].first.format() << std::endl;
   }
   std::cout << "\n   Found " << centres.size() << " peak positions:\n";
   return peaks;
}

// as a click on a row of Coot's peak list
void bandicoot_core_go_to_peak(const bandicoot_peak &peak, const std::string &label) {
   bandicoot_record_navigation("diff-map-peaks", label.c_str());
   graphics_info_t g;
   g.setRotationCentre(coot::Cartesian(peak.at.x, peak.at.y, peak.at.z));
   for (int i = 0; i < graphics_info_t::n_molecules(); i++) {
      graphics_info_t::molecules[i].update_map(graphics_info_t::auto_recontour_map_flag);
      graphics_info_t::molecules[i].update_symmetry();
   }
   g.make_pointer_distance_objects();
   graphics_draw();
}

bandicoot_check_waters bandicoot_core_check_waters_values() {
   bandicoot_check_waters c;
   c.imol = graphics_info_t::check_waters_molecule;
   c.b_factor = graphics_info_t::check_waters_b_factor_limit;
   c.map_rmsd = graphics_info_t::check_waters_map_sigma_limit;
   c.min_distance = graphics_info_t::check_waters_min_dist_limit;
   c.max_distance = graphics_info_t::check_waters_max_dist_limit;
   return c;
}

// do_check_waters_by_widget's reading of the criteria: kept for next time,
// and an unused one passed as -100
static std::vector<coot::atom_spec_t> water_baddies(const bandicoot_check_waters &c, bool &have_map) {
   graphics_info_t::check_waters_molecule = c.imol;
   graphics_info_t::check_waters_b_factor_limit = c.b_factor;
   graphics_info_t::check_waters_map_sigma_limit = c.map_rmsd;
   graphics_info_t::check_waters_min_dist_limit = c.min_distance;
   graphics_info_t::check_waters_max_dist_limit = c.max_distance;
   graphics_info_t g;
   int imol_map = g.Imol_Refinement_Map();
   have_map = is_valid_map_molecule(imol_map);
   if (! have_map || ! is_valid_model_molecule(c.imol)) return {};
   const molecule_class_info_t &map = graphics_info_t::molecules[imol_map];
   return graphics_info_t::molecules[c.imol].find_water_baddies(
      c.use_b_factor ? c.b_factor : -100.0f, map.xmap, map.map_sigma(),
      c.use_map_rmsd ? c.map_rmsd : -100.0f,
      c.use_min_distance ? c.min_distance : -100.0f,
      c.use_max_distance ? c.max_distance : -100.0f,
      c.ignore_partial_occupancy_contacts, c.ignore_zero_occupancy, c.any ? 1 : 0);
}

bool bandicoot_core_check_waters(const bandicoot_check_waters &c, std::vector<bandicoot_result_row> &rows) {
   bool have_map;
   std::vector<coot::atom_spec_t> baddies = water_baddies(c, have_map);
   if (! have_map) return false;
   for (const coot::atom_spec_t &b : baddies)
      rows.push_back(atom_row(c.imol, b, " [Occ: " + graphics_info_t::float_to_string(b.float_user_data) +
                                         "]   " + b.string_user_data));
   return true;
}

bool bandicoot_core_delete_checked_waters(const bandicoot_check_waters &c, int &n_deleted) {
   bool have_map;
   std::vector<coot::atom_spec_t> baddies = water_baddies(c, have_map);
   if (! have_map) return false;
   n_deleted = baddies.empty() ? 0 : graphics_info_t::molecules[c.imol].delete_atoms(baddies);
   graphics_draw();
   return true;
}

std::vector<bandicoot_result_row> bandicoot_core_waters_by_difference_map(int imol, int imol_diff_map) {
   std::vector<bandicoot_result_row> rows;
   if (! is_valid_model_molecule(imol) || ! is_valid_map_molecule(imol_diff_map)) return rows;
   const molecule_class_info_t &map = graphics_info_t::molecules[imol_diff_map];
   if (! map.is_difference_map_p()) return rows;
   std::vector<coot::atom_spec_t> v = graphics_info_t::molecules[imol].check_waters_by_difference_map(
      map.xmap, graphics_info_t::check_waters_by_difference_map_sigma_level);
   for (const coot::atom_spec_t &spec : v) rows.push_back(atom_row(imol, spec));
   return rows;
}

std::vector<bandicoot_result_row> bandicoot_core_gln_asn_outliers(int imol) {
   std::vector<bandicoot_result_row> rows;
   if (! is_valid_model_molecule(imol)) return rows;
   std::vector<std::pair<coot::atom_spec_t, std::string>> v =
      coot::util::gln_asn_b_factor_outliers(graphics_info_t::molecules[imol].atom_sel.mol);
   std::cout << "Found " << v.size() << " GLN/ASN B-factor outliers" << std::endl;
   for (const auto &o : v) {
      std::cout << o.second << std::endl;
      bandicoot_result_row r = atom_row(imol, o.first);
      r.label = o.second;
      rows.push_back(r);
   }
   return rows;
}

void bandicoot_core_flip_side_chain(const bandicoot_result_row &row) {
   do_180_degree_side_chain_flip(row.imol, row.chain.c_str(), row.resno, row.ins_code.c_str(),
                                 row.alt_conf.c_str());
}

std::string bandicoot_core_short_name(int imol) {
   if (imol < 0 || imol >= graphics_info_t::n_molecules()) return "";
   return graphics_info_t::molecules[imol].dotted_chopped_name();
}

// ---- Validate menu: the residue graphs

static bandicoot_graph_view graph_view;

void bandicoot_core_set_graph_view(const bandicoot_graph_view &view) {
   graph_view = view;
}

void bandicoot_wx_graph_open(const void *graph, int imol, int type, const std::string &title,
                             const std::string &label, int n_chains, int max_chain_length) {
   if (graph_view.open)
      graph_view.open(graph, imol, type, title, label, n_chains, max_chain_length,
                      type == coot::GEOMETRY_GRAPH_DENSITY_FIT);
}

void bandicoot_wx_graph_axis(const void *graph, int chain, int nres) {
   if (graph_view.axis) graph_view.axis(graph, chain, nres);
}

void bandicoot_wx_graph_ticks(const void *graph, int chain, int min_resno, int max_resno) {
   if (graph_view.ticks) graph_view.ticks(graph, chain, min_resno, max_resno);
}

void bandicoot_wx_graph_label(const void *graph, int chain, const std::string &text) {
   if (graph_view.label) graph_view.label(graph, chain, text);
}

void bandicoot_wx_graph_block(const void *graph, int chain, int slot, double distortion,
                              const std::string &colour, int imol, const coot::atom_spec_t &spec,
                              const std::string &info) {
   if (! graph_view.block) return;
   bandicoot_graph_block b;
   b.slot = slot;
   b.distortion = distortion;
   b.colour = colour;
   b.info = info;
   b.target = atom_row(imol, spec);
   b.target.label = info;
   graph_view.block(graph, chain, b);
}

void bandicoot_wx_graph_remove(const void *graph, int chain, int slot) {
   if (graph_view.remove) graph_view.remove(graph, chain, slot);
}

bool bandicoot_core_validation_graph(const std::string &list, int imol, std::string &why_not) {
   if (! is_valid_model_molecule(imol)) { why_not = "no such model"; return false; }
   graphics_info_t g;
   if (list == "geometry_analysis1") g.geometric_distortion(imol);
   else if (list == "peptide_omega_analysis1") g.omega_graphs(imol);
   else if (list == "temp_fact_variance_analysis1") g.b_factor_graphs(imol);
   else if (list == "temp_fact_analysis1") g.calc_b_factor_graphs(imol);
   else if (list == "rotamer_analysis1") g.rotamer_graphs(imol);
   else if (list == "density_fit_analysis1") {
      // asks for the refinement map itself when none is set
      g.density_fit_graphs(imol);
      if (! is_valid_map_molecule(g.Imol_Refinement_Map())) { why_not = "no refinement map is set"; return false; }
   } else if (list == "ncs_differences1") {
      if (! g.molecules[imol].first_ncs_master_chain_id().first) { why_not = "the model has no NCS"; return false; }
      g.ncs_diffs_from_mol(imol);
   } else {
      return false;
   }
   return true;
}

float bandicoot_core_density_fit_scale() {
   return graphics_info_t::residue_density_fit_scale_factor;
}

// Coot's Rescale button
void bandicoot_core_graph_rescale(const void *graph, float scale) {
   coot::geometry_graphs *gr = static_cast<coot::geometry_graphs *>(const_cast<void *>(graph));
   graphics_info_t::residue_density_fit_scale_factor = scale;
   graphics_info_t g;
   int imol = gr->get_imol();
   if (! is_valid_model_molecule(imol)) return;
   gr->update_residue_blocks(g.density_fit_from_mol(g.molecules[imol].atom_sel, imol, g.Imol_Refinement_Map()));
}

// as the GTK window's destroy did; the object itself is left alone, as
// there, since an update in progress may still hold it
void bandicoot_core_graph_closed(const void *graph) {
   const coot::geometry_graphs *gr = static_cast<const coot::geometry_graphs *>(graph);
   int imol = gr->Imol();
   if (is_valid_model_molecule(imol) &&
       coot::get_validation_graph(imol, gr->Graph_Type()) == gr->dialog())
      coot::set_validation_graph(imol, gr->Graph_Type(), NULL);
}

// ---- Validate menu: the Ramachandran plot

static bandicoot_rama_view rama_view;

void bandicoot_core_set_rama_view(const bandicoot_rama_view &view) {
   rama_view = view;
}

// called where Coot updated its Dynarama
void bandicoot_wx_rama_changed(int imol) {
   if (rama_view.changed) rama_view.changed(imol);
}

void bandicoot_wx_rama_highlight(int imol, const coot::residue_spec_t &spec) {
   if (rama_view.highlight) rama_view.highlight(imol, spec.chain_id, spec.res_no, spec.ins_code);
}

// Coot's tables and contour levels (rama_plot::setup_internal), made again
// when the levels change.
struct rama_tables_t {
   float preferred = -1, allowed = -1;
   clipper::Ramachandran all, gly, pro, general, pre_pro, ile_val;
};

static const rama_tables_t &rama_tables() {
   static rama_tables_t t;
   float preferred = graphics_info_t::rama_level_prefered;
   float allowed = graphics_info_t::rama_level_allowed;
   if (t.preferred != preferred || t.allowed != allowed) {
#ifdef CLIPPER_HAS_TOP8000
      t.all.init(clipper::Ramachandran::All2);
      t.gly.init(clipper::Ramachandran::Gly2);
      t.pro.init(clipper::Ramachandran::Pro2);
      t.general.init(clipper::Ramachandran::NoGPIVpreP2);
      t.pre_pro.init(clipper::Ramachandran::PrePro2);
      t.ile_val.init(clipper::Ramachandran::IleVal2);
#else
      t.all.init(clipper::Ramachandran::All5);
      t.gly.init(clipper::Ramachandran::Gly5);
      t.pro.init(clipper::Ramachandran::Pro5);
      t.general.init(clipper::Ramachandran::NonGlyPro5);
      t.pre_pro.init(clipper::Ramachandran::NonGlyPro5);
      t.ile_val.init(clipper::Ramachandran::NonGlyPro5);
#endif
      for (clipper::Ramachandran *r : { &t.all, &t.gly, &t.pro, &t.general, &t.pre_pro, &t.ile_val })
         r->set_thresholds(preferred, allowed);
      t.preferred = preferred;
      t.allowed = allowed;
   }
   return t;
}

static const clipper::Ramachandran &rama_table(int table) {
   const rama_tables_t &t = rama_tables();
   switch (table) {
   case BRAMA_GLY: return t.gly;
   case BRAMA_PRO: return t.pro;
   case BRAMA_PRE_PRO: return t.pre_pro;
   case BRAMA_ILE_VAL: return t.ile_val;
   case BRAMA_ALL: return t.all;
   default: return t.general;
   }
}

static int rama_table_for(const std::string &res_name, bool is_pre_pro) {
   if (res_name == "GLY") return BRAMA_GLY;
   if (res_name == "PRO") return BRAMA_PRO;
   if (is_pre_pro) return BRAMA_PRE_PRO;
   if (res_name == "ILE" || res_name == "VAL") return BRAMA_ILE_VAL;
   return BRAMA_GENERAL;
}

// as rama_plot::draw_phi_psi_point_internal judges it
static void add_rama_point(const coot::util::phi_psi_t &pp, mmdb::Residue *residue, bandicoot_rama_set &set) {
   bandicoot_rama_point p;
   p.phi = pp.phi();
   p.psi = pp.psi();
   std::string name = pp.residue_name();
   p.kind = name == "GLY" ? 1 : (name == "PRO" ? 2 : 0);
   p.table = rama_table_for(name, pp.is_pre_pro());
   const clipper::Ramachandran &r = rama_table(p.table);
   double phi = clipper::Util::d2rad(p.phi), psi = clipper::Util::d2rad(p.psi);
   p.region = r.allowed(phi, psi) ? (r.favored(phi, psi) ? 2 : 1) : 0;
   p.label = pp.label();
   p.chain = pp.chain_id;
   p.resno = pp.residue_number;
   p.ins_code = residue ? residue->GetInsCode() : "";
   if (p.region == 2) set.n_favoured++;
   if (p.region == 1) set.n_allowed++;
   set.points.push_back(p);
}

// rama_plot::generate_phi_psis and generate_phi_psis_by_selection
bool bandicoot_core_rama_points(int imol, const std::string &selection, bandicoot_rama_set &set) {
   set = bandicoot_rama_set();
   if (! is_valid_model_molecule(imol)) return false;
   mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
   if (selection.empty()) {
      for (int imod = 1; imod <= mol->GetNumberOfModels(); imod++) {
         mmdb::Model *model_p = mol->GetModel(imod);
         if (! model_p) continue;
         for (int ichain = 0; ichain < model_p->GetNumberOfChains(); ichain++) {
            mmdb::Chain *chain_p = model_p->GetChain(ichain);
            int nres = chain_p->GetNumberOfResidues();
            for (int ires = 1; ires < nres - 1; ires++) {
               mmdb::Residue *prev = chain_p->GetResidue(ires - 1);
               mmdb::Residue *here = chain_p->GetResidue(ires);
               mmdb::Residue *next = chain_p->GetResidue(ires + 1);
               if (! prev || ! here || ! next) continue;
               try {
                  add_rama_point(coot::util::phi_psi_with_residues_t(prev, here, next), here, set);
               }
               catch (const std::runtime_error &) {
                  // no phi/psi for this one (e.g. a chain break)
               }
            }
         }
      }
   } else {
      int handle = mol->NewSelection();
      mol->Select(handle, mmdb::STYPE_RESIDUE, selection.c_str(), mmdb::SKEY_NEW);
      mmdb::PResidue *residues = nullptr;
      int n_residues = 0;
      mol->GetSelIndex(handle, residues, n_residues);
      for (int ires = 1; ires < n_residues - 1; ires++) {
         mmdb::Residue *prev = residues[ires - 1], *here = residues[ires], *next = residues[ires + 1];
         if (std::string(prev->GetChainID()) != here->GetChainID() ||
             std::string(here->GetChainID()) != next->GetChainID())
            continue;
         try {
            add_rama_point(coot::util::phi_psi_t(prev, here, next), here, set);
         }
         catch (const std::runtime_error &) {
         }
      }
      mol->DeleteSelection(handle);
   }
   return true;
}

// rama_plot::draw_2_phi_psi_sets_on_canvas and find_phi_psi_differences
bool bandicoot_core_kleywegt(int imol1, int imol2, bool by_chain, const std::string &chain1,
                             const std::string &chain2, std::vector<bandicoot_kleywegt_pair> &pairs) {
   pairs.clear();
   if (! is_valid_model_molecule(imol1) || ! is_valid_model_molecule(imol2)) return false;
   bandicoot_rama_set set1, set2;
   bandicoot_core_rama_points(imol1, by_chain ? "//" + chain1 : std::string(), set1);
   bandicoot_core_rama_points(imol2, by_chain ? "//" + chain2 : std::string(), set2);
   std::map<std::tuple<std::string, int, std::string>, const bandicoot_rama_point *> second;
   for (const bandicoot_rama_point &p : set2.points) second[{ p.chain, p.resno, p.ins_code }] = &p;
   std::vector<std::pair<double, bandicoot_kleywegt_pair>> v;
   for (const bandicoot_rama_point &p : set1.points) {
      auto it = second.find({ by_chain ? chain2 : p.chain, p.resno, p.ins_code });
      if (it == second.end()) continue;
      double d1 = std::fabs(p.phi - it->second->phi), d2 = std::fabs(p.psi - it->second->psi);
      if (d1 > 180) d1 -= 360;
      if (d2 > 180) d2 -= 360;
      v.push_back({ std::sqrt(d1 * d1 + d2 * d2), { p, *it->second } });
   }
   std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
   for (size_t i = 0; i < v.size() && int(i) < graphics_info_t::rama_n_diffs; i++) pairs.push_back(v[i].second);
   return true;
}

// rama_plot::get_intermediate_bg_colour
static void rama_blend(const int start[3], const int end[3], float p_min, float p_max, float p,
                       int quad_channel, unsigned char *out) {
   float x = (p - p_min) / (p_max - p_min);
   for (int i = 0; i < 3; i++) {
      float f = (i == quad_channel) ? std::sqrt(std::max(x, 0.0f)) : x;
      out[i] = (unsigned char) std::clamp(int((1.0f - f) * start[i] + f * end[i]), 0, 255);
   }
}

std::string bandicoot_core_rama_background(int table, std::vector<unsigned char> &rgb) {
   rgb.clear();
   float preferred = graphics_info_t::rama_level_prefered;
   float allowed = graphics_info_t::rama_level_allowed;
   if (std::fabs(preferred - 0.02f) < 0.000001f && std::fabs(allowed - 0.002f) < 0.000001f) {
#ifdef CLIPPER_HAS_TOP8000
      static const char *files[] = { "rama2_all.png", "rama2_non_gly_pro_pre_pro_ileval.png", "rama2_gly.png",
                                     "rama2_pro.png", "rama2_pre_pro.png", "rama2_ileval.png" };
#else
      static const char *files[] = { "rama_all.png", "rama_non_gly_pro.png", "rama_gly.png",
                                     "rama_pro.png", "rama_non_gly_pro.png", "rama_non_gly_pro.png" };
#endif
      if (table >= 0 && table < BRAMA_N_TABLES) {
         std::string path = coot::package_data_dir() + "/pixmaps/" + files[table];
         if (coot::file_exists(path)) return path;
      }
   }
   // rama_plot::make_background: shaded blocks, favoured pink, allowed cream
   const clipper::Ramachandran &r = rama_table(table);
   int step = std::max(1, int(graphics_info_t::rama_plot_background_block_size));
   float max_prob = 0;
   for (int i = -180; i < 180; i += step)
      for (int j = -180; j < 180; j += step)
         max_prob = std::max(max_prob, float(r.probability(clipper::Util::d2rad(i + step / 2.0),
                                                           clipper::Util::d2rad(-(j + step / 2.0)))));
   const int cream[3] = { 255, 240, 220 }, pink[3] = { 210, 150, 190 }, white[3] = { 255, 255, 255 };
   rgb.assign(360 * 360 * 3, 255);
   for (int i = -180; i < 180; i += step) {
      for (int j = -180; j < 180; j += step) {
         double x = clipper::Util::d2rad(i + step / 2.0), y = clipper::Util::d2rad(-(j + step / 2.0));
         unsigned char c[3] = { 255, 255, 255 };
         float p = r.probability(x, y);
         if (r.favored(x, y))
            rama_blend(cream, pink, preferred, max_prob, p, 1, c);
         else if (r.allowed(x, y))
            rama_blend(white, cream, allowed, preferred, p, 2, c);
         else
            continue;
         for (int row = j + 180; row < std::min(j + 180 + step, 360); row++)
            for (int col = i + 180; col < std::min(i + 180 + step, 360); col++)
               std::copy(c, c + 3, &rgb[(row * 360 + col) * 3]);
      }
   }
   return "";
}

// as update_ramachandran_plot_background_from_res_spec chooses
int bandicoot_core_rama_table(int imol, const std::string &chain, int resno, const std::string &ins_code) {
   if (! is_valid_model_molecule(imol)) return BRAMA_GENERAL;
   std::string name = residue_name(imol, chain.c_str(), resno, ins_code.c_str());
   std::string next = residue_name(imol, chain.c_str(), resno + 1, ins_code.c_str());
   return rama_table_for(name, next == "PRO");
}

std::string bandicoot_core_rama_heading(int imol) {
   if (! is_valid_model_molecule(imol)) return "";
   return graphics_info_t::molecules[imol].dotted_chopped_name();   // starts with imol
}

bool bandicoot_wx_step_list(int step) {
   return ui_view.step_list && ui_view.step_list(step);
}

// ---- Calculate menu: PanDDA Inspect

void set_refinement_lennard_jones_epsilon(float epsilon);  // c-interface-refine.cc
int bandicoot_load_pandda_uimode();                        // c-interface-preferences.cc

static int pandda_saved_restraint_warnings = 1;
static bool pandda_open = false;

void bandicoot_core_pandda_opened() {
   safe_python_command("import bandicoot_pandda");
   // a softer contact term, so a ligand refined into PanDDA density is not
   // held off the protein
   set_refinement_lennard_jones_epsilon(0.1f);
   if (! pandda_open) pandda_saved_restraint_warnings = show_ligand_restraint_warnings_state();
   pandda_open = true;
   set_show_ligand_restraint_warnings(0);
}

void bandicoot_core_pandda_closed() {
   if (! pandda_open) return;
   pandda_open = false;
   set_show_ligand_restraint_warnings(pandda_saved_restraint_warnings);
}

std::string bandicoot_core_pandda_call(const std::string &call) {
   std::string s;
   PyGILState_STATE gstate = PyGILState_Ensure();
   // not released: the call can hand back Py_None without a reference of its own
   PyObject *r = safe_python_command_with_return("bandicoot_pandda." + call);
   if (r && PyUnicode_Check(r)) s = PyUnicode_AsUTF8(r);
   PyGILState_Release(gstate);
   return s;
}

bool bandicoot_core_pandda_basic_ui() {
   return bandicoot_load_pandda_uimode() == 1;
}

static std::function<void(const std::string &)> pandda_view;

void bandicoot_core_set_pandda_view(std::function<void(const std::string &folder)> open) {
   pandda_view = std::move(open);
}

bool bandicoot_wx_pandda(const std::string &folder) {
   if (! pandda_view) return false;
   pandda_view(folder);
   return true;
}

std::string bandicoot_core_plain_sequence(const std::string &text) {
   return coot::util::plain_text_to_sequence(text);
}

bool bandicoot_core_mutate_residue_range(const bandicoot_range_sequence &r, bool autofit, std::string &why_not) {
   if (! is_valid_model_molecule(r.imol)) {
      why_not = "not a model molecule";
      return false;
   }
   int first = std::min(r.first, r.last), last = std::max(r.first, r.last);
   std::string sequence = coot::util::plain_text_to_sequence(r.sequence);
   int n = last - first + 1;
   if (int(sequence.length()) != n) {
      why_not = "the sequence has " + std::to_string(sequence.length()) + " residues, the range " +
                std::to_string(n);
      return false;
   }
   graphics_info_t g;
   if (autofit && g.Imol_Refinement_Map() == -1) {
      why_not = "autofit needs a refinement map";
      return false;
   }
   // the scripting functions, as Coot's own dialog runs them
   std::string function = autofit ? "mutate_and_autofit_residue_range" : "mutate_residue_range";
   safe_python_command(function + "(" + std::to_string(r.imol) + ", " + single_quote(r.chain) + ", " +
                       std::to_string(first) + ", " + std::to_string(last) + ", " + single_quote(sequence) + ")");
   update_go_to_atom_window_on_changed_mol(r.imol);
   g.update_geometry_graphs(g.molecules[r.imol].atom_sel, r.imol);
   return true;
}

bool bandicoot_core_fit_loop(const bandicoot_range_sequence &r, bool ramachandran_restraints,
                             std::string &why_not, std::string &warning) {
   if (! is_valid_model_molecule(r.imol)) {
      why_not = "not a model molecule";
      return false;
   }
   graphics_info_t g;
   if (g.Imol_Refinement_Map() == -1) {
      why_not = "fitting a loop needs a refinement map";
      return false;
   }
   std::string sequence = coot::util::plain_text_to_sequence(r.sequence);
   int n = std::abs(r.last - r.first) + 1;
   if (! sequence.empty() && int(sequence.length()) != n) {
      sequence = std::string(n, 'A');
      warning = "Mis-matched sequence length. Using Poly-Ala.";
   }
   // the range's order is the direction the loop is built in
   safe_python_command("fit_gap(" + std::to_string(r.imol) + ", " + single_quote(r.chain) + ", " +
                       std::to_string(r.first) + ", " + std::to_string(r.last) + ", " + single_quote(sequence) +
                       ", " + std::to_string(ramachandran_restraints ? 1 : 0) + ")");
   return true;
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

void bandicoot_wx_release_tools_toggle(const std::string &widget) {
   if (ui_view.release_tools_toggle) ui_view.release_tools_toggle(widget);
}

// ---- Multi-Residue Torsion

// the bonds last offered, in the window's order
static std::vector<std::pair<coot::atom_spec_t, coot::atom_spec_t>> torsion_bonds;

bool bandicoot_wx_multi_torsion(const std::vector<std::pair<coot::atom_spec_t, coot::atom_spec_t>> &bonds) {
   if (! ui_view.multi_torsion) return false;
   torsion_bonds = bonds;
   std::vector<std::string> labels;
   auto atom = [](const coot::atom_spec_t &s) {
      return coot::util::remove_whitespace(s.atom_name) + " " + std::to_string(s.res_no);
   };
   for (const auto &b : bonds) labels.push_back(atom(b.first) + "  ->  " + atom(b.second));
   ui_view.multi_torsion(labels);
   return true;
}

void bandicoot_core_multi_torsion_start() {
   graphics_info_t g;
   g.in_multi_residue_torsion_define = true;
   g.multi_residue_torsion_picked_residue_specs.clear();
   g.multi_residue_torsion_reverse_fragment_mode = false;
   pick_cursor_maybe();
   add_status_bar_text("Multi-Residue Torsion: click on atoms in the residues to twist");
}

void bandicoot_core_multi_torsion_picked() {
   clear_pending_picks();
   show_multi_residue_torsion_dialog();
}

void bandicoot_core_multi_torsion_select(int bond, bool reverse) {
   graphics_info_t g;
   g.multi_residue_torsion_reverse_fragment_mode = reverse;
   if (bond < 0 || bond >= int(torsion_bonds.size())) return;
   graphics_info_t::select_multi_residue_torsion_bond(torsion_bonds[bond].first, torsion_bonds[bond].second);
}

void bandicoot_core_multi_torsion_reverse(bool reverse) {
   set_multi_residue_torsion_reverse_mode(reverse ? 1 : 0);
}

void bandicoot_core_multi_torsion_finish(bool accept) {
   if (accept) {
      accept_regularizement();
   } else {
      clear_up_moving_atoms();
      clear_pending_picks();
      normal_cursor();
   }
   clear_multi_residue_torsion_mode();
   torsion_bonds.clear();
   graphics_draw();
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

// judged against the general table, as Coot's backbone-edit plot did (its
// points carry no residue type)
void bandicoot_wx_backbone_rama(bool has_1, double phi_1, double psi_1, const std::string &label_1,
                                bool has_2, double phi_2, double psi_2, const std::string &label_2) {
   if (! ma_view.backbone_rama) return;
   std::vector<bandicoot_rama_point> points;
   auto add = [&points](double phi, double psi, const std::string &label) {
      bandicoot_rama_point p;
      p.phi = phi;
      p.psi = psi;
      p.label = label;
      const clipper::Ramachandran &r = rama_table(BRAMA_GENERAL);
      double x = clipper::Util::d2rad(phi), y = clipper::Util::d2rad(psi);
      p.region = r.allowed(x, y) ? (r.favored(x, y) ? 2 : 1) : 0;
      points.push_back(p);
   };
   if (has_1) add(phi_1, psi_1, label_1);
   if (has_2) add(phi_2, psi_2, label_2);
   ma_view.backbone_rama(points);
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
   if (rama_view.molecules_changed) rama_view.molecules_changed();
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
      bandicoot_dm_molecule d { imol, m.name_for_display_manager(), mol_is_displayed(imol) != 0,
                                mol_is_active(imol) != 0, render_as, {} };
      for (const coot::additional_representations_t &r : m.add_reps) {
         std::string label = r.info_string();
         if (r.atom_sel_info.type == coot::atom_selection_info_t::BY_STRING &&
             r.atom_sel_info.atom_selection_str == entire_molecule_selection)
            label = label.substr(0, label.find(": ") + 2) + "Entire Molecule";
         d.add_reps.push_back({ label, r.show_it });
      }
      v.push_back(d);
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

// ---- About menu: Header/Remarks Browser

namespace {

bandicoot_header_panel header_panel(const std::string &title, const std::string &text,
                                    unsigned char r, unsigned char g, unsigned char b,
                                    bool fixed_width = false) {
   bandicoot_header_panel p;
   p.title = title;
   p.text = text;
   p.red = r; p.green = g; p.blue = b;
   p.fixed_width = fixed_width;
   p.shown = true;
   return p;
}

// Pale washes by family, as the GTK browser has them, so the eye groups the
// window at a glance.
void header_family_rgb(coot::header_family_t f, unsigned char *rgb) {
   switch (f) {
   case coot::header_family_t::bibliography:  rgb[0] = 255; rgb[1] = 247; rgb[2] = 226; break;
   case coot::header_family_t::entity:        rgb[0] = 255; rgb[1] = 237; rgb[2] = 243; break;
   case coot::header_family_t::experiment:    rgb[0] = 228; rgb[1] = 253; rgb[2] = 228; break;
   case coot::header_family_t::refinement:    rgb[0] = 222; rgb[1] = 251; rgb[2] = 255; break;
   case coot::header_family_t::symmetry:      rgb[0] = 239; rgb[1] = 233; rgb[2] = 255; break;
   case coot::header_family_t::annotation:    rgb[0] = 233; rgb[1] = 245; rgb[2] = 251; break;
   case coot::header_family_t::heterogeneity: rgb[0] = 255; rgb[1] = 233; rgb[2] = 198; break;
   default:                                   rgb[0] = 251; rgb[1] = 251; rgb[2] = 251; break;
   }
}

void remark_rgb(int remark_number, unsigned char *rgb) {
   rgb[0] = rgb[1] = rgb[2] = 255;
   switch (remark_number) {
   case 2:   rgb[2] = 233; break;
   case 3:   rgb[0] = 233; break;
   case 4:   rgb[1] = 233; break;
   case 5:   rgb[1] = 241; rgb[2] = 241; break;
   case 280: rgb[0] = 243; rgb[1] = 237; break;
   case 350: rgb[1] = 237; rgb[2] = 239; break;
   case 465: rgb[1] = 233; rgb[2] = 233; break;
   default: break;
   }
}

std::string joined_lines(mmdb::TitleContainer *c, bool journal) {
   std::string s;
   if (! c) return s;
   for (int i = 0; i < c->Length(); i++) {
      const char *line = nullptr;
      if (journal) {
         mmdb::Journal *j = mmdb::PJournal(c->GetContainerClass(i));
         if (j) line = j->Line;
      } else {
         mmdb::Author *a = mmdb::PAuthor(c->GetContainerClass(i));
         if (a) line = a->Line;
      }
      if (line) s += std::string(line) + "\n";
   }
   return s;
}

std::string link_lines(mmdb::Manager *mol) {
   std::string s;
   mmdb::Model *model_p = mol->GetModel(1);
   if (! model_p) return s;
   int n_links = model_p->GetNumberOfLinks();
   for (int i = 0; i < n_links; i++) {
      mmdb::Link *l = model_p->GetLink(i);
      if (! l) continue;
      float dist = -1;
#ifdef MMDB_HAS_LINK_DISTANCE
      dist = l->dist;
#endif
      // residue names padded to three, for alignment
      std::string rn1 = l->resName1, rn2 = l->resName2;
      while (rn1.length() < 3) rn1 += " ";
      while (rn2.length() < 3) rn2 += " ";
      s += std::string("LINK ") + l->atName1 + " " + l->aloc1 + " " + rn1 + " " +
           coot::util::int_to_string(l->seqNum1) + " " + l->insCode1 + " " +
           l->atName2 + " " + l->aloc2 + " " + rn2 + " " +
           coot::util::int_to_string(l->seqNum2) + " " + l->insCode2 + " " +
           coot::util::float_to_string_using_dec_pl(dist, 3) + "\n";
   }
   return s;
}

void pdb_header_view(mmdb::Manager *mol, bandicoot_header_view *view) {

   view->title = coot::get_title(mol);

   std::string compound;
   for (const std::string &line : coot::get_compound_lines(mol)) compound += line + "\n";
   if (! compound.empty())
      view->panels.push_back(header_panel("Compound", compound, 255, 247, 247));

   access_title *at = static_cast<access_title *>(
      const_cast<mmdb::Title *>(static_cast<access_mol *>(mol)->GetTitle()));
   std::string authors = joined_lines(at->GetAuthor(), false);
   if (! authors.empty())
      view->panels.push_back(header_panel("Author", authors, 247, 232, 208));
   std::string journal = joined_lines(at->GetJournal(), true);
   if (! journal.empty())
      view->panels.push_back(header_panel("Journal", journal, 177, 193, 208));
   std::string links = link_lines(mol);
   if (! links.empty())
      view->panels.push_back(header_panel("Links", links, 177, 208, 247, true));

   std::map<int, std::string> remarks;
   mmdb::TitleContainer *tc = mol->GetRemarks();
   for (int i = 0; i < tc->Length(); i++) {
      mmdb::Remark *r = static_cast<mmdb::Remark *>(tc->GetContainerClass(i));
      if (r) remarks[r->remarkNum] += std::string(r->remark ? r->remark : "") + "\n";
   }
   for (const auto &r : remarks) {
      unsigned char rgb[3];
      remark_rgb(r.first, rgb);
      view->panels.push_back(header_panel("REMARK " + coot::util::int_to_string(r.first),
                                          r.second, rgb[0], rgb[1], rgb[2]));
   }
}

// The categories of the document last shown, so typing in the filter does
// not rebuild them on each key.
const coot::mmcif_document_t *header_doc = nullptr;
std::vector<coot::header_category_t> header_categories;

} // namespace

bool bandicoot_core_header_view(int imol, const std::string &filter, bool show_all,
                                bandicoot_header_view *view) {

   *view = bandicoot_header_view();
   if (! is_valid_model_molecule(imol)) return false;
   mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
   if (! mol) return false;

   const coot::mmcif_document_t *doc = graphics_info_t::molecules[imol].get_mmcif_document();
   if (! doc) {
      pdb_header_view(mol, view);
      return ! view->panels.empty();
   }

   if (doc != header_doc || filter.empty()) {
      header_categories = coot::mmcif_header_view(doc);
      header_doc = doc;
   }
   if (header_categories.empty()) return false;

   view->mmcif = true;
   std::string title = coot::get_title(mol);
   if (title != "Not available") {
      for (char &c : title) c = std::toupper(static_cast<unsigned char>(c));
      view->title = title;
   }
   for (const coot::header_category_t &c : header_categories) {
      unsigned char rgb[3];
      header_family_rgb(c.family, rgb);
      std::string text;
      bool any = coot::mmcif_header_category_text(c, filter, &text);
      bandicoot_header_panel p = header_panel(c.label.empty() ? c.name : c.label, text,
                                              rgb[0], rgb[1], rgb[2], true);
      p.shown = any && (show_all || ! c.body);
      view->panels.push_back(p);
   }
   return true;
}

std::string bandicoot_core_package_data_dir() {
   return coot::package_data_dir();
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

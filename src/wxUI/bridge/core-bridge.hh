// src/wxUI/bridge/core-bridge.hh
//
// Entry points the wx main window calls into the existing core: GL setup and
// drawing, and input. Only compiled into a --with-ui=wx build.
//
// Input is handed over in GDK's terms (modifier bits, X11 keysyms) because the
// core's mouse and key handlers are written against them; reusing those
// handlers keeps every binding identical to the GTK build. No wx or GTK types
// appear here -- see wxUI/main/surface.hh.

#ifndef BANDICOOT_WX_CORE_BRIDGE_HH
#define BANDICOOT_WX_CORE_BRIDGE_HH

#include <functional>
#include <map>
#include <string>
#include <vector>

// Modifier and button bits, numerically identical to GdkModifierType
// (the bridge checks this at compile time).
enum {
   BANDICOOT_MOD_SHIFT   = 1 << 0,
   BANDICOOT_MOD_CONTROL = 1 << 2,
   BANDICOOT_MOD_ALT     = 1 << 3,
   BANDICOOT_MOD_BUTTON1 = 1 << 8,
   BANDICOOT_MOD_BUTTON2 = 1 << 9,
   BANDICOOT_MOD_BUTTON3 = 1 << 10
};

// GL surface, sizes in points. The window size is what the state file records.
void bandicoot_core_gl_init();
void bandicoot_core_gl_resize(int surface_width, int surface_height,
                              int window_width, int window_height);
void bandicoot_core_gl_draw();

// Mouse. button is 1 (left), 2 (middle) or 3 (right); click_count is 2 for
// the second press of a double click. mods is the pointer state at the time of
// the event, including the button just pressed.
void bandicoot_core_button_press(int button, int click_count,
                                 double x, double y, unsigned int mods);
void bandicoot_core_button_release(int button, double x, double y, unsigned int mods);
void bandicoot_core_motion(double x, double y, unsigned int mods);
void bandicoot_core_scroll(bool up, unsigned int mods);

// Keyboard; keyval is an X11 keysym.
void bandicoot_core_key(bool press, unsigned int keyval, unsigned int mods);

// Drag and drop: a local file, or text (a URL or an accession code).
void bandicoot_core_drop_file(const std::string &file_name);
void bandicoot_core_drop_text(const std::string &text);

// The directory holding Bandicoot's icons.
std::string bandicoot_core_pixmaps_dir();

// The sidebar style the core holds: 1 icons, 2 icons and text, 3 text.
int bandicoot_core_sidebar_style();

// The sidebar tools the core can show or hide, by Coot's widget name.
struct bandicoot_sidebar_icon {
   std::string widget;
   bool shown;
   bool shown_by_default;
   std::string label, icon_file;  // as the Preferences list names them
};
std::vector<bandicoot_sidebar_icon> bandicoot_core_sidebar_icons();
// The Settings menu's Main Icons (true) or All Icons (false).
void bandicoot_core_sidebar_show_icon_set(bool main_icons_only);

// The sidebar's saved dock state (docked unless saved otherwise), and saving it.
bool bandicoot_core_sidebar_docked();
void bandicoot_core_save_sidebar_docked(bool docked);
// The side it docks on, in Coot's numbering: 0 right (default), 1 left, 3 bottom.
int bandicoot_core_sidebar_position();
void bandicoot_core_save_sidebar_position(int position);
// Its icon size in points: 24, 20, 16 or 12 (default).
int bandicoot_core_sidebar_icon_size();
void bandicoot_core_save_sidebar_icon_size(int points);

// The top toolbar as the user last arranged it: style 1 icons, 2 icons and
// text, 3 text, and the ids of the items after the fixed ones, in order. False
// if it has never been saved.
bool bandicoot_core_top_toolbar(int &style, std::vector<std::string> &ids);
void bandicoot_core_save_top_toolbar(int style, const std::vector<std::string> &ids);

// Whether this build has an optional capability, e.g. "enhanced_ligand_tools"
// (the ligand tools that need RDKit).
bool bandicoot_core_has_capability(const std::string &capability);

// File choosers: the extensions the core offers for each kind of file, and the
// directory the last chooser was left in (empty until one has been used).
std::vector<std::string> bandicoot_core_coordinates_extensions();
std::vector<std::string> bandicoot_core_data_extensions();
std::vector<std::string> bandicoot_core_map_extensions();
std::string bandicoot_core_chooser_directory();
void bandicoot_core_save_chooser_directory(const std::string &directory);

// Reading files. recentre is the coordinates chooser's choice: 0 recentre on
// the molecule, 1 don't recentre, 2 move the molecule to the view.
bool bandicoot_core_recentre_on_read();
void bandicoot_core_read_coordinates(const std::string &file_name, int recentre);
void bandicoot_core_read_map(const std::string &file_name, bool is_difference_map);
// Auto-open MTZ: maps from a file with map coefficients, or phases calculated
// for an MTZ without them. False, with the reason, for a file it cannot use.
bool bandicoot_core_auto_open_dataset(const std::string &file_name, std::string &why_not);

// Accept/Reject. After a refinement or fit the core asks the wx side to show
// the results and wait for Accept or Reject (themselves actions, run through
// bandicoot_core_run_action like the window's other buttons).
struct bandicoot_ar_light {
   std::string name;    // "Bonds", "Angles", ...
   std::string label;   // the text shown beside it
   float value;         // the distortion it is coloured by
   unsigned char red, green, blue;  // coloured by distortion
};
struct bandicoot_ar_results {
   std::string status;  // "Running...", "Success", ...; empty if none
   std::vector<bandicoot_ar_light> lights;  // empty: leave the shown ones
};
struct bandicoot_ar_view {
   // a fit is waiting: show (or refresh) the window
   std::function<void(const std::string &fit_type, const bandicoot_ar_results &results,
                      bool show_reverse, bool auto_clear_atom_pull)> present;
   // results while refinement runs
   std::function<void(const bandicoot_ar_results &results)> update;
   // chiral-volume and cis-peptide warnings; empty clears them
   std::function<void(const std::string &text)> warnings;
   // the fit ended other than by the window's buttons: stand down
   std::function<void()> dismiss;
   // docked or not, or always shown or not, changed (by a script or a
   // saved preference)
   std::function<void()> settings_changed;
};
void bandicoot_core_set_ar_view(const bandicoot_ar_view &view);
// Docked: the bar above the surface answers fits instead of the window.
// Always shown: the docked bar stays up between fits. Both are Coot's
// preferences and are saved with them.
bool bandicoot_core_ar_docked();
bool bandicoot_core_ar_always_shown();
void bandicoot_core_set_ar_docked(bool docked);
void bandicoot_core_set_ar_always_shown(bool always);
// Where the window was left, kept with the state as the GTK dialog's was.
// False if it has never been placed.
bool bandicoot_core_ar_position(int &x, int &y);
void bandicoot_core_save_ar_position(int x, int y);

// Go To Atom. The core keeps one target atom (molecule, chain, residue,
// insertion code, atom name, alt conf); the window shows and edits it.
struct bandicoot_goto_target {
   int imol = -1;
   std::string chain, ins_code, atom_name, alt_conf;
   int resno = 0;
};
bandicoot_goto_target bandicoot_core_goto_target();
// Model molecules, as the window's molecule menu lists them (imol, label).
std::vector<std::pair<int, std::string>> bandicoot_core_goto_molecules();
void bandicoot_core_set_goto_molecule(int imol);

struct bandicoot_goto_residue {
   std::string label;   // "A 42B LYS"
   std::string chain, ins_code;
   int resno;
};
struct bandicoot_goto_chain {
   std::string label;   // "Chain A"
   std::vector<bandicoot_goto_residue> residues;
};
std::vector<bandicoot_goto_chain> bandicoot_core_goto_residues(int imol);
struct bandicoot_goto_atom {
   std::string label;   // " CA ,A occ=1 bf=20.5"
   std::string name, alt_conf;
};
std::vector<bandicoot_goto_atom> bandicoot_core_goto_atoms(int imol, const std::string &chain,
                                                           int resno, const std::string &ins_code);

// Setting the target. centre: also move the view there.
// A residue's target is its central atom (CA or the like).
void bandicoot_core_goto_residue(int imol, const bandicoot_goto_residue &residue, bool centre);
void bandicoot_core_goto_atom(int imol, const bandicoot_goto_residue &residue,
                              const bandicoot_goto_atom &atom, bool centre);
// From the window's three fields: chain, "42B", "CA,A".
void bandicoot_core_goto_fields(const std::string &chain, const std::string &residue,
                                const std::string &atom);
void bandicoot_core_goto_next_residue(bool next);
void bandicoot_core_goto_from_current_position();

struct bandicoot_goto_view {
   std::function<bool()> open;                        // show the window
   std::function<void()> target_changed;              // refresh the fields
   std::function<void(int imol)> molecules_changed;   // refill the menu; imol to select, or -1
   std::function<void(int imol)> model_changed;       // imol's residues changed
};
void bandicoot_core_set_goto_view(const bandicoot_goto_view &view);
// Where the window was left, kept with the state. False if never placed.
bool bandicoot_core_goto_position(int &x, int &y);
void bandicoot_core_save_goto_position(int x, int y);

// ---- File menu

// Datasets (File > Open MTZ, mmCIF, fcf or phs...). What a chosen file
// needs next, as Coot decides it.
enum class bandicoot_dataset_step {
   columns,       // an MTZ with map coefficients: choose its columns
   phs,           // an XtalView .phs file: take a cell and symmetry from a molecule
   done,          // read (or handed on) as it is
   unreadable     // not a dataset Bandicoot can read
};
bandicoot_dataset_step bandicoot_core_open_dataset(const std::string &file_name);
// An MTZ file's columns by type, and Coot's preferred amplitudes and phases.
struct bandicoot_mtz_columns {
   std::vector<std::string> amplitudes;  // F, then anomalous differences (D)
   std::vector<std::string> phases, weights, fobs, sigfobs, r_free;
   int preferred_amplitude = 0, preferred_phase = 0;
};
bool bandicoot_core_mtz_columns(const std::string &file_name, bandicoot_mtz_columns &columns);
struct bandicoot_map_from_mtz {
   std::string file_name, amplitudes, phases, weights;
   bool use_weights = false, is_difference_map = false;
   bool refmac_labels = false;           // keep labels for structure factor calculation
   std::string fobs, sigfobs, r_free;
   bool use_resolution_limits = false;
   float low_resolution = 9999.9f, high_resolution = -1.0f;
};
int bandicoot_core_make_map_from_mtz(const bandicoot_map_from_mtz &request);
// A .phs file read with the cell and symmetry of molecule imol.
void bandicoot_core_read_phs(const std::string &file_name, int imol);

// Dictionaries (File > Import CIF dictionary...). molecule: -2 Auto, -1 All,
// a model's number, or -3 when no molecule is loaded.
std::vector<std::string> bandicoot_core_dictionary_extensions();
bool bandicoot_core_dictionary_generates_molecule();
void bandicoot_core_import_dictionary(const std::string &file_name, int molecule, bool generate_molecule);

// Monomers (Get Monomer..., Search Monomer Library...). Each returns the
// new molecule's number, or -1.
int bandicoot_core_get_monomer(const std::string &comp_id);
int bandicoot_core_get_monomer_from_network(const std::string &comp_id);
int bandicoot_core_get_sbase_monomer(const std::string &comp_id);
// (comp id, name) for each monomer whose name holds every word of text
std::vector<std::pair<std::string, std::string>>
bandicoot_core_search_monomers(const std::string &text, bool minimal_descriptions, bool sbase);
// A monomer's 2D depiction as PNG data; empty if it cannot be drawn.
std::string bandicoot_core_monomer_depiction(const std::string &comp_id, unsigned int size);

// Fetching by accession code (the Fetch items).
enum class bandicoot_accession_kind { pdb, pdb_and_map_eds, pdb_and_map_pdb_redo, alphafold_uniprot };
void bandicoot_core_fetch_by_accession(bandicoot_accession_kind kind, const std::string &code);
// The AlphaFold models matching the active molecule's UniProt references,
// superposed on it.
void bandicoot_core_fetch_and_superpose_alphafold();

// Saving coordinates (Save Coordinates..., Save Symmetry Coordinates...).
struct bandicoot_coords_format {
   std::string label;                    // "PDB"
   std::vector<std::string> extensions;  // ".pdb", ".ent"
};
struct bandicoot_save_coords {
   int imol = -1;
   bool symmetry = false;    // a picked symmetry mate; the core holds its operator
   std::vector<bandicoot_coords_format> formats;  // offered, in order
   int format = 0;           // the molecule's own format at first
   std::string file_name;    // suggested, with the format's extension
   std::string directory;    // where coordinates were last saved
   bool hydrogens = true, aniso = true;
};
// The model offered first: the first with unsaved changes, else the first.
int bandicoot_core_save_coords_default_molecule();
bandicoot_save_coords bandicoot_core_save_coords_request(int imol);
// The file's extension is made to match the chosen format. False if nothing
// was written.
bool bandicoot_core_save_coords(const bandicoot_save_coords &request, const std::string &path);
// Save Symmetry Coordinates: wait for a click on a symmetry atom, then call
// the view with the request for it.
void bandicoot_core_pick_symmetry_save();
struct bandicoot_save_view {
   std::function<void(const bandicoot_save_coords &request)> symmetry_picked;
};
void bandicoot_core_set_save_view(const bandicoot_save_view &view);

// Save State...: the state file's name (as last saved, else Coot's default).
std::string bandicoot_core_save_state_name();
void bandicoot_core_save_state(const std::string &path);

// Recover Session...: the models that have a backup newer than the file they
// were read from.
struct bandicoot_backup {
   int imol = -1;
   std::string name, backup_file_name;
};
std::vector<bandicoot_backup> bandicoot_core_recoverable_backups();
void bandicoot_core_recover_from_backup(const bandicoot_backup &backup);

// Export Map... / Export Map Fragment...: the map offered first; the whole
// map, or the part within radius of the screen centre. False if nothing was
// written.
int bandicoot_core_export_map_default();
bool bandicoot_core_export_map(int imol, const std::string &path);
bool bandicoot_core_export_map_fragment(int imol, float radius, const std::string &path);

// Delete Molecules and Maps...: every model and map (imol, label as the
// Display Manager names it); bandicoot_core_close_molecule deletes one.
std::vector<std::pair<int, std::string>> bandicoot_core_models_and_maps();

// ---- Edit menu

// A model's chain IDs, in order.
std::vector<std::string> bandicoot_core_chain_ids(int imol);

// Change Chain IDs: the whole chain, or residues from_resno to to_resno.
// False, with the reason, if the core refused.
bool bandicoot_core_change_chain_id(int imol, const std::string &from_chain,
                                    const std::string &to_chain, bool use_range,
                                    int from_resno, int to_resno, std::string &why_not);

// Copy Molecule, Copy Fragment: the new molecule's number, or -1.
int bandicoot_core_copy_molecule(int imol);
int bandicoot_core_copy_fragment(int imol, const std::string &atom_selection);

// Replace Fragment: the atoms an mmdb selection picks from imol_fragment
// replace their counterparts in imol_target. False, with the reason, if
// nothing was replaced.
bool bandicoot_core_replace_fragment(int imol_target, int imol_fragment,
                                     const std::string &atom_selection, std::string &why_not);
// Replace Residue: the active residue becomes one of type comp_id, fitted by
// overlap. False, with the reason, if it was not replaced.
bool bandicoot_core_replace_active_residue(const std::string &comp_id, std::string &why_not);

// Merge Molecules: the target offered first; molecules equal to the target
// are skipped.
int bandicoot_core_merge_target_default();
void bandicoot_core_merge_molecules(const std::vector<int> &add, int into);

// Renumber Residues: the active atom's residue number (false if there is no
// active atom); renumber chain's residues by offset, from start (or the
// N-terminus) to end (or the C-terminus). False if the core refused, e.g.
// when the new numbers overlap existing residues.
bool bandicoot_core_active_residue_number(int &resno);
struct bandicoot_renumber {
   int imol = -1;
   std::string chain;
   bool from_n_terminus = true, to_c_terminus = true;
   int start = 0, end = 0, offset = 0;
};
bool bandicoot_core_renumber_residues(const bandicoot_renumber &request);

// Residue Info: pick an atom; the core then calls the view with its residue.
struct bandicoot_residue_atom {
   std::string label;            // "B/209 HIS/ N  "
   std::string chain, ins_code, atom_name, alt_conf;
   int resno = 0;
   float occupancy = 1.0f, b_factor = 0.0f;
};
struct bandicoot_residue_info {
   int imol = -1;
   std::string molecule_label;   // "Molecule: 3 /path/model.pdb"
   std::string residue_name;     // "HISTIDINE", if the dictionary has it
   std::vector<bandicoot_residue_atom> atoms;
   std::vector<std::string> chi_angles;  // "Chi 1:  alt conf: A -60.61 degrees"
   float default_b_factor = 30.0f;
};
void bandicoot_core_pick_residue_info();
struct bandicoot_residue_info_view {
   std::function<void(const bandicoot_residue_info &info)> show;
};
void bandicoot_core_set_residue_info_view(const bandicoot_residue_info_view &view);
// The residue as it is now (false if it has gone).
bool bandicoot_core_residue_info(int imol, const std::string &chain, int resno,
                                 const std::string &ins_code, bandicoot_residue_info &info);
// after: before's atoms with their occupancy, B-factor and alt conf edited.
void bandicoot_core_apply_atom_edits(int imol, const std::vector<bandicoot_residue_atom> &before,
                                     const std::vector<bandicoot_residue_atom> &after);

// Bond Colours: each model's colour rotation, in degrees; whether it
// recolours carbons only.
struct bandicoot_bond_colour {
   int imol = -1;
   std::string label;       // "Molecule 0 model.pdb"
   float rotation = 0.0f;
};
std::vector<bandicoot_bond_colour> bandicoot_core_bond_colours();
void bandicoot_core_set_bond_colour_rotation(int imol, float degrees);
bool bandicoot_core_bond_colour_carbons_only();
void bandicoot_core_set_bond_colour_carbons_only(bool on);

// Bond Parameters: the model shown first (the one last shown, while it is
// loaded); its bond width in pixels, and whether it draws hydrogens. Changes
// apply at once.
int bandicoot_core_bond_parameters_molecule();
void bandicoot_core_set_bond_parameters_molecule(int imol);
int bandicoot_core_bond_width(int imol);
void bandicoot_core_set_bond_width(int imol, int width);
bool bandicoot_core_draws_hydrogens(int imol);
void bandicoot_core_set_draws_hydrogens(int imol, bool on);

// Colours as red, green, blue from 0 to 1. A map's colour can be put back to
// what it was when last remembered.
struct bandicoot_rgb { float r = 0, g = 0, b = 0; };
bandicoot_rgb bandicoot_core_map_colour(int imol);
void bandicoot_core_set_map_colour(int imol, const bandicoot_rgb &colour);
void bandicoot_core_remember_map_colour(int imol);
void bandicoot_core_restore_map_colour(int imol);
bandicoot_rgb bandicoot_core_skeleton_colour();
void bandicoot_core_set_skeleton_colour(const bandicoot_rgb &colour);

// Map Parameters, as Coot shows them (radii in A, increments in e/A^3).
struct bandicoot_map_parameters {
   std::string radius_xray, radius_em, increment, diff_increment, sampling_rate;
   bool dynamic_sampling = false, dynamic_size = false;
};
bandicoot_map_parameters bandicoot_core_map_parameters();
void bandicoot_core_set_map_radius(bool em, double radius);
// Everything but the dynamic options, which apply as they are ticked.
void bandicoot_core_set_map_parameters(double radius_xray, double radius_em, double increment,
                                       double diff_increment, double sampling_rate);
void bandicoot_core_set_dynamic_map_sampling(bool on);
void bandicoot_core_set_dynamic_map_size(bool on);

// Skeleton Box Radius (A) and Skeletonization Level (e/A^3), as Coot shows
// them; setting the level recalculates the skeletons.
std::string bandicoot_core_skeleton_box_radius();
void bandicoot_core_set_skeleton_box_radius(double radius);
std::string bandicoot_core_skeletonization_level();
void bandicoot_core_set_skeletonization_level(double level);

// Font Size of atom labels: 1 small, 2 medium, 3 large.
int bandicoot_core_font_size();
void bandicoot_core_set_font_size(int size);

// Pink Pointer Size (A), as Coot shows it.
std::string bandicoot_core_pointer_size();
void bandicoot_core_set_pointer_size(double size);

// Background Colour: black or white.
bool bandicoot_core_background_is_black();
void bandicoot_core_set_background_black(bool black);

// Restraints: the monomer types the dictionary holds, and one monomer's
// restraints as seven editable tables -- Info, Atoms, Bonds, Angles,
// Torsions, Chirals, Planes -- with values written as Coot shows them.
struct bandicoot_restraint_table {
   std::string title;
   std::vector<std::string> columns;
   std::vector<std::vector<std::string>> rows;
};
std::vector<std::string> bandicoot_core_restraint_monomer_types();
bool bandicoot_core_monomer_restraints(const std::string &comp_id,
                                       std::vector<bandicoot_restraint_table> &tables);
// From the edited tables (comp_id: the monomer they were opened for). A row
// that does not make a restraint is left out; what the tables do not show
// (coordinates, descriptors) is kept. Apply replaces the dictionary's
// restraints; save writes them to an mmCIF file. False, with the reason, if
// the Info row does not describe a monomer.
bool bandicoot_core_apply_restraints(const std::string &comp_id,
                                     const std::vector<bandicoot_restraint_table> &tables,
                                     std::string &why_not);
bool bandicoot_core_save_restraints(const std::string &comp_id,
                                    const std::vector<bandicoot_restraint_table> &tables,
                                    const std::string &path, std::string &why_not);

// Preferences. Each setter applies its setting at once and records it with
// Coot's preferences; save writes them to the preferences file. The
// Bandicoot-only settings (sidebar, sequence view) keep their own files and
// are saved as they change.
void bandicoot_core_save_preferences();
struct bandicoot_general_preferences {
   bool ar_docked = true, ar_always_shown = false;
   bool sequence_view_docked = true;
   int sequence_view_max_chains = 5;
   bool sidebar_docked = true;
   int sidebar_position = 0;   // Coot's numbering: 0 right, 1 left, 3 bottom
   int sidebar_style = 1;      // 1 icons, 2 icons and text, 3 text
   int sidebar_icon_size = 12;
   bool recentre_on_read = true;
   bool smooth_scroll = true;
   int smooth_scroll_steps = 40;
   float smooth_scroll_limit = 10.0f;
   bool flat_trackball = false;
};
// What Reset Defaults returns the General settings to.
bandicoot_general_preferences bandicoot_core_general_preference_defaults();
void bandicoot_core_save_sequence_view_max_chains(int n);
void bandicoot_core_set_sidebar_style(int style);
// The sidebar tool at pos (in bandicoot_core_sidebar_icons order) shown or not.
void bandicoot_core_set_sidebar_icon_shown(int pos, bool shown);
void bandicoot_core_set_recentre_on_read(bool on);
bool bandicoot_core_smooth_scroll();
int bandicoot_core_smooth_scroll_steps();
float bandicoot_core_smooth_scroll_limit();
void bandicoot_core_set_smooth_scroll(bool on);
void bandicoot_core_set_smooth_scroll_steps(int steps);
void bandicoot_core_set_smooth_scroll_limit(float angstroms);
// The virtual trackball: flat (turntable) or spherical (Coot's default).
bool bandicoot_core_flat_trackball();
void bandicoot_core_set_flat_trackball(bool flat);

// The Bonds, Maps, Geometry and Colours settings. Angles in degrees, the map
// radius in A, increments in e/A^3.
struct bandicoot_display_preferences {
   int bond_width = 0;                       // pixels, 1 to 20
   bool scale_bond_width_to_occupancy = false;
   int bond_scheme = -1;                     // in bandicoot_core_bond_scheme_labels(), -1 if none fits
   float bond_colour_rotation = 0;           // a new molecule's turn of the colour map
   bool carbons_only = false;
   float altloc_conf_a_offset = 0, altloc_colour_difference = 0;
   float map_radius = 0, map_increment = 0, diff_map_increment = 0, map_sampling_rate = 0;
   bool dynamic_map_sampling = false, dynamic_map_size = false;
   bool o_difference_map_colours = false;   // swapped: O's colours rather than Coot's
   float map_colour_rotation = 0;            // a new map's turn of the colour map
   bool active_map_drag = false;
   bool mark_cis_peptides_bad = false;
   bandicoot_rgb background;
};
bandicoot_display_preferences bandicoot_core_display_preferences();
// What Reset Defaults returns these settings to.
bandicoot_display_preferences bandicoot_core_display_preference_defaults();
// The schemes newly read coordinates can be drawn in.
std::vector<std::string> bandicoot_core_bond_scheme_labels();
void bandicoot_core_set_default_bond_width(int pixels);
void bandicoot_core_set_scale_bond_width_to_occupancy(bool on);
void bandicoot_core_set_default_bond_scheme(int index);
void bandicoot_core_set_bond_colour_rotation_on_read(float degrees);
void bandicoot_core_set_carbons_only_preference(bool on);
void bandicoot_core_set_altloc_conf_a_offset(float degrees);
void bandicoot_core_set_altloc_colour_difference(float degrees);
void bandicoot_core_set_map_radius_preference(float angstroms);
void bandicoot_core_set_map_increment_preference(float increment);
void bandicoot_core_set_diff_map_increment_preference(float increment);
void bandicoot_core_set_map_sampling_rate_preference(float rate);
void bandicoot_core_set_dynamic_map_sampling_preference(bool on);
void bandicoot_core_set_dynamic_map_size_preference(bool on);
void bandicoot_core_set_o_difference_map_colours(bool on);
void bandicoot_core_set_map_colour_rotation_on_read(float degrees);
void bandicoot_core_set_active_map_drag(bool on);
void bandicoot_core_set_mark_cis_peptides_bad(bool on);
void bandicoot_core_set_background_preference(const bandicoot_rgb &colour);

// The Others settings. The Coot ones are recorded with Coot's preferences;
// Pick Atom, PanDDA and Ligands keep Bandicoot's own files and are saved as
// they change.
struct bandicoot_other_preferences {
   bool antialias = false;
   int font_size = 0;                        // 1 small, 2 medium, 3 large, 4 on: the other fonts
   bandicoot_rgb font_colour;
   bool console_commands = false;
   float pointer_size = 0;                   // A
   int refinement_speed = 0;                 // steps per frame
   float spin_speed = 0;                     // degrees per frame
   float pick_radius = 0, symmetry_pick_radius = 0;              // A
   float intermediate_pick_far = 0, intermediate_pick_near = 0;  // A
   std::string pandda_cifs_dir;
   bool pandda_split = false, pandda_basic = false;
   bool auto_rename = false, ask_rename = false, auto_generate = false, ask_generate = false;
   bool apply_restraints_to_all = false;
   // view settings Coot kept in Draw menu dialogs, saved here with the rest
   float clipping = 0;                       // front and back together
   bool crosshairs = false;
   bool label_on_recentre = false;
};
bandicoot_other_preferences bandicoot_core_other_preferences();
// What Reset Defaults returns these settings to; the PanDDA directory is kept.
bandicoot_other_preferences bandicoot_core_other_preference_defaults();
// The label fonts beyond small, medium and large, from font size 4 on.
std::vector<std::string> bandicoot_core_other_font_labels();
bandicoot_rgb bandicoot_core_default_font_colour();
void bandicoot_core_set_antialias(bool on);
void bandicoot_core_set_font_size_preference(int size);
void bandicoot_core_set_font_colour_preference(const bandicoot_rgb &colour);
void bandicoot_core_set_console_commands(bool on);
void bandicoot_core_set_pointer_size_preference(float angstroms);
void bandicoot_core_set_refinement_speed(int steps_per_frame);
void bandicoot_core_set_spin_speed(float degrees);
void bandicoot_core_set_pick_radius(float angstroms);
void bandicoot_core_set_symmetry_pick_radius(float angstroms);
void bandicoot_core_set_intermediate_pick_radii(float far_angstroms, float near_angstroms);
void bandicoot_core_set_pandda_config(const std::string &cifs_dir, bool split);
void bandicoot_core_set_pandda_basic(bool basic);
void bandicoot_core_set_ligand_behaviour(bool auto_rename, bool ask_rename, bool auto_generate,
                                         bool ask_generate, bool apply_restraints_to_all);
void bandicoot_core_set_clipping(float clipping);
void bandicoot_core_set_crosshairs(bool on);
void bandicoot_core_set_label_on_recentre(bool on);

// Sequence View: a model's chains as one-letter sequences, residues
// coloured by secondary structure, as Coot's view shows them.
struct bandicoot_seq_residue {
   int resno;
   std::string ins_code, res_name;
   char letter;                        // '~' for water
   unsigned char red, green, blue;     // by secondary structure
};
struct bandicoot_seq_chain {
   std::string chain_id;
   std::vector<bandicoot_seq_residue> residues;
};
struct bandicoot_sequence {
   std::string title;                  // "Molecule Number 0: 1abc.pdb"
   std::vector<bandicoot_seq_chain> chains;
};
bandicoot_sequence bandicoot_core_sequence(int imol);
// Centre on a residue (its central atom) and make it the Go To target.
void bandicoot_core_sequence_view_centre(int imol, const std::string &chain, int resno,
                                         const std::string &ins_code);
// Docked or in a window of its own, and how many chains a docked view shows
// when it opens; both saved with Bandicoot's preferences.
bool bandicoot_core_sequence_view_docked();
void bandicoot_core_save_sequence_view_docked(bool docked);
int  bandicoot_core_sequence_view_max_chains();
struct bandicoot_sequence_view_hooks {
   std::function<void(int imol)> model_changed;   // imol's residues changed
   std::function<void()> molecules_changed;      // a molecule was added or closed
   // the current residue moved to this one
   std::function<void(int imol, const std::string &chain, int resno,
                      const std::string &ins_code)> highlight;
};
void bandicoot_core_set_sequence_view_hooks(const bandicoot_sequence_view_hooks &hooks);

// Display Manager: the loaded maps and models and their display state.
struct bandicoot_dm_molecule {
   int imol;
   std::string name;    // as the Display Manager shows it
   bool displayed;
   bool active;         // models
   int render_as;       // models: index in bandicoot_core_render_as_labels(), -1 if none fits
   // models: their additional representations, by label and whether shown
   std::vector<std::pair<std::string, bool>> add_reps;
};
std::vector<bandicoot_dm_molecule> bandicoot_core_dm_maps();
std::vector<bandicoot_dm_molecule> bandicoot_core_dm_models();
int bandicoot_core_scroll_map();
// The "Render As" choices, in the menu's order.
std::vector<std::string> bandicoot_core_render_as_labels();
void bandicoot_core_set_render_as(int imol, int index);
void bandicoot_core_set_map_displayed(int imol, bool state);
// as the GTK window did: a model shown or hidden is also made active or not
void bandicoot_core_set_model_displayed(int imol, bool state);
void bandicoot_core_set_model_active(int imol, bool state);
void bandicoot_core_set_scroll_map(int imol);
void bandicoot_core_close_molecule(int imol);
void bandicoot_core_set_all_maps_displayed(bool state);
void bandicoot_core_set_all_models_displayed(bool state);
void bandicoot_core_display_last_model_only();

struct bandicoot_dm_view {
   std::function<bool()> open;               // show the window
   std::function<void()> molecules_changed;  // a molecule was added or closed
   std::function<void()> state_changed;      // displayed / active / scroll map / names
};
void bandicoot_core_set_dm_view(const bandicoot_dm_view &view);
// Where and how big the window was left, and its split; kept with the state
// as the GTK window's were. False if it has never been placed.
struct bandicoot_dm_geometry { int x, y, width, height, split; };
bool bandicoot_core_dm_geometry(bandicoot_dm_geometry &g);
void bandicoot_core_save_dm_geometry(const bandicoot_dm_geometry &g);

// Things the core asks of the window as a whole.
// A results list's row (validation checks, PDB-REDO's changes, ...): a
// point, or an atom of a molecule.
struct bandicoot_result_row {
   std::string label;
   bool at_point = false;
   float x = 0, y = 0, z = 0;
   int imol = -1;
   std::string chain, atom;
   int resno = 0;
   std::string ins_code, alt_conf;   // for a row's action (e.g. a flip)
};
// Go where a row points.
void bandicoot_core_go_to_result(const bandicoot_result_row &row);

// A script's radio-button choice: choosing an option runs its Python command
// (often a preview), and Accept or Reject runs theirs and ends the choice.
struct bandicoot_radio_choice {
   std::string title;
   std::vector<std::pair<std::string, std::string>> choices;  // label, command
   int selected = 0;
   std::string accept_label, accept_command, reject_label, reject_command;
};
// A script's column of buttons: each runs its Python command, and the close
// button runs its own and ends the window.
struct bandicoot_button_choice {
   std::string title;
   std::vector<std::pair<std::string, std::string>> buttons;  // label, command
   std::string close_label, close_command;
   // optional: a check button above the list that, while ticked, shows
   // checked_buttons in its place
   std::string check_label;
   std::vector<std::pair<std::string, std::string>> checked_buttons;
};
// Run a script's Python command.
void bandicoot_core_run_python(const std::string &command);

struct bandicoot_ui_view {
   std::function<void(const std::string &text)> info;  // Coot's info dialogs
   // a results list (empty: nothing was found)
   std::function<void(const std::string &title, const std::vector<bandicoot_result_row> &rows)> results;
   // choose the refinement map, waiting for the answer as Coot's chooser
   // did (the tools that ask go on with whatever was chosen)
   std::function<void()> select_map;
   // a fixed-atom pick is done: un-press Fix Atom / Unfix Atom
   std::function<void()> release_fixed_atom_toggles;
   // the core let go of a sidebar toggle (by Coot's widget name), as it
   // un-presses the GTK one when its picks are done or cancelled
   std::function<void(const std::string &widget)> release_sidebar_toggle;
   // and likewise for an Other Modelling Tools toggle
   std::function<void(const std::string &widget)> release_tools_toggle;
   // and for a Distances & Angles pick (see bandicoot_core_geometry_pick)
   std::function<void(const std::string &kind)> release_geometry_toggle;
   // the . and , keys: step the open peak or water list by one row, as
   // Coot's keys did; false if no such list is open
   std::function<bool(int step)> step_list;
   // Multi-Residue Torsion's picked residues are ready to twist: the
   // rotatable bonds to offer, by label (see bandicoot_core_multi_torsion_*)
   std::function<void(const std::vector<std::string> &bonds)> multi_torsion;
   // a yes/no the core waits on (true for yes)
   std::function<bool(const std::string &question)> question;
   // molecules to choose among (imol, label), all ticked at first, waiting
   // for the answer: the ticked ones, or none if cancelled
   std::function<std::vector<int>(const std::string &title, const std::string &header,
                                  const std::vector<std::pair<int, std::string>> &molecules)> choose_molecules;
   // Quick Ligand Validate's report: each test 0 passed, 1 failed, 2 not done
   std::function<void(const std::string &residue, int density, int geometry, int bumps)> ligand_check;
   // a window of read-only text
   std::function<void(const std::string &title, const std::string &text)> text_window;
   // run callback every interval_ms until it returns false (scripts'
   // timeouts, which in the GTK build ran on GLib's main loop)
   std::function<void(int interval_ms, std::function<bool()> callback)> timeout_add;
   // a script's choice among radio buttons (its own pygtk one cannot appear)
   std::function<void(const bandicoot_radio_choice &choice)> radio_choice;
   // a script's column of buttons (its own pygtk one cannot appear)
   std::function<void(const bandicoot_button_choice &choice)> button_choice;
   // Draw > Generic Display Objects (scripts open it after drawing objects)
   std::function<void()> generic_objects;
};
void bandicoot_core_set_ui_view(const bandicoot_ui_view &view);

// Run Refmac: the models offered (imol, label), the MTZ file used last
// (empty if none), and the run, which goes on in the background and reports
// when it is done.
std::vector<std::pair<int, std::string>> bandicoot_core_refmac_models();
std::string bandicoot_core_refmac_mtz();
void bandicoot_core_run_refmac(int imol, const std::string &mtz_file_name);

// Refinement and regularization parameters (the R/RC window).
struct bandicoot_refine_params {
   bool torsions, planar_peptides, trans_peptides, rama;
   int secondary_structure;  // 0 none, 1 alpha helix, 2 beta strand
   float weight;             // the map weight
   double lj_epsilon, gm_alpha, torsion_weight;
   float rama_weight;
   bool more_control_shown;
};
bandicoot_refine_params bandicoot_core_refine_params();
void bandicoot_core_set_torsion_restraints(bool on);
void bandicoot_core_set_planar_peptide_restraints(bool on);
void bandicoot_core_set_trans_peptide_restraints(bool on);
void bandicoot_core_set_rama_restraints(bool on);
void bandicoot_core_set_secondary_structure_restraints(int type);
void bandicoot_core_set_refinement_weight(float w);
// The map weight as estimated from the refinement map, which also becomes
// the weight. False, leaving w alone, when there is no refinement map.
bool bandicoot_core_estimate_refinement_weight(float &w);
// The More Control values, by the label they are shown under, from the
// text chosen; each takes effect at once on a running refinement.
void bandicoot_core_set_refine_control(const std::string &which, const std::string &value);
void bandicoot_core_set_more_control_shown(bool shown);

// The refinement map, and the maps it can be (imol, label).
std::vector<std::pair<int, std::string>> bandicoot_core_maps_for_fitting();
int bandicoot_core_refinement_map();  // -1 if none
void bandicoot_core_set_refinement_map(int imol);

// Map Sharpening/Blurring: a map's B-factor in A^2 (negative sharpens,
// positive blurs), within plus or minus the limit.
float bandicoot_core_map_sharpening_limit();
float bandicoot_core_map_sharpening(int imol);
void bandicoot_core_sharpen_map(int imol, float b_factor);
// The B-factor that makes the map sharpest; converged false when the search
// found none and the value may be bogus.
float bandicoot_core_optimal_sharpening(int imol, bool &converged);

// Map Skeleton: the map to start from (-1 if none), whether a map has its
// skeleton, and turning it on (optionally pruned of side chains) or off.
int bandicoot_core_skeleton_map();
bool bandicoot_core_map_skeletonized(int imol);
void bandicoot_core_set_map_skeleton(int imol, bool on, bool prune);

// NCS Maps: the models with NCS, and maps of each NCS copy's density.
std::vector<std::pair<int, std::string>> bandicoot_core_ncs_models();
void bandicoot_core_make_ncs_maps(int imol_model, int imol_map);

// Updating Maps: recalculate a difference map from a model and a map with
// data attached, once or each time the model changes.
std::vector<std::pair<int, std::string>> bandicoot_core_difference_maps();
void bandicoot_core_update_maps(int imol_model, int imol_map, int imol_difference_map, bool automatic);

// Move Molecule Here: centre a model on the screen centre.
void bandicoot_core_move_molecule_here(int imol);

// LSQ Superpose: a residue range of one chain fitted onto another's. The
// values are remembered from one opening of the window to the next.
struct bandicoot_lsq {
   int imol_reference = -1, imol_moving = -1;
   int reference_start = 1, reference_end = 999, moving_start = 1, moving_end = 999;
   std::string reference_chain, moving_chain;
   int match = 0;         // 0 all atoms, 1 main chain, 2 C-alphas
   bool move_copy = false;
};
bandicoot_lsq bandicoot_core_lsq_remembered();
void bandicoot_core_remember_lsq(const bandicoot_lsq &lsq);
// False, with the reason, if no fit was made.
bool bandicoot_core_lsq_fit(const bandicoot_lsq &lsq, std::string &why_not);

// SSM Superpose: the moving model (or its copy) laid onto the reference by
// secondary structure, each optionally by one chain (empty: all chains).
void bandicoot_core_ssm_superpose(int imol_reference, int imol_moving, const std::string &reference_chain,
                                  const std::string &moving_chain, bool move_copy);

// Align & Mutate: a chain aligned to a one-letter sequence and mutated to
// match it. False, with the reason, if nothing was done.
bool bandicoot_core_align_and_mutate(int imol, const std::string &chain, const std::string &sequence,
                                     bool autofit, std::string &why_not);

// Mutate Residue Range and Fit Loop by Rama Search: a residue range of one
// chain (either way round) and a one-letter sequence for it.
struct bandicoot_range_sequence {
   int imol = -1;
   std::string chain;
   int first = 0, last = 0;
   std::string sequence;   // as typed
};
// The residues a typed sequence holds, as Coot reads it.
std::string bandicoot_core_plain_sequence(const std::string &text);
// False, with the reason, if nothing was mutated.
bool bandicoot_core_mutate_residue_range(const bandicoot_range_sequence &r, bool autofit, std::string &why_not);
// False, with the reason, if no loop was fitted. warning is set when the
// sequence did not fit the range and poly-Ala was built instead.
bool bandicoot_core_fit_loop(const bandicoot_range_sequence &r, bool ramachandran_restraints,
                             std::string &why_not, std::string &warning);

// Frames/Sec: the rate reported in the status bar while the view redraws.
bool bandicoot_core_fps_shown();
void bandicoot_core_show_fps(bool on);
// Load Tutorial Model and Data: the tutorial model and its two maps. False,
// with the file missing, if the data are not installed.
bool bandicoot_core_load_tutorial(std::string &why_not);
// Run Script: a Python (or Scheme) file, as Coot runs one.
void bandicoot_core_run_script(const std::string &path);
// The Python console: run code in the console's lasting namespace; output
// holds what it printed (or the error), had_error says which.
std::string bandicoot_core_console_run(const std::string &code, bool &had_error);

// Modelling menu: the items that act at once on the active residue or
// molecule, by their menu ids (e.g. "BMOD_INVERT_CHIRAL"). False if the id is
// not one of them.
bool bandicoot_core_is_modelling_direct(const std::string &id);
bool bandicoot_core_modelling_direct(const std::string &id);

// Ligand menu: the items that act at once, by their menu ids (e.g.
// "BLIG_JIGGLE_FIT"). False if the id is not one of them.
bool bandicoot_core_is_ligand_direct(const std::string &id);
bool bandicoot_core_ligand_direct(const std::string &id);

// Draw menu: the items that act at once, by their menu ids. False if the id
// is not one of them.
bool bandicoot_core_is_draw_direct(const std::string &id);
bool bandicoot_core_draw_direct(const std::string &id);
// Draw > Anisotropic Atoms, for every molecule at once.
struct bandicoot_aniso {
   bool show = false, limit = false;
   float radius = 0;                         // A, used when limit is on
   float probability = 0;                    // %
};
bandicoot_aniso bandicoot_core_aniso();
// Everything but the probability, which applies on its own as it moves.
void bandicoot_core_set_aniso(const bandicoot_aniso &a);
void bandicoot_core_set_aniso_probability(float percent);

// Draw > Additional Representation: a further, fatter or ball-and-stick
// drawing of part of a model, listed under it in the Display Manager.
struct bandicoot_add_rep_request {
   int imol = -1;                  // for by residues and by selection
   int by = 0;                     // 0 the residue at the centre, 1 residues, 2 selection string,
                                   // 3 the entire molecule
   std::string chain_id, ins_code, selection;
   int resno_start = 0, resno_end = 0;
   bool ball_and_stick = false;
   float bond_width = 8;           // fat bonds
};
// False, with the reason, if nothing was added.
bool bandicoot_core_add_representation(const bandicoot_add_rep_request &r, std::string &why_not);
// The molecule the window chose last.
int bandicoot_core_add_rep_molecule();
// The first and last residue numbers of a chain; false if it has none.
bool bandicoot_core_chain_residue_range(int imol, const std::string &chain_id, int &first, int &last);
void bandicoot_core_set_additional_representation_shown(int imol, int index, bool shown);
void bandicoot_core_set_all_additional_representations_shown(int imol, bool shown);

// Draw > Cell & Symmetry. The unit cell state is read from the first molecule.
struct bandicoot_symmetry {
   bool shown = false;
   float radius = 0;               // A
   bandicoot_rgb colour;
   float colour_merge = 0;
   bool expanded_labels = false;
   bool unit_cells = false;
};
bandicoot_symmetry bandicoot_core_symmetry();
void bandicoot_core_set_symmetry_shown(bool shown);
// Turns symmetry on and saves that with the preferences.
void bandicoot_core_set_symmetry_always_on();
// False if the radius is out of range.
bool bandicoot_core_set_symmetry_radius(float radius);
void bandicoot_core_set_symmetry_colour(const bandicoot_rgb &colour);
void bandicoot_core_set_symmetry_colour_merge(float merge);
void bandicoot_core_set_symmetry_expanded_labels(bool expanded);
void bandicoot_core_set_unit_cells_shown(bool shown);
// Symmetry by Molecule, for each model.
struct bandicoot_molecule_symmetry {
   int imol;
   std::string label;              // "Molecule 2 2abc.pdb"
   bool shown;
   int display;                    // 0 sphere, 1 near chains, 2 C-alphas
   int colouring;                  // 0 standard, 1 by symop, 2 by molecule
};
std::vector<bandicoot_molecule_symmetry> bandicoot_core_molecule_symmetries();
void bandicoot_core_set_molecule_symmetry_shown(int imol, bool shown);
void bandicoot_core_set_molecule_symmetry_display(int imol, int display);
void bandicoot_core_set_molecule_symmetry_colouring(int imol, int colouring);

// Draw > Generic Display Objects: those not closed, by their object numbers.
struct bandicoot_generic_object {
   int index;
   std::string name;
   bool displayed;
};
std::vector<bandicoot_generic_object> bandicoot_core_generic_objects();
void bandicoot_core_set_generic_object_displayed(int index, bool displayed);
void bandicoot_core_set_all_generic_objects_displayed(bool displayed);
void bandicoot_core_close_all_generic_objects();

// Draw > NCS Ghost Control: the models that have NCS ghosts.
struct bandicoot_ncs_molecule {
   int imol;
   std::string label;              // "Molecule 2 2abc.pdb"
   bool ghosts_shown;
   std::vector<std::string> chains;
   std::vector<bool> chain_shown;  // as a ghost; never the master
   int master = -1;                // index in chains
};
std::vector<bandicoot_ncs_molecule> bandicoot_core_ncs_molecules();
void bandicoot_core_set_ncs_ghosts_shown(int imol, bool shown);
void bandicoot_core_set_ncs_ghost_chain_shown(int imol, int ichain, bool shown);
// The other chains are all shown as ghosts of the new master, as in Coot.
void bandicoot_core_set_ncs_master(int imol, int ichain);

// Draw > Screenshot. Simple is the graphics window as it is; Povray and
// Raster3D write a scene file beside the image and render it with that
// program. False if no image was written; renderer_found says whether the
// program was there to run.
bool bandicoot_core_screenshot(const std::string &path);
bool bandicoot_core_povray_image(const std::string &path, std::string &image_path, bool &renderer_found);
bool bandicoot_core_raster3d_image(const std::string &path, std::string &image_path, bool &renderer_found);
// Coot's suggestion when a dark blue map would project badly; empty if none.
std::string bandicoot_core_dark_density_warning();

// Draw > Representation Tools. The items that act at once, by their menu ids
// (e.g. "BREP_GREY_CARBONS"); false if the id is not one of them. why_not is
// set when the item had nothing to act on.
bool bandicoot_core_is_representation_direct(const std::string &id);
bool bandicoot_core_representation_direct(const std::string &id, std::string &why_not);
// The handle of the new object, -1 on failure.
int  bandicoot_core_ball_and_stick(int imol, const std::string &selection);
void bandicoot_core_clear_ball_and_stick(int imol);
// -1 if the selection matches no atoms
int  bandicoot_core_dotted_surface(int imol, const std::string &selection);
void bandicoot_core_clear_dots(int imol, int handle);
float bandicoot_core_map_radius();
// 0 for no limit
void bandicoot_core_limit_model_display_radius(float radius);
void bandicoot_core_label_all_cas(int imol);
// HOLE: a pore's radius along a path from start to end, drawn as dots and
// written to dots_file.
struct bandicoot_xyz { float x = 0, y = 0, z = 0; };
bandicoot_xyz bandicoot_core_rotation_centre();
void bandicoot_core_hole(int imol, const bandicoot_xyz &start, const bandicoot_xyz &end,
                         const std::string &dots_file);
// Views
std::string bandicoot_core_next_view_name();
void bandicoot_core_add_view(const std::string &name);
void bandicoot_core_add_spin_view(int steps, float degrees);
std::vector<std::string> bandicoot_core_views();
void bandicoot_core_go_to_view(int index);
float bandicoot_core_views_play_speed();
void bandicoot_core_set_views_play_speed(float speed);
void bandicoot_core_save_views(const std::string &path);
// 3D annotations: text placed in the scene
void bandicoot_core_add_annotation(const std::string &text);
void bandicoot_core_save_annotations(const std::string &path);
void bandicoot_core_load_annotations(const std::string &path);

// Measures > Distances & Angles: the atom picks, "distance", "angle",
// "torsion" and "dynamic_distance"; on waits for the pick, off stops waiting.
// The core lets go of a pick through bandicoot_ui_view::release_geometry_toggle
// once it is done.
void bandicoot_core_geometry_pick(const std::string &kind, bool on);
void bandicoot_core_clear_last_distance();
void bandicoot_core_clear_all_distances();
void bandicoot_core_clear_atom_labels();
// Measures > Environment Distances: the distances shown around the residue at
// the screen centre. Each part applies on its own; the limits together.
struct bandicoot_environment {
   bool shown = false, h_bonds = false, bumps = false, label_atom = false;
   float min_distance = 0, max_distance = 0;   // A
};
bandicoot_environment bandicoot_core_environment();
void bandicoot_core_set_environment_shown(bool shown);
void bandicoot_core_set_environment_h_bonds(bool shown);
void bandicoot_core_set_environment_bumps(bool shown);
void bandicoot_core_set_environment_label_atom(bool label);
// False, changing nothing, if either is out of range; swapped if reversed.
bool bandicoot_core_set_environment_limits(float min_distance, float max_distance);
// Measures > Pointer Distances: distances from the pointer to nearby atoms.
struct bandicoot_pointer_distances {
   bool shown = false;
   float min_distance = 0, max_distance = 0;   // A
};
bandicoot_pointer_distances bandicoot_core_pointer_distances();
void bandicoot_core_set_pointer_distances_shown(bool shown);
// False, changing nothing, if either is out of range.
bool bandicoot_core_set_pointer_distance_limits(float min_distance, float max_distance);
// Measures > Plane Distances: picks either add an atom to the plane or
// measure an atom's deviation from it. Ending clears the plane.
void bandicoot_core_plane_begin();
void bandicoot_core_plane_mode(bool deviation);
void bandicoot_core_plane_remove_last_atom();
void bandicoot_core_plane_end();

// Validate > Incorrect Chiral Volumes: the inverted chiral centres of a model,
// and the residue types it had no restraints for.
struct bandicoot_bad_chirals {
   std::vector<bandicoot_result_row> atoms;
   std::vector<std::string> unknown_types;
};
bandicoot_bad_chirals bandicoot_core_bad_chirals(int imol);
// Validate > Unmodelled blobs: density above rmsd in the map once the model
// is masked out, too big to be waters.
std::vector<bandicoot_xyz> bandicoot_core_find_blobs(int imol_model, int imol_map, float rmsd);
// Validate > Difference Map Peaks. The models are optional (-1 for none).
// The difference maps by number and full name, as Find Waters lists maps.
std::vector<std::pair<int, std::string>> bandicoot_core_difference_maps_full_names();
float bandicoot_core_difference_map_peaks_level();
struct bandicoot_peak {
   bandicoot_xyz at;
   float value = 0, rmsd = 0;
   std::string label;              // as Coot's list shows it
};
// Remembers rmsd for next time.
std::vector<bandicoot_peak> bandicoot_core_difference_map_peaks(int imol_map, int imol_model, float rmsd,
                                                                bool positive, bool negative,
                                                                bool around_model_only);
// Centre on a peak, label being its row in the list.
void bandicoot_core_go_to_peak(const bandicoot_peak &peak, const std::string &label);
// Validate > Check/Delete Waters. A criterion that is off takes no part.
struct bandicoot_check_waters {
   int imol = -1;
   float b_factor = 0, map_rmsd = 0, min_distance = 0, max_distance = 0;
   bool use_b_factor = true, use_map_rmsd = true, use_min_distance = true, use_max_distance = true;
   bool ignore_partial_occupancy_contacts = false, ignore_zero_occupancy = false;
   bool any = true;                     // match any criterion (false: all)
};
// The limits last used.
bandicoot_check_waters bandicoot_core_check_waters_values();
// Both need a refinement map; false (doing nothing) without one.
bool bandicoot_core_check_waters(const bandicoot_check_waters &c, std::vector<bandicoot_result_row> &rows);
bool bandicoot_core_delete_checked_waters(const bandicoot_check_waters &c, int &n_deleted);
// Waters whose difference-map density looks odd.
std::vector<bandicoot_result_row> bandicoot_core_waters_by_difference_map(int imol, int imol_diff_map);
// Validate > GLN and ASN B-factor Outliers: amides whose O and N B-factors
// suggest they are flipped; the flip turns the side chain 180 degrees.
std::vector<bandicoot_result_row> bandicoot_core_gln_asn_outliers(int imol);
void bandicoot_core_flip_side_chain(const bandicoot_result_row &row);
// The model name as Coot shortens it in messages.
std::string bandicoot_core_short_name(int imol);

// Validate > the residue graphs (Geometry, Peptide omega, Temp. fact.
// variance, Average Temp. fact., Rotamer, Density fit, NCS Differences).
// Coot's graph objects draw through this view, one window per graph: a
// residue's block sits in a slot along its chain's axis and is replaced or
// removed as the model changes. graph is the core's handle for a window.
struct bandicoot_graph_block {
   int slot = 0;                   // place along the axis, in residues
   double distortion = 0;          // its height, in Coot's units
   std::string colour;             // as Coot names it ("gold1", ...)
   std::string info;               // shown when the pointer is over it
   bandicoot_result_row target;    // where a click goes
};
struct bandicoot_graph_view {
   std::function<void(const void *graph, int imol, int type, const std::string &title,
                      const std::string &label, int n_chains, int max_chain_length,
                      bool rescale)> open;
   std::function<void(const void *graph, int chain, int nres)> axis;
   std::function<void(const void *graph, int chain, int min_resno, int max_resno)> ticks;
   std::function<void(const void *graph, int chain, const std::string &text)> label;
   std::function<void(const void *graph, int chain, const bandicoot_graph_block &block)> block;
   std::function<void(const void *graph, int chain, int slot)> remove;
};
void bandicoot_core_set_graph_view(const bandicoot_graph_view &view);
// Open a graph by its menu list name ("geometry_analysis1", ...) for a
// model. False, with the reason, if there was nothing to show.
bool bandicoot_core_validation_graph(const std::string &list, int imol, std::string &why_not);
// Density fit: redraw at a new scale (kept for later graphs, as in Coot).
float bandicoot_core_density_fit_scale();
void bandicoot_core_graph_rescale(const void *graph, float scale);
// The window is gone: the core stops updating that graph.
void bandicoot_core_graph_closed(const void *graph);

// Validate > Ramachandran Plot. Each residue with a phi and psi, judged
// against the table for its type, as Coot's Dynarama does.
enum bandicoot_rama_table { BRAMA_ALL, BRAMA_GENERAL, BRAMA_GLY, BRAMA_PRO, BRAMA_PRE_PRO, BRAMA_ILE_VAL,
                            BRAMA_N_TABLES };
struct bandicoot_rama_point {
   double phi = 0, psi = 0;
   int kind = 0;                    // 0 general, 1 GLY, 2 PRO (its symbol)
   int region = 0;                  // 0 outlier, 1 allowed, 2 favoured
   int table = BRAMA_GENERAL;       // the background to show for it
   std::string label;               // "1665 A MET", as Coot labels it
   std::string chain, ins_code;
   int resno = 0;
};
struct bandicoot_rama_set {
   std::vector<bandicoot_rama_point> points;
   int n_favoured = 0, n_allowed = 0;    // of points.size()
};
// selection empty: the whole model; otherwise an mmdb residue selection
// ("//A/1620-1660"). False if the model is gone.
bool bandicoot_core_rama_points(int imol, const std::string &selection, bandicoot_rama_set &set);
// A background picture for a table (an image file Coot ships, 360 x 360
// degrees), or empty when the contour levels are not the standard ones; then
// rgb is filled with Coot's shaded blocks instead, 360 x 360 pixels.
std::string bandicoot_core_rama_background(int table, std::vector<unsigned char> &rgb);
// The table for a residue of the model (as it is now).
int bandicoot_core_rama_table(int imol, const std::string &chain, int resno, const std::string &ins_code);
// The window's heading, as Coot's: "28 ...ed_datasets/x0191/x0191-pandda-input.pdb".
std::string bandicoot_core_rama_heading(int imol);
// Validate > Probe clashes: reduce and probe run on the model by Coot's
// script, which then lists reduce's flips and shows the dots. False, with the
// reason, if either program cannot be found.
bool bandicoot_core_probe_clashes(int imol, std::string &why_not);

// Validate > Kleywegt Plot: residue by residue, how a model's phi and psi
// differ from another's (or one chain's from another's), the biggest
// differences first and no more than Coot shows. Without chains every chain
// of imol1 is compared with the same chain of imol2. False if a model is gone.
struct bandicoot_kleywegt_pair { bandicoot_rama_point first, second; };
bool bandicoot_core_kleywegt(int imol1, int imol2, bool by_chain, const std::string &chain1,
                             const std::string &chain2, std::vector<bandicoot_kleywegt_pair> &pairs);
// What the core tells an open plot.
struct bandicoot_rama_view {
   std::function<void(int imol)> changed;                    // redraw from the model
   // the current residue is now this one (Coot's green square)
   std::function<void(int imol, const std::string &chain, int resno, const std::string &ins_code)> highlight;
   std::function<void()> molecules_changed;                  // one may have closed
};
void bandicoot_core_set_rama_view(const bandicoot_rama_view &view);

// Find Ligands: what the window offers, filled from Coot's current settings,
// and the choices it hands back to search with.
struct bandicoot_find_ligands {
   struct molecule { int imol; std::string label; int n_atoms; bool difference_map; };
   std::vector<molecule> maps, proteins, ligands;
   int atom_limit = 0;           // ligands listed have fewer atoms than this
   int many_atoms = 200;         // more than this asks before searching
   // the choices
   int map = -1, protein = -1;
   std::vector<std::pair<int, bool>> chosen;   // ligand, flexible
   bool mask_waters = true, here = false, multi = false, refine = true;
   float sigma = 1.0f, frac_scoring = 0.9f, frac_correl = 0.9f;
   int n_top_sites = 10, n_conformers = 50;
};
bandicoot_find_ligands bandicoot_core_find_ligands_setup();
// Stores the choices as Coot's settings and searches; Coot reports the result.
void bandicoot_core_find_ligands(const bandicoot_find_ligands &f);

// The residue at the screen centre; false if there is none.
bool bandicoot_core_active_residue(int &imol, std::string &chain_id, int &resno,
                                   std::string &ins_code, std::string &res_name);
// SMILES -> simple 3D: a new molecule of residue type comp_id, built by RDKit
// at the screen centre.
bool bandicoot_core_smiles_to_3d(const std::string &smiles, const std::string &comp_id,
                                 std::string &why_not);
// Rename Atom to Reference: writes a dictionary for the residue as type
// new_comp_id, its atoms named as in reference_comp_id's.
bool bandicoot_core_rename_to_reference(int imol, const std::string &chain_id, int resno,
                                        const std::string &ins_code,
                                        const std::string &reference_comp_id,
                                        const std::string &new_comp_id,
                                        const std::string &cif_file_name, std::string &why_not);

// Monomer from Dictionary: a new molecule for a 3-letter code (from the
// dictionary, or fetched). New Molecule by Sphere: the atoms within radius of
// the screen centre. New Molecule from Symmetry Op: a copy moved by a symop
// ("X,Y,Z" style). Residue Type Selection: the residues of one type. Fetch
// PDBe Ligand Description: its name and SMILES (status bar and console),
// placing the monomer. False, with the reason, if nothing was made.
bool bandicoot_core_monomer_from_dictionary(const std::string &comp_id, std::string &why_not);
// Rename Residue: the residue at the screen centre gets a new name.
bool bandicoot_core_rename_active_residue(const std::string &name, std::string &why_not);
bool bandicoot_core_new_molecule_by_sphere(int imol, float radius, std::string &why_not);
bool bandicoot_core_new_molecule_by_symop(int imol, const std::string &symop, std::string &why_not);
bool bandicoot_core_residue_type_selection(int imol, const std::string &residue_type, std::string &why_not);
void bandicoot_core_fetch_pdbe_ligand(const std::string &comp_id);

// Ligand Restraints: the components to offer (every ligand, or only those a
// load left without restraints), each with its label and whether nothing
// describes it yet; and generation for the ticked ones. progress gets each
// step; report the failures and caveats (empty if none). False if no
// generator could be found, with the reason in report.
struct bandicoot_restraint_row { std::string comp_id, label; bool needed = false; };
std::vector<bandicoot_restraint_row> bandicoot_core_restraint_rows(bool show_all);
bool bandicoot_core_generate_restraints(bool show_all, const std::vector<std::string> &comp_ids,
                                        std::function<void(const std::string &)> progress,
                                        std::string &report);
// The core asks for the window: from the menu (show_all, say so if nothing is
// found) or after a load (only what is missing; fresh = what this load brought).
void bandicoot_core_set_restraints_view(
   std::function<void(bool show_all, const std::vector<std::string> &fresh, bool announce_empty)> open);
// Modelling > Generate Ligand Restraints, as the menu opens it.
void bandicoot_core_open_ligand_restraints();

// The Modelling items that act on a whole model chosen first (e.g.
// "BMOD_RENUMBER_WATERS"). False if the id is not one of them.
bool bandicoot_core_modelling_on_model(const std::string &id, int imol);

// Rigid Body Fit Residue Range, into the refinement map. Superpose Ligands:
// the moving ligand's whole molecule is moved onto the reference ligand.
// Associate Sequence: a FASTA or PIR file, to one chain or every protein
// chain. Cootaneer: assigns side chains from the map, after the sequence in
// file if one is given, then optionally fits the protein. False, with the
// reason, if nothing was done.
bool bandicoot_core_rigid_body_fit_range(int imol, const std::string &chain, int first, int last,
                                         std::string &why_not);
bool bandicoot_core_superpose_ligands(int imol_reference, const std::string &chain_reference, int resno_reference,
                                      int imol_moving, const std::string &chain_moving, int resno_moving,
                                      std::string &why_not);
bool bandicoot_core_associate_sequence(int imol, const std::string &chain, const std::string &file, bool pir,
                                       bool all_protein_chains, std::string &why_not);
bool bandicoot_core_cootaneer(int imol, const std::string &file, bool fit_protein_after, std::string &why_not);

// Add Other Solvent Molecules: the comp-ids offered, and adding one to a
// model (jiggle-fitted at the screen centre when there is a refinement map).
std::vector<std::string> bandicoot_core_solvent_comp_ids();
void bandicoot_core_add_solvent(int imol, const std::string &comp_id);

// Other Modelling Tools. The atom picks are named as Coot names their toggles
// ("cis_trans_conversion_toggle_button", "model_refine_dialog_db_main_togglebutton",
// "reverse_fragment_direction_togglebutton", "other_tools_base_pair_toggle_button");
// on waits for the pick, off stops waiting. The core lets go of a toggle
// through bandicoot_ui_view::release_tools_toggle once its pick is done.
void bandicoot_core_tools_pick(const std::string &widget, bool on);
void bandicoot_core_make_link_pick();
// Place a helix or strand at the screen centre, into the refinement map.
bool bandicoot_core_place_helix_here(std::string &why_not);
bool bandicoot_core_place_strand_here(int n_residues, std::string &why_not);
// Fill Partial Residues, Delete Hydrogen Atoms (in the molecule at the screen
// centre) and Delete Side-chains for Active Chain, by those labels.
bool bandicoot_core_active_molecule_tool(const std::string &tool, std::string &why_not);
void bandicoot_core_set_undo_molecule(int imol);
// A ligand built from SMILES, placed at the screen centre. message says how
// it went (also on the status bar); false if no molecule was made.
bool bandicoot_core_ligand_from_smiles(const std::string &smiles, const std::string &code, std::string &message);

// Find Secondary Structure (templates and lengths as Coot numbers them;
// radius 0 searches the whole map) and Build Nucleic Acid (within radius of
// the screen centre), into the refinement map.
bool bandicoot_core_find_secondary_structure(bool helices, int helix_template, int helix_length,
                                             bool strands, int strand_template, int strand_length,
                                             float radius, std::string &why_not);
bool bandicoot_core_find_nucleic_acids(float radius, std::string &why_not);
// Add OXT at the chain's C terminus, or after the given residue.
bool bandicoot_core_add_oxt(int imol, const std::string &chain, bool c_terminus, int resno,
                            std::string &why_not);
// DNA & RNA Models: an ideal model of sequence at the screen centre.
bool bandicoot_core_ideal_nucleic_acid(bool dna, bool form_b, bool single_stranded, const std::string &sequence,
                                       std::string &why_not);

// C-alpha Baton Mode. start is false, doing nothing, when no map has a
// skeleton. The ops are the Baton window's buttons ("accept", "try another",
// "previous", "undo", "lengthen", "shorten"); stop puts the baton away.
bool bandicoot_core_baton_start();
void bandicoot_core_baton_op(const std::string &op);
void bandicoot_core_baton_stop();
void bandicoot_core_baton_build_params(int start_resno, const std::string &chain, bool backwards);

// Multi-Residue Torsion: start picking residues; picked (Apply) offers their
// rotatable bonds through bandicoot_ui_view::multi_torsion; select makes one
// the bond that dragging turns; finish keeps or drops the result, and also
// ends a pick that never got that far.
void bandicoot_core_multi_torsion_start();
void bandicoot_core_multi_torsion_picked();
void bandicoot_core_multi_torsion_select(int bond, bool reverse);
void bandicoot_core_multi_torsion_reverse(bool reverse);
void bandicoot_core_multi_torsion_finish(bool accept);

// Add N-linked Glycan: opening installs the sugar ring restraints; the sugars
// and links it offers (label, residue type, link), and which of them suit the
// residue now centred for a glycan tree type (by label).
void bandicoot_core_glycan_opened();
struct bandicoot_glycan_link { std::string label, residue, link; };
std::vector<bandicoot_glycan_link> bandicoot_core_glycan_links();
std::map<std::string, bool> bandicoot_core_glycan_link_states(const std::string &tree_type);
void bandicoot_core_add_glycan_link(const bandicoot_glycan_link &link);
void bandicoot_core_refine_glycan_tree();
// The core asks for the window to be brought up to date when the view is recentred.
void bandicoot_core_set_glycan_view(std::function<void()> refresh);

// PanDDA Inspect, driven by python/bandicoot_pandda.py. Opening readies the
// driver and quiets the per-dataset ligand restraint questions, which a
// PanDDA folder would otherwise raise once per dataset; closing gives them
// back as they were.
void bandicoot_core_pandda_opened();
void bandicoot_core_pandda_closed();
// A driver call (e.g. "next_event()") and the string it returns: a status
// such as "PanDDA 3/12 ...", an error starting "PanDDA: ", or a value.
std::string bandicoot_core_pandda_call(const std::string &call);
// The UI the panel opens in, from Preferences: true for Basic, false for Full.
bool bandicoot_core_pandda_basic_ui();
// The core asks for the panel (the --pandda launcher), with a folder to load
// (empty for none).
void bandicoot_core_set_pandda_view(std::function<void(const std::string &folder)> open);

// Windows that edit the moving atoms after a pick: Rotation & Translation,
// Select Rotamer, Edit Chi Angles, Edit Backbone Torsions. The core opens
// them; while any is up, a drag in the graphics does not move the atoms
// bodily (see open_windows).
struct bandicoot_moving_atoms_view {
   std::function<void()> rot_trans;
   // rotamer labels, best first; alt_conf: show the new alt conf occupancy
   std::function<void(const std::vector<std::string> &labels, bool alt_conf,
                      float occupancy)> rotamers;
   // chi labels; mode 1 is the alt-conf split by torsion, retitled
   std::function<void(const std::vector<std::string> &labels, int mode,
                      bool hydrogens)> edit_chi;
   std::function<void()> edit_backbone;
   // Edit Backbone Torsions' Ramachandran plot: the residue edited and the
   // next one, as they now are (either may be missing)
   std::function<void(const std::vector<bandicoot_rama_point> &points)> backbone_rama;
   std::function<bool()> open_windows;
};
void bandicoot_core_set_moving_atoms_view(const bandicoot_moving_atoms_view &view);

// Rotate Translate's drop-down: 0 by residue range, 1 by chain, 2 by
// molecule; picking starts at once, as Coot's menu did.
void bandicoot_core_rot_trans_by(int which);
// Rotation & Translation: slider i (x, y, z translation, then x, y, z
// rotation) set to v, in the slider's units.
void bandicoot_core_rot_trans_slider(int i, double v);
void bandicoot_core_rot_trans_finish(bool accept);
// Select Rotamer: show rotamer i (0-based), the new alt conf's occupancy.
void bandicoot_core_rotamer_show(int i);
void bandicoot_core_rotamer_occupancy(float v);
void bandicoot_core_rotamer_finish(bool accept);
// Edit Chi Angles: drag torsion i (0-based); flash it; the hydrogen torsions
// listed or not (returning the new labels); reverse which part moves; view
// rotation (no torsion dragged).
void bandicoot_core_chi_choose(int i);
void bandicoot_core_chi_flash(int i);
std::vector<std::string> bandicoot_core_chi_hydrogens(bool on);
void bandicoot_core_chi_reverse(bool on);
void bandicoot_core_chi_view_rotation();
void bandicoot_core_chi_finish(bool accept);
// Edit Backbone Torsions: the peptide or carbonyl rotation, in degrees.
void bandicoot_core_backbone_peptide(double degrees);
void bandicoot_core_backbone_carbonyl(double degrees);
void bandicoot_core_backbone_finish(bool accept);

// The choosers the core opens after a pick or a sidebar click: residue
// type (Simple Mutate, with a Stub option; Mutate & Auto Fit), nucleic-acid
// base, pointer atom type, Add Alternate Conformation.
struct bandicoot_chooser_view {
   std::function<void(bool show_stub)> residue_type;
   std::function<void()> base;
   // models (imol, label) and the one to add to
   std::function<void(const std::vector<std::pair<int, std::string>> &models,
                      int imol)> pointer_atom_type;
   // split_type: 0 at the C-alpha, 1 the whole residue, 2 a residue range
   std::function<void(int split_type)> add_alt_conf;
   std::function<void()> add_alt_conf_done;  // the pick is made
};
void bandicoot_core_set_chooser_view(const bandicoot_chooser_view &view);

// Mutate the picked residue to res_type ("ALA", ...), or by its one-letter
// code (false, mutating nothing, if it is not a standard amino acid).
void bandicoot_core_mutate(const std::string &res_type, bool stub);
bool bandicoot_core_mutate_by_letter(char code, bool stub);
// Mutate the picked nucleotide to base "A", "C", "G", "T" or "U"; the base
// chooser closing (any way) ends its pick, as Coot's did.
void bandicoot_core_mutate_base(const std::string &base);
void bandicoot_core_base_chooser_closed();
// Put an atom of this type ("Water", "Ca", "SO4", or typed in) at the
// screen centre, in molecule imol.
void bandicoot_core_place_atom(int imol, const std::string &type);
// Add Alternate Conformation: the split type; cancelled.
void bandicoot_core_set_alt_conf_split_type(int split_type);
void bandicoot_core_cancel_add_alt_conf();

// Delete item and Find Waters, and the list of unmodelled blobs Find
// Waters can leave.
struct bandicoot_delete_view {
   // open with this kind of item chosen (see bandicoot_core_delete_kind)
   std::function<void(int kind)> open;
   std::function<bool()> is_open;
   std::function<bool()> keep_active;
   // the core closes it (after a pick); cleanup: also end the delete pick
   std::function<void(bool cleanup)> close;
   // centre-able blobs: x, y, z each
   std::function<void(const std::vector<std::vector<float>> &blobs)> blobs;
};
void bandicoot_core_set_delete_view(const bandicoot_delete_view &view);
// Delete item: opening arms the pick and says which kind to show first;
// kind 0 residue, 1 water, 2 atom, 3 side chain, 4 side-chain zone, 5 chain,
// 6 hydrogens in residue, 7 zone. Closing by hand ends the pick.
int  bandicoot_core_delete_begin();
void bandicoot_core_delete_kind(int kind);
void bandicoot_core_delete_closed();
// Find Waters: the maps and models offered (imol, label), the values last
// used, and the search itself.
struct bandicoot_find_waters_values { float rmsd, min_distance, max_distance; };
std::vector<std::pair<int, std::string>> bandicoot_core_find_waters_maps();
std::vector<std::pair<int, std::string>> bandicoot_core_find_waters_models();
bandicoot_find_waters_values bandicoot_core_find_waters_values();
void bandicoot_core_find_waters(int imol_map, int imol_model, bool new_molecule,
                                const bandicoot_find_waters_values &values);
void bandicoot_core_centre_on(float x, float y, float z);

// Fixed atoms: 0 stop picking, 1 pick atoms to fix, 2 pick atoms to unfix.
void bandicoot_core_fixed_atom_pick(int mode);
void bandicoot_core_clear_fixed_atoms();

// About > Header/Remarks Browser. An mmCIF model shows its categories, which
// a filter narrows; a PDB model shows its Compound, Author, Journal and Links
// panels and one panel per REMARK number, unfiltered.
struct bandicoot_header_panel {
   std::string title;
   std::string text;
   unsigned char red, green, blue;  // background
   bool fixed_width;
   bool shown;                      // mmCIF: has something matching the filter
};
struct bandicoot_header_view {
   std::string title;  // the structure's title
   bool mmcif = false;
   std::vector<bandicoot_header_panel> panels;
};
// The panels for this filter (any case; empty for all) and, for mmCIF, with
// or without the per-atom and per-residue tables. The panels come in the same
// order, and as many, whatever the filter. False if the model has no header
// information.
bool bandicoot_core_header_view(int imol, const std::string &filter, bool show_all,
                                bandicoot_header_view *view);

// Display Manager > Properties: a map's settings, and its density histogram
// with the contour level marked.
struct bandicoot_map_properties {
   std::string cell, spacegroup, resolution;
   bool solid_surface = false;     // drawn as a surface rather than lines
   float opacity = 1;              // of the surface, 0 to 1
   float contour_level = 0;        // absolute
   bool step_by_rmsd = false;      // contour level steps in r.m.s.d.
   float rmsd_step = 0;
   bool skeleton = false;
   bandicoot_rgb colour;
   // the histogram: counts in bins of equal width from min_density; empty if
   // the map has none
   std::vector<int> bins;
   float min_density = 0, bin_width = 0, mean = 0, rmsd = 0;
};
bool bandicoot_core_map_properties(int imol, bandicoot_map_properties *p);
void bandicoot_core_set_map_surface(int imol, bool solid);
void bandicoot_core_set_map_opacity(int imol, float opacity);
// level in r.m.s.d. if in_rmsd, otherwise absolute
void bandicoot_core_set_map_contour_level(int imol, float level, bool in_rmsd);
// on: the scroll wheel and +/- step the contour level by step r.m.s.d.
void bandicoot_core_set_map_rmsd_step(int imol, bool on, float step);
void bandicoot_core_set_map_skeleton(int imol, bool on);

// The directory holding Bandicoot's data files (the licence among them).
std::string bandicoot_core_package_data_dir();

// Runs a toolbar, menu or sidebar action by its action name; state is a
// toggle's new state (true for a push button). False if the wx build does not
// run that action yet.
bool bandicoot_core_run_action(const std::string &action, bool state);

// Molecules with unsaved changes, one "imol  name" line each.
std::vector<std::string> bandicoot_core_unsaved_molecules();

// The main window is closing: save state and history, then exit.
void bandicoot_core_quit();

#endif // BANDICOOT_WX_CORE_BRIDGE_HH

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
};
void bandicoot_core_set_ar_view(const bandicoot_ar_view &view);
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

// Display Manager: the loaded maps and models and their display state.
struct bandicoot_dm_molecule {
   int imol;
   std::string name;    // as the Display Manager shows it
   bool displayed;
   bool active;         // models
   int render_as;       // models: index in bandicoot_core_render_as_labels(), -1 if none fits
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

// Runs a toolbar, menu or sidebar action by its action name; state is a
// toggle's new state (true for a push button). False if the wx build does not
// run that action yet.
bool bandicoot_core_run_action(const std::string &action, bool state);

// Molecules with unsaved changes, one "imol  name" line each.
std::vector<std::string> bandicoot_core_unsaved_molecules();

// The main window is closing: save state and history, then exit.
void bandicoot_core_quit();

#endif // BANDICOOT_WX_CORE_BRIDGE_HH

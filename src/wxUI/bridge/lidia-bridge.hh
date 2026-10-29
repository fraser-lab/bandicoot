// src/wxUI/bridge/lidia-bridge.hh
//
// The ligand builder (Lidia) for the wx interface. The builder's own code
// keeps its molecule and its drawing; a wx window shows the drawing and
// passes on the pointer and the tool choices. Plain types only, as the rest
// of the bridge.

#ifndef BANDICOOT_WXUI_LIDIA_BRIDGE_HH
#define BANDICOOT_WXUI_LIDIA_BRIDGE_HH

#include <functional>
#include <string>
#include <vector>

// One open builder. Opaque outside the bridge.
typedef void *bandicoot_lidia_id;

// What the builder reports to its window.
struct bandicoot_lidia_hooks {
   std::function<void(bandicoot_lidia_id)> open; // a builder needs a window
   std::function<void(bandicoot_lidia_id, const std::string &)> status_text;
   std::function<void(bandicoot_lidia_id, bool shown, const std::string &)> alert;
   std::function<void(bandicoot_lidia_id, bool)> apply_sensitive;
   std::function<void(bandicoot_lidia_id, const std::string &element)> atom_x_dialog;
   std::function<void(bandicoot_lidia_id, const std::string &what_failed,
                      const std::string &subject)> error;
};

// From then on every builder the core opens (Calculate > Ligand Builder,
// FLEV, ...) goes to hooks.open instead of a GTK window.
void bandicoot_lidia_install(const bandicoot_lidia_hooks &hooks);

// Calculate > Ligand Builder: an empty builder.
void bandicoot_lidia_start();

// What a click on the canvas adds or changes.
enum class bandicoot_lidia_tool {
   none,
   ring_3, ring_4, ring_5, ring_6, ring_6_aromatic, ring_7, ring_8,
   carbon, nitrogen, oxygen, hydrogen, sulfur, phosphorus,
   fluorine, chlorine, iodine, bromine, other_element,
   charge, single_bond, double_bond, triple_bond, stereo_bond,
   cut
};
void bandicoot_lidia_set_tool(bandicoot_lidia_id id, bandicoot_lidia_tool tool);

// The canvas, in its own units, and a picture of part of it: width x height
// pixels showing canvas units from (x0, y0) at `scale` pixels per unit, as
// RGB bytes, row by row.
void bandicoot_lidia_canvas_size(bandicoot_lidia_id id, double *width, double *height);
void bandicoot_lidia_render(bandicoot_lidia_id id, int width, int height,
                            double scale, double x0, double y0,
                            std::vector<unsigned char> *rgb);

// The pointer, at canvas units (x, y).
enum class bandicoot_lidia_mouse { press, double_press, release, motion };
void bandicoot_lidia_pointer(bandicoot_lidia_id id, bandicoot_lidia_mouse what, int button,
                             double x, double y, bool shift, bool control, bool button_1_down);

void bandicoot_lidia_clear(bandicoot_lidia_id id);
void bandicoot_lidia_undo(bandicoot_lidia_id id);
void bandicoot_lidia_delete_hydrogens(bandicoot_lidia_id id);
void bandicoot_lidia_tidy_up(bandicoot_lidia_id id);
std::string bandicoot_lidia_smiles(bandicoot_lidia_id id);
void bandicoot_lidia_set_atom_x(bandicoot_lidia_id id, const std::string &element);
void bandicoot_lidia_show_alerts(bandicoot_lidia_id id, bool shown);

// For a builder opened by FLEV: the ligand's surroundings, and their key.
void bandicoot_lidia_show_environment(bandicoot_lidia_id id, bool shown);
void bandicoot_lidia_show_key(bandicoot_lidia_id id, bool shown);

// Display > Standard, Atom Indices, Atom Names.
enum class bandicoot_lidia_labels { standard, indices, names };
void bandicoot_lidia_set_labels(bandicoot_lidia_id id, bandicoot_lidia_labels labels);

void bandicoot_lidia_new(bandicoot_lidia_id id); // File > New: another builder
void bandicoot_lidia_import_smiles(bandicoot_lidia_id id, const std::string &smiles);
void bandicoot_lidia_import_comp_id(bandicoot_lidia_id id, const std::string &comp_id,
                                    bool keep_hydrogens);

// File menu. Paths are absolute.
void bandicoot_lidia_open_file(bandicoot_lidia_id id, const std::string &path); // mol or cif
void bandicoot_lidia_save(bandicoot_lidia_id id);   // to the last name saved as
void bandicoot_lidia_save_as(bandicoot_lidia_id id, const std::string &path);
enum class bandicoot_lidia_export { pdf, png, svg };
void bandicoot_lidia_export_as(bandicoot_lidia_id id, bandicoot_lidia_export format,
                               const std::string &path);
void bandicoot_lidia_fetch(bandicoot_lidia_id id, const std::string &name);

// Apply: a 3D model of the molecule, with its restraints, made in Coot.
void bandicoot_lidia_apply(bandicoot_lidia_id id);

// Ligand > SMILES -> 2D: a builder showing the molecule.
void bandicoot_lidia_smiles_to_2d(const std::string &smiles);

// The window has gone; the builder goes with it.
void bandicoot_lidia_close(bandicoot_lidia_id id);

#endif // BANDICOOT_WXUI_LIDIA_BRIDGE_HH

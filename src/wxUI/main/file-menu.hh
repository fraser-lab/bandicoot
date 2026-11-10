// src/wxUI/main/file-menu.hh
//
// The File menu's windows beyond the plain file choosers: Column Label
// Assignment (for an MTZ opened as a dataset), Get Monomer, Search Monomer
// Library, the accession-code window the Fetch items share, Recover
// Coordinates, loading a saved state, Export Map and Delete Molecules.

#ifndef BANDICOOT_WXUI_FILE_MENU_HH
#define BANDICOOT_WXUI_FILE_MENU_HH

#include <string>
#include <vector>

class wxWindow;

namespace wxui {

// Choose an MTZ file's map coefficients, then make the map.
void show_column_labels(wxWindow *parent, const std::string &mtz_file_name);

void show_get_monomer(wxWindow *parent);
void show_monomer_search(wxWindow *parent);

// kind: as bandicoot_accession_kind (core-bridge.hh)
void show_accession_code(wxWindow *parent, int kind);

// One Recover Coordinates window per model with a newer backup.
void show_recover_session(wxWindow *parent);
// File > Load State: choose a state file, then load it.
void show_load_state(wxWindow *parent);
// At startup: recover the auto-saved state at default_path (contents: the
// models and maps it would load) or another saved state, or neither.
void show_startup_state(wxWindow *parent, const std::string &default_path,
                        const std::vector<std::string> &contents);
// Choose a map (and, for a fragment, a radius), then the file to write.
void show_export_map(wxWindow *parent, bool fragment);
void show_delete_molecules(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_FILE_MENU_HH

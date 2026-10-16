// src/wxUI/main/file-menu.hh
//
// The File menu's windows beyond the plain file choosers: Column Label
// Assignment (for an MTZ opened as a dataset), Get Monomer, Search Monomer
// Library and the accession-code window the Fetch items share.

#ifndef BANDICOOT_WXUI_FILE_MENU_HH
#define BANDICOOT_WXUI_FILE_MENU_HH

#include <string>

class wxWindow;

namespace wxui {

// Choose an MTZ file's map coefficients, then make the map.
void show_column_labels(wxWindow *parent, const std::string &mtz_file_name);

void show_get_monomer(wxWindow *parent);
void show_monomer_search(wxWindow *parent);

// kind: as bandicoot_accession_kind (core-bridge.hh)
void show_accession_code(wxWindow *parent, int kind);

} // namespace wxui

#endif // BANDICOOT_WXUI_FILE_MENU_HH

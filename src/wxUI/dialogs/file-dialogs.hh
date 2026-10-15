// src/wxUI/dialogs/file-dialogs.hh
//
// File choosers: the system's own open and save panels, with Coot's file-type
// filters and, where Coot had them, extra controls inside the panel.
//
// The extension lists come from the caller: the core owns them (scripts can add
// to them), so they are not repeated here.

#ifndef BANDICOOT_WXUI_FILE_DIALOGS_HH
#define BANDICOOT_WXUI_FILE_DIALOGS_HH

#include <wx/string.h>

#include <vector>

class wxWindow;

namespace wxui {

struct FileType {
   wxString description;              // "Coordinates"
   std::vector<wxString> extensions;  // ".pdb", ".cif.gz", ...
};

// A labelled check box or drop-down shown in the panel. value is read back
// when the panel closes with a file.
struct CheckOption  { wxString label; bool value = false; };
struct ChoiceOption { wxString label; std::vector<wxString> items; int value = 0; };

struct FileRequest {
   wxString title;
   std::vector<FileType> types;  // offered as filters, in order
   int type_index = 0;           // in: the initial filter; out: the one in use
   wxString directory;           // in: where to start; out: where the file was
   wxString file_name;           // save: the suggested name
   bool multiple = false;        // open: allow several files
   std::vector<CheckOption>  checks;
   std::vector<ChoiceOption> choices;
};

// Empty when cancelled. Opening always offers "All files" after the given types.
std::vector<wxString> open_files(wxWindow *parent, FileRequest &request);
wxString save_file(wxWindow *parent, FileRequest &request);
wxString choose_folder(wxWindow *parent, const wxString &title, const wxString &start);

// "Coordinates (*.pdb;*.cif)|*.pdb;*.cif|..." for the given types.
wxString wildcard_for(const std::vector<FileType> &types, bool add_all_files);

// ------------------------------------------------------------------------
// Coot's choosers that carry extra controls

// "Recentre?" on opening coordinates
enum class Recentre { OnMolecule = 0, DontRecentre = 1, MoleculeHere = 2 };

struct CoordinatesChoice {
   std::vector<wxString> paths;  // one or more, as Coot allowed
   Recentre recentre = Recentre::OnMolecule;
};
bool open_coordinates(wxWindow *parent, const std::vector<FileType> &types,
                      wxString &directory, Recentre initial, CoordinatesChoice &out);

struct MapChoice {
   wxString path;
   bool is_difference_map = false;
};
bool open_map(wxWindow *parent, const std::vector<FileType> &types,
              wxString &directory, MapChoice &out);

// formats: one entry per output format, offered as the panel's file types, so
// picking one also fixes the extension.
struct SaveCoordinatesChoice {
   wxString path;
   int format = 0;
   bool save_hydrogens = true;
   bool save_aniso = true;
};
bool save_coordinates(wxWindow *parent, const std::vector<FileType> &formats,
                      wxString &directory, const wxString &suggested_name,
                      SaveCoordinatesChoice &inout);

} // namespace wxui

#endif // BANDICOOT_WXUI_FILE_DIALOGS_HH

// src/wxUI/main/file-actions.cc

#include "wxUI/main/file-actions.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/message.hh"

#include <wx/filename.h>

#include <vector>

namespace wxui {

namespace {

FileType file_type(const char *description, const std::vector<std::string> &extensions) {
   FileType t;
   t.description = description;
   for (const std::string &e : extensions) t.extensions.push_back(wxString::FromUTF8(e));
   return t;
}

wxString chooser_directory() {
   return wxString::FromUTF8(bandicoot_core_chooser_directory());
}

void save_chooser_directory(const wxString &directory) {
   bandicoot_core_save_chooser_directory(directory.utf8_string());
}

void open_coordinates_action(wxWindow *parent) {
   wxString dir = chooser_directory();
   Recentre initial = bandicoot_core_recentre_on_read() ? Recentre::OnMolecule
                                                        : Recentre::DontRecentre;
   CoordinatesChoice choice;
   if (! open_coordinates(parent, { file_type("Coordinates", bandicoot_core_coordinates_extensions()) },
                          dir, initial, choice))
      return;
   save_chooser_directory(dir);
   for (const wxString &p : choice.paths)
      bandicoot_core_read_coordinates(p.utf8_string(), int(choice.recentre));
}

void auto_open_mtz_action(wxWindow *parent) {
   FileRequest r;
   r.title = "Select Dataset File";
   r.types = { file_type("Data", bandicoot_core_data_extensions()) };
   r.directory = chooser_directory();
   std::vector<wxString> paths = open_files(parent, r);
   if (paths.empty()) return;
   save_chooser_directory(r.directory);

   std::string why_not;
   if (! bandicoot_core_auto_open_dataset(paths.front().utf8_string(), why_not))
      error(parent, "Could not auto-open", wxFileName(paths.front()).GetFullName(),
            "Auto-open MTZ: " + paths.front().utf8_string() + ": " + why_not);
}

void open_map_action(wxWindow *parent) {
   wxString dir = chooser_directory();
   MapChoice choice;
   if (! open_map(parent, { file_type("Maps", bandicoot_core_map_extensions()) }, dir, choice))
      return;
   save_chooser_directory(dir);
   bandicoot_core_read_map(choice.path.utf8_string(), choice.is_difference_map);
}

} // namespace

bool run_file_action(wxWindow *parent, const std::string &action) {

   if (action == "on_coords_toolbutton_clicked")
      open_coordinates_action(parent);
   else if (action == "bandicoot_action_auto_open_mtz")
      auto_open_mtz_action(parent);
   else if (action == "bandicoot_action_open_map")
      open_map_action(parent);
   else
      return false;
   return true;
}

} // namespace wxui

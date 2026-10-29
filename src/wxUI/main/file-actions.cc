// src/wxUI/main/file-actions.cc

#include "wxUI/main/file-actions.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/main/file-menu.hh"

#include <wx/filename.h>
#include <wx/utils.h>

#include <functional>
#include <map>
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

void open_chosen_dataset(wxWindow *parent, const std::string &file);

void open_dataset_action(wxWindow *parent) {
   FileRequest r;
   r.title = "Select Dataset File";
   r.types = { file_type("Data", bandicoot_core_data_extensions()) };
   r.directory = chooser_directory();
   std::vector<wxString> paths = open_files(parent, r);
   if (paths.empty()) return;
   save_chooser_directory(r.directory);
   open_chosen_dataset(parent, paths.front().utf8_string());
}

void open_chosen_dataset(wxWindow *parent, const std::string &file) {
   switch (bandicoot_core_open_dataset(file)) {
   case bandicoot_dataset_step::columns:
      show_column_labels(parent, file);
      break;
   case bandicoot_dataset_step::phs: {
      // a .phs file carries no cell or symmetry: a molecule supplies them
      MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
      if (models.empty()) {
         info(parent, ".phs files additionally need cell and symmetry.\n"
                      "We need a coordinates file to assign them.\n\n"
                      "(Note that this will not read coordinates -\njust the cell and symmetry)");
         break;
      }
      ChooserDialog *d = new ChooserDialog(parent, "Cell and Symmetry",
                                           "Take the cell and symmetry from:", models);
      d->on_ok = [file](int imol) { bandicoot_core_read_phs(file, imol); };
      d->CentreOnParent();
      d->Show();
      break;
   }
   case bandicoot_dataset_step::done:
      break;
   case bandicoot_dataset_step::unreadable:
      error(parent, "Could not open dataset", wxFileName(wxString::FromUTF8(file)).GetFullName(),
            "Open dataset: " + file + ": not an MTZ, mmCIF, fcf or phs file");
      break;
   }
}

void import_dictionary_action(wxWindow *parent) {
   FileRequest r;
   r.title = "Select Cif Dictionary File";
   r.types = { file_type("Dictionaries", bandicoot_core_dictionary_extensions()) };
   r.directory = chooser_directory();
   // Coot's Select Mol: Auto, All, then each model (shown, as in Coot,
   // when no model is loaded too)
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   ChoiceOption mol;
   mol.label = "Select Mol";
   if (models.empty()) {
      mol.items = { "No molecules" };
   } else {
      mol.items = { "Auto", "All" };
      for (const auto &m : models) mol.items.push_back(wxString::FromUTF8(m.second));
   }
   r.choices.push_back(mol);
   CheckOption generate;
   generate.label = "Generate a Molecule";
   generate.value = bandicoot_core_dictionary_generates_molecule();
   r.checks.push_back(generate);

   std::vector<wxString> paths = open_files(parent, r);
   if (paths.empty()) return;
   save_chooser_directory(r.directory);
   int molecule = -3;  // no molecules
   if (! models.empty()) {
      int i = r.choices[0].value;
      molecule = i == 0 ? -2 : i == 1 ? -1 : models[i - 2].first;
   }
   bandicoot_core_import_dictionary(paths.front().utf8_string(), molecule, r.checks[0].value);
}

// The save panel, with the request's formats as its file types.
void save_coordinates_panel(wxWindow *parent, bandicoot_save_coords request) {
   std::vector<FileType> formats;
   for (const bandicoot_coords_format &f : request.formats)
      formats.push_back(file_type(f.label.c_str(), f.extensions));
   SaveCoordinatesChoice choice;
   choice.format = request.format;
   choice.save_hydrogens = request.hydrogens;
   choice.save_aniso = request.aniso;
   wxString dir = wxString::FromUTF8(request.directory);
   if (! save_coordinates(parent, formats, dir, wxString::FromUTF8(request.file_name), choice))
      return;
   request.format = choice.format;
   request.hydrogens = choice.save_hydrogens;
   request.aniso = choice.save_aniso;
   std::string path = choice.path.utf8_string();
   bool saved;
   {
      wxBusyCursor busy;
      saved = bandicoot_core_save_coords(request, path);
   }
   if (! saved)
      error(parent, "Could not save coordinates to", wxFileName(choice.path).GetFullName(),
            "Save Coordinates: molecule " + std::to_string(request.imol) + " not written to " + path);
}

void save_coordinates_action(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No coordinates to save");
      return;
   }
   ChooserDialog *d = new ChooserDialog(parent, "Save Coordinates Molecule Selector",
                                        "Select Molecule Number to Save:", models);
   d->chooser()->set_imol(bandicoot_core_save_coords_default_molecule());
   if (wxWindow *ok = d->FindWindow(wxID_OK)) ok->SetLabel("Select Filename...");
   d->Fit();
   // the file panel follows once the selector has gone
   d->on_ok = [parent](int imol) {
      parent->CallAfter([parent, imol]() {
         save_coordinates_panel(parent, bandicoot_core_save_coords_request(imol));
      });
   };
   d->CentreOnParent();
   d->Show();
}

void save_state_action(wxWindow *parent) {
   // a bare name (Coot's default) is in the working directory
   wxFileName name(wxString::FromUTF8(bandicoot_core_save_state_name()));
   name.MakeAbsolute();
   FileRequest r;
   r.title = "Select File-name for State File";
   r.directory = name.GetPath();
   r.file_name = name.GetFullName();
   wxString path = save_file(parent, r);
   if (path.empty()) return;
   bandicoot_core_save_state(path.utf8_string());
}

wxWindow *owner = nullptr;

// The File menu's items, by the GTK build's handler names; the toolbar's
// Open Coords, Auto-open MTZ and Open Map share the first three.
const std::map<std::string, std::function<void(wxWindow *)>> &file_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "on_coords_toolbutton_clicked",    open_coordinates_action },
      { "on_open_coordinates1_activate",   open_coordinates_action },
      { "bandicoot_action_auto_open_mtz",  auto_open_mtz_action },
      { "on_auto_open_mtz_activate",       auto_open_mtz_action },
      { "bandicoot_action_open_map",       open_map_action },
      { "on_open_map1_activate",           open_map_action },
      { "on_open_dataset1_activate",       open_dataset_action },
      { "on_import_cif_dictionary1_activate", import_dictionary_action },
      { "on_get_monomer1_activate",        [](wxWindow *p) { show_get_monomer(p); } },
      { "on_search_monomer_library1_activate", [](wxWindow *p) { show_monomer_search(p); } },
      { "on_get_pdb_using_code1_activate", [](wxWindow *p) {
           show_accession_code(p, int(bandicoot_accession_kind::pdb)); } },
      { "on_get_pdb_and_map_using_eds1_activate", [](wxWindow *p) {
           show_accession_code(p, int(bandicoot_accession_kind::pdb_and_map_eds)); } },
      { "on_fetch_pdb_and_map_using_pdbredo1_activate", [](wxWindow *p) {
           show_accession_code(p, int(bandicoot_accession_kind::pdb_and_map_pdb_redo)); } },
      { "on_fetch_alphafold_model_using_uniprot_id1_activate", [](wxWindow *p) {
           show_accession_code(p, int(bandicoot_accession_kind::alphafold_uniprot)); } },
      { "on_fetch_corresponding_alphafold_model_and_superpose1_activate", [](wxWindow *) {
           wxBusyCursor busy;
           bandicoot_core_fetch_and_superpose_alphafold(); } },
      { "on_save_coordinates1_activate",   save_coordinates_action },
      { "on_save_symmetry_coordinates1_activate", [](wxWindow *) {
           bandicoot_core_pick_symmetry_save(); } },
      { "on_save_state1_activate",         save_state_action },
      { "on_recover_session1_activate",    [](wxWindow *p) { show_recover_session(p); } },
      { "on_export_map1_activate",         [](wxWindow *p) { show_export_map(p, false); } },
      { "on_export_map_fragment1_activate", [](wxWindow *p) { show_export_map(p, true); } },
      { "on_close_molecule1_activate",     [](wxWindow *p) { show_delete_molecules(p); } },
      // closing the main window asks about unsaved changes first
      { "on_exit1_activate",               [](wxWindow *p) { wxGetTopLevelParent(p)->Close(); } },
   };
   return actions;
}

} // namespace

bool is_file_action(const std::string &action) {
   return file_actions().count(action) > 0;
}

bool run_file_action(wxWindow *parent, const std::string &action) {
   auto it = file_actions().find(action);
   if (it == file_actions().end()) return false;
   it->second(parent);
   return true;
}

void open_dataset_file(wxWindow *parent, const std::string &file) {
   open_chosen_dataset(parent, file);
}

void install_file_actions(wxWindow *parent) {
   owner = parent;
   bandicoot_save_view v;
   // after the click that picked the atom has been handled
   v.symmetry_picked = [](const bandicoot_save_coords &request) {
      owner->CallAfter([request]() { save_coordinates_panel(owner, request); });
   };
   bandicoot_core_set_save_view(v);
}

} // namespace wxui

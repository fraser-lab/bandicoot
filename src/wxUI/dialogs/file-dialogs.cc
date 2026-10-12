// src/wxUI/dialogs/file-dialogs.cc

#include "wxUI/dialogs/file-dialogs.hh"

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filedlgcustomize.h>
#include <wx/filename.h>

namespace wxui {

namespace {

// Adds the request's check boxes and drop-downs to the panel, and reads their
// values back when it closes with a file.
class ExtrasHook : public wxFileDialogCustomizeHook {
public:
   explicit ExtrasHook(FileRequest &r) : request(r) {}

   void AddCustomControls(wxFileDialogCustomize &customizer) override {
      for (const CheckOption &c : request.checks) {
         wxFileDialogCheckBox *box = customizer.AddCheckBox(c.label);
         box->SetValue(c.value);
         boxes.push_back(box);
      }
      for (const ChoiceOption &c : request.choices) {
         customizer.AddStaticText(c.label);
         wxFileDialogChoice *ch = customizer.AddChoice(c.items.size(), c.items.data());
         ch->SetSelection(c.value);
         menus.push_back(ch);
      }
   }

   void TransferDataFromCustomControls() override {
      for (size_t i = 0; i < boxes.size(); i++)
         request.checks[i].value = boxes[i]->GetValue();
      for (size_t i = 0; i < menus.size(); i++)
         request.choices[i].value = menus[i]->GetSelection();
   }

private:
   FileRequest &request;
   std::vector<wxFileDialogCheckBox *> boxes;
   std::vector<wxFileDialogChoice *> menus;
};

wxString patterns_for(const FileType &t) {
   wxString p;
   for (const wxString &e : t.extensions) {
      if (! p.empty()) p += ";";
      p += "*" + e;
   }
   return p.empty() ? wxString("*") : p;
}

void remember_directory(FileRequest &request, const wxString &path) {
   request.directory = wxFileName(path).GetPath();
}

} // namespace

wxString wildcard_for(const std::vector<FileType> &types, bool add_all_files) {
   wxString w;
   for (const FileType &t : types) {
      wxString p = patterns_for(t);
      if (! w.empty()) w += "|";
      w += t.description + " (" + p + ")|" + p;
   }
   if (add_all_files || types.empty()) {
      if (! w.empty()) w += "|";
      w += "All files|*";
   }
   return w;
}

std::vector<wxString> open_files(wxWindow *parent, FileRequest &request) {

   long style = wxFD_OPEN | wxFD_FILE_MUST_EXIST | (request.multiple ? wxFD_MULTIPLE : 0);
   wxFileDialog d(parent, request.title, request.directory, wxEmptyString,
                  wildcard_for(request.types, true), style);
   d.SetFilterIndex(request.type_index);
   ExtrasHook hook(request);
   if (! request.checks.empty() || ! request.choices.empty())
      d.SetCustomizeHook(hook);

   std::vector<wxString> paths;
   if (d.ShowModal() != wxID_OK) return paths;
   wxArrayString chosen;
   if (request.multiple) d.GetPaths(chosen); else chosen.Add(d.GetPath());
   for (const wxString &p : chosen) paths.push_back(p);
   request.type_index = d.GetFilterIndex();
   if (! paths.empty()) remember_directory(request, paths.front());
   return paths;
}

wxString save_file(wxWindow *parent, FileRequest &request) {

   wxFileDialog d(parent, request.title, request.directory, request.file_name,
                  wildcard_for(request.types, false), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
   d.SetFilterIndex(request.type_index);
   ExtrasHook hook(request);
   if (! request.checks.empty() || ! request.choices.empty())
      d.SetCustomizeHook(hook);

   if (d.ShowModal() != wxID_OK) return wxEmptyString;
   wxString path = d.GetPath();
   request.type_index = d.GetFilterIndex();
   remember_directory(request, path);
   return path;
}

wxString choose_folder(wxWindow *parent, const wxString &title, const wxString &start) {
   wxDirDialog d(parent, title, start, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
   return d.ShowModal() == wxID_OK ? d.GetPath() : wxString();
}

// ------------------------------------------------------------------------

bool open_coordinates(wxWindow *parent, const std::vector<FileType> &types,
                      wxString &directory, Recentre initial, CoordinatesChoice &out) {
   FileRequest r;
   r.title = "Select Coordinates File";
   r.types = types;
   r.directory = directory;
   r.choices.push_back({ "Recentre?",
                         { "Recentre on Molecule", "Don't Recentre", "Recentre Molecule Here" },
                         int(initial) });
   std::vector<wxString> paths = open_files(parent, r);
   if (paths.empty()) return false;
   directory = r.directory;
   out.path = paths.front();
   out.recentre = Recentre(r.choices[0].value);
   return true;
}

bool open_map(wxWindow *parent, const std::vector<FileType> &types,
              wxString &directory, MapChoice &out) {
   FileRequest r;
   r.title = "Select Map...";
   r.types = types;
   r.directory = directory;
   r.checks.push_back({ "Is Difference Map", false });
   std::vector<wxString> paths = open_files(parent, r);
   if (paths.empty()) return false;
   directory = r.directory;
   out.path = paths.front();
   out.is_difference_map = r.checks[0].value;
   return true;
}

bool save_coordinates(wxWindow *parent, const std::vector<FileType> &formats,
                      wxString &directory, const wxString &suggested_name,
                      SaveCoordinatesChoice &inout) {
   FileRequest r;
   r.title = "Select Filename for Saved Coordinates";
   r.types = formats;
   r.type_index = inout.format;
   r.directory = directory;
   r.file_name = suggested_name;
   r.checks.push_back({ "Save Hydrogens", inout.save_hydrogens });
   r.checks.push_back({ "Save ANISO Records", inout.save_aniso });
   wxString path = save_file(parent, r);
   if (path.empty()) return false;
   directory = r.directory;
   inout.path = path;
   inout.format = r.type_index;
   inout.save_hydrogens = r.checks[0].value;
   inout.save_aniso = r.checks[1].value;
   return true;
}

} // namespace wxui

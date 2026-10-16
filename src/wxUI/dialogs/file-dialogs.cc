// src/wxUI/dialogs/file-dialogs.cc

#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/file-panel-size.hh"

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/filename.h>

namespace wxui {

namespace {

// The request's check boxes and drop-downs, one to a row and aligned with
// each other on the right, below the file list. They keep their own width:
// one that follows the panel's would also become the panel's least width. The panel is made through a plain
// function pointer, so the request being shown is held here while it is up.
FileRequest *shown_request = nullptr;

class ExtrasPanel : public wxPanel {
public:
   explicit ExtrasPanel(wxWindow *parent) : wxPanel(parent) {
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      for (const CheckOption &c : shown_request->checks) {
         wxCheckBox *box = new wxCheckBox(this, wxID_ANY, c.label);
         box->SetValue(c.value);
         boxes.push_back(box);
         col->Add(box, 0, wxALIGN_RIGHT | wxTOP, 4);
      }
      for (const ChoiceOption &c : shown_request->choices) {
         wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
         row->Add(new wxStaticText(this, wxID_ANY, c.label), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
         wxChoice *ch = new wxChoice(this, wxID_ANY);
         for (const wxString &item : c.items) ch->Append(item);
         ch->SetSelection(c.value);
         menus.push_back(ch);
         row->Add(ch, 0, wxALIGN_CENTER_VERTICAL);
         col->Add(row, 0, wxALIGN_RIGHT | wxTOP, 4);
      }
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 12);
      SetSizer(outer);
      Fit();
   }

   void read_back(FileRequest &request) const {
      for (size_t i = 0; i < boxes.size(); i++) request.checks[i].value = boxes[i]->GetValue();
      for (size_t i = 0; i < menus.size(); i++) request.choices[i].value = menus[i]->GetSelection();
   }

private:
   std::vector<wxCheckBox *> boxes;
   std::vector<wxChoice *> menus;
};

wxWindow *make_extras(wxWindow *parent) {
   return new ExtrasPanel(parent);
}

bool has_extras(const FileRequest &r) {
   return ! r.checks.empty() || ! r.choices.empty();
}

// Shows d with the request's extras, reading them back if a file was chosen.
int show_with_extras(wxFileDialog &d, FileRequest &request) {
   if (has_extras(request)) {
      shown_request = &request;
      d.SetExtraControlCreator(make_extras);
   }
   limit_file_panel_size();
   int result = d.ShowModal();
   if (has_extras(request)) {
      if (result == wxID_OK)
         if (ExtrasPanel *extras = dynamic_cast<ExtrasPanel *>(d.GetExtraControl()))
            extras->read_back(request);
      shown_request = nullptr;
   }
   return result;
}

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

   std::vector<wxString> paths;
   if (show_with_extras(d, request) != wxID_OK) return paths;
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

   if (show_with_extras(d, request) != wxID_OK) return wxEmptyString;
   wxString path = d.GetPath();
   request.type_index = d.GetFilterIndex();
   remember_directory(request, path);
   return path;
}

wxString choose_folder(wxWindow *parent, const wxString &title, const wxString &start) {
   wxDirDialog d(parent, title, start, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
   limit_file_panel_size();
   return d.ShowModal() == wxID_OK ? d.GetPath() : wxString();
}

// ------------------------------------------------------------------------

bool open_coordinates(wxWindow *parent, const std::vector<FileType> &types,
                      wxString &directory, Recentre initial, CoordinatesChoice &out) {
   FileRequest r;
   r.title = "Select Coordinates File";
   r.types = types;
   r.directory = directory;
   r.multiple = true;
   r.choices.push_back({ "Recentre?",
                         { "Recentre on Molecule", "Don't Recentre", "Recentre Molecule Here" },
                         int(initial) });
   std::vector<wxString> paths = open_files(parent, r);
   if (paths.empty()) return false;
   directory = r.directory;
   out.paths = paths;
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

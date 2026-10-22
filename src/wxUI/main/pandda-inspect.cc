// src/wxUI/main/pandda-inspect.cc

#include "wxUI/main/pandda-inspect.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/file-dialogs.hh"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <functional>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

// The driver's string arguments go in single quotes.
std::string quoted(const wxString &s) {
   std::string e;
   for (char c : s.utf8_string()) {
      if (c == '\\' || c == '\'') e += '\\';
      e += c;
   }
   return "'" + e + "'";
}

// Fields the driver joins with '|' (or lines with '\n').
std::vector<std::string> split(const std::string &s, char sep) {
   std::vector<std::string> v;
   size_t start = 0;
   while (start <= s.size()) {
      size_t at = s.find(sep, start);
      v.push_back(s.substr(start, at == std::string::npos ? std::string::npos : at - start));
      if (at == std::string::npos) break;
      start = at + 1;
   }
   return v;
}

// The driver's empty value reads "None".
wxString text_of(const std::string &s) {
   return s == "None" ? wxString() : wxString::FromUTF8(s);
}

const char *selections[] = {
   "All events", "Sort by Z-score", "Sort by cluster size", "Sort alphabetically",
   "Unviewed only", "Interesting only", "Modelled only", "Not modelled only" };

// Basic's confidence list, in the driver's order; Full's three radios use
// three of them.
const char *confidences[] = {
   "unassigned", "no ligand bound", "unknown ligand", "low confidence", "high confidence" };

class PanddaInspect : public wxFrame {
public:
   explicit PanddaInspect(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "PanDDA Inspect") {
      SetBackgroundColour(palette::face());
      basic = bandicoot_core_pandda_basic_ui();
      bandicoot_core_pandda_opened();
      body = new wxPanel(this);
      body->SetBackgroundColour(palette::face());
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(body, 1, wxEXPAND | wxALL, 8);
      SetSizer(outer);
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
      rebuild();
   }

   ~PanddaInspect() override {
      bandicoot_core_pandda_closed();
      shown = nullptr;
   }

   static PanddaInspect *shown;

   void load_folder(const wxString &path) {
      std::string s = run("set_folder(" + quoted(path) + ")");
      bool loaded = ! s.empty() && s.compare(0, 8, "PanDDA: ") != 0;
      set_loaded(loaded);
      if (loaded) {
         if (selection) selection->SetSelection(0);
         refresh();
         fill_datasets();
      }
   }

private:
   bool basic;
   wxPanel *body;
   wxStaticText *status = nullptr;
   std::vector<wxWindow *> needs_folder;   // usable once a folder is loaded

   // shared
   wxChoice *selection = nullptr;
   wxComboBox *go_to = nullptr;
   wxTextCtrl *comment = nullptr;
   // Basic
   std::vector<wxStaticText *> basic_info;
   wxCheckBox *map_checks[4] = { nullptr, nullptr, nullptr, nullptr };
   wxCheckBox *interesting = nullptr;
   wxChoice *confidence = nullptr;
   // Full
   std::vector<wxStaticText *> full_info;
   wxStaticText *progress[4] = { nullptr, nullptr, nullptr, nullptr };
   wxRadioButton *interesting_yes = nullptr, *interesting_no = nullptr;
   wxRadioButton *placed_yes = nullptr, *placed_no = nullptr;
   wxRadioButton *conf_high = nullptr, *conf_med = nullptr, *conf_low = nullptr;
   wxTextCtrl *site_name = nullptr, *site_comment = nullptr;

   // A driver call whose returned status goes to the status line.
   std::string run(const std::string &call) {
      std::string s = bandicoot_core_pandda_call(call);
      if (status && ! s.empty()) status->SetLabel(wxString::FromUTF8(s));
      return s;
   }

   // A call that moves to another event: then show that event.
   void move(const std::string &call) {
      run(call);
      refresh();
   }

   void set_loaded(bool on) {
      for (wxWindow *w : needs_folder) w->Enable(on);
   }

   // ---- building blocks

   BevelButton *button(wxWindow *parent, const wxString &label, std::function<void()> action,
                       bool needs_loaded = true) {
      BevelButton *b = new BevelButton(parent, wxID_ANY, label);
      b->Bind(wxEVT_BUTTON, [action](wxCommandEvent &) { action(); });
      if (needs_loaded) needs_folder.push_back(b);
      return b;
   }

   wxStaticBoxSizer *frame(wxWindow *parent, const wxString &title) {
      return new wxStaticBoxSizer(wxVERTICAL, parent, title);
   }

   // A text entry that saves its value with call once it is finished with.
   wxTextCtrl *annotation_entry(wxWindow *parent, const std::string &call) {
      wxTextCtrl *t = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                     wxTE_PROCESS_ENTER);
      auto commit = [this, t, call]() { run(call + "(" + quoted(t->GetValue()) + ")"); };
      t->Bind(wxEVT_TEXT_ENTER, [commit](wxCommandEvent &) { commit(); });
      t->Bind(wxEVT_KILL_FOCUS, [t, commit](wxFocusEvent &e) {
         if (! t->IsBeingDeleted()) commit();
         e.Skip();
      });
      needs_folder.push_back(t);
      return t;
   }

   wxChoice *make_selection(wxWindow *parent) {
      wxChoice *c = new wxChoice(parent, wxID_ANY);
      for (const char *s : selections) c->Append(s);
      c->SetSelection(0);
      c->Bind(wxEVT_CHOICE, [this, c](wxCommandEvent &) {
         run("set_selection(" + quoted(c->GetStringSelection()) + ")");
         refresh();
         fill_datasets();
      });
      needs_folder.push_back(c);
      return c;
   }

   // Picking a dataset from the list goes there; a typed one goes on Return or Go.
   wxSizer *make_go_to(wxWindow *parent) {
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      go_to = new wxComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, -1),
                             0, nullptr, wxTE_PROCESS_ENTER);
      auto go = [this]() {
         wxString d = go_to->GetValue().Trim().Trim(false);
         if (! d.empty()) move("go_to_dataset(" + quoted(d) + ")");
      };
      go_to->Bind(wxEVT_COMBOBOX, [go](wxCommandEvent &) { go(); });
      go_to->Bind(wxEVT_TEXT_ENTER, [go](wxCommandEvent &) { go(); });
      row->Add(go_to, 1, wxALIGN_CENTER_VERTICAL);
      row->Add(button(parent, "Go", go), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
      needs_folder.push_back(go_to);
      return row;
   }

   void fill_datasets() {
      if (! go_to) return;
      wxString typed = go_to->GetValue();
      go_to->Clear();
      for (const std::string &d : split(run_quiet("dataset_list()"), '\n'))
         if (! d.empty()) go_to->Append(wxString::FromUTF8(d));
      go_to->ChangeValue(typed);
   }

   std::string run_quiet(const std::string &call) { return bandicoot_core_pandda_call(call); }

   void open_folder() {
      wxString path = choose_folder(this, "Select PanDDA folder", wxEmptyString);
      if (! path.empty()) load_folder(path);
   }

   void load_ligand() {
      FileRequest request;
      request.title = "Load ligand coordinates";
      request.types = { { "Ligand coords (pdb, cif, ent, mol)", { ".pdb", ".ent", ".cif", ".mmcif", ".mol", ".mol2" } } };
      std::vector<wxString> paths = open_files(this, request);
      if (! paths.empty()) run("load_ligand_file(" + quoted(paths.front()) + ")");
   }

   void ligand_from_smiles() {
      wxDialog d(this, wxID_ANY, "Ligand from SMILES");
      d.SetBackgroundColour(palette::face());
      wxFlexGridSizer *grid = new wxFlexGridSizer(2, 6, 6);
      grid->AddGrowableCol(1);
      wxTextCtrl *code = new wxTextCtrl(&d, wxID_ANY, "LIG");
      code->SetMaxLength(3);
      wxTextCtrl *smiles = new wxTextCtrl(&d, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(320, -1),
                                          wxTE_PROCESS_ENTER);
      grid->Add(new wxStaticText(&d, wxID_ANY, "3-letter code:"), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(code, 1, wxEXPAND);
      grid->Add(new wxStaticText(&d, wxID_ANY, "SMILES:"), 0, wxALIGN_CENTER_VERTICAL);
      grid->Add(smiles, 1, wxEXPAND);
      smiles->Bind(wxEVT_TEXT_ENTER, [&d](wxCommandEvent &) { d.EndModal(wxID_OK); });
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(grid, 0, wxEXPAND | wxBOTTOM, 10);
      col->Add(d.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND);
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(col, 1, wxEXPAND | wxALL, 10);
      d.SetSizerAndFit(outer);
      d.CentreOnParent();
      smiles->SetFocus();
      if (d.ShowModal() == wxID_OK)
         run("ligand_from_smiles(" + quoted(smiles->GetValue()) + ", " + quoted(code->GetValue()) + ")");
   }

   // Summary opens the driver's HTML page in the browser.
   void summary() {
      std::string p = run_quiet("summary_html()");
      if (! p.empty() && p.compare(0, 8, "PanDDA: ") != 0) wxLaunchDefaultApplication(wxString::FromUTF8(p));
      if (status && ! p.empty()) status->SetLabel(wxString::FromUTF8(p));
   }

   // ---- the two layouts

   void rebuild() {
      body->DestroyChildren();
      needs_folder.clear();
      status = nullptr;
      selection = nullptr;
      go_to = nullptr;
      comment = nullptr;
      basic_info.clear();
      full_info.clear();
      for (auto &c : map_checks) c = nullptr;
      for (auto &p : progress) p = nullptr;
      interesting = nullptr;
      confidence = nullptr;
      interesting_yes = interesting_no = placed_yes = placed_no = nullptr;
      conf_high = conf_med = conf_low = nullptr;
      site_name = site_comment = nullptr;

      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      if (basic) build_basic(col);
      else       build_full(col);
      build_bottom(col);
      body->SetSizer(col, true);
      GetSizer()->SetSizeHints(this);
      Layout();

      bool loaded = run_quiet("is_loaded()") == "True";
      set_loaded(loaded);
      if (loaded) {
         refresh();
         fill_datasets();
         std::string s = run_quiet("status()");
         if (! s.empty()) status->SetLabel(wxString::FromUTF8(s));
      }
   }

   void build_basic(wxBoxSizer *col) {
      wxBoxSizer *top = new wxBoxSizer(wxHORIZONTAL);
      wxStaticBoxSizer *info = frame(body, "Dataset Info");
      wxFlexGridSizer *info_grid = new wxFlexGridSizer(2, 2, 8);
      for (const char *t : { "Crystal", "Resolution", "Rwork", "Rfree", "Event", "Site", "BDC" }) {
         info_grid->Add(new wxStaticText(info->GetStaticBox(), wxID_ANY, t));
         wxStaticText *v = new wxStaticText(info->GetStaticBox(), wxID_ANY, wxEmptyString);
         info_grid->Add(v);
         basic_info.push_back(v);
      }
      info->Add(info_grid, 0, wxALL, 4);
      top->Add(info, 0, wxEXPAND | wxRIGHT, 6);

      wxBoxSizer *controls = new wxBoxSizer(wxVERTICAL);
      controls->Add(button(body, "Quit", [this]() { Close(); }, false), 0, wxEXPAND | wxBOTTOM, 4);
      controls->Add(button(body, "Select PanDDA folder...", [this]() { open_folder(); }, false), 0,
                    wxEXPAND | wxBOTTOM, 4);
      wxBoxSizer *sel_row = new wxBoxSizer(wxHORIZONTAL);
      sel_row->Add(new wxStaticText(body, wxID_ANY, "Event Selection"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      selection = make_selection(body);
      sel_row->Add(selection, 1);
      controls->Add(sel_row, 0, wxEXPAND | wxBOTTOM, 4);
      wxBoxSizer *go_row = new wxBoxSizer(wxHORIZONTAL);
      go_row->Add(new wxStaticText(body, wxID_ANY, "Go to Dataset"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      go_row->Add(make_go_to(body), 1);
      controls->Add(go_row, 0, wxEXPAND);
      top->Add(controls, 1);
      col->Add(top, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *nav = frame(body, "Navigator");
      wxWindow *nb = nav->GetStaticBox();
      wxGridSizer *nav_grid = new wxGridSizer(2, 4, 4);
      nav_grid->Add(button(nb, "< Prev event", [this]() { move("prev_event()"); }), 0, wxEXPAND);
      nav_grid->Add(button(nb, "Next event >", [this]() { move("next_event()"); }), 0, wxEXPAND);
      nav_grid->Add(button(nb, "< Prev site", [this]() { move("prev_site()"); }), 0, wxEXPAND);
      nav_grid->Add(button(nb, "Next site >", [this]() { move("next_site()"); }), 0, wxEXPAND);
      nav->Add(nav_grid, 0, wxEXPAND | wxALL, 2);
      col->Add(nav, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *maps = frame(body, "Toggle Maps");
      wxWindow *mb = maps->GetStaticBox();
      wxGridSizer *map_grid = new wxGridSizer(4, 0, 8);
      const char *map_labels[] = { "Event map", "Z-map", "(2)Fo-Fc", "Average" };
      const char *map_calls[] = { "set_show_emap", "set_show_zmap", "set_show_xray", "set_show_average" };
      for (int i = 0; i < 4; i++) {
         wxCheckBox *c = new wxCheckBox(mb, wxID_ANY, map_labels[i]);
         std::string call = map_calls[i];
         c->Bind(wxEVT_CHECKBOX, [this, c, call](wxCommandEvent &) {
            run(call + (c->IsChecked() ? "(True)" : "(False)"));
         });
         map_grid->Add(c);
         map_checks[i] = c;
         needs_folder.push_back(c);
      }
      map_checks[0]->SetValue(true);
      map_checks[1]->SetValue(true);
      maps->Add(map_grid, 0, wxEXPAND | wxALL, 2);
      col->Add(maps, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *ligand = frame(body, "Ligand Modeling");
      wxWindow *lb = ligand->GetStaticBox();
      wxGridSizer *lig_grid = new wxGridSizer(2, 4, 4);
      lig_grid->Add(button(lb, "Load Ligand...", [this]() { load_ligand(); }), 0, wxEXPAND);
      lig_grid->Add(button(lb, "Ligand from SMILES...", [this]() { ligand_from_smiles(); }), 0, wxEXPAND);
      lig_grid->Add(button(lb, "Place Ligand", [this]() { run("place_ligand()"); }), 0, wxEXPAND);
      lig_grid->Add(button(lb, "Merge Ligand", [this]() { run("merge_ligand()"); }), 0, wxEXPAND);
      ligand->Add(lig_grid, 0, wxEXPAND | wxALL, 2);
      col->Add(ligand, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *anno = frame(body, "Annotation");
      wxWindow *ab = anno->GetStaticBox();
      wxBoxSizer *anno_row = new wxBoxSizer(wxHORIZONTAL);
      interesting = new wxCheckBox(ab, wxID_ANY, "Interesting");
      interesting->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
         run(interesting->IsChecked() ? "set_interesting(True)" : "set_interesting(False)");
      });
      needs_folder.push_back(interesting);
      confidence = new wxChoice(ab, wxID_ANY);
      for (const char *c : confidences) confidence->Append(c);
      confidence->SetSelection(0);
      confidence->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
         run("set_confidence(" + quoted(confidence->GetStringSelection()) + ")");
      });
      needs_folder.push_back(confidence);
      anno_row->Add(interesting, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      anno_row->Add(new wxStaticText(ab, wxID_ANY, "Confidence:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      anno_row->Add(confidence, 1);
      anno->Add(anno_row, 0, wxEXPAND | wxALL, 2);
      wxBoxSizer *comment_row = new wxBoxSizer(wxHORIZONTAL);
      comment_row->Add(new wxStaticText(ab, wxID_ANY, "Comment:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      comment = annotation_entry(ab, "set_comment");
      comment_row->Add(comment, 1);
      anno->Add(comment_row, 0, wxEXPAND | wxALL, 2);
      col->Add(anno, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *model = frame(body, wxEmptyString);
      wxWindow *sb = model->GetStaticBox();
      wxGridSizer *model_grid = new wxGridSizer(3, 4, 4);
      model_grid->Add(button(sb, "Save Model", [this]() { run("save_model()"); }), 0, wxEXPAND);
      model_grid->Add(button(sb, "Reload Last Saved Model", [this]() { run("reload_saved_model()"); }), 0, wxEXPAND);
      model_grid->Add(button(sb, "Reset to Unfitted Model", [this]() { run("reset_to_unfitted()"); }), 0, wxEXPAND);
      model->Add(model_grid, 0, wxEXPAND | wxALL, 2);
      col->Add(model, 0, wxEXPAND | wxBOTTOM, 4);
   }

   void build_full(wxBoxSizer *col) {
      wxBoxSizer *top = new wxBoxSizer(wxHORIZONTAL);

      wxStaticBoxSizer *file_box = frame(body, wxEmptyString);
      wxWindow *fb = file_box->GetStaticBox();
      file_box->Add(button(fb, "Quit", [this]() { Close(); }, false), 0, wxEXPAND | wxALL, 2);
      file_box->Add(button(fb, "Summary", [this]() { summary(); }, false), 0, wxEXPAND | wxALL, 2);
      file_box->Add(button(fb, "Update HTML", [this]() { run("write_html()"); }, false), 0, wxEXPAND | wxALL, 2);
      top->Add(file_box, 0, wxEXPAND | wxRIGHT, 6);

      wxStaticBoxSizer *prog = frame(body, wxEmptyString);
      wxWindow *pb = prog->GetStaticBox();
      prog->Add(new wxStaticText(pb, wxID_ANY, "Overall Inspection of\nEvent and Site Progress",
                                 wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL),
                0, wxALIGN_CENTER | wxALL, 4);
      for (int r = 0; r < 2; r++) {
         wxGridSizer *row = new wxGridSizer(4, 0, 6);
         row->Add(new wxStaticText(pb, wxID_ANY, r == 0 ? "Event" : "Site"));
         progress[2 * r] = new wxStaticText(pb, wxID_ANY, "0");
         row->Add(progress[2 * r]);
         row->Add(new wxStaticText(pb, wxID_ANY, "of"));
         progress[2 * r + 1] = new wxStaticText(pb, wxID_ANY, "0");
         row->Add(progress[2 * r + 1]);
         prog->Add(row, 0, wxEXPAND | wxALL, 4);
      }
      top->Add(prog, 0, wxEXPAND | wxRIGHT, 6);

      wxBoxSizer *right = new wxBoxSizer(wxVERTICAL);
      wxBoxSizer *pick = new wxBoxSizer(wxHORIZONTAL);
      wxBoxSizer *sel_col = new wxBoxSizer(wxVERTICAL);
      sel_col->Add(new wxStaticText(body, wxID_ANY, "Event Selection"), 0, wxBOTTOM, 2);
      selection = make_selection(body);
      sel_col->Add(selection, 0, wxEXPAND);
      pick->Add(sel_col, 1, wxRIGHT, 8);
      wxBoxSizer *go_col = new wxBoxSizer(wxVERTICAL);
      go_col->Add(new wxStaticText(body, wxID_ANY, "Go to Dataset"), 0, wxBOTTOM, 2);
      go_col->Add(make_go_to(body), 0, wxEXPAND);
      pick->Add(go_col, 1);
      right->Add(pick, 0, wxEXPAND | wxBOTTOM, 4);
      wxGridSizer *site_nav = new wxGridSizer(2, 2, 4, 4);
      site_nav->Add(button(body, "<<< Go to Prev Site <<<", [this]() { move("prev_site()"); }), 0, wxEXPAND);
      site_nav->Add(button(body, ">>> Go to Next Site >>>", [this]() { move("next_site()"); }), 0, wxEXPAND);
      site_nav->Add(button(body, ">>> Go to Next Unviewed >>>", [this]() { move("next_unviewed()"); }), 0, wxEXPAND);
      site_nav->Add(button(body, ">>> Go to Next Modelled >>>", [this]() { move("next_modelled()"); }), 0, wxEXPAND);
      right->Add(site_nav, 1, wxEXPAND);
      top->Add(right, 1, wxEXPAND);
      col->Add(top, 0, wxEXPAND | wxBOTTOM, 4);

      wxGridSizer *event_nav = new wxGridSizer(3, 0, 4);
      event_nav->Add(button(body, "<<< Prev <<<\n(Don't Save Model)", [this]() { move("prev_event()"); }), 0, wxEXPAND);
      event_nav->Add(button(body, ">>> Next >>>\n(Don't Save Model)", [this]() { move("next_event()"); }), 0, wxEXPAND);
      event_nav->Add(button(body, ">>> Next >>>\n(Save Model)", [this]() {
         run("save_model()");
         move("next_event()");
      }), 0, wxEXPAND);
      col->Add(event_nav, 0, wxEXPAND | wxBOTTOM, 4);

      wxBoxSizer *mid = new wxBoxSizer(wxHORIZONTAL);
      wxBoxSizer *info_col = new wxBoxSizer(wxVERTICAL);
      const char *titles[] = { "Dataset ID", "Event #", "1-BDC", "Z-blob Peak", "Z-blob Size",
                               "Resolution", "Map Uncertainty", "R-Free", "R-Work" };
      full_info.assign(9, nullptr);
      wxStaticBoxSizer *id_box = frame(body, wxEmptyString);
      wxBoxSizer *id_row = new wxBoxSizer(wxHORIZONTAL);
      id_row->Add(new wxStaticText(id_box->GetStaticBox(), wxID_ANY, titles[0]), 0, wxRIGHT, 8);
      full_info[0] = new wxStaticText(id_box->GetStaticBox(), wxID_ANY, wxEmptyString);
      id_row->Add(full_info[0], 1);
      id_box->Add(id_row, 0, wxEXPAND | wxALL, 2);
      info_col->Add(id_box, 0, wxEXPAND | wxBOTTOM, 4);
      wxBoxSizer *two = new wxBoxSizer(wxHORIZONTAL);
      auto info_frame = [this, &titles](const wxString &title, int first) {
         wxStaticBoxSizer *box = frame(body, title);
         wxFlexGridSizer *grid = new wxFlexGridSizer(2, 2, 8);
         for (int i = first; i < first + 4; i++) {
            grid->Add(new wxStaticText(box->GetStaticBox(), wxID_ANY, titles[i]));
            full_info[i] = new wxStaticText(box->GetStaticBox(), wxID_ANY, wxEmptyString);
            grid->Add(full_info[i]);
         }
         box->Add(grid, 1, wxALL, 4);
         return box;
      };
      two->Add(info_frame("Event Information", 1), 1, wxEXPAND | wxRIGHT, 4);
      two->Add(info_frame("Dataset Information", 5), 1, wxEXPAND);
      info_col->Add(two, 1, wxEXPAND);
      mid->Add(info_col, 1, wxEXPAND | wxRIGHT, 6);

      wxGridSizer *model = new wxGridSizer(4, 2, 4, 4);
      model->Add(button(body, "Merge Ligand\nWith Model", [this]() { run("merge_ligand()"); }), 0, wxEXPAND);
      model->Add(button(body, "Save Model", [this]() { run("save_model()"); }), 0, wxEXPAND);
      model->Add(button(body, "Move New\nLigand Here", [this]() { run("place_ligand()"); }), 0, wxEXPAND);
      model->Add(button(body, "Reload Last\nSaved Model", [this]() { run("reload_saved_model()"); }), 0, wxEXPAND);
      model->Add(button(body, "Open Next\nLigand", [this]() { run("open_next_ligand()"); }), 0, wxEXPAND);
      model->Add(button(body, "Reset to\nUnfitted Model", [this]() { run("reset_to_unfitted()"); }), 0, wxEXPAND);
      model->Add(button(body, "Open Ligand Here\n(all isomers)", [this]() { run("open_ligand_here()"); }), 0, wxEXPAND);
      model->Add(button(body, "Load Unfitted Model\n(for comparison only)",
                        [this]() { run("load_unfitted_comparison()"); }), 0, wxEXPAND);
      mid->Add(model, 0, wxEXPAND);
      col->Add(mid, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *event = frame(body, "Record Event Information (this event only)");
      wxWindow *eb = event->GetStaticBox();
      wxBoxSizer *comment_row = new wxBoxSizer(wxHORIZONTAL);
      comment_row->Add(new wxStaticText(eb, wxID_ANY, "Event Comment:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      comment = annotation_entry(eb, "set_comment");
      comment_row->Add(comment, 1);
      event->Add(comment_row, 0, wxEXPAND | wxALL, 2);
      wxGridSizer *radios = new wxGridSizer(3, 0, 12);
      auto pair = [this, eb](const wxString &yes, const wxString &no, wxRadioButton *&y, wxRadioButton *&n,
                             const std::string &call) {
         wxBoxSizer *box = new wxBoxSizer(wxVERTICAL);
         y = new wxRadioButton(eb, wxID_ANY, yes, wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
         n = new wxRadioButton(eb, wxID_ANY, no);
         wxRadioButton *yy = y;
         auto changed = [this, yy, call](wxCommandEvent &) { run(call + (yy->GetValue() ? "(True)" : "(False)")); };
         y->Bind(wxEVT_RADIOBUTTON, changed);
         n->Bind(wxEVT_RADIOBUTTON, changed);
         box->Add(y, 0, wxBOTTOM, 2);
         box->Add(n, 0);
         needs_folder.push_back(y);
         needs_folder.push_back(n);
         return box;
      };
      radios->Add(pair("Mark Event as Interesting", "Mark Event as Not Interesting",
                       interesting_yes, interesting_no, "set_interesting"));
      radios->Add(pair("Ligand Placed", "No Ligand Placed", placed_yes, placed_no, "set_placed"));
      wxBoxSizer *conf = new wxBoxSizer(wxVERTICAL);
      conf_high = new wxRadioButton(eb, wxID_ANY, "High Confidence", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      conf_med = new wxRadioButton(eb, wxID_ANY, "Medium Confidence");
      conf_low = new wxRadioButton(eb, wxID_ANY, "Low Confidence");
      // three radios over the driver's five values, as Coot's pandda.inspect has
      const std::pair<wxRadioButton *, const char *> tokens[] = {
         { conf_high, "high confidence" }, { conf_med, "unknown ligand" }, { conf_low, "low confidence" } };
      for (const auto &t : tokens) {
         std::string tok = t.second;
         t.first->Bind(wxEVT_RADIOBUTTON, [this, tok](wxCommandEvent &) { run("set_confidence('" + tok + "')"); });
         conf->Add(t.first, 0, wxBOTTOM, 2);
         needs_folder.push_back(t.first);
      }
      radios->Add(conf);
      event->Add(radios, 0, wxEXPAND | wxALL, 4);
      col->Add(event, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *site = frame(body, "Record Site Information (for all events with this site)");
      wxWindow *stb = site->GetStaticBox();
      wxFlexGridSizer *site_grid = new wxFlexGridSizer(2, 4, 6);
      site_grid->AddGrowableCol(1);
      site_grid->Add(new wxStaticText(stb, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
      site_name = annotation_entry(stb, "set_site_name");
      site_grid->Add(site_name, 1, wxEXPAND);
      site_grid->Add(new wxStaticText(stb, wxID_ANY, "Comment:"), 0, wxALIGN_CENTER_VERTICAL);
      site_comment = annotation_entry(stb, "set_site_comment");
      site_grid->Add(site_comment, 1, wxEXPAND);
      site->Add(site_grid, 0, wxEXPAND | wxALL, 2);
      col->Add(site, 0, wxEXPAND | wxBOTTOM, 4);

      wxStaticBoxSizer *misc = frame(body, "Miscellaneous");
      wxWindow *mb = misc->GetStaticBox();
      wxBoxSizer *misc_row = new wxBoxSizer(wxHORIZONTAL);
      misc_row->Add(button(mb, "Select PanDDA folder...", [this]() { open_folder(); }, false), 0, wxRIGHT, 4);
      misc_row->Add(button(mb, "Load input mtz file", [this]() { run("set_show_xray(True)"); }), 0, wxRIGHT, 4);
      misc_row->Add(button(mb, "Load average map", [this]() { run("set_show_average(True)"); }), 0);
      misc->Add(misc_row, 0, wxALIGN_CENTER | wxALL, 4);
      col->Add(misc, 0, wxEXPAND | wxBOTTOM, 4);
   }

   // Both layouts: the status line, then the layout's own drop-down.
   void build_bottom(wxBoxSizer *col) {
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      wxStaticBoxSizer *status_box = frame(body, wxEmptyString);
      status = new wxStaticText(status_box->GetStaticBox(), wxID_ANY, "Select a PanDDA folder to begin.");
      status_box->Add(status, 1, wxEXPAND | wxALL, 2);
      row->Add(status_box, 1, wxEXPAND | wxRIGHT, 6);
      row->Add(new wxStaticText(body, wxID_ANY, "UI Mode:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      wxChoice *mode = new wxChoice(body, wxID_ANY);
      mode->Append("Basic");
      mode->Append("Full");
      mode->SetSelection(basic ? 0 : 1);
      // the drop-down is part of what is rebuilt, so rebuild once its event is over
      mode->Bind(wxEVT_CHOICE, [this, mode](wxCommandEvent &) {
         bool want_basic = mode->GetSelection() == 0;
         if (want_basic == basic) return;
         basic = want_basic;
         CallAfter([this]() { rebuild(); });
      });
      row->Add(mode, 0, wxALIGN_CENTER_VERTICAL);
      col->Add(row, 0, wxEXPAND | wxTOP, 2);
   }

   // ---- the current event, as the driver has it

   void refresh() {
      bool is_interesting = run_quiet("get_interesting()") == "True";
      std::string conf = run_quiet("get_confidence()");
      if (interesting) interesting->SetValue(is_interesting);
      if (confidence) {
         int i = confidence->FindString(wxString::FromUTF8(conf));
         confidence->SetSelection(i == wxNOT_FOUND ? 0 : i);
      }
      if (comment) comment->ChangeValue(text_of(run_quiet("get_comment()")));

      // the dataset's code, in bold: it is the key to the event
      auto show_info = [](std::vector<wxStaticText *> &labels, const std::string &joined) {
         std::vector<std::string> fields = split(joined, '|');
         for (size_t i = 0; i < labels.size(); i++) {
            if (! labels[i]) continue;
            labels[i]->SetLabel(i < fields.size() ? wxString::FromUTF8(fields[i]) : wxString());
            if (i == 0) labels[i]->SetFont(labels[i]->GetFont().Bold());
         }
      };
      if (! basic_info.empty()) show_info(basic_info, run_quiet("get_info()"));

      if (map_checks[0]) {
         std::vector<std::string> shown = split(run_quiet("get_map_state()"), '|');
         for (int i = 0; i < 4; i++)
            map_checks[i]->SetValue(i < int(shown.size()) && shown[i] == "True");
      }

      if (interesting_yes) {
         interesting_yes->SetValue(is_interesting);
         interesting_no->SetValue(! is_interesting);
      }
      if (placed_yes) {
         bool placed = run_quiet("get_placed()") == "True";
         placed_yes->SetValue(placed);
         placed_no->SetValue(! placed);
      }
      if (conf_high) {
         wxRadioButton *r = conf == "high confidence" ? conf_high : conf == "low confidence" ? conf_low : conf_med;
         r->SetValue(true);
      }
      if (site_name) site_name->ChangeValue(text_of(run_quiet("get_site_name()")));
      if (site_comment) site_comment->ChangeValue(text_of(run_quiet("get_site_comment()")));
      if (progress[0]) {
         std::vector<std::string> p = split(run_quiet("progress()"), '|');
         for (int i = 0; i < 4; i++) progress[i]->SetLabel(i < int(p.size()) ? wxString::FromUTF8(p[i]) : "0");
      }
      if (! full_info.empty()) {
         show_info(full_info, run_quiet("get_info_inspect()"));
         // Go to Dataset starts on the event's own dataset
         if (go_to && full_info[0]) go_to->ChangeValue(full_info[0]->GetLabel());
      }
      body->Layout();
   }
};

PanddaInspect *PanddaInspect::shown = nullptr;

} // namespace

void show_pandda_inspect(wxWindow *parent, const std::string &folder) {
   if (! PanddaInspect::shown) {
      PanddaInspect *p = new PanddaInspect(parent);
      PanddaInspect::shown = p;
      p->CentreOnParent();
      p->Show();
   }
   PanddaInspect::shown->Raise();
   if (! folder.empty()) PanddaInspect::shown->load_folder(wxString::FromUTF8(folder));
}

void install_pandda_inspect(wxWindow *parent) {
   owner = parent;
   bandicoot_core_set_pandda_view([](const std::string &folder) { show_pandda_inspect(owner, folder); });
}

} // namespace wxui

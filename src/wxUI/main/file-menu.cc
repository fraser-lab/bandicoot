// src/wxUI/main/file-menu.cc

#include "wxUI/main/file-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/entry-row.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/mstream.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <string>
#include <vector>

namespace wxui {

namespace {

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   b->set_icon(icon(icon_file, wxSize(16, 16)));
   return b;
}

wxChoice *label_choice(wxWindow *parent, const std::vector<std::string> &labels, int selected) {
   wxChoice *c = new wxChoice(parent, wxID_ANY);
   for (const std::string &l : labels) c->Append(wxString::FromUTF8(l));
   if (! labels.empty()) c->SetSelection(std::clamp(selected, 0, int(labels.size()) - 1));
   return c;
}

std::string chosen(const wxChoice *c) {
   int i = c->GetSelection();
   return i < 0 ? std::string() : c->GetString(i).utf8_string();
}

// A label row: "Amplitudes  [choice]"
wxBoxSizer *labelled(wxWindow *parent, const wxString &label, wxWindow *control, wxStaticText **label_out = nullptr) {
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   wxStaticText *t = new wxStaticText(parent, wxID_ANY, label);
   row->Add(t, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   row->Add(control, 1, wxALIGN_CENTER_VERTICAL);
   if (label_out) *label_out = t;
   return row;
}

} // namespace

// ------------------------------------------------------------ Column Label Assignment

void show_column_labels(wxWindow *parent, const std::string &mtz_file_name) {

   bandicoot_mtz_columns cols;
   if (! bandicoot_core_mtz_columns(mtz_file_name, cols)) {
      error(parent, "Could not read the columns of", wxString::FromUTF8(mtz_file_name),
            "Column Label Assignment: no columns read from " + mtz_file_name);
      return;
   }

   wxDialog d(parent, wxID_ANY, "Column Label Assignment");
   d.SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   col->Add(new wxStaticText(&d, wxID_ANY, "Column Labels for Map Synthesis:"), 0,
            wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);

   TitledPanel *amp_frame = new TitledPanel(&d);
   wxChoice *amplitudes = label_choice(amp_frame->content(), cols.amplitudes, cols.preferred_amplitude);
   amp_frame->content_sizer()->Add(labelled(amp_frame->content(), "Amplitudes", amplitudes), 0, wxEXPAND);
   col->Add(amp_frame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   TitledPanel *phi_frame = new TitledPanel(&d);
   wxChoice *phases = label_choice(phi_frame->content(), cols.phases, cols.preferred_phase);
   phi_frame->content_sizer()->Add(labelled(phi_frame->content(), "Phases", phases), 0, wxEXPAND);
   col->Add(phi_frame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   TitledPanel *w_frame = new TitledPanel(&d);
   wxCheckBox *use_weights = new wxCheckBox(w_frame->content(), wxID_ANY, "Use Weights?");
   wxChoice *weights = label_choice(w_frame->content(), cols.weights, 0);
   wxStaticText *weights_label;
   w_frame->content_sizer()->Add(use_weights, 0, wxBOTTOM, 6);
   w_frame->content_sizer()->Add(labelled(w_frame->content(), "Weights", weights, &weights_label), 0, wxEXPAND);
   col->Add(w_frame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   TitledPanel *reso_frame = new TitledPanel(&d, "Resolution limits");
   wxWindow *rc = reso_frame->content();
   wxCheckBox *use_reso = new wxCheckBox(rc, wxID_ANY, "Use Resolution Limits");
   wxTextCtrl *low = new wxTextCtrl(rc, wxID_ANY);
   wxTextCtrl *high = new wxTextCtrl(rc, wxID_ANY);
   wxStaticText *low_label, *high_label;
   wxBoxSizer *reso_row = new wxBoxSizer(wxHORIZONTAL);
   reso_row->Add(labelled(rc, "Low:", low, &low_label), 1, wxRIGHT, 12);
   reso_row->Add(labelled(rc, "High:", high, &high_label), 1);
   reso_frame->content_sizer()->Add(use_reso, 0, wxBOTTOM, 6);
   reso_frame->content_sizer()->Add(reso_row, 0, wxEXPAND);
   col->Add(reso_frame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   wxCheckBox *is_diff = new wxCheckBox(&d, wxID_ANY, "Is a Difference Map");
   col->Add(is_diff, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
   col->Add(new wxStaticLine(&d), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

   wxCheckBox *assign = new wxCheckBox(&d, wxID_ANY, "Assign Labels for Structure Factor Calculation?");
   col->Add(assign, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
   TitledPanel *sf_frame = new TitledPanel(&d);
   wxWindow *sc = sf_frame->content();
   wxChoice *fobs = label_choice(sc, cols.fobs, 0);
   wxChoice *sigfobs = label_choice(sc, cols.sigfobs, 0);
   wxChoice *r_free = label_choice(sc, cols.r_free, 0);
   wxFlexGridSizer *sf_grid = new wxFlexGridSizer(2, 6, 8);
   sf_grid->AddGrowableCol(1);
   for (auto &p : std::vector<std::pair<const char *, wxChoice *>>{ { "Fobs", fobs }, { "Sig Fobs", sigfobs },
                                                                    { "R free", r_free } }) {
      sf_grid->Add(new wxStaticText(sc, wxID_ANY, p.first), 0, wxALIGN_CENTER_VERTICAL);
      sf_grid->Add(p.second, 1, wxEXPAND);
   }
   sf_frame->content_sizer()->Add(sf_grid, 0, wxEXPAND);
   col->Add(sf_frame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   BevelButton *ok = icon_button(&d, "OK", "stock-button-ok.png");
   BevelButton *cancel = icon_button(&d, "Cancel", "stock-button-cancel.png");
   for (BevelButton *b : { ok, cancel }) b->SetMinSize(wxSize(150, 32));
   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   buttons->Add(ok, 1, wxRIGHT, 12);
   buttons->Add(cancel, 1);
   col->Add(buttons, 0, wxEXPAND | wxALL, 8);

   // the parts each check box governs
   auto sync = [&]() {
      weights->Enable(use_weights->IsChecked());
      weights_label->Enable(use_weights->IsChecked());
      for (wxWindow *w : std::vector<wxWindow *>{ low, high, low_label, high_label })
         w->Enable(use_reso->IsChecked());
      sf_frame->Enable(assign->IsChecked());
   };
   for (wxCheckBox *c : { use_weights, use_reso, assign })
      c->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent &) { sync(); });
   // amplitudes named DEL... are a difference map's, as Coot reads them
   auto diff_from_label = [&]() {
      std::string l = chosen(amplitudes);
      std::string tail = l.substr(l.find_last_of('/') == std::string::npos ? 0 : l.find_last_of('/') + 1);
      is_diff->SetValue(tail.length() > 3 && tail.compare(0, 3, "DEL") == 0);
   };
   amplitudes->Bind(wxEVT_CHOICE, [&](wxCommandEvent &) { diff_from_label(); });
   diff_from_label();
   sync();

   bandicoot_map_from_mtz request;
   ok->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) {
      if (use_reso->IsChecked()) {
         double lo = 9999.9, hi;
         if (! low->GetValue().Trim().empty() && ! low->GetValue().ToCDouble(&lo)) {
            wxBell(); low->SetFocus(); return;
         }
         if (! high->GetValue().ToCDouble(&hi)) { wxBell(); high->SetFocus(); return; }
         request.use_resolution_limits = true;
         request.low_resolution = float(lo);
         request.high_resolution = float(hi);
      }
      d.EndModal(wxID_OK);
   });
   cancel->Bind(wxEVT_BUTTON, [&](wxCommandEvent &) { d.EndModal(wxID_CANCEL); });

   d.SetSizerAndFit(col);
   d.SetSize(wxSize(std::max(d.GetSize().x, 480), d.GetSize().y));
   d.CentreOnParent();
   if (d.ShowModal() != wxID_OK) return;

   request.file_name = mtz_file_name;
   request.amplitudes = chosen(amplitudes);
   request.phases = chosen(phases);
   request.use_weights = use_weights->IsChecked();
   request.weights = chosen(weights);
   request.is_difference_map = is_diff->IsChecked();
   if (assign->IsChecked()) {
      request.refmac_labels = true;
      request.fobs = chosen(fobs);
      request.sigfobs = chosen(sigfobs);
      request.r_free = chosen(r_free);
   }
   wxBusyCursor busy;
   bandicoot_core_make_map_from_mtz(request);
}

// ------------------------------------------------------------ Get Monomer

void show_get_monomer(wxWindow *parent) {
   EntryDialog *d = new EntryDialog(parent, "Get monomer molecule", "Note: Code is Case Sensitive");
   d->add_field("3 Letter Code:");
   d->on_ok = [parent](EntryDialog &dialog) {
      std::string code = dialog.field(0)->get_text().utf8_string();
      if (code.empty()) { wxBell(); return false; }
      wxBusyCursor busy;
      if (bandicoot_core_get_monomer(code) >= 0) return true;
      // not in the dictionaries here: Coot offers to fetch and generate it
      if (! confirm(&dialog, "We don't have the entry for " + wxString::FromUTF8(code) +
                    ". Shall I go and get it?", "Get it", "Cancel"))
         return false;
      bandicoot_core_get_monomer_from_network(code);
      return true;
   };
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Search Monomer Library

namespace {

// One result: the monomer's 2D picture (drawn when first shown) beside
// "CODE : name"; clicking it reads the monomer in.
class MonomerRow : public wxPanel {
public:
   MonomerRow(wxWindow *parent, const std::string &comp_id_in, const std::string &name, bool sbase_in)
      : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, image_size + 12)),
        comp_id(comp_id_in), label(wxString::FromUTF8(comp_id_in + " : " + name)), sbase(sbase_in) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) { paint(); });
      Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hovered = true; Refresh(); });
      Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hovered = false; Refresh(); });
      Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
         wxBusyCursor busy;
         if (sbase) bandicoot_core_get_sbase_monomer(comp_id);
         else bandicoot_core_get_monomer(comp_id);
      });
   }
   static const int image_size = 150;

private:
   std::string comp_id;
   wxString label;
   bool sbase;
   bool hovered = false;
   bool image_tried = false;
   wxBitmap image;

   void paint() {
      if (! image_tried) {
         image_tried = true;
         std::string png = bandicoot_core_monomer_depiction(comp_id, image_size);
         if (! png.empty()) {
            wxMemoryInputStream in(png.data(), png.size());
            wxImage img(in, wxBITMAP_TYPE_PNG);
            if (img.IsOk()) image = wxBitmap(img);
         }
      }
      wxAutoBufferedPaintDC dc(this);
      wxRect r = GetClientRect();
      dc.SetBackground(wxBrush(hovered ? palette::face_hover() : palette::face()));
      dc.Clear();
      dc.SetPen(wxPen(palette::shadow_of(palette::face())));
      dc.SetBrush(*wxTRANSPARENT_BRUSH);
      dc.DrawRectangle(r.Deflate(2));
      wxRect pic(8, 6, image_size, image_size);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(*wxWHITE_BRUSH);
      dc.DrawRectangle(pic);
      if (image.IsOk()) dc.DrawBitmap(image, pic.x, pic.y);
      dc.SetTextForeground(palette::text());
      wxSize ext = dc.GetTextExtent(label);
      dc.DrawText(label, pic.GetRight() + 12, (r.height - ext.y) / 2);
   }
};

} // namespace

void show_monomer_search(wxWindow *parent) {

   wxFrame *w = new wxFrame(parent, wxID_ANY, "Search Monomer Library", wxDefaultPosition, wxDefaultSize,
                            wxDEFAULT_FRAME_STYLE);
   wxPanel *p = new wxPanel(w);
   p->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   TitledPanel *top = new TitledPanel(p);
   wxWindow *tc = top->content();
   top->content_sizer()->Add(new wxStaticText(tc, wxID_ANY, "Search the Monomer Library"), 0,
                             wxALIGN_CENTER | wxBOTTOM, 6);
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *text = new wxTextCtrl(tc, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                     wxTE_PROCESS_ENTER);
   BevelButton *search = icon_button(tc, "Search", "stock-button-apply.png");
   row->Add(labelled(tc, "Text:", text), 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   row->Add(search, 0, wxALIGN_CENTER_VERTICAL);
   top->content_sizer()->Add(row, 0, wxEXPAND | wxBOTTOM, 6);
   wxCheckBox *minimal = new wxCheckBox(tc, wxID_ANY, "Include Monomers with Minimal Descriptions");
   wxCheckBox *sbase = new wxCheckBox(tc, wxID_ANY, "Use SBase Monomers");
   top->content_sizer()->Add(minimal, 0, wxBOTTOM, 4);
   top->content_sizer()->Add(sbase, 0);
   col->Add(top, 0, wxEXPAND | wxALL, 4);

   wxScrolledWindow *results = new wxScrolledWindow(p, wxID_ANY, wxDefaultPosition, wxSize(-1, 120),
                                                    wxVSCROLL | wxBORDER_SUNKEN);
   results->SetBackgroundColour(palette::face());
   results->SetScrollRate(0, 20);
   results->SetSizer(new wxBoxSizer(wxVERTICAL));
   col->Add(results, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

   BevelButton *close = icon_button(p, "Close", "stock-menu-close.png");
   close->Bind(wxEVT_BUTTON, [w](wxCommandEvent &) { w->Close(); });
   col->Add(close, 0, wxALIGN_RIGHT | wxALL, 6);
   p->SetSizer(col);

   auto run = [w, results, text, minimal, sbase]() {
      wxBusyCursor busy;
      results->Freeze();
      results->DestroyChildren();
      results->GetSizer()->Clear();
      bool use_sbase = sbase->IsChecked();
      auto hits = bandicoot_core_search_monomers(text->GetValue().utf8_string(),
                                                 minimal->IsChecked(), use_sbase);
      for (const auto &h : hits)
         results->GetSizer()->Add(new MonomerRow(results, h.first, h.second, use_sbase), 0, wxEXPAND | wxBOTTOM, 2);
      results->FitInside();
      results->Scroll(0, 0);
      results->Thaw();
      // grow to show a few results, as Coot's window did
      int want = std::min(int(hits.size()), 3) * (MonomerRow::image_size + 14);
      wxSize s = w->GetSize();
      int extra = want - results->GetClientSize().y;
      if (extra > 0) w->SetSize(wxSize(std::max(s.x, 560), s.y + extra));
   };
   search->Bind(wxEVT_BUTTON, [run](wxCommandEvent &) { run(); });
   text->Bind(wxEVT_TEXT_ENTER, [run](wxCommandEvent &) { run(); });

   w->Fit();
   w->SetSize(wxSize(std::max(w->GetSize().x, 420), w->GetSize().y));
   w->CentreOnParent();
   w->Show();
}

// ------------------------------------------------------------ accession codes

void show_accession_code(wxWindow *parent, int kind_number) {
   bandicoot_accession_kind kind = bandicoot_accession_kind(kind_number);
   bool uniprot = kind == bandicoot_accession_kind::alphafold_uniprot;
   wxDialog *d = new wxDialog(parent, wxID_ANY, uniprot ? "Fetch AlphaFold Model" : "PDB Accession Code");
   d->SetBackgroundColour(palette::face());
   TitledPanel *frame = new TitledPanel(d);
   wxWindow *c = frame->content();
   frame->content_sizer()->Add(new wxStaticText(c, wxID_ANY, uniprot ? "UniProt ID:" : "PDB Accession Code:"),
                               0, wxALIGN_CENTER | wxBOTTOM, 4);
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *code = new wxTextCtrl(c, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(180, -1),
                                     wxTE_PROCESS_ENTER);
   BevelButton *get = new BevelButton(c, wxID_ANY, "Get it");
   row->Add(code, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
   row->Add(get, 0, wxALIGN_CENTER_VERTICAL);
   frame->content_sizer()->Add(row, 0, wxEXPAND);

   // the window closes once asked, whether or not the fetch succeeds
   auto fetch = [d, code, kind]() {
      std::string text = code->GetValue().utf8_string();
      if (text.empty()) { wxBell(); return; }
      d->Hide();
      {
         wxBusyCursor busy;
         bandicoot_core_fetch_by_accession(kind, text);
      }
      d->Destroy();
   };
   get->Bind(wxEVT_BUTTON, [fetch](wxCommandEvent &) { fetch(); });
   code->Bind(wxEVT_TEXT_ENTER, [fetch](wxCommandEvent &) { fetch(); });

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(frame, 1, wxEXPAND | wxALL, 6);
   d->SetSizerAndFit(outer);
   d->CentreOnParent();
   d->Show();
   code->SetFocus();
}

// ------------------------------------------------------------ Recover Session

void show_recover_session(wxWindow *parent) {
   std::vector<bandicoot_backup> backups = bandicoot_core_recoverable_backups();
   if (backups.empty()) {
      info(parent, "Nothing to recover");
      return;
   }
   for (const bandicoot_backup &b : backups) {
      wxDialog *d = new wxDialog(parent, wxID_ANY, "Recover Coordinates");
      d->SetBackgroundColour(palette::face());
      TitledPanel *frame = new TitledPanel(d);
      wxWindow *c = frame->content();
      for (const std::string &line : { std::string("Read Coordinates:"), b.name,
                                       std::string("Backup Coordinates:"), b.backup_file_name })
         frame->content_sizer()->Add(new wxStaticText(c, wxID_ANY, wxString::FromUTF8(line)),
                                     0, wxBOTTOM, 6);
      wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
      buttons->GetAffirmativeButton()->SetLabel("Recover");

      d->Bind(wxEVT_BUTTON, [d, b](wxCommandEvent &) {
         bandicoot_core_recover_from_backup(b);
         d->Destroy();
      }, wxID_OK);
      d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
      d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });

      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(frame, 1, wxEXPAND | wxALL, 10);
      outer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
      d->SetSizerAndFit(outer);
      d->CentreOnParent();
      d->Show();
   }
}

// ------------------------------------------------------------ Export Map

namespace {

void export_map_to_file(wxWindow *parent, int imol, bool fragment, float radius) {
   FileRequest r;
   r.title = "Export Map";
   r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
   wxString path = save_file(parent, r);
   if (path.empty()) return;
   std::string file = path.utf8_string();
   bool written;
   {
      wxBusyCursor busy;
      written = fragment ? bandicoot_core_export_map_fragment(imol, radius, file)
                         : bandicoot_core_export_map(imol, file);
   }
   if (! written)
      error(parent, "Could not export map to", wxFileName(path).GetFullName(),
            "Export Map: map " + std::to_string(imol) + " not written to " + file);
}

} // namespace

void show_export_map(wxWindow *parent, bool fragment) {
   MoleculeChooser::molecule_list maps = bandicoot_core_maps_for_fitting();
   if (maps.empty()) {
      info(parent, "No maps to export");
      return;
   }
   EntryDialog *d = new EntryDialog(parent, "Export Map", wxEmptyString, maps);
   d->chooser()->set_imol(bandicoot_core_export_map_default());
   if (fragment) {
      EntryRow *radius = d->add_field("Export Map within", "10");
      radius->GetSizer()->Add(new wxStaticText(radius, wxID_ANY,
                                 wxString::FromUTF8("\xC3\x85ngstr\xC3\xB6ms of the screen centre")),
                              0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
      radius->Layout();
      d->Fit();
   }
   d->on_ok = [parent, fragment](EntryDialog &dialog) {
      int imol = dialog.get_imol();
      double radius = 0;
      if (fragment && (! dialog.field(0)->get_double(radius) || radius <= 0)) {
         wxBell();
         return false;
      }
      // the file panel follows once this window has gone
      parent->CallAfter([parent, imol, fragment, radius]() {
         export_map_to_file(parent, imol, fragment, float(radius));
      });
      return true;
   };
   d->CentreOnParent();
   d->Show();
}

// ------------------------------------------------------------ Delete Molecules

void show_delete_molecules(wxWindow *parent) {
   MoleculeChooser::molecule_list molecules = bandicoot_core_models_and_maps();
   wxDialog *d = new wxDialog(parent, wxID_ANY, "Delete Molecules", wxDefaultPosition,
                              wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
   d->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(d, wxID_ANY, "Delete?   Name"), 0, wxLEFT | wxTOP | wxBOTTOM, 10);

   wxScrolledWindow *list = new wxScrolledWindow(d, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                 wxVSCROLL | wxBORDER_THEME);
   wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
   std::vector<std::pair<int, wxCheckBox *>> boxes;
   for (const auto &m : molecules) {
      wxCheckBox *b = new wxCheckBox(list, wxID_ANY, wxString::FromUTF8(m.second));
      rows->Add(b, 0, wxALL, 4);
      boxes.push_back({ m.first, b });
   }
   list->SetSizer(rows);
   list->SetScrollRate(0, 10);
   // as tall as the list, up to seven rows; longer lists scroll
   wxSize all = rows->CalcMin();
   int row_height = boxes.empty() ? 24 : boxes.front().second->GetBestSize().y + 8;
   list->SetMinSize(wxSize(std::max(all.x + 24, 300), std::min(all.y, 7 * row_height) + 4));
   col->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

   wxStdDialogButtonSizer *buttons = d->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
   buttons->GetAffirmativeButton()->SetLabel("Delete Marked Molecules && Maps");
   col->Add(buttons, 0, wxEXPAND | wxALL, 10);

   d->Bind(wxEVT_BUTTON, [d, boxes](wxCommandEvent &) {
      std::vector<int> marked;
      for (const auto &b : boxes)
         if (b.second->IsChecked()) marked.push_back(b.first);
      d->Destroy();
      for (int imol : marked) bandicoot_core_close_molecule(imol);
   }, wxID_OK);
   d->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });

   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
}

} // namespace wxui

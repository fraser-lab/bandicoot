// src/wxUI/main/delete-waters.cc

#include "wxUI/main/delete-waters.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/click-through-dialog.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/checkbox.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   b->set_icon(icon(icon_file, wxSize(16, 16)));
   b->SetMinSize(wxSize(110, 28));
   return b;
}

wxFrame *grey_frame(const wxString &title, wxPanel *&panel) {
   wxFrame *f = new wxFrame(owner, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                            (wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER)) |
                            wxFRAME_FLOAT_ON_PARENT);
   panel = new wxPanel(f);
   panel->SetBackgroundColour(palette::face());
   return f;
}

// ------------------------------------------------ Delete item

class DeleteWindow {
public:
   void open(int kind) {
      if (frame) dispose();
      wxPanel *p;
      frame = grey_frame("Delete item", p);
      TitledPanel *body = new TitledPanel(p);
      wxWindow *b = body->content();
      wxBoxSizer *bs = body->content_sizer();
      bs->Add(new wxStaticText(b, wxID_ANY, "Delete What Item Type?"), 0, wxALIGN_CENTER | wxBOTTOM, 6);
      // Coot's order, which is also the core's numbering of the kinds
      const char *labels[] = { "Residue/Monomer", "Water", "Atom", "Sidechain", "Sidechain Zone",
                               "Chain", "Hydrogens in Residue", "Delete Zone" };
      for (int k = 0; k < 8; k++) {
         wxRadioButton *r = new wxRadioButton(b, wxID_ANY, labels[k], wxDefaultPosition, wxDefaultSize,
                                              k == 0 ? wxRB_GROUP : 0);
         r->SetValue(k == kind);
         r->Bind(wxEVT_RADIOBUTTON, [k](wxCommandEvent &) { bandicoot_core_delete_kind(k); });
         bs->Add(r, 0, wxLEFT | wxBOTTOM, 4);
      }
      keep = new wxCheckBox(b, wxID_ANY, "Keep Delete Active");
      keep->SetToolTip("Deleting an item does not dismiss this dialog. "
                       "(Has the same action as using Ctrl-Left Click)");
      bs->Add(keep, 0, wxLEFT | wxTOP | wxBOTTOM, 8);
      bs->Add(new wxStaticText(b, wxID_ANY, "Now click on an atom (of the residue)"), 0, wxALIGN_CENTER | wxTOP, 8);
      bs->Add(new wxStaticText(b, wxID_ANY, "that you wish to delete."), 0, wxALIGN_CENTER);

      BevelButton *close = icon_button(p, "Close", "stock-menu-close.png");
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { close_by_hand(); });
      frame->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { close_by_hand(); });

      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(body, 1, wxEXPAND | wxALL, 6);
      col->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
      p->SetSizer(col);
      col->SetSizeHints(frame);
      if (at != wxDefaultPosition) frame->Move(at); else frame->CentreOnParent();
      frame->Show();
   }

   bool is_open() const { return frame != nullptr; }
   bool keep_active() const { return keep && keep->GetValue(); }

   // closed by the core after a pick (cleanup: the pick ends too)
   void close(bool cleanup) {
      if (! frame) return;
      dispose();
      if (cleanup) bandicoot_core_delete_closed();
   }

private:
   wxFrame *frame = nullptr;
   wxCheckBox *keep = nullptr;
   wxPoint at = wxDefaultPosition;

   void close_by_hand() { close(true); }

   void dispose() {
      at = frame->GetPosition();
      frame->Destroy();
      frame = nullptr;
      keep = nullptr;
   }
};

DeleteWindow delete_window;

// ------------------------------------------------ Find Waters

wxPoint find_waters_at = wxDefaultPosition;
wxFrame *find_waters = nullptr;

std::string two_places(double v) {
   char b[32];
   snprintf(b, sizeof b, "%.2f", v);
   return b;
}

// one radio per molecule, the first chosen; the molecule's number for each
std::vector<std::pair<wxRadioButton *, int>>
radio_list(TitledPanel *frame, const wxString &title,
           const std::vector<std::pair<int, std::string>> &molecules) {
   std::vector<std::pair<wxRadioButton *, int>> v;
   frame->content_sizer()->Add(new wxStaticText(frame->content(), wxID_ANY, title), 0,
                               wxALIGN_CENTER | wxBOTTOM, 4);
   for (size_t i = 0; i < molecules.size(); i++) {
      wxRadioButton *r = new wxRadioButton(frame->content(), wxID_ANY,
                                           wxString::FromUTF8(molecules[i].second),
                                           wxDefaultPosition, wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
      r->SetValue(i == 0);
      frame->content_sizer()->Add(r, 0, wxLEFT | wxBOTTOM, 4);
      v.push_back({ r, molecules[i].first });
   }
   return v;
}

int chosen(const std::vector<std::pair<wxRadioButton *, int>> &radios) {
   for (const auto &r : radios)
      if (r.first->GetValue()) return r.second;
   return -1;
}

void open_find_waters() {
   if (find_waters) find_waters->Close();
   wxPanel *p;
   wxFrame *w = grey_frame("Find Waters", p);
   find_waters = w;

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   TitledPanel *maps_frame = new TitledPanel(p);
   auto maps = radio_list(maps_frame, "Select Map:", bandicoot_core_find_waters_maps());
   col->Add(maps_frame, 0, wxEXPAND | wxALL, 6);
   TitledPanel *models_frame = new TitledPanel(p);
   auto models = radio_list(models_frame, "Select Protein (which Masks Map):",
                            bandicoot_core_find_waters_models());
   col->Add(models_frame, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   bandicoot_find_waters_values v = bandicoot_core_find_waters_values();
   TitledPanel *peaks = new TitledPanel(p);
   peaks->content_sizer()->Add(new wxStaticText(peaks->content(), wxID_ANY, "Find Peaks above:"), 0,
                               wxALIGN_CENTER | wxBOTTOM, 4);
   wxBoxSizer *prow = new wxBoxSizer(wxHORIZONTAL);
   wxTextCtrl *rmsd = new wxTextCtrl(peaks->content(), wxID_ANY, wxString::Format("%.1f", v.rmsd),
                                     wxDefaultPosition, wxSize(90, -1));
   rmsd->SetToolTip("Note that this is r.m.s.d. of the map after it has been masked by the protein. "
                    "1.8 r.m.s.d seems good for a 2Fo-Fc style map and about 3.0 for a difference map.");
   prow->Add(rmsd, 0, wxRIGHT, 8);
   prow->Add(new wxStaticText(peaks->content(), wxID_ANY, "rmsd"), 0, wxALIGN_CENTER_VERTICAL);
   peaks->content_sizer()->Add(prow);
   col->Add(peaks, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   TitledPanel *distances = new TitledPanel(p);
   wxFlexGridSizer *grid = new wxFlexGridSizer(2, 4, 8);
   grid->AddGrowableCol(1);
   grid->Add(new wxStaticText(distances->content(), wxID_ANY, "Minimum distance to protein atoms:"), 0,
             wxALIGN_CENTER_VERTICAL);
   wxTextCtrl *min_d = new wxTextCtrl(distances->content(), wxID_ANY, two_places(v.min_distance));
   grid->Add(min_d, 1, wxEXPAND);
   grid->Add(new wxStaticText(distances->content(), wxID_ANY, "Maximum distance to protein atoms:"), 0,
             wxALIGN_CENTER_VERTICAL);
   wxTextCtrl *max_d = new wxTextCtrl(distances->content(), wxID_ANY, two_places(v.max_distance));
   grid->Add(max_d, 1, wxEXPAND);
   distances->content_sizer()->Add(grid, 0, wxEXPAND);
   col->Add(distances, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   TitledPanel *dest = new TitledPanel(p);
   dest->content_sizer()->Add(new wxStaticText(dest->content(), wxID_ANY, "Waters should be:"), 0, wxBOTTOM, 4);
   wxRadioButton *into_model = new wxRadioButton(dest->content(), wxID_ANY,
                                                 "Added to Molecule that masks the map",
                                                 wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
   wxRadioButton *new_mol = new wxRadioButton(dest->content(), wxID_ANY, "A new \"Waters\" molecule");
   into_model->SetValue(true);
   dest->content_sizer()->Add(into_model, 0, wxBOTTOM, 4);
   dest->content_sizer()->Add(new_mol);
   col->Add(dest, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   BevelButton *go = icon_button(p, "Find Waters", "stock-button-ok.png");
   BevelButton *cancel = icon_button(p, "Cancel", "stock-button-cancel.png");
   go->Bind(wxEVT_BUTTON, [w, maps, models, rmsd, min_d, max_d, new_mol](wxCommandEvent &) {
      int imol_map = chosen(maps), imol_model = chosen(models);
      if (imol_map < 0 || imol_model < 0) {
         error(w, "Cannot find waters", imol_map < 0 ? "No map is loaded." : "No model is loaded.");
         return;
      }
      // numbers only, refused with a beep; the r.m.s.d. as the core accepts it
      double r, lo, hi;
      if (! rmsd->GetValue().ToCDouble(&r) || r <= 0.0 || r >= 100.0) { wxBell(); rmsd->SetFocus(); return; }
      if (! min_d->GetValue().ToCDouble(&lo) || lo < 0.0) { wxBell(); min_d->SetFocus(); return; }
      if (! max_d->GetValue().ToCDouble(&hi) || hi < lo) { wxBell(); max_d->SetFocus(); return; }
      bool into_new = new_mol->GetValue();
      find_waters_at = w->GetPosition();
      find_waters = nullptr;
      w->Destroy();  // gone before the search, as Coot's
      bandicoot_core_find_waters(imol_map, imol_model, into_new,
                                 { float(r), float(lo), float(hi) });
   });
   cancel->Bind(wxEVT_BUTTON, [w](wxCommandEvent &) { w->Close(); });
   w->Bind(wxEVT_CLOSE_WINDOW, [w](wxCloseEvent &) {
      find_waters_at = w->GetPosition();
      find_waters = nullptr;
      w->Destroy();
   });
   buttons->Add(go, 0, wxRIGHT, 12);
   buttons->Add(cancel);
   col->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);

   p->SetSizer(col);
   col->SetSizeHints(w);
   if (find_waters_at != wxDefaultPosition) w->Move(find_waters_at); else w->CentreOnParent();
   w->Show();
}

// ------------------------------------------------ Unmodelled blobs

void show_blobs(const std::vector<std::vector<float>> &blobs) {
   ClickThroughDialog *d = new ClickThroughDialog(owner, "Unmodelled blobs of density:",
                                                  ClickThroughList::RowType::Button,
                                                  "There are unexplained blobs of\n"
                                                  "density (too big to be waters):");
   for (size_t i = 0; i < blobs.size(); i++)
      d->list()->add_item(wxString::Format("Blob %zu", i + 1));
   d->list()->on_click = [blobs](size_t i) {
      if (i < blobs.size() && blobs[i].size() == 3)
         bandicoot_core_centre_on(blobs[i][0], blobs[i][1], blobs[i][2]);
   };
   d->CentreOnParent();
   d->Show();
}

} // namespace

void install_delete_waters(wxWindow *parent) {
   owner = parent;
   bandicoot_delete_view v;
   v.open = [](int kind) { delete_window.open(kind); };
   v.is_open = []() { return delete_window.is_open(); };
   v.keep_active = []() { return delete_window.keep_active(); };
   v.close = [](bool cleanup) { delete_window.close(cleanup); };
   v.blobs = [](const std::vector<std::vector<float>> &blobs) { show_blobs(blobs); };
   bandicoot_core_set_delete_view(v);
}

void show_find_waters(wxWindow *parent) {
   owner = parent;
   open_find_waters();
}

} // namespace wxui

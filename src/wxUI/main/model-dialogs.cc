// src/wxUI/main/model-dialogs.cc

#include "wxUI/main/model-dialogs.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/widgets/click-through-list.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/tglbtn.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   if (icon_file) b->set_icon(icon(icon_file, wxSize(16, 16)));
   return b;
}

// A free-floating window over the main one, grey like Coot's, that ends in
// one of two ways: accepted (OK / Apply) or not (Cancel, the close box).
class MovingAtomsWindow : public wxFrame {
public:
   MovingAtomsWindow(const wxString &title, std::function<void(bool)> finish_in)
      : wxFrame(owner, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                (wxDEFAULT_FRAME_STYLE & ~wxMAXIMIZE_BOX) | wxFRAME_FLOAT_ON_PARENT),
        finish(finish_in) {
      panel = new wxPanel(this);
      panel->SetBackgroundColour(palette::face());
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { end(false); });
   }

   wxPanel *panel;

   // the OK-like and Cancel buttons, side by side at the bottom right
   wxBoxSizer *buttons(const wxString &ok_label, const char *ok_icon) {
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      BevelButton *ok = icon_button(panel, ok_label, ok_icon);
      BevelButton *cancel = icon_button(panel, "Cancel", "stock-button-cancel.png");
      ok->SetMinSize(wxSize(100, 28));
      cancel->SetMinSize(wxSize(100, 28));
      ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { end(true); });
      cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { end(false); });
      row->Add(ok, 0, wxRIGHT, 6);
      row->Add(cancel);
      return row;
   }

   // Hidden first, then the core is told, then the window goes.
   void end(bool accept) {
      if (ended) return;
      ended = true;
      Hide();
      if (on_end) on_end();
      if (finish) finish(accept);
      Destroy();
   }

   // dropped without telling the core (another of its kind replaces it)
   void drop() {
      ended = true;
      if (on_end) on_end();
      Destroy();
   }

   std::function<void()> on_end;

protected:
   std::function<void(bool)> finish;
   bool ended = false;
};

void lay_out(MovingAtomsWindow *w, wxSizer *content) {
   w->panel->SetSizer(content);
   content->SetSizeHints(w);
   w->CentreOnParent();
   w->Show();
}

// ------------------------------------------------ Rotation & Translation

MovingAtomsWindow *rot_trans = nullptr;

void open_rot_trans() {
   if (rot_trans) rot_trans->drop();
   MovingAtomsWindow *w = new MovingAtomsWindow("Rotation & Translation",
                                                bandicoot_core_rot_trans_finish);
   rot_trans = w;
   w->on_end = []() { rot_trans = nullptr; };

   TitledPanel *body = new TitledPanel(w->panel);
   const char *titles[] = { "X translation", "Y translation", "Z translation",
                            "X rotate", "Y rotate", "Z rotate" };
   for (int i = 0; i < 6; i++) {
      // Coot's range and step; the core moves by the change in value
      TitledSlider *s = new TitledSlider(body->content(), titles[i], -180.0, 360.0, 0.1, 0.0, 1);
      s->on_change = [i](double v) { bandicoot_core_rot_trans_slider(i, v); };
      body->content_sizer()->Add(s, 0, wxEXPAND | wxBOTTOM, 4);
   }
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(body, 1, wxEXPAND | wxALL, 6);
   col->Add(w->buttons("OK", "stock-button-ok.png"), 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   lay_out(w, col);
   w->SetSize(wxSize(std::max(w->GetSize().x, 260), w->GetSize().y));
}

// ------------------------------------------------ Select Rotamer

class RotamerWindow : public MovingAtomsWindow {
public:
   RotamerWindow(const std::vector<std::string> &labels, bool alt_conf, float occupancy)
      : MovingAtomsWindow("Select Rotamer", bandicoot_core_rotamer_finish) {
      list = new ClickThroughList(panel, ClickThroughList::RowType::Radio);
      for (const std::string &l : labels) list->add_item(wxString::FromUTF8(l));
      list->SetMinSize(wxSize(300, 300));
      list->select(0);  // the core shows the first already
      list->on_click = [this](size_t i) { show(int(i)); };
      n = int(labels.size());

      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(list, 1, wxEXPAND | wxALL, 6);
      if (alt_conf) {
         TitledSlider *occ = new TitledSlider(panel, "New Alt Conf Occupancy", 0.0, 1.0, 0.01,
                                              occupancy, 2);
         occ->on_change = [](double v) { bandicoot_core_rotamer_occupancy(float(v)); };
         col->Add(occ, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
      }
      col->Add(buttons("OK", "stock-button-ok.png"), 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
      lay_out(this, col);
   }

   // "." and ",": the next or previous rotamer, round the end of the list
   void step(int by) {
      if (n <= 0) return;
      int i = (list->selected() + by + n) % n;
      list->select(i);
      show(i);
   }

   void accept() { end(true); }

private:
   ClickThroughList *list;
   int n = 0;
   void show(int i) { bandicoot_core_rotamer_show(i); }
};

RotamerWindow *rotamers = nullptr;

void open_rotamers(const std::vector<std::string> &labels, bool alt_conf, float occupancy) {
   if (rotamers) rotamers->drop();
   rotamers = new RotamerWindow(labels, alt_conf, occupancy);
   rotamers->on_end = []() { rotamers = nullptr; };
}

// ------------------------------------------------ Edit Chi Angles

void show_chi_help() {
   wxDialog *d = new wxDialog(owner, wxID_ANY, "Chi Angle Help");
   d->SetBackgroundColour(palette::face());
   TitledPanel *body = new TitledPanel(d);
   // one plain label per line, each centred by the sizer: a single
   // centre-aligned multi-line label can take far more width than its text
   const char *lines[] = {
      "Select the chi angle (rotatable bond) by clicking",
      "on the button in the list.",
      "",
      "In the graphics window, click and drag as you",
      "would if you wanted to change the view - this",
      "causes the atoms to move.",
      "",
      "There are keyboard equivalents to the buttons, \"1\"",
      "will rotate chi_1, \"2\" will rotate chi_2 etc...",
      "",
      "To make the view move (rather than the intermediate",
      "atoms), click the \"View Rotation Mode\" button or use",
      "the Ctrl key when moving the mouse."
   };
   for (const char *l : lines)
      body->content_sizer()->Add(new wxStaticText(body->content(), wxID_ANY, l), 0,
                                 wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, 6);
   BevelButton *dismiss = icon_button(d, "Dismiss", "stock-menu-close.png");
   dismiss->SetMinSize(wxSize(110, 28));
   dismiss->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(body, 1, wxEXPAND | wxALL, 6);
   col->Add(dismiss, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d->SetSizerAndFit(col);
   // no larger than its contents
   d->SetMaxSize(d->GetSize());
   d->CentreOnParent();
   d->Show();
}

class ChiWindow : public MovingAtomsWindow {
public:
   ChiWindow(const std::vector<std::string> &labels, int mode, bool hydrogens)
      : MovingAtomsWindow(mode == 2 ? "Add Alternative Conformer Split by Torsion" : "Edit Chi Angles",
                          bandicoot_core_chi_finish) {
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(new wxStaticText(panel, wxID_ANY, "Chi Angles Rotation Mode:"), 0,
               wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);

      chis = new wxScrolledWindow(panel, wxID_ANY, wxDefaultPosition, wxSize(300, 160),
                                  wxVSCROLL | wxHSCROLL | wxBORDER_SUNKEN);
      chis->SetBackgroundColour(palette::face());
      chis->SetScrollRate(10, 10);
      chis->SetSizer(new wxBoxSizer(wxVERTICAL));
      fill(labels);
      col->Add(chis, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);

      wxCheckBox *h = new wxCheckBox(panel, wxID_ANY, "Add Chi Angles for Hydrogens");
      h->SetValue(hydrogens);
      h->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &e) {
         // the list is remade; it may start again from the first torsion
         std::vector<std::string> l = bandicoot_core_chi_hydrogens(e.IsChecked());
         CallAfter([this, l]() { fill(l); });
      });
      col->Add(h, 0, wxALL, 6);

      BevelButton *reverse = icon_button(panel, "Reverse Fragment", "stock-menu-refresh.png");
      reverse->set_toggle(true);
      reverse->SetToolTip("Reverse the moving and static parts of the residue/ligand");
      reverse->Bind(wxEVT_TOGGLEBUTTON, [](wxCommandEvent &e) {
         bandicoot_core_chi_reverse(e.GetInt() != 0);
      });
      BevelButton *view = icon_button(panel, "View Rotation Mode", nullptr);
      view->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_chi_view_rotation(); });
      BevelButton *help = icon_button(panel, "Help me out here...", "stock-menu-about.png");
      help->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { show_chi_help(); });
      for (BevelButton *b : { reverse, view, help }) {
         b->SetMinSize(wxSize(-1, 30));
         col->Add(b, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
      }
      col->Add(buttons("Apply", "stock-button-apply.png"), 0, wxALIGN_RIGHT | wxALL, 6);
      lay_out(this, col);
   }

private:
   wxScrolledWindow *chis;
   std::vector<wxWindow *> chi_buttons;

   // One button per rotatable torsion: a click drags it, hovering flashes
   // its bond.
   void fill(const std::vector<std::string> &labels) {
      for (wxWindow *b : chi_buttons) b->Destroy();
      chi_buttons.clear();
      for (size_t i = 0; i < labels.size(); i++) {
         BevelButton *b = new BevelButton(chis, wxID_ANY, wxString::FromUTF8(labels[i]));
         int ichi = int(i);
         b->Bind(wxEVT_BUTTON, [ichi](wxCommandEvent &) { bandicoot_core_chi_choose(ichi); });
         b->Bind(wxEVT_ENTER_WINDOW, [ichi](wxMouseEvent &e) {
            bandicoot_core_chi_flash(ichi);
            e.Skip();
         });
         chis->GetSizer()->Add(b, 0, wxEXPAND | wxALL, 2);
         chi_buttons.push_back(b);
      }
      chis->FitInside();
      chis->Layout();
   }
};

ChiWindow *edit_chi = nullptr;

void open_edit_chi(const std::vector<std::string> &labels, int mode, bool hydrogens) {
   if (edit_chi) edit_chi->drop();
   edit_chi = new ChiWindow(labels, mode, hydrogens);
   edit_chi->on_end = []() { edit_chi = nullptr; };
}

// ------------------------------------------------ Edit Backbone Torsions

MovingAtomsWindow *edit_backbone = nullptr;

void open_edit_backbone() {
   if (edit_backbone) edit_backbone->drop();
   MovingAtomsWindow *w = new MovingAtomsWindow("Edit Backbone Torsions",
                                                bandicoot_core_backbone_finish);
   edit_backbone = w;
   w->on_end = []() { edit_backbone = nullptr; };
   TitledPanel *body = new TitledPanel(w->panel);
   // degrees either way, as Coot's sliders give them
   TitledSlider *peptide  = new TitledSlider(body->content(), "Rotate Peptide",  -180.0, 180.0, 0.1, 0.0, 1);
   TitledSlider *carbonyl = new TitledSlider(body->content(), "Rotate Carbonyl", -180.0, 180.0, 0.1, 0.0, 1);
   peptide->on_change  = [](double v) { bandicoot_core_backbone_peptide(v); };
   carbonyl->on_change = [](double v) { bandicoot_core_backbone_carbonyl(v); };
   body->content_sizer()->Add(peptide,  0, wxEXPAND | wxBOTTOM, 6);
   body->content_sizer()->Add(carbonyl, 0, wxEXPAND);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(body, 1, wxEXPAND | wxALL, 6);
   col->Add(w->buttons("OK", "stock-button-ok.png"), 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   lay_out(w, col);
   w->SetSize(wxSize(std::max(w->GetSize().x, 380), w->GetSize().y));
}

} // namespace

void install_model_dialogs(wxWindow *parent) {
   owner = parent;
   bandicoot_moving_atoms_view v;
   v.rot_trans = []() { open_rot_trans(); };
   v.rotamers = [](const std::vector<std::string> &labels, bool alt_conf, float occupancy) {
      open_rotamers(labels, alt_conf, occupancy);
   };
   v.edit_chi = [](const std::vector<std::string> &labels, int mode, bool hydrogens) {
      open_edit_chi(labels, mode, hydrogens);
   };
   v.edit_backbone = []() { open_edit_backbone(); };
   // the windows during which a drag must not move the atoms bodily, as
   // Coot's rotamer and backbone dialogs
   v.open_windows = []() { return rotamers != nullptr || edit_backbone != nullptr; };
   bandicoot_core_set_moving_atoms_view(v);
}

bool model_dialogs_take_key(int key_code) {
   if (! rotamers) return false;
   if (key_code == WXK_RETURN || key_code == WXK_NUMPAD_ENTER) { rotamers->accept(); return true; }
   if (key_code == '.') { rotamers->step(+1); return true; }
   if (key_code == ',') { rotamers->step(-1); return true; }
   return false;
}

} // namespace wxui

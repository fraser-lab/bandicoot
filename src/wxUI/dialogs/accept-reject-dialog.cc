// src/wxUI/dialogs/accept-reject-dialog.cc

#include "wxUI/dialogs/accept-reject-dialog.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace wxui {

namespace {

// the stock dialog's lights, in its order
const char *light_names[] = { "Bonds", "Angles", "Torsions", "Planes", "Chirals",
                              "Non-bonded", "Rama" };

// One light: a sunken box whose lower part shows the distortion colour, as
// GTK drew a coloured event box inside a frame.
class LightBox : public wxPanel {
public:
   explicit LightBox(wxWindow *parent)
      : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(28, 22)) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
         wxPaintDC dc(this);
         wxRect r = GetClientRect();
         draw_frame(dc, r, FrameStyle::Sunken, palette::face(), palette::shadow_of(palette::face()));
         wxRect in = frame_interior(r);
         const int strip = 4; // points of frame face left above the colour
         in.y += strip;
         in.height -= strip;
         dc.SetPen(*wxTRANSPARENT_PEN);
         dc.SetBrush(wxBrush(colour));
         dc.DrawRectangle(in);
      });
   }
   void set_colour(const wxColour &c) { colour = c; Refresh(); }
private:
   wxColour colour = palette::face();
};

} // namespace

AcceptRejectDialog::AcceptRejectDialog(wxWindow *parent, const Hooks &h)
   : wxFrame(parent, wxID_ANY, "Accept Refinement?", wxDefaultPosition, wxDefaultSize,
             (wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER)) | wxFRAME_FLOAT_ON_PARENT),
     hooks(h) {

   panel = new wxPanel(this);
   panel->SetBackgroundColour(palette::face());

   // everything but Accept and Reject sits in one frame
   TitledPanel *body = new TitledPanel(panel);
   wxWindow *b = body->content();
   wxBoxSizer *col = body->content_sizer();

   warnings_label = new wxStaticText(b, wxID_ANY, wxEmptyString);
   question_label = new wxStaticText(b, wxID_ANY, "Accept Refinement?");
   status_label   = new wxStaticText(b, wxID_ANY, wxEmptyString);
   BevelButton *reverse = new BevelButton(b, wxID_ANY, "Reverse");
   reverse->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      run("on_accept_reject_reverse_button_clicked");
   });
   reverse_button = reverse;
   col->Add(warnings_label, 0, wxALIGN_CENTER | wxBOTTOM, 4);
   col->Add(question_label, 0, wxALIGN_CENTER | wxBOTTOM, 6);
   col->Add(status_label,   0, wxALIGN_CENTER | wxBOTTOM, 12);
   col->Add(reverse_button, 0, wxALIGN_CENTER | wxBOTTOM, 6);

   // lights: a colour box and its label, one row each
   lights_panel = new wxPanel(b);
   wxFlexGridSizer *grid = new wxFlexGridSizer(2, 12, 2);
   for (const char *name : light_names) {
      LightBox *box = new LightBox(lights_panel);
      wxStaticText *label = new wxStaticText(lights_panel, wxID_ANY, wxEmptyString);
      grid->Add(box);
      grid->Add(label, 0, wxALIGN_CENTER_VERTICAL);
      light_rows[name] = { box, label };
   }
   lights_panel->SetSizer(grid);
   col->Add(lights_panel, 0, wxALIGN_LEFT | wxBOTTOM, 6);

   TitledPanel *pull = new TitledPanel(b, "Atom Pull Restraint");
   auto_clear_box = new wxCheckBox(pull->content(), wxID_ANY, "Auto-clear");
   auto_clear_box->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &e) {
      show_auto_clear(e.IsChecked());
      run("on_accept_reject_refinement_atom_pull_autoclear_checkbutton_toggled", e.IsChecked());
   });
   BevelButton *clear = new BevelButton(pull->content(), wxID_ANY, "Clear Atom Pull Restraint");
   clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      run("on_accept_reject_atom_pull_clear_button_clicked");
   });
   pull->content_sizer()->Add(auto_clear_box, 0, wxLEFT | wxBOTTOM, 8);
   pull->content_sizer()->Add(clear, 0, wxLEFT | wxEXPAND, 8);
   col->Add(pull, 0, wxEXPAND | wxBOTTOM, 6);

   TitledPanel *active = new TitledPanel(b, "Active Refinement");
   const std::pair<const char *, const char *> tools[] = {
      { "Flip This Peptide",           "on_accept_reject_flip_this_peptide_button_clicked" },
      { "Flip Next Peptide",           "on_accept_reject_flip_next_peptide_button_clicked" },
      { "Backrub Rotamer Fit",         "on_accept_reject_backrub_rotamer_button_clicked" },
      { "Side-chain Flip 180\xc2\xb0", "on_accept_reject_side_chain_180_flip_clicked" },  // degree sign, UTF-8
      { "Crankshaft Peptide Optimise", "on_accept_reject_crankshaft_button_clicked" }
   };
   for (const auto &t : tools) {
      BevelButton *tool = new BevelButton(active->content(), wxID_ANY, wxString::FromUTF8(t.first));
      std::string action = t.second;
      tool->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent &) { run(action); });
      active->content_sizer()->Add(tool, 0, wxEXPAND | wxLEFT | wxBOTTOM, 4);
   }
   col->Add(active, 0, wxEXPAND);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   BevelButton *accept_button = new BevelButton(panel, wxID_ANY, "Accept");
   BevelButton *reject_button = new BevelButton(panel, wxID_ANY, "Reject");
   accept_button->set_icon(icon("stock-button-ok.png", wxSize(16, 16)));
   reject_button->set_icon(icon("stock-button-cancel.png", wxSize(16, 16)));
   accept_button->SetMinSize(wxSize(84, 28));
   reject_button->SetMinSize(wxSize(80, 28));
   accept_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { accept(); });
   reject_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reject(); });
   buttons->Add(accept_button, 0, wxRIGHT, 12);
   buttons->Add(reject_button, 0);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(body, 1, wxEXPAND | wxALL, 8);
   outer->Add(buttons, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   panel->SetSizer(outer);

   // Return and Escape answer the window, as they did the stock dialog
   Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
      if (e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER) accept();
      else if (e.GetKeyCode() == WXK_ESCAPE) reject();
      else e.Skip();
   });
   // closing the window rejects, as destroying the stock dialog did
   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { reject(); });

   set_warnings(wxEmptyString);
   lights_panel->Hide();
   reverse_button->Hide();
}

void AcceptRejectDialog::present(const wxString &fit_type, bool show_reverse,
                                 bool auto_clear_atom_pull) {
   question_label->SetLabel("Accept " + fit_type + "?");
   reverse_button->Show(show_reverse);
   auto_clear_box->SetValue(auto_clear_atom_pull);
   show_auto_clear(auto_clear_atom_pull);
   fit();
   Show();
}

void AcceptRejectDialog::set_status(const wxString &status) {
   status_label->SetLabel(status);
   status_label->Show(! status.empty());
   fit();
}

void AcceptRejectDialog::set_lights(const std::vector<Light> &lights) {
   if (lights.empty()) return;
   for (auto &row : light_rows) {
      row.second.first->Hide();
      row.second.second->Hide();
   }
   for (const Light &l : lights) {
      auto it = light_rows.find(l.name);
      if (it == light_rows.end()) continue;
      static_cast<LightBox *>(it->second.first)->set_colour(l.colour);
      it->second.second->SetLabel(l.label);
      it->second.first->Show();
      it->second.second->Show();
   }
   lights_panel->Show();
   fit();
}

void AcceptRejectDialog::set_warnings(const wxString &text) {
   warnings_label->SetLabel(text);
   warnings_label->Show(! text.empty());
   fit();
}

void AcceptRejectDialog::accept() {
   finish("on_accept_reject_refinement_accept_button_clicked");
}

void AcceptRejectDialog::reject() {
   finish("on_accept_reject_refinement_reject_button_clicked");
}

// a checked toggle's label takes Coot's "on" colour
void AcceptRejectDialog::show_auto_clear(bool on) {
   auto_clear_box->SetForegroundColour(on ? palette::text_on() : palette::text());
   auto_clear_box->Refresh();
}

void AcceptRejectDialog::run(const std::string &action, bool state) {
   if (hooks.on_action) hooks.on_action(action, state);
}

// Hidden before the action runs: accepting can start work that presents
// the window again.
void AcceptRejectDialog::finish(const std::string &action) {
   if (! IsShown()) return;
   wxPoint where = GetPosition();
   Hide();
   lights_panel->Hide();
   set_warnings(wxEmptyString);
   if (hooks.on_hidden) hooks.on_hidden(where);
   run(action);
}

void AcceptRejectDialog::fit() {
   panel->Layout();
   panel->GetSizer()->Fit(panel);
   Fit();
}

} // namespace wxui

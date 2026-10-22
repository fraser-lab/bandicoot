// src/wxUI/main/choosers.cc

#include "wxUI/main/choosers.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   b->set_icon(icon(icon_file, wxSize(16, 16)));
   b->SetMinSize(wxSize(100, 28));
   return b;
}

// A small grey window over the main one that remembers where it was left
// (each kind for itself), as Bandicoot's GTK choosers did.
class Chooser : public wxFrame {
public:
   Chooser(const wxString &title, wxPoint *where_in)
      : wxFrame(owner, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
                (wxDEFAULT_FRAME_STYLE & ~(wxMAXIMIZE_BOX | wxRESIZE_BORDER)) | wxFRAME_FLOAT_ON_PARENT),
        where(where_in) {
      panel = new wxPanel(this);
      panel->SetBackgroundColour(palette::face());
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { close(); });
   }

   void open(wxSizer *content) {
      panel->SetSizer(content);
      content->SetSizeHints(this);
      if (where && *where != wxDefaultPosition) Move(*where); else CentreOnParent();
      Show();
   }

   // closed by its own buttons or the close box; on_close is told first
   void close() {
      if (closed) return;
      closed = true;
      if (where) *where = GetPosition();
      if (on_close) on_close();
      Destroy();
   }

   std::function<void()> on_close;
   wxPanel *panel;

private:
   wxPoint *where;
   bool closed = false;
};

// ------------------------------------------------ Residue Type

wxPoint residue_type_at = wxDefaultPosition;
Chooser *residue_type = nullptr;

void open_residue_type(bool show_stub) {
   if (residue_type) residue_type->close();
   Chooser *w = new Chooser("Residue Type", &residue_type_at);
   residue_type = w;
   w->on_close = []() { residue_type = nullptr; };

   TitledPanel *body = new TitledPanel(w->panel);
   wxWindow *b = body->content();
   wxCheckBox *stub = nullptr;
   if (show_stub) {
      stub = new wxCheckBox(b, wxID_ANY, "Stub");
      stub->SetToolTip("Change the residue type, but only put in the main-chain atoms and the CB");
      body->content_sizer()->Add(stub, 0, wxBOTTOM, 4);
   }
   const std::vector<std::pair<std::string, const char *>> types = {
      { "ALA", "ALA (A)" }, { "ARG", "ARG (R)" }, { "ASN", "ASN (N)" }, { "ASP", "ASP (D)" },
      { "CYS", "CYS (C)" }, { "GLN", "GLN (Q)" }, { "GLU", "GLU (E)" }, { "GLY", "GLY (G)" },
      { "HIS", "HIS (H)" }, { "ILE", "ILE (I)" }, { "LEU", "LEU (L)" }, { "LYS", "LYS (K)" },
      { "MET", "MET (M)" }, { "MSE", "MSE" },     { "PHE", "PHE (F)" }, { "PRO", "PRO (P)" },
      { "SER", "SER (S)" }, { "THR", "THR (T)" }, { "TRP", "TRP (W)" }, { "TYR", "TYR (Y)" },
      { "VAL", "VAL (V)" }
   };
   for (const auto &t : types) {
      BevelButton *button = new BevelButton(b, wxID_ANY, t.second);
      std::string res_type = t.first;
      button->Bind(wxEVT_BUTTON, [w, stub, res_type](wxCommandEvent &) {
         bool s = stub && stub->GetValue();
         w->close();  // closed first, as Coot's
         bandicoot_core_mutate(res_type, s);
      });
      body->content_sizer()->Add(button, 0, wxEXPAND | wxBOTTOM, 3);
   }
   // a residue's one- or three-letter code (any case) and Return: the same
   // as its button; anything else is refused with a beep
   wxTextCtrl *entry = new wxTextCtrl(b, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      wxDefaultSize, wxTE_PROCESS_ENTER);
   entry->SetToolTip("Type a one- or three-letter residue code, then \"Enter\" to execute");
   entry->Bind(wxEVT_TEXT_ENTER, [w, stub, entry, types](wxCommandEvent &) {
      wxString t = entry->GetValue().Strip(wxString::both).Upper();
      bool s = stub && stub->GetValue();
      std::string res_type;
      if (t.length() == 3)
         for (const auto &ty : types)
            if (t == ty.first) res_type = ty.first;
      if (t.length() == 1) {
         if (bandicoot_core_mutate_by_letter(char(t[0]), s)) { w->close(); return; }
      } else if (! res_type.empty()) {
         w->close();
         bandicoot_core_mutate(res_type, s);
         return;
      }
      wxBell();
   });
   body->content_sizer()->Add(entry, 0, wxEXPAND | wxTOP, 3);

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(body, 1, wxEXPAND | wxALL, 6);
   w->open(col);
   w->SetSize(wxSize(std::max(w->GetSize().x, 190), w->GetSize().y));
}

// ------------------------------------------------ Base

wxPoint base_at = wxDefaultPosition;
Chooser *base = nullptr;

void open_base() {
   if (base) base->close();
   Chooser *w = new Chooser("Base", &base_at);
   base = w;
   // any closing ends the pick, as Coot's chooser
   w->on_close = []() { base = nullptr; bandicoot_core_base_chooser_closed(); };

   TitledPanel *body = new TitledPanel(w->panel);
   const std::pair<const char *, const char *> bases[] = {
      { "A", "Adenine (A)" }, { "C", "Cytosine (C)" }, { "G", "Guanine (G)" },
      { "T", "Thymine (T)" }, { "U", "Uracil (U)" }
   };
   for (const auto &bs : bases) {
      BevelButton *button = new BevelButton(body->content(), wxID_ANY, bs.second);
      std::string code = bs.first;
      button->Bind(wxEVT_BUTTON, [w, code](wxCommandEvent &) {
         bandicoot_core_mutate_base(code);
         w->close();
      });
      body->content_sizer()->Add(button, 0, wxEXPAND | wxBOTTOM, 3);
   }
   BevelButton *cancel = icon_button(w->panel, "Cancel", "stock-button-cancel.png");
   cancel->Bind(wxEVT_BUTTON, [w](wxCommandEvent &) { w->close(); });

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(body, 1, wxEXPAND | wxALL, 6);
   col->Add(cancel, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   w->open(col);
}

// ------------------------------------------------ Pointer atom type

wxPoint pointer_atom_at = wxDefaultPosition;
Chooser *pointer_atom = nullptr;

void open_pointer_atom_type(const std::vector<std::pair<int, std::string>> &models, int imol) {
   if (pointer_atom) pointer_atom->close();
   Chooser *w = new Chooser("Pointer atom type", &pointer_atom_at);
   pointer_atom = w;
   w->on_close = []() { pointer_atom = nullptr; };
   wxWindow *p = w->panel;

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(p, wxID_ANY, "Pointer Atom Type:"), 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);

   TitledPanel *types = new TitledPanel(p);
   // label, and the type the core is given
   const std::pair<const char *, const char *> kinds[] = {
      { "Water", "Water" }, { "Ca (calcium)", "Ca" }, { "Mg", "Mg" }, { "Na", "Na" },
      { "K", "K" }, { "Cl", "Cl" }, { "Br", "Br" }, { "SO4", "SO4" }, { "PO4", "PO4" },
      { "Zn", "ZN" }
   };
   auto radios = std::make_shared<std::vector<std::pair<wxRadioButton *, std::string>>>();
   bool first = true;
   for (const auto &k : kinds) {
      wxRadioButton *r = new wxRadioButton(types->content(), wxID_ANY, k.first, wxDefaultPosition,
                                           wxDefaultSize, first ? wxRB_GROUP : 0);
      if (first) r->SetValue(true);
      first = false;
      radios->push_back({ r, k.second });
      types->content_sizer()->Add(r, 0, wxLEFT | wxBOTTOM, 4);
   }
   wxBoxSizer *other_row = new wxBoxSizer(wxHORIZONTAL);
   BevelButton *other = new BevelButton(types->content(), wxID_ANY, "Other...");
   wxTextCtrl *other_type = new wxTextCtrl(types->content(), wxID_ANY);
   other_type->Enable(false);
   other->Bind(wxEVT_BUTTON, [other_type](wxCommandEvent &) {
      other_type->Enable(true);
      other_type->SetFocus();
   });
   other_row->Add(other, 0, wxRIGHT, 4);
   other_row->Add(other_type, 1, wxALIGN_CENTER_VERTICAL);
   types->content_sizer()->Add(other_row, 0, wxEXPAND | wxTOP, 4);
   col->Add(types, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

   TitledPanel *target = new TitledPanel(p, "Pointer Atom Added to Molecule:");
   wxChoice *molecule = new wxChoice(target->content(), wxID_ANY);
   auto imols = std::make_shared<std::vector<int>>();
   for (const auto &m : models) {
      imols->push_back(m.first);
      molecule->Append(wxString::FromUTF8(m.second));
      if (m.first == imol) molecule->SetSelection(int(imols->size()) - 1);
   }
   if (molecule->GetSelection() == wxNOT_FOUND && ! imols->empty()) molecule->SetSelection(0);
   target->content_sizer()->Add(molecule, 0, wxEXPAND);
   col->Add(target, 0, wxEXPAND | wxALL, 6);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   BevelButton *ok = icon_button(p, "OK", "stock-button-ok.png");
   BevelButton *cancel = icon_button(p, "Cancel", "stock-button-cancel.png");
   ok->Bind(wxEVT_BUTTON, [w, radios, other_type, molecule, imols](wxCommandEvent &) {
      int i = molecule->GetSelection();
      if (i < 0 || i >= int(imols->size())) { wxBell(); return; }
      // a typed-in type wins over the choices, as in Coot
      std::string type = other_type->GetValue().Strip(wxString::both).utf8_string();
      if (type.empty())
         for (const auto &r : *radios)
            if (r.first->GetValue()) type = r.second;
      int target_imol = (*imols)[i];
      w->close();
      bandicoot_core_place_atom(target_imol, type);
   });
   cancel->Bind(wxEVT_BUTTON, [w](wxCommandEvent &) { w->close(); });
   buttons->Add(ok, 0, wxRIGHT, 6);
   buttons->Add(cancel);
   col->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   w->open(col);
   w->SetSize(wxSize(std::max(w->GetSize().x, 380), w->GetSize().y));
}

// ------------------------------------------------ Add Alternate Conformation

wxPoint add_alt_conf_at = wxDefaultPosition;
Chooser *add_alt_conf = nullptr;

void open_add_alt_conf(int split_type) {
   if (add_alt_conf) {
      add_alt_conf->on_close = []() { add_alt_conf = nullptr; };
      add_alt_conf->close();
   }
   Chooser *w = new Chooser("Add Alternate Conformation", &add_alt_conf_at);
   add_alt_conf = w;
   // closing without a pick stops the pick
   w->on_close = []() { add_alt_conf = nullptr; bandicoot_core_cancel_add_alt_conf(); };
   wxWindow *p = w->panel;

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticText(p, wxID_ANY, "Add Alternate Conformation"), 0,
            wxALIGN_CENTER | wxTOP | wxBOTTOM, 8);
   TitledPanel *kinds = new TitledPanel(p);
   const char *labels[] = { "Split Single Residue at C-alpha", "Split All of a Single Residue" };
   for (int i = 0; i < 2; i++) {
      wxRadioButton *r = new wxRadioButton(kinds->content(), wxID_ANY, labels[i], wxDefaultPosition,
                                           wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
      r->SetValue(split_type == i);
      r->Bind(wxEVT_RADIOBUTTON, [i](wxCommandEvent &) { bandicoot_core_set_alt_conf_split_type(i); });
      kinds->content_sizer()->Add(r, 0, wxLEFT | wxBOTTOM, 4);
   }
   col->Add(kinds, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);
   BevelButton *cancel = icon_button(p, "Cancel", "stock-button-cancel.png");
   cancel->Bind(wxEVT_BUTTON, [w](wxCommandEvent &) { w->close(); });
   col->Add(cancel, 0, wxALIGN_RIGHT | wxALL, 8);
   w->open(col);
}

// the pick is made: the window goes without cancelling it
void add_alt_conf_done() {
   if (! add_alt_conf) return;
   add_alt_conf->on_close = []() { add_alt_conf = nullptr; };
   add_alt_conf->close();
}

} // namespace

void install_choosers(wxWindow *parent) {
   owner = parent;
   bandicoot_chooser_view v;
   v.residue_type = [](bool show_stub) { open_residue_type(show_stub); };
   v.base = []() { open_base(); };
   v.pointer_atom_type = [](const std::vector<std::pair<int, std::string>> &models, int imol) {
      open_pointer_atom_type(models, imol);
   };
   v.add_alt_conf = [](int split_type) { open_add_alt_conf(split_type); };
   v.add_alt_conf_done = []() { add_alt_conf_done(); };
   bandicoot_core_set_chooser_view(v);
}

// The chosen option's command runs as it is chosen, the first time once the
// script that asked has returned. Closing the window counts as Reject, which
// puts back what the options changed.
void show_radio_choice(wxWindow *parent, const bandicoot_radio_choice &c) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, wxString::FromUTF8(c.title));
   d->SetBackgroundColour(palette::face());
   std::vector<wxString> labels;
   for (const auto &choice : c.choices) labels.push_back(wxString::FromUTF8(choice.first));
   RadioGroup *options = new RadioGroup(d, wxEmptyString, labels, c.selected);
   std::vector<std::string> commands;
   for (const auto &choice : c.choices) commands.push_back(choice.second);
   options->on_change = [commands](int i) {
      if (i >= 0 && i < int(commands.size())) bandicoot_core_run_python(commands[i]);
   };

   wxStdDialogButtonSizer *buttons = new wxStdDialogButtonSizer();
   wxButton *accept = new wxButton(d, wxID_OK, wxString::FromUTF8(c.accept_label.empty() ? "OK" : c.accept_label));
   buttons->AddButton(accept);
   if (! c.reject_label.empty())
      buttons->AddButton(new wxButton(d, wxID_CANCEL, wxString::FromUTF8(c.reject_label)));
   buttons->Realize();

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(options, 1, wxEXPAND | wxBOTTOM, 10);
   col->Add(buttons, 0, wxEXPAND);
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);

   std::string accept_command = c.accept_command, reject_command = c.reject_command;
   d->Bind(wxEVT_BUTTON, [d, accept_command](wxCommandEvent &) {
      bandicoot_core_run_python(accept_command);
      d->Destroy();
   }, wxID_OK);
   auto reject = [d, reject_command]() {
      bandicoot_core_run_python(reject_command);
      d->Destroy();
   };
   d->Bind(wxEVT_BUTTON, [reject](wxCommandEvent &) { reject(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [reject](wxCloseEvent &) { reject(); });
   d->CentreOnParent();
   d->Show();
   std::string first = commands[options->get_selection()];
   d->CallAfter([first]() { bandicoot_core_run_python(first); });
}

// Each button runs its command and the window stays; Close (or the close box)
// runs the close command and ends it.
void show_button_choice(wxWindow *parent, const bandicoot_button_choice &c) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, wxString::FromUTF8(c.title), wxDefaultPosition, wxDefaultSize,
                              wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
   d->SetBackgroundColour(palette::face());
   wxScrolledWindow *list = new wxScrolledWindow(d, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
   list->SetBackgroundColour(palette::face());
   list->SetScrollRate(0, 10);
   wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
   for (const auto &b : c.buttons) {
      BevelButton *button = new BevelButton(list, wxID_ANY, wxString::FromUTF8(b.first));
      std::string command = b.second;
      button->Bind(wxEVT_BUTTON, [command](wxCommandEvent &) { bandicoot_core_run_python(command); });
      rows->Add(button, 0, wxEXPAND | wxBOTTOM, 4);
   }
   list->SetSizer(rows);
   wxSize all = rows->CalcMin();
   list->SetMinSize(wxSize(std::max(all.x + 20, 300), std::min(all.y + 4, 420)));

   wxButton *close = new wxButton(d, wxID_CANCEL, wxString::FromUTF8(c.close_label.empty() ? "Close" : c.close_label));
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(list, 1, wxEXPAND | wxBOTTOM, 10);
   col->Add(close, 0, wxALIGN_RIGHT);
   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 10);
   d->SetSizerAndFit(outer);

   std::string close_command = c.close_command;
   auto finish = [d, close_command]() {
      bandicoot_core_run_python(close_command);
      d->Destroy();
   };
   d->Bind(wxEVT_BUTTON, [finish](wxCommandEvent &) { finish(); }, wxID_CANCEL);
   d->Bind(wxEVT_CLOSE_WINDOW, [finish](wxCloseEvent &) { finish(); });
   d->CentreOnParent();
   d->Show();
}

} // namespace wxui

// src/wxUI/main/go-to-atom.cc

#include "wxUI/main/go-to-atom.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/choice.h>
#include <wx/dataview.h>
#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treelist.h>

#include <algorithm>
#include <vector>

namespace wxui {

namespace {

class GoToAtomWindow : public wxFrame {
public:
   explicit GoToAtomWindow(wxWindow *parent);

   void open();
   // from the core
   void target_changed();
   void molecules_changed(int imol);
   void model_changed(int imol);

private:
   wxChoice *molecule_choice;
   wxTextCtrl *chain_entry, *residue_entry, *atom_entry;
   wxTreeListCtrl *residue_tree;
   wxListCtrl *atom_list;

   std::vector<int> imols;                    // molecule_choice's entries
   std::vector<bandicoot_goto_chain> chains;  // what residue_tree shows
   int tree_imol = -1;
   bandicoot_goto_residue list_residue;       // whose atoms atom_list shows
   std::vector<bandicoot_goto_atom> atoms;

   int chosen_imol() const;
   void fill_molecules(int select_imol);
   void fill_residues(int imol);
   void fill_atoms(const bandicoot_goto_residue &r);
   void fit_residue_column();
   const bandicoot_goto_residue *residue_of(wxTreeListItem item) const;
   void residue_picked(wxTreeListItem item, bool centre);
   void atom_picked(long index, bool centre);
   void apply();
   void close();
};

// a tree row's place in chains: chain index, residue index (-1 for a chain)
class RowData : public wxClientData {
public:
   RowData(int c, int r) : chain(c), residue(r) {}
   int chain, residue;
};

wxTextCtrl *entry(wxWindow *parent) {
   wxTextCtrl *t = new wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxSize(60, -1), wxTE_PROCESS_ENTER);
   return t;
}

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   b->set_icon(icon(icon_file, wxSize(16, 16)));
   return b;
}

GoToAtomWindow::GoToAtomWindow(wxWindow *parent)
   : wxFrame(parent, wxID_ANY, "Go To Atom...", wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT) {

   wxPanel *panel = new wxPanel(this);
   panel->SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   // the target
   TitledPanel *define = new TitledPanel(panel);
   wxWindow *d = define->content();
   define->content_sizer()->Add(new wxStaticText(d, wxID_ANY, "Define an Atom for Centering:"),
                                0, wxALIGN_CENTER | wxBOTTOM, 6);
   wxBoxSizer *mol_row = new wxBoxSizer(wxHORIZONTAL);
   molecule_choice = new wxChoice(d, wxID_ANY);
   mol_row->Add(molecule_choice, 1, wxALIGN_CENTER_VERTICAL);
   mol_row->Add(new wxStaticText(d, wxID_ANY, "Molecule"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
   define->content_sizer()->Add(mol_row, 0, wxEXPAND | wxBOTTOM, 6);

   wxFlexGridSizer *fields = new wxFlexGridSizer(3, 4, 8);
   chain_entry   = entry(d);
   residue_entry = entry(d);
   atom_entry    = entry(d);
   BevelButton *update = new BevelButton(d, wxID_ANY, "Update from Current Position");
   fields->Add(chain_entry);
   fields->Add(new wxStaticText(d, wxID_ANY, "Chain"), 0, wxALIGN_CENTER_VERTICAL);
   fields->Add(update, 0, wxLEFT, 8);
   fields->Add(residue_entry);
   fields->Add(new wxStaticText(d, wxID_ANY, "Residue Number"), 0, wxALIGN_CENTER_VERTICAL);
   fields->AddSpacer(0);
   fields->Add(atom_entry);
   fields->Add(new wxStaticText(d, wxID_ANY, "Atom Name"), 0, wxALIGN_CENTER_VERTICAL);
   fields->AddSpacer(0);
   define->content_sizer()->Add(fields);
   col->Add(define, 0, wxEXPAND | wxBOTTOM, 6);

   col->Add(new wxStaticLine(panel), 0, wxEXPAND | wxBOTTOM, 6);

   wxBoxSizer *steps = new wxBoxSizer(wxHORIZONTAL);
   BevelButton *previous = icon_button(panel, "Previous Residue", "stock-button-previous.png");
   BevelButton *next     = icon_button(panel, "Next Residue", "stock-button-next.png");
   steps->Add(previous, 1, wxRIGHT, 6);
   steps->Add(next, 1);
   col->Add(steps, 0, wxEXPAND | wxBOTTOM, 6);

   // the residue tree beside the selected residue's atoms
   wxBoxSizer *lists = new wxBoxSizer(wxHORIZONTAL);
   residue_tree = new wxTreeListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(200, 179),
                                     wxTL_SINGLE);
   residue_tree->AppendColumn("Chains");
   atom_list = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(200, 179),
                              wxLC_REPORT | wxLC_SINGLE_SEL);
   atom_list->AppendColumn("Atoms");
   for (wxWindow *w : { static_cast<wxWindow *>(residue_tree->GetDataView()),
                        static_cast<wxWindow *>(atom_list) })
      w->SetBackgroundColour(palette::list_background());
   lists->Add(residue_tree, 1, wxEXPAND | wxRIGHT, 6);
   lists->Add(atom_list, 1, wxEXPAND);
   col->Add(lists, 1, wxEXPAND | wxBOTTOM, 6);

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   BevelButton *apply_button = icon_button(panel, "Apply", "stock-button-apply.png");
   BevelButton *close_button = icon_button(panel, "Close", "stock-menu-close.png");
   buttons->Add(apply_button, 1, wxRIGHT, 6);
   buttons->Add(close_button, 1);
   col->Add(buttons, 0, wxEXPAND);

   wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
   outer->Add(col, 1, wxEXPAND | wxALL, 8);
   panel->SetSizer(outer);
   outer->SetSizeHints(this);

   molecule_choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) {
      int imol = chosen_imol();
      bandicoot_core_set_goto_molecule(imol);
      if (imol != tree_imol) fill_residues(imol);
   });
   for (wxTextCtrl *t : { chain_entry, residue_entry, atom_entry })
      t->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { apply(); });
   update->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_goto_from_current_position(); });
   previous->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_goto_next_residue(false); });
   next->Bind(wxEVT_BUTTON, [](wxCommandEvent &) { bandicoot_core_goto_next_residue(true); });
   apply_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply(); });
   close_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { close(); });
   Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { close(); });

   // one click picks; a double click also goes there
   residue_tree->Bind(wxEVT_TREELIST_SELECTION_CHANGED, [this](wxTreeListEvent &e) {
      residue_picked(e.GetItem(), false);
   });
   residue_tree->Bind(wxEVT_TREELIST_ITEM_ACTIVATED, [this](wxTreeListEvent &e) {
      residue_picked(e.GetItem(), true);
   });
   residue_tree->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
      e.Skip();
      CallAfter([this]() { fit_residue_column(); });
   });
   atom_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent &e) {
      atom_picked(e.GetIndex(), false);
   });
   atom_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent &e) {
      atom_picked(e.GetIndex(), true);
   });
}

void GoToAtomWindow::open() {
   if (! IsShown()) {
      int x, y;
      if (bandicoot_core_goto_position(x, y))
         Move(x, y);
      else
         CentreOnParent();
      // shown first: the fills only act on a window that is up
      Show();
      bandicoot_goto_target t = bandicoot_core_goto_target(); // fills an unset target in
      fill_molecules(t.imol);
      fill_residues(chosen_imol());
      target_changed();
   }
   Raise();
}

void GoToAtomWindow::target_changed() {
   if (! IsShown()) return;
   bandicoot_goto_target t = bandicoot_core_goto_target();
   chain_entry->ChangeValue(wxString::FromUTF8(t.chain));
   residue_entry->ChangeValue(wxString::Format("%d", t.resno) + wxString::FromUTF8(t.ins_code));
   wxString atom = wxString::FromUTF8(t.atom_name);
   if (! t.alt_conf.empty()) atom += "," + wxString::FromUTF8(t.alt_conf);
   atom_entry->ChangeValue(atom);
}

void GoToAtomWindow::molecules_changed(int imol) {
   if (! IsShown()) return;
   int before = chosen_imol();
   fill_molecules(imol >= 0 ? imol : before);
   if (chosen_imol() != tree_imol || imol >= 0)
      fill_residues(chosen_imol());
}

void GoToAtomWindow::model_changed(int imol) {
   if (! IsShown()) return;
   if (imol == chosen_imol())
      fill_residues(imol);
}

int GoToAtomWindow::chosen_imol() const {
   int i = molecule_choice->GetSelection();
   return i >= 0 && i < int(imols.size()) ? imols[i] : -1;
}

// keeps select_imol chosen if it is offered, else the first molecule
void GoToAtomWindow::fill_molecules(int select_imol) {
   molecule_choice->Clear();
   imols.clear();
   int selection = 0;
   for (const auto &m : bandicoot_core_goto_molecules()) {
      if (m.first == select_imol) selection = int(imols.size());
      imols.push_back(m.first);
      molecule_choice->Append(wxString::FromUTF8(m.second));
   }
   if (! imols.empty()) molecule_choice->SetSelection(selection);
}

void GoToAtomWindow::fill_residues(int imol) {
   tree_imol = imol;
   residue_tree->DeleteAllItems();
   atom_list->DeleteAllItems();
   atoms.clear();
   chains = bandicoot_core_goto_residues(imol);
   wxTreeListItem root = residue_tree->GetRootItem();
   for (size_t c = 0; c < chains.size(); c++) {
      wxTreeListItem ci = residue_tree->AppendItem(root, wxString::FromUTF8(chains[c].label),
                                                   -1, -1, new RowData(int(c), -1));
      for (size_t r = 0; r < chains[c].residues.size(); r++)
         residue_tree->AppendItem(ci, wxString::FromUTF8(chains[c].residues[r].label),
                                  -1, -1, new RowData(int(c), int(r)));
   }
   fit_residue_column();
}

// The column is as wide as its longest label or the list, whichever is
// wider; left alone it keeps the width it had before the refill.
void GoToAtomWindow::fit_residue_column() {
   const int indent = 40; // points for the expander and the child indent
   int w = residue_tree->WidthFor("Chains");
   for (const bandicoot_goto_chain &c : chains) {
      w = std::max(w, residue_tree->WidthFor(wxString::FromUTF8(c.label)));
      for (const bandicoot_goto_residue &r : c.residues)
         w = std::max(w, residue_tree->WidthFor(wxString::FromUTF8(r.label)));
   }
   w = std::max(w + indent, residue_tree->GetClientSize().x);
   residue_tree->SetColumnWidth(0, w);
}

void GoToAtomWindow::fill_atoms(const bandicoot_goto_residue &r) {
   list_residue = r;
   atom_list->DeleteAllItems();
   atoms = bandicoot_core_goto_atoms(tree_imol, r.chain, r.resno, r.ins_code);
   for (size_t i = 0; i < atoms.size(); i++)
      atom_list->InsertItem(long(i), wxString::FromUTF8(atoms[i].label));
   atom_list->SetColumnWidth(0, wxLIST_AUTOSIZE);
}

const bandicoot_goto_residue *GoToAtomWindow::residue_of(wxTreeListItem item) const {
   if (! item.IsOk()) return nullptr;
   RowData *d = static_cast<RowData *>(residue_tree->GetItemData(item));
   if (! d || d->residue < 0 || d->chain >= int(chains.size())) return nullptr;
   const std::vector<bandicoot_goto_residue> &rs = chains[d->chain].residues;
   return d->residue < int(rs.size()) ? &rs[d->residue] : nullptr;
}

void GoToAtomWindow::residue_picked(wxTreeListItem item, bool centre) {
   const bandicoot_goto_residue *r = residue_of(item);
   if (! r) return;  // a chain row
   bandicoot_goto_residue picked = *r;
   bandicoot_core_goto_residue(tree_imol, picked, centre);
   fill_atoms(picked);
}

void GoToAtomWindow::atom_picked(long index, bool centre) {
   if (index < 0 || index >= long(atoms.size())) return;
   bandicoot_core_goto_atom(tree_imol, list_residue, atoms[index], centre);
}

void GoToAtomWindow::apply() {
   bandicoot_core_goto_fields(chain_entry->GetValue().utf8_string(),
                              residue_entry->GetValue().utf8_string(),
                              atom_entry->GetValue().utf8_string());
}

void GoToAtomWindow::close() {
   wxPoint p = GetPosition();
   bandicoot_core_save_goto_position(p.x, p.y);
   Hide();
}

GoToAtomWindow *window = nullptr;

} // namespace

void install_go_to_atom(wxWindow *parent) {
   window = new GoToAtomWindow(parent);
   bandicoot_goto_view v;
   v.open = []() { window->open(); return true; };
   v.target_changed = []() { window->target_changed(); };
   // after the core has finished adding or closing the molecule
   v.molecules_changed = [](int imol) {
      window->CallAfter([imol]() { window->molecules_changed(imol); });
   };
   v.model_changed = [](int imol) { window->model_changed(imol); };
   bandicoot_core_set_goto_view(v);
}

void show_go_to_atom() {
   if (window) window->open();
}

} // namespace wxui

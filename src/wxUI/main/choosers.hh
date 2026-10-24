// src/wxUI/main/choosers.hh
//
// The small choosers the core opens from the sidebar: Residue Type (Simple
// Mutate, Mutate & Auto Fit), Base (Simple Mutate on a nucleotide), Pointer
// atom type (Place Atom At Pointer), Add Alternate Conformation; and the
// molecules a dictionary import applies to.

#ifndef BANDICOOT_WXUI_CHOOSERS_HH
#define BANDICOOT_WXUI_CHOOSERS_HH

#include <string>
#include <utility>
#include <vector>

class wxWindow;
struct bandicoot_radio_choice;
struct bandicoot_button_choice;

namespace wxui {

// Connects the choosers to the core; they open when the core asks.
void install_choosers(wxWindow *parent);

// A script's choice among radio buttons, as the core hands it over.
void show_radio_choice(wxWindow *parent, const bandicoot_radio_choice &choice);
// A script's column of buttons, as the core hands it over.
void show_button_choice(wxWindow *parent, const bandicoot_button_choice &choice);
// Molecules (imol, label) to tick, all ticked at first; waits for OK. The
// ticked ones, or none if cancelled.
std::vector<int> choose_molecules(wxWindow *parent, const std::string &title, const std::string &header,
                                  const std::vector<std::pair<int, std::string>> &molecules);

} // namespace wxui

#endif // BANDICOOT_WXUI_CHOOSERS_HH

// src/wxUI/main/choosers.hh
//
// The small choosers the core opens from the sidebar: Residue Type (Simple
// Mutate, Mutate & Auto Fit), Base (Simple Mutate on a nucleotide), Pointer
// atom type (Place Atom At Pointer), Add Alternate Conformation.

#ifndef BANDICOOT_WXUI_CHOOSERS_HH
#define BANDICOOT_WXUI_CHOOSERS_HH

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

} // namespace wxui

#endif // BANDICOOT_WXUI_CHOOSERS_HH

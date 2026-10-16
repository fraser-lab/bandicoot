// src/wxUI/main/choosers.hh
//
// The small choosers the core opens from the sidebar: Residue Type (Simple
// Mutate, Mutate & Auto Fit), Base (Simple Mutate on a nucleotide), Pointer
// atom type (Place Atom At Pointer), Add Alternate Conformation.

#ifndef BANDICOOT_WXUI_CHOOSERS_HH
#define BANDICOOT_WXUI_CHOOSERS_HH

class wxWindow;

namespace wxui {

// Connects the choosers to the core; they open when the core asks.
void install_choosers(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_CHOOSERS_HH

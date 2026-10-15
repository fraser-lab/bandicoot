// src/wxUI/main/go-to-atom.hh
//
// Coot's "Go To Atom..." window: shows and edits the core's go-to target
// (molecule, chain, residue, atom), steps to the next or previous residue, and
// lists a molecule's residues and a residue's atoms to pick from. One window,
// kept and reused; the core asks it to follow changes to the target and to
// the molecules.

#ifndef BANDICOOT_WXUI_GO_TO_ATOM_HH
#define BANDICOOT_WXUI_GO_TO_ATOM_HH

class wxWindow;

namespace wxui {

// Creates the window on first use and connects it to the core.
void install_go_to_atom(wxWindow *parent);
// Shows it (F6, the toolbar button).
void show_go_to_atom();

} // namespace wxui

#endif // BANDICOOT_WXUI_GO_TO_ATOM_HH

// src/wxUI/main/refine-dialogs.hh
//
// The refinement windows the sidebar opens: Refinement and Regularization
// Parameters (R/RC), Select Map for Fitting (Map, and any tool that needs a
// refinement map), Run Refmac, and Fixed Atoms for Refinement.

#ifndef BANDICOOT_WXUI_REFINE_DIALOGS_HH
#define BANDICOOT_WXUI_REFINE_DIALOGS_HH

class wxWindow;

namespace wxui {

// Shows the parameters window, made afresh with the values in use.
void show_refine_params(wxWindow *parent);

// Asks for the refinement map and its weight and returns when answered, as
// Coot's chooser did: the tools that ask go on with what was chosen.
void select_refinement_map(wxWindow *parent);

// Run Refmac: asks for the model and the MTZ file, then starts the run.
void run_refmac(wxWindow *parent);

// Shows the fixed-atoms window (kept and reused).
void show_fixed_atoms(wxWindow *parent);
// A fixed-atom pick is done: its button is let go.
void release_fixed_atom_toggles();

} // namespace wxui

#endif // BANDICOOT_WXUI_REFINE_DIALOGS_HH

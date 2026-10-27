// src/wxUI/main/validation-graphs.hh
//
// The windows of the Validate menu's residue graphs (Geometry, Peptide omega,
// Temp. fact. variance, Average Temp. fact., Rotamer, Density fit and NCS
// Differences): one bar per residue along each chain, coloured and sized by
// how bad it is, kept up to date by the core as the model changes.

#ifndef BANDICOOT_WXUI_VALIDATION_GRAPHS_HH
#define BANDICOOT_WXUI_VALIDATION_GRAPHS_HH

class wxWindow;

namespace wxui {

// Lets the core open and draw the graph windows.
void install_validation_graphs(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_VALIDATION_GRAPHS_HH

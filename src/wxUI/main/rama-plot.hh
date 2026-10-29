// src/wxUI/main/rama-plot.hh
//
// Validate > Ramachandran Plot and Kleywegt Plot: Coot's Dynarama window. Each
// residue's phi and psi over the background for its type, kept up to date as
// the model changes, with the current residue marked.

#ifndef BANDICOOT_WXUI_RAMA_PLOT_HH
#define BANDICOOT_WXUI_RAMA_PLOT_HH

#include <string>
#include <vector>

struct bandicoot_rama_point;

class wxWindow;

namespace wxui {

// A plot of model imol, in place of one already open for it.
void show_rama_plot(wxWindow *parent, int imol);
// A Kleywegt plot: imol1's phi and psi against imol2's, one chain against
// another or (by_chain false) residue by residue.
void show_kleywegt_plot(wxWindow *parent, int imol1, int imol2, bool by_chain, const std::string &chain1,
                        const std::string &chain2);

// Edit Backbone Torsions' plot of the two residues it moves, made into its
// window (one at a time), and its points as they now are.
wxWindow *make_backbone_plot(wxWindow *parent);
void show_backbone_points(const std::vector<bandicoot_rama_point> &points);

// Lets the core update the open plots.
void install_rama_plot(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_RAMA_PLOT_HH

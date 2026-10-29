// src/wxUI/main/pandda-inspect.hh
//
// PanDDA Inspect (Calculate > PanDDA Inspect): a window of its own for
// stepping through a PanDDA folder's events, toggling their maps, modelling
// ligands and recording annotations. Two layouts, Basic and Full, switched
// from its bottom bar. Everything it does goes through
// python/bandicoot_pandda.py.

#ifndef BANDICOOT_WXUI_PANDDA_INSPECT_HH
#define BANDICOOT_WXUI_PANDDA_INSPECT_HH

#include <string>

class wxWindow;

namespace wxui {

// Opens the window (or brings it forward), loading folder if one is given.
void show_pandda_inspect(wxWindow *parent, const std::string &folder = std::string());

// Lets the core open the window (the --pandda launcher).
void install_pandda_inspect(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_PANDDA_INSPECT_HH

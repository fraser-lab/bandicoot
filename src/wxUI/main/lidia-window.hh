// src/wxUI/main/lidia-window.hh
//
// Lidia, Coot's ligand builder: a window per builder the core opens, showing
// the builder's own drawing and passing it the pointer and the tool choices.

#ifndef BANDICOOT_WXUI_LIDIA_WINDOW_HH
#define BANDICOOT_WXUI_LIDIA_WINDOW_HH

class wxWindow;

namespace wxui {

// From then on the core's builders open in wx windows owned by `owner`.
void install_lidia(wxWindow *owner);

} // namespace wxui

#endif // BANDICOOT_WXUI_LIDIA_WINDOW_HH

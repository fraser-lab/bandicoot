// src/wxUI/main/map-properties.hh
//
// Display Manager > Properties: a map's cell and symmetry, display style,
// contour level (with its density histogram), colour and skeleton.

#ifndef BANDICOOT_WXUI_MAP_PROPERTIES_HH
#define BANDICOOT_WXUI_MAP_PROPERTIES_HH

class wxWindow;

namespace wxui {

// One window per map: opening it again raises the one there is.
void show_map_properties(wxWindow *parent, int imol);

} // namespace wxui

#endif // BANDICOOT_WXUI_MAP_PROPERTIES_HH

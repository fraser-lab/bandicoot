// src/wxUI/assets/bevel.hh
//
// Drawing primitives shared by the custom controls: GTK 2 style frames, the
// radio and check indicators, and the dotted focus outline. Lines are one point
// wide, so they stay crisp at any backing scale.

#ifndef BANDICOOT_WXUI_BEVEL_HH
#define BANDICOOT_WXUI_BEVEL_HH

#include <wx/dc.h>
#include <wx/gdicmn.h>

namespace wxui {

enum class FrameStyle {
   Raised,  // highlight top-left; shadow and outer black bottom-right
   Sunken,  // shadow and inner black top-left; highlight bottom-right
   Etched   // a groove: shadow then highlight on every side
};

// Fill rect with face, then frame it (2 points thick). shadow is the inner
// shadow line; the outer one is black.
void draw_frame(wxDC &dc, const wxRect &rect, FrameStyle style,
                const wxColour &face, const wxColour &shadow);

// The area inside a frame drawn by draw_frame().
wxRect frame_interior(const wxRect &rect);

enum class Indicator { None, Radio, Check };
const int indicator_size = 12; // points

void draw_indicator(wxDC &dc, const wxRect &box, Indicator kind, bool on);

void draw_focus_outline(wxDC &dc, const wxRect &rect, const wxColour &colour);

} // namespace wxui

#endif // BANDICOOT_WXUI_BEVEL_HH

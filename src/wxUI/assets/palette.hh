// src/wxUI/assets/palette.hh
//
// Colours of the custom controls, matching Coot 0.9 as Bandicoot renders it.
// Where cootrc styles a state, the value comes from there; the rest are GTK 2's
// own defaults, under which a bevel's shadow is its face darkened to 70% and
// its highlight is white.

#ifndef BANDICOOT_WXUI_PALETTE_HH
#define BANDICOOT_WXUI_PALETTE_HH

#include <wx/colour.h>
#include <wx/gdicmn.h>

namespace wxui {
namespace palette {

inline wxColour face()             { return wxColour(0xdc, 0xdc, 0xdc); }
inline wxColour face_pressed()     { return wxColour(0xc4, 0xc4, 0xc4); }
inline wxColour face_hover()       { return wxColour(0xed, 0xed, 0xf5); } // cootrc "button"
inline wxColour face_hover_toggle(){ return wxColour(0xeb, 0xeb, 0xfa); } // cootrc "toggle_button"
inline wxColour face_on()          { return wxColour(0xe6, 0xcc, 0xe6); } // cootrc toggle bg[ACTIVE]

inline wxColour text()             { return *wxBLACK; }
inline wxColour text_on()          { return wxColour(0x33, 0x00, 0x00); } // cootrc toggle fg[ACTIVE]
inline wxColour text_disabled()    { return wxColour(0x82, 0x82, 0x82); }

inline wxColour highlight()        { return *wxWHITE; }
inline wxColour outer_shadow()     { return *wxBLACK; }
// GTK 2's shadow for a given face colour
inline wxColour shadow_of(const wxColour &c) {
   return wxColour(c.Red() * 7 / 10, c.Green() * 7 / 10, c.Blue() * 7 / 10);
}
// the sidebar's hovered buttons are drawn with a bluer shadow than the rule gives
inline wxColour toolbar_hover_shadow() { return wxColour(0x87, 0x87, 0xc9); }

// lists and trees, a little lighter than the face so rows stand apart from it
inline wxColour list_background()  { return wxColour(0xe8, 0xe8, 0xe8); }

inline wxColour indicator_ring()   { return *wxBLACK; }
inline wxColour indicator_fill()   { return *wxWHITE; }

} // namespace palette
} // namespace wxui

#endif // BANDICOOT_WXUI_PALETTE_HH

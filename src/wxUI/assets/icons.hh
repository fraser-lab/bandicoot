// src/wxUI/assets/icons.hh
//
// Icons from Bandicoot's pixmaps directory (SVG or PNG), sized in points and
// kept sharp at any backing scale.

#ifndef BANDICOOT_WXUI_ICONS_HH
#define BANDICOOT_WXUI_ICONS_HH

#include <wx/bmpbndl.h>
#include <wx/string.h>

namespace wxui {

// Where icons are looked up; set once at startup.
void set_icon_directory(const wxString &dir);

// An empty bundle when the file is missing (noted on the terminal).
wxBitmapBundle icon(const wxString &file_name, const wxSize &size);

} // namespace wxui

#endif // BANDICOOT_WXUI_ICONS_HH

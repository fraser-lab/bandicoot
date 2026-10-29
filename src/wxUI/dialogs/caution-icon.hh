// src/wxUI/dialogs/caution-icon.hh
//
// macOS's own caution sign (the yellow triangle its alerts carry).

#ifndef BANDICOOT_WXUI_CAUTION_ICON_HH
#define BANDICOOT_WXUI_CAUTION_ICON_HH

#include <wx/bmpbndl.h>

namespace wxui {

// The sign at size x size points; not IsOk() if the system has none.
wxBitmapBundle caution_icon(int size);

} // namespace wxui

#endif // BANDICOOT_WXUI_CAUTION_ICON_HH

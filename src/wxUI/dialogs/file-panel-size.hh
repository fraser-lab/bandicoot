// src/wxUI/dialogs/file-panel-size.hh
//
// The size macOS's open and save panels first appear at. Left alone they
// open nearly as large as the screen; Bandicoot's open at a quarter of the
// screen's width and a third of its height, or their smallest size if that
// is larger (they can still be resized).

#ifndef BANDICOOT_WXUI_FILE_PANEL_SIZE_HH
#define BANDICOOT_WXUI_FILE_PANEL_SIZE_HH

namespace wxui {

// Call before showing a panel; the limit applies to every panel from then on.
void limit_file_panel_size();

} // namespace wxui

#endif // BANDICOOT_WXUI_FILE_PANEL_SIZE_HH

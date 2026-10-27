// src/wxUI/main/delete-waters.hh
//
// Delete item (the sidebar's Delete...), Find Waters, and the unmodelled
// blobs of density: Validate's search for them, and the list it and Find
// Waters can leave.

#ifndef BANDICOOT_WXUI_DELETE_WATERS_HH
#define BANDICOOT_WXUI_DELETE_WATERS_HH

class wxWindow;

namespace wxui {

// Connects Delete item and the blob list to the core, which opens them.
void install_delete_waters(wxWindow *parent);

// Shows Find Waters (the sidebar's Find Waters...).
void show_find_waters(wxWindow *parent);

// Shows Validate > Unmodelled blobs.
void show_unmodelled_blobs(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_DELETE_WATERS_HH

// src/wxUI/main/model-dialogs.hh
//
// The windows the core opens after a modelling pick, to edit the moving
// atoms: Rotation & Translation, Select Rotamer, Edit Chi Angles (with its
// help), Edit Backbone Torsions. Each ends with the moving atoms accepted
// or discarded.

#ifndef BANDICOOT_WXUI_MODEL_DIALOGS_HH
#define BANDICOOT_WXUI_MODEL_DIALOGS_HH

class wxWindow;

namespace wxui {

// Connects the windows to the core; they open when the core asks.
void install_model_dialogs(wxWindow *parent);

// A key pressed in the graphics: Return, "." and "," answer Select Rotamer
// while it is up, as they did Coot's. True if the key was taken.
bool model_dialogs_take_key(int key_code);

} // namespace wxui

#endif // BANDICOOT_WXUI_MODEL_DIALOGS_HH

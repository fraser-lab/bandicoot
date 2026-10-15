// src/wxUI/dialogs/accept-reject-dialog.hh
//
// AcceptRejectDialog: Coot's free-floating "Accept Refinement?" window. It
// shows a pending fit's status and geometry lights and offers Accept, Reject
// and the tools that act on the moving atoms. Every control reports through
// on_action with the Coot action name it stands for; the owner runs it.
//
// The window is kept and reused: Accept, Reject and closing it hide it.

#ifndef BANDICOOT_WXUI_ACCEPT_REJECT_DIALOG_HH
#define BANDICOOT_WXUI_ACCEPT_REJECT_DIALOG_HH

#include <wx/colour.h>
#include <wx/frame.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

class wxCheckBox;
class wxPanel;
class wxStaticText;

namespace wxui {

class AcceptRejectDialog : public wxFrame {
public:
   struct Light {
      wxString name;    // "Bonds", ...
      wxString label;   // shown beside the colour box
      wxColour colour;
   };
   struct Hooks {
      // state: a check box's new state; true for a button
      std::function<void(const std::string &action, bool state)> on_action;
      // the window was hidden where it stood
      std::function<void(const wxPoint &where)> on_hidden;
   };

   AcceptRejectDialog(wxWindow *parent, const Hooks &hooks);

   // A fit is waiting: title it ("Accept Refinement?"), show or hide Reverse,
   // and show the window.
   void present(const wxString &fit_type, bool show_reverse, bool auto_clear_atom_pull);
   void set_status(const wxString &status);
   void set_lights(const std::vector<Light> &lights);  // empty: unchanged
   void set_warnings(const wxString &text);           // empty: hidden

   bool pending() const { return IsShown(); }
   // as the buttons; for the keyboard's Return and Escape
   void accept();
   void reject();

private:
   Hooks hooks;
   wxPanel *panel;
   wxStaticText *warnings_label;
   wxStaticText *question_label;
   wxStaticText *status_label;
   wxWindow *reverse_button;
   wxPanel *lights_panel;
   wxCheckBox *auto_clear_box;
   std::map<wxString, std::pair<wxPanel *, wxStaticText *>> light_rows;

   void show_auto_clear(bool on);
   void run(const std::string &action, bool state = true);
   void finish(const std::string &action);
   void fit();
};

} // namespace wxui

#endif // BANDICOOT_WXUI_ACCEPT_REJECT_DIALOG_HH

// src/wxUI/main/accept-reject-bar.hh
//
// AcceptRejectBar: the docked Accept/Reject bar above the surface. It shows
// a pending fit's geometry lights (each a box coloured by distortion, its
// name over its value), Reject and Accept, the fit's status and any chiral
// or cis-peptide warnings, and menus of the atom pull restraint and active
// refinement tools the floating window has. Between fits it stands idle: no
// lights, the buttons greyed out. Its settings button docks it out (into
// the floating Accept/Reject window) and chooses whether it stays up
// between fits.

#ifndef BANDICOOT_WXUI_ACCEPT_REJECT_BAR_HH
#define BANDICOOT_WXUI_ACCEPT_REJECT_BAR_HH

#include <wx/colour.h>
#include <wx/panel.h>

#include <functional>
#include <string>
#include <vector>

class wxStaticText;

namespace wxui {

class BevelButton;
class LightTile;

class AcceptRejectBar : public wxPanel {
public:
   struct Light {
      wxString name;    // "Bonds", ...
      float value;
      wxString label;   // the full text, shown as the tooltip
      wxColour colour;
   };
   struct Hooks {
      // the Coot action a button or menu item stands for; state: a check
      // item's new state, true otherwise
      std::function<void(const std::string &action, bool state)> on_action;
      // the bar has gone idle after Accept or Reject
      std::function<void()> on_idle;
      // the settings menu
      std::function<bool()> always_shown;
      std::function<void(bool always)> set_always_shown;
      std::function<void()> undock;
   };

   AcceptRejectBar(wxWindow *parent, const Hooks &hooks);

   // A fit is waiting: the buttons come on.
   void present(bool auto_clear_atom_pull);
   void set_status(const wxString &status);
   void set_lights(const std::vector<Light> &lights);  // empty: unchanged
   void set_warnings(const wxString &text);           // empty: hidden
   // Idle, with nothing run.
   void stand_down();

   bool pending() const { return is_pending; }
   // as the buttons; for the keyboard's Return and Escape
   void accept();
   void reject();

private:
   Hooks hooks;
   bool is_pending = false;
   bool auto_clear = true;
   std::vector<LightTile *> lights;
   LightTile *chiral_tile;
   LightTile *cis_tile;
   BevelButton *reject_button;
   BevelButton *accept_button;
   BevelButton *pull_button;
   BevelButton *active_button;
   BevelButton *settings_button;
   wxStaticText *status_label;

   void finish(const std::string &action);
   void show_pull_menu();
   void show_active_menu();
   void show_settings_menu();
};

} // namespace wxui

#endif // BANDICOOT_WXUI_ACCEPT_REJECT_BAR_HH

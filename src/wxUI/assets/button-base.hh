// src/wxUI/assets/button-base.hh
//
// Pointer handling shared by the custom buttons: hover, press, click, and an
// optional toggle state. A click sends wxEVT_BUTTON, or wxEVT_TOGGLEBUTTON with
// the new state in GetInt() for a toggle, so callers bind to them as they would
// to a stock wx button. Subclasses only draw and size themselves.

#ifndef BANDICOOT_WXUI_BUTTON_BASE_HH
#define BANDICOOT_WXUI_BUTTON_BASE_HH

#include <wx/control.h>

namespace wxui {

class ButtonBase : public wxControl {
public:
   ButtonBase(wxWindow *parent, wxWindowID id, const wxString &label);

   // A toggle button stays "on" after a click until clicked again.
   void set_toggle(bool state) { toggle = state; }
   bool is_toggle() const { return toggle; }
   // On: a toggle that is down, or (for a row button) the selected one.
   void set_on(bool state);
   bool is_on() const { return on; }

   bool Enable(bool enable = true) override;
   bool AcceptsFocus() const override { return false; }

   // Draw the current state into dc, as the paint handler does.
   void render(wxDC &dc);

protected:
   enum class Look { Idle, Hover, Pressed };
   Look look() const;
   virtual void draw(wxDC &dc, const wxRect &rect) = 0;

private:
   bool toggle   = false;
   bool on       = false;
   bool hovered  = false;
   bool pressing = false;

   void on_paint(wxPaintEvent &e);
   void on_enter(wxMouseEvent &e);
   void on_leave(wxMouseEvent &e);
   void on_down(wxMouseEvent &e);
   void on_up(wxMouseEvent &e);
   void on_capture_lost(wxMouseCaptureLostEvent &e);
   void click();
};

} // namespace wxui

#endif // BANDICOOT_WXUI_BUTTON_BASE_HH

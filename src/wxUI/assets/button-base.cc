// src/wxUI/assets/button-base.cc

#include "wxUI/assets/button-base.hh"

#include <wx/dcbuffer.h>
#include <wx/tglbtn.h>

namespace wxui {

ButtonBase::ButtonBase(wxWindow *parent, wxWindowID id, const wxString &label)
   : wxControl(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {

   SetLabel(label);
   SetBackgroundStyle(wxBG_STYLE_PAINT);
   Bind(wxEVT_PAINT,              &ButtonBase::on_paint,        this);
   Bind(wxEVT_ENTER_WINDOW,       &ButtonBase::on_enter,        this);
   Bind(wxEVT_LEAVE_WINDOW,       &ButtonBase::on_leave,        this);
   Bind(wxEVT_LEFT_DOWN,          &ButtonBase::on_down,         this);
   Bind(wxEVT_LEFT_DCLICK,        &ButtonBase::on_down,         this);
   Bind(wxEVT_LEFT_UP,            &ButtonBase::on_up,           this);
   Bind(wxEVT_MOUSE_CAPTURE_LOST, &ButtonBase::on_capture_lost, this);
}

void ButtonBase::set_on(bool state) {
   if (on != state) { on = state; Refresh(); }
}

bool ButtonBase::Enable(bool enable) {
   bool changed = wxControl::Enable(enable);
   if (! enable) { hovered = false; pressing = false; }
   Refresh();
   return changed;
}

ButtonBase::Look ButtonBase::look() const {
   if (! IsEnabled()) return Look::Idle;
   if (pressing && hovered) return Look::Pressed;
   if (hovered || pressing) return Look::Hover;
   return Look::Idle;
}

void ButtonBase::render(wxDC &dc) {
   draw(dc, wxRect(GetClientSize()));
}

void ButtonBase::on_paint(wxPaintEvent &) {
   wxAutoBufferedPaintDC dc(this);
   render(dc);
}

void ButtonBase::on_enter(wxMouseEvent &e) {
   if (IsEnabled()) { hovered = true; Refresh(); }
   e.Skip();
}

void ButtonBase::on_leave(wxMouseEvent &e) {
   hovered = false;
   Refresh();
   e.Skip();
}

void ButtonBase::on_down(wxMouseEvent &e) {
   if (! IsEnabled()) return;
   pressing = true;
   hovered = true;
   if (! HasCapture()) CaptureMouse(); // the release may happen outside
   Refresh();
}

void ButtonBase::on_up(wxMouseEvent &e) {
   if (! pressing) return;
   pressing = false;
   if (HasCapture()) ReleaseMouse();
   // a press that is dragged off the button and released there is cancelled
   bool inside = wxRect(GetClientSize()).Contains(e.GetPosition());
   hovered = inside;
   Refresh();
   if (inside) click();
}

void ButtonBase::on_capture_lost(wxMouseCaptureLostEvent &) {
   pressing = false;
   Refresh();
}

void ButtonBase::click() {
   if (toggle) {
      on = ! on;
      Refresh();
      wxCommandEvent ev(wxEVT_TOGGLEBUTTON, GetId());
      ev.SetEventObject(this);
      ev.SetInt(on ? 1 : 0);
      ProcessWindowEvent(ev);
   } else {
      wxCommandEvent ev(wxEVT_BUTTON, GetId());
      ev.SetEventObject(this);
      ProcessWindowEvent(ev);
   }
}

} // namespace wxui

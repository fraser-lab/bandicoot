// src/wxUI/dialogs/colour-panel-linux.cc
//
// show_colour_panel() for Linux: the toolkit's colour dialog. Unlike the
// macOS panel it is modal; where the toolkit reports colours as they are
// picked, the graphics still follow the choice.

#include "wxUI/dialogs/colour-panel.hh"

#include <wx/app.h>
#include <wx/colordlg.h>

namespace wxui {

namespace {

PanelColour from_wx(const wxColour &c) {
   PanelColour pc;
   pc.r = c.Red() / 255.0f;
   pc.g = c.Green() / 255.0f;
   pc.b = c.Blue() / 255.0f;
   return pc;
}

unsigned char channel(float v) {
   if (v < 0) v = 0;
   if (v > 1) v = 1;
   return static_cast<unsigned char>(v * 255.0f + 0.5f);
}

} // namespace

void show_colour_panel(const char *title, const PanelColour &initial,
                       std::function<void(const PanelColour &)> changed,
                       std::function<void(bool kept)> done) {

   wxColourData data;
   data.SetColour(wxColour(channel(initial.r), channel(initial.g), channel(initial.b)));
   data.SetChooseFull(true);
   wxColourDialog dialog(wxTheApp ? wxTheApp->GetTopWindow() : nullptr, &data);
   if (title) dialog.SetTitle(wxString::FromUTF8(title));
   dialog.Bind(wxEVT_COLOUR_CHANGED, [changed](wxColourDialogEvent &e) {
      if (changed) changed(from_wx(e.GetColour()));
   });
   bool kept = dialog.ShowModal() == wxID_OK;
   if (kept && changed) changed(from_wx(dialog.GetColourData().GetColour()));
   if (done) done(kept);
}

} // namespace wxui

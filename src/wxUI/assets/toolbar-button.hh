// src/wxUI/assets/toolbar-button.hh
//
// ToolbarButton: a modelling-toolbar button. Flush with its background until
// the pointer is over it, then raised; sunken while pressed; a toggle shows
// pink while down. Shows an icon, a label, or both.

#ifndef BANDICOOT_WXUI_TOOLBAR_BUTTON_HH
#define BANDICOOT_WXUI_TOOLBAR_BUTTON_HH

#include "wxUI/assets/button-base.hh"

#include <wx/bmpbndl.h>

namespace wxui {

class ToolbarButton : public ButtonBase {
public:
   enum class Content { Icon, Text, IconAboveText, IconBesideText };

   ToolbarButton(wxWindow *parent, wxWindowID id, const wxString &label,
                 const wxBitmapBundle &icon, Content content = Content::Icon);

   void set_content(Content c);
   Content get_content() const { return content; }

protected:
   void draw(wxDC &dc, const wxRect &rect) override;
   wxSize DoGetBestSize() const override;

private:
   wxBitmapBundle icon;
   Content content;
   bool shows_icon() const;
   bool shows_text() const;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_TOOLBAR_BUTTON_HH

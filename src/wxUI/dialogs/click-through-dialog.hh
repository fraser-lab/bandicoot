// src/wxUI/dialogs/click-through-dialog.hh
//
// ClickThroughDialog: a free-floating window holding a ClickThroughList, as
// the validation results and Check Waters use. An optional header area above
// the list takes extra controls (a check button, a short explanation).

#ifndef BANDICOOT_WXUI_CLICK_THROUGH_DIALOG_HH
#define BANDICOOT_WXUI_CLICK_THROUGH_DIALOG_HH

#include "wxUI/widgets/click-through-list.hh"

#include <wx/frame.h>

class wxBoxSizer;
class wxPanel;

namespace wxui {

class ClickThroughDialog : public wxFrame {
public:
   ClickThroughDialog(wxWindow *parent, const wxString &title,
                      ClickThroughList::RowType type = ClickThroughList::RowType::Button,
                      const wxString &header_text = wxEmptyString);

   ClickThroughList *list() const { return rows; }
   // Put extra controls here, created with header() as their parent.
   wxPanel *header() const { return header_panel; }
   wxBoxSizer *header_sizer() const { return header_box; }

private:
   ClickThroughList *rows;
   wxPanel *header_panel;
   wxBoxSizer *header_box;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_CLICK_THROUGH_DIALOG_HH

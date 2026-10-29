// src/wxUI/dialogs/text-dialog.hh
//
// TextDialog: a window showing a block of read-only text (file headers,
// remarks, reports). Does not block; closes itself.

#ifndef BANDICOOT_WXUI_TEXT_DIALOG_HH
#define BANDICOOT_WXUI_TEXT_DIALOG_HH

#include <wx/frame.h>

namespace wxui {

class TextDialog : public wxFrame {
public:
   TextDialog(wxWindow *parent, const wxString &title, const wxString &text,
              bool fixed_width = false);
};

// Create and show one.
TextDialog *show_text(wxWindow *parent, const wxString &title, const wxString &text,
                      bool fixed_width = false);

} // namespace wxui

#endif // BANDICOOT_WXUI_TEXT_DIALOG_HH

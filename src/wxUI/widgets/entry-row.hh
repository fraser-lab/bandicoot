// src/wxUI/widgets/entry-row.hh
//
// EntryRow: a labelled text field, optionally with a button beside it. Return
// in the field does what the button does.

#ifndef BANDICOOT_WXUI_ENTRY_ROW_HH
#define BANDICOOT_WXUI_ENTRY_ROW_HH

#include <wx/panel.h>

#include <functional>

class wxTextCtrl;

namespace wxui {

class EntryRow : public wxPanel {
public:
   EntryRow(wxWindow *parent, const wxString &label, const wxString &initial = wxEmptyString,
            const wxString &button_label = wxEmptyString);

   wxString get_text() const;
   void set_text(const wxString &text);
   // false, leaving value untouched, when the text is not a number
   bool get_double(double &value) const;
   bool get_long(long &value) const;

   wxTextCtrl *text_ctrl() const { return text; }

   std::function<void(const wxString &text)> on_button;

private:
   wxTextCtrl *text;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_ENTRY_ROW_HH

// src/wxUI/widgets/mixed-row.hh
//
// MixedRow: one row of assorted controls with consistent spacing, optionally
// in an etched frame -- e.g. a Display Manager entry: number, name, Display and
// Active checks, a button. Each add_* returns the control so the caller can
// bind it.

#ifndef BANDICOOT_WXUI_MIXED_ROW_HH
#define BANDICOOT_WXUI_MIXED_ROW_HH

#include <wx/panel.h>

#include <vector>

class wxBoxSizer;
class wxCheckBox;
class wxChoice;
class wxRadioButton;
class wxStaticText;
class wxTextCtrl;

namespace wxui {

class BevelButton;

class MixedRow : public wxPanel {
public:
   explicit MixedRow(wxWindow *parent, bool framed = false);

   wxStaticText  *add_label(const wxString &text);
   // read-only by default; width in characters, 0 to stretch
   wxTextCtrl    *add_text(const wxString &text, int width_chars = 0, bool read_only = true);
   wxCheckBox    *add_check(const wxString &label, bool state = false);
   // starts a new radio group when first_in_group
   wxRadioButton *add_radio(const wxString &label, bool first_in_group = false);
   BevelButton   *add_button(const wxString &label);  // Coot's grey push button
   wxChoice      *add_choice(const std::vector<wxString> &items, int selection = 0);
   void add_stretch();

private:
   bool framed;
   wxBoxSizer *row;
   void add(wxWindow *w, int proportion = 0);
   void on_paint(wxPaintEvent &e);
};

} // namespace wxui

#endif // BANDICOOT_WXUI_MIXED_ROW_HH

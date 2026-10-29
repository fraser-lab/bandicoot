// src/wxUI/widgets/row-list.hh
//
// RowList: a scrolling column of rows that are added and removed at run time
// (the molecule rows of the Display Manager, the results of a validation check).
// A row is any window created with the list as its parent.

#ifndef BANDICOOT_WXUI_ROW_LIST_HH
#define BANDICOOT_WXUI_ROW_LIST_HH

#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <vector>

namespace wxui {

class RowList : public wxScrolledWindow {
public:
   explicit RowList(wxWindow *parent, wxWindowID id = wxID_ANY);

   size_t add_row(wxWindow *row);
   void remove_row(size_t index);
   void clear();
   size_t count() const { return rows.size(); }
   wxWindow *row(size_t index) const { return index < rows.size() ? rows[index] : nullptr; }
   void scroll_to(size_t index);
   // Scroll sideways too, for rows wider than the list.
   void allow_horizontal_scroll();

private:
   wxBoxSizer *sizer;
   std::vector<wxWindow *> rows;
   void relayout();
};

} // namespace wxui

#endif // BANDICOOT_WXUI_ROW_LIST_HH

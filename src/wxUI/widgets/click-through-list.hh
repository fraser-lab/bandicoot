// src/wxUI/widgets/click-through-list.hh
//
// ClickThroughList: the list behind Coot's validation results and other
// click-through dialogs. Each row is a button; clicking one reports its index
// (typically to centre on what the row describes).
//
// Button rows are plain push buttons. Radio rows show which one was clicked
// last, as Check Waters does. A row may carry a second button for an action
// on that item (e.g. "Fix").

#ifndef BANDICOOT_WXUI_CLICK_THROUGH_LIST_HH
#define BANDICOOT_WXUI_CLICK_THROUGH_LIST_HH

#include "wxUI/widgets/row-list.hh"

#include <functional>

namespace wxui {

class ButtonBase;

class ClickThroughList : public RowList {
public:
   enum class RowType { Button, Radio };

   explicit ClickThroughList(wxWindow *parent, RowType type = RowType::Button,
                             wxWindowID id = wxID_ANY);

   // An empty action_label means the row has no second button.
   size_t add_item(const wxString &label, const wxString &action_label = wxEmptyString);
   void clear_items();
   // rows are addressed by index, so they are only ever cleared together
   void remove_row(size_t) = delete;

   // Radio rows: the selected row, or -1.
   int selected() const { return selection; }
   void select(int index);

   std::function<void(size_t index)> on_click;
   std::function<void(size_t index)> on_action;

private:
   RowType type;
   std::vector<ButtonBase *> buttons;
   int selection = -1;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_CLICK_THROUGH_LIST_HH

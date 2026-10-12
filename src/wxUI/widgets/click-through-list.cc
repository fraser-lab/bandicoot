// src/wxUI/widgets/click-through-list.cc

#include "wxUI/widgets/click-through-list.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/flat-button.hh"

#include <wx/panel.h>

namespace wxui {

ClickThroughList::ClickThroughList(wxWindow *parent, RowType type_in, wxWindowID id)
   : RowList(parent, id), type(type_in) {}

size_t ClickThroughList::add_item(const wxString &label, const wxString &action_label) {

   size_t index = buttons.size();
   wxPanel *row = new wxPanel(this);
   wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);

   ButtonBase *main = nullptr;
   if (type == RowType::Radio)
      main = new FlatButton(row, wxID_ANY, label, Indicator::Radio);
   else
      main = new BevelButton(row, wxID_ANY, label);
   main->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent &) {
      if (type == RowType::Radio) select(int(index));
      if (on_click) on_click(index);
   });
   h->Add(main, 1, wxEXPAND);

   if (! action_label.empty()) {
      BevelButton *action = new BevelButton(row, wxID_ANY, action_label);
      action->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent &) {
         if (on_action) on_action(index);
      });
      h->Add(action, 0, wxEXPAND | wxLEFT, 4);
   }

   row->SetSizer(h);
   buttons.push_back(main);
   return add_row(row);
}

void ClickThroughList::clear_items() {
   buttons.clear();
   selection = -1;
   clear();
}

void ClickThroughList::select(int index) {
   if (type != RowType::Radio) return;
   if (selection >= 0 && selection < int(buttons.size()))
      buttons[selection]->set_on(false);
   selection = (index >= 0 && index < int(buttons.size())) ? index : -1;
   if (selection >= 0)
      buttons[selection]->set_on(true);
}

} // namespace wxui

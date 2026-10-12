// src/wxUI/widgets/row-list.cc

#include "wxUI/widgets/row-list.hh"
#include "wxUI/assets/palette.hh"

namespace wxui {

namespace {
const int row_gap = 4, margin = 4; // points
}

RowList::RowList(wxWindow *parent, wxWindowID id)
   : wxScrolledWindow(parent, id, wxDefaultPosition, wxDefaultSize, wxVSCROLL) {
   SetBackgroundColour(palette::face());
   SetScrollRate(0, 10);
   sizer = new wxBoxSizer(wxVERTICAL);
   SetSizer(sizer);
}

size_t RowList::add_row(wxWindow *row) {
   sizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, rows.empty() ? margin : row_gap);
   rows.push_back(row);
   relayout();
   return rows.size() - 1;
}

void RowList::remove_row(size_t index) {
   if (index >= rows.size()) return;
   sizer->Detach(rows[index]);
   rows[index]->Destroy();
   rows.erase(rows.begin() + index);
   relayout();
}

void RowList::clear() {
   sizer->Clear(true);
   rows.clear();
   relayout();
}

void RowList::scroll_to(size_t index) {
   if (index >= rows.size()) return;
   int unit_x = 0, unit_y = 1;
   GetScrollPixelsPerUnit(&unit_x, &unit_y);
   if (unit_y > 0)
      Scroll(-1, rows[index]->GetPosition().y / unit_y + GetViewStart().y);
}

void RowList::relayout() {
   FitInside();
   Layout();
   Refresh();
}

} // namespace wxui

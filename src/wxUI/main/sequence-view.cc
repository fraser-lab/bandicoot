// src/wxUI/main/sequence-view.cc

#include "wxUI/main/sequence-view.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/palette.hh"

#include <wx/dcbuffer.h>
#include <wx/menu.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace wxui {

namespace {

// Coot's view's geometry, in points
const int letter_width = 10;
const int row_height   = 12;
const int label_width  = 70;   // the chain names, kept in view
const int left_pad     = 5;
const int ruler_height = 18;   // a ruler and its numbers
const int row_gap      = 2;    // between a ruler and the rows

const wxColour hover_fill(0xff, 0xe4, 0xb5);      // moccasin
const wxColour highlight_fill(0xff, 0xa5, 0x00);  // orange
const wxColour label_colour(0x11, 0x11, 0x11);

int content_height(int rows) {
   return ruler_height + row_gap + rows * row_height + row_gap + ruler_height;
}

} // namespace

class SequenceCanvas : public wxScrolledCanvas {
public:
   explicit SequenceCanvas(wxWindow *parent)
      : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxHSCROLL | wxVSCROLL | wxFULL_REPAINT_ON_RESIZE) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      SetScrollRate(letter_width, row_height);
      ShowScrollbars(wxSHOW_SB_ALWAYS, wxSHOW_SB_DEFAULT);
      font = wxFont(wxFontInfo(9).Family(wxFONTFAMILY_SWISS));
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) { paint(); });
      Bind(wxEVT_MOTION, [this](wxMouseEvent &e) { set_hover(cell_at(e.GetPosition())); });
      Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { set_hover(Cell()); });
      Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
         Cell c = cell_at(e.GetPosition());
         if (! c.valid()) return;
         const bandicoot_seq_residue &r = residue(c);
         highlight = c;
         Refresh();
         if (on_click) on_click(rows[c.row].chain_id, r.resno, r.ins_code);
      });
      Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent &e) {
         wxMenu menu;
         menu.Append(wxID_CLOSE, "Close");
         menu.Bind(wxEVT_MENU, [this](wxCommandEvent &) { if (on_close) on_close(); });
         PopupMenu(&menu, e.GetPosition());
      });
   }

   std::function<void(const std::string &chain, int resno, const std::string &ins)> on_click;
   std::function<void()> on_close;

   void set_sequence(const bandicoot_sequence &seq) {
      // keep the highlight on the same residue if it is still there
      std::string hl_chain, hl_ins;
      int hl_resno = 0;
      bool had_highlight = highlight.valid();
      if (had_highlight) {
         hl_chain = rows[highlight.row].chain_id;
         hl_resno = residue(highlight).resno;
         hl_ins = residue(highlight).ins_code;
      }
      rows = seq.chains;
      build_columns();
      hover = Cell();
      highlight = Cell();
      SetVirtualSize(label_width + left_pad + int(columns.size()) * letter_width + left_pad,
                     content_height(int(rows.size())));
      if (had_highlight) set_highlight(hl_chain, hl_resno, hl_ins, false);
      Refresh();
   }

   int chain_count() const { return int(rows.size()); }

   void set_highlight(const std::string &chain, int resno, const std::string &ins, bool show) {
      highlight = Cell();
      for (int r = 0; r < int(rows.size()); r++) {
         if (rows[r].chain_id != chain) continue;
         auto col = column_of.find({ resno, ins });
         if (col == column_of.end()) break;
         auto it = by_column[r].find(col->second);
         if (it == by_column[r].end()) break;
         highlight = { r, col->second };
         if (show) scroll_into_view(highlight);
         break;
      }
      Refresh();
   }

   int horizontal_scrollbar_height() {
      int h = wxSystemSettings::GetMetric(wxSYS_HSCROLL_Y, this);
      return h > 0 ? h : 15;
   }

private:
   struct Cell {
      int row = -1, column = -1;
      bool valid() const { return row >= 0; }
      bool operator==(const Cell &o) const { return row == o.row && column == o.column; }
   };

   std::vector<bandicoot_seq_chain> rows;
   // a column per residue number from the lowest to the highest, each
   // followed by one per insertion code used with that number
   std::vector<std::pair<int, std::string>> columns;
   std::map<std::pair<int, std::string>, int> column_of;
   std::vector<std::map<int, int>> by_column;  // per row: column -> residue index
   Cell hover, highlight;
   wxFont font;

   const bandicoot_seq_residue &residue(const Cell &c) const {
      return rows[c.row].residues[by_column[c.row].at(c.column)];
   }

   void build_columns() {
      columns.clear();
      column_of.clear();
      by_column.assign(rows.size(), std::map<int, int>());
      int lowest = 0, highest = -1;
      bool any = false;
      std::map<int, std::set<std::string>> inserted;
      for (const bandicoot_seq_chain &c : rows)
         for (const bandicoot_seq_residue &r : c.residues) {
            if (! any || r.resno < lowest) lowest = r.resno;
            if (! any || r.resno > highest) highest = r.resno;
            any = true;
            if (! r.ins_code.empty()) inserted[r.resno].insert(r.ins_code);
         }
      for (int n = lowest; any && n <= highest; n++) {
         column_of[{ n, std::string() }] = int(columns.size());
         columns.push_back({ n, std::string() });
         auto it = inserted.find(n);
         if (it == inserted.end()) continue;
         for (const std::string &ins : it->second) {
            column_of[{ n, ins }] = int(columns.size());
            columns.push_back({ n, ins });
         }
      }
      for (size_t r = 0; r < rows.size(); r++)
         for (size_t i = 0; i < rows[r].residues.size(); i++) {
            const bandicoot_seq_residue &res = rows[r].residues[i];
            by_column[r][column_of[{ res.resno, res.ins_code }]] = int(i);
         }
   }

   // in unscrolled coordinates
   static int column_centre(int column) { return label_width + left_pad + column * letter_width + letter_width / 2; }
   static int row_top(int row) { return ruler_height + row_gap + row * row_height; }

   Cell cell_at(const wxPoint &window_point) const {
      if (window_point.x < label_width) return Cell();
      wxPoint p = CalcUnscrolledPosition(window_point);
      int x = p.x - label_width - left_pad;
      int y = p.y - ruler_height - row_gap;
      if (x < 0 || y < 0) return Cell();
      int column = x / letter_width, row = y / row_height;
      if (row >= int(rows.size()) || column >= int(columns.size())) return Cell();
      if (by_column[row].find(column) == by_column[row].end()) return Cell();
      return { row, column };
   }

   void set_hover(const Cell &c) {
      if (c == hover) return;
      hover = c;
      if (c.valid()) {
         const bandicoot_seq_residue &r = residue(c);
         wxString tip = wxString::FromUTF8(rows[c.row].chain_id) + " " + wxString::Format("%d", r.resno);
         if (! r.ins_code.empty()) tip += wxString::FromUTF8(r.ins_code);
         tip += " " + wxString::FromUTF8(r.res_name);
         SetToolTip(tip);
      } else {
         UnsetToolTip();
      }
      Refresh();
   }

   void scroll_into_view(const Cell &c) {
      int ux, uy;
      GetScrollPixelsPerUnit(&ux, &uy);
      if (ux <= 0 || uy <= 0) return;
      wxSize client = GetClientSize();
      wxPoint origin = CalcUnscrolledPosition(wxPoint(0, 0));
      int x = column_centre(c.column), y = row_top(c.row);
      int new_x = origin.x, new_y = origin.y;
      if (x < origin.x + label_width + letter_width || x > origin.x + client.x - letter_width)
         new_x = std::max(0, x - label_width - (client.x - label_width) / 2);
      if (y < origin.y + ruler_height || y + row_height > origin.y + client.y - ruler_height)
         new_y = std::max(0, y - client.y / 2);
      if (new_x != origin.x || new_y != origin.y)
         Scroll(new_x / ux, new_y / uy);
   }

   void paint() {
      wxAutoBufferedPaintDC dc(this);
      wxSize client = GetClientSize();
      wxPoint o = CalcUnscrolledPosition(wxPoint(0, 0));
      dc.SetBackground(*wxWHITE_BRUSH);
      dc.Clear();
      dc.SetFont(font);

      int ncols = int(columns.size());
      int first = std::max(0, (o.x - left_pad) / letter_width - 1);
      int last = std::min(ncols - 1, (o.x + client.x - label_width - left_pad) / letter_width + 1);

      // the rows and rulers, right of the chain names
      dc.SetClippingRegion(label_width, 0, std::max(0, client.x - label_width), client.y);

      auto cell_rect = [&](const Cell &c) {
         return wxRect(column_centre(c.column) - 4 - o.x, row_top(c.row) - o.y, 9, row_height);
      };
      dc.SetPen(*wxTRANSPARENT_PEN);
      if (highlight.valid()) {
         dc.SetBrush(wxBrush(highlight_fill));
         dc.DrawRectangle(cell_rect(highlight));
      }
      if (hover.valid() && ! (hover == highlight)) {
         dc.SetBrush(wxBrush(hover_fill));
         dc.DrawRectangle(cell_rect(hover));
      }

      for (int r = 0; r < int(rows.size()); r++) {
         int top = row_top(r) - o.y;
         if (top + row_height < 0 || top > client.y) continue;
         auto it = by_column[r].lower_bound(first);
         for (; it != by_column[r].end() && it->first <= last; ++it) {
            const bandicoot_seq_residue &res = rows[r].residues[it->second];
            wxString s(wxUniChar(res.letter));
            wxSize ext = dc.GetTextExtent(s);
            dc.SetTextForeground(wxColour(res.red, res.green, res.blue));
            dc.DrawText(s, column_centre(it->first) - ext.x / 2 - o.x,
                        top + (row_height - ext.y) / 2);
         }
      }

      // rulers above and below, ticked and numbered every 5 residues
      if (ncols > 0) {
         int top_axis = ruler_height - o.y;
         int bottom_axis = row_top(int(rows.size())) + row_gap - o.y;
         int x0 = label_width + left_pad - o.x;
         int x1 = column_centre(ncols - 1) - o.x;
         dc.SetPen(*wxBLACK_PEN);
         dc.SetTextForeground(*wxBLACK);
         dc.DrawLine(x0, top_axis, x1, top_axis);
         dc.DrawLine(x0, bottom_axis, x1, bottom_axis);
         for (int c = first; c <= last; c++) {
            if (! columns[c].second.empty() || columns[c].first % 5 != 0) continue;
            int x = column_centre(c) - o.x;
            wxString n = wxString::Format("%d", columns[c].first);
            wxSize ext = dc.GetTextExtent(n);
            dc.DrawLine(x, top_axis, x, top_axis - 3);
            dc.DrawText(n, x - ext.x / 2, top_axis - 4 - ext.y);
            dc.DrawLine(x, bottom_axis, x, bottom_axis + 3);
            dc.DrawText(n, x - ext.x / 2, bottom_axis + 4);
         }
      }
      dc.DestroyClippingRegion();

      // the chain names, which do not scroll sideways
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetBrush(*wxWHITE_BRUSH);
      dc.DrawRectangle(0, 0, label_width, client.y);
      dc.SetTextForeground(label_colour);
      for (int r = 0; r < int(rows.size()); r++) {
         int top = row_top(r) - o.y;
         if (top + row_height < 0 || top > client.y) continue;
         wxString s = "Chain " + wxString::FromUTF8(rows[r].chain_id);
         wxSize ext = dc.GetTextExtent(s);
         dc.DrawText(s, 10, top + (row_height - ext.y) / 2);
      }
   }
};

SequenceView::SequenceView(wxWindow *parent, int imol_in, bool docked, const Hooks &h)
   : wxPanel(parent), imol(imol_in), hooks(h) {

   SetBackgroundColour(palette::face());

   title = new wxStaticText(this, wxID_ANY, wxEmptyString);
   BevelButton *dock = new BevelButton(this, wxID_ANY, docked ? "Undock" : "Dock");
   BevelButton *close = new BevelButton(this, wxID_ANY, "Close");
   for (BevelButton *b : { dock, close }) b->SetMinSize(wxSize(72, 22));
   // once the click is done: either can destroy this panel
   dock->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      CallAfter([this]() { if (hooks.toggle_docked) hooks.toggle_docked(); });
   });
   close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
      CallAfter([this]() { if (hooks.close) hooks.close(); });
   });

   wxBoxSizer *top = new wxBoxSizer(wxHORIZONTAL);
   top->AddStretchSpacer();
   top->Add(title, 0, wxALIGN_CENTER_VERTICAL);
   top->AddStretchSpacer();
   top->Add(dock, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
   top->Add(close, 0, wxALIGN_CENTER_VERTICAL);

   canvas = new SequenceCanvas(this);
   canvas->on_click = [this](const std::string &chain, int resno, const std::string &ins) {
      bandicoot_core_sequence_view_centre(imol, chain, resno, ins);
   };
   canvas->on_close = [this]() {
      CallAfter([this]() { if (hooks.close) hooks.close(); });
   };

   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(top, 0, wxEXPAND | wxALL, 3);
   col->Add(canvas, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 3);
   SetSizer(col);
   rebuild();
}

void SequenceView::rebuild() {
   bandicoot_sequence seq = bandicoot_core_sequence(imol);
   title->SetLabel(wxString::FromUTF8(seq.title));
   canvas->set_sequence(seq);
   Layout();
}

void SequenceView::highlight(const std::string &chain, int resno, const std::string &ins_code) {
   canvas->set_highlight(chain, resno, ins_code, true);
}

int SequenceView::chain_count() const {
   return canvas->chain_count();
}

int SequenceView::height_for_rows(int n) const {
   wxSizer *top = GetSizer()->GetItem(size_t(0))->GetSizer();
   int title_height = top->GetMinSize().y + 6;
   return title_height + content_height(std::max(1, n)) +
          canvas->horizontal_scrollbar_height() + 3 + 2;
}

} // namespace wxui

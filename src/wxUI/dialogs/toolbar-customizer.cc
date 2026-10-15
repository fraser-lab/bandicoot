// src/wxUI/dialogs/toolbar-customizer.cc

#include "wxUI/dialogs/toolbar-customizer.hh"
#include "wxUI/assets/palette.hh"

#include <wx/button.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/wrapsizer.h>
#include <wx/stattext.h>

#include <algorithm>
#include <cstdlib>

namespace wxui {

namespace {

const wxSize tile_size(96, 66);   // points: icon, then up to two lines of label
const int icon_points = 24;
const int drag_threshold = 4;     // points the pointer moves before a drag starts
const int palette_gap = 6;        // points between palette tiles

// The label in at most two lines, broken at the space nearest the middle.
wxString wrap_label(wxDC &dc, const wxString &label, int width) {
   if (dc.GetTextExtent(label).x <= width) return label;
   int best = -1, mid = int(label.length()) / 2;
   for (size_t i = 0; i < label.length(); i++)
      if (label[i] == ' ' && (best < 0 || std::abs(int(i) - mid) < std::abs(best - mid)))
         best = int(i);
   if (best < 0) return label;
   return label.substr(0, best) + "\n" + label.substr(best + 1);
}

// One tile: icon over label, as the macOS sheet draws them.
void draw_tile(wxDC &dc, const wxRect &r, const ToolbarCustomizer::Item &item, bool dimmed,
               wxWindow *for_scale) {
   wxColour text = dimmed ? palette::text_disabled() : palette::text();
   wxRect box(r.x + (r.width - icon_points) / 2, r.y + 6, icon_points, icon_points);
   if (item.icon.IsOk()) {
      wxBitmap b = item.icon.GetBitmapFor(for_scale);
      if (dimmed) b = b.ConvertToDisabled();
      dc.DrawBitmap(b, box.x, box.y, true);
   }
   dc.SetTextForeground(text);
   wxRect label_rect(r.x + 2, box.GetBottom() + 4, r.width - 4, r.GetBottom() - box.GetBottom() - 4);
   dc.DrawLabel(wrap_label(dc, item.label, label_rect.width), label_rect,
                wxALIGN_CENTER_HORIZONTAL | wxALIGN_TOP);
}

// A tile that can be picked up. It keeps the pointer while the button is
// down and reports the drag in screen coordinates; the dialog decides what
// the drag means.
class Tile : public wxControl {
public:
   Tile(wxWindow *parent, const ToolbarCustomizer::Item &item_in, bool dimmed_in)
      : wxControl(parent, wxID_ANY, wxDefaultPosition, tile_size, wxBORDER_NONE),
        item(item_in), dimmed(dimmed_in) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      SetMinSize(tile_size);
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
         wxAutoBufferedPaintDC dc(this);
         dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
         dc.Clear();
         dc.SetFont(GetFont());
         draw_tile(dc, GetClientRect(), item, dimmed, this);
      });
      Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
         down_at = ClientToScreen(e.GetPosition());
         pressing = true;
         CaptureMouse();
      });
      Bind(wxEVT_MOTION, [this](wxMouseEvent &e) {
         if (! pressing) return;
         wxPoint p = ClientToScreen(e.GetPosition());
         if (! dragging) {
            if (std::abs(p.x - down_at.x) + std::abs(p.y - down_at.y) < drag_threshold) return;
            if (! drag_started || ! drag_started(down_at)) { pressing = false; release(); return; }
            dragging = true;
         }
         if (drag_moved) drag_moved(p);
      });
      Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &e) {
         wxPoint p = ClientToScreen(e.GetPosition());
         bool was_dragging = dragging;
         pressing = dragging = false;
         release();
         // last: the drop may rebuild the window this tile is in
         if (was_dragging && drag_ended) drag_ended(p);
      });
      Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) {
         bool was_dragging = dragging;
         pressing = dragging = false;
         if (was_dragging && drag_cancelled) drag_cancelled();
      });
   }

   bool AcceptsFocus() const override { return false; }

   std::function<bool(const wxPoint &)> drag_started;  // false: not draggable
   std::function<void(const wxPoint &)> drag_moved;
   std::function<void(const wxPoint &)> drag_ended;
   std::function<void()> drag_cancelled;

   ToolbarCustomizer::Item item;
   bool dimmed;

private:
   wxPoint down_at;
   bool pressing = false, dragging = false;
   void release() { if (HasCapture()) ReleaseMouse(); }
};

// What follows the pointer during a drag.
class Ghost : public wxPopupWindow {
public:
   Ghost(wxWindow *parent, const ToolbarCustomizer::Item &item_in)
      : wxPopupWindow(parent), item(item_in) {
      SetSize(tile_size);
      SetBackgroundColour(palette::face());
      SetTransparent(200);
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
         wxPaintDC dc(this);
         dc.SetFont(GetParent()->GetFont());
         draw_tile(dc, GetClientRect(), item, false, this);
      });
   }
   void follow(const wxPoint &where) {
      Move(where.x - tile_size.x / 2, where.y - tile_size.y / 2);
      if (! IsShown()) Show();
   }
private:
   ToolbarCustomizer::Item item;
};

} // namespace

ToolbarCustomizer::ToolbarCustomizer(wxWindow *parent, const std::vector<Item> &fixed_in,
                                     const std::vector<Item> &palette_in,
                                     const std::vector<std::string> &layout_in,
                                     const std::vector<std::string> &default_layout_in)
   : wxDialog(parent, wxID_ANY, "Customize Toolbar", wxDefaultPosition, wxDefaultSize,
              wxCAPTION | wxSYSTEM_MENU | wxCLOSE_BOX | wxRESIZE_BORDER),
     fixed(fixed_in), palette(palette_in), layout(layout_in),
     default_layout(default_layout_in) {

   SetBackgroundColour(palette::face());
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   col->Add(new wxStaticText(this, wxID_ANY, "Drag your favorite items into the toolbar below..."),
            0, wxALL, 12);
   // the palette wraps like text, keeping its spacing, and scrolls when the
   // window is too short for it
   palette_panel = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                        wxVSCROLL);
   palette_panel->SetBackgroundColour(palette::face());
   palette_panel->SetScrollRate(0, 10);
   palette_panel->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
      e.Skip();
      CallAfter([this]() { palette_panel->FitInside(); });
   });
   col->Add(palette_panel, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

   // the toolbar stand-in: as wide as the window, scrolled for the rest
   strip = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                wxSize(-1, tile_size.y + 28),
                                wxHSCROLL | wxALWAYS_SHOW_SB | wxBORDER_SUNKEN);
   // its tiles must not widen the window: it scrolls instead
   strip->SetMinSize(wxSize(3 * tile_size.x, tile_size.y + 28));
   strip->SetBackgroundColour(palette::list_background());
   strip->SetScrollRate(10, 0);
   col->Add(strip, 0, wxEXPAND | wxALL, 12);
   marker = new wxWindow(strip, wxID_ANY, wxDefaultPosition, wxSize(2, tile_size.y));
   marker->SetBackgroundColour(*wxBLUE);
   marker->Hide();

   col->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
   wxBoxSizer *bottom = new wxBoxSizer(wxHORIZONTAL);
   wxButton *restore = new wxButton(this, wxID_ANY, "Restore Defaults");
   restore->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { set_layout(default_layout); });
   bottom->Add(restore, 0, wxALIGN_CENTER_VERTICAL);
   bottom->AddStretchSpacer();
   wxButton *close = new wxButton(this, wxID_OK, "Close");
   close->SetDefault();
   bottom->Add(close, 0, wxALIGN_CENTER_VERTICAL);
   col->Add(bottom, 0, wxEXPAND | wxALL, 12);

   SetSizer(col);
   fill_palette();
   fill_strip();
   // Opens as wide as eight palette columns and as tall as the palette needs
   // there; can be made smaller, down to a few columns.
   const int cell = tile_size.x + palette_gap;
   const int margins = 2 * 12;
   int rows = int((palette.size() + 7) / 8);
   palette_panel->SetMinSize(wxSize(8 * cell, rows * (tile_size.y + palette_gap)));
   col->SetSizeHints(this);
   palette_panel->SetMinSize(wxSize(3 * cell, tile_size.y + palette_gap));
   SetMinSize(wxSize(3 * cell + margins, -1));
   CentreOnParent();
}

ToolbarCustomizer::~ToolbarCustomizer() {
   if (ghost) ghost->Destroy();
}

const ToolbarCustomizer::Item *ToolbarCustomizer::find(const std::string &id) const {
   for (const Item &i : palette)
      if (i.id == id) return &i;
   return nullptr;
}

bool ToolbarCustomizer::on_toolbar(const std::string &id) const {
   return std::find(layout.begin(), layout.end(), id) != layout.end();
}

void ToolbarCustomizer::fill_palette() {
   // only the tiles go, as in the strip
   for (wxWindow *w : palette_tiles) w->Destroy();
   palette_tiles.clear();
   // no stretching of the last tile on a line: tiles keep their spacing
   wxWrapSizer *grid = new wxWrapSizer(wxHORIZONTAL, wxREMOVE_LEADING_SPACES);
   for (const Item &i : palette) {
      bool dimmed = on_toolbar(i.id);
      Tile *t = new Tile(palette_panel, i, dimmed);
      std::string id = i.id;
      t->drag_started = [this, t, id](const wxPoint &p) {
         if (t->dimmed) return false;
         begin_drag(Source::Palette, id, -1, p);
         return true;
      };
      t->drag_moved = [this](const wxPoint &p) { move_drag(p); };
      t->drag_ended = [this](const wxPoint &p) { end_drag(p); };
      t->drag_cancelled = [this]() { cancel_drag(); };
      grid->Add(t, 0, wxRIGHT | wxBOTTOM, palette_gap);
      palette_tiles.push_back(t);
   }
   palette_panel->SetSizer(grid, true);
   palette_panel->FitInside();
   palette_panel->Layout();
}

void ToolbarCustomizer::fill_strip() {
   // Only the tiles go: the strip's other children include its own scrollbars
   // on some platforms.
   for (wxWindow *w : all_strip_tiles) w->Destroy();
   all_strip_tiles.clear();
   strip_tiles.clear();
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   for (const Item &f : fixed) {
      Tile *t = new Tile(strip, f, false);   // fixed: never picked up
      row->Add(t, 0, wxALL, 4);
      all_strip_tiles.push_back(t);
   }
   for (size_t n = 0; n < layout.size(); n++) {
      const Item *i = find(layout[n]);
      if (! i) continue;
      Tile *t = new Tile(strip, *i, false);
      int index = int(n);
      t->drag_started = [this, index](const wxPoint &p) {
         begin_drag(Source::Strip, std::string(), index, p);
         return true;
      };
      t->drag_moved = [this](const wxPoint &p) { move_drag(p); };
      t->drag_ended = [this](const wxPoint &p) { end_drag(p); };
      t->drag_cancelled = [this]() { cancel_drag(); };
      row->Add(t, 0, wxALL, 4);
      strip_tiles.push_back(t);
      all_strip_tiles.push_back(t);
   }
   // a scrolled window takes its sizer's size as its own; the minimum set in
   // the constructor is put back so the strip scrolls rather than grows
   wxSize keep = strip->GetMinSize();
   strip->SetSizer(row, true);
   strip->SetMinSize(keep);
   strip->FitInside();
   strip->Layout();
}

void ToolbarCustomizer::begin_drag(Source s, const std::string &id, int index,
                                   const wxPoint &where) {
   source = s;
   drag_id = id;
   drag_index = index;
   Item shown;
   if (s == Source::Palette) {
      if (const Item *i = find(id)) shown = *i;
   } else if (s == Source::Strip) {
      if (const Item *i = find(layout[index])) shown = *i;
   }
   ghost = new Ghost(this, shown);
   static_cast<Ghost *>(ghost)->follow(where);
}

void ToolbarCustomizer::move_drag(const wxPoint &where) {
   if (ghost) static_cast<Ghost *>(ghost)->follow(where);
   scroll_strip_near(where);
   if (over_strip(where))
      show_marker(insertion_index(where));
   else
      marker->Hide();
}

void ToolbarCustomizer::end_drag(const wxPoint &where) {
   bool into_strip = over_strip(where);
   int at = insertion_index(where);
   Source s = source;
   std::string id = drag_id;
   int from = drag_index;
   cancel_drag();

   std::vector<std::string> l = layout;
   if (s == Source::Palette) {
      if (! into_strip) return;
      l.insert(l.begin() + at, id);
   } else if (s == Source::Strip) {
      std::string moved = l[from];
      l.erase(l.begin() + from);
      if (into_strip) {
         if (at > from) at--;
         l.insert(l.begin() + at, moved);
      }  // dragged out of the strip: removed
   } else {
      return;
   }
   // after the tile that reported the drop has returned
   CallAfter([this, l]() { set_layout(l); });
}

void ToolbarCustomizer::cancel_drag() {
   source = Source::None;
   marker->Hide();
   if (ghost) {
      ghost->Destroy();
      ghost = nullptr;
   }
}

bool ToolbarCustomizer::over_strip(const wxPoint &where) const {
   return strip->GetScreenRect().Contains(where);
}

// Where in the arranged part a drop at this point would go.
int ToolbarCustomizer::insertion_index(const wxPoint &where) const {
   for (size_t i = 0; i < strip_tiles.size(); i++) {
      wxRect r = strip_tiles[i]->GetScreenRect();
      if (where.x < r.x + r.width / 2) return int(i);
   }
   return int(strip_tiles.size());
}

void ToolbarCustomizer::show_marker(int index) {
   int x;
   if (index < int(strip_tiles.size())) {
      x = strip_tiles[index]->GetPosition().x - 3;
   } else if (! strip_tiles.empty()) {
      wxWindow *last = strip_tiles.back();
      x = last->GetPosition().x + last->GetSize().x + 2;
   } else {
      // after the fixed tools
      int right = 0;
      for (wxWindow *w : strip->GetChildren())
         if (w != marker) right = std::max(right, w->GetPosition().x + w->GetSize().x);
      x = right + 2;
   }
   marker->SetSize(x, 4, 2, tile_size.y);
   marker->Show();
   marker->Raise();
}

// Near either end of the strip, a drag scrolls it, so every place in a long
// toolbar can be reached.
void ToolbarCustomizer::scroll_strip_near(const wxPoint &where) {
   wxRect r = strip->GetScreenRect();
   if (where.y < r.y || where.y > r.GetBottom()) return;
   const int edge = 40;  // points
   int x, y;
   strip->GetViewStart(&x, &y);
   if (where.x < r.x + edge && where.x >= r.x - edge)
      strip->Scroll(std::max(0, x - 2), -1);
   else if (where.x > r.GetRight() - edge && where.x <= r.GetRight() + edge)
      strip->Scroll(x + 2, -1);
}

void ToolbarCustomizer::set_layout(const std::vector<std::string> &l) {
   layout = l;
   fill_strip();
   fill_palette();
}

} // namespace wxui

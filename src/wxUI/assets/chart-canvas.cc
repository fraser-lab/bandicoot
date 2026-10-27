// src/wxUI/assets/chart-canvas.cc

#include "wxUI/assets/chart-canvas.hh"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace wxui {

namespace {

// pointer slack for hitting thin lines and small items, in points
const double hit_tolerance_points = 3.0;

double distance_to_segment(const wxPoint2DDouble &p,
                           const wxPoint2DDouble &a, const wxPoint2DDouble &b) {
   double dx = b.m_x - a.m_x, dy = b.m_y - a.m_y;
   double len2 = dx * dx + dy * dy;
   double t = 0.0;
   if (len2 > 0.0)
      t = std::clamp(((p.m_x - a.m_x) * dx + (p.m_y - a.m_y) * dy) / len2, 0.0, 1.0);
   double cx = a.m_x + t * dx - p.m_x;
   double cy = a.m_y + t * dy - p.m_y;
   return std::sqrt(cx * cx + cy * cy);
}

bool inside_polygon(const wxPoint2DDouble &p, const std::vector<wxPoint2DDouble> &v) {
   bool in = false;
   for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
      if (((v[i].m_y > p.m_y) != (v[j].m_y > p.m_y)) &&
          (p.m_x < (v[j].m_x - v[i].m_x) * (p.m_y - v[i].m_y) / (v[j].m_y - v[i].m_y) + v[i].m_x))
         in = ! in;
   }
   return in;
}

wxGraphicsPen make_pen(wxGraphicsContext *gc, const ChartStyle &style) {
   if (! style.stroke.IsOk())
      return wxNullGraphicsPen;
   wxGraphicsPenInfo info(style.stroke, style.stroke_width);
   std::vector<wxDash> dashes;
   if (! style.dashes.empty() && style.stroke_width > 0.0) {
      // wx measures dashes in multiples of the line width
      for (double d : style.dashes)
         dashes.push_back(static_cast<wxDash>(std::clamp(std::lround(d / style.stroke_width), 1L, 127L)));
      info.Style(wxPENSTYLE_USER_DASH).Dashes(int(dashes.size()), dashes.data());
   }
   return gc->CreatePen(info);
}

wxGraphicsBrush make_brush(wxGraphicsContext *gc, const ChartStyle &style) {
   if (! style.fill.IsOk())
      return wxNullGraphicsBrush;
   return gc->CreateBrush(wxBrush(style.fill));
}

} // namespace

ChartCanvas::ChartCanvas(wxWindow *parent, wxWindowID id)
   : wxScrolledCanvas(parent, id) {

   SetBackgroundStyle(wxBG_STYLE_PAINT);
   SetBackgroundColour(*wxWHITE);
   Bind(wxEVT_PAINT, &ChartCanvas::on_paint, this);
   Bind(wxEVT_SIZE,  &ChartCanvas::on_size,  this);
   for (auto type : { wxEVT_LEFT_DOWN, wxEVT_MIDDLE_DOWN, wxEVT_RIGHT_DOWN,
                      wxEVT_LEFT_DCLICK, wxEVT_MIDDLE_DCLICK, wxEVT_RIGHT_DCLICK,
                      wxEVT_LEFT_UP, wxEVT_MIDDLE_UP, wxEVT_RIGHT_UP, wxEVT_MOTION })
      Bind(type, &ChartCanvas::on_mouse, this);
   Bind(wxEVT_LEAVE_WINDOW, &ChartCanvas::on_leave, this);
   layout();
}

// ---------------------------------------------------------------- geometry

void ChartCanvas::set_world_bounds(double x0, double y0, double x1, double y1) {
   world_x0 = std::min(x0, x1);
   world_y0 = std::min(y0, y1);
   world_x1 = std::max(x0, x1);
   world_y1 = std::max(y0, y1);
   layout();
   Refresh();
}

void ChartCanvas::set_scaling(Scaling mode, double points_per_unit) {
   scaling = mode;
   if (points_per_unit > 0.0)
      scale = points_per_unit;
   layout();
   Refresh();
}

void ChartCanvas::layout() {

   wxSize client = GetClientSize();
   double ww = std::max(world_x1 - world_x0, 1e-9);
   double wh = std::max(world_y1 - world_y0, 1e-9);

   if (scaling == Scaling::Fit) {
      if (client.x > 0 && client.y > 0)
         scale = std::min(client.x / ww, client.y / wh);
      offset.m_x = (client.x - ww * scale) / 2.0 - world_x0 * scale;
      offset.m_y = (client.y - wh * scale) / 2.0 - world_y0 * scale;
      SetScrollRate(0, 0);
      SetVirtualSize(client);
   } else {
      offset.m_x = -world_x0 * scale;
      offset.m_y = -world_y0 * scale;
      SetScrollRate(10, 10);
      SetVirtualSize(int(std::ceil(ww * scale)), int(std::ceil(wh * scale)));
   }
}

wxPoint2DDouble ChartCanvas::to_world(const wxPoint &window_point) const {
   wxPoint p = CalcUnscrolledPosition(window_point);
   return wxPoint2DDouble((p.x - offset.m_x) / scale, (p.y - offset.m_y) / scale);
}

// ---------------------------------------------------------------- items

ChartCanvas::item_id ChartCanvas::add(Item &&item) {
   item_id id = next_id++;
   item.id = id;
   items.push_back(std::move(item));
   Refresh();
   return id;
}

ChartCanvas::Item *ChartCanvas::find(item_id id) {
   // ids increase in insertion order, and items stay in insertion order
   auto it = std::lower_bound(items.begin(), items.end(), id,
                              [](const Item &i, item_id v) { return i.id < v; });
   return (it != items.end() && it->id == id) ? &*it : nullptr;
}

const ChartCanvas::Item *ChartCanvas::find(item_id id) const {
   return const_cast<ChartCanvas *>(this)->find(id);
}

ChartCanvas::item_id ChartCanvas::add_rect(double x, double y, double w, double h,
                                           const ChartStyle &style, int group) {
   Item i;
   i.kind = Kind::Rect;
   i.points = { wxPoint2DDouble(x, y), wxPoint2DDouble(w, h) };
   i.style = style;
   i.group = group;
   return add(std::move(i));
}

ChartCanvas::item_id ChartCanvas::add_ellipse(double cx, double cy, double rx, double ry,
                                              const ChartStyle &style, int group) {
   Item i;
   i.kind = Kind::Ellipse;
   i.points = { wxPoint2DDouble(cx - rx, cy - ry), wxPoint2DDouble(2 * rx, 2 * ry) };
   i.style = style;
   i.group = group;
   return add(std::move(i));
}

ChartCanvas::item_id ChartCanvas::add_polyline(const std::vector<wxPoint2DDouble> &points,
                                               bool closed, const ChartStyle &style, int group) {
   Item i;
   i.kind = Kind::Polyline;
   i.points = points;
   i.closed = closed;
   i.style = style;
   i.group = group;
   return add(std::move(i));
}

ChartCanvas::item_id ChartCanvas::add_text(double x, double y, const wxString &text,
                                           ChartAnchor anchor, const wxFont &font,
                                           const wxColour &colour, int group) {
   Item i;
   i.kind = Kind::Text;
   i.points = { wxPoint2DDouble(x, y) };
   i.text = text;
   i.anchor = anchor;
   i.font = font;
   i.style.stroke = colour;
   i.group = group;
   return add(std::move(i));
}

ChartCanvas::item_id ChartCanvas::add_image(double x, double y, double w, double h,
                                            const wxBitmap &bitmap, int group) {
   Item i;
   i.kind = Kind::Image;
   i.points = { wxPoint2DDouble(x, y), wxPoint2DDouble(w, h) };
   i.bitmap = bitmap;
   i.group = group;
   return add(std::move(i));
}

void ChartCanvas::set_style(item_id id, const ChartStyle &style) {
   if (Item *i = find(id)) { i->style = style; Refresh(); }
}

void ChartCanvas::set_clickable(item_id id, bool state) {
   if (Item *i = find(id)) i->clickable = state;
}

void ChartCanvas::set_tag(item_id id, long tag) {
   if (Item *i = find(id)) i->tag = tag;
}

long ChartCanvas::get_tag(item_id id) const {
   const Item *i = find(id);
   return i ? i->tag : 0;
}

void ChartCanvas::remove(item_id id) {
   auto it = std::lower_bound(items.begin(), items.end(), id,
                              [](const Item &i, item_id v) { return i.id < v; });
   if (it != items.end() && it->id == id) {
      items.erase(it);
      if (hovered == id) hovered = no_item;
      Refresh();
   }
}

void ChartCanvas::clear_group(int group) {
   items.erase(std::remove_if(items.begin(), items.end(),
                              [group](const Item &i) { return i.group == group; }),
               items.end());
   if (! find(hovered)) hovered = no_item;
   Refresh();
}

void ChartCanvas::clear() {
   items.clear();
   hovered = no_item;
   Refresh();
}

// ---------------------------------------------------------------- hit testing

bool ChartCanvas::hits(const Item &i, const wxPoint2DDouble &p, double tol) const {

   switch (i.kind) {
   case Kind::Rect:
   case Kind::Image: {
      wxRect2DDouble r(i.points[0].m_x - tol, i.points[0].m_y - tol,
                       i.points[1].m_x + 2 * tol, i.points[1].m_y + 2 * tol);
      return r.Contains(p);
   }
   case Kind::Ellipse: {
      double rx = i.points[1].m_x / 2 + tol, ry = i.points[1].m_y / 2 + tol;
      double dx = (p.m_x - (i.points[0].m_x + i.points[1].m_x / 2)) / rx;
      double dy = (p.m_y - (i.points[0].m_y + i.points[1].m_y / 2)) / ry;
      return dx * dx + dy * dy <= 1.0;
   }
   case Kind::Polyline: {
      if (i.points.empty()) return false;
      if (i.closed && i.style.fill.IsOk() && inside_polygon(p, i.points))
         return true;
      double reach = std::max(i.style.stroke_width / 2, tol);
      size_t n = i.points.size();
      for (size_t k = 0; k + 1 < n; k++)
         if (distance_to_segment(p, i.points[k], i.points[k + 1]) <= reach) return true;
      if (i.closed && n > 2 && distance_to_segment(p, i.points[n - 1], i.points[0]) <= reach)
         return true;
      if (n == 1)
         return distance_to_segment(p, i.points[0], i.points[0]) <= reach;
      return false;
   }
   case Kind::Text:
      return i.text_bounds.Contains(p);
   }
   return false;
}

ChartCanvas::item_id ChartCanvas::hit_test(const wxPoint2DDouble &p) const {
   double tol = hit_tolerance_points / scale;
   for (auto it = items.rbegin(); it != items.rend(); ++it)
      if (it->clickable && hits(*it, p, tol))
         return it->id;
   return no_item;
}

// ---------------------------------------------------------------- drawing

void ChartCanvas::on_paint(wxPaintEvent &) {

   wxAutoBufferedPaintDC dc(this);
   dc.SetBackground(wxBrush(GetBackgroundColour()));
   dc.Clear();

   std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
   if (! gc) return;

   wxPoint view = CalcUnscrolledPosition(wxPoint(0, 0));
   gc->Translate(offset.m_x - view.x, offset.m_y - view.y);
   gc->Scale(scale, scale);
   draw_items(gc.get());
}

void ChartCanvas::render(wxGraphicsContext *gc, double points_per_unit) {
   gc->Scale(points_per_unit, points_per_unit);
   gc->Translate(-world_x0, -world_y0);
   draw_items(gc);
}

void ChartCanvas::draw_items(wxGraphicsContext *gc) {

   for (Item &i : items) {
      switch (i.kind) {
      case Kind::Rect:
      case Kind::Ellipse: {
         gc->SetPen(make_pen(gc, i.style));
         gc->SetBrush(make_brush(gc, i.style));
         if (i.kind == Kind::Rect)
            gc->DrawRectangle(i.points[0].m_x, i.points[0].m_y, i.points[1].m_x, i.points[1].m_y);
         else
            gc->DrawEllipse(i.points[0].m_x, i.points[0].m_y, i.points[1].m_x, i.points[1].m_y);
         break;
      }
      case Kind::Polyline: {
         if (i.points.empty()) break;
         wxGraphicsPath path = gc->CreatePath();
         path.MoveToPoint(i.points[0]);
         for (size_t k = 1; k < i.points.size(); k++)
            path.AddLineToPoint(i.points[k]);
         if (i.closed) path.CloseSubpath();
         if (i.closed && i.style.fill.IsOk()) {
            gc->SetBrush(make_brush(gc, i.style));
            gc->FillPath(path);
         }
         if (i.style.stroke.IsOk()) {
            gc->SetPen(make_pen(gc, i.style));
            gc->StrokePath(path);
         }
         break;
      }
      case Kind::Text: {
         gc->SetFont(i.font, i.style.stroke);
         double w = 0, h = 0;
         gc->GetTextExtent(i.text, &w, &h);
         double x = i.points[0].m_x, y = i.points[0].m_y;
         switch (i.anchor) {
         case ChartAnchor::N:  case ChartAnchor::CENTRE: case ChartAnchor::S:  x -= w / 2; break;
         case ChartAnchor::NE: case ChartAnchor::E:      case ChartAnchor::SE: x -= w;     break;
         default: break;
         }
         switch (i.anchor) {
         case ChartAnchor::W:  case ChartAnchor::CENTRE: case ChartAnchor::E:  y -= h / 2; break;
         case ChartAnchor::SW: case ChartAnchor::S:      case ChartAnchor::SE: y -= h;     break;
         default: break;
         }
         gc->DrawText(i.text, x, y);
         i.text_bounds = wxRect2DDouble(x, y, w, h);
         break;
      }
      case Kind::Image:
         if (i.bitmap.IsOk())
            gc->DrawBitmap(i.bitmap, i.points[0].m_x, i.points[0].m_y,
                           i.points[1].m_x, i.points[1].m_y);
         break;
      }
   }
}

void ChartCanvas::on_size(wxSizeEvent &e) {
   layout();
   Refresh();
   e.Skip();
}

// ---------------------------------------------------------------- events

void ChartCanvas::on_mouse(wxMouseEvent &e) {

   wxPoint2DDouble w = to_world(e.GetPosition());
   item_id hit = hit_test(w);

   if (e.GetEventType() == wxEVT_MOTION) {
      if (hit != hovered) {
         item_id left = hovered;
         hovered = hit;
         if (on_hover) on_hover(hit, left);
      }
      if (on_motion) on_motion(hit, e, w);
   } else if (e.ButtonDown() || e.ButtonDClick()) {
      if (on_press) on_press(hit, e, w);
   } else if (e.ButtonUp()) {
      if (on_release) on_release(hit, e, w);
   }
   e.Skip();
}

void ChartCanvas::on_leave(wxMouseEvent &e) {
   if (hovered != no_item) {
      item_id left = hovered;
      hovered = no_item;
      if (on_hover) on_hover(no_item, left);
   }
   e.Skip();
}

} // namespace wxui

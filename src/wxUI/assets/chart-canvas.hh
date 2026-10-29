// src/wxUI/assets/chart-canvas.hh
//
// ChartCanvas: a clickable drawing surface for the charts that used goocanvas
// (sequence view, Ramachandran plot, validation graphs).
//
// Items are kept in a list and redrawn from it, in the order added (later items
// on top). Coordinates are "world" units chosen by the caller; the canvas maps
// them to the window either by fitting the world bounds to it or at a fixed
// scale with scrolling. Line widths, dashes and fonts are in world units too,
// so a chart looks the same at any size. Clicks and hovering report the
// topmost clickable item under the pointer.

#ifndef BANDICOOT_WXUI_CHART_CANVAS_HH
#define BANDICOOT_WXUI_CHART_CANVAS_HH

#include <wx/scrolwin.h>
#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/geometry.h>

#include <functional>
#include <vector>

class wxGraphicsContext;

namespace wxui {

struct ChartStyle {
   wxColour stroke = *wxBLACK;    // wxNullColour: no outline
   double   stroke_width = 1.0;
   std::vector<double> dashes;    // alternating on/off lengths; empty is solid
   wxColour fill = wxNullColour;  // wxNullColour: not filled
};

enum class ChartAnchor { NW, N, NE, W, CENTRE, E, SW, S, SE };

class ChartCanvas : public wxScrolledCanvas {
public:
   typedef int item_id;           // -1: no item
   static const item_id no_item = -1;

   enum class Scaling {
      Fit,   // the world bounds fill the window, keeping their aspect ratio
      Fixed  // a fixed number of points per world unit; scrolls when larger
   };

   explicit ChartCanvas(wxWindow *parent, wxWindowID id = wxID_ANY);

   void set_world_bounds(double x0, double y0, double x1, double y1);
   void set_scaling(Scaling mode, double points_per_unit = 1.0);
   double get_scale() const { return scale; }

   // Items. group lets related items be removed together (clear_group()).
   item_id add_rect(double x, double y, double w, double h,
                    const ChartStyle &style, int group = 0);
   item_id add_ellipse(double cx, double cy, double rx, double ry,
                       const ChartStyle &style, int group = 0);
   item_id add_polyline(const std::vector<wxPoint2DDouble> &points, bool closed,
                        const ChartStyle &style, int group = 0);
   item_id add_text(double x, double y, const wxString &text, ChartAnchor anchor,
                    const wxFont &font, const wxColour &colour, int group = 0);
   item_id add_image(double x, double y, double w, double h,
                     const wxBitmap &bitmap, int group = 0);

   void set_style(item_id item, const ChartStyle &style);
   void set_clickable(item_id item, bool state);
   // A caller-defined value, e.g. a residue index, read back in event handlers.
   void set_tag(item_id item, long tag);
   long get_tag(item_id item) const;

   void remove(item_id item);
   void clear_group(int group);
   void clear();

   // Draws every item into gc, the top-left of the world bounds at its
   // origin, at points_per_unit (to save the chart as an image, say).
   void render(wxGraphicsContext *gc, double points_per_unit);
   double world_width() const { return world_x1 - world_x0; }
   double world_height() const { return world_y1 - world_y0; }

   wxPoint2DDouble to_world(const wxPoint &window_point) const;
   // topmost clickable item at a world position
   item_id hit_test(const wxPoint2DDouble &world_point) const;

   // Event handlers; item is no_item over the background.
   std::function<void(item_id, const wxMouseEvent &, const wxPoint2DDouble &)> on_press;
   std::function<void(item_id, const wxMouseEvent &, const wxPoint2DDouble &)> on_release;
   std::function<void(item_id, const wxMouseEvent &, const wxPoint2DDouble &)> on_motion;
   // the pointer moved from one item to another (either may be no_item)
   std::function<void(item_id entered, item_id left)> on_hover;

private:
   enum class Kind { Rect, Ellipse, Polyline, Text, Image };
   struct Item {
      Kind kind;
      item_id id = no_item;
      bool clickable = false;
      int group = 0;
      long tag = 0;
      ChartStyle style;
      std::vector<wxPoint2DDouble> points; // rect/ellipse/image: corner, size
      bool closed = false;
      wxString text;
      ChartAnchor anchor = ChartAnchor::NW;
      wxFont font;
      wxBitmap bitmap;
      wxRect2DDouble text_bounds;  // world units, measured when drawn
   };

   std::vector<Item> items;     // drawing order; ids increase along it
   item_id next_id = 0;
   double world_x0 = 0, world_y0 = 0, world_x1 = 100, world_y1 = 100;
   Scaling scaling = Scaling::Fit;
   double scale = 1.0;            // points per world unit, as last laid out
   wxPoint2DDouble offset;        // window position of the world origin
   item_id hovered = no_item;

   item_id add(Item &&item);
   Item *find(item_id item);
   const Item *find(item_id item) const;
   void layout();
   bool hits(const Item &item, const wxPoint2DDouble &p, double tolerance) const;

   void draw_items(wxGraphicsContext *gc);
   void on_paint(wxPaintEvent &e);
   void on_size(wxSizeEvent &e);
   void on_mouse(wxMouseEvent &e);
   void on_leave(wxMouseEvent &e);
};

} // namespace wxui

#endif // BANDICOOT_WXUI_CHART_CANVAS_HH

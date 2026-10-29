// src/wxUI/main/map-properties.cc

#include "wxUI/main/map-properties.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/chart-canvas.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/main/edit-menu.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/display.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxString two_places(double v) {
   return wxString::Format("%.2f", v);
}

// A chosen radio button or ticked box has its label in Coot's "on" colour.
void show_chosen(const std::vector<wxRadioButton *> &group) {
   for (wxRadioButton *r : group) {
      r->SetForegroundColour(r->GetValue() ? palette::text_on() : palette::text());
      r->Refresh();
   }
}

void show_ticked(wxCheckBox *c) {
   c->SetForegroundColour(c->GetValue() ? palette::text_on() : palette::text());
   c->Refresh();
}

// Keeps a group's labels coloured as the choice changes.
void colour_on_choice(const std::vector<wxRadioButton *> &group) {
   show_chosen(group);
   for (wxRadioButton *r : group)
      r->Bind(wxEVT_RADIOBUTTON, [group](wxCommandEvent &e) {
         show_chosen(group);
         e.Skip();
      });
}

bool read_float(wxTextCtrl *entry, float &value) {
   double v;
   if (! entry->GetValue().Trim().Trim(false).ToCDouble(&v)) {
      wxBell();
      entry->SetFocus();
      return false;
   }
   value = float(v);
   return true;
}

// ---------------------------------------------- the histogram

// The map's density histogram, its middle four r.m.s.d. marked off along the
// bottom, with a bar at the contour level that can be dragged to change it.
// Laid out as Coot's goograph draws it: the marked range takes the left part
// of the chart and the density runs on to its right edge, so a contour level
// above the range is still on the chart.
class Histogram : public ChartCanvas {
public:
   std::function<void(float level)> on_level_dragged;

   Histogram(wxWindow *parent) : ChartCanvas(parent) {
      set_world_bounds(0, 0, width, height);
      set_scaling(Scaling::Fit);
      SetMinSize(FromDIP(wxSize(int(width), int(height))));
      on_press = [this](item_id item, const wxMouseEvent &, const wxPoint2DDouble &) {
         if (item != no_item && item == bar) {
            dragging = true;
            CaptureMouse();
         }
      };
      on_motion = [this](item_id, const wxMouseEvent &, const wxPoint2DDouble &w) {
         if (dragging) draw_bar(to_density(w.m_x));
      };
      on_release = [this](item_id, const wxMouseEvent &, const wxPoint2DDouble &w) {
         if (! dragging) return;
         end_drag();
         float level = to_density(w.m_x);
         draw_bar(level);
         if (on_level_dragged) on_level_dragged(level);
      };
      Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) { dragging = false; });
   }

   void set_data(const bandicoot_map_properties &p) {
      clear();
      bar = no_item;
      bins = p.bins;
      min_density = p.min_density;
      bin_width = p.bin_width;
      rmsd = p.rmsd;
      x_min = p.mean - 2 * p.rmsd;
      x_range = 4 * p.rmsd;
      colour = wxColour(int(255 * p.colour.r), int(255 * p.colour.g), int(255 * p.colour.b));
      if (bins.empty() || x_range <= 0 || bin_width <= 0) return;
      draw_density();
      draw_ticks();
      draw_bar(p.contour_level);
   }

   void set_level(float level) {
      if (! bins.empty() && x_range > 0) draw_bar(level);
   }

private:
   const double width = 300, height = 76;
   const double baseline = 57;     // the density's zero
   const double plot_left = 10;    // where the marked range starts
   const double plot_width = 172;  // and its width
   const int bar_group = 1;

   std::vector<int> bins;
   float min_density = 0, bin_width = 0, rmsd = 0;
   double x_min = 0, x_range = 1;
   wxColour colour;
   item_id bar = no_item;
   bool dragging = false;

   double to_chart(double density) const {
      return plot_left + plot_width * (density - x_min) / x_range;
   }
   float to_density(double chart_x) const {
      return float(x_min + (chart_x - plot_left) * x_range / plot_width);
   }

   void end_drag() {
      dragging = false;
      if (HasCapture()) ReleaseMouse();
   }

   void draw_density() {
      // scaled so the tallest bin but one reaches near the top: the tallest
      // is often a spike (at zero, say) that would flatten the rest
      int top = 0, second = 0;
      for (int n : bins) {
         if (n > top) { second = top; top = n; }
         else if (n > second) second = n;
      }
      if (second <= 0) second = top;
      if (second <= 0) return;
      double y_scale = (baseline - 15) / second;

      std::vector<wxPoint2DDouble> outline;
      double first_x = std::max(0.0, to_chart(min_density));
      outline.push_back(wxPoint2DDouble(first_x, baseline));
      double last_x = first_x;
      for (size_t i = 0; i < bins.size(); i++) {
         double x0 = to_chart(min_density + i * bin_width);
         double x1 = to_chart(min_density + (i + 1) * bin_width);
         if (x1 < 0) continue;
         if (x0 > width) break;
         x0 = std::max(0.0, x0);
         x1 = std::min(width, x1);
         double y = std::max(0.0, baseline - bins[i] * y_scale);
         outline.push_back(wxPoint2DDouble(x0, y));
         outline.push_back(wxPoint2DDouble(x1, y));
         last_x = x1;
      }
      outline.push_back(wxPoint2DDouble(last_x, baseline));
      ChartStyle fill;
      fill.stroke = wxColour(0x33, 0x33, 0x33);
      fill.stroke_width = 1.0;
      fill.fill = wxColour(0x11, 0x11, 0x11);
      add_polyline(outline, true, fill);
   }

   // Major ticks on round values, labelled, with unlabelled minor ticks at a
   // fifth of the spacing, as goograph chooses them.
   void draw_ticks() {
      double major = tick_spacing(x_range);
      double minor = major * 0.2;
      int decimals = x_range < 2 ? 2 : (x_range < 5 ? 1 : 0);
      double x_max = x_min + x_range;
      ChartStyle tick;
      tick.stroke = wxColour(0x11, 0x11, 0x11);
      wxFont font(wxFontInfo(7));
      double last_label_right = -1e9;
      for (double t = std::ceil(x_min / minor - 1e-6) * minor; t <= x_max + 1e-9; t += minor) {
         bool is_major = std::fabs(t / major - std::round(t / major)) < 1e-4;
         double x = to_chart(t);
         double length = is_major ? 4 : 2;
         add_polyline({ wxPoint2DDouble(x, baseline), wxPoint2DDouble(x, baseline + length) }, false, tick);
         if (! is_major) continue;
         wxString label = wxString::Format("%.*f", decimals, std::fabs(t) < 1e-9 ? 0.0 : t);
         double half = GetTextExtent(label).x * 0.5;
         if (x - half < last_label_right + 2) continue; // would overlap the one before
         add_text(x, baseline + 6, label, ChartAnchor::N, font, wxColour(0x33, 0x33, 0x33));
         last_label_right = x + half;
      }
   }

   // the nearest of 0.1, 0.2, 0.5, 1, 2, 5, ... to a tenth of the range
   static double tick_spacing(double range) {
      double m = 1.0 / std::log(10.0);
      double r1 = std::log(range * 0.1) * m, d1 = std::fabs(r1 - std::nearbyint(r1));
      double r2 = std::log(range / 2) * m,   d2 = std::fabs(r2 - std::nearbyint(r2));
      double r5 = std::log(range / 5) * m,   d5 = std::fabs(r5 - std::nearbyint(r5));
      double r = std::pow(10, std::nearbyint(r1));
      if (d2 < d1) {
         r = 0.2 * std::pow(10, std::nearbyint(r2));
         if (d5 < d2) r = 0.5 * std::pow(10, std::nearbyint(r5));
      }
      return r;
   }

   void draw_bar(float level) {
      clear_group(bar_group);
      double half_width = 0.07 * rmsd * plot_width / x_range;
      double x = to_chart(level);
      ChartStyle style;
      style.stroke = wxColour(0x11, 0x11, 0x11);
      style.stroke_width = 1.4;
      style.fill = colour;
      bar = add_rect(x - half_width, 10, 2 * half_width, baseline + 4 - 10, style, bar_group);
      set_clickable(bar, true);
   }
};

// ---------------------------------------------- the window

class MapProperties : public wxDialog {
public:
   MapProperties(wxWindow *parent, int imol_in, const bandicoot_map_properties &p)
      : wxDialog(parent, wxID_ANY, wxString::Format("Properties for Map %d", imol_in),
                 wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        imol(imol_in) {

      SetBackgroundColour(palette::face());
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      wxStaticText *heading = new wxStaticText(this, wxID_ANY, "Map Settings");
      heading->SetFont(heading->GetFont().Bold());
      outer->Add(heading, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 6);

      // scrolls only when the screen is too small for it
      wxScrolledWindow *scroller = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                                        wxDefaultSize, wxVSCROLL | wxHSCROLL);
      scroller->SetBackgroundColour(palette::face());
      scroller->SetScrollRate(10, 10);
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      col->Add(cell_and_symmetry(scroller, p), 0, wxEXPAND | wxBOTTOM, 10);
      col->Add(map_style(scroller, p), 0, wxEXPAND | wxBOTTOM, 30);
      col->Add(contouring(scroller, p), 0, wxEXPAND | wxBOTTOM, 30);
      col->Add(map_colour(scroller), 0, wxEXPAND | wxBOTTOM, 10);
      col->Add(skeleton(scroller, p), 0, wxEXPAND);

      wxBoxSizer *pad = new wxBoxSizer(wxVERTICAL);
      pad->Add(col, 1, wxEXPAND | wxALL, 10);
      scroller->SetSizer(pad);
      outer->Add(scroller, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

      BevelButton *ok = new BevelButton(this, wxID_ANY, "OK");
      ok->set_icon(icon("stock-button-ok.png", wxSize(16, 16)));
      ok->SetMinSize(wxSize(80, 28));
      ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply_and_close(); });
      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      buttons->AddStretchSpacer();
      buttons->Add(ok);
      outer->Add(buttons, 0, wxEXPAND | wxALL, 10);
      SetSizer(outer);

      // as big as the content, within the screen
      wxSize best = pad->GetMinSize();
      wxSize chrome = outer->GetMinSize() - scroller->GetMinSize();
      wxSize want(best.x + chrome.x + 8, best.y + chrome.y + 8);
      int n = wxDisplay::GetFromWindow(parent);
      wxRect area = wxDisplay(n == wxNOT_FOUND ? 0 : n).GetClientArea();
      want.y = std::min(want.y, area.height - 40);
      SetSize(want);
      scroller->FitInside();

      // the colour may have changed in its own window meanwhile
      Bind(wxEVT_ACTIVATE, [this](wxActivateEvent &e) {
         if (e.GetActive()) refresh_histogram();
         e.Skip();
      });
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
   }

private:
   int imol;
   wxSlider *opacity = nullptr;
   wxStaticText *opacity_value = nullptr;
   wxTextCtrl *level_entry = nullptr;
   wxRadioButton *level_absolute = nullptr, *level_rmsd = nullptr;
   Histogram *histogram = nullptr;
   wxCheckBox *by_rmsd = nullptr;
   wxTextCtrl *step_entry = nullptr;
   wxRadioButton *skeleton_on = nullptr;

   static wxStaticText *text(wxWindow *parent, const wxString &s) {
      return new wxStaticText(parent, wxID_ANY, s);
   }

   wxWindow *cell_and_symmetry(wxWindow *parent, const bandicoot_map_properties &p) {
      TitledPanel *f = new TitledPanel(parent, "Cell and Symmetry:", true);
      wxWindow *c = f->content();
      wxBoxSizer *s = f->content_sizer();
      s->Add(text(c, "Cell:"));
      s->Add(text(c, wxString::FromUTF8(p.cell)), 0, wxLEFT | wxTOP | wxBOTTOM, 4);
      s->Add(text(c, "Spacegroup:"));
      s->Add(text(c, wxString::FromUTF8(p.spacegroup)), 0, wxTOP | wxBOTTOM, 4);
      s->Add(text(c, "Resolution:"));
      s->Add(text(c, wxString::FromUTF8(p.resolution)), 0, wxTOP, 4);
      return f;
   }

   wxWindow *map_style(wxWindow *parent, const bandicoot_map_properties &p) {
      TitledPanel *f = new TitledPanel(parent, "Displayed Map Style:", true);
      wxWindow *c = f->content();
      wxBoxSizer *s = f->content_sizer();
      wxRadioButton *lines = new wxRadioButton(c, wxID_ANY, "Standard Lines", wxDefaultPosition,
                                               wxDefaultSize, wxRB_GROUP);
      wxRadioButton *solid = new wxRadioButton(c, wxID_ANY, "Solid/Transparent");
      (p.solid_surface ? solid : lines)->SetValue(true);
      s->Add(lines, 0, wxLEFT | wxBOTTOM, 20);
      s->Add(solid, 0, wxLEFT | wxBOTTOM, 20);
      lines->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent &e) {
         bandicoot_core_set_map_surface(imol, false);
         e.Skip();
      });
      solid->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent &e) {
         bandicoot_core_set_map_surface(imol, true);
         e.Skip();
      });
      colour_on_choice({ lines, solid });

      // the value sits over the slider, as GTK's scale shows it
      opacity = new wxSlider(c, wxID_ANY, int(std::lround(p.opacity * 1000)), 0, 1000,
                             wxDefaultPosition, wxSize(220, -1));
      opacity_value = text(c, wxEmptyString);
      opacity_value->SetForegroundColour(palette::text_on());
      show_opacity();
      opacity->Bind(wxEVT_SLIDER, [this](wxCommandEvent &) {
         show_opacity();
         bandicoot_core_set_map_opacity(imol, opacity->GetValue() / 1000.0f);
      });
      wxBoxSizer *slider_col = new wxBoxSizer(wxVERTICAL);
      slider_col->Add(opacity_value, 0, wxALIGN_CENTER_HORIZONTAL);
      slider_col->Add(opacity, 0, wxEXPAND);
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(text(c, "Opacity (%):"), 0, wxALIGN_BOTTOM | wxRIGHT, 8);
      row->Add(slider_col, 1);
      s->Add(row, 0, wxEXPAND | wxLEFT, 20);
      return f;
   }

   void show_opacity() {
      opacity_value->SetLabel(wxString::Format("%.1f", opacity->GetValue() / 10.0));
      opacity_value->GetParent()->Layout();
   }

   wxWindow *contouring(wxWindow *parent, const bandicoot_map_properties &p) {
      TitledPanel *f = new TitledPanel(parent, "Contouring:", true);
      wxWindow *c = f->content();
      wxBoxSizer *s = f->content_sizer();

      // Contour Level: set it, and the histogram
      TitledPanel *level = new TitledPanel(c, "Contour Level:", true);
      wxWindow *lc = level->content();
      level_entry = new wxTextCtrl(lc, wxID_ANY, two_places(p.contour_level), wxDefaultPosition,
                                   wxSize(90, -1), wxTE_PROCESS_ENTER);
      level_absolute = new wxRadioButton(lc, wxID_ANY, "absolute", wxDefaultPosition,
                                         wxDefaultSize, wxRB_GROUP);
      level_rmsd = new wxRadioButton(lc, wxID_ANY, "r.m.s.d.");
      level_absolute->SetValue(true);
      colour_on_choice({ level_absolute, level_rmsd });
      BevelButton *apply = new BevelButton(lc, wxID_ANY, "Apply");
      apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { apply_level(); });
      level_entry->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { apply_level(); });
      wxBoxSizer *kinds = new wxBoxSizer(wxVERTICAL);
      kinds->Add(level_absolute);
      kinds->Add(level_rmsd, 0, wxTOP, 4);
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(text(lc, "Set Level:"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 8);
      row->Add(level_entry, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
      row->Add(kinds, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
      row->Add(apply, 0, wxALIGN_TOP);
      level->content_sizer()->Add(row, 0, wxBOTTOM, 8);

      TitledPanel *hist_frame = new TitledPanel(lc, "Map Histogram:", true);
      histogram = new Histogram(hist_frame->content());
      histogram->set_data(p);
      histogram->on_level_dragged = [this](float level) {
         bandicoot_core_set_map_contour_level(imol, level, false);
         level_absolute->SetValue(true);
         show_chosen({ level_absolute, level_rmsd });
         level_entry->ChangeValue(two_places(level));
      };
      hist_frame->content_sizer()->Add(histogram, 0);
      if (p.bins.empty()) hist_frame->Hide();
      level->content_sizer()->Add(hist_frame, 0, wxEXPAND);
      s->Add(level, 0, wxEXPAND | wxBOTTOM, 10);

      // Contour Level Step Size
      TitledPanel *step = new TitledPanel(c, "Contour Level Step Size:", true);
      wxWindow *sc = step->content();
      by_rmsd = new wxCheckBox(sc, wxID_ANY, "Change by rmsd?");
      step_entry = new wxTextCtrl(sc, wxID_ANY, two_places(p.rmsd_step), wxDefaultPosition,
                                  wxSize(90, -1));
      by_rmsd->SetValue(p.step_by_rmsd);
      step_entry->Enable(p.step_by_rmsd);
      show_ticked(by_rmsd);
      by_rmsd->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) {
         show_ticked(by_rmsd);
         bool on = by_rmsd->GetValue();
         step_entry->Enable(on);
         float v = 0;
         if (on && ! read_float(step_entry, v)) return;
         bandicoot_core_set_map_rmsd_step(imol, on, v);
      });
      wxBoxSizer *entry_col = new wxBoxSizer(wxVERTICAL);
      entry_col->Add(text(sc, "r.m.s.d. step"), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 2);
      entry_col->Add(step_entry);
      wxBoxSizer *step_row = new wxBoxSizer(wxHORIZONTAL);
      step_row->Add(by_rmsd, 0, wxALIGN_BOTTOM | wxLEFT | wxRIGHT, 20);
      step_row->Add(entry_col);
      step->content_sizer()->Add(step_row);
      s->Add(step, 0, wxEXPAND);
      return f;
   }

   wxWindow *map_colour(wxWindow *parent) {
      TitledPanel *f = new TitledPanel(parent);
      wxWindow *c = f->content();
      BevelButton *colour = new BevelButton(c, wxID_ANY, "Colour");
      colour->SetMinSize(wxSize(100, 28));
      colour->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { show_map_colour(this, imol); });
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(text(c, "Map Colour"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 20);
      row->Add(colour);
      f->content_sizer()->Add(row, 0, wxTOP | wxBOTTOM, 6);
      return f;
   }

   wxWindow *skeleton(wxWindow *parent, const bandicoot_map_properties &p) {
      TitledPanel *f = new TitledPanel(parent, "Skeleton:", true);
      wxWindow *c = f->content();
      skeleton_on = new wxRadioButton(c, wxID_ANY, "On", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
      wxRadioButton *off = new wxRadioButton(c, wxID_ANY, "Off");
      (p.skeleton ? skeleton_on : off)->SetValue(true);
      colour_on_choice({ skeleton_on, off });
      f->content_sizer()->Add(skeleton_on, 0, wxBOTTOM, 10);
      f->content_sizer()->Add(off);
      return f;
   }

   void apply_level() {
      float level;
      if (! read_float(level_entry, level)) return;
      bandicoot_core_set_map_contour_level(imol, level, level_rmsd->GetValue());
      refresh_histogram();
   }

   void refresh_histogram() {
      bandicoot_map_properties p;
      if (bandicoot_core_map_properties(imol, &p)) histogram->set_data(p);
   }

   // The step size and the skeleton take effect on OK, as in Coot.
   void apply_and_close() {
      float step = 0;
      if (by_rmsd->GetValue() && ! read_float(step_entry, step)) return;
      bandicoot_core_set_map_rmsd_step(imol, by_rmsd->GetValue(), step);
      bandicoot_core_set_map_skeleton(imol, skeleton_on->GetValue());
      Close();
   }
};

std::map<int, MapProperties *> shown;

} // namespace

void show_map_properties(wxWindow *parent, int imol) {
   auto it = shown.find(imol);
   if (it != shown.end()) {
      it->second->Raise();
      return;
   }
   bandicoot_map_properties p;
   if (! bandicoot_core_map_properties(imol, &p)) return;
   MapProperties *d = new MapProperties(parent, imol, p);
   shown[imol] = d;
   d->Bind(wxEVT_DESTROY, [d, imol](wxWindowDestroyEvent &e) {
      if (e.GetEventObject() == d) shown.erase(imol);
      e.Skip();
   });
   d->CentreOnParent();
   d->Show();
}

} // namespace wxui

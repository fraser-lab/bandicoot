// src/wxUI/main/validation-graphs.cc

#include "wxUI/main/validation-graphs.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/chart-canvas.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"

#include <wx/button.h>
#include <wx/dcclient.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

// Coot's layout, in its canvas units: each chain a strip 80 high, a residue
// 10 wide, bars standing on a base line 50 down the strip.
const double residue_width = 10.0;
const double chain_height = 80.0;
const double bar_base = 50.0;
const double bar_top = 5.0;       // a taller bar stops here, short of the chain above

// Coot's block colours, by their X11 names.
wxColour block_colour(const std::string &name) {
   static const std::map<std::string, wxColour> colours = {
      { "green1", wxColour(0x00, 0xff, 0x00) },     { "chartreuse1", wxColour(0x7f, 0xff, 0x00) },
      { "OliveDrab1", wxColour(0xc0, 0xff, 0x3e) }, { "DarkOliveGreen1", wxColour(0xca, 0xff, 0x70) },
      { "yellow2", wxColour(0xee, 0xee, 0x00) },    { "gold1", wxColour(0xff, 0xd7, 0x00) },
      { "goldenrod1", wxColour(0xff, 0xc1, 0x25) }, { "tan1", wxColour(0xff, 0xa5, 0x4f) },
      { "orange1", wxColour(0xff, 0xa5, 0x00) },    { "coral1", wxColour(0xff, 0x72, 0x56) },
      { "tomato1", wxColour(0xff, 0x63, 0x47) },    { "OrangeRed1", wxColour(0xff, 0x45, 0x00) },
      { "firebrick1", wxColour(0xff, 0x30, 0x30) }, { "red", wxColour(0xff, 0x00, 0x00) },
      { "slate blue", wxColour(0x6a, 0x5a, 0xcd) },
   };
   auto it = colours.find(name);
   return it == colours.end() ? *wxRED : it->second;
}

wxFont chart_font(int size) {
   return wxFont(wxFontInfo(size).Family(wxFONTFAMILY_SWISS));
}

class GraphWindow : public wxFrame {
public:
   static std::map<const void *, GraphWindow *> shown;

   GraphWindow(const void *graph_in, int imol_in, int type_in, const std::string &title, const std::string &label,
               int n_chains, int max_chain_length, bool rescale)
      : wxFrame(owner, wxID_ANY, wxString::FromUTF8(title)), imol(imol_in), type(type_in), graph(graph_in) {
      wxPanel *p = new wxPanel(this);
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      col->Add(new wxStaticText(p, wxID_ANY, wxString::FromUTF8(label)), 0, wxLEFT | wxTOP, 12);

      chart = new ChartCanvas(p);
      chart->SetWindowStyle(chart->GetWindowStyle() | wxBORDER_SUNKEN);
      chart->SetBackgroundColour(palette::face());
      chart->set_scaling(ChartCanvas::Scaling::Fixed, 1.0);
      world_width = max_chain_length * residue_width + 220.0;
      world_height = chain_height * n_chains + 50.0;
      chart->set_world_bounds(0, 0, world_width, world_height);
      chart->on_press = [this](ChartCanvas::item_id item, const wxMouseEvent &, const wxPoint2DDouble &) {
         auto it = blocks.find(item);
         if (it != blocks.end()) bandicoot_core_go_to_result(it->second.target);
      };
      chart->on_hover = [this](ChartCanvas::item_id entered, ChartCanvas::item_id) { show_info(entered); };
      col->Add(chart, 1, wxEXPAND | wxALL, 4);

      if (rescale) {
         wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
         wxTextCtrl *scale = new wxTextCtrl(p, wxID_ANY, wxString::Format("%.1f", bandicoot_core_density_fit_scale()),
                                            wxDefaultPosition, wxSize(90, -1));
         wxButton *apply = new wxButton(p, wxID_ANY, "Rescale");
         apply->Bind(wxEVT_BUTTON, [this, scale](wxCommandEvent &) {
            double v;
            if (! scale->GetValue().Trim().Trim(false).ToCDouble(&v) || v <= 0) { wxBell(); return; }
            bandicoot_core_graph_rescale(graph, float(v));
         });
         row->Add(scale, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
         row->Add(apply, 0, wxALIGN_CENTER_VERTICAL);
         col->Add(row, 0, wxLEFT | wxTOP, 6);
      }

      BevelButton *close = new BevelButton(p, wxID_ANY, "Close");
      close->set_icon(icon("stock-menu-close.png", wxSize(16, 16)));
      close->SetMinSize(wxSize(110, 28));
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      col->Add(close, 0, wxALIGN_RIGHT | wxALL, 8);
      p->SetSizer(col);

      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { finish(); });
      chains.resize(std::max(n_chains, 0));
      // big enough for the whole graph where the screen allows
      wxSize want(int(world_width) + 40, int(world_height) + (rescale ? 140 : 100));
      SetSize(wxSize(std::clamp(want.x, 600, 1400), std::clamp(want.y, 300, 800)));
      CentreOnParent();
      Show();
   }

   int imol, type;

   // The core no longer draws here; gone.
   void finish() {
      bandicoot_core_graph_closed(graph);
      shown.erase(graph);
      Destroy();
   }

   void axis(int chain, int nres) {
      Chain &c = at(chain);
      if (c.axis != ChartCanvas::no_item) chart->remove(c.axis);
      // the base line along the chain, and a short upright at its start
      double y0 = bar_base + 5.0 + chain * chain_height;
      double x0 = 7.0;
      ChartStyle s;
      s.stroke_width = 3.0;
      c.axis = chart->add_polyline({ { x0 + residue_width * nres + 10.0, y0 }, { x0, y0 }, { x0, y0 - 50.0 } },
                                   false, s);
   }

   void ticks(int chain, int min_resno, int max_resno) {
      int group = 1000 + chain;
      chart->clear_group(group);
      double y0 = bar_base + 5.0 + chain * chain_height;
      ChartStyle s;
      s.stroke_width = 2.0;
      for (int i = min_resno + 9; i < max_resno; i += 10) {
         double x = (i - min_resno + 1) * residue_width + 5.0;
         chart->add_polyline({ { x, y0 }, { x, y0 + 5.0 } }, false, s, group);
         chart->add_text(x - 6.0, y0 + 9.0, wxString::Format("%d", i), ChartAnchor::W, chart_font(10), *wxBLACK, group);
      }
   }

   void label(int chain, const std::string &text) {
      at(chain).label_text = wxString::FromUTF8(text);
      raise_label(chain);
   }

   // A slot can hold more than one block (Average Temp. fact. stands the
   // total in front of the side chain's); the core removes them before
   // replacing them.
   void block(int chain, const bandicoot_graph_block &b) {
      Chain &c = at(chain);
      double base = bar_base + chain * chain_height;
      double h = std::min(std::max(b.distortion * 0.5, 0.0), bar_base - bar_top);
      ChartStyle s;
      s.fill = block_colour(b.colour);
      ChartCanvas::item_id item = chart->add_rect(b.slot * residue_width, base - h, 8.0, h, s);
      chart->set_clickable(item, true);
      c.slots[b.slot].push_back(item);
      Block record;
      record.target = b.target;
      record.info = wxString::FromUTF8(b.info);
      record.chain = chain;
      record.slot = b.slot;
      blocks[item] = record;
      raise_label(chain);
   }

   void remove(int chain, int slot) {
      Chain &c = at(chain);
      auto it = c.slots.find(slot);
      if (it == c.slots.end()) return;
      for (ChartCanvas::item_id item : it->second) {
         if (item == info_for) show_info(ChartCanvas::no_item);
         blocks.erase(item);
         chart->remove(item);
      }
      c.slots.erase(it);
   }

private:
   struct Chain {
      ChartCanvas::item_id axis = ChartCanvas::no_item, label = ChartCanvas::no_item;
      wxString label_text;
      std::map<int, std::vector<ChartCanvas::item_id>> slots;
   };
   struct Block {
      bandicoot_result_row target;
      wxString info;
      int chain = 0, slot = 0;
   };

   const void *graph;
   ChartCanvas *chart;
   double world_width, world_height;
   std::vector<Chain> chains;
   std::map<ChartCanvas::item_id, Block> blocks;
   ChartCanvas::item_id info_box = ChartCanvas::no_item, info_text = ChartCanvas::no_item;
   ChartCanvas::item_id info_for = ChartCanvas::no_item;

   Chain &at(int chain) {
      if (chain >= int(chains.size())) chains.resize(chain + 1);
      return chains[std::max(chain, 0)];
   }

   // Chain labels stay above the bars, as Coot drew them last.
   void raise_label(int chain) {
      Chain &c = at(chain);
      if (c.label_text.empty()) return;
      if (c.label != ChartCanvas::no_item) chart->remove(c.label);
      c.label = chart->add_text(20.0, 10.0 + chain * chain_height, c.label_text, ChartAnchor::W, chart_font(11),
                                *wxBLACK);
   }

   // The pale green box with a block's details, under the block the pointer
   // is over.
   void show_info(ChartCanvas::item_id item) {
      if (info_box != ChartCanvas::no_item) chart->remove(info_box);
      if (info_text != ChartCanvas::no_item) chart->remove(info_text);
      info_box = info_text = info_for = ChartCanvas::no_item;
      auto it = blocks.find(item);
      if (it == blocks.end()) return;
      wxFont font = chart_font(10);
      wxClientDC dc(chart);
      dc.SetFont(font);
      double width = dc.GetTextExtent(it->second.info).GetWidth() / chart->get_scale() + 10.0;
      double x = it->second.slot * residue_width;
      double y = bar_base + it->second.chain * chain_height;
      ChartStyle s;
      s.fill = wxColour(0x98, 0xfb, 0x98);   // PaleGreen
      info_box = chart->add_rect(x, y, width, 16.0, s);
      info_text = chart->add_text(x + 5.0, y + 8.0, it->second.info, ChartAnchor::W, font, *wxBLACK);
      info_for = item;
   }
};

std::map<const void *, GraphWindow *> GraphWindow::shown;

GraphWindow *find(const void *graph) {
   auto it = GraphWindow::shown.find(graph);
   return it == GraphWindow::shown.end() ? nullptr : it->second;
}

} // namespace

void install_validation_graphs(wxWindow *parent) {
   owner = parent;
   bandicoot_graph_view v;
   v.open = [](const void *graph, int imol, int type, const std::string &title, const std::string &label,
               int n_chains, int max_chain_length, bool rescale) {
      // a graph of the same kind for the same model takes the old one's place
      std::vector<GraphWindow *> old;
      for (auto &w : GraphWindow::shown)
         if (w.second->imol == imol && w.second->type == type) old.push_back(w.second);
      for (GraphWindow *w : old) w->finish();
      GraphWindow::shown[graph] = new GraphWindow(graph, imol, type, title, label, n_chains, max_chain_length, rescale);
   };
   v.axis = [](const void *graph, int chain, int nres) {
      if (GraphWindow *w = find(graph)) w->axis(chain, nres);
   };
   v.ticks = [](const void *graph, int chain, int min_resno, int max_resno) {
      if (GraphWindow *w = find(graph)) w->ticks(chain, min_resno, max_resno);
   };
   v.label = [](const void *graph, int chain, const std::string &text) {
      if (GraphWindow *w = find(graph)) w->label(chain, text);
   };
   v.block = [](const void *graph, int chain, const bandicoot_graph_block &b) {
      if (GraphWindow *w = find(graph)) w->block(chain, b);
   };
   v.remove = [](const void *graph, int chain, int slot) {
      if (GraphWindow *w = find(graph)) w->remove(chain, slot);
   };
   bandicoot_core_set_graph_view(v);
}

} // namespace wxui

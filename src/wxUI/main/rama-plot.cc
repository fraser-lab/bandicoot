// src/wxUI/main/rama-plot.cc

#include "wxUI/main/rama-plot.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/chart-canvas.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/dcmemory.h>
#include <wx/frame.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>

#include <CoreGraphics/CoreGraphics.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

// Coot's canvas, in degrees: phi across, psi up (drawn as -psi), with room
// for the axis labels.
const double world_x0 = -240, world_y0 = -220, world_x1 = 210, world_y1 = 240;

const wxColour dodger_blue(0x1e, 0x90, 0xff);
const wxColour red3(0xcd, 0x00, 0x00);
const wxColour outline(0x33, 0x33, 0x33);

wxFont axis_font() { return wxFont(wxFontInfo(11).Family(wxFONTFAMILY_SWISS)); }

// The background for each table, read once.
const wxBitmap &background(int table) {
   static std::map<int, wxBitmap> made;
   auto it = made.find(table);
   if (it != made.end()) return it->second;
   std::vector<unsigned char> rgb;
   std::string file = bandicoot_core_rama_background(table, rgb);
   wxImage image;
   if (! file.empty()) {
      image.LoadFile(wxString::FromUTF8(file), wxBITMAP_TYPE_PNG);
   } else if (rgb.size() == 360 * 360 * 3) {
      image.Create(360, 360);
      std::copy(rgb.begin(), rgb.end(), image.GetData());
   }
   return made[table] = image.IsOk() ? wxBitmap(image) : wxBitmap();
}

void axes(ChartCanvas *chart) {
   wxFont f = axis_font();
   ChartStyle s;
   for (int v = -180; v <= 180; v += 60) {
      chart->add_polyline({ { double(v), 180 }, { double(v), 190 } }, false, s);    // phi ticks
      chart->add_polyline({ { -180, double(v) }, { -190, double(v) } }, false, s);  // psi ticks
      chart->add_text(v - 10.0, 200, wxString::Format("%d", v), ChartAnchor::W, f, *wxBLACK);
      chart->add_text(-230, -v, wxString::Format("%d", v), ChartAnchor::W, f, *wxBLACK);
   }
   chart->add_text(-10, 230, "Phi", ChartAnchor::W, f, *wxBLACK);
   chart->add_text(-230, 15, "Psi", ChartAnchor::W, f, *wxBLACK);
}


// circle, Gly an open triangle, Pro a square edged in white; as_white,
// Kleywegt's starting points, a white circle (Gly still its triangle)
ChartCanvas::item_id point(ChartCanvas *chart, const bandicoot_rama_point &pt, bool as_white) {
   wxColour c = pt.region == 0 ? red3 : dodger_blue;
   if (as_white && pt.kind != 1) {
      ChartStyle s;
      s.fill = *wxWHITE;
      s.stroke = outline;
      return chart->add_ellipse(pt.phi, -pt.psi, 5, 5, s);
   }
   double x = pt.phi, y = -pt.psi;
   ChartStyle s;
   if (pt.kind == 1) {
      s.stroke = c;
      s.stroke_width = 2.0;
      return chart->add_polyline({ { x, y - 3 }, { x - 3, y + 3 }, { x + 3, y + 3 } }, true, s);
   }
   s.fill = c;
   if (pt.kind == 2) {
      s.stroke = *wxWHITE;
      return chart->add_rect(x - 3, y - 3, 6, 6, s);
   }
   s.stroke = outline;
   return chart->add_ellipse(x, y, 5, 5, s);
}

// Kleywegt's arrow from one point to the other; one that is shorter the
// other way round the plot is drawn as two pieces, off one edge and in at
// the opposite one (rama_plot::test_kleywegt_wrap)
void arrow(ChartCanvas *chart, const bandicoot_rama_point &a, const bandicoot_rama_point &b) {
   double phi_1 = a.phi, psi_1 = a.psi, phi_2 = b.phi, psi_2 = b.psi;
   bool wrapped = false;
   std::pair<double, double> edge_1, edge_2;   // where the pieces leave and re-enter
   if (std::fabs(phi_1 - phi_2) > 200.0) {
      wrapped = true;
      double swap = 1.0;
      if (phi_1 - phi_2 < 0) { std::swap(phi_1, phi_2); std::swap(psi_1, psi_2); swap = -1.0; }
      double g = (180.0 - phi_1) / (phi_2 + 360.0 - phi_1);
      double psi_c = psi_1 + g * (psi_2 - psi_1);
      edge_1 = { 180.0 * swap, psi_c };
      edge_2 = { -180.0 * swap, psi_c };
   }
   if (std::fabs(psi_1 - psi_2) > 200.0) {
      wrapped = true;
      double swap = 1.0;
      if (psi_1 - psi_2 > 0) { std::swap(phi_1, phi_2); std::swap(psi_1, psi_2); swap = -1.0; }
      double g = (-180.0 - (psi_2 - 360.0)) / (psi_1 - (psi_2 - 360.0));
      double phi_c = phi_2 + g * (phi_1 - phi_2);
      edge_1 = { phi_c, -180.0 * swap };
      edge_2 = { phi_c, 180.0 * swap };
   }
   ChartStyle s;
   wxPoint2DDouble from(a.phi, -a.psi), to(b.phi, -b.psi);
   if (wrapped) {
      chart->add_polyline({ from, { edge_1.first, -edge_1.second } }, false, s);
      from = wxPoint2DDouble(edge_2.first, -edge_2.second);
   }
   chart->add_polyline({ from, to }, false, s);
   // the head: 8 long, 6 wide, at the second point
   double dx = to.m_x - from.m_x, dy = to.m_y - from.m_y, len = std::sqrt(dx * dx + dy * dy);
   if (len < 1e-6) return;
   dx /= len;
   dy /= len;
   wxPoint2DDouble base(to.m_x - 8 * dx, to.m_y - 8 * dy);
   ChartStyle head;
   head.fill = *wxBLACK;
   chart->add_polyline({ to, { base.m_x - 3 * dy, base.m_y + 3 * dx }, { base.m_x + 3 * dy, base.m_y - 3 * dx } },
                       true, head);
}

// What every plot has under its points: the background for table, the
// frame, the zero lines and the axes.
void draw_frame(ChartCanvas *chart, int table) {
   const wxBitmap &bg = background(table);
   if (bg.IsOk()) chart->add_image(-180, -180, 360, 360, bg);
   ChartStyle border;
   border.stroke_width = 2.0;
   chart->add_polyline({ { -180, -180 }, { -180, 180 }, { 180, 180 }, { 180, -180 } }, true, border);
   ChartStyle grey;
   grey.stroke = wxColour(0xbe, 0xbe, 0xbe);
   chart->add_polyline({ { 0, -180 }, { 0, 180 } }, false, grey);
   chart->add_polyline({ { -180, 0 }, { 180, 0 } }, false, grey);
   axes(chart);
}

// A plot's canvas: Coot's area at 1:1, white.
ChartCanvas *new_chart(wxWindow *parent) {
   ChartCanvas *chart = new ChartCanvas(parent);
   chart->SetWindowStyle(chart->GetWindowStyle() | wxBORDER_SUNKEN);
   chart->set_world_bounds(world_x0, world_y0, world_x1, world_y1);
   chart->set_scaling(ChartCanvas::Scaling::Fixed, 1.0);
   chart->SetMinSize(wxSize(int(world_x1 - world_x0) + 6, int(world_y1 - world_y0) + 6));
   return chart;
}

class RamaWindow : public wxFrame {
public:
   static std::map<int, RamaWindow *> shown;
   const int imol;

   RamaWindow(int imol_in)
      : wxFrame(owner, wxID_ANY, "Dynarama", wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT),
        imol(imol_in) {
      wxPanel *p = new wxPanel(this);
      body = p;
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      // Coot's menu bar, a strip of its own along the top of the window (a
      // real menu bar would take over the main one)
      TitledPanel *bar = new TitledPanel(p);
      wxBoxSizer *menus = new wxBoxSizer(wxHORIZONTAL);
      menus->Add(menu_button(bar->content(), "File", [this]() { return file_menu(); }), 0, wxRIGHT, 2);
      menus->Add(menu_button(bar->content(), "View", [this]() { return view_menu(); }), 0, wxRIGHT, 2);
      menus->Add(menu_button(bar->content(), "Help", [this]() { return help_menu(); }), 0);
      bar->content_sizer()->Add(menus, 0, wxALL, 2);
      col->Add(bar, 0, wxEXPAND | wxALL, 4);
      col->Add(new wxStaticText(p, wxID_ANY, wxString::FromUTF8(bandicoot_core_rama_heading(imol)),
                                wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL),
               0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

      chart = new_chart(p);
      chart->on_hover = [this](ChartCanvas::item_id entered, ChartCanvas::item_id) { hover(entered); };
      chart->on_press = [this](ChartCanvas::item_id item, const wxMouseEvent &e, const wxPoint2DDouble &) {
         if (e.RightDown()) { set_zoom(zoom + 0.2); return; }
         if (e.MiddleDown()) { set_zoom(zoom - 0.2); return; }
         auto it = items.find(item);
         if (it == items.end()) return;
         const bandicoot_rama_point &pt = it->second.second;
         bandicoot_result_row row;
         row.imol = it->second.first;
         row.chain = pt.chain;
         row.resno = pt.resno;
         row.atom = " CA ";
         row.label = pt.label;
         bandicoot_core_go_to_result(row);
      };
      col->Add(chart, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

      // selection, outliers only, zoom with resize
      wxBoxSizer *controls = new wxBoxSizer(wxHORIZONTAL);
      use_selection = new wxCheckBox(p, wxID_ANY, "Selection");
      selection = new wxTextCtrl(p, wxID_ANY, "//*", wxDefaultPosition, wxSize(170, -1), wxTE_PROCESS_ENTER);
      wxButton *apply = new wxButton(p, wxID_ANY, "Apply");
      selection->Enable(false);
      apply->Enable(false);
      use_selection->Bind(wxEVT_CHECKBOX, [this, apply](wxCommandEvent &e) {
         selection->Enable(e.IsChecked());
         apply->Enable(e.IsChecked());
         reload();
      });
      apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { reload(); });
      selection->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &) { reload(); });
      outliers_button = new wxToggleButton(p, wxID_ANY, "Outliers Only");
      outliers_button->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) { set_outliers_only(e.IsChecked()); });
      fit_button = new wxBitmapToggleButton(p, wxID_ANY, icon("stock-zoom-follow-window-12.png", wxSize(16, 16)));
      fit_button->SetToolTip("Zoom with resize");
      fit_button->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) { set_fit(e.IsChecked()); });
      selection_row = new wxBoxSizer(wxHORIZONTAL);
      selection_row->Add(use_selection, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      selection_row->Add(selection, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      selection_row->Add(apply, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
      controls->Add(selection_row, 0, wxALIGN_CENTER_VERTICAL);
      controls->AddStretchSpacer(1);
      controls->Add(outliers_button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
      controls->Add(fit_button, 0, wxALIGN_CENTER_VERTICAL);
      col->Add(controls, 0, wxEXPAND | wxALL, 4);

      // Kleywegt: which chains to compare
      chain_row = new wxBoxSizer(wxHORIZONTAL);
      chain1_choice = new wxChoice(p, wxID_ANY);
      chain2_choice = new wxChoice(p, wxID_ANY);
      BevelButton *compare = new BevelButton(p, wxID_ANY, "Apply");
      compare->set_icon(icon("stock-button-apply.png", wxSize(16, 16)));
      compare->SetMinSize(wxSize(110, 28));
      compare->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
         if (chain1_choice->GetSelection() < 0 || chain2_choice->GetSelection() < 0) { wxBell(); return; }
         by_chain = true;
         chain1 = chain1_choice->GetStringSelection().utf8_string();
         chain2 = chain2_choice->GetStringSelection().utf8_string();
         reload();
      });
      chain_row->Add(new wxStaticText(p, wxID_ANY, "Compare Chain:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      chain_row->Add(chain1_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
      chain_row->Add(new wxStaticText(p, wxID_ANY, "to Chain:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      chain_row->Add(chain2_choice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);
      chain_row->AddStretchSpacer();
      chain_row->Add(compare, 0, wxALIGN_CENTER_VERTICAL);
      col->Add(chain_row, 0, wxEXPAND | wxALL, 4);
      col->Hide(chain_row);

      TitledPanel *stats = new TitledPanel(p);
      favoured_label = new wxStaticText(stats->content(), wxID_ANY, wxEmptyString);
      outliers_label = new wxStaticText(stats->content(), wxID_ANY, wxEmptyString);
      stats->content_sizer()->Add(favoured_label, 0, wxLEFT | wxTOP, 8);
      stats->content_sizer()->Add(outliers_label, 0, wxLEFT | wxBOTTOM, 8);
      stats_panel = stats;
      col->Add(stats, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

      // Coot's OK and Cancel both close it
      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      for (auto b : { std::make_pair("OK", "stock-button-ok.png"), std::make_pair("Cancel", "stock-button-cancel.png") }) {
         BevelButton *button = new BevelButton(p, wxID_ANY, b.first);
         button->set_icon(icon(b.second, wxSize(16, 16)));
         button->SetMinSize(wxSize(110, 30));
         button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
         buttons->Add(button, 1, wxALL, 4);
      }
      col->Add(buttons, 0, wxEXPAND | wxALL, 4);
      p->SetSizer(col);
      col->SetSizeHints(this);

      // + and - zoom, as Coot's keys did, except while typing a selection
      Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent &e) {
         if (FindFocus() == selection) e.Skip();
         else if (e.GetKeyCode() == '+' || e.GetKeyCode() == '=') set_zoom(zoom + 0.2);
         else if (e.GetKeyCode() == '-') set_zoom(zoom - 0.2);
         else e.Skip();
      });
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
         shown.erase(imol);
         Destroy();
      });
      reload();
      CentreOnParent();
      Show();
   }

   // Kleywegt: imol's phi and psi against imol2's, by chain or residue by residue.
   void set_kleywegt(int imol2_in, bool by_chain_in, const std::string &chain1_in, const std::string &chain2_in) {
      kleywegt = true;
      imol2 = imol2_in;
      by_chain = by_chain_in;
      chain1 = chain1_in;
      chain2 = chain2_in;
      fill_chain_choice(chain1_choice, imol, chain1, 0);
      fill_chain_choice(chain2_choice, imol2, chain2, 1);
      wxSizer *col = body->GetSizer();
      col->Hide(selection_row, true);
      col->Show(chain_row, true);
      reload();
   }

   void set_ramachandran() {
      kleywegt = false;
      wxSizer *col = body->GetSizer();
      col->Show(selection_row, true);
      col->Hide(chain_row, true);
      reload();
   }

   bool models_present() const { return ! kleywegt || ! bandicoot_core_rama_heading(imol2).empty(); }

   // Changes to imol need this plot redrawn.
   bool shows(int imol_in) const { return imol_in == imol || (kleywegt && imol_in == imol2); }

   // The model changed: plot it again.
   void reload() {
      if (kleywegt) {
         bandicoot_core_kleywegt(imol, imol2, by_chain, chain1, chain2, pairs);
         stats_panel->Hide();
         body->Layout();
         redraw();
         return;
      }
      std::string sel = use_selection->GetValue() ? selection->GetValue().utf8_string() : std::string();
      bandicoot_core_rama_points(imol, sel, set);
      int n = int(set.points.size());
      if (n > 0) {
         int n_outliers = n - set.n_favoured - set.n_allowed;
         favoured_label->SetLabel(wxString::Format("In Favoured Regions:  %d  (%s%%)", set.n_favoured,
                                                   percent(set.n_favoured, n)));
         outliers_label->SetLabel(wxString::Format("Outliers:  %d  (%s%%)", n_outliers, percent(n_outliers, n)));
      }
      stats_panel->Show(n > 0);
      body->Layout();
      redraw();
   }

   // The core moved to this residue: Coot's green square, and the background
   // for its type.
   void highlight(const std::string &chain, int resno, const std::string &ins_code) {
      current = { chain, resno, ins_code };
      have_current = true;
      table = bandicoot_core_rama_table(imol, chain, resno, ins_code);
      redraw();
   }

private:
   struct residue { std::string chain; int resno = 0; std::string ins_code; };

   wxPanel *body;
   ChartCanvas *chart;
   wxCheckBox *use_selection;
   wxTextCtrl *selection;
   wxToggleButton *outliers_button;
   wxBitmapToggleButton *fit_button;
   wxPanel *stats_panel;
   wxStaticText *favoured_label, *outliers_label;
   wxBoxSizer *selection_row, *chain_row;
   wxChoice *chain1_choice, *chain2_choice;

   bandicoot_rama_set set;
   bool kleywegt = false, by_chain = false;
   int imol2 = -1;
   std::string chain1, chain2;
   std::vector<bandicoot_kleywegt_pair> pairs;
   // drawn points, with the model each is from
   std::map<ChartCanvas::item_id, std::pair<int, bandicoot_rama_point>> items;
   int table = BRAMA_ALL;
   residue current;
   bool have_current = false;
   bool outliers_only = false, fit = false;
   double zoom = 1.0;

   static wxString percent(int part, int whole) {
      // Coot's float_to_string: two decimals
      return wxString::Format("%.2f", 100.0 * part / whole);
   }

   wxButton *menu_button(wxWindow *parent, const wxString &label, std::function<wxMenu *()> make) {
      wxButton *b = new wxButton(parent, wxID_ANY, label, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
      b->Bind(wxEVT_BUTTON, [this, b, make](wxCommandEvent &) {
         std::unique_ptr<wxMenu> m(make());
         b->PopupMenu(m.get(), 0, b->GetSize().y);
      });
      return b;
   }

   wxMenu *file_menu() {
      wxMenu *m = new wxMenu;
      m->Append(1, "Export to PDF");
      m->Append(2, "Save as PNG");
      m->AppendSeparator();
      m->Append(3, "Close");
      m->Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
         if (e.GetId() == 1) save(true);
         if (e.GetId() == 2) save(false);
         if (e.GetId() == 3) CallAfter([this]() { Close(); });
      });
      return m;
   }

   wxMenu *view_menu() {
      wxMenu *m = new wxMenu;
      m->AppendRadioItem(5, "Ramachandran")->Check(! kleywegt);
      m->AppendRadioItem(6, "Kleywegt Plot")->Check(kleywegt);
      m->AppendSeparator();
      m->AppendCheckItem(1, "Outliers only")->Check(outliers_only);
      m->AppendSeparator();
      m->Append(2, "Zoom Out\t-");
      m->Append(3, "Zoom In\t+");
      m->AppendCheckItem(4, "Zoom with resize")->Check(fit);
      m->Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
         if (e.GetId() == 1) set_outliers_only(! outliers_only);
         if (e.GetId() == 2) set_zoom(zoom - 0.2);
         if (e.GetId() == 3) set_zoom(zoom + 0.2);
         if (e.GetId() == 4) set_fit(! fit);
         if (e.GetId() == 5 && kleywegt) set_ramachandran();
         if (e.GetId() == 6 && ! kleywegt) kleywegt_from_menu();
      });
      return m;
   }

   wxMenu *help_menu() {
      wxMenu *m = new wxMenu;
      m->Append(1, "About");
      m->Bind(wxEVT_MENU, [this](wxCommandEvent &) { CallAfter([this]() { about(); }); });
      return m;
   }

   // Coot's About box. Modal, so that it shows above this floating window.
   void about() {
      wxDialog d(this, wxID_ANY, "About Dynarama");
      wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);
      wxStaticText *name = new wxStaticText(&d, wxID_ANY, "Dynarama");
      name->SetFont(name->GetFont().Bold().Larger());
      v->Add(name, 0, wxALIGN_CENTER | wxTOP | wxLEFT | wxRIGHT, 16);
      v->Add(new wxStaticText(&d, wxID_ANY, "Authors: PaulE and myself"), 0, wxALIGN_CENTER | wxALL, 12);
      wxButton *close = new wxButton(&d, wxID_OK, "Close");
      close->SetDefault();
      v->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 12);
      d.SetSizerAndFit(v);
      d.CentreOnParent();
      d.ShowModal();
   }

   // Coot's View > Kleywegt Plot: this model's first chain against its second.
   void kleywegt_from_menu() {
      std::vector<std::string> chains = bandicoot_core_chain_ids(imol);
      if (chains.size() < 2) {
         info(this, "A Kleywegt plot of one model needs two chains to compare");
         return;
      }
      set_kleywegt(imol, true, chains[0], chains[1]);
   }

   // The chains of imol; select chain, or the one at fallback.
   static void fill_chain_choice(wxChoice *c, int imol_in, const std::string &chain, unsigned fallback) {
      c->Clear();
      std::vector<std::string> chains = bandicoot_core_chain_ids(imol_in);
      for (const std::string &id : chains) c->Append(wxString::FromUTF8(id));
      int at = c->FindString(wxString::FromUTF8(chain));
      if (at == wxNOT_FOUND && ! chains.empty()) at = int(std::min<size_t>(fallback, chains.size() - 1));
      c->SetSelection(at);
   }

   void set_outliers_only(bool state) {
      outliers_only = state;
      outliers_button->SetValue(state);
      redraw();
   }

   // Coot's zoom steps, never smaller than 0.8; zooming leaves zoom with resize.
   void set_zoom(double z) {
      zoom = std::max(0.8, z);
      if (fit) {
         fit = false;
         fit_button->SetValue(false);
      }
      chart->set_scaling(ChartCanvas::Scaling::Fixed, zoom);
      chart->Refresh();
   }

   // Zoom with resize: the plot fills the window as it changes size.
   void set_fit(bool state) {
      fit = state;
      fit_button->SetValue(state);
      if (fit) chart->set_scaling(ChartCanvas::Scaling::Fit);
      else chart->set_scaling(ChartCanvas::Scaling::Fixed, zoom);
      chart->Refresh();
   }

   // The pointer over a point: its label, and the background for its type.
   void hover(ChartCanvas::item_id item) {
      auto it = items.find(item);
      if (it == items.end()) {
         chart->UnsetToolTip();
         return;
      }
      const bandicoot_rama_point &pt = it->second.second;
      chart->SetToolTip(wxString::FromUTF8(pt.label));
      if (pt.table != table) {
         table = pt.table;
         redraw();
      }
   }

   void redraw() {
      chart->clear();
      items.clear();
      draw_frame(chart, table);

      const bandicoot_rama_point *boxed = nullptr;
      auto is_current = [this](const bandicoot_rama_point &pt) {
         return have_current && pt.chain == current.chain && pt.resno == current.resno &&
                pt.ins_code == current.ins_code;
      };
      auto add = [this](int from, const bandicoot_rama_point &pt, bool as_white) {
         ChartCanvas::item_id id = point(chart, pt, as_white);
         chart->set_clickable(id, true);
         items[id] = { from, pt };
      };
      if (kleywegt) {
         // the first model's point in white, an arrow to the second's
         for (const bandicoot_kleywegt_pair &pair : pairs) {
            if (is_current(pair.first)) boxed = &pair.first;
            if (outliers_only && pair.first.region != 0 && pair.second.region != 0) continue;
            add(imol, pair.first, true);
            add(imol2, pair.second, false);
            arrow(chart, pair.first, pair.second);
         }
      } else {
         for (const bandicoot_rama_point &pt : set.points) {
            if (is_current(pt)) boxed = &pt;
            if (outliers_only && pt.region != 0) continue;
            add(imol, pt, false);
         }
      }
      if (boxed) {
         ChartStyle s;
         s.fill = wxColour(0x00, 0x80, 0x00);
         s.stroke_width = 2.0;
         chart->add_rect(boxed->phi - 4, -boxed->psi - 4, 8, 8, s);
      }
      chart->Refresh();
   }

   // File > Export to PDF / Save as PNG: the plot as drawn, without the window.
   void save(bool pdf) {
      FileRequest r;
      r.title = pdf ? "Export to PDF" : "Save as PNG";
      r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
      r.file_name = pdf ? "rama.pdf" : "rama.png";
      r.types = { pdf ? FileType{ "PDF", { ".pdf" } } : FileType{ "PNG", { ".png" } } };
      wxString path = save_file(this, r);
      if (path.empty()) return;
      double w = chart->world_width(), h = chart->world_height();
      bool written = false;
      if (pdf) {
         CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            nullptr, (const UInt8 *) path.utf8_str().data(), strlen(path.utf8_str().data()), false);
         CGRect box = CGRectMake(0, 0, w, h);
         CGContextRef cg = url ? CGPDFContextCreateWithURL(url, &box, nullptr) : nullptr;
         if (url) CFRelease(url);
         if (cg) {
            CGContextBeginPage(cg, &box);
            // wx draws top-down, as in a window
            CGContextTranslateCTM(cg, 0, h);
            CGContextScaleCTM(cg, 1, -1);
            std::unique_ptr<wxGraphicsContext> gc(
               wxGraphicsRenderer::GetDefaultRenderer()->CreateContextFromNativeContext(cg));
            if (gc) {
               chart->render(gc.get(), 1.0);
               written = true;
            }
            gc.reset();
            CGContextEndPage(cg);
            CGPDFContextClose(cg);
            CGContextRelease(cg);
         }
      } else {
         const double scale = 2.0;
         wxBitmap bitmap(int(w * scale), int(h * scale), 24);
         {
            wxMemoryDC dc(bitmap);
            dc.SetBackground(*wxWHITE_BRUSH);
            dc.Clear();
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
            if (gc) chart->render(gc.get(), scale);
         }
         written = bitmap.ConvertToImage().SaveFile(path, wxBITMAP_TYPE_PNG);
      }
      if (! written) {
         std::cout << "ERROR:: could not write " << path.utf8_string() << std::endl;
         error(this, pdf ? "Could not export the plot to" : "Could not save the plot to", path);
      }
   }
};

std::map<int, RamaWindow *> RamaWindow::shown;

// ------------------------------------------- Edit Backbone Torsions' plot

ChartCanvas *backbone_chart = nullptr;
std::map<ChartCanvas::item_id, wxString> backbone_labels;

} // namespace

void show_rama_plot(wxWindow *parent, int imol) {
   owner = parent;
   auto it = RamaWindow::shown.find(imol);
   if (it != RamaWindow::shown.end()) it->second->Close();
   RamaWindow::shown[imol] = new RamaWindow(imol);
}

void show_kleywegt_plot(wxWindow *parent, int imol1, int imol2, bool by_chain, const std::string &chain1,
                        const std::string &chain2) {
   show_rama_plot(parent, imol1);
   RamaWindow::shown[imol1]->set_kleywegt(imol2, by_chain, chain1, chain2);
}

wxWindow *make_backbone_plot(wxWindow *parent) {
   ChartCanvas *chart = new_chart(parent);
   backbone_chart = chart;
   backbone_labels.clear();
   chart->on_hover = [chart](ChartCanvas::item_id entered, ChartCanvas::item_id) {
      auto it = backbone_labels.find(entered);
      if (it == backbone_labels.end()) chart->UnsetToolTip();
      else chart->SetToolTip(it->second);
   };
   chart->Bind(wxEVT_DESTROY, [chart](wxWindowDestroyEvent &e) {
      if (e.GetEventObject() == chart && backbone_chart == chart) backbone_chart = nullptr;
      e.Skip();
   });
   draw_frame(chart, BRAMA_GENERAL);
   return chart;
}

void show_backbone_points(const std::vector<bandicoot_rama_point> &points) {
   if (! backbone_chart) return;
   backbone_chart->clear();
   backbone_labels.clear();
   draw_frame(backbone_chart, BRAMA_GENERAL);
   for (const bandicoot_rama_point &pt : points) {
      ChartCanvas::item_id id = point(backbone_chart, pt, false);
      backbone_chart->set_clickable(id, true);   // for its label
      backbone_labels[id] = wxString::FromUTF8(pt.label);
   }
   backbone_chart->Refresh();
}

void install_rama_plot(wxWindow *parent) {
   owner = parent;
   bandicoot_rama_view v;
   v.changed = [](int imol) {
      for (auto &w : RamaWindow::shown)
         if (w.second->shows(imol)) w.second->reload();
   };
   v.highlight = [](int imol, const std::string &chain, int resno, const std::string &ins_code) {
      auto it = RamaWindow::shown.find(imol);
      if (it != RamaWindow::shown.end()) it->second->highlight(chain, resno, ins_code);
   };
   v.molecules_changed = []() {
      std::vector<RamaWindow *> gone;
      for (auto &w : RamaWindow::shown)
         if (bandicoot_core_rama_heading(w.first).empty() || ! w.second->models_present())
            gone.push_back(w.second);
      for (RamaWindow *w : gone) w->Close();
   };
   bandicoot_core_set_rama_view(v);
}

} // namespace wxui

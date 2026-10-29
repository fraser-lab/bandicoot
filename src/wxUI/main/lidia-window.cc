// src/wxUI/main/lidia-window.cc

#include "wxUI/main/lidia-window.hh"
#include "wxUI/bridge/lidia-bridge.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/assets/toolbar-button.hh"
#include "wxUI/dialogs/file-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/menu-strip.hh"
#include "wxUI/widgets/titled-panel.hh"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/geometry.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>
#include <wx/tglbtn.h>
#include <wx/artprov.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wxui {

namespace {

wxWindow *owner = nullptr;

// ---------------------------------------------- the canvas

// The builder's drawing, scrolled and scaled. The builder draws it; this only
// shows the picture and hands the pointer back in the builder's own units.
class LidiaCanvas : public wxScrolledCanvas {
public:
   std::function<void()> after_pointer; // the drawing may have changed

   LidiaCanvas(wxWindow *parent, bandicoot_lidia_id id_in)
      : wxScrolledCanvas(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxHSCROLL | wxVSCROLL | wxFULL_REPAINT_ON_RESIZE | wxBORDER_SUNKEN),
        id(id_in) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      SetScrollRate(10, 10);
      fit_virtual_size();
      Bind(wxEVT_PAINT, &LidiaCanvas::on_paint, this);
      Bind(wxEVT_LEFT_DOWN,    [this](wxMouseEvent &e) { press(e, 1, false); });
      Bind(wxEVT_LEFT_DCLICK,  [this](wxMouseEvent &e) { press(e, 1, true); });
      Bind(wxEVT_LEFT_UP,      [this](wxMouseEvent &e) { release(e, 1); });
      Bind(wxEVT_MIDDLE_DOWN,  [this](wxMouseEvent &e) { press(e, 2, false); });
      Bind(wxEVT_MIDDLE_UP,    [this](wxMouseEvent &e) { release(e, 2); });
      Bind(wxEVT_RIGHT_DOWN,   [this](wxMouseEvent &e) { press(e, 3, false); });
      Bind(wxEVT_RIGHT_UP,     [this](wxMouseEvent &e) { release(e, 3); });
      Bind(wxEVT_MOTION,       [this](wxMouseEvent &e) { motion(e); });
      Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent &) {});
   }

   void set_scale(double s) {
      scale = s;
      fit_virtual_size();
      Refresh();
   }

private:
   bandicoot_lidia_id id;
   double scale = 1.0;

   void fit_virtual_size() {
      double w = 0, h = 0;
      bandicoot_lidia_canvas_size(id, &w, &h);
      SetVirtualSize(int(std::ceil(w * scale)), int(std::ceil(h * scale)));
   }

   wxPoint2DDouble to_canvas(const wxPoint &p) const {
      wxPoint u = CalcUnscrolledPosition(p);
      return wxPoint2DDouble(u.x / scale, u.y / scale);
   }

   void on_paint(wxPaintEvent &) {
      wxPaintDC dc(this);
      wxSize size = GetClientSize();
      double backing = GetContentScaleFactor();
      int w = int(std::ceil(size.x * backing));
      int h = int(std::ceil(size.y * backing));
      if (w <= 0 || h <= 0) return;
      wxPoint origin = CalcUnscrolledPosition(wxPoint(0, 0));
      std::vector<unsigned char> rgb;
      bandicoot_lidia_render(id, w, h, scale * backing, origin.x / scale, origin.y / scale, &rgb);
      wxImage image(w, h, false);
      std::memcpy(image.GetData(), rgb.data(), rgb.size());
      wxBitmap bitmap(image);
      bitmap.SetScaleFactor(backing);
      dc.DrawBitmap(bitmap, 0, 0);
   }

   void send(bandicoot_lidia_mouse what, int button, const wxMouseEvent &e) {
      wxPoint2DDouble p = to_canvas(e.GetPosition());
      bandicoot_lidia_pointer(id, what, button, p.m_x, p.m_y,
                              e.ShiftDown(), e.ControlDown() || e.RawControlDown(), e.LeftIsDown());
      Refresh();
      if (after_pointer) after_pointer();
   }

   void press(wxMouseEvent &e, int button, bool twice) {
      SetFocus();
      if (! HasCapture()) CaptureMouse();
      send(twice ? bandicoot_lidia_mouse::double_press : bandicoot_lidia_mouse::press, button, e);
   }

   void release(wxMouseEvent &e, int button) {
      if (HasCapture()) ReleaseMouse();
      send(bandicoot_lidia_mouse::release, button, e);
   }

   void motion(wxMouseEvent &e) {
      send(bandicoot_lidia_mouse::motion, 0, e);
   }
};

// ---------------------------------------------- small dialogs

BevelButton *dialog_button(wxWindow *parent, const wxString &label, const wxString &icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   b->set_icon(icon(icon_file, wxSize(16, 16)));
   return b;
}

// A non-modal dialog that goes away on Escape.
wxDialog *small_dialog(wxWindow *parent, const wxString &title) {
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   d->Bind(wxEVT_CHAR_HOOK, [d](wxKeyEvent &e) {
      if (e.GetKeyCode() == WXK_ESCAPE) d->Destroy();
      else e.Skip();
   });
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   return d;
}

// OK and Cancel along the bottom; OK (or Return in `entry`) runs ok().
void ok_cancel(wxDialog *d, wxBoxSizer *col, wxTextCtrl *entry, std::function<void()> ok) {
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   BevelButton *ok_button = dialog_button(d, "OK", "stock-button-ok.png");
   BevelButton *cancel = dialog_button(d, "Cancel", "stock-button-cancel.png");
   row->Add(ok_button, 1, wxRIGHT, 6);
   row->Add(cancel, 1);
   col->Add(row, 0, wxEXPAND | wxALL, 10);
   auto run = [d, ok]() {
      ok();
      d->Destroy();
   };
   ok_button->Bind(wxEVT_BUTTON, [run](wxCommandEvent &) { run(); });
   entry->Bind(wxEVT_TEXT_ENTER, [run](wxCommandEvent &) { run(); });
   cancel->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
}

void show_dialog(wxDialog *d, wxBoxSizer *col, wxTextCtrl *focus) {
   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
   focus->SetFocus();
}

// A row of controls inside a bordered panel.
wxBoxSizer *row_in(TitledPanel *panel) {
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   panel->content_sizer()->Add(row, 1, wxEXPAND);
   return row;
}

// ---------------------------------------------- About

wxDialog *about_shown = nullptr, *credits_shown = nullptr, *license_shown = nullptr;

// One of each at a time: asking again raises the one there is.
wxDialog *single_dialog(wxWindow *parent, wxDialog *&shown, const wxString &title) {
   if (shown) {
      shown->Raise();
      return nullptr;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, title);
   d->SetBackgroundColour(palette::face());
   wxDialog **slot = &shown;
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   d->Bind(wxEVT_DESTROY, [d, slot](wxWindowDestroyEvent &e) {
      if (e.GetEventObject() == d) *slot = nullptr;
      e.Skip();
   });
   shown = d;
   return d;
}

// A short text in a white box, with a Close button under it.
void text_window(wxWindow *parent, wxDialog *&shown, const wxString &title,
                 const wxString &tab, const wxString &text) {
   wxDialog *d = single_dialog(parent, shown, title);
   if (! d) return;
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   wxTextCtrl *t = nullptr;
   if (tab.empty()) {
      t = new wxTextCtrl(d, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                         wxTE_MULTILINE | wxTE_READONLY);
      col->Add(t, 1, wxEXPAND | wxALL, 8);
   } else {
      wxNotebook *tabs = new wxNotebook(d, wxID_ANY);
      t = new wxTextCtrl(tabs, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                         wxTE_MULTILINE | wxTE_READONLY);
      tabs->AddPage(t, tab);
      col->Add(tabs, 1, wxEXPAND | wxALL, 8);
   }
   t->SetBackgroundColour(*wxWHITE);
   BevelButton *close = dialog_button(d, "Close", "stock-menu-close.png");
   close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   row->AddStretchSpacer();
   row->Add(close);
   col->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d->SetSizer(col);
   d->SetSize(d->FromDIP(wxSize(400, 300)));
   d->CentreOnParent();
   d->Show();
}

void show_about(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, about_shown, "About Coot LIDIA");
   if (! d) return;
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(new wxStaticBitmap(d, wxID_ANY, icon("coot-icon.png", wxSize(64, 64))), 0,
            wxALIGN_CENTER_HORIZONTAL | wxTOP, 12);
   wxStaticText *name = new wxStaticText(d, wxID_ANY, "Coot LIDIA 0.8.9");
   name->SetFont(name->GetFont().Bold().Scaled(1.4f));
   col->Add(name, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 10);
   col->Add(new wxStaticText(d, wxID_ANY, "Ligand Sketcher"), 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 8);
   wxStaticText *copyright = new wxStaticText(d, wxID_ANY,
      "Copyright The University of Oxford 2010\nSilicos-it 2013",
      wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
   copyright->SetFont(copyright->GetFont().Smaller());
   col->Add(copyright, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxTOP, 10);

   BevelButton *credits = dialog_button(d, "Credits", "stock-menu-about.png");
   BevelButton *license = new BevelButton(d, wxID_ANY, "License");
   BevelButton *close = dialog_button(d, "Close", "stock-menu-close.png");
   credits->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) {
      text_window(d, credits_shown, "Credits", "Written by",
                  "Lidia by Paul Emsley\nQED Scores by Silicos-it's module Biscu-it");
   });
   license->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) {
      text_window(d, license_shown, "License", wxEmptyString, "Lidia: GNU GPL v3\nBiscu-it: LGPL");
   });
   close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });
   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   buttons->Add(credits, 1, wxRIGHT, 6);
   buttons->Add(license, 1, wxRIGHT, 6);
   buttons->Add(close, 1);
   col->Add(buttons, 0, wxEXPAND | wxALL, 12);
   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
}

// ---------------------------------------------- the window

struct Tool {
   const char *label;
   const char *icon_file; // empty: a text-only button
   bandicoot_lidia_tool tool;
};

class LidiaWindow : public wxFrame {
public:
   static std::map<bandicoot_lidia_id, LidiaWindow *> shown;

   static LidiaWindow *find(bandicoot_lidia_id id) {
      auto it = shown.find(id);
      return it == shown.end() ? nullptr : it->second;
   }

   explicit LidiaWindow(bandicoot_lidia_id id_in)
      : wxFrame(owner, wxID_ANY, "Lidia: Coot's Ligand Builder"), id(id_in) {

      shown[id] = this;
      wxPanel *p = body = new wxPanel(this);
      p->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      MenuStrip *menus = new MenuStrip(p);
      menus->add("&File", [this]() { return file_menu(); });
      menus->add("&Display", [this]() { return display_menu(); });
      menus->add("&Help", [this]() { return help_menu(); });
      col->Add(menus, 0, wxEXPAND);
      Bind(wxEVT_CHAR_HOOK, [menus](wxKeyEvent &e) {
         if (! menus->handle_key(e)) e.Skip();
      });

      col->Add(top_toolbar(p), 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
      col->Add(ring_toolbar(p), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);

      wxBoxSizer *middle = new wxBoxSizer(wxHORIZONTAL);
      middle->Add(element_column(p), 0, wxEXPAND | wxRIGHT, 2);
      canvas = new LidiaCanvas(p, id);
      canvas->SetMinSize(FromDIP(wxSize(700, 450)));
      middle->Add(canvas, 1, wxEXPAND);
      col->Add(middle, 1, wxEXPAND | wxALL, 4);

      // the molecule's SMILES, selectable for copying, beside the scale
      wxBoxSizer *status_row = new wxBoxSizer(wxHORIZONTAL);
      TitledPanel *status_box = new TitledPanel(p);
      wxBoxSizer *status_box_row = row_in(status_box);
      status_box_row->Add(new wxStaticText(status_box->content(), wxID_ANY, "SMILES:"), 0,
                          wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
      status = new wxTextCtrl(status_box->content(), wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxDefaultSize, wxTE_READONLY);
      status_box_row->Add(status, 1, wxALIGN_CENTER_VERTICAL | wxALL, 2);
      status_row->Add(status_box, 1, wxEXPAND | wxRIGHT, 4);
      TitledPanel *scale_box = new TitledPanel(p);
      wxBoxSizer *scale_box_row = row_in(scale_box);
      scale_box_row->Add(new wxStaticText(scale_box->content(), wxID_ANY, "Scale"), 0,
                         wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);
      wxSpinCtrlDouble *scale = new wxSpinCtrlDouble(scale_box->content(), wxID_ANY, "1.00", wxDefaultPosition,
                                                     wxDefaultSize, wxSP_ARROW_KEYS, 0.2, 12.0, 1.0, 0.2);
      scale->SetDigits(2);
      scale->Bind(wxEVT_SPINCTRLDOUBLE, [this](wxSpinDoubleEvent &e) { canvas->set_scale(e.GetValue()); });
      scale_box_row->Add(scale, 0, wxALIGN_CENTER_VERTICAL | wxALL, 2);
      status_row->Add(scale_box, 0, wxEXPAND);
      col->Add(status_row, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);

      // the structural alerts, one per line, when alerts are shown, beside the switch
      wxBoxSizer *alert_row = new wxBoxSizer(wxHORIZONTAL);
      TitledPanel *alert_box = new TitledPanel(p);
      wxBoxSizer *alert_box_row = row_in(alert_box);
      alert_icon = new wxStaticBitmap(alert_box->content(), wxID_ANY,
                                      wxArtProvider::GetBitmapBundle(wxART_WARNING, wxART_MENU));
      alert_icon->Hide();
      alert_box_row->Add(alert_icon, 0, wxALIGN_TOP | wxALL, 4);
      alert_title = new wxStaticText(alert_box->content(), wxID_ANY, "ALERT!");
      alert_title->Hide();
      alert_box_row->Add(alert_title, 0, wxALIGN_TOP | wxTOP | wxBOTTOM | wxRIGHT, 4);
      // selectable for copying; scrolls past a few lines so the window stays on screen
      alert_name = new wxTextCtrl(alert_box->content(), wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
      alert_name->Hide();
      alert_box_row->Add(alert_name, 1, wxALIGN_TOP | wxALL, 4);
      alert_row->Add(alert_box, 1, wxEXPAND | wxRIGHT, 4);
      TitledPanel *show_box = new TitledPanel(p);
      wxBoxSizer *show_box_row = row_in(show_box);
      show_alerts = new wxCheckBox(show_box->content(), wxID_ANY, "Show Alerts");
      show_alerts->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &e) {
         show_alerts->SetForegroundColour(e.IsChecked() ? palette::text_on() : palette::text());
         if (! e.IsChecked()) set_alert(false, std::string());
         bandicoot_lidia_show_alerts(id, e.IsChecked());
         canvas->Refresh();
      });
      show_box_row->Add(show_alerts, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
      alert_row->Add(show_box, 0, wxEXPAND);
      col->Add(alert_row, 0, wxEXPAND | wxALL, 4);

      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      buttons->AddStretchSpacer();
      apply = dialog_button(p, "Apply", "stock-button-apply.png");
      apply->Enable(false);
      apply->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { bandicoot_lidia_apply(id); });
      BevelButton *close = dialog_button(p, "Close", "stock-menu-close.png");
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      buttons->Add(apply, 0, wxRIGHT, 12);
      buttons->Add(close, 0);
      col->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

      p->SetSizer(col);
      wxBoxSizer *frame_sizer = new wxBoxSizer(wxVERTICAL);
      frame_sizer->Add(p, 1, wxEXPAND);
      SetSizerAndFit(frame_sizer);

      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) {
         shown.erase(id);
         bandicoot_lidia_close(id);
         Destroy();
      });
      CentreOnParent();
      Show();
   }

   // The builder's status is its SMILES, or else a message about the molecule.
   void set_status(const std::string &text) {
      wxString t = wxString::FromUTF8(text).Trim(false);
      t.StartsWith("SMILES:", &t);
      status->ChangeValue(t.Trim(false));
   }

   // `names`: one alert per line
   void set_alert(bool on, const std::string &names) {
      wxString text = on ? wxString::FromUTF8(names) : wxString();
      if (text == alert_name->GetValue() && alert_title->IsShown() == on) return; // a redraw
      alert_name->ChangeValue(text);
      alert_name->ShowPosition(0);
      const int max_lines = 5;
      int lines = std::min(int(text.Freq('\n')) + 1, max_lines);
      int h = lines * alert_name->GetCharHeight() + alert_name->FromDIP(8);
      alert_name->SetMinSize(wxSize(-1, h));
      alert_name->SetMaxSize(wxSize(-1, h));
      alert_name->Show(on);
      alert_icon->Show(on);
      alert_title->Show(on);
      // the box grows with the list; the window grows if the canvas can't give way
      wxSize fit = GetSizer()->ComputeFittingWindowSize(this);
      SetMinSize(fit);
      if (GetSize().y < fit.y) SetSize(wxSize(GetSize().x, fit.y));
      Layout();
      body->Layout();
   }

   // Apply only for a molecule RDKit can sanitize, as Lidia decides
   void set_apply_sensitive(bool sensitive) {
      apply->Enable(sensitive);
   }

   void ask_atom_x(const std::string &element) {
      wxDialog *d = small_dialog(this, "Atom Element");
      wxBoxSizer *c = new wxBoxSizer(wxVERTICAL);
      c->Add(new wxStaticText(d, wxID_ANY, "Atom Element"), 0, wxALIGN_CENTER | wxTOP | wxLEFT | wxRIGHT, 10);
      wxTextCtrl *entry = new wxTextCtrl(d, wxID_ANY, wxString::FromUTF8(element), wxDefaultPosition,
                                         FromDIP(wxSize(120, -1)), wxTE_PROCESS_ENTER);
      entry->SelectAll();
      c->Add(entry, 0, wxEXPAND | wxALL, 6);
      BevelButton *close = dialog_button(d, "Close", "stock-menu-close.png");
      c->Add(close, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
      bandicoot_lidia_id builder = id;
      entry->Bind(wxEVT_TEXT, [builder, entry](wxCommandEvent &) {
         bandicoot_lidia_set_atom_x(builder, entry->GetValue().Trim().Trim(false).ToStdString());
      });
      entry->Bind(wxEVT_TEXT_ENTER, [d](wxCommandEvent &) { d->Destroy(); });
      close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
      show_dialog(d, c, entry);
   }

private:
   bandicoot_lidia_id id;
   LidiaCanvas *canvas = nullptr;
   wxTextCtrl *status = nullptr;
   wxStaticBitmap *alert_icon = nullptr;
   wxStaticText *alert_title = nullptr;
   wxTextCtrl *alert_name = nullptr;
   wxCheckBox *show_alerts = nullptr;
   wxPanel *body = nullptr;
   bandicoot_lidia_labels labels = bandicoot_lidia_labels::standard;
   BevelButton *apply = nullptr;
   // the editing tools, of which one at most is down
   std::vector<std::pair<ToolbarButton *, bandicoot_lidia_tool>> tools;

   // After anything that changes the molecule.
   void redraw() {
      canvas->Refresh();
   }

   wxMenu *file_menu() {
      enum { NEW = 1, OPEN, IMPORT_SMILES, IMPORT_MOLECULE, FETCH, SAVE, SAVE_AS,
             EXPORT_PDF, EXPORT_PNG, EXPORT_SVG, CLOSE };
      wxMenu *m = new wxMenu;
      m->Append(NEW, "New");
      m->Append(OPEN, "Open");
      m->Append(IMPORT_SMILES, "Import from SMILES...");
      m->Append(IMPORT_MOLECULE, "Import Molecule...");
      m->Append(FETCH, "Fetch Molecule...");
      m->Append(SAVE, "Save");
      m->Append(SAVE_AS, "Save As");
      m->Append(EXPORT_PDF, "Export as PDF...");
      m->Append(EXPORT_PNG, "Export as PNG...");
      m->Append(EXPORT_SVG, "Export as SVG...");
      m->AppendSeparator();
      m->Append(CLOSE, "Close");
      m->Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
         switch (e.GetId()) {
         case NEW:             CallAfter([this]() { bandicoot_lidia_new(id); }); break;
         case OPEN:            CallAfter([this]() { open_file(); }); break;
         case IMPORT_SMILES:   CallAfter([this]() { import_smiles(); }); break;
         case IMPORT_MOLECULE: CallAfter([this]() { import_molecule(); }); break;
         case FETCH:           CallAfter([this]() { fetch_molecule(); }); break;
         case SAVE:            bandicoot_lidia_save(id); break;
         case SAVE_AS:         CallAfter([this]() { save_as(); }); break;
         case EXPORT_PDF:      CallAfter([this]() { export_as(bandicoot_lidia_export::pdf); }); break;
         case EXPORT_PNG:      CallAfter([this]() { export_as(bandicoot_lidia_export::png); }); break;
         case EXPORT_SVG:      CallAfter([this]() { export_as(bandicoot_lidia_export::svg); }); break;
         case CLOSE:           CallAfter([this]() { Close(); }); break;
         }
      });
      return m;
   }

   wxMenu *display_menu() {
      wxMenu *m = new wxMenu;
      m->AppendRadioItem(1, "Standard");
      m->AppendRadioItem(2, "Atom Indices");
      m->AppendRadioItem(3, "Atom Names");
      m->Check(labels == bandicoot_lidia_labels::standard ? 1
               : labels == bandicoot_lidia_labels::indices ? 2 : 3, true);
      m->Bind(wxEVT_MENU, [this](wxCommandEvent &e) {
         if (e.GetId() == 1) labels = bandicoot_lidia_labels::standard;
         if (e.GetId() == 2) labels = bandicoot_lidia_labels::indices;
         if (e.GetId() == 3) labels = bandicoot_lidia_labels::names;
         bandicoot_lidia_set_labels(id, labels);
         redraw();
      });
      return m;
   }

   wxMenu *help_menu() {
      wxMenu *m = new wxMenu;
      m->Append(1, "About");
      m->Bind(wxEVT_MENU, [this](wxCommandEvent &) { CallAfter([this]() { show_about(this); }); });
      return m;
   }

   // ---- toolbars

   wxPanel *toolbar_panel(wxWindow *parent, wxBoxSizer *&row) {
      wxPanel *t = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
      t->SetBackgroundColour(palette::face());
      row = new wxBoxSizer(wxHORIZONTAL);
      t->SetSizer(row);
      return t;
   }

   ToolbarButton *tool_button(wxWindow *parent, const wxString &label, const wxString &icon_file,
                              ToolbarButton::Content content) {
      wxBitmapBundle bundle;
      if (! icon_file.empty()) bundle = icon(icon_file, wxSize(20, 20));
      if (! bundle.IsOk()) content = ToolbarButton::Content::Text;
      return new ToolbarButton(parent, wxID_ANY, label, bundle, content);
   }

   // A button for an editing tool: down while the tool is in use.
   ToolbarButton *add_tool(wxWindow *parent, wxBoxSizer *row, const Tool &t,
                           ToolbarButton::Content content, int flags) {
      ToolbarButton *b = tool_button(parent, t.label, t.icon_file, content);
      b->set_toggle(true);
      tools.push_back({ b, t.tool });
      bandicoot_lidia_tool tool = t.tool;
      b->Bind(wxEVT_TOGGLEBUTTON, [this, b, tool](wxCommandEvent &e) { choose_tool(b, tool, e.GetInt() != 0); });
      row->Add(b, 0, flags, 1);
      return b;
   }

   ToolbarButton *add_action(wxWindow *parent, wxBoxSizer *row, const wxString &label,
                             const wxString &icon_file, std::function<void()> action) {
      ToolbarButton *b = tool_button(parent, label, icon_file, ToolbarButton::Content::IconAboveText);
      b->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent &) {
         action();
         redraw();
      });
      row->Add(b, 0, wxALL, 1);
      return b;
   }

   void choose_tool(ToolbarButton *chosen, bandicoot_lidia_tool tool, bool on) {
      for (auto &t : tools)
         if (t.first != chosen && t.first->is_on()) t.first->set_on(false);
      bandicoot_lidia_set_tool(id, on ? tool : bandicoot_lidia_tool::none);
   }

   wxPanel *top_toolbar(wxWindow *parent) {
      wxBoxSizer *row;
      wxPanel *t = toolbar_panel(parent, row);
      const auto above = ToolbarButton::Content::IconAboveText;
      add_action(t, row, "Clear", "stock-clear.png", [this]() { bandicoot_lidia_clear(id); });
      add_action(t, row, "Undo", "stock-undo.png", [this]() { bandicoot_lidia_undo(id); });
      const Tool bonds[] = {
         { "Single", "single.svg",     bandicoot_lidia_tool::single_bond },
         { "Double", "double.svg",     bandicoot_lidia_tool::double_bond },
         { "Triple", "triple.svg",     bandicoot_lidia_tool::triple_bond },
         { "Stereo", "stereo-out.svg", bandicoot_lidia_tool::stereo_bond },
         { "Charge", "charge.svg",     bandicoot_lidia_tool::charge },
         { "Cut",    "stock-cut.svg",  bandicoot_lidia_tool::cut } };
      for (const Tool &tool : bonds) add_tool(t, row, tool, above, wxALL);
      add_action(t, row, "Delete Hydrogens", "Trash-hydrogens-Andy_Trash_Can.svg",
                 [this]() { bandicoot_lidia_delete_hydrogens(id); });
      add_action(t, row, "SMILES", "face-smile.svg", [this]() { show_smiles(); });
      add_action(t, row, "Tidy Up", "05_Hotel_Icon_Hair_Salon.svg", [this]() { bandicoot_lidia_tidy_up(id); });
      return t;
   }

   wxPanel *ring_toolbar(wxWindow *parent) {
      wxBoxSizer *row;
      wxPanel *t = toolbar_panel(parent, row);
      const Tool rings[] = {
         { "3-C",    "3-c.svg",       bandicoot_lidia_tool::ring_3 },
         { "4-C",    "4-c.svg",       bandicoot_lidia_tool::ring_4 },
         { "5-C",    "5-c.svg",       bandicoot_lidia_tool::ring_5 },
         { "6-C",    "6-c-exp-2.svg", bandicoot_lidia_tool::ring_6 },
         { "6-Arom", "6-c-arom.svg",  bandicoot_lidia_tool::ring_6_aromatic },
         { "7-C",    "7-c.svg",       bandicoot_lidia_tool::ring_7 },
         { "8-C",    "8-c.svg",       bandicoot_lidia_tool::ring_8 } };
      for (const Tool &tool : rings) add_tool(t, row, tool, ToolbarButton::Content::IconAboveText, wxALL);
      // the ligand's surroundings and their key, for a builder opened by FLEV
      ToolbarButton *environment = tool_button(t, "Env. Residues", "stock-menu-add.png",
                                               ToolbarButton::Content::IconAboveText);
      environment->set_toggle(true);
      environment->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
         bandicoot_lidia_show_environment(id, e.GetInt() != 0);
         redraw();
      });
      row->Add(environment, 0, wxALL, 1);
      ToolbarButton *key = tool_button(t, "Key", "Anonymous_Architetto_--_Chiave.svg",
                                       ToolbarButton::Content::IconAboveText);
      key->set_toggle(true);
      key->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
         bandicoot_lidia_show_key(id, e.GetInt() != 0);
         redraw();
      });
      row->Add(key, 0, wxALL, 1);
      return t;
   }

   wxPanel *element_column(wxWindow *parent) {
      wxPanel *c = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_THEME);
      c->SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
      const Tool elements[] = {
         { "C",  "", bandicoot_lidia_tool::carbon },
         { "N",  "", bandicoot_lidia_tool::nitrogen },
         { "O",  "", bandicoot_lidia_tool::oxygen },
         { "S",  "", bandicoot_lidia_tool::sulfur },
         { "P",  "", bandicoot_lidia_tool::phosphorus },
         { "H",  "", bandicoot_lidia_tool::hydrogen },
         { "F",  "", bandicoot_lidia_tool::fluorine },
         { "Cl", "", bandicoot_lidia_tool::chlorine },
         { "Br", "", bandicoot_lidia_tool::bromine },
         { "I",  "", bandicoot_lidia_tool::iodine },
         { "X",  "", bandicoot_lidia_tool::other_element } };
      for (const Tool &tool : elements)
         add_tool(c, col, tool, ToolbarButton::Content::Text, wxALL | wxEXPAND);
      c->SetSizer(col);
      return c;
   }

   // ---- file panels

   void open_file() {
      FileRequest r;
      r.title = "Open";
      r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
      r.types = { FileType{ "Molecules", { ".mol", ".mdl", ".sdf", ".cif" } } };
      std::vector<wxString> paths = open_files(this, r);
      if (paths.empty()) return;
      bandicoot_lidia_open_file(id, paths.front().utf8_string());
      redraw();
   }

   void save_as() {
      FileRequest r;
      r.title = "Save As";
      r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
      r.file_name = "coot.mol";
      r.types = { FileType{ "MDL mol", { ".mol" } } };
      wxString path = save_file(this, r);
      if (! path.empty()) bandicoot_lidia_save_as(id, path.utf8_string());
   }

   void export_as(bandicoot_lidia_export format) {
      const char *ext = format == bandicoot_lidia_export::pdf ? "pdf"
                      : format == bandicoot_lidia_export::png ? "png" : "svg";
      FileRequest r;
      r.title = wxString::Format("Export as %s", wxString(ext).Upper());
      r.directory = wxString::FromUTF8(bandicoot_core_chooser_directory());
      r.file_name = wxString::Format("coot-lidia.%s", ext);
      r.types = { FileType{ wxString(ext).Upper(), { wxString(".") + ext } } };
      wxString path = save_file(this, r);
      if (! path.empty()) bandicoot_lidia_export_as(id, format, path.utf8_string());
   }

   // ---- dialogs

   void fetch_molecule() {
      wxDialog *d = small_dialog(this, "Fetch Molecule");
      wxBoxSizer *c = new wxBoxSizer(wxVERTICAL);
      c->Add(new wxStaticText(d, wxID_ANY, "Fetch Molecule (from the Internet):"), 0,
             wxTOP | wxLEFT | wxRIGHT, 10);
      wxTextCtrl *entry = new wxTextCtrl(d, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                         FromDIP(wxSize(260, -1)), wxTE_PROCESS_ENTER);
      c->Add(entry, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      BevelButton *apply_button = dialog_button(d, "Apply", "stock-button-apply.png");
      BevelButton *cancel = dialog_button(d, "Cancel", "stock-button-cancel.png");
      row->Add(apply_button, 1, wxRIGHT, 6);
      row->Add(cancel, 1);
      c->Add(row, 0, wxEXPAND | wxALL, 10);
      auto run = [this, d, entry]() {
         std::string name = entry->GetValue().Trim().Trim(false).ToStdString();
         if (name.empty()) return;
         d->Destroy();
         wxBusyCursor busy;
         bandicoot_lidia_fetch(id, name);
         redraw();
      };
      apply_button->Bind(wxEVT_BUTTON, [run](wxCommandEvent &) { run(); });
      entry->Bind(wxEVT_TEXT_ENTER, [run](wxCommandEvent &) { run(); });
      cancel->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
      show_dialog(d, c, entry);
   }

   void import_smiles() {
      wxDialog *d = small_dialog(this, "Import from SMILES");
      wxBoxSizer *c = new wxBoxSizer(wxVERTICAL);
      c->Add(new wxStaticText(d, wxID_ANY, "Enter SMILES string:"), 0, wxALIGN_CENTER | wxTOP | wxLEFT | wxRIGHT, 10);
      wxTextCtrl *entry = new wxTextCtrl(d, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                         FromDIP(wxSize(320, -1)), wxTE_PROCESS_ENTER);
      c->Add(entry, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
      ok_cancel(d, c, entry, [this, entry]() {
         std::string smiles = entry->GetValue().Trim().Trim(false).ToStdString();
         if (smiles.empty()) return;
         bandicoot_lidia_import_smiles(id, smiles);
         redraw();
      });
      show_dialog(d, c, entry);
   }

   void import_molecule() {
      wxDialog *d = small_dialog(this, "Import Molecule");
      wxBoxSizer *c = new wxBoxSizer(wxVERTICAL);
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(d, wxID_ANY, "Enter comp-id:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      wxTextCtrl *entry = new wxTextCtrl(d, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                         FromDIP(wxSize(120, -1)), wxTE_PROCESS_ENTER);
      row->Add(entry, 1);
      c->Add(row, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
      wxCheckBox *hydrogens = new wxCheckBox(d, wxID_ANY, "Keep All Hydrogens");
      hydrogens->Bind(wxEVT_CHECKBOX, [hydrogens](wxCommandEvent &e) {
         hydrogens->SetForegroundColour(e.IsChecked() ? palette::text_on() : palette::text());
      });
      c->Add(hydrogens, 0, wxLEFT | wxRIGHT | wxTOP, 10);
      ok_cancel(d, c, entry, [this, entry, hydrogens]() {
         std::string comp_id = entry->GetValue().Trim().Trim(false).ToStdString();
         if (comp_id.empty()) return;
         bandicoot_lidia_import_comp_id(id, comp_id, hydrogens->GetValue());
         redraw();
      });
      show_dialog(d, c, entry);
   }

   void show_smiles() {
      wxDialog *d = small_dialog(this, "SMILES");
      wxBoxSizer *c = new wxBoxSizer(wxVERTICAL);
      wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
      row->Add(new wxStaticText(d, wxID_ANY, "SMILES:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      wxTextCtrl *entry = new wxTextCtrl(d, wxID_ANY, wxString::FromUTF8(bandicoot_lidia_smiles(id)),
                                         wxDefaultPosition, FromDIP(wxSize(360, -1)),
                                         wxTE_READONLY | wxTE_PROCESS_ENTER);
      entry->SelectAll();
      row->Add(entry, 1);
      c->Add(row, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
      BevelButton *ok = dialog_button(d, "OK", "stock-button-ok.png");
      c->Add(ok, 0, wxALIGN_RIGHT | wxALL, 10);
      ok->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Destroy(); });
      entry->Bind(wxEVT_TEXT_ENTER, [d](wxCommandEvent &) { d->Destroy(); });
      show_dialog(d, c, entry);
   }
};

std::map<bandicoot_lidia_id, LidiaWindow *> LidiaWindow::shown;

} // namespace

void install_lidia(wxWindow *owner_in) {
   owner = owner_in;
   bandicoot_lidia_hooks h;
   h.open = [](bandicoot_lidia_id id) { new LidiaWindow(id); };
   h.status_text = [](bandicoot_lidia_id id, const std::string &text) {
      if (LidiaWindow *w = LidiaWindow::find(id)) w->set_status(text);
   };
   h.alert = [](bandicoot_lidia_id id, bool on, const std::string &name) {
      if (LidiaWindow *w = LidiaWindow::find(id)) w->set_alert(on, name);
   };
   h.apply_sensitive = [](bandicoot_lidia_id id, bool on) {
      if (LidiaWindow *w = LidiaWindow::find(id)) w->set_apply_sensitive(on);
   };
   h.atom_x_dialog = [](bandicoot_lidia_id id, const std::string &element) {
      if (LidiaWindow *w = LidiaWindow::find(id)) w->ask_atom_x(element);
   };
   h.error = [](bandicoot_lidia_id id, const std::string &what_failed, const std::string &subject) {
      error(LidiaWindow::find(id), wxString::FromUTF8(what_failed), wxString::FromUTF8(subject));
   };
   bandicoot_lidia_install(h);
}

} // namespace wxui

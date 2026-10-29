// src/wxUI/widgets/menu-strip.cc

#include "wxUI/widgets/menu-strip.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/palette.hh"

#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <cctype>
#include <memory>

namespace wxui {

namespace {
const int title_padding_x = 8; // points
const int title_padding_y = 3;
}

// One title in the strip. Opens its menu on press, as a menu bar does.
class MenuTitle : public wxControl {
public:
   MenuTitle(wxWindow *parent, const wxString &label_in, std::function<wxMenu *()> make_in)
      : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
        make(make_in) {
      SetBackgroundStyle(wxBG_STYLE_PAINT);
      // "&File": shown as "File", with the F underlined and taken as the key
      int amp = label_in.Find('&');
      text = wxStripMenuCodes(label_in, wxStrip_Mnemonics);
      if (amp != wxNOT_FOUND && size_t(amp + 1) < label_in.length()) {
         accel_index = amp;
         key = std::toupper(int(label_in[amp + 1].GetValue()));
      }
      Bind(wxEVT_PAINT, &MenuTitle::on_paint, this);
      Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) { hovered = true; Refresh(); });
      Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) { hovered = false; Refresh(); });
      Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { open(); });
      SetInitialSize();
   }

   int mnemonic() const { return key; }

   void open() {
      std::unique_ptr<wxMenu> menu(make());
      if (! menu) return;
      showing = true;
      Refresh();
      Update();
      PopupMenu(menu.get(), 0, GetSize().y);
      showing = false;
      hovered = GetClientRect().Contains(ScreenToClient(wxGetMousePosition()));
      Refresh();
   }

   bool AcceptsFocus() const override { return false; }

protected:
   wxSize DoGetBestSize() const override {
      wxClientDC dc(const_cast<MenuTitle *>(this));
      dc.SetFont(GetFont());
      wxSize t = dc.GetTextExtent(text);
      return wxSize(t.x + 2 * FromDIP(title_padding_x), t.y + 2 * FromDIP(title_padding_y));
   }

private:
   std::function<wxMenu *()> make;
   wxString text;
   int accel_index = -1;
   int key = 0;
   bool hovered = false;
   bool showing = false;

   void on_paint(wxPaintEvent &) {
      wxAutoBufferedPaintDC dc(this);
      wxRect r = GetClientRect();
      dc.SetBackground(wxBrush(palette::face()));
      dc.Clear();
      bool lit = hovered || showing;
      if (lit)
         draw_frame(dc, r, FrameStyle::Raised, palette::face_hover(), palette::toolbar_hover_shadow());
      dc.SetFont(GetFont());
      dc.SetTextForeground(IsEnabled() ? palette::text() : palette::text_disabled());
      dc.DrawLabel(text, r, wxALIGN_CENTER, accel_index);
   }
};

MenuStrip::MenuStrip(wxWindow *parent) : wxPanel(parent) {
   SetBackgroundStyle(wxBG_STYLE_PAINT);
   row = new wxBoxSizer(wxHORIZONTAL);
   wxBoxSizer *frame = new wxBoxSizer(wxVERTICAL);
   // inside the strip's own raised edge
   frame->Add(row, 0, wxALL, FromDIP(3));
   SetSizer(frame);
   Bind(wxEVT_PAINT, &MenuStrip::on_paint, this);
}

void MenuStrip::add(const wxString &label, std::function<wxMenu *()> make) {
   MenuTitle *t = new MenuTitle(this, label, make);
   titles.push_back(t);
   row->Add(t, 0, wxRIGHT, FromDIP(2));
   Layout();
   InvalidateBestSize();
}

bool MenuStrip::handle_key(const wxKeyEvent &e) {
   // Alt on Linux and Windows, Option on macOS; nothing else held
   if (! e.AltDown() || e.ControlDown() || e.RawControlDown() || e.ShiftDown()) return false;
   int code = e.GetKeyCode();
   if (code > 0 && code < 128) code = std::toupper(code);
   for (MenuTitle *t : titles) {
      if (t->mnemonic() && t->mnemonic() == code) {
         t->CallAfter([t]() { t->open(); });
         return true;
      }
   }
   return false;
}

void MenuStrip::on_paint(wxPaintEvent &) {
   wxAutoBufferedPaintDC dc(this);
   draw_frame(dc, GetClientRect(), FrameStyle::Raised, palette::face(), palette::shadow_of(palette::face()));
}

} // namespace wxui

// src/wxUI/dialogs/click-through-dialog.cc

#include "wxUI/dialogs/click-through-dialog.hh"
#include "wxUI/assets/palette.hh"

#include <wx/button.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace wxui {

ClickThroughDialog::ClickThroughDialog(wxWindow *parent, const wxString &title,
                                       ClickThroughList::RowType type,
                                       const wxString &header_text)
   : wxFrame(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_FRAME_STYLE) {

   wxPanel *p = new wxPanel(this);
   p->SetBackgroundColour(palette::face());

   header_panel = new wxPanel(p);
   header_box = new wxBoxSizer(wxVERTICAL);
   if (! header_text.empty())
      header_box->Add(new wxStaticText(header_panel, wxID_ANY, header_text), 0, wxBOTTOM, 4);
   header_panel->SetSizer(header_box);

   rows = new ClickThroughList(p, type);
   wxButton *close = new wxButton(p, wxID_CLOSE, "Close");
   close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });

   wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);
   v->Add(header_panel, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
   v->Add(rows, 1, wxEXPAND | wxALL, 6);
   v->Add(close, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 6);
   p->SetSizer(v);
   SetSize(FromDIP(wxSize(320, 420)));
}

} // namespace wxui

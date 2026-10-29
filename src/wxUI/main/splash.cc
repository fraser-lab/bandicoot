// src/wxUI/main/splash.cc
//
// The startup splash of the wx build: a borderless picture, centred and kept
// above other windows, shown for a minimum time and taken down just before the
// main window appears (as the GTK build's).

#include "wxUI/main/surface.hh"

#include <wx/wx.h>
#include <wx/statbmp.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace {

wxFrame *splash = nullptr;
std::chrono::steady_clock::time_point shown_at;

// the default picture is drawn this wide, in points
const int default_width = 540;
const auto minimum_shown = std::chrono::milliseconds(2500);

void pump() {
   if (wxTheApp) wxTheApp->Yield(true);
}

} // namespace

void bandicoot_wx_show_splash(const std::string &file_name, bool scale_to_default) {

   wxInitAllImageHandlers();
   wxImage image(wxString::FromUTF8(file_name), wxBITMAP_TYPE_ANY);
   if (! image.IsOk()) {
      std::cout << "WARNING:: splash image could not be read: " << file_name << std::endl;
      return;
   }
   wxSize size(image.GetWidth(), image.GetHeight());
   if (scale_to_default && size.x > default_width)
      size = wxSize(default_width, size.y * default_width / size.x);

   // a copy at double scale keeps the picture sharp on a Retina screen
   wxVector<wxBitmap> copies;
   copies.push_back(wxBitmap(image.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH)));
   if (image.GetWidth() >= 2 * size.x)
      copies.push_back(wxBitmap(image.Scale(2 * size.x, 2 * size.y, wxIMAGE_QUALITY_HIGH)));

   splash = new wxFrame(nullptr, wxID_ANY, "Bandicoot", wxDefaultPosition, wxDefaultSize,
                        wxBORDER_NONE | wxSTAY_ON_TOP | wxFRAME_NO_TASKBAR);
   wxStaticBitmap *picture = new wxStaticBitmap(splash, wxID_ANY, wxBitmapBundle::FromBitmaps(copies));
   wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
   sizer->Add(picture);
   splash->SetSizerAndFit(sizer);
   splash->Centre();
   splash->Show();
   splash->Raise();
   splash->Update();
   pump();
   shown_at = std::chrono::steady_clock::now();
}

void bandicoot_wx_close_splash() {

   if (! splash) return;
   while (std::chrono::steady_clock::now() - shown_at < minimum_shown) {
      pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
   }
   splash->Destroy();
   splash = nullptr;
   pump();
}

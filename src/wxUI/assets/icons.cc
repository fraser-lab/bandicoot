// src/wxUI/assets/icons.cc

#include "wxUI/assets/icons.hh"

#include <wx/filename.h>
#include <wx/image.h>

#include <iostream>

namespace wxui {

namespace {
wxString icon_directory;
}

void set_icon_directory(const wxString &dir) {
   icon_directory = dir;
}

wxBitmapBundle icon(const wxString &file_name, const wxSize &size) {

   wxFileName path(icon_directory, file_name);
   if (! path.FileExists()) {
      std::cout << "WARNING:: icon not found: " << path.GetFullPath().utf8_string() << std::endl;
      return wxBitmapBundle();
   }
   if (path.GetExt().Lower() == "svg")
      return wxBitmapBundle::FromSVGFile(path.GetFullPath(), size);

   wxInitAllImageHandlers();
   wxImage image(path.GetFullPath(), wxBITMAP_TYPE_ANY);
   if (! image.IsOk()) {
      std::cout << "WARNING:: icon could not be read: " << path.GetFullPath().utf8_string() << std::endl;
      return wxBitmapBundle();
   }
   // A bitmap image big enough for the double-scale screen gets a copy at
   // each scale; a smaller one is scaled up by the system, as GTK did.
   wxImage one = image.Scale(size.x, size.y, wxIMAGE_QUALITY_HIGH);
   if (image.GetWidth() >= 2 * size.x && image.GetHeight() >= 2 * size.y) {
      wxImage two = image.Scale(2 * size.x, 2 * size.y, wxIMAGE_QUALITY_HIGH);
      return wxBitmapBundle::FromBitmaps(wxBitmap(one), wxBitmap(two));
   }
   return wxBitmapBundle::FromBitmap(wxBitmap(one));
}

} // namespace wxui

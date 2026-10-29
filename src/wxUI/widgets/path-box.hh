// src/wxUI/widgets/path-box.hh
//
// PathBox: a label, a path field and a Browse button that fills the field from
// a file (or folder) dialog.

#ifndef BANDICOOT_WXUI_PATH_BOX_HH
#define BANDICOOT_WXUI_PATH_BOX_HH

#include <wx/panel.h>

#include <functional>

class wxTextCtrl;

namespace wxui {

class PathBox : public wxPanel {
public:
   enum class Mode { Open, Save, Folder };

   PathBox(wxWindow *parent, const wxString &label, Mode mode = Mode::Open,
           const wxString &wildcard = wxEmptyString);

   wxString get_path() const;
   void set_path(const wxString &path);

   // called after Browse sets the path, or when the field is edited
   std::function<void(const wxString &path)> on_change;

private:
   Mode mode;
   wxString wildcard;
   wxTextCtrl *field;
   void browse();
};

} // namespace wxui

#endif // BANDICOOT_WXUI_PATH_BOX_HH

// src/wxUI/dialogs/message.hh
//
// The generic messages, in the system's own style.
//
// info() does not block, as Coot's info dialogs never did. error() names what
// failed and what it failed on; the full detail goes to the terminal, where it
// can be copied into a report. confirm() asks and waits.

#ifndef BANDICOOT_WXUI_MESSAGE_HH
#define BANDICOOT_WXUI_MESSAGE_HH

#include <wx/string.h>

#include <string>

class wxWindow;

namespace wxui {

void info(wxWindow *parent, const wxString &text);

void error(wxWindow *parent, const wxString &what_failed, const wxString &subject,
           const std::string &detail = std::string());

// consequence: shown only when choosing yes has a delayed effect the user
// would not otherwise connect to this moment.
bool confirm(wxWindow *parent, const wxString &question,
             const wxString &yes_label = "OK", const wxString &no_label = "Cancel",
             const wxString &consequence = wxEmptyString);

} // namespace wxui

#endif // BANDICOOT_WXUI_MESSAGE_HH

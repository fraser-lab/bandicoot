// src/wxUI/main/python-console.hh
//
// The Python console (Calculate > Scripting > Python): a script editor above,
// the output below, Run (or Cmd+Return), Save and Exit between them. What a
// run defines lasts to the next run.

#ifndef BANDICOOT_WXUI_PYTHON_CONSOLE_HH
#define BANDICOOT_WXUI_PYTHON_CONSOLE_HH

class wxWindow;

namespace wxui {

// Opens the console, or brings the open one forward.
void show_python_console(wxWindow *parent);

} // namespace wxui

#endif // BANDICOOT_WXUI_PYTHON_CONSOLE_HH

// src/wxUI/main/python-console.cc

#include "wxUI/main/python-console.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/dialogs/file-dialogs.hh"

#include <wx/button.h>
#include <wx/ffile.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/textctrl.h>

#include <string>

namespace wxui {

namespace {

class PythonConsole : public wxFrame {
public:
   explicit PythonConsole(wxWindow *parent)
      : wxFrame(parent, wxID_ANY, "Bandicoot Python Scripting", wxDefaultPosition, wxSize(720, 560),
                wxDEFAULT_FRAME_STYLE | wxFRAME_FLOAT_ON_PARENT) {
      SetBackgroundColour(palette::face());
      wxSplitterWindow *split = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                     wxSP_LIVE_UPDATE | wxSP_3DSASH);
      split->SetMinimumPaneSize(60);
      wxFont mono(wxFontInfo(12).Family(wxFONTFAMILY_TELETYPE));

      editor = new wxTextCtrl(split, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_DONTWRAP);
      editor->SetFont(mono);

      wxPanel *lower = new wxPanel(split);
      lower->SetBackgroundColour(palette::face());
      wxButton *run = new wxButton(lower, wxID_ANY, "Run");
      wxButton *save = new wxButton(lower, wxID_ANY, "Save...");
      wxButton *exit = new wxButton(lower, wxID_ANY, "Exit");
      wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
      buttons->Add(run, 0, wxRIGHT, 6);
      buttons->Add(save, 0);
      buttons->AddStretchSpacer();
      buttons->Add(exit, 0);
      output = new wxTextCtrl(lower, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP);
      output->SetFont(mono);
      wxBoxSizer *lower_col = new wxBoxSizer(wxVERTICAL);
      lower_col->Add(buttons, 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
      lower_col->Add(output, 1, wxEXPAND);
      lower->SetSizer(lower_col);

      // the divider starts half way down, once the window has its size
      split->SplitHorizontally(editor, lower);
      split->SetSashGravity(0.5);
      CallAfter([split]() { split->SetSashPosition(split->GetClientSize().y / 2); });
      wxBoxSizer *outer = new wxBoxSizer(wxVERTICAL);
      outer->Add(split, 1, wxEXPAND | wxALL, 8);
      SetSizer(outer);

      append("# Bandicoot Python console. Write/paste a script above, then Run (or Cmd+Return).\n"
             "# Variables and imports persist between runs; output and errors appear here.\n", false);

      run->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { run_editor(); });
      save->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { save_editor(); });
      exit->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      // Cmd+Return (or Ctrl+Return) runs; plain Return is a new line
      editor->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &e) {
         bool enter = e.GetKeyCode() == WXK_RETURN || e.GetKeyCode() == WXK_NUMPAD_ENTER;
         if (enter && (e.CmdDown() || e.RawControlDown())) run_editor();
         else e.Skip();
      });
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
      editor->SetFocus();
   }

   ~PythonConsole() override { shown = nullptr; }

   static PythonConsole *shown;

private:
   wxTextCtrl *editor;
   wxTextCtrl *output;

   // Errors in red; the pane follows the end.
   void append(const wxString &text, bool is_error) {
      output->SetDefaultStyle(wxTextAttr(is_error ? *wxRED : *wxBLACK));
      output->AppendText(text);
      output->SetDefaultStyle(wxTextAttr(*wxBLACK));
      output->ShowPosition(output->GetLastPosition());
   }

   void run_editor() {
      std::string code = editor->GetValue().utf8_string();
      if (code.find_first_not_of(" \t\r\n") == std::string::npos) return;
      bool had_error = false;
      std::string out = bandicoot_core_console_run(code, had_error);
      if (out.empty() && ! had_error) append("(no output)\n", false);
      else append(wxString::FromUTF8(out), had_error);
   }

   void save_editor() {
      FileRequest request;
      request.title = "Save Script";
      request.types = { { "Python scripts", { ".py" } } };
      request.file_name = "script.py";
      wxString path = save_file(this, request);
      if (path.empty()) return;
      wxFFile f(path, "w");
      if (f.IsOpened() && f.Write(editor->GetValue(), wxConvUTF8))
         append("# saved to " + path + "\n", false);
      else
         append("# ERROR: could not write " + path + "\n", true);
   }
};

PythonConsole *PythonConsole::shown = nullptr;

} // namespace

void show_python_console(wxWindow *parent) {
   if (PythonConsole::shown) {
      PythonConsole::shown->Raise();
      return;
   }
   PythonConsole *c = new PythonConsole(parent);
   PythonConsole::shown = c;
   c->CentreOnParent();
   c->Show();
}

} // namespace wxui

// src/wxUI/widgets/titled-panel.hh
//
// TitledPanel: an etched frame with an optional title set into its top edge,
// as GTK draws a labelled frame, around a content area. Three ready-made
// contents: a slider with its value, a set of radio buttons, a stack of buttons.

#ifndef BANDICOOT_WXUI_TITLED_PANEL_HH
#define BANDICOOT_WXUI_TITLED_PANEL_HH

#include <wx/panel.h>

#include <functional>
#include <vector>

class wxBoxSizer;
class wxButton;
class wxRadioButton;
class wxSlider;
class wxStaticText;

namespace wxui {

class TitledPanel : public wxPanel {
public:
   // bold: the title in bold, as GTK frames titled with <b> markup; the
   // content keeps the ordinary font
   TitledPanel(wxWindow *parent, const wxString &title = wxEmptyString, bool bold = false);

   // Put children here, created with content() as their parent.
   wxPanel *content() const { return inner; }
   wxBoxSizer *content_sizer() const { return inner_sizer; }

private:
   wxPanel *inner;
   wxBoxSizer *inner_sizer;
   int title_height() const;
   void on_paint(wxPaintEvent &e);
};

// A slider over a range of real values between a left and a right arrow,
// each of which moves it one step, with the current value after them.
class StepSlider : public wxPanel {
public:
   StepSlider(wxWindow *parent, double min, double max, double step, double value, int decimals = 2);

   double get_value() const;
   void set_value(double value);

   std::function<void(double value)> on_change;

private:
   double min, step;
   int decimals;
   wxSlider *slider;
   wxStaticText *readout;
   void show_value();
   void step_by(int steps);
};

// A StepSlider in a titled frame.
class TitledSlider : public TitledPanel {
public:
   TitledSlider(wxWindow *parent, const wxString &title,
                double min, double max, double step, double value, int decimals = 2);

   double get_value() const;
   void set_value(double value);

   std::function<void(double value)> on_change;

private:
   StepSlider *control;
};

// One choice from several, laid out vertically or in a row.
class RadioGroup : public TitledPanel {
public:
   RadioGroup(wxWindow *parent, const wxString &title, const std::vector<wxString> &options,
              int initial = 0, bool horizontal = false);

   int get_selection() const;
   void set_selection(int index);

   std::function<void(int index)> on_change;

private:
   std::vector<wxRadioButton *> radios;
};

// A column of push buttons, each with its own action.
class ButtonStack : public TitledPanel {
public:
   explicit ButtonStack(wxWindow *parent, const wxString &title = wxEmptyString);

   wxButton *add_button(const wxString &label, std::function<void()> action);
};

} // namespace wxui

#endif // BANDICOOT_WXUI_TITLED_PANEL_HH

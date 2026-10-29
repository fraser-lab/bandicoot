// src/wxUI/main/sequence-view.hh
//
// SequenceView: Coot's sequence view of one model. Each chain is a row of
// one-letter codes placed by residue number (numbering gaps are blank
// columns; an inserted residue takes a column after its number), coloured by
// secondary structure, between residue-number rulers. Pointing at a residue
// names it; clicking centres on it; the current residue is boxed in orange.
// The chain names stay in view while the rows scroll sideways.
//
// The same panel serves docked (above the surface) and in a window of its
// own; its owner moves it between the two.

#ifndef BANDICOOT_WXUI_SEQUENCE_VIEW_HH
#define BANDICOOT_WXUI_SEQUENCE_VIEW_HH

#include <wx/panel.h>

#include <functional>
#include <string>

class wxStaticText;

namespace wxui {

class BevelButton;
class SequenceCanvas;

class SequenceView : public wxPanel {
public:
   struct Hooks {
      std::function<void()> toggle_docked;  // Undock / Dock
      std::function<void()> close;
   };

   SequenceView(wxWindow *parent, int imol, bool docked, const Hooks &hooks);

   int molecule() const { return imol; }
   // Re-reads the model.
   void rebuild();
   void highlight(const std::string &chain, int resno, const std::string &ins_code);
   int chain_count() const;
   // The panel's height showing n rows of chains.
   int height_for_rows(int n) const;

private:
   int imol;
   Hooks hooks;
   wxStaticText *title;
   SequenceCanvas *canvas;
};

} // namespace wxui

#endif // BANDICOOT_WXUI_SEQUENCE_VIEW_HH

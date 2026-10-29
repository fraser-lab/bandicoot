// src/wxUI/dialogs/toolbar-customizer.hh
//
// ToolbarCustomizer: arranging a toolbar by dragging icons, after the macOS
// toolbar sheet. A palette of every tool the toolbar can hold sits above a
// strip that stands for the toolbar; tools are dragged from the palette into
// the strip, moved within it, or dragged out of it to remove them; Restore
// Defaults puts the default set back. The fixed tools lead the strip and do
// not move. The caller reads the arrangement back once the dialog has closed.

#ifndef BANDICOOT_WXUI_TOOLBAR_CUSTOMIZER_HH
#define BANDICOOT_WXUI_TOOLBAR_CUSTOMIZER_HH

#include <wx/bmpbndl.h>
#include <wx/dialog.h>

#include <functional>
#include <string>
#include <vector>

class wxPopupWindow;
class wxScrolledWindow;
class wxWindow;

namespace wxui {

class ToolbarCustomizer : public wxDialog {
public:
   struct Item {
      std::string id;
      wxString label;
      wxBitmapBundle icon;
   };
   // fixed: the tools that lead the toolbar. palette: everything else it can
   // hold; a tool already on the toolbar is shown dimmed.
   // layout and default_layout are ids from the palette.
   ToolbarCustomizer(wxWindow *parent, const std::vector<Item> &fixed,
                     const std::vector<Item> &palette,
                     const std::vector<std::string> &layout,
                     const std::vector<std::string> &default_layout);
   ~ToolbarCustomizer() override;

   const std::vector<std::string> &arrangement() const { return layout; }

private:
   std::vector<Item> fixed, palette;
   std::vector<std::string> layout, default_layout;

   wxScrolledWindow *palette_panel;
   wxScrolledWindow *strip;
   wxWindow *marker;          // where a drop into the strip would land
   wxPopupWindow *ghost = nullptr;

   // what is being dragged
   enum class Source { None, Palette, Strip };
   Source source = Source::None;
   std::string drag_id;       // Palette
   int drag_index = -1;       // Strip
   std::vector<wxWindow *> strip_tiles;  // the arranged part, in layout order
   std::vector<wxWindow *> all_strip_tiles;  // and the fixed ones
   std::vector<wxWindow *> palette_tiles;

   const Item *find(const std::string &id) const;
   bool on_toolbar(const std::string &id) const;
   void fill_palette();
   void fill_strip();
   void begin_drag(Source s, const std::string &id, int index, const wxPoint &where);
   void move_drag(const wxPoint &where);
   void end_drag(const wxPoint &where);
   void cancel_drag();
   bool over_strip(const wxPoint &where) const;
   int insertion_index(const wxPoint &where) const;
   void show_marker(int index);
   void scroll_strip_near(const wxPoint &where);
   void set_layout(const std::vector<std::string> &l);
};

} // namespace wxui

#endif // BANDICOOT_WXUI_TOOLBAR_CUSTOMIZER_HH

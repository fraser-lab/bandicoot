// src/wxUI/main/about-menu.cc

#include "wxUI/main/about-menu.hh"
#include "wxUI/bridge/core-bridge.hh"
#include "wxUI/assets/bevel-button.hh"
#include "wxUI/assets/bevel.hh"
#include "wxUI/assets/icons.hh"
#include "wxUI/assets/palette.hh"
#include "wxUI/assets/toolbar-button.hh"
#include "wxUI/dialogs/form-dialogs.hh"
#include "wxUI/dialogs/message.hh"
#include "wxUI/widgets/titled-panel.hh"

#include "bandicoot-version.h"
#include "bandicoot-build-id.h"

#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/dialog.h>
#include <wx/display.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/hyperlink.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace wxui {

namespace {

BevelButton *icon_button(wxWindow *parent, const wxString &label, const char *icon_file) {
   BevelButton *b = new BevelButton(parent, wxID_ANY, label);
   if (icon_file) b->set_icon(icon(icon_file, wxSize(16, 16)));
   b->SetMinSize(wxSize(80, 28));
   return b;
}

BevelButton *close_button(wxWindow *parent) {
   return icon_button(parent, "Close", "stock-menu-close.png");
}

// One of each window at a time: opening it again raises the one there is.
wxDialog *single_dialog(wxWindow *parent, wxDialog *&shown, const wxString &title,
                        long style = wxDEFAULT_DIALOG_STYLE) {
   if (shown) {
      shown->Raise();
      return nullptr;
   }
   wxDialog *d = new wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, style);
   d->SetBackgroundColour(palette::face());
   wxDialog **slot = &shown;
   d->Bind(wxEVT_CLOSE_WINDOW, [d](wxCloseEvent &) { d->Destroy(); });
   d->Bind(wxEVT_DESTROY, [d, slot](wxWindowDestroyEvent &e) {
      if (e.GetEventObject() == d) *slot = nullptr;
      e.Skip();
   });
   shown = d;
   return d;
}

// A read-only text in a white box with its own scroll bars.
wxTextCtrl *text_box(wxWindow *parent, const wxString &text, bool wrap) {
   long style = wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2;
   if (! wrap) style |= wxTE_DONTWRAP | wxHSCROLL;
   wxTextCtrl *t = new wxTextCtrl(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, style);
   t->SetBackgroundColour(*wxWHITE);
   return t;
}

// The size asked for, but no taller than the screen the parent is on.
wxSize within_screen(wxWindow *w, wxSize size) {
   int n = wxDisplay::GetFromWindow(w->GetParent() ? w->GetParent() : w);
   wxRect area = wxDisplay(n == wxNOT_FOUND ? 0 : n).GetClientArea();
   size.y = std::min(size.y, area.height - 40);
   return size;
}

wxBoxSizer *right_aligned(wxWindow *button) {
   wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
   row->AddStretchSpacer();
   row->Add(button);
   return row;
}

// ---------------------------------------------- Header/Remarks Browser

// A panel's text, sized to show all of it: the window scrolls, not the panel.
// Not wxTE_DONTWRAP, which gives each panel a horizontal scroll bar; the
// width fits the longest line, so nothing wraps.
class HeaderText : public wxTextCtrl {
public:
   HeaderText(wxWindow *parent, const bandicoot_header_panel &p)
      : wxTextCtrl(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                   wxTE_MULTILINE | wxTE_READONLY | wxTE_NO_VSCROLL |
                   wxTE_RICH2 | wxBORDER_NONE) {
      if (p.fixed_width)
         SetFont(wxFont(wxFontInfo(GetFont().GetPointSize()).Family(wxFONTFAMILY_TELETYPE)));
      SetBackgroundColour(wxColour(p.red, p.green, p.blue));
      set_text(p.text);
   }

   void set_text(const std::string &text) {
      wxString s = wxString::FromUTF8(text);
      if (s.EndsWith("\n")) s.RemoveLast();
      ChangeValue(s);
      int width = 0, lines = 0;
      wxStringTokenizer tk(s, "\n", wxTOKEN_RET_EMPTY_ALL);
      while (tk.HasMoreTokens()) {
         wxString line = tk.GetNextToken();
         width = std::max(width, GetTextExtent(line).x);
         lines++;
      }
      int line_height = GetCharHeight();
      SetMinSize(wxSize(width + 32, std::max(1, lines) * line_height + 8));
   }
};

class HeaderBrowser : public wxDialog {
public:
   HeaderBrowser(wxWindow *parent, int imol_in, const bandicoot_header_view &view)
      : wxDialog(parent, wxID_ANY, "Coot Header Browser", wxDefaultPosition, wxDefaultSize,
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        imol(imol_in) {

      SetBackgroundColour(palette::face());
      wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

      if (! view.title.empty()) {
         wxStaticText *title = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(view.title),
                                                wxDefaultPosition, wxDefaultSize,
                                                wxALIGN_CENTRE_HORIZONTAL);
         title->SetFont(title->GetFont().Bold());
         title_label = title;
         col->Add(title, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
      }

      if (view.mmcif) {
         wxBoxSizer *controls = new wxBoxSizer(wxHORIZONTAL);
         filter = new wxTextCtrl(this, wxID_ANY);
         // off by default: the per-atom and per-residue tables are not header
         // information
         show_all = new wxCheckBox(this, wxID_ANY, "Show all categories");
         controls->Add(new wxStaticText(this, wxID_ANY, "Filter:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
         controls->Add(filter, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
         controls->Add(show_all, 0, wxALIGN_CENTER_VERTICAL);
         col->Add(controls, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);
         filter->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { refresh(); });
         show_all->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { refresh(); });
      }

      scroller = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                      wxVSCROLL | wxHSCROLL | wxBORDER_SUNKEN);
      scroller->SetBackgroundColour(palette::face());
      scroller->SetScrollRate(10, 10);
      wxBoxSizer *rows = new wxBoxSizer(wxVERTICAL);
      for (const bandicoot_header_panel &p : view.panels) {
         TitledPanel *frame = new TitledPanel(scroller, wxString::FromUTF8(p.title));
         HeaderText *text = new HeaderText(frame->content(), p);
         frame->content_sizer()->Add(text, 0, wxEXPAND);
         frame->Show(p.shown);
         rows->Add(frame, 0, wxEXPAND | wxALL, 2);
         panels.push_back({ frame, text });
      }
      scroller->SetSizer(rows);
      col->Add(scroller, 1, wxEXPAND | wxALL, 8);

      BevelButton *close = icon_button(this, "Close", nullptr);
      close->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Close(); });
      col->Add(right_aligned(close), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

      SetSizer(col);
      SetSize(within_screen(this, FromDIP(view.mmcif ? wxSize(640, 560) : wxSize(500, 400))));
      if (title_label) title_label->Wrap(GetClientSize().x - 16);
      scroller->FitInside();
      Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &) { Destroy(); });
   }

private:
   struct Panel {
      TitledPanel *frame;
      HeaderText *text;
   };
   int imol;
   wxStaticText *title_label = nullptr;
   wxTextCtrl *filter = nullptr;
   wxCheckBox *show_all = nullptr;
   wxScrolledWindow *scroller;
   std::vector<Panel> panels;

   void refresh() {
      bandicoot_header_view view;
      if (! bandicoot_core_header_view(imol, filter->GetValue().utf8_string(),
                                       show_all->GetValue(), &view))
         return; // the molecule has gone: keep what is shown
      if (view.panels.size() != panels.size()) return;
      scroller->Freeze();
      for (size_t i = 0; i < panels.size(); i++) {
         const bandicoot_header_panel &p = view.panels[i];
         if (p.shown) panels[i].text->set_text(p.text);
         panels[i].frame->Show(p.shown);
      }
      scroller->Layout();
      scroller->FitInside();
      scroller->Thaw();
   }
};

void show_header_browser(wxWindow *parent, int imol) {
   bandicoot_header_view view;
   if (! bandicoot_core_header_view(imol, std::string(), false, &view)) {
      info(parent, "WARNING:: No header information");
      return;
   }
   HeaderBrowser *d = new HeaderBrowser(parent, imol, view);
   d->CentreOnParent();
   d->Show();
}

void remarks_browser(wxWindow *parent) {
   MoleculeChooser::molecule_list models = bandicoot_core_goto_molecules();
   if (models.empty()) {
      info(parent, "No molecules to view");
      return;
   }
   ChooserDialog *d = new ChooserDialog(parent, "View REMARKs in Molecule", wxEmptyString, models);
   d->on_ok = [parent](int imol) { show_header_browser(parent, imol); };
   d->CentreOnParent();
   d->Show();
}

// ---------------------------------------------- Hints

const char *hints_text =
   "Mouse\n"
   "  Left-drag:                 Rotate the view\n"
   "  Ctrl + left-drag:          Pan (move the view centre)\n"
   "  Shift + left-click:        Label an atom (double-click also works)\n"
   "  Middle-click:              Centre on the atom clicked\n"
   "  Middle-drag:               Pan\n"
   "  Right-drag:                Zoom\n"
   "  Ctrl + right-drag:         Change depth and clipping\n"
   "  Ctrl + Shift + right-drag: Rotate around the screen Z axis\n"
   "  Scroll wheel:              Change the contour level of the scroll map\n"
   "\n"
   "On a Mac trackpad or one-button mouse, Option-click stands in for\n"
   "the middle button. N and M zoom in and out.\n"
   "\n"
   "Keyboard\n"
   "  + / -   Change the contour level of the scroll map\n"
   "  Choose the scroll map with the Scroll button in the Display Manager.\n"
   "\n"
   "Left-click both picks atoms and rotates the view, so when a tool is\n"
   "waiting for you to pick atoms, start a rotation away from the model.\n"
   "\n"
   "During refinement, drag an atom to pull it and its neighbours;\n"
   "Ctrl-drag to move that atom alone.\n";

wxDialog *hints_shown = nullptr;

void hints(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, hints_shown, "Hints", wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
   if (! d) return;
   wxTextCtrl *t = text_box(d, hints_text, false);
   // the columns of the mouse table line up only in a fixed-width font
   t->SetFont(wxFont(wxFontInfo(t->GetFont().GetPointSize()).Family(wxFONTFAMILY_TELETYPE)));
   BevelButton *ok = icon_button(d, "OK", nullptr);
   ok->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(t, 1, wxEXPAND | wxALL, 8);
   col->Add(right_aligned(ok), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d->SetSizer(col);
   wxSize text_size = t->GetTextExtent("  Ctrl + Shift + right-drag: Rotate around the screen Z axis  ");
   d->SetSize(within_screen(d, wxSize(text_size.x + d->FromDIP(60), d->FromDIP(520))));
   d->CentreOnParent();
   d->Show();
}

// ---------------------------------------------- About, Credits, License, References

const char *written_by =
   "   Paul Emsley\n"
   "   Kevin Cowtan\n"
   "   Bernhard Lohkamp\n"
   "\n"
   "Using addtional code by\n"
   "   Kevin Keating\n"
   "   Stuart McNicholas\n"
   "   Martin Noble\n"
   "   Jan Gruber\n"
   "\n"
   "Using the libraries of:\n"
   "   Eugene Krissinel\n"
   "   Kevin Cowtan\n"
   "   Janne Lof\n"
   "   Raghavendra Chandrashekara\n"
   "   Paul Bourke & Cory Gene Bloyd\n"
   "   Matteo Frigo & Steven G. Johnson\n"
   "   & many others.\n"
   "\n"
   "Windows 2000 Binaries\n"
   "   Bernhard Lohkamp\n"
   "\n"
   "  Macintosh Binaries\n"
   "   William Scott\n"
   "\n"
   "Bandicoot fork\n"
   "   Art Lyubimov\n";

const char *documented_by = "Paul Emsley\n";

const char *translated_by =
   "Miguel Ortiz-Lombardia.\n"
   "\n"
   "Translations don't work yet, but when they do, we can\n"
   "have a Spanish Coot because of Miguel's work.\n";

const char *artwork_by =
   "John O'Neill took the bandicoot photo for the splash screen "
   "(https://commons.wikimedia.org/w/index.php?curid=41268880)\n"
   "\n"
   "\"radacina\" make the \"edit restraints\" icon.\n"
   "\n"
   "\"lunanaut\" made the connect-to-world globe icon.\n"
   "\n"
   "\"crispy\" made the mouse icon.\n"
   "\n"
   "Art Lyubimov created the splash screen, the Bandicoot icon, and a bunch of "
   "new and improved icons all over the place\n"
   "\n"
   "Everything else: Paul Emsley\n";

const char *copyright_text =
   "Copyright 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012 by The University of York\n"
   "Copyright 2007, 2008, 2009, 2010, 2011, 2012 by The University of Oxford\n"
   "Copyright 2001, 2007 by Paul Emsley";

const char *coot_website = "http://www2.mrc-lmb.cam.ac.uk/Personal/pemsley/coot";

wxDialog *credits_shown = nullptr;

void credits(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, credits_shown, "Credits");
   if (! d) return;
   wxNotebook *tabs = new wxNotebook(d, wxID_ANY);
   const std::vector<std::pair<const char *, const char *>> pages = {
      { "Written by", written_by }, { "Documented by", documented_by },
      { "Translated by", translated_by }, { "Artwork by", artwork_by } };
   for (const auto &page : pages)
      tabs->AddPage(text_box(tabs, wxString::FromUTF8(page.second), true), page.first);
   BevelButton *close = close_button(d);
   close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(tabs, 1, wxEXPAND | wxALL, 8);
   col->Add(right_aligned(close), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d->SetSizer(col);
   d->SetSize(d->FromDIP(wxSize(360, 460)));
   d->CentreOnParent();
   d->Show();
}

wxDialog *license_shown = nullptr;

void license(wxWindow *parent) {
   wxString path = wxFileName(wxString::FromUTF8(bandicoot_core_package_data_dir()), "COPYING").GetFullPath();
   wxString text;
   wxFFile file(path, "r");
   if (! file.IsOpened() || ! file.ReadAll(&text)) {
      error(parent, "Could not read the license", path, "License: could not read " + path.utf8_string());
      return;
   }
   wxDialog *d = single_dialog(parent, license_shown, "License", wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
   if (! d) return;
   wxTextCtrl *t = text_box(d, text, false);
   BevelButton *close = close_button(d);
   close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(t, 1, wxEXPAND | wxALL, 8);
   col->Add(right_aligned(close), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d->SetSizer(col);
   wxSize text_size = t->GetTextExtent(
      "  freedoms that you received.  You must make sure that they, too, receive  ");
   d->SetSize(within_screen(d, wxSize(text_size.x + d->FromDIP(60), d->FromDIP(900))));
   d->CentreOnParent();
   d->Show();
}

struct Reference {
   const char *button;
   const char *description;
   const char *author, *title, *journal, *year, *volume, *pages;
   const char *bib_id, *bib_author;
   const char *bib_title;    // null: the title
   const char *bib_journal;  // null: the journal
};

const char *acta_d = "ACTA CRYSTALLOGRAPHICA SECTION D-BIOLOGICAL CRYSTALLOGRAPHY";

const std::vector<Reference> &references() {
   static const std::vector<Reference> refs = {
      { "Coot",
        "If you have found this software to be useful, you are requested to cite:",
        "Emsley P, Lohkamp B, Scott W, Cowtan K", "Features and Development of Coot", acta_d,
        "2010", "66", "486-501",
        "emsley10:coot", "Paul Emsley, Bernhard Lohkamp, William Scott and Kevin Cowtan",
        nullptr, "Acta Crystallographica Section D - Biological Crystallography" },
      { "WinCoot",
        "Please cite as for Coot now. You find additional information on WinCoot in the "
        "following. Feel free to cite as well as above:",
        "Lohkamp B, Emsley P, Cowtan K", "Coot News", "CCP4 Newsletter", "2005", "42", "",
        "lohkamp05:wincoot", "Bernhard Lohkamp and Paul Emsley and Kevin Cowtan",
        nullptr, nullptr },
      { "Refmac dictionary",
        "The reference for the REFMAC5 Dictionary is:",
        "Vagin AA, Steiner RA, Lebedev AA, Potterton L, McNicholas S, Long F, Murshudov GN",
        "REFMAC5 dictionary: organization of prior chemical knowledge and guidelines for its use",
        acta_d, "2004", "60", "2184-2195",
        "Vagin:ba5073",
        "Vagin, Alexei A. and Steiner, Roberto A. and Lebedev, Andrey A. and Potterton, Liz and "
        "McNicholas, Stuart and Long, Fei and Murshudov, Garib N.",
        "{\\it REFMAC}5 dictionary: organization of prior chemical knowledge and guidelines for its use",
        "Acta Crystallographica Section D" },
      { "SSM",
        "If using \"SSM Superposition\", please cite:",
        "Krissinel E, Henrick K",
        "Secondary-structure matching (SSM), a new tool for fast protein structure alignment in three dimensions",
        acta_d, "2004", "60", "2256-2268",
        "Krissinel:ba5056", "Krissinel, E. and Henrick, K.", nullptr, "Acta Crystallographica Section D" },
      { "MMDB",
        "The reference for the Macromolecular Database (MMDB) is:",
        "Krissinel EB, Winn MD, Ballard CC, Ashton AW, Patel P, Potterton EA, McNicholas SJ, Cowtan KD, Emsley P.",
        "The new CCP4 Coordinate Library as a toolkit for the design of coordinate-related "
        "applications in protein crystallography",
        acta_d, "2004", "60", "2250-2255",
        "Krissinel:ba5055",
        "Krissinel, E. B. and Winn, M. D. and Ballard, C. C. and Ashton, A. W. and Patel, P. and "
        "Potterton, E. A. and McNicholas, S. J. and Cowtan, K. D. and Emsley, P.",
        nullptr, "Acta Crystallographica Section D" },
      { "Clipper",
        "The reference for clipper is:",
        "Cowtan K", "The Clipper C++ libraries for X-ray crystallography",
        "IUCr Computing Commission Newsletter", "2003", "2", "4-9",
        "cowtan03:clipper", "Kevin Cowtan", nullptr, nullptr },
      { "Buccaneer",
        "If using the Coot sequencing tool (\"Cootaneer\") or Fast Secondary Structure Search, please cite:",
        "Cowtan K", "Fitting molecular fragments into electron density", acta_d, "2008", "64", "83-89",
        "Cowtan:ba5104", "Cowtan, Kevin", nullptr, "Acta Crystallographica Section D" },
      { "Molprobity",
        "If using Molprobity tools (probe, reduce, etc.), please cite:",
        "Davis IW, Leaver-Fay A, Chen VB, Block JN, Kapral GJ, Wang X, Murray LW, Arendall WB 3rd, "
        "Snoeyink J, Richardson JS, Richardson DC.",
        "MolProbity: all-atom contacts and structure validation for proteins and nucleic acids",
        "Nucleic Acids Research", "2007", "35", "375-383",
        "Davis:molprobity07",
        "Ian W. Davis and Andrew Leaver-Fay and Vincent B. Chen and Jeremy N. Block and Gary J. Kapral "
        "and Xueyi Wang and Laura W. Murray and W. Bryan Arendall III and Jack Snoeyink and "
        "Jane S. Richardson and David C. Richardson",
        nullptr, nullptr },
      { "C-ALPHA",
        "The reference for CALPHA (idea as used in Coot when converting batons to main-chain atoms) is:",
        "Esnouf RM",
        "Polyalanine Reconstruction from C[alpha] Positions Using the Program CALPHA Can Aid "
        "Initial Phasing of Data by Molecular Replacement Procedures",
        acta_d, "1997", "53", "665--672",
        "Esnouf:ad0021", "Esnouf, R. M.",
        "Polyalanine Reconstruction from C{$\\alpha$} Positions Using the Program {\\it CALPHA} Can "
        "Aid Initial Phasing of Data by Molecular Replacement Procedures",
        "Acta Crystallographica Section D" },
      { "X-LIGAND",
        "The reference for X-LIGAND (algorithm similar to is used in ligand fitting in Coot):",
        "Oldfield, TJ",
        "X-LIGAND: an application for the automated addition of flexible ligands into electron density",
        acta_d, "2001", "57", "696-705",
        "Oldfield:be0006", "Oldfield, T. J.",
        "{\\it X-LIGAND}: an application for the automated addition of flexible ligands into electron density",
        "Acta Crystallographica Section D" },
      { "EDS",
        "The reference for The Electron Density Server:",
        "Kleywegt GJ, Harris MR, Zou JY, Taylor TC, Wahlby A, Jones TA",
        "The Uppsala Electron-Density Server", acta_d, "2004", "60", "2240-2249",
        "kleywegt:eds",
        "G. J.,Kleywegt and M. R. Harris and J. Y. Zou and T. C. Taylor and A. Wahlby A and T. A. Jones",
        "The Uppsala Electron-Density Server", "Acta Crystallographica Section D" },
   };
   return refs;
}

wxString reference_text(const Reference &r) {
   return wxString::FromUTF8(std::string(r.description) + "\n\n" +
                             r.author + "\n" + r.title + "\n" +
                             r.journal + " " + r.volume + ", " + r.pages + ", " + r.year + ".\n");
}

wxString bibtex_text(const Reference &r) {
   std::string s = std::string("@Article{") + r.bib_id + ",\n";
   s += std::string("  author  =  {") + r.bib_author + "},\n";
   s += std::string("  title   =  {") + (r.bib_title ? r.bib_title : r.title) + "},\n";
   s += std::string("  journal =  {") + (r.bib_journal ? r.bib_journal : r.journal) + "},\n";
   s += std::string("  year    =  ") + r.year + ",\n";
   s += std::string("  volume  =  ") + r.volume + ",\n";
   if (std::string(r.pages).empty())
      s += "\n";
   else
      s += std::string("  pages   =  ") + r.pages + "\n";
   s += "}";
   return wxString::FromUTF8(s);
}

// The column of reference names, in a raised frame.
class ReferenceList : public wxPanel {
public:
   ReferenceList(wxWindow *parent) : wxPanel(parent) {
      SetBackgroundColour(palette::face());
      Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
         wxPaintDC dc(this);
         draw_frame(dc, wxRect(GetClientSize()), FrameStyle::Raised, GetBackgroundColour(),
                    palette::shadow_of(GetBackgroundColour()));
      });
   }
};

wxDialog *references_shown = nullptr;

void show_references(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, references_shown, "References",
                               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
   if (! d) return;

   ReferenceList *list = new ReferenceList(d);
   wxBoxSizer *names = new wxBoxSizer(wxVERTICAL);

   wxNotebook *tabs = new wxNotebook(d, wxID_ANY);
   wxTextCtrl *ref = text_box(tabs, wxEmptyString, true);
   wxTextCtrl *bib = text_box(tabs, wxEmptyString, false);
   tabs->AddPage(ref, "Reference");
   tabs->AddPage(bib, "BibTeX");

   auto show_reference = [ref, bib, tabs](size_t i) {
      ref->ChangeValue(reference_text(references()[i]));
      bib->ChangeValue(bibtex_text(references()[i]));
      tabs->SetSelection(0);
   };
   for (size_t i = 0; i < references().size(); i++) {
      ToolbarButton *b = new ToolbarButton(list, wxID_ANY, references()[i].button, wxBitmapBundle(),
                                           ToolbarButton::Content::Text);
      b->UnsetToolTip();
      b->Bind(wxEVT_BUTTON, [show_reference, i](wxCommandEvent &) { show_reference(i); });
      names->Add(b, 0, wxEXPAND | wxLEFT | wxRIGHT, 4);
   }
   wxBoxSizer *list_box = new wxBoxSizer(wxVERTICAL);
   list_box->Add(names, 1, wxEXPAND | wxALL, 4);
   list->SetSizer(list_box);

   BevelButton *close = close_button(d);
   close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });

   wxBoxSizer *body = new wxBoxSizer(wxHORIZONTAL);
   body->Add(list, 0, wxEXPAND | wxRIGHT, 16);
   body->Add(tabs, 1, wxEXPAND);
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);
   col->Add(body, 1, wxEXPAND | wxALL, 8);
   col->Add(right_aligned(close), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
   d->SetSizer(col);
   show_reference(0);
   d->SetSize(d->FromDIP(wxSize(565, 450)));
   d->CentreOnParent();
   d->Show();
}

wxDialog *about_shown = nullptr;

void about(wxWindow *parent) {
   wxDialog *d = single_dialog(parent, about_shown, "About Bandicoot");
   if (! d) return;
   wxBoxSizer *col = new wxBoxSizer(wxVERTICAL);

   // bandicoot.png is 377 x 405
   wxStaticBitmap *logo = new wxStaticBitmap(d, wxID_ANY, icon("bandicoot.png", wxSize(60, 64)));
   col->Add(logo, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 12);

   wxStaticText *name = new wxStaticText(d, wxID_ANY,
      wxString("Bandicoot ") + BANDICOOT_VERSION + BANDICOOT_BUILD_SUFFIX);
   name->SetFont(name->GetFont().Bold().Scaled(1.4f));
   col->Add(name, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 10);

   wxStaticText *copyright = new wxStaticText(d, wxID_ANY, copyright_text, wxDefaultPosition,
                                              wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
   copyright->SetFont(copyright->GetFont().Smaller());
   col->Add(copyright, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT | wxTOP, 12);

   wxHyperlinkCtrl *link = new wxHyperlinkCtrl(d, wxID_ANY, "Coot Website", coot_website);
   col->Add(link, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, 8);

   BevelButton *credits_button = icon_button(d, "Credits", "stock-menu-about.png");
   BevelButton *references_button = icon_button(d, "References", nullptr);
   BevelButton *license_button = icon_button(d, "License", nullptr);
   BevelButton *close = close_button(d);
   credits_button->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { credits(d); });
   references_button->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { show_references(d); });
   license_button->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { license(d); });
   close->Bind(wxEVT_BUTTON, [d](wxCommandEvent &) { d->Close(); });

   wxBoxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
   buttons->Add(credits_button, 0, wxRIGHT, 6);
   buttons->Add(references_button, 0, wxRIGHT, 6);
   buttons->Add(license_button);
   buttons->AddStretchSpacer();
   buttons->Add(close);
   col->Add(buttons, 0, wxEXPAND | wxALL, 12);

   d->SetSizerAndFit(col);
   d->CentreOnParent();
   d->Show();
}

const std::map<std::string, std::function<void(wxWindow *)>> &about_actions() {
   static const std::map<std::string, std::function<void(wxWindow *)>> actions = {
      { "on_remarks_browser1_activate", remarks_browser },
      { "on_hints1_activate",           hints },
      { "on_about1_activate",           about },
   };
   return actions;
}

} // namespace

bool is_about_action(const std::string &action) {
   return about_actions().count(action) > 0;
}

bool run_about_action(wxWindow *parent, const std::string &action) {
   auto it = about_actions().find(action);
   if (it == about_actions().end()) return false;
   it->second(parent);
   return true;
}

} // namespace wxui

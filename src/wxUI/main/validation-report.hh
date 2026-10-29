// src/wxUI/main/validation-report.hh
//
// The wwPDB validation report of an entry fetched from the PDB: its
// percentile ranks drawn as sliders, and its problem residues as a
// click-through list.

#ifndef BANDICOOT_WXUI_VALIDATION_REPORT_HH
#define BANDICOOT_WXUI_VALIDATION_REPORT_HH

struct bandicoot_validation_report;
class wxWindow;

namespace wxui {

void show_validation_report(wxWindow *parent, const bandicoot_validation_report &report);

} // namespace wxui

#endif // BANDICOOT_WXUI_VALIDATION_REPORT_HH

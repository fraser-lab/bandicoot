/* src/restraints-gui.cc
 *
 * Bandicoot v0.2: the load-time ligand restraints notification.
 *
 * The load never blocks and never asks: generating restraints takes seconds,
 * and a coordinate load should feel like a coordinate load. This dialog offers
 * the work instead, and can be ignored indefinitely.
 *
 * Rows are component ids, never molecule numbers, and their contents are
 * recomputed from live state rather than stored. A long-lived dialog is a
 * request, not a transaction: by the time it is acted on, molecules may have
 * been deleted or dictionaries imported, and re-deriving makes all of that
 * harmless.
 *
 * Generation itself blocks, so that refinement cannot start against a
 * half-written dictionary.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifdef USE_PYTHON
#include <Python.h>  // before system includes, for the POSIX_C_SOURCE clash
#endif

#include "compat/coot-sysdep.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "restraints-gui.hh"

#include <mmdb2/mmdb_tables.h>          // isSolvent/isAminoacid/isNucleotide
#include "utils/coot-utils.hh"          // coot::util::int_to_string()
#include "coot-utils/comp-id-collision.hh" // comp_ids_matching_dictionary()
#include "graphics-info.h"
#include "c-interface.h"                // info_dialog(), graphics_draw(),
                                        // is_valid_model_molecule()
#ifdef USE_PYTHON
#include "cc-interface-scripting.hh"   // safe_python_command_with_return()
#endif

namespace {

   // The one dialog. There is never a second: a new load adds rows to this one.
   GtkWidget *dialog     = NULL;
   GtkWidget *rows_vbox  = NULL;
   GtkWidget *generate_button = NULL;
   GtkWidget *header_label = NULL;

   // Set while a generation run is in progress, so that responses arriving on
   // the notification dialog during the run (the window manager can still
   // deliver a delete-event) do not destroy it from under the loop.
   bool generation_running = false;

   // Show every ligand (Modelling menu) rather than only what lacks a
   // dictionary (a coordinate load). One dialog instance serves both: the menu
   // switches it into show-all, a later load ADDS rows without changing the
   // mode, and closing the dialog resets it (2026-09-03).
   bool show_all_mode = false;

   // Rows the user has switched OFF. Carried across a rebuild; everything else
   // about a row is recomputed. Comp ids, so this survives deletions too.
   std::set<std::string> unchecked;

   const char *COMP_ID_KEY = "bandicoot-comp-id";

   // What already describes a component. Generate silently only when nothing
   // does; otherwise make the user ask for it.
   //
   // The storage scope IS the provenance: the monomer library loads unscoped,
   // while a generated dictionary is stored against its own molecule. A global
   // import is indistinguishable from a library entry, which is acceptable --
   // neither should be overwritten without being asked.
   enum row_state_t { ROW_NEEDS, ROW_LIBRARY, ROW_GENERATED };

   // One row: a component, where it was found, and what already describes it.
   // imols and names are strictly derived -- see the header comment.
   struct row_t {
      std::string comp_id;
      std::vector<int> imols;
      std::vector<std::string> names;
      row_state_t state;
      row_t() : state(ROW_NEEDS) {}
   };

   row_state_t component_state(int imol, const std::string &comp_id) {

      graphics_info_t g;
      const coot::protein_geometry &geom = *g.Geom_p();

      // Scanned directly rather than through the index lookup, which stops at
      // the first scope match and so cannot tell whether a scoped entry also
      // exists alongside an unscoped one.
      bool scoped = false, global = false;
      for (unsigned int i=0; i<geom.size(); i++) {
         const std::pair<int, coot::dictionary_residue_restraints_t> &e = geom[i];
         if (e.second.residue_info.comp_id != comp_id) continue;
         if (e.second.is_bond_order_data_only()) continue;   // minimal, not restraints
         if (e.first == imol) scoped = true;
         else if (e.first == coot::protein_geometry::IMOL_ENC_ANY) global = true;
      }

      // Scoped first: that is the precedence refinement itself uses.
      if (scoped) return ROW_GENERATED;
      if (global) return ROW_LIBRARY;
      return ROW_NEEDS;
   }

   // Is this component a ligand, i.e. worth deriving restraints for?
   //
   // Uses mmdb's tables rather than Coot's standard-residue list, which covers
   // only the amino acids and so treats every nucleotide as a ligand. Deriving
   // self-made restraints for a nucleic acid backbone is the harm this filter
   // exists to prevent. Modified bases are not in mmdb's tables either, but
   // they have library dictionaries, so they arrive unticked.
   bool is_ligand_comp_id(const std::string &comp_id) {

      if (comp_id.empty()) return false;
      const char *n = comp_id.c_str();
      if (mmdb::isSolvent(n))    return false;
      if (mmdb::isAminoacid(n))  return false;
      if (mmdb::isNucleotide(n)) return false;
      return true;
   }

   // Every component in every loaded molecule that still has no dictionary.
   //
   // no_dictionary_for_residue_type_as_yet() is the same question the load path
   // asks, after the same lookup ladder, so a component the user has since
   // supplied a dictionary for simply stops being listed. That is also why
   // nothing here needs to know about dictionary imports: it asks rather than
   // tracking.
   // show_all false: only what has no dictionary -- the load-time question,
   //                 asked exactly as before.
   // show_all true:  every ligand in every loaded molecule, whatever already
   //                 describes it, so the user can deliberately replace
   //                 restraints they do not like (Modelling menu).
   std::vector<row_t> live_rows(bool show_all) {

      graphics_info_t g;
      std::vector<row_t> rows;

      for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
         if (! is_valid_model_molecule(imol)) continue;

         std::vector<std::string> types;
         if (show_all) {
            std::vector<std::string> all =
               coot::util::non_standard_residue_types_in_molecule(g.molecules[imol].atom_sel.mol);
            for (unsigned int i=0; i<all.size(); i++)
               if (is_ligand_comp_id(all[i]))
                  types.push_back(all[i]);
         } else {
            // Unchanged: the same lookup ladder the load path uses, so the
            // load-time dialog behaves exactly as it did before show_all.
            types = g.molecules[imol].no_dictionary_for_residue_type_as_yet(*g.Geom_p());
         }

         for (unsigned int i=0; i<types.size(); i++) {
            unsigned int j = 0;
            for (; j<rows.size(); j++)
               if (rows[j].comp_id == types[i]) break;
            if (j == rows.size()) {
               row_t r;
               r.comp_id = types[i];
               rows.push_back(r);
            }
            rows[j].imols.push_back(imol);
            rows[j].names.push_back(g.molecules[imol].name_for_display_manager());

            // A row spans molecules and the state can differ between them
            // (generated in one, missing in another). Report the WEAKEST, so a
            // row still offering something to do reads as needing doing -- and
            // ticking it fills the gaps, because generation fans out over every
            // molecule in the row.
            // ROW_NEEDS < ROW_LIBRARY < ROW_GENERATED, so "weakest" is the
            // smallest. imols.size()==1 means this is the row's first molecule.
            row_state_t s = show_all ? component_state(imol, types[i]) : ROW_NEEDS;
            if (rows[j].imols.size() == 1) rows[j].state = s;
            else if (s < rows[j].state)    rows[j].state = s;
         }
      }
      return rows;
   }

   // "LIG (x0191-pandda-model.pdb)".
   //
   // Both halves, because neither alone is enough: a comp id says nothing about
   // where it came from when several files are open, and a file name is not
   // descriptive of one residue inside a whole structure. A comp id can occur
   // in several files, so the label may carry more than one name -- and it is
   // still ONE row, because one dictionary will serve all of them.
   std::string row_label(const row_t &r) {

      std::string label = r.comp_id;
      if (r.names.empty()) return label;

      label += " (";
      unsigned int shown = (r.names.size() > 2) ? 1 : r.names.size();
      for (unsigned int i=0; i<shown; i++) {
         if (i) label += ", ";
         label += r.names[i];
      }
      if (r.names.size() > shown) {
         label += " +";
         label += coot::util::int_to_string(r.names.size() - shown);
         label += " more";
      }
      label += ")";

      // Say WHY a row arrives unticked. Without this, an unchecked FMN just
      // looks like the dialog forgot it.
      if (r.state == ROW_LIBRARY)
         label += "  -- has a library dictionary";
      else if (r.state == ROW_GENERATED)
         label += "  -- restraints already generated";

      return label;
   }

   std::string comp_id_of_row_widget(GtkWidget *w) {
      const char *s = (const char *) g_object_get_data(G_OBJECT(w), COMP_ID_KEY);
      return s ? std::string(s) : std::string();
   }

   // Read the check buttons back into `unchecked` before they are destroyed.
   void remember_check_state() {

      if (! rows_vbox) return;
      GList *children = gtk_container_get_children(GTK_CONTAINER(rows_vbox));
      for (GList *l=children; l; l=l->next) {
         GtkWidget *w = GTK_WIDGET(l->data);
         std::string comp_id = comp_id_of_row_widget(w);
         if (comp_id.empty()) continue;
         if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
            unchecked.erase(comp_id);
         else
            unchecked.insert(comp_id);
      }
      g_list_free(children);
   }

   std::vector<std::string> checked_comp_ids() {

      std::vector<std::string> v;
      if (! rows_vbox) return v;
      GList *children = gtk_container_get_children(GTK_CONTAINER(rows_vbox));
      for (GList *l=children; l; l=l->next) {
         GtkWidget *w = GTK_WIDGET(l->data);
         std::string comp_id = comp_id_of_row_widget(w);
         if (comp_id.empty()) continue;
         if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
            v.push_back(comp_id);
      }
      g_list_free(children);
      return v;
   }

   // Python functions are called by BARE name, with no module prefix: the
   // loader exec()s these modules into __main__ rather than importing them, so
   // there is no module object to reach through.

   // "elbow", "acedrg", or empty. Asked afresh each time the dialog is built,
   // so a user who sets their environment up and reopens the dialog is not told
   // the old answer.
   std::string generator_name() {

      std::string s;
#ifdef USE_PYTHON
      PyGILState_STATE gil = PyGILState_Ensure();
      PyObject *r = safe_python_command_with_return("restraint_generator_name()");
      if (r && PyString_Check(r))
         s = PyString_AsString(r);
      PyGILState_Release(gil);
#endif
      return s;
   }

   // Rebuild the row list from live state. Returns the number of rows.
   int rebuild_rows() {

      if (! rows_vbox) return 0;

      remember_check_state();

      // gtk_widget_destroy on each child; the container empties itself.
      GList *children = gtk_container_get_children(GTK_CONTAINER(rows_vbox));
      for (GList *l=children; l; l=l->next)
         gtk_widget_destroy(GTK_WIDGET(l->data));
      g_list_free(children);

      std::vector<row_t> rows = live_rows(show_all_mode);
      for (unsigned int i=0; i<rows.size(); i++) {
         GtkWidget *cb = gtk_check_button_new_with_label(row_label(rows[i]).c_str());
         g_object_set_data_full(G_OBJECT(cb), COMP_ID_KEY,
                                g_strdup(rows[i].comp_id.c_str()), g_free);
         // Ticked only when nothing describes it. A component that already
         // has restraints arrives UNTICKED, so ticking it is the user saying
         // "yes, replace them" -- that ladder, expressed as the default state
         // rather than as a confirmation dialog per component. On a structure
         // with several ligands a modal each would be a storm.
         bool want = (rows[i].state == ROW_NEEDS) &&
                     (unchecked.find(rows[i].comp_id) == unchecked.end());
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), want);
         gtk_box_pack_start(GTK_BOX(rows_vbox), cb, FALSE, FALSE, 1);
         gtk_widget_show(cb);
      }

      return rows.size();
   }

   // ---- generation -------------------------------------------------------

   void pump_events() {
      while (gtk_events_pending())
         gtk_main_iteration();
   }

   // Progress is reported in this dialog's own header rather than in a separate
   // window: the blocked draw window and the wait cursor already say "wait".
   //
   // The modality is not cosmetic. Pumping events repaints the message between
   // components and also delivers whatever was clicked during the previous one,
   // so without a modal grab a click could start refinement mid-run.
   const char *HEADER_IDLE = "These components loaded without restraints:";

   void set_busy(bool busy) {

      if (! dialog) return;

      gtk_window_set_modal(GTK_WINDOW(dialog), busy ? TRUE : FALSE);
      gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
                                        GTK_RESPONSE_ACCEPT, busy ? FALSE : TRUE);
      gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog),
                                        GTK_RESPONSE_CLOSE, busy ? FALSE : TRUE);
      if (rows_vbox)
         gtk_widget_set_sensitive(rows_vbox, busy ? FALSE : TRUE);
      if (! busy && header_label)
         gtk_label_set_text(GTK_LABEL(header_label), HEADER_IDLE);
   }

   void say_progress(const std::string &message) {

      if (header_label)
         gtk_label_set_text(GTK_LABEL(header_label), message.c_str());
      pump_events();
   }

   // Run the generator for one component. Returns the tagged status string from
   // bandicoot_restraints.generate_restraints_status().
   std::string generate_one(int imol, const std::string &comp_id) {

      std::string s;
#ifdef USE_PYTHON
      std::string cmd = "generate_restraints_status(";   // bare: see above
      cmd += coot::util::int_to_string(imol);
      cmd += ", \"" + comp_id + "\")";

      PyGILState_STATE gil = PyGILState_Ensure();
      PyObject *r = safe_python_command_with_return(cmd);
      if (r && PyString_Check(r))
         s = PyString_AsString(r);
      PyGILState_Release(gil);
#endif
      return s;
   }

   void run_generation(const std::vector<std::string> &wanted) {

      generation_running = true;

      std::vector<std::string> failed;    // "LIG: message"
      std::vector<std::string> warned;
      int generated = 0;
      int dropped = 0;                    // no longer needed by the time we got here

      // Ask now rather than when the dialog was built -- see the note in
      // build_dialog(). This is the call that may have to consult the user's
      // login shell, and here the user has asked for the work.
      if (generator_name().empty()) {
         generation_running = false;
         std::cout << "WARNING:: no restraint generator: neither phenix.elbow "
                   << "nor acedrg could be found" << std::endl;
         info_dialog("No restraint generator was found.\n\n"
                     "Bandicoot looked for phenix.elbow and acedrg, both on\n"
                     "PATH and in the environment your login shell provides.\n"
                     "Install Phenix or CCP4, or read in a dictionary with\n"
                     "File -> Import CIF dictionary...");
         return;
      }

      set_busy(true);

      for (unsigned int i=0; i<wanted.size(); i++) {

         // RE-VALIDATE, one component at a time and immediately before
         // running. The list was read off widgets that may have been sitting
         // there for an hour, and the previous component's dictionary may even
         // have covered this one.
         //
         // FAN OUT ACROSS EVERY MOLECULE IN THE ROW. Dictionaries are stored
         // per molecule now, so describing a comp id once no longer describes
         // it everywhere: taking imols[0] left every other molecule holding
         // that ligand with no restraints at all (measured 2026-09-03 --
         // molecule 0 got 20 bonds, molecule 1 got none). One row still means
         // one chemistry to describe; it now means N molecules to describe it
         // TO. The same fan-out is what Auto import needs, for the same reason.
         std::vector<int> imols;
         {
            std::vector<row_t> rows = live_rows(show_all_mode);
            for (unsigned int j=0; j<rows.size(); j++)
               if (rows[j].comp_id == wanted[i]) { imols = rows[j].imols; break; }
         }
         if (imols.empty()) {
            dropped++;
            continue;
         }

         std::string m = "Generating restraints for " + wanted[i];
         if (wanted.size() > 1) {
            m += " (";
            m += coot::util::int_to_string(i + 1);
            m += " of ";
            m += coot::util::int_to_string(wanted.size());
            m += ")";
         }
         m += "...";
         say_progress(m);

         // Derived separately for each molecule rather than generated once and
         // copied: two molecules can hold chemically DIFFERENT ligands under
         // one comp id, and that is precisely the case per-molecule storage
         // exists to serve. Sharing one dictionary between them would put back
         // the bug the scoping removed.
         bool any_ok = false;
         for (unsigned int k=0; k<imols.size(); k++) {

            if (! is_valid_model_molecule(imols[k])) continue;

            std::string status = generate_one(imols[k], wanted[i]);
            const std::string where =
               wanted[i] + " (molecule " + coot::util::int_to_string(imols[k]) + ")";

            if (status == "ok") {
               any_ok = true;
            } else if (status.compare(0, 5, "warn:") == 0) {
               any_ok = true;
               warned.push_back(where + ": " + status.substr(5));
            } else if (status.compare(0, 5, "fail:") == 0) {
               failed.push_back(where + ": " + status.substr(5));
            } else {
               // No string came back at all: python is unavailable or the call
               // itself broke. Nothing useful beyond naming the component.
               failed.push_back(where + ": restraint generation did not run");
            }
            pump_events();
         }
         if (any_ok) generated++;

         pump_events();
      }

      set_busy(false);
      generation_running = false;

      // A new dictionary changes how the ligand is drawn.
      if (generated > 0) {
         graphics_info_t g;
         for (int imol=0; imol<graphics_info_t::n_molecules(); imol++)
            if (is_valid_model_molecule(imol))
               g.molecules[imol].make_bonds_type_checked();
         graphics_draw();
      }

      std::cout << "INFO:: restraint generation: " << generated << " generated, "
                << failed.size() << " failed, " << dropped
                << " no longer needed" << std::endl;

      // Terse in the dialog, detail on stdout -- which the generator has
      // already written. Success says nothing at all: the rows disappearing is
      // the report, and a dialog announcing that what was asked for happened is
      // noise.
      if (! failed.empty() || ! warned.empty()) {
         std::string m;
         if (! failed.empty()) {
            m += "Restraints could not be generated for:\n\n";
            for (unsigned int i=0; i<failed.size(); i++) {
               std::cout << "WARNING:: " << failed[i] << std::endl;
               m += "    " + failed[i] + "\n";
            }
         }
         if (! warned.empty()) {
            if (! m.empty()) m += "\n";
            m += "Generated, with a caveat:\n\n";
            for (unsigned int i=0; i<warned.size(); i++) {
               std::cout << "WARNING:: " << warned[i] << std::endl;
               m += "    " + warned[i] + "\n";
            }
         }
         info_dialog(m.c_str());
      }
   }


/* ------------------------------------------------------------------------ */
/*  Applying an imported restraints CIF to every molecule it fits           */
/* ------------------------------------------------------------------------ */

// WHY THIS EXISTS: per-molecule dictionaries broke "import your own restraints".
//
// Generated restraints are stored against their molecule. An imported CIF used
// to land at IMOL_ENC_ANY, and the lookup tries an exact-scope match FIRST --
// so for any molecule that had generated restraints, the user's own CIF was
// silently ignored. That is the founding requirement of this feature
// ("if user then reads in a set of custom restraints, they will naturally
// override the default ones"), broken by the scoping.
//
// Importing SCOPED to each molecule the CIF fits repairs both halves at once:
// it reaches every molecule rather than one, and it lands at the same scope as
// the generated dictionary, where mon_lib_add_chem_comp() supersedes on a new
// read number instead of losing to it.

// Which loaded molecules does this dictionary actually describe?
//
// Parsed into a THROWAWAY protein_geometry so the matching happens before
// anything touches the real store -- otherwise deciding where a dictionary
// belongs would require importing it first. The constructor only fills the
// metal-distance and non-auto-load tables, so a temporary is cheap.
std::vector<int> molecules_fitting_dictionary(const std::string &file_name) {

   std::vector<int> targets;

   coot::protein_geometry tmp;
   tmp.set_verbose(false);
   coot::read_refmac_mon_lib_info_t info =
      tmp.init_refmac_mon_lib(file_name, 0, coot::protein_geometry::IMOL_ENC_ANY);
   if (info.success <= 0) return targets;

   graphics_info_t g;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
      if (! is_valid_model_molecule(imol)) continue;
      mmdb::Manager *mol = g.molecules[imol].atom_sel.mol;
      if (! mol) continue;

      bool fits = false;
      for (unsigned int i=0; i<tmp.size() && ! fits; i++) {
         // Matched by ATOM NAMES and connectivity, not by comp id: the whole
         // point is to find the molecules this chemistry describes, and after
         // a placeholder rename the file's own name may match nothing.
         std::vector<std::string> hit =
            coot::comp_id_collision::comp_ids_matching_dictionary(mol, tmp[i].second);
         if (! hit.empty()) fits = true;
      }
      if (fits) targets.push_back(imol);
   }
   return targets;
}

// Transient molecule chooser for an import that fits several molecules.
//
// NOTE THE ROWS HERE ARE MOLECULES, which the notification dialog's rows
// deliberately never are. That rule exists because THAT dialog is long-lived
// and a molecule number goes stale in it. This one is modal and answered
// immediately -- a transaction, not a request -- so naming molecules is safe
// and is the only thing that would mean anything to the user.
std::vector<int> choose_molecules_for_dictionary(const std::string &file_name,
                                                 const std::vector<int> &targets) {

   std::vector<int> chosen;
   graphics_info_t g;

   GtkWidget *d =
      gtk_dialog_new_with_buttons("Apply Restraints", NULL, GTK_DIALOG_MODAL,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT,
                                  (char *) NULL);
   gtk_window_set_keep_above(GTK_WINDOW(d), TRUE);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(d));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

   std::string head = "More than one molecule compatible with these restraints found:\n";
   head += coot::util::file_name_non_directory(file_name);
   GtkWidget *label = gtk_label_new(head.c_str());
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);

   std::vector<GtkWidget *> boxes;
   for (unsigned int i=0; i<targets.size(); i++) {
      std::string t = coot::util::int_to_string(targets[i]) + "  "
                    + g.molecules[targets[i]].name_for_display_manager();
      GtkWidget *cb = gtk_check_button_new_with_label(t.c_str());
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb), TRUE);   // all checked
      gtk_box_pack_start(GTK_BOX(vbox), cb, FALSE, FALSE, 1);
      boxes.push_back(cb);
   }

   gtk_widget_show_all(d);
   if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
      for (unsigned int i=0; i<boxes.size(); i++)
         if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(boxes[i])))
            chosen.push_back(targets[i]);

   gtk_widget_destroy(d);
   return chosen;
}

   // ---- the dialog itself ------------------------------------------------

   void dialog_destroy(GtkObject *o, gpointer u) {
      show_all_mode = false;   // the mode belongs to the open dialog
      dialog = NULL;
      rows_vbox = NULL;
      generate_button = NULL;
      header_label = NULL;
   }

   void dialog_response(GtkDialog *d, gint response, gpointer u) {

      // Reentrancy: run_generation() pumps the main loop, so a response can
      // arrive while it is working. Nothing may destroy the dialog then.
      if (generation_running) return;

      if (response != GTK_RESPONSE_ACCEPT) {
         gtk_widget_destroy(GTK_WIDGET(d));
         return;
      }

      std::vector<std::string> wanted = checked_comp_ids();
      if (wanted.empty()) {
         info_dialog("No components are selected.");
         return;
      }

      run_generation(wanted);

      // Whatever is left still has no dictionary; if nothing is, the dialog has
      // nothing to say and should not sit there empty.
      if (rebuild_rows() == 0)
         gtk_widget_destroy(GTK_WIDGET(d));
   }

   gboolean rebuild_on_idle(gpointer u) {
      if (dialog && ! generation_running)
         if (rebuild_rows() == 0)
            gtk_widget_destroy(dialog);   // nothing left to offer
      return FALSE;                       // one shot
   }

   gboolean dialog_focus_in(GtkWidget *w, GdkEventFocus *e, gpointer u) {
      // Re-validate at the moment the user looks at it: a molecule deleted or a
      // dictionary imported since the last refresh loses its row here rather
      // than at OK.
      //
      // On an idle rather than inline, because the rebuild destroys every row
      // widget and one of them may be the widget this very focus event is
      // being delivered about.
      if (! generation_running)
         g_idle_add(rebuild_on_idle, NULL);
      return FALSE;
   }

   void build_dialog() {

      dialog = gtk_dialog_new_with_buttons("Ligand Restraints", NULL,
                                           (GtkDialogFlags) 0,
                                           GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                           (char *) NULL);
      // Above the GL window so it does not open behind it, but NOT transient
      // for it: on GTK-Quartz a transient parent glues the dialog to the main
      // window's move/minimize group, and this one is meant to be ignorable on
      // its own.
      gtk_window_set_keep_above(GTK_WINDOW(dialog), TRUE);
      gtk_window_set_default_size(GTK_WINDOW(dialog), 380, 260);

      GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
      gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

      // Doubles as the progress line during a run -- see set_busy().
      header_label = gtk_label_new(HEADER_IDLE);
      gtk_misc_set_alignment(GTK_MISC(header_label), 0.0, 0.5);
      gtk_box_pack_start(GTK_BOX(vbox), header_label, FALSE, FALSE, 4);

      GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
      gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                     GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
      gtk_box_pack_start(GTK_BOX(vbox), scrolled, TRUE, TRUE, 0);
      rows_vbox = gtk_vbox_new(FALSE, 0);
      gtk_container_set_border_width(GTK_CONTAINER(rows_vbox), 4);
      gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), rows_vbox);

      // WARNING: THE GENERATOR IS NOT LOOKED FOR HERE, AND THAT IS DELIBERATE.
      //
      // The obvious version of this dialog names the tool it would use, and
      // greys the button out when there is none. Both need the answer at BUILD
      // time -- which is during a coordinate load, and finding out can cost a
      // couple of seconds: when Bandicoot was started from the Dock the tools
      // are not on PATH and the lookup has to ask the user's login shell (see
      // python/bandicoot_restraints.py).
      //
      // Spending that on the load is exactly the thing this whole design
      // exists to avoid. Asked at the moment the user presses the button it
      // costs the same seconds inside an action they chose, which is the one
      // place the cost is acceptable. So the text below names no tool, the
      // button is always live, and "there is no generator" is reported as an
      // outcome of pressing it.
      GtkWidget *foot_label =
         gtk_label_new("Refinement cannot restrain them until a dictionary is\n"
                       "read in (File -> Import CIF dictionary...) or generated\n"
                       "here from the model geometry.");
      gtk_misc_set_alignment(GTK_MISC(foot_label), 0.0, 0.5);
      gtk_box_pack_start(GTK_BOX(vbox), foot_label, FALSE, FALSE, 6);

      generate_button =
         gtk_dialog_add_button(GTK_DIALOG(dialog), "Generate Restraints",
                               GTK_RESPONSE_ACCEPT);

      g_signal_connect(dialog, "response", G_CALLBACK(dialog_response), NULL);
      g_signal_connect(dialog, "destroy",  G_CALLBACK(dialog_destroy), NULL);
      g_signal_connect(dialog, "focus-in-event", G_CALLBACK(dialog_focus_in), NULL);
   }

   // announce_empty: say so when there is nothing to list. Right when the user
   // asked for the dialog, wrong on a load, where silence IS the good news.
   void show_dialog(bool announce_empty) {

      if (! graphics_info_t::use_graphics_interface_flag) return;

      bool is_new = (dialog == NULL);
      if (is_new)
         build_dialog();

      // Nothing to list: never open an empty dialog, and take a stale one away.
      if (rebuild_rows() == 0) {
         gtk_widget_destroy(dialog);      // the destroy handler nulls it
         if (announce_empty)
            info_dialog(show_all_mode
                        ? "No ligands found in the loaded molecules."
                        : "Every component in the loaded molecules has restraints.");
         return;
      }

      if (is_new)
         gtk_widget_show_all(dialog);
      else
         gtk_window_present(GTK_WINDOW(dialog));
   }
}

// Preferences -> Others -> Ligands. Declared here in the house style used for
// the other preference readers rather than dragging in the preferences header.
void bandicoot_load_ligand_behaviour(int *auto_rename, int *show_rename,
                                     int *auto_generate, int *show_generate,
                                     int *apply_all);


int bandicoot_import_restraints_sweep(const char *file_name_in,
                                      short int new_molecule_flag) {

   if (! file_name_in) return 0;
   const std::string file_name(file_name_in);

   std::vector<int> targets = molecules_fitting_dictionary(file_name);

   // Nothing loaded matches. Read it unscoped rather than dropping it: a
   // dictionary is often read AHEAD of its coordinates, and scoping it to a
   // molecule that does not have the component would put it out of reach of
   // the one that eventually does. This is what Auto already did.
   if (targets.empty()) {
      std::cout << "INFO:: no loaded molecule matches " << file_name
                << " - reading it for all molecules" << std::endl;
      return handle_cif_dictionary_for_molecule(file_name.c_str(),
                                                coot::protein_geometry::IMOL_ENC_ANY,
                                                new_molecule_flag);
   }

   // Exactly one match needs no question asked.
   if (targets.size() > 1) {
      int apply_all = 0;
      bandicoot_load_ligand_behaviour(NULL, NULL, NULL, NULL, &apply_all);
      if (! apply_all && graphics_info_t::use_graphics_interface_flag)
         targets = choose_molecules_for_dictionary(file_name, targets);
   }

   if (targets.empty()) {
      std::cout << "INFO:: restraints from " << file_name
                << " applied to no molecules, at the user's request" << std::endl;
      return 0;
   }

   int r = 0;
   for (unsigned int i=0; i<targets.size(); i++) {
      // new_molecule_flag on the FIRST only: the user asked for a molecule
      // from the dictionary, not one per molecule it happens to fit.
      short int mk = (i == 0) ? new_molecule_flag : 0;
      int n = handle_cif_dictionary_for_molecule(file_name.c_str(), targets[i], mk);
      if (n > 0) r = n;
      std::cout << "INFO:: restraints from " << file_name
                << " applied to molecule " << targets[i] << std::endl;
   }
   return r;
}

void bandicoot_restraints_notify(int imol, const std::vector<std::string> &comp_ids) {

   if (! graphics_info_t::use_graphics_interface_flag) return;
   if (comp_ids.empty()) return;

   // Preferences -> Others -> Ligands:
   //   auto_generate Yes -> generate now, no dialog
   //   auto_generate No, show_generate Yes -> the notification dialog (default)
   //   both No -> load quietly; the user generates from the Modelling menu
   //
   // NOTE the automatic path deliberately reintroduces the wait on the load
   // that the whole notify-don't-prompt design exists to avoid. That is fine
   // BECAUSE THE USER ASKED FOR IT in preferences: the cost is attached to an
   // action they chose, which is the actual content of the latency rule. There
   // is no progress dialog -- generation blocks, so the beach ball says "wait"
   // exactly as it does for Ligand from SMILES.
   {
      int auto_generate = 0, show_generate = 1;
      bandicoot_load_ligand_behaviour(NULL, NULL, &auto_generate, &show_generate, NULL);

      if (auto_generate) {
         std::cout << "INFO:: generating restraints without asking "
                   << "(Preferences -> Others -> Ligands)" << std::endl;
         run_generation(comp_ids);
         return;
      }
      if (! show_generate) {
         std::cout << "INFO:: not offering restraint generation (Preferences -> "
                   << "Others -> Ligands)" << std::endl;
         return;
      }
   }

   if (! graphics_info_t::show_ligand_restraint_warnings_flag) return;

   // A load that contributes a component the user had switched off gets it
   // back: the row is being offered again about a different file, and silently
   // keeping it unchecked would hide it.
   for (unsigned int i=0; i<comp_ids.size(); i++)
      unchecked.erase(comp_ids[i]);

   show_dialog(false);
}

void bandicoot_restraints_dialog() {

   if (! graphics_info_t::use_graphics_interface_flag) return;

   // Modelling -> Generate Ligand Restraints: EVERY ligand in EVERY loaded
   // molecule, labelled by file, with anything already described arriving
   // unticked. If a load-time dialog is already open showing only what is
   // missing, this switches that same window into show-all rather than opening
   // a second one.
   show_all_mode = true;
   show_dialog(true);
}

/* src/restraints-gui.cc
 *
 * Bandicoot v0.2: the load-time ligand restraints notification. See the header.
 *
 * WHY THIS NOTIFIES AND DOES NOT PROMPT
 *
 * Generating restraints takes seconds, not milliseconds. Attaching that to a
 * coordinate load would be wrong even if it were fast, because the cost a user
 * will accept is set by what they believe they asked for: the same wait is
 * unremarkable in "build me a ligand from SMILES" and infuriating in "open a
 * file". So the load never blocks and never asks. The coordinates appear at
 * normal speed and this dialog then sits there, ignorable indefinitely, until
 * the user decides to spend the time.
 *
 * The rejected alternative was a modal Yes/No on load. It still interrupts an
 * action the user thinks of as opening a file; it merely swaps a wait for an
 * unasked-for decision.
 *
 * WHY THE ROWS ARE COMPONENT IDS AND NEVER MOLECULE NUMBERS
 *
 * Restraints in Coot are GLOBAL and keyed by comp id -- one dictionary for
 * "LIG", whichever molecules contain it. A row that named a molecule would go
 * stale the moment that molecule was deleted, which is the flaw in the
 * "fix nomenclature errors" dialog this one is otherwise modelled on: leave it
 * open long enough and it offers to fix molecules that no longer exist.
 *
 * Keying on comp ids removes the whole class of problem. A deleted molecule
 * cannot invalidate a row, because a row was never about a molecule.
 *
 * WHY THE CONTENTS ARE RECOMPUTED AND NEVER STORED
 *
 * The rows, the file names on them, and the decision to generate at all are
 * derived from live state every time they are needed -- on a new load, when
 * the dialog regains focus, and again inside the OK handler before anything
 * runs. Nothing about which components need restraints is remembered between
 * those moments.
 *
 * A long-lived non-modal dialog is a REQUEST, not a transaction: by the time
 * it is acted on, the user may have deleted the molecule, imported a
 * dictionary by hand, or loaded three more files. Re-deriving is what makes
 * all of those harmless. The one thing carried across a rebuild is which rows
 * the user has UNCHECKED, because that is a decision of theirs and not a fact
 * about the molecules.
 *
 * WHY THE DIALOG IS NON-BLOCKING BUT THE GENERATION IS NOT
 *
 * Real-space refinement started while a generator is halfway through writing
 * that ligand's dictionary would be entertaining and useless. So generation
 * blocks: the main loop stops, the draw window goes unresponsive, and the
 * cursor becomes the spinning beach ball, which is the platform's own way of
 * saying "wait".
 *
 * The dialog itself goes modal for the duration and reports progress in its
 * own header. It does NOT open a second window to do that -- see the note on
 * set_busy() for why, and for the reason the modality is the half that must
 * not be dropped.
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

#include "utils/coot-utils.hh"          // coot::util::int_to_string()
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

   // Rows the user has switched OFF. Carried across a rebuild; everything else
   // about a row is recomputed. Comp ids, so this survives deletions too.
   std::set<std::string> unchecked;

   const char *COMP_ID_KEY = "bandicoot-comp-id";

   // One row: a component with no dictionary, and where it was found. imols and
   // names are strictly derived -- see the header comment.
   struct row_t {
      std::string comp_id;
      std::vector<int> imols;
      std::vector<std::string> names;
   };

   // Every component in every loaded molecule that still has no dictionary.
   //
   // no_dictionary_for_residue_type_as_yet() is the same question the load path
   // asks, after the same lookup ladder, so a component the user has since
   // supplied a dictionary for simply stops being listed. That is also why
   // nothing here needs to know about dictionary imports: it asks rather than
   // tracking.
   std::vector<row_t> live_rows() {

      graphics_info_t g;
      std::vector<row_t> rows;

      for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) {
         if (! is_valid_model_molecule(imol)) continue;

         std::vector<std::string> types =
            g.molecules[imol].no_dictionary_for_residue_type_as_yet(*g.Geom_p());

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

   // WARNING: BARE NAMES, NOT "bandicoot_restraints.something()".
   //
   // safe_python_command_with_return() evaluates in __main__'s globals, and
   // coot_load_modules.py.in does not IMPORT the python files -- it compiles
   // and exec()s each one into those same globals. So there is no module
   // object called bandicoot_restraints to reach through; its functions are
   // already top-level names here. (That is also why the module may use bare
   // coot scripting calls at all: it is running inside the namespace that did
   // "from coot import *".)

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

      std::vector<row_t> rows = live_rows();
      for (unsigned int i=0; i<rows.size(); i++) {
         GtkWidget *cb = gtk_check_button_new_with_label(row_label(rows[i]).c_str());
         g_object_set_data_full(G_OBJECT(cb), COMP_ID_KEY,
                                g_strdup(rows[i].comp_id.c_str()), g_free);
         gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb),
                                      unchecked.find(rows[i].comp_id) == unchecked.end());
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

   // WARNING: PROGRESS IS REPORTED IN THIS DIALOG'S OWN HEADER, NOT IN A SECOND WINDOW.
   //
   // There WAS a separate modal progress window naming the component and its
   // position, with a Cancel that was honoured between components. Art removed
   // it after testing (2026-09-01): it opened tiny in the corner of a large
   // monitor where he could barely see it, and it was not telling him anything
   // the spinning-beach-ball cursor and the dead draw window had not already
   // said. His comparison was "Ligand from SMILES", which waits far longer with
   // no progress window at all and is perfectly clear.
   //
   // The dialog the user is already looking at says it instead. Cancellation
   // goes with the window -- his ruling, since a run is usually one component
   // and each takes a few seconds.
   //
   // WARNING: BUT THE MODALITY MUST NOT GO WITH IT, and this is the subtle part.
   // pump_events() below is what lets the message repaint between components,
   // and it also DELIVERS whatever the user clicked during the previous one.
   // The progress window used to absorb those clicks. Without it, a click made
   // during component 1 of 3 would reach the main window mid-run -- which is
   // exactly the "user runs RSR while the generator is halfway through" case
   // that made generation blocking in the first place. So this dialog goes
   // modal for the duration, with its own buttons insensitive, and the clicks
   // land on a window that ignores them.
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

         // RE-VALIDATE, one component at a time and immediately before running.
         // The list was read off widgets that may have been sitting there for
         // an hour, and the previous component's dictionary may even have
         // covered this one.
         std::vector<row_t> rows = live_rows();
         int imol = -1;
         for (unsigned int j=0; j<rows.size(); j++)
            if (rows[j].comp_id == wanted[i] && ! rows[j].imols.empty()) {
               imol = rows[j].imols[0];
               break;
            }
         if (imol < 0) {
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

         std::string status = generate_one(imol, wanted[i]);

         if (status == "ok") {
            generated++;
         } else if (status.compare(0, 5, "warn:") == 0) {
            generated++;
            warned.push_back(wanted[i] + ": " + status.substr(5));
         } else if (status.compare(0, 5, "fail:") == 0) {
            failed.push_back(wanted[i] + ": " + status.substr(5));
         } else {
            // No string came back at all: python is unavailable or the call
            // itself broke. Nothing useful to say beyond naming the component.
            failed.push_back(wanted[i] + ": restraint generation did not run");
         }

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

   // ---- the dialog itself ------------------------------------------------

   void dialog_destroy(GtkObject *o, gpointer u) {
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
            info_dialog("Every component in the loaded molecules has restraints.");
         return;
      }

      if (is_new)
         gtk_widget_show_all(dialog);
      else
         gtk_window_present(GTK_WINDOW(dialog));
   }
}

void bandicoot_restraints_notify(int imol, const std::vector<std::string> &comp_ids) {

   if (! graphics_info_t::use_graphics_interface_flag) return;
   if (! graphics_info_t::show_ligand_restraint_warnings_flag) return;
   if (comp_ids.empty()) return;

   // A load that contributes a component the user had switched off gets it
   // back: the row is being offered again about a different file, and silently
   // keeping it unchecked would hide it.
   for (unsigned int i=0; i<comp_ids.size(); i++)
      unchecked.erase(comp_ids[i]);

   show_dialog(false);
}

void bandicoot_restraints_dialog() {

   if (! graphics_info_t::use_graphics_interface_flag) return;
   show_dialog(true);
}

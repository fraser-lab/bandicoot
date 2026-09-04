/* src/save-coords-gui.cc
 *
 * Bandicoot v0.2: the GTK2 half of the Save Coordinates dialog. See the header.
 *
 * WHY THE FORMAT MENU IS BUILT IN CODE AND NOT IN GLADE
 *
 * Two reasons, and the second is the one that matters. The small one: adding it
 * to coot-gtk2.glade means hand-editing 38k lines of XML. The real one: glade
 * XML is GTK2-only and carries none of the design across to another toolkit,
 * whereas this file is a short, readable statement of the same behaviour that a
 * wx port can be written against.
 *
 * WHY THERE IS ONE CHOOSER AND NOT TWO
 *
 * "Save Coordinates" and "Save Symmetry Coordinates" were separate glade
 * dialogs. They are the same dialog with one extra piece of state, and keeping
 * them apart is why the symmetry save silently lacked the Save Hydrogens and
 * Save ANISO Records options for years. One factory now serves both.
 *
 * THE CHECKBOXES ARE THE GLADE ONES, NOT OURS
 *
 * save_coords_filechooserdialog1 already contains "Save Hydrogens" and "Save
 * ANISO Records" in hbox413. An earlier version of this file built a second
 * pair, which was rightly reported as a duplicated set. We read the originals
 * and pack the format menu into that same row.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#include <Python.h>

#include "compat/coot-sysdep.h"

#include <iostream>
#include <string>
#include <vector>

#include "save-coords-gui.hh"

#include "interface.h"        // create_save_coords_filechooserdialog1() etc
#include "support.h"          // lookup_widget()
#include "graphics-info.h"
#include "c-interface.h"
#include "c-interface-gtk-widgets.h"

namespace {

   const char *OPTIONS_KEY = "coot-save-coords-options";
   const char *FMT_KEY     = "coot-combo-format";
   const char *NAME_KEY    = "coot-save-coords-basename";

   void free_options(gpointer data) {
      delete static_cast<coot::save_coords_options_t *>(data);
   }

   coot::save_coords_options_t *opts_of(GtkWidget *chooser) {
      return static_cast<coot::save_coords_options_t *>
         (g_object_get_data(G_OBJECT(chooser), OPTIONS_KEY));
   }

   // The format currently selected in the menu (falling back to the stored one).
   coot::coord_file_format_t current_format(GtkWidget *chooser) {

      coot::save_coords_options_t *o = opts_of(chooser);
      coot::coord_file_format_t f = o ? o->format : coot::coord_file_format_t::PDB;

      GtkWidget *combo = GTK_WIDGET(g_object_get_data(G_OBJECT(chooser), FMT_KEY));
      if (combo) {
         int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
         const std::vector<std::pair<coot::coord_file_format_t, std::string> > e =
            coot::save_coords_options_t::menu_entries(o ? o->is_symmetry : false);
         if (idx >= 0 && idx < static_cast<int>(e.size()))
            f = e[idx].first;
      }
      return f;
   }

   // Remember the basename we last put in the entry.
   //
   // GTK2 has no gtk_file_chooser_get_current_name() -- that arrived in GTK
   // 3.10 -- so after the user types, the only way to read the name back is
   // gtk_file_chooser_get_filename(), which returns NULL until the name
   // resolves against a folder. The stored copy is the fallback for that gap.
   void remember_basename(GtkWidget *chooser, const std::string &basename) {
      g_object_set_data_full(G_OBJECT(chooser), NAME_KEY,
                             g_strdup(basename.c_str()), g_free);
   }

   std::string recall_basename(GtkWidget *chooser) {
      gpointer p = g_object_get_data(G_OBJECT(chooser), NAME_KEY);
      return p ? std::string(static_cast<const char *>(p)) : std::string();
   }

   std::string current_basename(GtkWidget *chooser) {

      if (graphics_info_t::gtk2_file_chooser_selector_flag == coot::CHOOSER_STYLE) {
         gchar *full = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
         if (full) {
            gchar *base = g_path_get_basename(full);
            std::string s(base ? base : "");
            g_free(base);
            g_free(full);
            if (! s.empty())
               return s;
         }
      }
      return recall_basename(chooser);
   }

   // Show only files matching the chosen format (plus an All Files escape).
   void apply_filter_for_format(GtkWidget *chooser, coot::coord_file_format_t f) {

      if (graphics_info_t::gtk2_file_chooser_selector_flag != coot::CHOOSER_STYLE)
         return;

      char key[64];
      g_snprintf(key, sizeof(key), "coot-filter-%d", static_cast<int>(f));
      gpointer p = g_object_get_data(G_OBJECT(chooser), key);
      if (p)
         gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), GTK_FILE_FILTER(p));
   }

   void build_filters(GtkWidget *chooser, bool is_symmetry) {

      if (graphics_info_t::gtk2_file_chooser_selector_flag != coot::CHOOSER_STYLE)
         return;

      const std::vector<std::pair<coot::coord_file_format_t, std::string> > entries =
         coot::save_coords_options_t::menu_entries(is_symmetry);

      for (unsigned int i = 0; i < entries.size(); i++) {
         const coot::coord_file_format_t f = entries[i].first;
         GtkFileFilter *filt = gtk_file_filter_new();
         gtk_file_filter_set_name(filt,
            coot::save_coords_options_t::filter_label_for(f).c_str());
         const std::vector<std::string> pats =
            coot::save_coords_options_t::filename_patterns_for(f);
         for (unsigned int j = 0; j < pats.size(); j++)
            gtk_file_filter_add_pattern(filt, pats[j].c_str());
         // add_filter sinks the floating ref; the chooser owns it from here.
         gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filt);
         char key[64];
         g_snprintf(key, sizeof(key), "coot-filter-%d", static_cast<int>(f));
         g_object_set_data(G_OBJECT(chooser), key, filt);
      }

      GtkFileFilter *all = gtk_file_filter_new();
      gtk_file_filter_set_name(all, "All Files");
      gtk_file_filter_add_pattern(all, "*");
      gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), all);
   }

   // Make the name in the entry agree with the chosen format, live.
   //
   // Without this the entry keeps saying "saved.cif" while the menu says PDB,
   // and the file lands as saved.pdb -- which also makes the overwrite
   // confirmation fire against the wrong name. All three were reported as one
   // problem, and they are.
   void sync_name_to_format(GtkWidget *chooser) {

      if (graphics_info_t::gtk2_file_chooser_selector_flag != coot::CHOOSER_STYLE)
         return;

      const std::string base = current_basename(chooser);
      if (base.empty())
         return;

      const coot::coord_file_format_t f = current_format(chooser);
      const std::string want =
         coot::save_coords_options_t::apply_format_to_filename(base, f);

      if (want != base)
         gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), want.c_str());
      remember_basename(chooser, want);
   }

   void on_format_changed(GtkComboBox *, gpointer user_data) {
      GtkWidget *chooser = GTK_WIDGET(user_data);
      sync_name_to_format(chooser);
      apply_filter_for_format(chooser, current_format(chooser));
   }

   // Label + menu, packed into the glade row that already holds the two
   // checkboxes so the dialog gets one options row rather than two.
   void attach_format_menu(GtkWidget *chooser,
                           const coot::save_coords_options_t &initial) {

      GtkWidget *lab = gtk_label_new("Format:");
      GtkWidget *combo = gtk_combo_box_new_text();

      const std::vector<std::pair<coot::coord_file_format_t, std::string> > entries =
         coot::save_coords_options_t::menu_entries(initial.is_symmetry);
      int active = 0;
      for (unsigned int i = 0; i < entries.size(); i++) {
         gtk_combo_box_append_text(GTK_COMBO_BOX(combo), entries[i].second.c_str());
         if (entries[i].first == initial.format)
            active = static_cast<int>(i);
      }
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);

      g_object_set_data(G_OBJECT(chooser), FMT_KEY, combo);

      GtkWidget *row = lookup_widget(chooser, "hbox413");
      if (row) {
         gtk_box_pack_end(GTK_BOX(row), combo, FALSE, FALSE, 4);
         gtk_box_pack_end(GTK_BOX(row), lab,   FALSE, FALSE, 0);

         // Put "Format:" to the RIGHT of the two checkboxes.
         //
         // Both glade checkbuttons are GTK_PACK_END, and a pack-end box lays its
         // children out from the right edge in the order they appear in the
         // child list -- so anything packed later lands FURTHER LEFT. Packing
         // alone therefore put the menu to the left of the checkboxes, which is
         // what was seen and called funky (2026-08-26).
         //
         // gtk_box_reorder_child() moves a child within that list, and for
         // pack-end children earlier in the list means nearer the right edge.
         // combo at 0 and label at 1 gives, left to right:
         //   Save ANISO Records | Save Hydrogens | Format: [menu]
         gtk_box_reorder_child(GTK_BOX(row), combo, 0);
         gtk_box_reorder_child(GTK_BOX(row), lab,   1);

         gtk_widget_show(lab);
         gtk_widget_show(combo);
      } else {
         // hbox413 is where the glade dialog keeps Save Hydrogens / Save ANISO
         // Records. If that ever moves, fall back to the extra-widget slot
         // rather than losing the menu entirely.
         GtkWidget *box = gtk_hbox_new(FALSE, 6);
         gtk_box_pack_end(GTK_BOX(box), combo, FALSE, FALSE, 4);
         gtk_box_pack_end(GTK_BOX(box), lab,   FALSE, FALSE, 0);
         gtk_widget_show_all(box);
         if (graphics_info_t::gtk2_file_chooser_selector_flag == coot::CHOOSER_STYLE)
            gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(chooser), box);
         else
            gtk_box_pack_start(GTK_BOX(GTK_FILE_SELECTION(chooser)->main_vbox),
                               box, FALSE, FALSE, 0);
      }

      g_signal_connect(G_OBJECT(combo), "changed",
                       G_CALLBACK(on_format_changed), chooser);
   }
}


GtkWidget *coot_save_coords_chooser_new(const coot::save_coords_options_t &initial) {

   GtkWidget *w;

   // ONE glade dialog for both uses. save_symmetry_coords_filechooserdialog1
   // and save_symmetry_coords_fileselection are no longer created by anything.
   if (graphics_info_t::gtk2_file_chooser_selector_flag == coot::OLD_STYLE) {
      w = create_save_coords_fileselection1();
   } else {
      w = create_save_coords_filechooserdialog1();
      gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(w), TRUE);
   }

   coot::save_coords_options_t *opts = new coot::save_coords_options_t(initial);
   g_object_set_data_full(G_OBJECT(w), OPTIONS_KEY, opts, free_options);

   build_filters(w, initial.is_symmetry);
   attach_format_menu(w, initial);
   apply_filter_for_format(w, initial.format);

   return w;
}


void coot_save_coords_chooser_set_name(GtkWidget *chooser, const char *basename) {

   if (! basename) return;
   const coot::coord_file_format_t f = current_format(chooser);
   const std::string want =
      coot::save_coords_options_t::apply_format_to_filename(std::string(basename), f);
   if (graphics_info_t::gtk2_file_chooser_selector_flag == coot::CHOOSER_STYLE)
      gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), want.c_str());
   remember_basename(chooser, want);
}


void coot_save_coords_chooser_sync_name(GtkWidget *chooser) {
   sync_name_to_format(chooser);
}


int coot_save_coords_chooser_execute(GtkWidget *chooser) {

   coot::save_coords_options_t *stored = opts_of(chooser);
   if (! stored) {
      std::cout << "ERROR:: save: no options attached to the chooser" << std::endl;
      return 1;
   }
   coot::save_coords_options_t opts = *stored;

   // The two checkboxes are the glade dialog's own, in hbox413.
   GtkWidget *chk_hyd = lookup_widget(chooser, "checkbutton_hydrogens");
   GtkWidget *chk_ani = lookup_widget(chooser, "checkbutton_aniso");
   if (chk_hyd) opts.hydrogens = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_hyd));
   if (chk_ani) opts.aniso     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_ani));

   opts.format = current_format(chooser);
   opts.conect = graphics_info_t::write_conect_records_flag;

   const gchar *fn;
   if (graphics_info_t::gtk2_file_chooser_selector_flag == coot::CHOOSER_STYLE)
      fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
   else
      fn = gtk_file_selection_get_filename(GTK_FILE_SELECTION(chooser));

   if (! fn) {
      std::cout << "ERROR:: save: no filename given" << std::endl;
      return 1;
   }

   // Belt and braces. The entry is kept in step with the menu live, so this
   // should already agree; it stays because apply_format_to_filename() is what
   // actually decides, and a silent disagreement here is the bug class the menu
   // was added to remove.
   const std::string typed(fn);
   const std::string filename =
      coot::save_coords_options_t::apply_format_to_filename(typed, opts.format);

   if (filename != typed)
      std::cout << "INFO:: adjusting " << typed << " to " << filename
                << " to match the chosen format" << std::endl;

   return opts.execute(filename);
}


// ---- C entry points, for callbacks.c ----------------------------------------

extern "C" GtkWidget *coot_save_coords_chooser_for_molecule(int imol) {

   coot::save_coords_options_t opts;
   opts.imol        = imol;
   opts.is_symmetry = false;

   // Default the menu to the format the molecule came IN as. Same reasoning as
   // the backup-format policy (2026-08-12): a user working in mmCIF who
   // saves without looking at the menu should not be silently downgraded to PDB
   // and lose the metadata the retained document is carrying.
   if (graphics_info_t::is_valid_model_molecule(imol))
      opts.format = graphics_info_t::molecules[imol].get_input_molecule_was_in_mmcif_state()
                       ? coot::coord_file_format_t::MMCIF
                       : coot::coord_file_format_t::PDB;

   return coot_save_coords_chooser_new(opts);
}

extern "C" int coot_save_coords_chooser_execute_widget(GtkWidget *chooser) {
   return coot_save_coords_chooser_execute(chooser);
}

extern "C" void coot_save_coords_chooser_sync_name_widget(GtkWidget *chooser) {
   coot_save_coords_chooser_sync_name(chooser);
}

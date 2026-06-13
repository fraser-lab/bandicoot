/* src/c-interface-build-gui.cc
 * 
 * Copyright 2002, 2003, 2004, 2005, 2006, 2007, 2008 The University of York
 * Author: Paul Emsley
 * Copyright 2007 by Paul Emsley
 * Copyright 2007,2008, 2009 by Bernhard Lohkamp
 * Copyright 2008 by Kevin Cowtan
 * Copyright 2007, 2008, 2009 The University of Oxford
 * Copyright 2015 by Medical Research Council
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#ifdef USE_PYTHON
#include <Python.h>  // before system includes to stop "POSIX_C_SOURCE" redefined problems
#endif

#include "compat/coot-sysdep.h"

#include <stdlib.h>
#include <iostream>

#define HAVE_CIF  // will become unnessary at some stage.

#include <sys/types.h> // for stating
#include <sys/stat.h>
#if !defined _MSC_VER
#include <unistd.h>
#else
#include <windows.h>
#endif
 
#include "globjects.h" //includes gtk/gtk.h

#include "callbacks.h"
#include "interface.h" // now that we are moving callback
		       // functionality to the file, we need this
		       // header since some of the callbacks call
		       // fuctions built by glade.

#include <vector>
#include <string>

#include <mmdb2/mmdb_manager.h>
#include "coords/mmdb-extras.h"
#include "coords/mmdb.h"
#include "coords/mmdb-crystal.h"

#include "coords/Cartesian.h"
#include "coords/Bond_lines.h"

#include "graphics-info.h"

#include "coot-utils/coot-coord-utils.hh"
#include "utils/coot-fasta.hh"

#include "skeleton/BuildCas.h"
#include "ligand/helix-placement.hh"
#include "ligand/fast-ss-search.hh"

#include "trackball.h" // adding exportable rotate interface

#include "utils/coot-utils.hh"  // for is_member_p
#include "coot-utils/coot-map-heavy.hh"  // for fffear

#include "guile-fixups.h"

// Including python needs to come after graphics-info.h, because
// something in Python.h (2.4 - chihiro) is redefining FF1 (in
// ssm_superpose.h) to be 0x00004000 (Grrr).
//
// 20100813: but in order that we do not get error: "_POSIX_C_SOURCE"
// redefined problems, we should include python at the
// beginning. Double grr!
//
// #ifdef USE_PYTHON
// #include "Python.h"
// #endif // USE_PYTHON


#include "c-interface.h"
#include "c-interface-gtk-widgets.h"
#include "cc-interface.hh"
#include "cc-interface-scripting.hh"
#include "c-interface-ligands-swig.hh"  // coot_reduce, invert_chiral_centre (Modelling menu)
#include "rotamer-search-modes.hh"   // ROTAMERSEARCHLOWRES / HIGHRES (Modelling menu)

#include "ligand/ligand.hh" // for rigid body fit by atom selection.

#include "cmtz-interface.hh" // for valid columns mtz_column_types_info_t
#include "c-interface-mmdb.hh"
#include "c-interface-scm.hh"
#include "c-interface-python.hh"

#ifdef USE_DUNBRACK_ROTAMERS
#include "ligand/dunbrack.hh"
#else 
#include "ligand/richardson-rotamer.hh"
#endif 

void do_regularize_kill_delete_dialog() {
   graphics_info_t g;
   if (g.delete_item_widget) { 
      gtk_widget_destroy(g.delete_item_widget);
      g.delete_item_widget = NULL;
      // hopefully superfluous:
      g.delete_item_atom = 0;
      g.delete_item_residue = 0;
      g.delete_item_residue_hydrogens = 0;
   }
}
   

/* moving gtk functionn out of build functions, delete_atom() updates
   the go to atom atom list on deleting an atom  */
void update_go_to_atom_residue_list(int imol) {

   graphics_info_t g;
      if (g.go_to_atom_window) {
	 int go_to_atom_imol = g.go_to_atom_molecule();
	 if (go_to_atom_imol == imol) { 

	    // The go to atom molecule matched this molecule, so we
	    // need to regenerate the residue and atom lists.
	    GtkWidget *gtktree = lookup_widget(g.go_to_atom_window,
					       "go_to_atom_residue_tree");
	    GtkWidget *gtk_atom_list = lookup_widget(g.go_to_atom_window,
						     "go_to_atom_atom_list");
	    g.fill_go_to_atom_residue_tree_and_atom_list_gtk2(imol, gtktree, gtk_atom_list);
	 } 
      }
}

/* utility function, moving widget work out of c-interface-build.cc */
void delete_object_handle_delete_dialog(short int do_delete_dialog) {
   if (graphics_info_t::delete_item_widget != NULL) {
      if (do_delete_dialog) { // via ctrl

	 // another check is needed, is the check button active?
	 // 
	 // If not we can go ahead and delete the dialog
	 //
	 GtkWidget *checkbutton = lookup_widget(graphics_info_t::delete_item_widget,
						"delete_item_keep_active_checkbutton");
	 if (GTK_TOGGLE_BUTTON(checkbutton)->active) {
	    // don't kill the widget
	    pick_cursor_maybe(); // it was set to normal_cursor() in
                                 // graphics-info-define's delete_item().
	 } else {
	 
	    gint upositionx, upositiony;
	    gdk_window_get_root_origin (graphics_info_t::delete_item_widget->window,
					&upositionx, &upositiony);
	    graphics_info_t::delete_item_widget_x_position = upositionx;
	    graphics_info_t::delete_item_widget_y_position = upositiony;
	    gtk_widget_destroy(graphics_info_t::delete_item_widget);
	    graphics_info_t::delete_item_widget = NULL;
	    graphics_draw();
	 }
      }
   }
}

void
post_delete_item_dialog() {

   GtkWidget *w = wrapped_create_delete_item_dialog();
   gtk_widget_show(w);

}




/* We need to set the pending delete flag and that can't be done in
   callback, so this wrapper does it */
GtkWidget *wrapped_create_delete_item_dialog() {

   GtkWidget *widget = create_delete_item_dialog();
   GtkWidget *atom_toggle_button;

   if (delete_item_mode_is_atom_p()) { 
      atom_toggle_button = lookup_widget(GTK_WIDGET(widget),
					 "delete_item_atom_radiobutton");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(atom_toggle_button), TRUE);
      std::cout << "Click on the atom that you wish to delete\n";
   } else {
      if (delete_item_mode_is_water_p()) {
	 GtkWidget *water_toggle_button = lookup_widget(widget,
							"delete_item_water_radiobutton");
	 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(water_toggle_button), TRUE);
      } else { 
	 if (delete_item_mode_is_sidechain_p()) {
	 GtkWidget *sidechain_toggle_button = lookup_widget(widget,
							"delete_item_sidechain_radiobutton");
	 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sidechain_toggle_button), TRUE);

	 set_delete_sidechain_mode();
	 std::cout << "Click on an atom in the residue that you wish to delete\n";
	 } else {
	    if (delete_item_mode_is_chain_p()) {
	       GtkWidget *chain_toggle_button = lookup_widget(widget,
								  "delete_item_chain_radiobutton");
	       set_delete_chain_mode();
	       gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chain_toggle_button), TRUE);
	       std::cout << "Click on an atom in the chain that you wish to delete\n";
	    } else {

	       if (delete_item_mode_is_sidechain_range_p()) {

		  GtkWidget *sidechain_range_toggle_button = lookup_widget(widget,
									   "delete_item_sidechain_range_radiobutton");
		  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sidechain_range_toggle_button), TRUE);

		  set_delete_sidechain_range_mode();
	       } else {

		  // if (delete_item_mode_is_residue_p()) {
		  // if nothing else, let's choose delete residue mode
		  if (true) {
		     GtkWidget *chain_toggle_button = lookup_widget(widget,
								    "delete_item_residue_radiobutton");
		     set_delete_residue_mode();
		     gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chain_toggle_button), TRUE);
		  }
	       }
	    }
	 }
      }
   }
   graphics_info_t::pick_pending_flag = 1;
   pick_cursor_maybe();
   set_transient_and_position(COOT_DELETE_WINDOW, widget);
   store_delete_item_widget(widget);
   return widget; 
}

// -----------------------------------------------------
//  move molecule here widget
// -----------------------------------------------------
GtkWidget *wrapped_create_move_molecule_here_dialog() {

   GtkWidget *w = create_move_molecule_here_dialog();
   fill_move_molecule_here_dialog(w);
   return w;
}

// called (also) by the callback of toggling the
// move_molecule_here_big_molecules_checkbutton.
// 
void
fill_move_molecule_here_dialog(GtkWidget *w) {

   // GtkWidget *option_menu  = lookup_widget(w, "move_molecule_here_optionmenu");

   graphics_info_t g;

   GtkWidget *combobox  = lookup_widget(w, "move_molecule_here_combobox");
   GtkWidget *check_button = lookup_widget(w, "move_molecule_here_big_molecules_checkbutton");
   gtk_widget_hide(check_button);

   // GtkSignalFunc callback_func = GTK_SIGNAL_FUNC(graphics_info_t::move_molecule_here_item_select);

   GCallback callback_func = G_CALLBACK(graphics_info_t::move_molecule_here_combobox_changed);

   bool fill_with_small_molecule_only_flag = false;
   int imol_active = first_coords_imol();

   g.move_molecule_here_molecule_number = imol_active;
   g.fill_combobox_with_coordinates_options(combobox, callback_func, imol_active);

}


void move_molecule_here_by_widget(GtkWidget *w) {

   int imol = graphics_info_t::move_molecule_here_molecule_number;
   move_molecule_to_screen_centre_internal(imol);
   std::vector<std::string> command_strings;
   command_strings.push_back("move-molecule-here");
   command_strings.push_back(clipper::String(imol));
   add_to_history(command_strings);
}


void fill_place_atom_molecule_combobox(GtkWidget *combobox) {

   graphics_info_t g;
   GCallback callback_func = G_CALLBACK(g.pointer_atom_molecule_combobox_changed);
   int imol_active = g.user_pointer_atom_molecule;
   if (! is_valid_model_molecule(imol_active))
      imol_active = first_coords_imol();
   g.fill_combobox_with_coordinates_options(combobox, callback_func, imol_active);

}


/* Now the refinement weight can be set from an entry in the refine_params_dialog. */
void set_refinement_weight_from_entry(GtkWidget *entry) {

   const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
   try {
      float f = coot::util::string_to_float(text);
      graphics_info_t::geometry_vs_map_weight = f;
   }
   catch (const std::runtime_error &rte) {
      std::cout << "in set_refinemenent_weight_from_entry " << rte.what() << std::endl;
   } 
}

void add_estimated_map_weight_to_entry(GtkWidget *entry) {

   int imol_map = imol_refinement_map();
   if (is_valid_map_molecule(imol_map)) {
      float v = estimate_map_weight(imol_map);
      graphics_info_t::geometry_vs_map_weight = v;
      std::string t = coot::util::float_to_string(v);
      gtk_entry_set_text(GTK_ENTRY(entry), t.c_str());
   }

}

float estimate_map_weight(int imol_map) {

   graphics_info_t g;
   float w = g.get_estimated_map_weight(imol_map);
   return w;
}


void place_atom_at_pointer_by_window() { 

   // put up a widget which has a OK callback button which does a 
   // g.place_typed_atom_at_pointer();
   GtkWidget *window = create_pointer_atom_type_dialog();


   //   GtkSignalFunc callback_func =
   // 	GTK_SIGNAL_FUNC(graphics_info_t::pointer_atom_molecule_menu_item_activate);
   
   // GtkWidget *optionmenu = lookup_widget(window, "pointer_atom_molecule_optionmenu");
   //   fill_place_atom_molecule_option_menu(optionmenu);

   GtkWidget *combobox = lookup_widget(window, "pointer_atom_molecule_combobox");
   fill_place_atom_molecule_combobox(combobox);
   gtk_widget_show(window);

}


// User data has been placed in the window - we use it to get the
// molecule number.
void baton_mode_calculate_skeleton(GtkWidget *window) {

   int imol = -1;

   int *i;

   std::cout << "getting intermediate data in baton_mode_calculate_skeleton "
	     << std::endl;
   i = (int *) gtk_object_get_user_data(GTK_OBJECT(window));

   std::cout << "got intermediate int: " << i << " " << *i << std::endl;

   imol = *i;

   std::cout << "calculating map for molecule " << imol << std::endl;
   if (imol < graphics_info_t::n_molecules() && imol >= 0) { 
      skeletonize_map(imol, 0);
   }
}


GtkWidget *wrapped_create_renumber_residue_range_dialog() {

   GtkWidget *w = create_renumber_residue_range_dialog();
   int imol = first_coords_imol();
   graphics_info_t::renumber_residue_range_molecule = imol;
   if (is_valid_model_molecule(imol)) {
      graphics_info_t g;
      g.fill_renumber_residue_range_dialog(w);  // fills the coordinates option menu
      g.fill_renumber_residue_range_internal(w, imol); // fills the chain option menu

      // by default, now the N-term button is off for the first choice
      // (and C-term is on for the second)
      GtkWidget *entry_1 = lookup_widget(GTK_WIDGET(w), "renumber_residue_range_resno_1_entry");
      GtkWidget *entry_2 = lookup_widget(GTK_WIDGET(w), "renumber_residue_range_resno_2_entry");
      gtk_widget_set_sensitive(entry_2, FALSE);
      // but anyway, let's put the residue number of the active residue there, just in case
      // the user wanted to start from there.
      std::pair<bool, std::pair<int, coot::atom_spec_t> > pp = active_atom_spec();
      if (pp.first) {
	 int res_no = pp.second.second.res_no;
	 gtk_entry_set_text(GTK_ENTRY(entry_1), coot::util::int_to_string(res_no).c_str());
      }
   }
   return w;
}

void renumber_residues_from_widget(GtkWidget *window) {

   int imol = graphics_info_t::renumber_residue_range_molecule;

   if (is_valid_model_molecule(imol)) {

      GtkWidget *e1 = lookup_widget(window, "renumber_residue_range_resno_1_entry");
      GtkWidget *e2 = lookup_widget(window, "renumber_residue_range_resno_2_entry");
      GtkWidget *offent = lookup_widget(window, "renumber_residue_range_offset_entry");
      GtkWidget *rb1 = lookup_widget(window, "renumber_residue_range_radiobutton_1"); // N-term button
      GtkWidget *rb4 = lookup_widget(window, "renumber_residue_range_radiobutton_4"); // C-term button

      std::pair<short int, int> r1  = int_from_entry(e1);
      std::pair<short int, int> r2  = int_from_entry(e2);
      std::pair<short int, int> off = int_from_entry(offent);

      std::string chain_id = graphics_info_t::renumber_residue_range_chain;
      mmdb::Chain *chain_p = graphics_info_t::molecules[imol].get_chain(chain_id);

      if (chain_p) {
	 if (GTK_TOGGLE_BUTTON(rb1)->active) {
	    // use N-terminus of chain
	    std::pair<bool, int> nt_resno = coot::util::min_resno_in_chain(chain_p);
	    if (nt_resno.first) {
	       r1.first = 1;
	       r1.second = nt_resno.second;
	    }
	 }

	 if (GTK_TOGGLE_BUTTON(rb4)->active) {
	    // use C-terminus of chain
	    std::pair<bool, int> ct_resno = coot::util::max_resno_in_chain(chain_p);
	    if (ct_resno.first) {
	       r2.first = 1;
	       r2.second = ct_resno.second;
	    }
	 }

	 if (r1.first && r2.first && off.first) {
	    int start_res = r1.second;
	    int last_res =  r2.second;
	    int offset   = off.second;

	    if (imol >= 0) {
	       if (imol < graphics_info_t::n_molecules()) {
		  if (graphics_info_t::molecules[imol].has_model()) {


		     // renumber_residue_range returns 0 upon fail
		     // including overlap, so test for this here?!
		     int status;
		     status = renumber_residue_range(imol, chain_id.c_str(), start_res, last_res, offset);
		     if (!status) {
			// error of sorts
			std::string s = "WARNING:: could not renumber residue range.\n";
			s += "Maybe your selection overlaps with existing residues.\n";
			s += "Please revise your selection.";
			info_dialog(s.c_str());
                 
		     }

		  }
	       }
	    }
	 } else {
	    std::cout << "WARNING:: Sorry. Couldn't read residue or offset from entry widget\n";
	 }
      } else {
	 std::cout << "ERROR:: missing chain" << chain_id << std::endl;
      }
   }
}



void apply_add_OXT_from_widget(GtkWidget *ok_button) {

   std::cout << "---------- apply_add_OXT_from_widget() " << ok_button << std::endl;

   GtkWidget *combobox = lookup_widget(ok_button, "add_OXT_molecule_combobox");

   int imol = my_combobox_get_imol(GTK_COMBO_BOX(combobox));

   std::cout << "combobox " << combobox << " imol " << imol << std::endl;
   int resno = -9999;
   std::string chain_id = graphics_info_t::add_OXT_chain;

   GtkWidget *terminal_checkbutton = lookup_widget(ok_button, "add_OXT_c_terminus_radiobutton");
   GtkWidget *residue_number_entry = lookup_widget(ok_button, "add_OXT_residue_entry");

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal_checkbutton))) {
      std::cout << "DEBUG:: auto determine C terminus for imol " << imol << std::endl;
      // we need to determine the last residue in this chain:
      if (is_valid_model_molecule(imol)) {
	 std::cout << "in apply_add_OXT_from_widget() here with chain_id :" << chain_id <<  ":" << std::endl;
	 graphics_info_t g;
	 std::pair<bool, int> p = g.molecules[imol].last_protein_residue_in_chain(chain_id);
	 std::cout << "here with last_residue_in_chain " << p.first << " " << p.second << std::endl;
	 if (p.first) {
	    resno = p.second;
	 } 
      }
   } else {
      // we get the resno from the widget
      std::pair<short int, int> p = int_from_entry(residue_number_entry);
      if (p.first) {
	 resno = p.second;
      }
   }

   if (resno > -9999) { 
      if (is_valid_model_molecule(imol)) { 
	 if (graphics_info_t::molecules[imol].has_model()) {
	    if (false)
	       std::cout << "DEBUG:: adding OXT to " << imol << " "
			 << chain_id << " " << resno << std::endl;
	    add_OXT_to_residue(imol, chain_id.c_str(), resno, "");
	 }
      }
   } else {
      std::cout << "WARNING:: Could not determine last residue - not adding OXT "
		<< imol << " " << resno << "\n";
   } 
}


GtkWidget *wrapped_create_add_OXT_dialog() {

   graphics_info_t g;

   GtkWidget *w = create_add_OXT_dialog();

   GtkWidget *combobox = lookup_widget(w, "add_OXT_molecule_combobox");
   GCallback callback_func = G_CALLBACK(g.add_OXT_molecule_combobox_changed);

   int imol = first_coords_imol();
   g.add_OXT_molecule = imol;

   if (combobox) {
      g.fill_combobox_with_coordinates_options(combobox, callback_func, imol);
      g.fill_add_OXT_dialog_internal(w, imol); // function needs object (not static)
   } else {
      std::cout << "bad combobox!" << std::endl;
   }

   return w;
}

void setup_alt_conf_with_dialog(GtkWidget *dialog) {

   GtkWidget *widget_ca = lookup_widget(dialog, 
					"add_alt_conf_ca_radiobutton");
   GtkWidget *widget_whole = lookup_widget(dialog, 
					   "add_alt_conf_whole_single_residue_radiobutton");
   GtkWidget *widget_range = lookup_widget(dialog, 
					   "add_alt_conf_residue_range_radiobutton");

   if (graphics_info_t::alt_conf_split_type_number() == 0)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_ca), TRUE);
   if (graphics_info_t::alt_conf_split_type_number() == 1)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_whole), TRUE);
   if (graphics_info_t::alt_conf_split_type_number() == 2)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_range), TRUE);

   if (graphics_info_t::alt_conf_split_type_number() < 2) { 
      std::cout << "Click on the residue you want to split" << std::endl;
   } else { 
      std::cout << "Click on a residue range you want to split" << std::endl;
   }

   graphics_info_t::in_add_alt_conf_define = 1;
   graphics_info_t::pick_cursor_maybe();
   graphics_info_t::pick_pending_flag = 1;
   graphics_info_t::add_alt_conf_dialog = dialog;
} 


void altconf() { 

   GtkWidget *widget = create_add_alt_conf_dialog();
   setup_alt_conf_with_dialog(widget);
   gtk_widget_show(widget);
}

/*  ------------------------------------------------------------------------ */
/*                         recover session:                                  */
/*  ------------------------------------------------------------------------ */
/* section Recover Session Function */
/* After a crash (shock horror!) we provide this convenient interface
   to restore the session.  It runs through all the molecules with
   models and looks at the coot backup directory looking for related
   backup files that are more recent that the read file. */
void recover_session() { 

   int i_rec = 0;
   for (int imol=0; imol<graphics_info_t::n_molecules(); imol++) { 
      if (graphics_info_t::molecules[imol].has_model()) { 
	 coot::backup_file_info info = 
	    graphics_info_t::molecules[imol].recent_backup_file_info();
	 if (info.status) { 

	    coot::backup_file_info *info_copy = new coot::backup_file_info;
	    *info_copy = info;
	    info_copy->imol = imol;
	    
	    GtkWidget *widget = create_recover_coordinates_dialog();
	    gtk_object_set_user_data(GTK_OBJECT(widget), info_copy);
	    
	    GtkWidget *label1, *label2;
	    label1 = lookup_widget(widget, "recover_coordinates_read_coords_label");
	    label2 = lookup_widget(widget, "recover_coordinates_backup_coordinates_label");

	    gtk_label_set_text(GTK_LABEL(label1), info.name.c_str());
	    gtk_label_set_text(GTK_LABEL(label2), info.backup_file_name.c_str());

	    gtk_widget_show(widget);
	    i_rec++;
	 }
      }
   }
   if (i_rec == 0) {
      GtkWidget *w = create_nothing_to_recover_dialog();
      gtk_widget_show(w);
   }
}

// widget needed for lookup of user data:
// 
void execute_recover_session(GtkWidget *widget) { 

   coot::backup_file_info *info = (coot::backup_file_info *) gtk_object_get_user_data(GTK_OBJECT(widget));

   if (info) { 
      
      graphics_info_t g;
      if (info->imol >= 0 && info->imol < g.n_molecules()) {
	 std::string cwd = coot::util::current_working_dir();
	 g.molecules[info->imol].execute_restore_from_recent_backup(info->backup_file_name, cwd);
	 graphics_draw();
      }
   } else { 
      std::cout << "ERROR:: couldn't find user data in execute_recover_session\n";
   } 
} 


/*  ----------------------------------------------------------------------- */
/*                         Merge Molecules                                  */
/*  ----------------------------------------------------------------------- */

GtkWidget *wrapped_create_merge_molecules_dialog() {

   GtkWidget *w = create_merge_molecules_dialog();
   // fill the dialog here
   // GtkWidget *molecule_option_menu = lookup_widget(w, "merge_molecules_optionmenu");
   GtkWidget *combobox = lookup_widget(w, "merge_molecules_combobox");
   GtkWidget *molecules_vbox       = lookup_widget(w, "merge_molecules_vbox");

   // GtkSignalFunc callback_func = GTK_SIGNAL_FUNC(merge_molecules_menu_item_activate);
   GCallback callback_func = G_CALLBACK(merge_molecules_master_molecule_combobox_changed);

   GtkSignalFunc checkbox_callback_func = GTK_SIGNAL_FUNC(on_merge_molecules_check_button_toggled);


   fill_vbox_with_coordinates_options(molecules_vbox, checkbox_callback_func);

   int imol_master = graphics_info_t::merge_molecules_master_molecule;
   if (imol_master == -1) { 
      for (int i=0; i<graphics_info_t::n_molecules(); i++) {
	 if (graphics_info_t::molecules[i].has_model()) {
	    graphics_info_t::merge_molecules_master_molecule = i;
	    imol_master = i;
	    break;
	 }
      }
   }

   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(combobox, callback_func, imol_master);
   return w;
}

// void merge_molecules_menu_item_activate(GtkWidget *item, 
// 					GtkPositionType pos) {
//    graphics_info_t::merge_molecules_master_molecule = pos;
// }

// #include "c-interface-gui.hh"

void merge_molecules_master_molecule_combobox_changed(GtkWidget *combobox, 
						      gpointer data) {

   int imol = my_combobox_get_imol(GTK_COMBO_BOX(combobox));
   graphics_info_t::merge_molecules_master_molecule = imol;
}

void fill_vbox_with_coordinates_options(GtkWidget *dialog,
					GtkSignalFunc checkbox_callback_func) {

   GtkWidget *checkbutton;
   std::string button_label;
   GtkWidget *molecules_vbox = lookup_widget(dialog, "merge_molecules_vbox");

   // Unset any preconcieved notion of merging molecules:
   // 
   graphics_info_t::merge_molecules_merging_molecules->resize(0);

   for (int i=0; i<graphics_info_t::n_molecules(); i++) {
      if (graphics_info_t::molecules[i].has_model()) {
	 button_label = graphics_info_t::int_to_string(i);
	 button_label += " ";
	 button_label += graphics_info_t::molecules[i].name_for_display_manager();
	 std::string button_name = "merge_molecules_checkbutton_";
	 button_name += graphics_info_t::int_to_string(i);

	 checkbutton = gtk_check_button_new_with_label(button_label.c_str());
  	 gtk_widget_ref (checkbutton);
  	 gtk_object_set_data_full (GTK_OBJECT (dialog),
  				   button_name.c_str(), checkbutton,
  				   (GtkDestroyNotify) gtk_widget_unref);
	 // The callback (if active) adds this molecule to the merging molecules list.
	 // If not active, it tries to remove it from the list.
	 //
	 // Why am I doing it like this instead of just looking at the
	 // state of the checkbutton when the OK button is pressed?
	 // Because (for the life of me) I can't seem to correctly
	 // lookup the checkbuttons from the button (or dialog for
	 // that matter).
	 // 
	 //  We look at the state when the
	 // "Merge" button is pressed - we don't need a callback to do
	 // that.
	 // 
  	 gtk_signal_connect (GTK_OBJECT (checkbutton), "toggled",
  			     GTK_SIGNAL_FUNC (checkbox_callback_func),
  			     GINT_TO_POINTER(i));
	 gtk_widget_show (checkbutton);
	 gtk_box_pack_start (GTK_BOX (molecules_vbox), checkbutton, FALSE, FALSE, 0);
	 gtk_container_set_border_width (GTK_CONTAINER (checkbutton), 2);
      }
   }
}

// The callback (if active) adds this molecule to the merging molecules list.
// If not active, it tries to remove it from the list.
// 
void on_merge_molecules_check_button_toggled (GtkToggleButton *togglebutton,
					      gpointer         user_data) {

   int imol = GPOINTER_TO_INT(user_data);
   if (gtk_toggle_button_get_active(togglebutton)) {
      std::cout << "INFO:: adding molecule " << imol << " to merging list\n";
      graphics_info_t::merge_molecules_merging_molecules->push_back(imol);
   } else {
      std::cout << "INFO:: removing molecule " << imol << " from merging list\n";
      if (coot::is_member_p(*graphics_info_t::merge_molecules_merging_molecules, imol)) {
	 // passing a pointer
	 coot::remove_member(graphics_info_t::merge_molecules_merging_molecules, imol);
      }
   }
}


// Display the gui
void do_merge_molecules_gui() {

   GtkWidget *w = wrapped_create_merge_molecules_dialog();
   gtk_widget_show(w);
} 

// The action on Merge button press:
// 
void do_merge_molecules(GtkWidget *dialog) {

   std::vector<int> add_molecules = *graphics_info_t::merge_molecules_merging_molecules;
   if (add_molecules.size() > 0) {

      if (true)
	 std::cout << "calling merge_molecules_by_vector into "
		   << graphics_info_t::merge_molecules_master_molecule
		   << " n-molecules " << add_molecules.size()
		   << " starting with " << add_molecules[0]
		   << std::endl;
      std::pair<int, std::vector<merge_molecule_results_info_t> > stat =
	 merge_molecules_by_vector(add_molecules,
				   graphics_info_t::merge_molecules_master_molecule);
      if (stat.first)
	 graphics_draw();
   }
}

/*  ----------------------------------------------------------------------- */
/*                         Mutate Sequence GUI                              */
/*  ----------------------------------------------------------------------- */

GtkWidget *wrapped_create_mutate_sequence_dialog() {

   graphics_info_t g;

   GtkWidget *w = create_mutate_sequence_dialog();

   set_transient_and_position(COOT_MUTATE_RESIDUE_RANGE_WINDOW, w);

   // GtkWidget *molecule_option_menu = lookup_widget(w, "mutate_molecule_optionmenu");
   // GtkWidget *chain_option_menu    = lookup_widget(w, "mutate_molecule_chain_optionmenu");
   //   GtkWidget *chain_option_menu    = lookup_widget(w, "mutate_molecule_chain_optionmenu");
   //    GtkWidget *entry1 = lookup_widget(w, "mutate_molecule_resno_1_entry");
   //    GtkWidget *entry2 = lookup_widget(w, "mutate_molecule_resno_2_entry");
   //    GtkWidget *textwindow = lookup_widget(w, "mutate_molecule_sequence_text");

   GtkWidget *combobox_molecule = lookup_widget(w, "mutate_molecule_combobox");
   GtkWidget *combobox_chain    = lookup_widget(w, "mutate_molecule_chain_combobox");
   // GCallback callback_func      = G_CALLBACK(mutate_sequence_molecule_menu_item_activate);
   GCallback callback_func      = G_CALLBACK(mutate_sequence_molecule_combobox_changed);

   // Get the default molecule and fill chain optionmenu with the molecules chains:
   int imol = -1; 
   for (int i=0; i<graphics_info_t::n_molecules(); i++) {
      if (graphics_info_t::molecules[i].has_model()) {
	 imol = i;
	 break;
      }
   }
   if (imol >= 0) {
      graphics_info_t::mutate_sequence_imol = imol;
      // GCallback callback = G_CALLBACK(mutate_sequence_chain_option_menu_item_activate);

      GCallback callback = G_CALLBACK(mutate_sequence_chain_combobox_changed);
      std::string set_chain = graphics_info_t::fill_combobox_with_chain_options(combobox_chain, imol, callback);
      graphics_info_t::mutate_sequence_chain_from_combobox = set_chain;

   } else {
      graphics_info_t::mutate_sequence_imol = -1; // flag for can't mutate
   }

   std::cout << "DEBUG:: filling option menu with default molecule " << imol << std::endl;
   g.fill_combobox_with_coordinates_options(combobox_molecule, callback_func, imol);
   return w;
}


void mutate_sequence_molecule_combobox_changed(GtkWidget *combobox, gpointer data) {

   int imol = my_combobox_get_imol(GTK_COMBO_BOX(combobox));

   graphics_info_t::mutate_sequence_imol = imol;
   GCallback chain_callback_func = G_CALLBACK(mutate_sequence_chain_combobox_changed);
   GtkWidget *chain_combobox = lookup_widget(combobox, "mutate_molecule_chain_combobox");
   graphics_info_t g;
   std::string set_chain = g.fill_combobox_with_chain_options(chain_combobox, imol, chain_callback_func);
   // graphics_info_t::mutate_sequence_chain_from_optionmenu = set_chain;
   graphics_info_t::mutate_sequence_chain_from_combobox = set_chain;

}

void mutate_sequence_molecule_menu_item_activate(GtkWidget *item, 
						 GtkPositionType pos) {

   // change the chain id option menu here...
   std::cout << "DEBUG:: mutate_sequence_molecule_menu_item_activate got pos:"
	     << pos << std::endl;

   graphics_info_t::mutate_sequence_imol = pos;

   GtkWidget *chain_option_menu =
      lookup_widget(item, "mutate_molecule_chain_optionmenu");

   GtkSignalFunc callback_func =
      GTK_SIGNAL_FUNC(mutate_sequence_chain_option_menu_item_activate);
   
   std::string set_chain = graphics_info_t::fill_combobox_with_chain_options(chain_option_menu,
									     pos, callback_func);

   // graphics_info_t::mutate_sequence_chain_from_optionmenu = set_chain;
}

void mutate_sequence_chain_combobox_changed(GtkWidget *combobox, gpointer data) {

   char *atc = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox));
   if (atc)
      graphics_info_t::mutate_sequence_chain_from_combobox = atc;
}

void mutate_sequence_chain_option_menu_item_activate (GtkWidget *item,
						      GtkPositionType pos) { 

   // graphics_info_t::mutate_sequence_chain_from_optionmenu = menu_item_label(item);
}


// Don't use this function.  Use the one in graphics_info_t which you
// pass the callback function and get back a chain id.
// nvoid fill_chain_option_menu(GtkWidget *chain_option_menu, int imol) {

//   GtkSignalFunc callback_func =
//      GTK_SIGNAL_FUNC(mutate_sequence_chain_option_menu_item_activate);

   // fill_chain_option_menu_with_callback(chain_option_menu, imol, callback_func);
// }

// the generic form of the above
// void fill_chain_option_menu_with_callback(GtkWidget *chain_option_menu, int imol,
//  					  GtkSignalFunc callback_func) {

   // junk this function and use the one that returns a string.
// }




// The "Mutate" button action:
// 
void do_mutate_sequence(GtkWidget *dialog) {

#ifdef USE_PYTHON
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = coot::STATE_PYTHON;
#endif
#else // python not used
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = 0;
#endif
#endif

   // decode the dialog here

   GtkWidget *entry1 = lookup_widget(dialog, "mutate_molecule_resno_1_entry");
   GtkWidget *entry2 = lookup_widget(dialog, "mutate_molecule_resno_2_entry");

   int t;
   int res1 = -9999, res2 = -99999;
   graphics_info_t g;
   
   const gchar *entry_text = gtk_entry_get_text(GTK_ENTRY(entry1));
   t = atoi(entry_text);
   if ((t > -999) && (t < 9999))
      res1 = t;
   entry_text = gtk_entry_get_text(GTK_ENTRY(entry2));
   t = atoi(entry_text);
   if ((t > -999) && (t < 9999))
      res2 = t;

// BL says: we should set a flag that we swapped the direction and swap back
// before we call fit-gap to actually build backwards
   int swap_flag = 0;
   if (res2 < res1) {
      t = res1;
      res1 = res2;
      res2 = t;
      swap_flag = 1;
   }


   // set the imol and chain_id:
   // 
   int imol = graphics_info_t::mutate_sequence_imol;

   std::string chain_id = graphics_info_t::mutate_sequence_chain_from_combobox;

   
   // Auto fit?
   GtkWidget *checkbutton = lookup_widget(dialog, "mutate_sequence_do_autofit_checkbutton"); 
   short int autofit_flag = 0;

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton)))
      autofit_flag = 1;

   if (imol>= 0) { // redundant
      if (is_valid_model_molecule(imol)) { 

	 // get the sequence:
	 GtkWidget *text = lookup_widget(dialog, "mutate_molecule_sequence_text");
	 char *txt = NULL;

	 GtkTextView *tv = GTK_TEXT_VIEW(text);
	 GtkTextBuffer* tb = gtk_text_view_get_buffer(tv);
	 GtkTextIter startiter;
	 GtkTextIter enditer;
	 gtk_text_buffer_get_iter_at_offset(tb, &startiter, 0);
	 gtk_text_buffer_get_iter_at_offset(tb, &enditer, -1);
	 txt = gtk_text_buffer_get_text(tb, &startiter, &enditer, 0);

	 std::string mutate_scripting_function = "mutate-and-autofit-residue-range";
	 if (! autofit_flag)
	    mutate_scripting_function = "mutate-residue-range";

	 if (txt) {
	    std::string sequence(txt);
	    sequence = coot::util::plain_text_to_sequence(sequence);
	    std::cout << "we got the sequence: " << sequence << std::endl;

	    if (int(sequence.length()) == (res2 - res1 + 1)) {
	       std::vector<std::string> cmd_strings;
	       if (autofit_flag)
		  cmd_strings.push_back("mutate-and-autofit-residue-range");
	       else 
		  cmd_strings.push_back("mutate-residue-range");
	       cmd_strings.push_back(graphics_info_t::int_to_string(imol));
	       cmd_strings.push_back(single_quote(chain_id));
	       cmd_strings.push_back(graphics_info_t::int_to_string(res1));
	       cmd_strings.push_back(graphics_info_t::int_to_string(res2));
	       cmd_strings.push_back(single_quote(sequence));
	       std::string cmd = g.state_command(cmd_strings, state_lang);
	       
// BL says: I believe we should distinguish between python and guile here

#ifdef USE_GUILE
	       if (state_lang == coot::STATE_SCM) {
		  safe_scheme_command(cmd);
	       }
#else
#ifdef USE_PYTHON
              if (state_lang == coot::STATE_PYTHON) {
                 safe_python_command(cmd);
              }
#endif // PYTHON
#endif // GUILE

	      update_go_to_atom_window_on_changed_mol(imol);
	      g.update_geometry_graphs(g.molecules[imol].atom_sel, imol);

	    } else {
	       std::cout << "WARNING:: can't mutate.  Sequence of length: "
			 << sequence.length() << " but residue range size: "
			 << res2 - res1 + 1 << "\n";
	    } 
	 } else {
	    std::cout << "WARNING:: can't mutate.  No sequence\n";
	 } 
      } else {
	 std::cout << "WARNING:: Bad molecule number: " << imol << std::endl;
	 std::cout << "          Can't mutate." << std::endl;
      }
   } else {
      std::cout << "WARNING:: unassigned molecule number: " << imol << std::endl;
      std::cout << "          Can't mutate." << std::endl;
   }
}

GtkWidget *wrapped_fit_loop_rama_search_dialog() {

   GtkWidget *w = wrapped_create_mutate_sequence_dialog();

   GtkWidget *label              = lookup_widget(w, "function_for_molecule_label");
   GtkWidget *method_frame       = lookup_widget(w, "loop_fit_method_frame");
   GtkWidget *mutate_ok_button   = lookup_widget(w, "mutate_sequence_ok_button");
   GtkWidget *fit_loop_ok_button = lookup_widget(w, "fit_loop_ok_button");
   GtkWidget *checkbutton        = lookup_widget(w, "mutate_sequence_do_autofit_checkbutton");

   GtkWidget *rama_checkbutton   = lookup_widget(w, "mutate_sequence_use_ramachandran_restraints_checkbutton");
   
   gtk_label_set_text(GTK_LABEL(label), "\nFit loop in Molecule:\n");
   gtk_widget_hide(mutate_ok_button);
   gtk_widget_hide(checkbutton);
   gtk_widget_show(fit_loop_ok_button);
   gtk_widget_show(rama_checkbutton);
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rama_checkbutton), TRUE);

   gtk_widget_show(method_frame);

   return w;
}

void wrapped_fit_loop_db_loop_dialog() {

   std::vector<std::string> v;
   v.push_back("click-protein-db-loop-gui");

   if (graphics_info_t::prefer_python) {
#ifdef USE_PYTHON
      std::string c = graphics_info_t::pythonize_command_strings(v);
      safe_python_command(c);
#endif
   } else {
#ifdef USE_GUILE
      std::string c = graphics_info_t::schemize_command_strings(v);
      safe_scheme_command(c);
#endif
   }
}


// And the function called by the Fit Loop (OK) button.
// 
void fit_loop_from_widget(GtkWidget *dialog) {

#ifdef USE_PYTHON
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = coot::STATE_PYTHON;
#endif
#else // python not used
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = 0;
#endif
#endif

   // decode the dialog here

   GtkWidget *entry1 = lookup_widget(dialog, "mutate_molecule_resno_1_entry");
   GtkWidget *entry2 = lookup_widget(dialog, "mutate_molecule_resno_2_entry");

   int t;
   int res1 = -9999, res2 = -99999;
   graphics_info_t g;
   
   const gchar *entry_text = gtk_entry_get_text(GTK_ENTRY(entry1));
   t = atoi(entry_text);
   if ((t > -999) && (t < 9999))
      res1 = t;
   entry_text = gtk_entry_get_text(GTK_ENTRY(entry2));
   t = atoi(entry_text);
   if ((t > -999) && (t < 9999))
      res2 = t;

// BL says: we should set a flag that we swapped the direction and swap back
// before we call fit-gap to actually build backwards!!
   int swap_flag = 0;
   if (res2 < res1) {
      t = res1;
      res1 = res2;
      res2 = t;
      swap_flag = 1;
   }


   // set the imol and chain_id:
   // 
   int imol = graphics_info_t::mutate_sequence_imol;

   std::string chain_id = graphics_info_t::mutate_sequence_chain_from_combobox;

   // Auto fit?
   GtkWidget *checkbutton = lookup_widget(dialog, "mutate_sequence_do_autofit_checkbutton"); 
   short int autofit_flag = 0;

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton)))
      autofit_flag = 1;

   // use Ramachandran restraints?
   int use_rama_restraints = 0;
   GtkWidget *rama_checkbutton   = lookup_widget(dialog, "mutate_sequence_use_ramachandran_restraints_checkbutton");
   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rama_checkbutton)))
      use_rama_restraints = 1;

   if (imol>= 0) { // redundant
      if (is_valid_model_molecule(imol)) {

	 // get the sequence:
	 GtkWidget *text = lookup_widget(dialog, "mutate_molecule_sequence_text");
	 char *txt = NULL;

	 GtkTextView *tv = GTK_TEXT_VIEW(text);
	 GtkTextBuffer* tb = gtk_text_view_get_buffer(tv);
	 GtkTextIter startiter;
	 GtkTextIter enditer;
	 gtk_text_buffer_get_iter_at_offset(tb, &startiter, 0);
	 gtk_text_buffer_get_iter_at_offset(tb, &enditer, -1);
	 txt = gtk_text_buffer_get_text(tb, &startiter, &enditer, 0);

	 if (txt) {
	    std::string sequence(txt);
	    sequence = coot::util::plain_text_to_sequence(sequence);
	    int text_widget_sequence_length = sequence.length();
	    std::cout << "INFO:: mutating to the sequence :" << sequence
		      << ":" << std::endl;

	    if (int(sequence.length()) == (res2 - res1 + 1) ||
	        sequence == "") {
	    } else {
	       // so set sequence to poly-ala and give us a message:
	       sequence = "";
	       for (int i=0; i<(res2 - res1 + 1); i++)
		  sequence += "A";

	       std::cout << "WARNING:: Sequence of length: "
			 << text_widget_sequence_length << " but residue range size: "
			 << res2 - res1 + 1 << ".  Using Poly-Ala\n";
	       std::string s("WARNING:: Mis-matched sequence length\nUsing Poly Ala");
	       GtkWidget *w = wrapped_nothing_bad_dialog(s);
	       gtk_widget_show(w);
	    }
            if (swap_flag == 1) {
               t = res1;
               res1 = res2;
               res2 = t;
            }

	    std::vector<std::string> cmd_strings;
	    cmd_strings.push_back("fit-gap");
	    cmd_strings.push_back(graphics_info_t::int_to_string(imol));
	    cmd_strings.push_back(single_quote(chain_id));
	    cmd_strings.push_back(graphics_info_t::int_to_string(res1));
	    cmd_strings.push_back(graphics_info_t::int_to_string(res2));
	    cmd_strings.push_back(single_quote(sequence));
	    cmd_strings.push_back(graphics_info_t::int_to_string(use_rama_restraints));
	    std::string cmd = g.state_command(cmd_strings, state_lang);

#ifdef USE_GUILE
	    if (state_lang == coot::STATE_SCM) {
	       safe_scheme_command(cmd);
	    }
#else
#ifdef USE_PYTHON
            if (state_lang == coot::STATE_PYTHON) {
               safe_python_command(cmd);
            }
#endif // PYTHON
#endif // GUILE
	 }
      }
   }
}


/*  ----------------------------------------------------------------------- */
/*                         Align and Mutate GUI                             */
/*  ----------------------------------------------------------------------- */
GtkWidget *wrapped_create_align_and_mutate_dialog() {

   graphics_info_t g;
   GtkWidget *w = create_align_and_mutate_dialog();

   // GtkWidget *mol_optionmenu   = lookup_widget(w, "align_and_mutate_molecule_optionmenu");
   // GtkWidget *chain_optionmenu = lookup_widget(w, "align_and_mutate_chain_optionmenu");
   GtkWidget *mol_combobox   = lookup_widget(w, "align_and_mutate_molecule_combobox");
   GtkWidget *chain_combobox = lookup_widget(w, "align_and_mutate_chain_combobox");

   // GCallback callback = G_CALLBACK(align_and_mutate_molecule_menu_item_activate);
   // GCallback chain_callback = GCallback(align_and_mutate_chain_option_menu_item_activate);

   GCallback molecule_callback = G_CALLBACK(align_and_mutate_molecule_combobox_changed);
   GCallback    chain_callback = G_CALLBACK(align_and_mutate_chain_combobox_changed);

   int imol = graphics_info_t::align_and_mutate_imol;
   bool try_again = false;

   if (imol == -1) try_again = true;
   if (! is_valid_model_molecule(imol)) try_again = true;

   if (try_again) {
      for (int i=0; i<g.n_molecules(); i++) {
	 if (g.molecules[i].has_model()) {
	    imol = i;
	    break;
	 }
      }
   }

   if (imol >= 0) {
      g.fill_combobox_with_coordinates_options(mol_combobox, molecule_callback, imol);
      std::string set_chain = g.fill_combobox_with_chain_options(chain_combobox, imol,
								 chain_callback);
      g.align_and_mutate_chain_from_combobox = set_chain;
   }

   return w;
}


GtkWidget *wrapped_create_fixed_atom_dialog() {

   GtkWidget *w = create_fixed_atom_dialog();
   graphics_info_t::fixed_atom_dialog = w;
   return w;
}

#include "c-interface-gui.hh"

int do_align_mutate_sequence(GtkWidget *w) {

   //
   int handled_state = 0;  // initially unhandled (return value).

   bool renumber_residues_flag = 0; // make this derived from the GUI one day
   int imol = graphics_info_t::align_and_mutate_imol;

   GtkWidget *molecule_combobox = lookup_widget(w, "align_and_mutate_molecule_combobox");
   GtkWidget    *chain_combobox = lookup_widget(w, "align_and_mutate_chain_combobox");
   std::string chain_id = get_active_label_in_combobox(GTK_COMBO_BOX(chain_combobox));
   imol = my_combobox_get_imol(GTK_COMBO_BOX(molecule_combobox));

   GtkWidget *autofit_checkbutton = lookup_widget(w, "align_and_mutate_autofit_checkbutton");

   std::cout << "--- in do_align_mutate_sequence(): combobox " << molecule_combobox
	     << " " << GTK_IS_COMBO_BOX(molecule_combobox) << " chain-id:" << chain_id << ":"
	     << std::endl;

   short int do_auto_fit = 0;
   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autofit_checkbutton)))
      do_auto_fit = 1;

   graphics_info_t g;
   int imol_refinement_map = g.Imol_Refinement_Map();

   short int early_stop = 0;
   if (do_auto_fit == 1)
      if (imol_refinement_map == -1)
	 early_stop = 1;

   if (early_stop) {
      std::string s = "WARNING:: autofit requested, but \n   refinement map not set!";
      std::cout << s << "\n";
      GtkWidget *warn = wrapped_nothing_bad_dialog(s);
      gtk_widget_show(warn);

   } else {

      handled_state = 1;
      if (imol >= 0) {
	 GtkWidget *text = lookup_widget(w, "align_and_mutate_sequence_text");
	 char *txt = NULL;
      
	 // text is a GtkTextView in GTK2
	 GtkTextView *tv = GTK_TEXT_VIEW(text);
	 GtkTextBuffer* tb = gtk_text_view_get_buffer(tv);
	 GtkTextIter startiter;
	 GtkTextIter enditer;
	 gtk_text_buffer_get_iter_at_offset(tb, &startiter, 0);
	 gtk_text_buffer_get_iter_at_offset(tb, &enditer, -1);
	 txt = gtk_text_buffer_get_text(tb, &startiter, &enditer, 0);
      
	 if (txt) {
	    std::string sequence(txt);

	    if (is_valid_model_molecule(imol)) {

	       std::cout << "debug:: calling mutate_chain " << imol << " chain-id: " << chain_id << " "
			 << sequence << " " << do_auto_fit << std::endl;
	       g.mutate_chain(imol, chain_id, sequence, do_auto_fit, renumber_residues_flag);
	       g.update_geometry_graphs(g.molecules[imol].atom_sel, imol);
	       graphics_draw();

	    }
	 }
      } else {
	 std::cout << "WARNING:: inapproproate molecule number " << imol << std::endl;
      }
   }
   return handled_state;
}


// void align_and_mutate_molecule_menu_item_activate(GtkWidget *item, 
// 						  GtkPositionType pos) {

//    // GtkWidget *chain_optionmenu = lookup_widget(item, "align_and_mutate_chain_optionmenu");
//    GtkWidget *chain_combobox = lookup_widget(item, "align_and_mutate_chain_combobox");
//    GCallback chain_callback = GCallback(align_and_mutate_chain_option_menu_item_activate);
//    graphics_info_t::align_and_mutate_imol = pos;
//    int imol = pos;
//    std::string set_chain = graphics_info_t::fill_combobox_with_chain_options(chain_combobox, imol,
// 									     chain_callback);
// }

// // needs combobox version GTK-FIXME
// void align_and_mutate_chain_option_menu_item_activate (GtkWidget *item,
// 						       GtkPositionType pos) {

//    graphics_info_t::align_and_mutate_chain_from_combobox = menu_item_label(item);
//    std::cout << "align_and_mutate_chain_from_combobox is now "
// 	     << graphics_info_t::align_and_mutate_chain_from_combobox
// 	     << std::endl;
// }

void align_and_mutate_molecule_combobox_changed(GtkWidget *combobox,
						gpointer data) {

}

void align_and_mutate_chain_combobox_changed(GtkWidget *combobox,
					     gpointer data) {

}


/*  ----------------------------------------------------------------------- */
/*                  Change chain ID                                         */
/*  ----------------------------------------------------------------------- */

GtkWidget *wrapped_create_change_chain_id_dialog() {

   graphics_info_t g;

   GtkWidget *w = create_change_chain_id_dialog();
   // GtkWidget *mol_option_menu =  lookup_widget(w, "change_chain_id_molecule_optionmenu");
   // GtkWidget *chain_option_menu =  lookup_widget(w, "change_chain_id_chain_optionmenu");

   GtkWidget *mol_combobox   =  lookup_widget(w, "change_chain_id_molecule_combobox");
   GtkWidget *chain_combobox =  lookup_widget(w, "change_chain_id_chain_combobox");
   GtkWidget *residue_range_no_radiobutton =
      lookup_widget(w, "change_chain_residue_range_no_radiobutton");

   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(residue_range_no_radiobutton), TRUE);

   // GCallback callback_func = G_CALLBACK(change_chain_ids_mol_option_menu_item_activate);
   GCallback molecule_callback_func = G_CALLBACK(change_chain_ids_molecule_combobox_changed);

   int imol = first_coords_imol();
   if (imol >= 0) {
      g.change_chain_id_molecule = imol;
      GCallback chain_callback_func = NULL; // G_CALLBACK(change_chain_ids_chain_menu_item_activate);
      std::string set_chain = g.fill_combobox_with_chain_options(chain_combobox,
								 imol,
								 chain_callback_func);
      g.change_chain_id_from_chain = set_chain;
   }
   g.fill_combobox_with_coordinates_options(mol_combobox, molecule_callback_func, imol);
   return w;
}


// GTK3 dump
// void
// change_chain_ids_mol_option_menu_item_activate(GtkWidget *item,
// 					       GtkPositionType pos) {
//    graphics_info_t::change_chain_id_molecule = pos;
//    int imol = pos;
//    GtkWidget *chain_option_menu =  lookup_widget(item, "change_chain_id_chain_optionmenu");
//    GCallback chain_callback_func = G_CALLBACK(change_chain_ids_chain_menu_item_activate);
//    std::string set_chain = graphics_info_t::fill_combobox_with_chain_options(chain_option_menu,
// 									     imol,
// 									     chain_callback_func);
//    graphics_info_t::change_chain_id_from_chain = set_chain;
// }

void
change_chain_ids_molecule_combobox_changed(GtkWidget *combobox, gpointer data) {

   int imol = my_combobox_get_imol(GTK_COMBO_BOX(combobox));
   graphics_info_t::change_chain_id_molecule = imol;
   GtkWidget *chain_combobox = lookup_widget(combobox, "change_chain_id_chain_combobox");
   if (chain_combobox) {
      graphics_info_t g;
      g.fill_combobox_with_chain_options(chain_combobox, imol, NULL);
   }
}


// // needs a combobox version
// void
// change_chain_ids_chain_menu_item_activate(GtkWidget *item,
// 					  GtkPositionType pos) {
//    graphics_info_t::change_chain_id_from_chain = menu_item_label(item);
// }


void
change_chain_id_by_widget(GtkWidget *w) {

   GtkWidget *residue_range_yes_radiobutton = lookup_widget(w, "change_chain_residue_range_yes_radiobutton");
   GtkWidget *residue_range_from_entry      = lookup_widget(w, "change_chain_residues_from_entry");
   GtkWidget *residue_range_to_entry        = lookup_widget(w, "change_chains_residues_to_entry");
   GtkWidget *change_chains_new_chain_entry = lookup_widget(w, "change_chains_new_chain_id");
   GtkWidget *change_chain_id_from_chain_combobox = lookup_widget(w, "change_chain_id_chain_combobox");

   int imol = graphics_info_t::change_chain_id_molecule;
   bool use_res_range_flag = false;
   int from_resno = -9999;
   int to_resno   = -9999;

   if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(residue_range_yes_radiobutton))) { 
      use_res_range_flag = true;
      std::pair<short int, int> p1 = int_from_entry(residue_range_from_entry);
      std::pair<short int, int> p2 = int_from_entry(residue_range_to_entry);
      if (p1.first)
	 from_resno = p1.second;
      if (p2.first)
	 to_resno = p2.second;
   }

   const gchar *txt = gtk_entry_get_text(GTK_ENTRY(change_chains_new_chain_entry));

   if (txt) {
   
      if (is_valid_model_molecule(imol)) {
	 std::string to_chain_id(txt);

         // 20190810-PE use the widget to find the value now, rather than looking up a stored value
	 std::string from_chain_id = get_active_label_in_combobox(GTK_COMBO_BOX(change_chain_id_from_chain_combobox));

	 if (false)
	    std::cout << "in change_chain_id_molecule() with " << imol << " "
		      << from_chain_id << " " << to_chain_id<< std::endl;

	 std::pair<int, std::string> r = 
	    graphics_info_t::molecules[imol].change_chain_id(from_chain_id,
							     to_chain_id,
							     use_res_range_flag,
							     from_resno,
							     to_resno);
	 if (r.first == 1) { // it went OK
	    update_go_to_atom_window_on_changed_mol(imol);
	    graphics_draw();
	 } else {
	    GtkWidget *ws = wrapped_nothing_bad_dialog(r.second);
	    gtk_widget_show(ws);
	 }
	 graphics_info_t g;
	 g.update_geometry_graphs(g.molecules[imol].atom_sel, imol);
      }
   } else {
      std::cout << "ERROR: Couldn't get txt in change_chain_id_by_widget\n";
   }
}


void fill_option_menu_with_refine_options(GtkWidget *option_menu) { 

   graphics_info_t g;

   g.fill_option_menu_with_map_options(option_menu, 
				       GTK_SIGNAL_FUNC(graphics_info_t::refinement_map_select));
}

void
set_rigid_body_fit_acceptable_fit_fraction(float f) {
   if (f >= 0.0 && f<= 1.0) { 
      graphics_info_t::rigid_body_fit_acceptable_fit_fraction = f;
   } else {
      std::cout << "ignoring set_rigid_body_fit_acceptable_fit_fraction"
		<< " of " << f << std::endl;
   } 
} 


void
my_delete_ramachandran_mol_option(GtkWidget *widget, void *data) {
   gtk_container_remove(GTK_CONTAINER(data), widget);
}


void
show_fix_nomenclature_errors_gui(int imol,
				 const std::vector<std::pair<std::string, coot::residue_spec_t> > &nomenclature_errors) {
   if (graphics_info_t::use_graphics_interface_flag) {
      if (is_valid_model_molecule(imol)) {

	 GtkWidget *w = create_fix_nomenclature_errors_dialog();

	 GtkWidget *label = lookup_widget(w, "fix_nomenclature_errors_label");

	 std::string s = "\n  Molecule number ";
	 s += coot::util::int_to_string(imol);
	 s += " has ";
	 s += coot::util::int_to_string(nomenclature_errors.size());
	 s += " nomenclature error";
	 if (nomenclature_errors.size() > 1)
	    s += "s";
	 s += ".\n";
	 if (nomenclature_errors.size() > 1)
	    s += "  Correct them?\n";
	 else 
	    s += "  Correct it?\n";

	 gtk_object_set_user_data(GTK_OBJECT(w), GINT_TO_POINTER(imol));
	 
	 gtk_label_set_text(GTK_LABEL(label), s.c_str());

	 GtkWidget *box = lookup_widget(w, "nomenclature_errors_vbox");

	 if (box) {
	    // fill box

	    for (unsigned int i=0; i<nomenclature_errors.size(); i++) {
	       s = nomenclature_errors[i].first; // the residue type
	       s += " ";
	       s += nomenclature_errors[i].second.format();
	       GtkWidget *l = gtk_label_new(s.c_str());
	       gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(l), FALSE, FALSE, 2);
	       gtk_widget_show(GTK_WIDGET(l));
	    }
	 }
	 gtk_widget_show(w);

      }
   }
}


#include "get-monomer.hh"

/*  ----------------------------------------------------------------------- */
/*                  get molecule by libcheck/refmac code                    */
/*  ----------------------------------------------------------------------- */

/* Libcheck monomer code */
void 
handle_get_libcheck_monomer_code(GtkWidget *widget) { 

   GtkWidget *frame = lookup_widget(widget, "get_monomer_no_entry_frame");
   const gchar *text = gtk_entry_get_text(GTK_ENTRY(widget));

   int no_entry_frame_shown = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(frame), "shown"));

   if (! no_entry_frame_shown) { 

      int imol = get_monomer(text);

      if (is_valid_model_molecule(imol)) { 

	 GtkWidget *window = lookup_widget(GTK_WIDGET(widget), "libcheck_monomer_dialog");
	 if (window)
	    gtk_widget_destroy(window);
	 else 
	    std::cout << "failed to lookup window in handle_get_libcheck_monomer_code" 
		      << std::endl;
      } else {

	 gtk_widget_show(frame);
	 g_object_set_data(G_OBJECT(frame), "shown", GINT_TO_POINTER(1));
      }

   } else {

      std::cout << "Get-by-network method" << std::endl;

      int imol = get_monomer_molecule_by_network_and_dict_gen(text);
      if (! is_valid_model_molecule(imol)) {
	 info_dialog("Failed to import molecule");
      }

      GtkWidget *window = lookup_widget(GTK_WIDGET(widget), "libcheck_monomer_dialog");
      if (window)
	 gtk_widget_destroy(window);
   }
}


/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/*                               skeleton                                   */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

GtkWidget *
create_skeleton_colour_selection_window() { 

   GtkWidget  *colorseldialog;
   GtkWidget *colorsel;

   colorseldialog = 
      gtk_color_selection_dialog_new("Skeleton Colour Selection"); 

/* How do we get to the buttons? */

   colorsel = GTK_COLOR_SELECTION_DIALOG(colorseldialog)->colorsel;

  /* Capture "color_changed" events in col_sel_window */

  gtk_signal_connect (GTK_OBJECT (colorsel), "color_changed",
                      (GtkSignalFunc)on_skeleton_color_changed, 
		      (gpointer)colorsel);
  
  gtk_signal_connect(GTK_OBJECT(GTK_COLOR_SELECTION_DIALOG(colorseldialog)->
				ok_button), "clicked",
		     GTK_SIGNAL_FUNC(on_skeleton_col_sel_cancel_button_clicked),
		     colorseldialog);

  gtk_signal_connect(GTK_OBJECT(GTK_COLOR_SELECTION_DIALOG(colorseldialog)->
				cancel_button), "clicked",
		     GTK_SIGNAL_FUNC(on_skeleton_col_sel_cancel_button_clicked), 
		     colorseldialog);

  gtk_color_selection_set_color(GTK_COLOR_SELECTION(colorsel),
				graphics_info_t::skeleton_colour);

  return GTK_WIDGET(colorseldialog);

}

/*! \brief show the strand placement gui.

  Choose the python version in there, if needed.  Call scripting
  function, display it in place, don't return a widget. */
void   place_strand_here_dialog() {

   if (graphics_info_t::use_graphics_interface_flag) {
      if (graphics_info_t::prefer_python) {
#ifdef USE_PYTHON
	 std::cout << "safe python commaond place_strand_here_gui()"
		   << std::endl;
	 safe_python_command("place_strand_here_gui()");
#endif // PYTHON
      } else {
#ifdef USE_GUILE
	 safe_scheme_command("(place-strand-here-gui)");
#endif 	 
      } 
   } 
}


/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/*                               fast secondary structure search            */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

GtkWidget *
wrapped_create_fast_ss_search_dialog() {

  GtkWidget *dialog;
  GtkWidget *helix_temp_combobox;
  GtkWidget *strand_temp_combobox;
  GtkWidget *helix_noaa_combobox;
  GtkWidget *strand_noaa_combobox;
  GtkWidget *radius_combobox;

  dialog = create_fast_ss_search_dialog();

  helix_temp_combobox = lookup_widget(dialog, "fast_sss_dialog_helix_template_combobox");
  helix_noaa_combobox = lookup_widget(dialog, "fast_sss_dialog_helix_no_aa_combobox");
  strand_temp_combobox = lookup_widget(dialog, "fast_sss_dialog_strand_template_combobox");
  strand_noaa_combobox = lookup_widget(dialog, "fast_sss_dialog_strand_no_aa_combobox");
  radius_combobox = lookup_widget(dialog, "fast_sss_dialog_radius_combobox");

  // fill the comboboxes (done automatically, set the active ones)
  gtk_combo_box_set_active(GTK_COMBO_BOX(helix_temp_combobox), 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(helix_noaa_combobox), 1);
  gtk_combo_box_set_active(GTK_COMBO_BOX(strand_temp_combobox), 1);
  gtk_combo_box_set_active(GTK_COMBO_BOX(strand_noaa_combobox), 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(radius_combobox),1);

  return dialog;
}


/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
// Edit Functions that have been promoted from Extensions -> Modelling
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
#ifdef __APPLE__
// BANDICOOT: native C replacements for the molecule_chooser_gui() Python
// dialogs, which are no-ops here because `gtk` is the stub module (see
// python/coot_load_modules.py). The underlying ops (copy_molecule,
// new_molecule_by_atom_selection) are already C; only the chooser UI was
// stranded in Python. Builds a non-modal GtkDialog with a model-molecule
// combobox (+ optional atom-selection entry) and runs the op on OK.
int my_combobox_get_imol(GtkComboBox *combobox);   // gtk-manual.h
enum { BANDICOOT_CHOOSER_COPY_MOLECULE = 0,
       BANDICOOT_CHOOSER_COPY_FRAGMENT = 1 };

typedef struct {
   GtkWidget *combobox;
   GtkWidget *entry;   // NULL unless this chooser has an atom-selection entry
   int op_kind;
} bandicoot_chooser_t;

static void bandicoot_molecule_chooser_response(GtkDialog *dialog, gint response,
                                                gpointer user_data) {
   bandicoot_chooser_t *cd = (bandicoot_chooser_t *) user_data;
   if (response == GTK_RESPONSE_ACCEPT) {
      int imol = my_combobox_get_imol(GTK_COMBO_BOX(cd->combobox));
      if (is_valid_model_molecule(imol)) {
         if (cd->op_kind == BANDICOOT_CHOOSER_COPY_FRAGMENT && cd->entry) {
            const char *sel = gtk_entry_get_text(GTK_ENTRY(cd->entry));
            new_molecule_by_atom_selection(imol, sel);
         } else {
            copy_molecule(imol);
         }
      }
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_molecule_chooser_dialog(const char *title,
                                              const char *label_text,
                                              gboolean with_entry,
                                              const char *entry_default,
                                              int op_kind) {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT,
                                  (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

   GtkWidget *label = gtk_label_new(label_text);
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);

   GtkWidget *combobox = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(combobox, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(vbox), combobox, FALSE, FALSE, 4);

   bandicoot_chooser_t *cd = g_new0(bandicoot_chooser_t, 1);
   cd->combobox = combobox;
   cd->op_kind  = op_kind;

   if (with_entry) {
      GtkWidget *entry = gtk_entry_new();
      gtk_entry_set_text(GTK_ENTRY(entry), entry_default ? entry_default : "//A/1-10");
      gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 4);
      cd->entry = entry;
   }

   g_signal_connect(dialog, "response",
                    G_CALLBACK(bandicoot_molecule_chooser_response), cd);
   gtk_widget_show_all(dialog);
}

// BANDICOOT modelling ops salvaged from the dead extensions.py "Modelling..."
// submenu (built with stubbed pygtk gtk.Menu()). The simple, genuinely-useful
// ones act on the active atom's molecule/chain via active_atom_spec(). They are
// surfaced as buttons appended into the existing "Other Modelling Tools" dialog
// (see bandicoot_add_modelling_tools_buttons below) -- consolidating the two
// menu items, which felt redundant.
static int bandicoot_active_imol(coot::atom_spec_t *spec_out) {
   std::pair<bool, std::pair<int, coot::atom_spec_t> > aa = active_atom_spec();
   if (! aa.first) {
      add_status_bar_text("No active atom -- centre on a model atom first");
      return -1;
   }
   if (spec_out) *spec_out = aa.second.second;
   return aa.second.first;
}
static void bandicoot_modelling_fill_partial(GtkButton *b, gpointer u) {
   int imol = bandicoot_active_imol(NULL);
   if (is_valid_model_molecule(imol)) fill_partial_residues(imol);
}
static void bandicoot_modelling_delete_hydrogens(GtkButton *b, gpointer u) {
   int imol = bandicoot_active_imol(NULL);
   if (is_valid_model_molecule(imol)) delete_hydrogens(imol);
}
static void bandicoot_modelling_delete_sidechains(GtkButton *b, gpointer u) {
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (is_valid_model_molecule(imol)) delete_sidechains_for_chain(imol, spec.chain_id.c_str());
}

// Append the salvaged modelling ops as a "Modelling Tools" frame into an
// existing dialog's content area. Called from wrapped_create_other_model_tools_dialog
// so "Other Modelling Tools..." absorbs the old "Modelling..." submenu's ops
// (which is then suppressed from the menu).
extern "C" void bandicoot_add_modelling_tools_buttons(GtkWidget *dialog) {
   if (!dialog) return;
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   GtkWidget *frame = gtk_frame_new("Modelling Tools (active molecule/chain)");
   GtkWidget *fbox = gtk_vbox_new(FALSE, 2);
   gtk_container_set_border_width(GTK_CONTAINER(fbox), 4);
   gtk_container_add(GTK_CONTAINER(frame), fbox);

   const char *labels[3] = { "Fill Partial Residues",
                             "Delete Hydrogen Atoms",
                             "Delete Side-chains for Active Chain" };
   GCallback cbs[3] = { G_CALLBACK(bandicoot_modelling_fill_partial),
                        G_CALLBACK(bandicoot_modelling_delete_hydrogens),
                        G_CALLBACK(bandicoot_modelling_delete_sidechains) };
   for (unsigned int i = 0; i < 3; i++) {
      GtkWidget *btn = gtk_button_new_with_label(labels[i]);
      g_signal_connect(btn, "clicked", cbs[i], NULL);
      gtk_box_pack_start(GTK_BOX(fbox), btn, FALSE, FALSE, 2);
   }
   gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 4);
   gtk_widget_show_all(frame);
}

/* ------------------------------------------------------------------------ */
/* BANDICOOT native "Modelling" main-menu (v0.1.2.x)                        */
/* ------------------------------------------------------------------------ */
// Restores the orphaned extensions.py "Modelling..." submenu (built with the
// stubbed PyGTK gtk.Menu(), so dead) as a native top-level menu. The C menu
// items in gtk2-interface.c all route through on_bandicoot_modelling_activate
// -> bandicoot_modelling_dispatch() here, keyed by the BMOD_* op id. Each op
// calls the underlying C function directly (the engine ops were always C and
// GTK-independent); the few genuinely-Python helpers (refmac H-add,
// phosphorylate, water-chain merge, interactive duplicate-range) are driven
// via safe_python_command -- they are module-level functions in the pre_list
// (coot_utils.py / coot_gui.py), which load into __main__, so the bare names
// resolve (unlike the nested make_link helper -- see make-link-orphaned).
// Tier 2 ops act on a molecule picked through a native chooser (the Python
// molecule_chooser_gui() is a no-op against the gtk stub).

// Generic single-molecule chooser: pick a model molecule, then run op(imol).
typedef void (*bandicoot_imol_op_fn)(int imol);
typedef struct { GtkWidget *combobox; bandicoot_imol_op_fn op; } bandicoot_imol_chooser_t;

static void bandicoot_imol_chooser_response(GtkDialog *dialog, gint response,
                                            gpointer user_data) {
   bandicoot_imol_chooser_t *cd = (bandicoot_imol_chooser_t *) user_data;
   if (response == GTK_RESPONSE_ACCEPT) {
      int imol = my_combobox_get_imol(GTK_COMBO_BOX(cd->combobox));
      if (is_valid_model_molecule(imol)) cd->op(imol);
      else add_status_bar_text("No model molecule chosen");
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_imol_chooser(const char *title, const char *label_text,
                                   bandicoot_imol_op_fn op) {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT,
                                  (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   GtkWidget *label = gtk_label_new(label_text);
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);
   GtkWidget *combobox = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(combobox, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(vbox), combobox, FALSE, FALSE, 4);
   bandicoot_imol_chooser_t *cd = g_new0(bandicoot_imol_chooser_t, 1);
   cd->combobox = combobox;
   cd->op = op;
   g_signal_connect(dialog, "response",
                    G_CALLBACK(bandicoot_imol_chooser_response), cd);
   gtk_widget_show_all(dialog);
}

// Tier 2 per-molecule ops (run after the chooser picks imol).
static void bmod_op_arrange_waters(int imol)   { move_waters_to_around_protein(imol); }
static void bmod_op_assign_hetatm(int imol)    { assign_hetatms(imol); }
static void bmod_op_fix_nomenclature(int imol) { fix_nomenclature_errors(imol); }
static void bmod_op_renumber_waters(int imol)  { renumber_waters(imol); }
static void bmod_op_reorder_chains(int imol)   { sort_chains(imol); }
static void bmod_op_rigid_body_fit(int imol)   { rigid_body_refine_by_atom_selection(imol, "//"); }
static void bmod_op_use_segids(int imol)       { exchange_chain_ids_for_seg_ids(imol); }
static void bmod_op_merge_water_chains(int imol) {
   // No C entry point; merge_solvent_chains is a coot_utils.py helper (pure
   // logic, no GUI) loaded into __main__.
   char cmd[64];
   snprintf(cmd, sizeof(cmd), "merge_solvent_chains(%d)", imol);
   safe_python_command(cmd);
}

// Tier 1 active-atom ops.
static void bmod_add_hydrogens() {
   int imol = bandicoot_active_imol(NULL);
   if (is_valid_model_molecule(imol)) coot_reduce(imol);
}
static void bmod_assign_hetatms_residue() {
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (is_valid_model_molecule(imol))
      hetify_residue(imol, spec.chain_id.c_str(), spec.res_no, spec.ins_code.c_str());
}
static void bmod_invert_chiral() {
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (is_valid_model_molecule(imol))
      invert_chiral_centre(imol, spec.chain_id, spec.res_no, spec.ins_code, spec.atom_name);
}
static void bmod_morph_fit(float radius) {
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (is_valid_model_molecule(imol)) morph_fit_chain(imol, spec.chain_id, radius);
}
static void bmod_morph_fit_ss() {
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (is_valid_model_molecule(imol))
      morph_fit_by_secondary_structure_elements(imol, spec.chain_id);
}
static void bmod_whats_this() {
   // Reimplemented natively (the upstream whats_this() lived nested in the
   // not-loaded extensions.py). Show the active residue's comp-id and its full
   // chemical name from the dictionary -- this is comp_id2name (aliased to the
   // C comp_id_to_name, whose core is Geom_p()->get_monomer_name).
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (! is_valid_model_molecule(imol)) return;
   std::string rn = residue_name(imol, spec.chain_id, spec.res_no, spec.ins_code);
   graphics_info_t g;
   std::pair<bool, std::string> name =
      g.Geom_p()->get_monomer_name(rn, coot::protein_geometry::IMOL_ENC_ANY);
   std::string full = name.first ? name.second : std::string("<no-name-found>");
   std::string s = "(mol. no: " + std::to_string(imol) + ")  " + rn + ":  " + full;
   add_status_bar_text(s.c_str());
}

// ---- Tier 3: choosers/entries (native generic_chooser_and_entry equivalents) --
// molecule combobox + one labelled text entry, then op(imol, text).
typedef void (*bandicoot_imol_text_op_fn)(int imol, const char *text);
typedef struct { GtkWidget *combobox; GtkWidget *entry; bandicoot_imol_text_op_fn op; }
   bandicoot_imol_entry_t;

static void bandicoot_imol_entry_response(GtkDialog *dialog, gint response,
                                          gpointer user_data) {
   bandicoot_imol_entry_t *cd = (bandicoot_imol_entry_t *) user_data;
   if (response == GTK_RESPONSE_ACCEPT) {
      int imol = my_combobox_get_imol(GTK_COMBO_BOX(cd->combobox));
      const char *txt = gtk_entry_get_text(GTK_ENTRY(cd->entry));
      if (is_valid_model_molecule(imol)) cd->op(imol, txt);
      else add_status_bar_text("No model molecule chosen");
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_imol_entry_chooser(const char *title, const char *label_text,
                                         const char *entry_label, const char *entry_default,
                                         bandicoot_imol_text_op_fn op) {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT,
                                  (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   GtkWidget *label = gtk_label_new(label_text);
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);
   GtkWidget *combobox = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(combobox, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(vbox), combobox, FALSE, FALSE, 4);
   GtkWidget *hbox = gtk_hbox_new(FALSE, 4);
   GtkWidget *elabel = gtk_label_new(entry_label);
   gtk_box_pack_start(GTK_BOX(hbox), elabel, FALSE, FALSE, 0);
   GtkWidget *entry = gtk_entry_new();
   if (entry_default) gtk_entry_set_text(GTK_ENTRY(entry), entry_default);
   gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 4);
   bandicoot_imol_entry_t *cd = g_new0(bandicoot_imol_entry_t, 1);
   cd->combobox = combobox; cd->entry = entry; cd->op = op;
   g_signal_connect(dialog, "response",
                    G_CALLBACK(bandicoot_imol_entry_response), cd);
   gtk_widget_show_all(dialog);
}

// single labelled text entry (no molecule chooser), then op(text).
typedef void (*bandicoot_text_op_fn)(const char *text);
typedef struct { GtkWidget *entry; bandicoot_text_op_fn op; } bandicoot_entry_t;

static void bandicoot_single_entry_response(GtkDialog *dialog, gint response,
                                            gpointer user_data) {
   bandicoot_entry_t *cd = (bandicoot_entry_t *) user_data;
   if (response == GTK_RESPONSE_ACCEPT)
      cd->op(gtk_entry_get_text(GTK_ENTRY(cd->entry)));
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_single_entry(const char *title, const char *label_text,
                                   const char *entry_default, bandicoot_text_op_fn op) {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons(title, GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT,
                                  (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   GtkWidget *label = gtk_label_new(label_text);
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);
   GtkWidget *entry = gtk_entry_new();
   if (entry_default) gtk_entry_set_text(GTK_ENTRY(entry), entry_default);
   gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, FALSE, 4);
   bandicoot_entry_t *cd = g_new0(bandicoot_entry_t, 1);
   cd->entry = entry; cd->op = op;
   g_signal_connect(dialog, "response",
                    G_CALLBACK(bandicoot_single_entry_response), cd);
   gtk_widget_show_all(dialog);
}

// ---- Tier 4: native results-list dialog + rebound interesting_things_gui -----
#ifdef USE_PYTHON
// Navigation payload for one results-list row (freed with the button).
typedef struct {
   int has_coord;
   int imol, resno;
   double x, y, z;
   char chain[16], atom[8];
} bandicoot_baddie_t;

static void bandicoot_baddie_clicked(GtkButton *button, gpointer user_data) {
   bandicoot_baddie_t *bd = (bandicoot_baddie_t *) user_data;
   if (bd->has_coord) {
      set_rotation_centre(bd->x, bd->y, bd->z);
   } else {
      set_go_to_atom_molecule(bd->imol);
      set_go_to_atom_chain_residue_atom_name(bd->chain, bd->resno, bd->atom);
   }
}
static void bandicoot_results_close(GtkButton *button, gpointer window) {
   gtk_widget_destroy(GTK_WIDGET(window));
}
static std::string bandicoot_py_str(PyObject *o) {
   if (o && PyString_Check(o)) return PyString_AsString(o);
   return std::string();
}

static void bandicoot_msg_dialog_response(GtkDialog *dialog, gint response,
                                          gpointer user_data) {
   gtk_widget_destroy(GTK_WIDGET(dialog));
}
// Coot-style native info dialog (the Python info_dialog is the gtk stub).
static void bandicoot_native_info_dialog(const char *msg) {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *mw = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *d = gtk_message_dialog_new(mw ? GTK_WINDOW(mw) : NULL,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                                         "%s", msg ? msg : "");
   g_signal_connect(d, "response", G_CALLBACK(bandicoot_msg_dialog_response), NULL);
   gtk_widget_show(d);
}

// Native replacement for coot_gui.py interesting_things_gui(): a scrollable
// column of navigate-on-click buttons. Called from Python (so the GIL is held;
// the click handlers touch only C nav functions). baddie_list entries are
// [label, x, y, z] (go to point) or [label, imol, chain, resno, ins, atom, alt]
// (go to atom); any trailing "fix" callables are ignored.
void bandicoot_interesting_things_py(const char *title, PyObject *baddie_list) {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   if (! baddie_list) return;
   // Accept any iterable -- in Py3 a caller may pass a map()/generator, not a
   // list (e.g. interesting_residues_gui). PySequence_List materialises it.
   PyObject *blist = PySequence_List(baddie_list);
   if (! blist) { PyErr_Clear(); return; }
   Py_ssize_t n = PyList_Size(blist);

   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), title ? title : "Results");
   gtk_window_set_default_size(GTK_WINDOW(window), 320, 340);
   // Transient-for the main window so it doesn't open behind it (the normal-
   // level z-order issue -- see macos-quartz-gl-nsview-facts).
   GtkWidget *mw = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   if (mw) gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(mw));
   GtkWidget *outer = gtk_vbox_new(FALSE, 2);
   gtk_container_set_border_width(GTK_CONTAINER(outer), 4);
   gtk_container_add(GTK_CONTAINER(window), outer);
   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
   gtk_box_pack_start(GTK_BOX(outer), scrolled, TRUE, TRUE, 0);
   GtkWidget *inside = gtk_vbox_new(FALSE, 0);
   gtk_container_set_border_width(GTK_CONTAINER(inside), 4);
   gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), inside);

   int nrows = 0;
   for (Py_ssize_t i = 0; i < n; i++) {
      PyObject *item = PyList_GetItem(blist, i);   // borrowed
      if (! item || ! PyList_Check(item)) continue;
      Py_ssize_t li = PyList_Size(item);
      if (li < 1) continue;
      std::string label = bandicoot_py_str(PyList_GetItem(item, 0));
      bandicoot_baddie_t *bd = g_new0(bandicoot_baddie_t, 1);
      if (li == 4) {
         bd->has_coord = 1;
         bd->x = PyFloat_AsDouble(PyList_GetItem(item, 1));
         bd->y = PyFloat_AsDouble(PyList_GetItem(item, 2));
         bd->z = PyFloat_AsDouble(PyList_GetItem(item, 3));
      } else if (li >= 6) {
         bd->has_coord = 0;
         bd->imol  = (int) PyInt_AsLong(PyList_GetItem(item, 1));
         bd->resno = (int) PyInt_AsLong(PyList_GetItem(item, 3));
         g_strlcpy(bd->chain, bandicoot_py_str(PyList_GetItem(item, 2)).c_str(), sizeof(bd->chain));
         g_strlcpy(bd->atom,  bandicoot_py_str(PyList_GetItem(item, 5)).c_str(), sizeof(bd->atom));
      } else {
         g_free(bd);
         continue;
      }
      GtkWidget *button = gtk_button_new_with_label(label.c_str());
      g_signal_connect_data(button, "clicked", G_CALLBACK(bandicoot_baddie_clicked),
                            bd, (GClosureNotify) g_free, (GConnectFlags) 0);
      gtk_box_pack_start(GTK_BOX(inside), button, FALSE, FALSE, 1);
      nrows++;
   }
   Py_DECREF(blist);
   PyErr_Clear();   // tolerate any per-item conversion hiccup; don't leak it to the caller

   if (nrows == 0) {
      // Nothing found -- pop a Coot-style "No ... found" dialog instead of an
      // empty results window. Derive the phrase from the title (drop a trailing
      // ':'): "Cis Peptides:" -> "No Cis Peptides found".
      gtk_widget_destroy(window);
      std::string t = title ? title : "";
      if (! t.empty() && t[t.size() - 1] == ':') t.erase(t.size() - 1);
      std::string msg = t.empty() ? std::string("Nothing found")
                                  : ("No " + t + " found");
      bandicoot_native_info_dialog(msg.c_str());
      return;
   }

   GtkWidget *ok = gtk_button_new_with_label("  OK  ");
   gtk_box_pack_start(GTK_BOX(outer), ok, FALSE, FALSE, 4);
   g_signal_connect(ok, "clicked", G_CALLBACK(bandicoot_results_close), window);
   gtk_widget_show_all(window);
}
#endif // USE_PYTHON

// Tier 4 ops. Validation lists reuse the existing Python gatherers (which build
// the baddie list and call interesting_things_gui, now rebound to the native
// dialog above -- see coot_load_modules.py). Pick the molecule natively first.
static void bmod_op_alt_confs(int imol) {
   char c[48]; snprintf(c, sizeof(c), "alt_confs_gui(%d)", imol); safe_python_command(c);
}
static void bmod_op_cis_peptides(int imol) {
   char c[48]; snprintf(c, sizeof(c), "cis_peptides_gui(%d)", imol); safe_python_command(c);
}
static void bmod_op_missing_atoms(int imol) {
   char c[48]; snprintf(c, sizeof(c), "missing_atoms_gui(%d)", imol); safe_python_command(c);
}
static void bmod_op_zero_occ(int imol) {
   char c[48]; snprintf(c, sizeof(c), "zero_occ_atoms_gui(%d)", imol); safe_python_command(c);
}
static void bmod_op_rename_residue(const char *text) {
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (! is_valid_model_molecule(imol) || !text || !*text) return;
   set_residue_name(imol, spec.chain_id.c_str(), spec.res_no, spec.ins_code.c_str(), text);
}

// Tier 3 ops.
static void bmod_op_monomer_from_dict(const char *text) {
   int imol = get_monomer_from_dictionary(text, 0);  // 0 = non-idealised model coords
   if (! is_valid_model_molecule(imol)) get_monomer(text);  // fallback (with restraints)
}
static void bmod_op_new_mol_sphere(int imol, const char *text) {
   if (! is_valid_model_molecule(imol)) return;
   char *end = NULL;
   double radius = strtod(text, &end);
   if (end == text || radius <= 0) { add_status_bar_text("Bad radius"); return; }
   new_molecule_by_sphere_selection(imol, rotation_centre_position(0),
                                    rotation_centre_position(1), rotation_centre_position(2),
                                    (float) radius, 0);
}
static void bmod_op_new_mol_symop(int imol, const char *text) {
   if (! is_valid_model_molecule(imol)) return;
   mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
   try {
      clipper::Coord_frac cf = coot::util::shift_to_origin(mol);  // same pre-shift as origin_pre_shift
      new_molecule_by_symop(imol, text,
                            (int) lround(cf.u()), (int) lround(cf.v()), (int) lround(cf.w()));
   } catch (const std::runtime_error &rte) {
      add_status_bar_text(rte.what());
   }
}
static void bmod_op_residue_type_sel(int imol, const char *text) {
   if (! is_valid_model_molecule(imol)) return;
   new_molecule_by_residue_type_selection(imol, text);
   update_go_to_atom_window_on_new_mol();
}
// Fetch the PDBe dictionary for comp_id (network helper get_SMILES_for_comp_id
// _from_pdbe, in coot_utils.py / __main__; it reads the CIF into Coot's
// dictionary) then SURFACE a description -- chemical name + SMILES. Upstream
// computed the SMILES and threw it away, so the "...Description" menu items
// showed nothing; here we read it back from the now-loaded dict (the C++
// SMILES_for_comp_id / get_monomer_name) and report it.
static void bmod_pdbe_describe(const std::string &comp_id, bool also_get_monomer) {
   if (comp_id.empty()) return;
   std::string cmd = "get_SMILES_for_comp_id_from_pdbe('" + comp_id + "')";
   safe_python_command(cmd.c_str());          // network + read_cif_dictionary (manages GIL)
   if (also_get_monomer) get_monomer(comp_id);
   graphics_info_t g;
   std::pair<bool, std::string> nm =
      g.Geom_p()->get_monomer_name(comp_id, coot::protein_geometry::IMOL_ENC_ANY);
   std::string smiles = SMILES_for_comp_id(comp_id);   // from the now-loaded dict
   std::cout << "INFO:: PDBe " << comp_id
             << "   name: "   << (nm.first ? nm.second : std::string("(none found)"))
             << "   SMILES: " << (smiles.empty() ? std::string("(none found)") : smiles)
             << std::endl;
   std::string s = comp_id;
   if (nm.first && !nm.second.empty()) s += ":  " + nm.second;
   else                                s += ":  <no PDBe description found>";
   if (!smiles.empty()) s += "   [SMILES in console]";
   add_status_bar_text(s.c_str());
}
static void bmod_op_fetch_pdbe_ligand(const char *text) {
   if (!text || !*text) return;
   bmod_pdbe_describe(text, true);    // entry version also places the monomer
}
static void bmod_fetch_pdbe_this_ligand() {
   coot::atom_spec_t spec;
   int imol = bandicoot_active_imol(&spec);
   if (! is_valid_model_molecule(imol)) return;
   std::string rn = residue_name(imol, spec.chain_id, spec.res_no, spec.ins_code);
   bmod_pdbe_describe(rn, false);
}

// ---- Tier 4B: Rigid Body Fit Residue Range ---------------------------------
// Native equivalent of residue_range_gui() driving rigid_body_refine_by_residue
// _ranges. v1 supports a single [chain, start, end] range (the common case).
typedef struct { GtkWidget *combobox, *e_chain, *e_start, *e_end; } bandicoot_rbr_t;

static void bandicoot_rbr_response(GtkDialog *dialog, gint response, gpointer user_data) {
   bandicoot_rbr_t *cd = (bandicoot_rbr_t *) user_data;
   if (response == GTK_RESPONSE_ACCEPT) {
      int imol = my_combobox_get_imol(GTK_COMBO_BOX(cd->combobox));
      const char *chain  = gtk_entry_get_text(GTK_ENTRY(cd->e_chain));
      const char *s_strt = gtk_entry_get_text(GTK_ENTRY(cd->e_start));
      const char *s_end  = gtk_entry_get_text(GTK_ENTRY(cd->e_end));
      char *p1 = NULL, *p2 = NULL;
      long r1 = strtol(s_strt, &p1, 10);
      long r2 = strtol(s_end,  &p2, 10);
      if (! is_valid_model_molecule(imol) || !chain || !*chain ||
          p1 == s_strt || p2 == s_end) {
         add_status_bar_text("Rigid Body Fit: need a molecule, chain and two residue numbers");
      } else if (imol_refinement_map() == -1) {
         add_status_bar_text("Rigid Body Fit: set a refinement map first");
      } else {
         char cmd[256];
         snprintf(cmd, sizeof(cmd),
                  "rigid_body_refine_by_residue_ranges(%d, [['%s', %ld, %ld]])",
                  imol, chain, r1, r2);
         safe_python_command(cmd);
      }
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_rigid_body_ranges_dialog() {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons("Rigid Body Fit Residue Range", GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT, (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   GtkWidget *label = gtk_label_new("Rigid-body fit a residue range (needs a refinement map):");
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 4);
   GtkWidget *combobox = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(combobox, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(vbox), combobox, FALSE, FALSE, 4);
   GtkWidget *hbox = gtk_hbox_new(FALSE, 4);
   GtkWidget *e_chain = gtk_entry_new();
   GtkWidget *e_start = gtk_entry_new();
   GtkWidget *e_end   = gtk_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(e_chain), 3);
   gtk_entry_set_width_chars(GTK_ENTRY(e_start), 5);
   gtk_entry_set_width_chars(GTK_ENTRY(e_end),   5);
   gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Chain:"),    FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), e_chain,                    FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Res start:"),FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), e_start,                    FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Res end:"),  FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), e_end,                      FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 4);
   bandicoot_rbr_t *cd = g_new0(bandicoot_rbr_t, 1);
   cd->combobox = combobox; cd->e_chain = e_chain; cd->e_start = e_start; cd->e_end = e_end;
   g_signal_connect(dialog, "response", G_CALLBACK(bandicoot_rbr_response), cd);
   gtk_widget_show_all(dialog);
}

// ---- Tier 4B: Add Other Solvent Molecules ----------------------------------
// Native equivalent of solvent_ligands_gui: molecule chooser + a button per
// solvent comp-id (+ a custom entry). Each adds the solvent via the module-
// level coot_utils.bandicoot_add_solvent_ligand (get_monomer + merge + jiggle).
static const char *bandicoot_solvent_list[] = {
   "EDO", "GOL", "DMS", "ACT", "MPD", "CIT", "SO4", "PO4",
   "TRS", "TAM", "PEG", "PG4", "PE8", "EBE", "BTB" };

static void bandicoot_add_solvent(GtkWidget *combobox, const char *comp_id) {
   int imol = my_combobox_get_imol(GTK_COMBO_BOX(combobox));
   if (! is_valid_model_molecule(imol)) { add_status_bar_text("Choose a model molecule"); return; }
   if (!comp_id || !*comp_id) return;
   char cmd[128];
   snprintf(cmd, sizeof(cmd), "bandicoot_add_solvent_ligand(%d, '%s')", imol, comp_id);
   safe_python_command(cmd);
}
typedef struct { GtkWidget *combobox; char comp_id[8]; } bandicoot_solvent_btn_t;
static void bandicoot_solvent_btn_clicked(GtkButton *b, gpointer u) {
   bandicoot_solvent_btn_t *cd = (bandicoot_solvent_btn_t *) u;
   bandicoot_add_solvent(cd->combobox, cd->comp_id);
}
typedef struct { GtkWidget *combobox, *entry; } bandicoot_solvent_custom_t;
static void bandicoot_solvent_custom_clicked(GtkButton *b, gpointer u) {
   bandicoot_solvent_custom_t *cd = (bandicoot_solvent_custom_t *) u;
   bandicoot_add_solvent(cd->combobox, gtk_entry_get_text(GTK_ENTRY(cd->entry)));
}

static void bandicoot_add_solvent_dialog() {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *mw = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
   gtk_window_set_title(GTK_WINDOW(window), "Add Solvent Molecules");
   gtk_window_set_default_size(GTK_WINDOW(window), 320, 420);
   if (mw) gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(mw));
   gtk_container_set_border_width(GTK_CONTAINER(window), 8);
   GtkWidget *outer = gtk_vbox_new(FALSE, 4);
   gtk_container_add(GTK_CONTAINER(window), outer);
   GtkWidget *label = gtk_label_new("Add a solvent molecule to the chosen molecule\n"
                                    "(jiggle-fits into the refinement map if one is set):");
   gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(outer), label, FALSE, FALSE, 2);
   GtkWidget *combobox = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(combobox, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(outer), combobox, FALSE, FALSE, 2);
   GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
   gtk_box_pack_start(GTK_BOX(outer), scrolled, TRUE, TRUE, 0);
   GtkWidget *inside = gtk_vbox_new(FALSE, 0);
   gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scrolled), inside);
   for (unsigned int i = 0; i < G_N_ELEMENTS(bandicoot_solvent_list); i++) {
      GtkWidget *btn = gtk_button_new_with_label(bandicoot_solvent_list[i]);
      bandicoot_solvent_btn_t *cd = g_new0(bandicoot_solvent_btn_t, 1);
      cd->combobox = combobox;
      g_strlcpy(cd->comp_id, bandicoot_solvent_list[i], sizeof(cd->comp_id));
      g_signal_connect_data(btn, "clicked", G_CALLBACK(bandicoot_solvent_btn_clicked),
                            cd, (GClosureNotify) g_free, (GConnectFlags) 0);
      gtk_box_pack_start(GTK_BOX(inside), btn, FALSE, FALSE, 1);
   }
   gtk_box_pack_start(GTK_BOX(outer), gtk_hseparator_new(), FALSE, FALSE, 2);
   GtkWidget *hbox = gtk_hbox_new(FALSE, 4);
   gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Other code:"), FALSE, FALSE, 0);
   GtkWidget *entry = gtk_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(entry), 5);
   gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 0);
   GtkWidget *add_btn = gtk_button_new_with_label("  Add  ");
   bandicoot_solvent_custom_t *cc = g_new0(bandicoot_solvent_custom_t, 1);
   cc->combobox = combobox; cc->entry = entry;
   g_signal_connect_data(add_btn, "clicked", G_CALLBACK(bandicoot_solvent_custom_clicked),
                         cc, (GClosureNotify) g_free, (GConnectFlags) 0);
   gtk_box_pack_start(GTK_BOX(hbox), add_btn, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(outer), hbox, FALSE, FALSE, 2);
   GtkWidget *close = gtk_button_new_with_label("  Close  ");
   g_signal_connect(close, "clicked", G_CALLBACK(bandicoot_results_close), window);
   gtk_box_pack_start(GTK_BOX(outer), close, FALSE, FALSE, 2);
   gtk_widget_show_all(window);
}

// ---- Tier 4B: Superpose Ligands --------------------------------------------
// Native equivalent of superpose_ligand_gui: pick reference + moving ligand
// (molecule + chain + resno each) and overlay the moving one onto the reference
// via the module-level coot_utils.overlay_my_ligands (overlap_ligands + transform).
typedef struct {
   GtkWidget *ref_combo, *ref_chain, *ref_resno;
   GtkWidget *mov_combo, *mov_chain, *mov_resno;
} bandicoot_superpose_t;

static GtkWidget *bandicoot_ligand_pick_frame(const char *title, GtkWidget **combo_out,
                                              GtkWidget **chain_out, GtkWidget **resno_out) {
   GtkWidget *frame = gtk_frame_new(title);
   GtkWidget *vbox = gtk_vbox_new(FALSE, 2);
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);
   gtk_container_add(GTK_CONTAINER(frame), vbox);
   GtkWidget *combo = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(combo, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 2);
   GtkWidget *hbox = gtk_hbox_new(FALSE, 4);
   GtkWidget *chain = gtk_entry_new();
   GtkWidget *resno = gtk_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(chain), 3);
   gtk_entry_set_width_chars(GTK_ENTRY(resno), 5);
   gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Chain:"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), chain, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Res no:"), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), resno, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);
   *combo_out = combo; *chain_out = chain; *resno_out = resno;
   return frame;
}

static void bandicoot_superpose_response(GtkDialog *dialog, gint response, gpointer u) {
   bandicoot_superpose_t *cd = (bandicoot_superpose_t *) u;
   if (response == GTK_RESPONSE_ACCEPT) {
      int imol_ref = my_combobox_get_imol(GTK_COMBO_BOX(cd->ref_combo));
      int imol_mov = my_combobox_get_imol(GTK_COMBO_BOX(cd->mov_combo));
      const char *ch_ref = gtk_entry_get_text(GTK_ENTRY(cd->ref_chain));
      const char *ch_mov = gtk_entry_get_text(GTK_ENTRY(cd->mov_chain));
      const char *t_ref  = gtk_entry_get_text(GTK_ENTRY(cd->ref_resno));
      const char *t_mov  = gtk_entry_get_text(GTK_ENTRY(cd->mov_resno));
      char *p1 = NULL, *p2 = NULL;
      long rn_ref = strtol(t_ref, &p1, 10);
      long rn_mov = strtol(t_mov, &p2, 10);
      if (is_valid_model_molecule(imol_ref) && is_valid_model_molecule(imol_mov) &&
          *ch_ref && *ch_mov && p1 != t_ref && p2 != t_mov) {
         char cmd[256];
         snprintf(cmd, sizeof(cmd),
                  "overlay_my_ligands(%d, '%s', %ld, %d, '%s', %ld)",
                  imol_mov, ch_mov, rn_mov, imol_ref, ch_ref, rn_ref);
         safe_python_command(cmd);
      } else {
         add_status_bar_text("Superpose Ligands: need two molecules, chains and residue numbers");
      }
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_superpose_ligands_dialog() {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons("Superpose Ligands", GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  "Superpose", GTK_RESPONSE_ACCEPT, (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   GtkWidget *info = gtk_label_new("Overlay the moving ligand onto the reference ligand:");
   gtk_misc_set_alignment(GTK_MISC(info), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 2);
   bandicoot_superpose_t *cd = g_new0(bandicoot_superpose_t, 1);
   GtkWidget *ref_frame = bandicoot_ligand_pick_frame("Reference ligand (stays put)",
                              &cd->ref_combo, &cd->ref_chain, &cd->ref_resno);
   GtkWidget *mov_frame = bandicoot_ligand_pick_frame("Moving ligand (whole molecule moves)",
                              &cd->mov_combo, &cd->mov_chain, &cd->mov_resno);
   gtk_box_pack_start(GTK_BOX(vbox), ref_frame, FALSE, FALSE, 4);
   gtk_box_pack_start(GTK_BOX(vbox), mov_frame, FALSE, FALSE, 4);
   g_signal_connect(dialog, "response", G_CALLBACK(bandicoot_superpose_response), cd);
   gtk_widget_show_all(dialog);
}

// ---- Tier 4B: Assign Sequence (associate FASTA/PIR + cootaneer) ------------
// Native equivalents of associate_sequence_with_chain_gui and cootaneer_gui_bl;
// drive the module-level coot_utils helpers bandicoot_associate_sequence /
// bandicoot_cootaneer via safe_python_command.
typedef struct {
   GtkWidget *combo, *chain, *file_btn, *fasta_radio, *all_chains_chk;
} bandicoot_assocseq_t;

static void bandicoot_assocseq_response(GtkDialog *dialog, gint response, gpointer u) {
   bandicoot_assocseq_t *cd = (bandicoot_assocseq_t *) u;
   if (response == GTK_RESPONSE_ACCEPT) {
      int imol = my_combobox_get_imol(GTK_COMBO_BOX(cd->combo));
      const char *chain = gtk_entry_get_text(GTK_ENTRY(cd->chain));
      char *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(cd->file_btn));
      const char *fmt = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cd->fasta_radio))
                        ? "FASTA" : "PIR";
      gboolean all = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cd->all_chains_chk));
      if (is_valid_model_molecule(imol) && file && (*chain || all)) {
         std::string cmd = "bandicoot_associate_sequence(" + std::to_string(imol) +
                           ", '" + chain + "', '" + file + "', '" + fmt + "', " +
                           (all ? "True" : "False") + ")";
         safe_python_command(cmd.c_str());
      } else {
         add_status_bar_text("Associate Sequence: need a molecule, a chain (or all-chains), and a file");
      }
      if (file) g_free(file);
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_associate_sequence_dialog() {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons("Associate Sequence to Chain", GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT, (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   bandicoot_assocseq_t *cd = g_new0(bandicoot_assocseq_t, 1);
   cd->combo = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(cd->combo, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(vbox), cd->combo, FALSE, FALSE, 4);
   GtkWidget *hbox = gtk_hbox_new(FALSE, 4);
   gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Chain:"), FALSE, FALSE, 0);
   cd->chain = gtk_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(cd->chain), 3);
   gtk_box_pack_start(GTK_BOX(hbox), cd->chain, FALSE, FALSE, 0);
   cd->fasta_radio = gtk_radio_button_new_with_label(NULL, "FASTA");
   GtkWidget *pir_radio =
      gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(cd->fasta_radio), "PIR");
   gtk_box_pack_start(GTK_BOX(hbox), cd->fasta_radio, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(hbox), pir_radio, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 4);
   cd->file_btn = gtk_file_chooser_button_new("Select sequence file",
                                              GTK_FILE_CHOOSER_ACTION_OPEN);
   gtk_box_pack_start(GTK_BOX(vbox), cd->file_btn, FALSE, FALSE, 4);
   cd->all_chains_chk = gtk_check_button_new_with_label("Assign all protein chains (ignore chain above)");
   gtk_box_pack_start(GTK_BOX(vbox), cd->all_chains_chk, FALSE, FALSE, 4);
   g_signal_connect(dialog, "response", G_CALLBACK(bandicoot_assocseq_response), cd);
   gtk_widget_show_all(dialog);
}

typedef struct { GtkWidget *combo, *file_btn, *refine_chk; } bandicoot_cootaneer_t;

static void bandicoot_cootaneer_response(GtkDialog *dialog, gint response, gpointer u) {
   bandicoot_cootaneer_t *cd = (bandicoot_cootaneer_t *) u;
   if (response == GTK_RESPONSE_ACCEPT) {
      int imol = my_combobox_get_imol(GTK_COMBO_BOX(cd->combo));
      char *file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(cd->file_btn));
      gboolean refine = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cd->refine_chk));
      if (is_valid_model_molecule(imol)) {
         std::string f = file ? file : "";
         std::string cmd = "bandicoot_cootaneer(" + std::to_string(imol) +
                           ", '" + f + "', " + (refine ? "True" : "False") + ")";
         safe_python_command(cmd.c_str());
      } else {
         add_status_bar_text("Cootaneer: choose a model molecule");
      }
      if (file) g_free(file);
   }
   gtk_widget_destroy(GTK_WIDGET(dialog));
   g_free(cd);
}

static void bandicoot_cootaneer_dialog() {
   if (! graphics_info_t::use_graphics_interface_flag) return;
   GtkWidget *main_window = lookup_widget(GTK_WIDGET(graphics_info_t::glarea), "window1");
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons("Assign Sequence (Cootaneer)", GTK_WINDOW(main_window),
                                  GTK_DIALOG_DESTROY_WITH_PARENT,
                                  GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                  GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT, (char *) NULL);
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   GtkWidget *info = gtk_label_new("Assign sidechains from a sequence (needs a refinement map):");
   gtk_misc_set_alignment(GTK_MISC(info), 0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 2);
   bandicoot_cootaneer_t *cd = g_new0(bandicoot_cootaneer_t, 1);
   cd->combo = gtk_combo_box_new();
   graphics_info_t g;
   g.fill_combobox_with_coordinates_options(cd->combo, NULL, first_coords_imol());
   gtk_box_pack_start(GTK_BOX(vbox), cd->combo, FALSE, FALSE, 4);
   cd->file_btn = gtk_file_chooser_button_new("Select sequence file",
                                              GTK_FILE_CHOOSER_ACTION_OPEN);
   gtk_box_pack_start(GTK_BOX(vbox), cd->file_btn, FALSE, FALSE, 4);
   cd->refine_chk = gtk_check_button_new_with_label("Refine (fit protein) afterwards");
   gtk_box_pack_start(GTK_BOX(vbox), cd->refine_chk, FALSE, FALSE, 4);
   g_signal_connect(dialog, "response", G_CALLBACK(bandicoot_cootaneer_response), cd);
   gtk_widget_show_all(dialog);
}

extern "C" void bandicoot_modelling_dispatch(int op_id) {
   switch (op_id) {
   // ---- Tier 1 -------------------------------------------------------------
   case BMOD_ADD_HYDROGENS:          bmod_add_hydrogens();                 break;
   case BMOD_ADD_HYDROGENS_REFMAC:   // needs refmac on PATH; Python helper
      safe_python_command("using_active_atom(add_hydrogens_using_refmac, \"aa_imol\")"); break;
   case BMOD_ASSIGN_HETATMS_RESIDUE: bmod_assign_hetatms_residue();        break;
   case BMOD_INVERT_CHIRAL:          bmod_invert_chiral();                 break;
   case BMOD_PHOSPHORYLATE:          safe_python_command("phosphorylate_active_residue()"); break;
   case BMOD_WHATS_THIS:             bmod_whats_this();                    break;
   case BMOD_MORPH_FIT_7:            bmod_morph_fit(7);                    break;
   case BMOD_MORPH_FIT_11:           bmod_morph_fit(11);                   break;
   case BMOD_MORPH_FIT_SS:           bmod_morph_fit_ss();                  break;
   case BMOD_FIND_HELICES:           find_helices();                       break;
   case BMOD_FIND_STRANDS:           find_strands();                       break;
   case BMOD_BACKRUB_ON:             set_rotamer_search_mode(ROTAMERSEARCHLOWRES);  break;
   case BMOD_BACKRUB_OFF:            set_rotamer_search_mode(ROTAMERSEARCHHIGHRES); break;
   case BMOD_DUPLICATE_RANGE:        safe_python_command("duplicate_range_by_atom_pick()"); break;
   case BMOD_SYMM_SHIFT_REF_CHAIN:   move_reference_chain_to_symm_chain_position(); break;
   // ---- Tier 2 (molecule chooser) -----------------------------------------
   case BMOD_ARRANGE_WATERS:
      bandicoot_imol_chooser("Arrange Waters Around Protein",
                             "Arrange waters in molecule:", bmod_op_arrange_waters); break;
   case BMOD_MERGE_WATER_CHAINS:
      bandicoot_imol_chooser("Merge Water Chains",
                             "Merge water chains in molecule:", bmod_op_merge_water_chains); break;
   case BMOD_RENUMBER_WATERS:
      bandicoot_imol_chooser("Renumber Waters",
                             "Renumber waters of molecule:", bmod_op_renumber_waters); break;
   case BMOD_ASSIGN_HETATM_MOL:
      bandicoot_imol_chooser("Assign HETATM to Molecule",
                             "Assign HETATMs as per PDB definition:", bmod_op_assign_hetatm); break;
   case BMOD_FIX_NOMENCLATURE:
      bandicoot_imol_chooser("Fix Nomenclature Errors",
                             "Fix nomenclature errors in molecule:", bmod_op_fix_nomenclature); break;
   case BMOD_REORDER_CHAINS:
      bandicoot_imol_chooser("Reorder Chains",
                             "Sort chain IDs in molecule:", bmod_op_reorder_chains); break;
   case BMOD_RIGID_BODY_FIT_MOL:
      bandicoot_imol_chooser("Rigid Body Fit Molecule",
                             "Rigid body fit whole molecule:", bmod_op_rigid_body_fit); break;
   case BMOD_USE_SEGIDS:
      bandicoot_imol_chooser("Use SEGIDs",
                             "Exchange chain IDs for SEG IDs in molecule:", bmod_op_use_segids); break;
   // ---- Tier 3 (chooser and/or text entry) --------------------------------
   case BMOD_MONOMER_FROM_DICT:
      bandicoot_single_entry("Monomer from Dictionary",
                             "Pull coordinates from CIF dictionary for 3-letter code:",
                             "", bmod_op_monomer_from_dict); break;
   case BMOD_NEW_MOL_SPHERE:
      bandicoot_imol_entry_chooser("New Molecule by Sphere",
                                   "Choose a molecule to select a sphere of atoms from:",
                                   "Radius:", "10.0", bmod_op_new_mol_sphere); break;
   case BMOD_NEW_MOL_SYMOP:
      bandicoot_imol_entry_chooser("New Molecule from Symmetry Op",
                                   "Molecule to generate a symmetry copy from:",
                                   "SymOp:", "X,Y,Z", bmod_op_new_mol_symop); break;
   case BMOD_RESIDUE_TYPE_SEL:
      bandicoot_imol_entry_chooser("Residue Type Selection",
                                   "Choose a molecule to select residues from:",
                                   "Residue Type:", "", bmod_op_residue_type_sel); break;
   case BMOD_FETCH_PDBE_LIGAND:
      bandicoot_single_entry("Fetch PDBe Ligand Description",
                             "Fetch PDBe Ligand Description for comp-id:",
                             "", bmod_op_fetch_pdbe_ligand); break;
   case BMOD_FETCH_PDBE_THIS_LIGAND:  bmod_fetch_pdbe_this_ligand(); break;
   // ---- Tier 4 (custom result windows) ------------------------------------
   case BMOD_RENAME_RESIDUE:
      bandicoot_single_entry("Rename Residue",
                             "New residue name for the centred residue:",
                             "", bmod_op_rename_residue); break;
   case BMOD_RES_ALT_CONFS:
      bandicoot_imol_chooser("Residues with Alt Confs",
                             "Which molecule to check for alt confs?", bmod_op_alt_confs); break;
   case BMOD_RES_CIS_PEPTIDES:
      bandicoot_imol_chooser("Residues with Cis Peptide Bonds",
                             "Check for cis peptides in molecule:", bmod_op_cis_peptides); break;
   case BMOD_RES_MISSING_ATOMS:
      bandicoot_imol_chooser("Residues with Missing Atoms",
                             "Which molecule to check for missing atoms?", bmod_op_missing_atoms); break;
   case BMOD_RES_ZERO_OCC:
      bandicoot_imol_chooser("Atoms with Zero Occupancies",
                             "Which molecule to check for zero-occupancy atoms?", bmod_op_zero_occ); break;
   // ---- Tier 4 Batch B ----------------------------------------------------
   case BMOD_RIGID_BODY_RANGES:  bandicoot_rigid_body_ranges_dialog(); break;
   case BMOD_ADD_SOLVENT:        bandicoot_add_solvent_dialog();       break;
   case BMOD_SUPERPOSE_LIGANDS:  bandicoot_superpose_ligands_dialog(); break;
   case BMOD_ASSOC_SEQUENCE:     bandicoot_associate_sequence_dialog(); break;
   case BMOD_COOTANEER:          bandicoot_cootaneer_dialog();          break;
   default:
      break;
   }
}

// ---- PanDDA Inspect panel -------------------------------------------------
// A native panel driving python/bandicoot_pandda.py (parse pandda.analyse
// results, navigate events, load model + aligned event/z maps). The Python
// driver functions return a one-line status string, which we show in the panel.
static GtkWidget *bandicoot_pandda_status_label = NULL;
static GtkWidget *bandicoot_pandda_prev_btn = NULL;
static GtkWidget *bandicoot_pandda_next_btn = NULL;
static GtkWidget *bandicoot_pandda_prev_site_btn = NULL;
static GtkWidget *bandicoot_pandda_next_site_btn = NULL;
static GtkWidget *bandicoot_pandda_emap_check = NULL;
static GtkWidget *bandicoot_pandda_zmap_check = NULL;
static GtkWidget *bandicoot_pandda_place_btn = NULL;
static GtkWidget *bandicoot_pandda_merge_btn = NULL;
static GtkWidget *bandicoot_pandda_save_btn = NULL;
static GtkWidget *bandicoot_pandda_load_btn = NULL;
static GtkWidget *bandicoot_pandda_smiles_btn = NULL;
static GtkWidget *bandicoot_pandda_sel_combo = NULL;
static GtkWidget *bandicoot_pandda_xray_check = NULL;
static GtkWidget *bandicoot_pandda_avg_check = NULL;
static GtkWidget *bandicoot_pandda_interesting_check = NULL;
static GtkWidget *bandicoot_pandda_conf_combo = NULL;
static GtkWidget *bandicoot_pandda_comment_entry = NULL;
static GtkWidget *bandicoot_pandda_dataset_view = NULL;   // "Go to" dataset list
static GtkListStore *bandicoot_pandda_dataset_store = NULL;
static GtkWidget *bandicoot_pandda_go_btn = NULL;
static gboolean   bandicoot_pandda_refreshing = FALSE;  // suppress widget callbacks

// Dataset-info value labels: crystal, resolution, r_work, r_free, event, site, BDC.
#define BANDICOOT_PANDDA_NINFO 7
static GtkWidget *bandicoot_pandda_info_val[BANDICOOT_PANDDA_NINFO] = { NULL };
static const char *bandicoot_pandda_info_titles[BANDICOOT_PANDDA_NINFO] = {
   "Crystal", "Resolution", "Rwork", "Rfree", "Event", "Site", "BDC" };

// Ligand-confidence values, in the same order as PanddaInspect.CONFIDENCE_VALUES.
static const char *bandicoot_pandda_conf_values[] = {
   "unassigned", "no ligand bound", "unknown ligand",
   "low confidence", "high confidence" };

// --- pandda.inspect-style UI: shared dialog/mode state + extra widgets ---
// The dialog holds a swappable body; mode selects which builder fills it.
static GtkWidget *bandicoot_pandda_dialog_widget = NULL;   // the top dialog
static GtkWidget *bandicoot_pandda_body = NULL;            // swappable content vbox
static int        bandicoot_pandda_mode = 0;   // 0 = pandda.inspect-style, 1 = basic
// inspect-only widgets (left NULL in basic mode; all callers NULL-check):
static GtkWidget *bandicoot_pandda_event_n_label = NULL;  // "Event  N  of  M"
static GtkWidget *bandicoot_pandda_event_m_label = NULL;
static GtkWidget *bandicoot_pandda_site_n_label  = NULL;  // "Site   i  of  k"
static GtkWidget *bandicoot_pandda_site_m_label  = NULL;
static GtkWidget *bandicoot_pandda_goto_entry       = NULL;
static GtkWidget *bandicoot_pandda_uimode_combo     = NULL;  // bottom-bar UI Mode
static GtkWidget *bandicoot_pandda_interesting_yes  = NULL;
static GtkWidget *bandicoot_pandda_interesting_no   = NULL;
static GtkWidget *bandicoot_pandda_placed_yes       = NULL;
static GtkWidget *bandicoot_pandda_placed_no        = NULL;
static GtkWidget *bandicoot_pandda_conf_high        = NULL;
static GtkWidget *bandicoot_pandda_conf_med         = NULL;
static GtkWidget *bandicoot_pandda_conf_low         = NULL;
static GtkWidget *bandicoot_pandda_site_name_entry  = NULL;
static GtkWidget *bandicoot_pandda_site_comment_entry = NULL;
static GtkWidget *bandicoot_pandda_next_unviewed_btn = NULL;
static GtkWidget *bandicoot_pandda_next_modelled_btn = NULL;
static GtkWidget *bandicoot_pandda_next_save_btn    = NULL;  // Next (Save Model)
static GtkWidget *bandicoot_pandda_openlig_btn      = NULL;
static GtkWidget *bandicoot_pandda_reload_btn       = NULL;
static GtkWidget *bandicoot_pandda_reset_btn        = NULL;
static GtkWidget *bandicoot_pandda_compare_btn      = NULL;
static GtkWidget *bandicoot_pandda_inmtz_btn        = NULL;
static GtkWidget *bandicoot_pandda_avgmap_btn       = NULL;
#define BANDICOOT_PANDDA_NINSP 9
static GtkWidget *bandicoot_pandda_ins_info[BANDICOOT_PANDDA_NINSP] = { NULL };
static const char *bandicoot_pandda_ins_titles[BANDICOOT_PANDDA_NINSP] = {
   "Dataset ID", "Event #", "1-BDC", "Z-blob Peak", "Z-blob Size",
   "Resolution", "Map Uncertainty", "R-Free", "R-Work" };
// confidence radio <-> CSV token mapping (3 radios span the 5-token list)
static const char *bandicoot_pandda_conf_high_tok = "high confidence";
static const char *bandicoot_pandda_conf_med_tok  = "unknown ligand";
static const char *bandicoot_pandda_conf_low_tok  = "low confidence";

static void bandicoot_pandda_rebuild_body();              // fwd decls
static void bandicoot_pandda_load_folder(const char *path);
static void bandicoot_pandda_build_bottom(GtkWidget *vbox);  // shared bottom bar
// soften the global Lennard-Jones epsilon for PanDDA ligand RSR (defined in
// c-interface-refine.cc; declared in c-interface-refine.hh, not c-interface.h).
void set_refinement_lennard_jones_epsilon(float epsilon);
#define BANDICOOT_PANDDA_LJ_EPSILON 0.1f
// default UI mode from Preferences > Others > Ligands (0 Full, 1 Basic, 2 Expanded);
// defined in c-interface-preferences.cc.
int bandicoot_load_pandda_uimode();

// Navigation/editing widgets are usable only when a dataset is loaded (the
// Select-folder and Close buttons stay enabled at all times).
static void bandicoot_pandda_set_nav_sensitive(gboolean on) {
   GtkWidget *w[] = { bandicoot_pandda_prev_btn,      bandicoot_pandda_next_btn,
                      bandicoot_pandda_prev_site_btn, bandicoot_pandda_next_site_btn,
                      bandicoot_pandda_emap_check,     bandicoot_pandda_zmap_check,
                      bandicoot_pandda_xray_check,     bandicoot_pandda_avg_check,
                      bandicoot_pandda_load_btn,       bandicoot_pandda_smiles_btn,
                      bandicoot_pandda_place_btn,      bandicoot_pandda_merge_btn,
                      bandicoot_pandda_save_btn,       bandicoot_pandda_sel_combo,
                      bandicoot_pandda_interesting_check, bandicoot_pandda_conf_combo,
                      bandicoot_pandda_comment_entry,  bandicoot_pandda_dataset_view,
                      bandicoot_pandda_go_btn,
                      // inspect-only widgets:
                      bandicoot_pandda_goto_entry,
                      bandicoot_pandda_interesting_yes, bandicoot_pandda_interesting_no,
                      bandicoot_pandda_placed_yes,      bandicoot_pandda_placed_no,
                      bandicoot_pandda_conf_high, bandicoot_pandda_conf_med,
                      bandicoot_pandda_conf_low,
                      bandicoot_pandda_site_name_entry, bandicoot_pandda_site_comment_entry,
                      bandicoot_pandda_next_unviewed_btn, bandicoot_pandda_next_modelled_btn,
                      bandicoot_pandda_next_save_btn,   bandicoot_pandda_openlig_btn,
                      bandicoot_pandda_reload_btn,      bandicoot_pandda_reset_btn,
                      bandicoot_pandda_compare_btn,     bandicoot_pandda_inmtz_btn,
                      bandicoot_pandda_avgmap_btn };
   for (unsigned i = 0; i < G_N_ELEMENTS(w); i++)
      if (w[i]) gtk_widget_set_sensitive(w[i], on);
}

static void bandicoot_pandda_populate_datasets();   // defined below; used by handlers above it

// Run a python driver command, show its returned status in the label, and
// return that status so callers can distinguish success ("PanDDA N/M …") from
// an error ("PanDDA: …").
static std::string bandicoot_pandda_run(const std::string &cmd) {
   std::string s;
#ifdef USE_PYTHON
   PyGILState_STATE g = PyGILState_Ensure();
   PyObject *r = safe_python_command_with_return(cmd);
   if (r && PyString_Check(r))
      s = PyString_AsString(r);
   PyGILState_Release(g);
#endif
   if (bandicoot_pandda_status_label && !s.empty())
      gtk_label_set_text(GTK_LABEL(bandicoot_pandda_status_label), s.c_str());
   return s;
}

// Query a python driver string without disturbing the status label.
static std::string bandicoot_pandda_query(const std::string &cmd) {
   std::string s;
#ifdef USE_PYTHON
   PyGILState_STATE g = PyGILState_Ensure();
   PyObject *r = safe_python_command_with_return(cmd);
   if (r && PyString_Check(r))
      s = PyString_AsString(r);
   PyGILState_Release(g);
#endif
   return s;
}

// Pull the current event's annotations into the Interesting/Confidence/Comment
// widgets. The refreshing flag stops the widgets' own callbacks from writing
// back while we set them programmatically.
static void bandicoot_pandda_refresh_annotations() {
   bandicoot_pandda_refreshing = TRUE;
   if (bandicoot_pandda_interesting_check) {
      std::string v = bandicoot_pandda_query("bandicoot_pandda.get_interesting()");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bandicoot_pandda_interesting_check),
                                   v == "True");
   }
   if (bandicoot_pandda_conf_combo) {
      std::string v = bandicoot_pandda_query("bandicoot_pandda.get_confidence()");
      int idx = 0;
      for (unsigned i = 0; i < G_N_ELEMENTS(bandicoot_pandda_conf_values); i++)
         if (v == bandicoot_pandda_conf_values[i]) { idx = i; break; }
      gtk_combo_box_set_active(GTK_COMBO_BOX(bandicoot_pandda_conf_combo), idx);
   }
   if (bandicoot_pandda_comment_entry) {
      std::string v = bandicoot_pandda_query("bandicoot_pandda.get_comment()");
      if (v == "None") v = "";
      gtk_entry_set_text(GTK_ENTRY(bandicoot_pandda_comment_entry), v.c_str());
   }
   // Dataset Info: split the '|'-joined fields into the value labels.
   if (bandicoot_pandda_info_val[0]) {
      std::string info = bandicoot_pandda_query("bandicoot_pandda.get_info()");
      size_t start = 0;
      for (int i = 0; i < BANDICOOT_PANDDA_NINFO; i++) {
         std::string field;
         if (start <= info.size()) {
            size_t bar = info.find('|', start);
            field = info.substr(start, bar == std::string::npos ? std::string::npos
                                                                : bar - start);
            start = (bar == std::string::npos) ? info.size() + 1 : bar + 1;
         }
         if (i == 0) {            // Crystal (dtag) in bold — the key identifier
            char *esc = g_markup_escape_text(field.c_str(), -1);
            char *mk = g_strdup_printf("<b>%s</b>", esc);
            gtk_label_set_markup(GTK_LABEL(bandicoot_pandda_info_val[i]), mk);
            g_free(mk); g_free(esc);
         } else {
            gtk_label_set_text(GTK_LABEL(bandicoot_pandda_info_val[i]), field.c_str());
         }
      }
   }

   // ---- map checkboxes (basic mode): reflect what is displayed ----
   if (bandicoot_pandda_emap_check) {
      std::string ms = bandicoot_pandda_query("bandicoot_pandda.get_map_state()");
      gboolean b[4] = { FALSE, FALSE, FALSE, FALSE };
      size_t st = 0;
      for (int i = 0; i < 4 && st <= ms.size(); i++) {
         size_t bar = ms.find('|', st);
         std::string tok = ms.substr(st, bar == std::string::npos ? std::string::npos : bar - st);
         b[i] = (tok == "True");
         if (bar == std::string::npos) break;
         st = bar + 1;
      }
      GtkWidget *cb[4] = { bandicoot_pandda_emap_check, bandicoot_pandda_zmap_check,
                           bandicoot_pandda_xray_check, bandicoot_pandda_avg_check };
      for (int i = 0; i < 4; i++)
         if (cb[i]) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cb[i]), b[i]);
   }

   // ---- pandda.inspect-style widgets (NULL in basic mode) ----
   if (bandicoot_pandda_interesting_yes) {
      gboolean on = (bandicoot_pandda_query("bandicoot_pandda.get_interesting()") == "True");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
         on ? bandicoot_pandda_interesting_yes : bandicoot_pandda_interesting_no), TRUE);
   }
   if (bandicoot_pandda_placed_yes) {
      gboolean on = (bandicoot_pandda_query("bandicoot_pandda.get_placed()") == "True");
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
         on ? bandicoot_pandda_placed_yes : bandicoot_pandda_placed_no), TRUE);
   }
   if (bandicoot_pandda_conf_high) {
      std::string v = bandicoot_pandda_query("bandicoot_pandda.get_confidence()");
      GtkWidget *r = bandicoot_pandda_conf_med;            // default bucket
      if (v == bandicoot_pandda_conf_high_tok) r = bandicoot_pandda_conf_high;
      else if (v == bandicoot_pandda_conf_low_tok) r = bandicoot_pandda_conf_low;
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r), TRUE);
   }
   if (bandicoot_pandda_site_name_entry) {
      std::string v = bandicoot_pandda_query("bandicoot_pandda.get_site_name()");
      if (v == "None") v = "";
      gtk_entry_set_text(GTK_ENTRY(bandicoot_pandda_site_name_entry), v.c_str());
   }
   if (bandicoot_pandda_site_comment_entry) {
      std::string v = bandicoot_pandda_query("bandicoot_pandda.get_site_comment()");
      if (v == "None") v = "";
      gtk_entry_set_text(GTK_ENTRY(bandicoot_pandda_site_comment_entry), v.c_str());
   }
   if (bandicoot_pandda_event_n_label) {
      std::string p = bandicoot_pandda_query("bandicoot_pandda.progress()");
      // "ep|et|sp|st"
      int f[4] = {0,0,0,0}; int fi = 0; size_t st = 0;
      while (fi < 4 && st <= p.size()) {
         size_t bar = p.find('|', st);
         std::string tok = p.substr(st, bar == std::string::npos ? std::string::npos : bar - st);
         f[fi++] = atoi(tok.c_str());
         if (bar == std::string::npos) break;
         st = bar + 1;
      }
      char buf[24];
      GtkWidget *cell[4] = { bandicoot_pandda_event_n_label, bandicoot_pandda_event_m_label,
                             bandicoot_pandda_site_n_label,  bandicoot_pandda_site_m_label };
      for (int i = 0; i < 4; i++)
         if (cell[i]) {
            g_snprintf(buf, sizeof(buf), "%d", f[i]);
            gtk_label_set_text(GTK_LABEL(cell[i]), buf);
         }
   }
   if (bandicoot_pandda_ins_info[0]) {
      std::string info = bandicoot_pandda_query("bandicoot_pandda.get_info_inspect()");
      size_t start = 0;
      std::string dtag;
      for (int i = 0; i < BANDICOOT_PANDDA_NINSP; i++) {
         std::string field;
         if (start <= info.size()) {
            size_t bar = info.find('|', start);
            field = info.substr(start, bar == std::string::npos ? std::string::npos
                                                                : bar - start);
            start = (bar == std::string::npos) ? info.size() + 1 : bar + 1;
         }
         if (i == 0) {
            dtag = field;            // Dataset ID in bold — it's the key identifier
            char *esc = g_markup_escape_text(field.c_str(), -1);
            char *mk = g_strdup_printf("<b>%s</b>", esc);
            gtk_label_set_markup(GTK_LABEL(bandicoot_pandda_ins_info[i]), mk);
            g_free(mk); g_free(esc);
         } else {
            gtk_label_set_text(GTK_LABEL(bandicoot_pandda_ins_info[i]), field.c_str());
         }
      }
      // auto-fill "Go to Dataset" with the current dtag so the user can jump
      // between events in the same dataset without retyping it.
      if (bandicoot_pandda_goto_entry)
         gtk_entry_set_text(GTK_ENTRY(bandicoot_pandda_goto_entry), dtag.c_str());
   }
   bandicoot_pandda_refreshing = FALSE;
}

// A titled frame wrapping a child widget (the Krojer-style sectioning).
static GtkWidget *bandicoot_pandda_frame(const char *title, GtkWidget *child) {
   GtkWidget *f = gtk_frame_new(title);
   gtk_container_set_border_width(GTK_CONTAINER(f), 2);
   gtk_container_add(GTK_CONTAINER(f), child);
   return f;
}

// Run a navigation command, then refresh the annotation widgets for the new event.
static void bandicoot_pandda_nav(const char *cmd) {
   bandicoot_pandda_run(cmd);
   bandicoot_pandda_refresh_annotations();
}

// escape \ and ' so a C string can be embedded in a python single-quoted literal
static std::string bandicoot_pandda_py_escape(const char *s) {
   std::string e;
   for (const char *p = s; p && *p; ++p) {
      if (*p == '\\' || *p == '\'') e += '\\';
      e += *p;
   }
   return e;
}

static void bandicoot_pandda_next(GtkButton *b, gpointer u) {
   bandicoot_pandda_nav("bandicoot_pandda.next_event()");
}
static void bandicoot_pandda_prev(GtkButton *b, gpointer u) {
   bandicoot_pandda_nav("bandicoot_pandda.prev_event()");
}
static void bandicoot_pandda_next_site(GtkButton *b, gpointer u) {
   bandicoot_pandda_nav("bandicoot_pandda.next_site()");
}
static void bandicoot_pandda_prev_site(GtkButton *b, gpointer u) {
   bandicoot_pandda_nav("bandicoot_pandda.prev_site()");
}
static void bandicoot_pandda_toggle_emap(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;   // don't re-fire while reflecting state
   bandicoot_pandda_run(gtk_toggle_button_get_active(t)
                        ? "bandicoot_pandda.set_show_emap(True)"
                        : "bandicoot_pandda.set_show_emap(False)");
}
static void bandicoot_pandda_toggle_zmap(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   bandicoot_pandda_run(gtk_toggle_button_get_active(t)
                        ? "bandicoot_pandda.set_show_zmap(True)"
                        : "bandicoot_pandda.set_show_zmap(False)");
}
static void bandicoot_pandda_toggle_xray(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   bandicoot_pandda_run(gtk_toggle_button_get_active(t)
                        ? "bandicoot_pandda.set_show_xray(True)"
                        : "bandicoot_pandda.set_show_xray(False)");
}
static void bandicoot_pandda_toggle_avg(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   bandicoot_pandda_run(gtk_toggle_button_get_active(t)
                        ? "bandicoot_pandda.set_show_average(True)"
                        : "bandicoot_pandda.set_show_average(False)");
}
static void bandicoot_pandda_selection_changed(GtkComboBox *c, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   gchar *txt = gtk_combo_box_get_active_text(c);
   if (txt) {
      bandicoot_pandda_run("bandicoot_pandda.set_selection('"
                           + bandicoot_pandda_py_escape(txt) + "')");
      g_free(txt);
      bandicoot_pandda_refresh_annotations();
      bandicoot_pandda_populate_datasets();   // filter narrowed/changed the view
   }
}
static void bandicoot_pandda_interesting_toggled(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   bandicoot_pandda_run(gtk_toggle_button_get_active(t)
                        ? "bandicoot_pandda.set_interesting(True)"
                        : "bandicoot_pandda.set_interesting(False)");
}
static void bandicoot_pandda_confidence_changed(GtkComboBox *c, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   gchar *txt = gtk_combo_box_get_active_text(c);
   if (txt) {
      bandicoot_pandda_run("bandicoot_pandda.set_confidence('"
                           + bandicoot_pandda_py_escape(txt) + "')");
      g_free(txt);
   }
}
static void bandicoot_pandda_comment_commit(GtkEntry *e) {
   if (bandicoot_pandda_refreshing) return;
   bandicoot_pandda_run("bandicoot_pandda.set_comment('"
                        + bandicoot_pandda_py_escape(gtk_entry_get_text(e)) + "')");
}
static void bandicoot_pandda_comment_activate(GtkEntry *e, gpointer u) {
   bandicoot_pandda_comment_commit(e);
}
static gboolean bandicoot_pandda_comment_focus_out(GtkWidget *w, GdkEventFocus *ev,
                                                   gpointer u) {
   bandicoot_pandda_comment_commit(GTK_ENTRY(w));
   return FALSE;   // let other handlers run
}

// Refill the "Go to" list with the dataset codes of the current selection
// (dataset_list() reflects the active view order, so a filter narrows the list).
static void bandicoot_pandda_populate_datasets() {
   if (!bandicoot_pandda_dataset_store) return;
   gtk_list_store_clear(bandicoot_pandda_dataset_store);
   std::string s = bandicoot_pandda_query("bandicoot_pandda.dataset_list()");
   size_t start = 0;
   while (start < s.size()) {
      size_t nl = s.find('\n', start);
      std::string d = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
      if (!d.empty()) {
         GtkTreeIter it;
         gtk_list_store_append(bandicoot_pandda_dataset_store, &it);
         gtk_list_store_set(bandicoot_pandda_dataset_store, &it, 0, d.c_str(), -1);
      }
      if (nl == std::string::npos) break;
      start = nl + 1;
   }
}
static void bandicoot_pandda_go_selected() {
   if (!bandicoot_pandda_dataset_view) return;
   GtkTreeSelection *sel =
      gtk_tree_view_get_selection(GTK_TREE_VIEW(bandicoot_pandda_dataset_view));
   GtkTreeModel *model = NULL;
   GtkTreeIter it;
   if (gtk_tree_selection_get_selected(sel, &model, &it)) {
      gchar *d = NULL;
      gtk_tree_model_get(model, &it, 0, &d, -1);
      if (d) {
         bandicoot_pandda_run("bandicoot_pandda.go_to_dataset('"
                              + bandicoot_pandda_py_escape(d) + "')");
         g_free(d);
         bandicoot_pandda_refresh_annotations();
      }
   }
}
static void bandicoot_pandda_go_clicked(GtkButton *b, gpointer u) {
   bandicoot_pandda_go_selected();
}
static void bandicoot_pandda_dataset_activated(GtkTreeView *v, GtkTreePath *p,
                                               GtkTreeViewColumn *c, gpointer u) {
   bandicoot_pandda_go_selected();   // double-click / Enter on a row = Go
}
static void bandicoot_pandda_place_ligand(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.place_ligand()");
}
static void bandicoot_pandda_merge_ligand(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.merge_ligand()");
}
static void bandicoot_pandda_save_model(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.save_model()");
}
static void bandicoot_pandda_load_ligand(GtkButton *b, gpointer u) {
   GtkWidget *fc = gtk_file_chooser_dialog_new(
      "Load ligand coordinates", NULL, GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_OPEN,   GTK_RESPONSE_ACCEPT, (char *) NULL);
   GtkFileFilter *ff = gtk_file_filter_new();
   gtk_file_filter_set_name(ff, "Ligand coords (pdb, cif, ent, mol)");
   gtk_file_filter_add_pattern(ff, "*.pdb");   gtk_file_filter_add_pattern(ff, "*.ent");
   gtk_file_filter_add_pattern(ff, "*.cif");   gtk_file_filter_add_pattern(ff, "*.mmcif");
   gtk_file_filter_add_pattern(ff, "*.mol");   gtk_file_filter_add_pattern(ff, "*.mol2");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), ff);
   GtkFileFilter *fa = gtk_file_filter_new();
   gtk_file_filter_set_name(fa, "All files");
   gtk_file_filter_add_pattern(fa, "*");
   gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), fa);
   if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
      char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
      if (path) {
         bandicoot_pandda_run("bandicoot_pandda.load_ligand_file('"
                              + bandicoot_pandda_py_escape(path) + "')");
         g_free(path);
      }
   }
   gtk_widget_destroy(fc);
}
static void bandicoot_pandda_smiles_ligand(GtkButton *b, gpointer u) {
   GtkWidget *d = gtk_dialog_new_with_buttons(
      "Ligand from SMILES", NULL, GTK_DIALOG_MODAL,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_OK,     GTK_RESPONSE_ACCEPT, (char *) NULL);
   GtkWidget *vb = gtk_dialog_get_content_area(GTK_DIALOG(d));
   gtk_container_set_border_width(GTK_CONTAINER(vb), 8);
   GtkWidget *t = gtk_table_new(2, 2, FALSE);
   gtk_table_set_row_spacings(GTK_TABLE(t), 4);
   gtk_table_set_col_spacings(GTK_TABLE(t), 6);
   GtkWidget *l1 = gtk_label_new("3-letter code:");
   gtk_misc_set_alignment(GTK_MISC(l1), 0.0, 0.5);
   GtkWidget *tlc = gtk_entry_new();
   gtk_entry_set_text(GTK_ENTRY(tlc), "LIG");
   gtk_entry_set_max_length(GTK_ENTRY(tlc), 3);
   GtkWidget *l2 = gtk_label_new("SMILES:");
   gtk_misc_set_alignment(GTK_MISC(l2), 0.0, 0.5);
   GtkWidget *smi = gtk_entry_new();
   gtk_entry_set_width_chars(GTK_ENTRY(smi), 36);
   gtk_entry_set_activates_default(GTK_ENTRY(smi), TRUE);
   gtk_table_attach_defaults(GTK_TABLE(t), l1, 0, 1, 0, 1);
   gtk_table_attach_defaults(GTK_TABLE(t), tlc, 1, 2, 0, 1);
   gtk_table_attach_defaults(GTK_TABLE(t), l2, 0, 1, 1, 2);
   gtk_table_attach_defaults(GTK_TABLE(t), smi, 1, 2, 1, 2);
   gtk_box_pack_start(GTK_BOX(vb), t, FALSE, FALSE, 0);
   gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
   gtk_widget_show_all(d);
   if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
      std::string code = bandicoot_pandda_py_escape(gtk_entry_get_text(GTK_ENTRY(tlc)));
      std::string ss   = bandicoot_pandda_py_escape(gtk_entry_get_text(GTK_ENTRY(smi)));
      bandicoot_pandda_run("bandicoot_pandda.ligand_from_smiles('" + ss + "', '" + code + "')");
   }
   gtk_widget_destroy(d);
}
// ---- pandda.inspect-style callbacks ----
static void bandicoot_pandda_next_unviewed_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_nav("bandicoot_pandda.next_unviewed()");
}
static void bandicoot_pandda_next_modelled_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_nav("bandicoot_pandda.next_modelled()");
}
static void bandicoot_pandda_next_save_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.save_model()");
   bandicoot_pandda_nav("bandicoot_pandda.next_event()");
}
static void bandicoot_pandda_interesting_radio(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   // act on the "yes" radio only (its active state == interesting)
   gboolean on = gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(bandicoot_pandda_interesting_yes));
   bandicoot_pandda_run(on ? "bandicoot_pandda.set_interesting(True)"
                           : "bandicoot_pandda.set_interesting(False)");
}
static void bandicoot_pandda_placed_radio(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   gboolean on = gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(bandicoot_pandda_placed_yes));
   bandicoot_pandda_run(on ? "bandicoot_pandda.set_placed(True)"
                           : "bandicoot_pandda.set_placed(False)");
}
static void bandicoot_pandda_conf_radio(GtkToggleButton *t, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   if (!gtk_toggle_button_get_active(t)) return;   // only the newly-selected one
   const char *tok = (const char *) u;
   bandicoot_pandda_run("bandicoot_pandda.set_confidence('" + std::string(tok) + "')");
}
static void bandicoot_pandda_site_name_commit(GtkEntry *e) {
   if (bandicoot_pandda_refreshing) return;
   bandicoot_pandda_run("bandicoot_pandda.set_site_name('"
                        + bandicoot_pandda_py_escape(gtk_entry_get_text(e)) + "')");
}
static void bandicoot_pandda_site_name_activate(GtkEntry *e, gpointer u) {
   bandicoot_pandda_site_name_commit(e);
}
static gboolean bandicoot_pandda_site_name_focus_out(GtkWidget *w, GdkEventFocus *ev,
                                                     gpointer u) {
   bandicoot_pandda_site_name_commit(GTK_ENTRY(w));
   return FALSE;
}
static void bandicoot_pandda_site_comment_commit(GtkEntry *e) {
   if (bandicoot_pandda_refreshing) return;
   bandicoot_pandda_run("bandicoot_pandda.set_site_comment('"
                        + bandicoot_pandda_py_escape(gtk_entry_get_text(e)) + "')");
}
static void bandicoot_pandda_site_comment_activate(GtkEntry *e, gpointer u) {
   bandicoot_pandda_site_comment_commit(e);
}
static gboolean bandicoot_pandda_site_comment_focus_out(GtkWidget *w, GdkEventFocus *ev,
                                                        gpointer u) {
   bandicoot_pandda_site_comment_commit(GTK_ENTRY(w));
   return FALSE;
}
static void bandicoot_pandda_goto_activate(GtkEntry *e, gpointer u) {
   const char *t = gtk_entry_get_text(e);
   if (t && *t) {
      bandicoot_pandda_run("bandicoot_pandda.go_to_dataset('"
                           + bandicoot_pandda_py_escape(t) + "')");
      bandicoot_pandda_refresh_annotations();
   }
}
static void bandicoot_pandda_goto_clicked(GtkButton *b, gpointer u) {
   if (bandicoot_pandda_goto_entry)
      bandicoot_pandda_goto_activate(GTK_ENTRY(bandicoot_pandda_goto_entry), NULL);
}
static void bandicoot_pandda_openlig_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.open_next_ligand()");
}
static void bandicoot_pandda_reload_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.reload_saved_model()");
}
static void bandicoot_pandda_reset_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.reset_to_unfitted()");
}
static void bandicoot_pandda_compare_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.load_unfitted_comparison()");
}
static void bandicoot_pandda_inmtz_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.set_show_xray(True)");
}
static void bandicoot_pandda_avgmap_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.set_show_average(True)");
}
static void bandicoot_pandda_update_html_cb(GtkButton *b, gpointer u) {
   bandicoot_pandda_run("bandicoot_pandda.write_html()");
}
static void bandicoot_pandda_summary_cb(GtkButton *b, gpointer u) {
   std::string p = bandicoot_pandda_query("bandicoot_pandda.summary_html()");
   if (!p.empty() && p.compare(0, 8, "PanDDA: ") != 0) {
      std::string cmd = "open \"" + p + "\"";   // macOS: open in default browser
      int rc = system(cmd.c_str());
      (void) rc;
   }
   if (bandicoot_pandda_status_label && !p.empty())
      gtk_label_set_text(GTK_LABEL(bandicoot_pandda_status_label), p.c_str());
}
static void bandicoot_pandda_quit_cb(GtkButton *b, gpointer u) {
   if (bandicoot_pandda_dialog_widget)
      gtk_widget_destroy(bandicoot_pandda_dialog_widget);
}
// (UI-mode switching is handled by the bottom-bar dropdown, see
//  bandicoot_pandda_uimode_changed / bandicoot_pandda_build_bottom.)

// Load a PanDDA folder by path (shared by the chooser and the --pandda launcher).
static void bandicoot_pandda_load_folder(const char *path) {
   if (!path) return;
   std::string s = bandicoot_pandda_run("bandicoot_pandda.set_folder('"
                                        + bandicoot_pandda_py_escape(path) + "')");
   // success status is "PanDDA N/M …"; an error is "PanDDA: …".
   gboolean loaded = (!s.empty() && s.compare(0, 8, "PanDDA: ") != 0);
   bandicoot_pandda_set_nav_sensitive(loaded);
   if (loaded) {
      bandicoot_pandda_refreshing = TRUE;
      if (bandicoot_pandda_sel_combo)             // basic mode only
         gtk_combo_box_set_active(GTK_COMBO_BOX(bandicoot_pandda_sel_combo), 0);
      bandicoot_pandda_refreshing = FALSE;
      bandicoot_pandda_refresh_annotations();
      bandicoot_pandda_populate_datasets();       // NULL-checks the store internally
   }
}

static void bandicoot_pandda_choose_folder(GtkButton *b, gpointer u) {
   GtkWidget *fc = gtk_file_chooser_dialog_new(
      "Select PanDDA folder", NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_OPEN,   GTK_RESPONSE_ACCEPT, (char *) NULL);
   if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
      char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
      if (path) {
         bandicoot_pandda_load_folder(path);
         g_free(path);
      }
   }
   gtk_widget_destroy(fc);
}
// NULL every per-build widget pointer. Called when the body is rebuilt (mode
// swap) and when the dialog is destroyed, so stale pointers never linger.
static void bandicoot_pandda_null_widgets() {
   bandicoot_pandda_status_label = NULL;
   bandicoot_pandda_prev_btn = NULL;
   bandicoot_pandda_next_btn = NULL;
   bandicoot_pandda_prev_site_btn = NULL;
   bandicoot_pandda_next_site_btn = NULL;
   bandicoot_pandda_emap_check = NULL;
   bandicoot_pandda_zmap_check = NULL;
   bandicoot_pandda_place_btn = NULL;
   bandicoot_pandda_merge_btn = NULL;
   bandicoot_pandda_save_btn = NULL;
   bandicoot_pandda_load_btn = NULL;
   bandicoot_pandda_smiles_btn = NULL;
   bandicoot_pandda_sel_combo = NULL;
   bandicoot_pandda_xray_check = NULL;
   bandicoot_pandda_avg_check = NULL;
   bandicoot_pandda_interesting_check = NULL;
   bandicoot_pandda_conf_combo = NULL;
   bandicoot_pandda_comment_entry = NULL;
   bandicoot_pandda_dataset_view = NULL;
   bandicoot_pandda_dataset_store = NULL;
   bandicoot_pandda_go_btn = NULL;
   for (int i = 0; i < BANDICOOT_PANDDA_NINFO; i++)
      bandicoot_pandda_info_val[i] = NULL;
   // inspect-only:
   bandicoot_pandda_event_n_label = NULL;
   bandicoot_pandda_event_m_label = NULL;
   bandicoot_pandda_site_n_label = NULL;
   bandicoot_pandda_site_m_label = NULL;
   bandicoot_pandda_goto_entry = NULL;
   bandicoot_pandda_uimode_combo = NULL;
   bandicoot_pandda_interesting_yes = NULL;
   bandicoot_pandda_interesting_no = NULL;
   bandicoot_pandda_placed_yes = NULL;
   bandicoot_pandda_placed_no = NULL;
   bandicoot_pandda_conf_high = NULL;
   bandicoot_pandda_conf_med = NULL;
   bandicoot_pandda_conf_low = NULL;
   bandicoot_pandda_site_name_entry = NULL;
   bandicoot_pandda_site_comment_entry = NULL;
   bandicoot_pandda_next_unviewed_btn = NULL;
   bandicoot_pandda_next_modelled_btn = NULL;
   bandicoot_pandda_next_save_btn = NULL;
   bandicoot_pandda_openlig_btn = NULL;
   bandicoot_pandda_reload_btn = NULL;
   bandicoot_pandda_reset_btn = NULL;
   bandicoot_pandda_compare_btn = NULL;
   bandicoot_pandda_inmtz_btn = NULL;
   bandicoot_pandda_avgmap_btn = NULL;
   for (int i = 0; i < BANDICOOT_PANDDA_NINSP; i++)
      bandicoot_pandda_ins_info[i] = NULL;
}
static void bandicoot_pandda_dialog_destroy(GtkObject *o, gpointer u) {
   bandicoot_pandda_null_widgets();
   bandicoot_pandda_body = NULL;
   bandicoot_pandda_dialog_widget = NULL;
}
static void bandicoot_pandda_dialog_response(GtkDialog *d, gint r, gpointer u) {
   gtk_widget_destroy(GTK_WIDGET(d));
}

// ---- Basic (Krojer-style) UI builder: fills the swappable body `vbox` ----
static void bandicoot_pandda_build_basic(GtkWidget *vbox) {
   // ---- PanDDA Folder ----
   GtkWidget *sel = gtk_button_new_with_label("Select PanDDA folder\xe2\x80\xa6");
   g_signal_connect(sel, "clicked", G_CALLBACK(bandicoot_pandda_choose_folder), NULL);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_frame("PanDDA Folder", sel),
                      FALSE, FALSE, 2);

   // ---- Event Selection ----
   GtkWidget *sel_combo = gtk_combo_box_new_text();
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "All events");
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "Sort by Z-score");
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "Sort by cluster size");
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "Sort alphabetically");
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "Unviewed only");
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "Interesting only");
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "Modelled only");
   gtk_combo_box_append_text(GTK_COMBO_BOX(sel_combo), "Not modelled only");
   gtk_combo_box_set_active(GTK_COMBO_BOX(sel_combo), 0);
   g_signal_connect(sel_combo, "changed", G_CALLBACK(bandicoot_pandda_selection_changed), NULL);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_frame("Event Selection", sel_combo),
                      FALSE, FALSE, 2);

   // ---- Dataset Info (title | value rows, filled on each event load) ----
   GtkWidget *info_tbl = gtk_table_new(BANDICOOT_PANDDA_NINFO, 2, FALSE);
   gtk_table_set_row_spacings(GTK_TABLE(info_tbl), 1);
   gtk_table_set_col_spacings(GTK_TABLE(info_tbl), 8);
   gtk_container_set_border_width(GTK_CONTAINER(info_tbl), 3);
   for (int i = 0; i < BANDICOOT_PANDDA_NINFO; i++) {
      GtkWidget *t = gtk_label_new(bandicoot_pandda_info_titles[i]);
      gtk_misc_set_alignment(GTK_MISC(t), 0.0, 0.5);
      GtkWidget *v = gtk_label_new("");
      gtk_misc_set_alignment(GTK_MISC(v), 0.0, 0.5);
      gtk_label_set_selectable(GTK_LABEL(v), TRUE);
      gtk_table_attach(GTK_TABLE(info_tbl), t, 0, 1, i, i + 1, GTK_FILL, GTK_FILL, 0, 0);
      gtk_table_attach(GTK_TABLE(info_tbl), v, 1, 2, i, i + 1,
                       (GtkAttachOptions)(GTK_FILL | GTK_EXPAND), GTK_FILL, 0, 0);
      bandicoot_pandda_info_val[i] = v;
   }
   GtkWidget *info_row = gtk_hbox_new(FALSE, 4);
   gtk_box_pack_start(GTK_BOX(info_row),
                      bandicoot_pandda_frame("Dataset Info", info_tbl), TRUE, TRUE, 0);

   // ---- Go to (jump to a dataset; list reflects the current selection) ----
   GtkWidget *gotobox = gtk_vbox_new(FALSE, 4);
   GtkWidget *dset_scroll = gtk_scrolled_window_new(NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(dset_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
   gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(dset_scroll), GTK_SHADOW_IN);
   gtk_widget_set_size_request(dset_scroll, 130, 132);   // fixed-height list
   bandicoot_pandda_dataset_store = gtk_list_store_new(1, G_TYPE_STRING);
   GtkWidget *dset_view = gtk_tree_view_new_with_model(
                             GTK_TREE_MODEL(bandicoot_pandda_dataset_store));
   g_object_unref(bandicoot_pandda_dataset_store);   // the view now holds the ref
   gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(dset_view), FALSE);
   {
      GtkCellRenderer *rend = gtk_cell_renderer_text_new();
      GtkTreeViewColumn *dcol = gtk_tree_view_column_new_with_attributes(
                                   "Dataset", rend, "text", 0, (char *) NULL);
      gtk_tree_view_append_column(GTK_TREE_VIEW(dset_view), dcol);
   }
   g_signal_connect(dset_view, "row-activated",
                    G_CALLBACK(bandicoot_pandda_dataset_activated), NULL);
   gtk_container_add(GTK_CONTAINER(dset_scroll), dset_view);
   gtk_box_pack_start(GTK_BOX(gotobox), dset_scroll, TRUE, TRUE, 0);
   GtkWidget *go_btn = gtk_button_new_with_label("Go");
   g_signal_connect(go_btn, "clicked", G_CALLBACK(bandicoot_pandda_go_clicked), NULL);
   gtk_box_pack_start(GTK_BOX(gotobox), go_btn, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(info_row), bandicoot_pandda_frame("Go to", gotobox),
                      FALSE, FALSE, 0);
   bandicoot_pandda_dataset_view = dset_view;
   bandicoot_pandda_go_btn = go_btn;

   gtk_box_pack_start(GTK_BOX(vbox), info_row, FALSE, FALSE, 2);

   // ---- Navigator ----
   GtkWidget *navbox = gtk_vbox_new(FALSE, 4);
   GtkWidget *nav = gtk_hbox_new(TRUE, 4);
   GtkWidget *prev = gtk_button_new_with_label("\xe2\x97\x80 Prev event");
   GtkWidget *next = gtk_button_new_with_label("Next event \xe2\x96\xb6");
   g_signal_connect(prev, "clicked", G_CALLBACK(bandicoot_pandda_prev), NULL);
   g_signal_connect(next, "clicked", G_CALLBACK(bandicoot_pandda_next), NULL);
   gtk_box_pack_start(GTK_BOX(nav), prev, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(nav), next, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(navbox), nav, FALSE, FALSE, 0);
   GtkWidget *snav = gtk_hbox_new(TRUE, 4);
   GtkWidget *prev_site = gtk_button_new_with_label("\xe2\x97\x80 Prev site");
   GtkWidget *next_site = gtk_button_new_with_label("Next site \xe2\x96\xb6");
   g_signal_connect(prev_site, "clicked", G_CALLBACK(bandicoot_pandda_prev_site), NULL);
   g_signal_connect(next_site, "clicked", G_CALLBACK(bandicoot_pandda_next_site), NULL);
   gtk_box_pack_start(GTK_BOX(snav), prev_site, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(snav), next_site, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(navbox), snav, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_frame("Navigator", navbox),
                      FALSE, FALSE, 2);

   GtkWidget *maps = gtk_hbox_new(TRUE, 4);
   GtkWidget *emap_check = gtk_check_button_new_with_label("Event map");
   GtkWidget *zmap_check = gtk_check_button_new_with_label("Z-map");
   GtkWidget *xray_check = gtk_check_button_new_with_label("(2)Fo-Fc");
   GtkWidget *avg_check  = gtk_check_button_new_with_label("Average");
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(emap_check), TRUE);  // shown by default
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(zmap_check), TRUE);
   // xray + average default OFF (loaded lazily on first toggle)
   g_signal_connect(emap_check, "toggled", G_CALLBACK(bandicoot_pandda_toggle_emap), NULL);
   g_signal_connect(zmap_check, "toggled", G_CALLBACK(bandicoot_pandda_toggle_zmap), NULL);
   g_signal_connect(xray_check, "toggled", G_CALLBACK(bandicoot_pandda_toggle_xray), NULL);
   g_signal_connect(avg_check,  "toggled", G_CALLBACK(bandicoot_pandda_toggle_avg), NULL);
   gtk_box_pack_start(GTK_BOX(maps), emap_check, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(maps), zmap_check, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(maps), xray_check, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(maps), avg_check,  TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_frame("Toggle Maps", maps),
                      FALSE, FALSE, 2);

   // ---- Ligand Modeling ----
   GtkWidget *ligbox = gtk_vbox_new(FALSE, 4);
   GtkWidget *getlig = gtk_hbox_new(TRUE, 4);
   GtkWidget *load_btn   = gtk_button_new_with_label("Load Ligand\xe2\x80\xa6");
   GtkWidget *smiles_btn = gtk_button_new_with_label("Ligand from SMILES\xe2\x80\xa6");
   g_signal_connect(load_btn,   "clicked", G_CALLBACK(bandicoot_pandda_load_ligand), NULL);
   g_signal_connect(smiles_btn, "clicked", G_CALLBACK(bandicoot_pandda_smiles_ligand), NULL);
   gtk_box_pack_start(GTK_BOX(getlig), load_btn,   TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(getlig), smiles_btn, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(ligbox), getlig, FALSE, FALSE, 0);
   GtkWidget *lig = gtk_hbox_new(TRUE, 4);
   GtkWidget *place_btn = gtk_button_new_with_label("Place Ligand");
   GtkWidget *merge_btn = gtk_button_new_with_label("Merge Ligand");
   g_signal_connect(place_btn, "clicked", G_CALLBACK(bandicoot_pandda_place_ligand), NULL);
   g_signal_connect(merge_btn, "clicked", G_CALLBACK(bandicoot_pandda_merge_ligand), NULL);
   gtk_box_pack_start(GTK_BOX(lig), place_btn, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(lig), merge_btn, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(ligbox), lig, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_frame("Ligand Modeling", ligbox),
                      FALSE, FALSE, 2);

   // ---- Annotation (written to pandda_inspect_events.csv) ----
   GtkWidget *annobox = gtk_vbox_new(FALSE, 4);
   GtkWidget *anno = gtk_hbox_new(FALSE, 6);
   GtkWidget *interesting_check = gtk_check_button_new_with_label("Interesting");
   g_signal_connect(interesting_check, "toggled",
                    G_CALLBACK(bandicoot_pandda_interesting_toggled), NULL);
   gtk_box_pack_start(GTK_BOX(anno), interesting_check, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(anno), gtk_label_new("Confidence:"), FALSE, FALSE, 0);
   GtkWidget *conf_combo = gtk_combo_box_new_text();
   for (unsigned i = 0; i < G_N_ELEMENTS(bandicoot_pandda_conf_values); i++)
      gtk_combo_box_append_text(GTK_COMBO_BOX(conf_combo), bandicoot_pandda_conf_values[i]);
   gtk_combo_box_set_active(GTK_COMBO_BOX(conf_combo), 0);
   g_signal_connect(conf_combo, "changed",
                    G_CALLBACK(bandicoot_pandda_confidence_changed), NULL);
   gtk_box_pack_start(GTK_BOX(anno), conf_combo, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(annobox), anno, FALSE, FALSE, 0);

   GtkWidget *crow = gtk_hbox_new(FALSE, 6);
   gtk_box_pack_start(GTK_BOX(crow), gtk_label_new("Comment:"), FALSE, FALSE, 0);
   GtkWidget *comment_entry = gtk_entry_new();
   g_signal_connect(comment_entry, "activate",
                    G_CALLBACK(bandicoot_pandda_comment_activate), NULL);
   g_signal_connect(comment_entry, "focus-out-event",
                    G_CALLBACK(bandicoot_pandda_comment_focus_out), NULL);
   gtk_box_pack_start(GTK_BOX(crow), comment_entry, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(annobox), crow, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_frame("Annotation", annobox),
                      FALSE, FALSE, 2);

   // ---- Save / Reload / Reset (untitled frame) ----
   GtkWidget *savebox = gtk_hbox_new(TRUE, 4);
   GtkWidget *save_btn   = gtk_button_new_with_label("Save Model");
   GtkWidget *reload_btn = gtk_button_new_with_label("Reload Last Saved Model");
   GtkWidget *reset_btn  = gtk_button_new_with_label("Reset to Unfitted Model");
   g_signal_connect(save_btn,   "clicked", G_CALLBACK(bandicoot_pandda_save_model), NULL);
   g_signal_connect(reload_btn, "clicked", G_CALLBACK(bandicoot_pandda_reload_cb),  NULL);
   g_signal_connect(reset_btn,  "clicked", G_CALLBACK(bandicoot_pandda_reset_cb),   NULL);
   gtk_box_pack_start(GTK_BOX(savebox), save_btn,   TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(savebox), reload_btn, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(savebox), reset_btn,  TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_frame(NULL, savebox),
                      FALSE, FALSE, 2);
   bandicoot_pandda_reload_btn = reload_btn;
   bandicoot_pandda_reset_btn  = reset_btn;

   bandicoot_pandda_prev_btn = prev;
   bandicoot_pandda_next_btn = next;
   bandicoot_pandda_prev_site_btn = prev_site;
   bandicoot_pandda_next_site_btn = next_site;
   bandicoot_pandda_emap_check = emap_check;
   bandicoot_pandda_zmap_check = zmap_check;
   bandicoot_pandda_xray_check = xray_check;
   bandicoot_pandda_avg_check = avg_check;
   bandicoot_pandda_place_btn = place_btn;
   bandicoot_pandda_merge_btn = merge_btn;
   bandicoot_pandda_save_btn = save_btn;
   bandicoot_pandda_load_btn = load_btn;
   bandicoot_pandda_smiles_btn = smiles_btn;
   bandicoot_pandda_sel_combo = sel_combo;
   bandicoot_pandda_interesting_check = interesting_check;
   bandicoot_pandda_conf_combo = conf_combo;
   bandicoot_pandda_comment_entry = comment_entry;

   // ---- shared bottom bar: [ status ][ UI Mode ][ Close ] ----
   bandicoot_pandda_build_bottom(vbox);
}

// A 2-radio yes/no pair sharing one group, stacked vertically (a column);
// returns the box, sets *yes_out/*no_out.
static GtkWidget *bandicoot_pandda_radio_pair(const char *yes, const char *no,
                                              GtkWidget **yes_out, GtkWidget **no_out,
                                              GCallback cb) {
   GtkWidget *box = gtk_vbox_new(FALSE, 2);
   GtkWidget *ry = gtk_radio_button_new_with_label(NULL, yes);
   GtkWidget *rn = gtk_radio_button_new_with_label_from_widget(
                      GTK_RADIO_BUTTON(ry), no);
   g_signal_connect(ry, "toggled", cb, NULL);
   gtk_box_pack_start(GTK_BOX(box), ry, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(box), rn, FALSE, FALSE, 0);
   *yes_out = ry; *no_out = rn;
   return box;
}

// Build a 2-column (title | value) table for a subset of the inspect info
// fields; idxs[] are indices into bandicoot_pandda_ins_titles/_info.
static GtkWidget *bandicoot_pandda_info_table(const int *idxs, int n) {
   GtkWidget *t = gtk_table_new(n, 2, FALSE);
   gtk_table_set_row_spacings(GTK_TABLE(t), 1);
   gtk_table_set_col_spacings(GTK_TABLE(t), 8);
   gtk_container_set_border_width(GTK_CONTAINER(t), 3);
   for (int r = 0; r < n; r++) {
      int i = idxs[r];
      GtkWidget *lt = gtk_label_new(bandicoot_pandda_ins_titles[i]);
      gtk_misc_set_alignment(GTK_MISC(lt), 0.0, 0.5);
      GtkWidget *v = gtk_label_new("");
      gtk_misc_set_alignment(GTK_MISC(v), 0.0, 0.5);
      gtk_label_set_selectable(GTK_LABEL(v), TRUE);
      gtk_table_attach(GTK_TABLE(t), lt, 0, 1, r, r + 1, GTK_FILL, GTK_FILL, 0, 0);
      gtk_table_attach(GTK_TABLE(t), v, 1, 2, r, r + 1,
                       (GtkAttachOptions)(GTK_FILL | GTK_EXPAND), GTK_FILL, 0, 0);
      bandicoot_pandda_ins_info[i] = v;
   }
   return t;
}

// A fully-justified "Label   N   of   M" row (4 equal cells, like the original
// pandda.inspect progress box). Sets *n_out/*m_out to the value cells.
static GtkWidget *bandicoot_pandda_progress_row(const char *title,
                                                GtkWidget **n_out, GtkWidget **m_out) {
   GtkWidget *row = gtk_hbox_new(TRUE, 0);   // homogeneous → 4 equal cells
   GtkWidget *t  = gtk_label_new(title);
   GtkWidget *n  = gtk_label_new("0");
   GtkWidget *of = gtk_label_new("of");
   GtkWidget *m  = gtk_label_new("0");
   gtk_misc_set_alignment(GTK_MISC(t),  0.0, 0.5);
   gtk_misc_set_alignment(GTK_MISC(n),  0.0, 0.5);
   gtk_misc_set_alignment(GTK_MISC(of), 0.0, 0.5);
   gtk_misc_set_alignment(GTK_MISC(m),  0.0, 0.5);
   gtk_box_pack_start(GTK_BOX(row), t,  TRUE, TRUE, 6);
   gtk_box_pack_start(GTK_BOX(row), n,  TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(row), of, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(row), m,  TRUE, TRUE, 0);
   *n_out = n; *m_out = m;
   return row;
}

// Defer a body rebuild to an idle so we never destroy the widget whose signal
// is currently running (the UI-Mode combo lives inside the body).
static gboolean bandicoot_pandda_rebuild_idle(gpointer u) {
   bandicoot_pandda_rebuild_body();
   return FALSE;   // one-shot
}
// UI-Mode dropdown: index 0 = Basic (mode 1), 1 = Full (mode 0). "Expanded"
// (mode 2) will append later.
static void bandicoot_pandda_uimode_changed(GtkComboBox *c, gpointer u) {
   if (bandicoot_pandda_refreshing) return;
   gint idx = gtk_combo_box_get_active(c);
   int target = (idx == 0) ? 1 : (idx == 1) ? 0 : 2;
   if (target != bandicoot_pandda_mode) {
      bandicoot_pandda_mode = target;
      g_idle_add(bandicoot_pandda_rebuild_idle, NULL);
   }
}

// Shared bottom bar (both UIs): the status text sits in its OWN frame, with the
// UI-Mode dropdown and Close button OUTSIDE that frame but aligned on the same
// row to its right (keeps the controls from reading as part of the status text).
static void bandicoot_pandda_build_bottom(GtkWidget *vbox) {
   GtkWidget *row = gtk_hbox_new(FALSE, 6);

   bandicoot_pandda_status_label = gtk_label_new("Select a PanDDA folder to begin.");
   gtk_misc_set_alignment(GTK_MISC(bandicoot_pandda_status_label), 0.0, 0.5);
   gtk_misc_set_padding(GTK_MISC(bandicoot_pandda_status_label), 4, 2);
   gtk_box_pack_start(GTK_BOX(row),
                      bandicoot_pandda_frame(NULL, bandicoot_pandda_status_label),
                      TRUE, TRUE, 0);

   gtk_box_pack_start(GTK_BOX(row), gtk_label_new("UI Mode:"), FALSE, FALSE, 0);
   GtkWidget *combo = gtk_combo_box_new_text();
   gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Basic");
   gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Full");
   gtk_combo_box_append_text(GTK_COMBO_BOX(combo), "Expanded");
   gtk_combo_box_set_active(GTK_COMBO_BOX(combo),
      (bandicoot_pandda_mode == 1) ? 0 : (bandicoot_pandda_mode == 2) ? 2 : 1);
   g_signal_connect(combo, "changed",
                    G_CALLBACK(bandicoot_pandda_uimode_changed), NULL);
   bandicoot_pandda_uimode_combo = combo;
   gtk_box_pack_start(GTK_BOX(row), combo, FALSE, FALSE, 0);

   GtkWidget *close = gtk_button_new_with_label("Close");
   g_signal_connect(close, "clicked", G_CALLBACK(bandicoot_pandda_quit_cb), NULL);
   gtk_box_pack_start(GTK_BOX(row), close, FALSE, FALSE, 0);

   // bottom spacer so nothing sits flush with the panel's lower edge
   GtkWidget *align = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
   gtk_alignment_set_padding(GTK_ALIGNMENT(align), 2, 6, 2, 2);
   gtk_container_add(GTK_CONTAINER(align), row);
   gtk_box_pack_start(GTK_BOX(vbox), align, FALSE, FALSE, 2);
}

// ---- pandda.inspect-style UI builder: fills the swappable body `vbox` ----
static void bandicoot_pandda_build_inspect(GtkWidget *vbox) {
   // ===== top row: [Quit/Summary/HTML] [Progress] [Go-to-Dataset + site nav] =====
   GtkWidget *top = gtk_hbox_new(FALSE, 6);

   // -- Quit / Summary / Update HTML, framed, separated by separators --
   GtkWidget *btncol = gtk_vbox_new(FALSE, 0);
   GtkWidget *quit = gtk_button_new_with_label("Quit");
   GtkWidget *summary = gtk_button_new_with_label("Summary");
   GtkWidget *uphtml = gtk_button_new_with_label("Update HTML");
   g_signal_connect(quit,    "clicked", G_CALLBACK(bandicoot_pandda_quit_cb), NULL);
   g_signal_connect(summary, "clicked", G_CALLBACK(bandicoot_pandda_summary_cb), NULL);
   g_signal_connect(uphtml,  "clicked", G_CALLBACK(bandicoot_pandda_update_html_cb), NULL);
   gtk_box_pack_start(GTK_BOX(btncol), quit, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(btncol), gtk_hseparator_new(), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(btncol), summary, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(btncol), gtk_hseparator_new(), FALSE, FALSE, 3);
   gtk_box_pack_start(GTK_BOX(btncol), uphtml, FALSE, FALSE, 0);
   // centre the three buttons vertically (don't pin them to the top)
   GtkWidget *btnalign = gtk_alignment_new(0.5, 0.5, 1.0, 0.0);
   gtk_container_add(GTK_CONTAINER(btnalign), btncol);
   gtk_box_pack_start(GTK_BOX(top), bandicoot_pandda_frame(NULL, btnalign), FALSE, FALSE, 0);

   // -- Overall Inspection progress: title + Event/Site rows, each in an
   //    etched sub-frame; the rows are fully justified ("Event  N  of  M") --
   GtkWidget *progv = gtk_vbox_new(FALSE, 3);
   GtkWidget *progtitle = gtk_label_new("Overall Inspection Event/Site Progress:");
   gtk_misc_set_padding(GTK_MISC(progtitle), 6, 2);
   gtk_box_pack_start(GTK_BOX(progv), bandicoot_pandda_frame(NULL, progtitle),
                      FALSE, FALSE, 0);
   GtkWidget *erow = bandicoot_pandda_progress_row("Event",
                        &bandicoot_pandda_event_n_label, &bandicoot_pandda_event_m_label);
   GtkWidget *srow = bandicoot_pandda_progress_row("Site",
                        &bandicoot_pandda_site_n_label, &bandicoot_pandda_site_m_label);
   gtk_box_pack_start(GTK_BOX(progv), bandicoot_pandda_frame(NULL, erow), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(progv), bandicoot_pandda_frame(NULL, srow), FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(top), bandicoot_pandda_frame(NULL, progv), FALSE, FALSE, 0);

   // -- Go to Dataset (inline label, not a frame title), with the four
   //    site/quick-nav buttons framed beneath --
   GtkWidget *rightcol = gtk_vbox_new(FALSE, 4);
   GtkWidget *gotorow = gtk_hbox_new(FALSE, 4);
   gtk_box_pack_start(GTK_BOX(gotorow), gtk_label_new("Go to Dataset:"), FALSE, FALSE, 0);
   bandicoot_pandda_goto_entry = gtk_entry_new();
   g_signal_connect(bandicoot_pandda_goto_entry, "activate",
                    G_CALLBACK(bandicoot_pandda_goto_activate), NULL);
   GtkWidget *gobtn = gtk_button_new_with_label("Go");
   g_signal_connect(gobtn, "clicked", G_CALLBACK(bandicoot_pandda_goto_clicked), NULL);
   gtk_box_pack_start(GTK_BOX(gotorow), bandicoot_pandda_goto_entry, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(gotorow), gobtn, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(rightcol), gotorow, FALSE, FALSE, 0);

   GtkWidget *snav = gtk_table_new(2, 2, TRUE);
   gtk_table_set_row_spacings(GTK_TABLE(snav), 2);
   gtk_table_set_col_spacings(GTK_TABLE(snav), 2);
   GtkWidget *ps = gtk_button_new_with_label("<<< Go to Prev Site <<<");
   GtkWidget *ns = gtk_button_new_with_label(">>> Go to Next Site >>>");
   GtkWidget *nu = gtk_button_new_with_label(">>> Go to Next Unviewed >>>");
   GtkWidget *nm = gtk_button_new_with_label(">>> Go to Next Modelled >>>");
   g_signal_connect(ps, "clicked", G_CALLBACK(bandicoot_pandda_prev_site), NULL);
   g_signal_connect(ns, "clicked", G_CALLBACK(bandicoot_pandda_next_site), NULL);
   g_signal_connect(nu, "clicked", G_CALLBACK(bandicoot_pandda_next_unviewed_cb), NULL);
   g_signal_connect(nm, "clicked", G_CALLBACK(bandicoot_pandda_next_modelled_cb), NULL);
   gtk_table_attach_defaults(GTK_TABLE(snav), bandicoot_pandda_frame(NULL, ps), 0, 1, 0, 1);
   gtk_table_attach_defaults(GTK_TABLE(snav), bandicoot_pandda_frame(NULL, ns), 1, 2, 0, 1);
   gtk_table_attach_defaults(GTK_TABLE(snav), bandicoot_pandda_frame(NULL, nu), 0, 1, 1, 2);
   gtk_table_attach_defaults(GTK_TABLE(snav), bandicoot_pandda_frame(NULL, nm), 1, 2, 1, 2);
   gtk_box_pack_start(GTK_BOX(rightcol), snav, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(top), rightcol, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 2);
   bandicoot_pandda_prev_site_btn = ps;
   bandicoot_pandda_next_site_btn = ns;
   bandicoot_pandda_next_unviewed_btn = nu;
   bandicoot_pandda_next_modelled_btn = nm;

   // ===== event navigation (don't-save / save) =====
   GtkWidget *enav = gtk_hbox_new(TRUE, 4);
   GtkWidget *pv = gtk_button_new_with_label("<<< Prev <<<\n(Don't Save Model)");
   GtkWidget *nx = gtk_button_new_with_label(">>> Next >>>\n(Don't Save Model)");
   GtkWidget *nxs = gtk_button_new_with_label(">>> Next >>>\n(Save Model)");
   g_signal_connect(pv,  "clicked", G_CALLBACK(bandicoot_pandda_prev), NULL);
   g_signal_connect(nx,  "clicked", G_CALLBACK(bandicoot_pandda_next), NULL);
   g_signal_connect(nxs, "clicked", G_CALLBACK(bandicoot_pandda_next_save_cb), NULL);
   gtk_box_pack_start(GTK_BOX(enav), pv,  TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(enav), nx,  TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(enav), nxs, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), enav, FALSE, FALSE, 2);
   bandicoot_pandda_prev_btn = pv;
   bandicoot_pandda_next_btn = nx;
   bandicoot_pandda_next_save_btn = nxs;

   // ===== dataset/event info (two columns, Dataset ID spanning) + modeling =====
   GtkWidget *midrow = gtk_hbox_new(FALSE, 6);
   GtkWidget *infocol = gtk_vbox_new(FALSE, 3);
   // Dataset ID frame, spanning the two info columns below it
   GtkWidget *didrow = gtk_hbox_new(FALSE, 8);
   GtkWidget *didt = gtk_label_new(bandicoot_pandda_ins_titles[0]);   // "Dataset ID"
   gtk_misc_set_alignment(GTK_MISC(didt), 0.0, 0.5);
   bandicoot_pandda_ins_info[0] = gtk_label_new("");
   gtk_misc_set_alignment(GTK_MISC(bandicoot_pandda_ins_info[0]), 0.0, 0.5);
   gtk_label_set_selectable(GTK_LABEL(bandicoot_pandda_ins_info[0]), TRUE);
   gtk_box_pack_start(GTK_BOX(didrow), didt, FALSE, FALSE, 4);
   gtk_box_pack_start(GTK_BOX(didrow), bandicoot_pandda_ins_info[0], TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(infocol), bandicoot_pandda_frame(NULL, didrow), FALSE, FALSE, 0);
   // two-column Event Information | Dataset Information
   GtkWidget *cols = gtk_hbox_new(TRUE, 4);
   static const int ev_idx[] = { 1, 2, 3, 4 };   // Event#, 1-BDC, Z-blob Peak, Z-blob Size
   static const int ds_idx[] = { 5, 6, 7, 8 };   // Resolution, Map Uncertainty, R-Free, R-Work
   gtk_box_pack_start(GTK_BOX(cols),
      bandicoot_pandda_frame("Event Information", bandicoot_pandda_info_table(ev_idx, 4)),
      TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(cols),
      bandicoot_pandda_frame("Dataset Information", bandicoot_pandda_info_table(ds_idx, 4)),
      TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(infocol), cols, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(midrow), infocol, TRUE, TRUE, 0);

   GtkWidget *modtbl = gtk_table_new(3, 2, TRUE);
   gtk_table_set_row_spacings(GTK_TABLE(modtbl), 2);
   gtk_table_set_col_spacings(GTK_TABLE(modtbl), 4);
   GtkWidget *mg  = gtk_button_new_with_label("Merge Ligand\nWith Model");
   GtkWidget *mv  = gtk_button_new_with_label("Move New\nLigand Here");
   GtkWidget *ol  = gtk_button_new_with_label("Open Next\nLigand");
   GtkWidget *sv  = gtk_button_new_with_label("Save Model");
   GtkWidget *rl  = gtk_button_new_with_label("Reload Last\nSaved Model");
   GtkWidget *rs  = gtk_button_new_with_label("Reset to\nUnfitted Model");
   g_signal_connect(mg, "clicked", G_CALLBACK(bandicoot_pandda_merge_ligand), NULL);
   g_signal_connect(mv, "clicked", G_CALLBACK(bandicoot_pandda_place_ligand), NULL);
   g_signal_connect(ol, "clicked", G_CALLBACK(bandicoot_pandda_openlig_cb), NULL);
   g_signal_connect(sv, "clicked", G_CALLBACK(bandicoot_pandda_save_model), NULL);
   g_signal_connect(rl, "clicked", G_CALLBACK(bandicoot_pandda_reload_cb), NULL);
   g_signal_connect(rs, "clicked", G_CALLBACK(bandicoot_pandda_reset_cb), NULL);
   gtk_table_attach_defaults(GTK_TABLE(modtbl), mg, 0, 1, 0, 1);
   gtk_table_attach_defaults(GTK_TABLE(modtbl), sv, 1, 2, 0, 1);
   gtk_table_attach_defaults(GTK_TABLE(modtbl), mv, 0, 1, 1, 2);
   gtk_table_attach_defaults(GTK_TABLE(modtbl), rl, 1, 2, 1, 2);
   gtk_table_attach_defaults(GTK_TABLE(modtbl), ol, 0, 1, 2, 3);
   gtk_table_attach_defaults(GTK_TABLE(modtbl), rs, 1, 2, 2, 3);
   gtk_box_pack_start(GTK_BOX(midrow), modtbl, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), midrow, FALSE, FALSE, 2);
   bandicoot_pandda_merge_btn = mg;
   bandicoot_pandda_place_btn = mv;
   bandicoot_pandda_openlig_btn = ol;
   bandicoot_pandda_save_btn = sv;
   bandicoot_pandda_reload_btn = rl;
   bandicoot_pandda_reset_btn = rs;

   // ===== Record Event Information =====
   GtkWidget *evbox = gtk_vbox_new(FALSE, 3);
   GtkWidget *cr = gtk_hbox_new(FALSE, 6);
   gtk_box_pack_start(GTK_BOX(cr), gtk_label_new("Event Comment:"), FALSE, FALSE, 0);
   bandicoot_pandda_comment_entry = gtk_entry_new();
   g_signal_connect(bandicoot_pandda_comment_entry, "activate",
                    G_CALLBACK(bandicoot_pandda_comment_activate), NULL);
   g_signal_connect(bandicoot_pandda_comment_entry, "focus-out-event",
                    G_CALLBACK(bandicoot_pandda_comment_focus_out), NULL);
   gtk_box_pack_start(GTK_BOX(cr), bandicoot_pandda_comment_entry, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(evbox), cr, FALSE, FALSE, 0);

   // three columns of radio stacks: Interesting | Ligand-Placed | Confidence
   GtkWidget *radios = gtk_hbox_new(TRUE, 12);
   gtk_box_pack_start(GTK_BOX(radios),
      bandicoot_pandda_radio_pair("Mark Event as Interesting", "Mark Event as Not Interesting",
                                  &bandicoot_pandda_interesting_yes,
                                  &bandicoot_pandda_interesting_no,
                                  G_CALLBACK(bandicoot_pandda_interesting_radio)),
      TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(radios),
      bandicoot_pandda_radio_pair("Ligand Placed", "No Ligand Placed",
                                  &bandicoot_pandda_placed_yes,
                                  &bandicoot_pandda_placed_no,
                                  G_CALLBACK(bandicoot_pandda_placed_radio)),
      TRUE, TRUE, 0);
   // confidence: 3 radios in one group, stacked
   GtkWidget *confbox = gtk_vbox_new(FALSE, 2);
   bandicoot_pandda_conf_high = gtk_radio_button_new_with_label(NULL, "High Confidence");
   bandicoot_pandda_conf_med  = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(bandicoot_pandda_conf_high), "Medium Confidence");
   bandicoot_pandda_conf_low  = gtk_radio_button_new_with_label_from_widget(
      GTK_RADIO_BUTTON(bandicoot_pandda_conf_high), "Low Confidence");
   g_signal_connect(bandicoot_pandda_conf_high, "toggled",
                    G_CALLBACK(bandicoot_pandda_conf_radio),
                    (gpointer) bandicoot_pandda_conf_high_tok);
   g_signal_connect(bandicoot_pandda_conf_med, "toggled",
                    G_CALLBACK(bandicoot_pandda_conf_radio),
                    (gpointer) bandicoot_pandda_conf_med_tok);
   g_signal_connect(bandicoot_pandda_conf_low, "toggled",
                    G_CALLBACK(bandicoot_pandda_conf_radio),
                    (gpointer) bandicoot_pandda_conf_low_tok);
   gtk_box_pack_start(GTK_BOX(confbox), bandicoot_pandda_conf_high, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(confbox), bandicoot_pandda_conf_med,  FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(confbox), bandicoot_pandda_conf_low,  FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(radios), confbox, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(evbox), radios, FALSE, FALSE, 0);
   GtkWidget *evframe = bandicoot_pandda_frame("Record Event Information (this event only)",
                                               evbox);
   gtk_frame_set_label_align(GTK_FRAME(evframe), 0.5, 0.5);   // centred header
   gtk_box_pack_start(GTK_BOX(vbox), evframe, FALSE, FALSE, 2);

   // ---- Record Site Information ----
   GtkWidget *stbl = gtk_table_new(2, 2, FALSE);
   gtk_table_set_row_spacings(GTK_TABLE(stbl), 2);
   gtk_table_set_col_spacings(GTK_TABLE(stbl), 6);
   gtk_container_set_border_width(GTK_CONTAINER(stbl), 3);
   GtkWidget *snl = gtk_label_new("Name:");
   gtk_misc_set_alignment(GTK_MISC(snl), 0.0, 0.5);
   bandicoot_pandda_site_name_entry = gtk_entry_new();
   g_signal_connect(bandicoot_pandda_site_name_entry, "activate",
                    G_CALLBACK(bandicoot_pandda_site_name_activate), NULL);
   g_signal_connect(bandicoot_pandda_site_name_entry, "focus-out-event",
                    G_CALLBACK(bandicoot_pandda_site_name_focus_out), NULL);
   GtkWidget *scl = gtk_label_new("Comment:");
   gtk_misc_set_alignment(GTK_MISC(scl), 0.0, 0.5);
   bandicoot_pandda_site_comment_entry = gtk_entry_new();
   g_signal_connect(bandicoot_pandda_site_comment_entry, "activate",
                    G_CALLBACK(bandicoot_pandda_site_comment_activate), NULL);
   g_signal_connect(bandicoot_pandda_site_comment_entry, "focus-out-event",
                    G_CALLBACK(bandicoot_pandda_site_comment_focus_out), NULL);
   gtk_table_attach(GTK_TABLE(stbl), snl, 0, 1, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
   gtk_table_attach_defaults(GTK_TABLE(stbl), bandicoot_pandda_site_name_entry, 1, 2, 0, 1);
   gtk_table_attach(GTK_TABLE(stbl), scl, 0, 1, 1, 2, GTK_FILL, GTK_FILL, 0, 0);
   gtk_table_attach_defaults(GTK_TABLE(stbl), bandicoot_pandda_site_comment_entry, 1, 2, 1, 2);
   GtkWidget *stframe = bandicoot_pandda_frame(
      "Record Site Information (for all events with this site)", stbl);
   gtk_frame_set_label_align(GTK_FRAME(stframe), 0.5, 0.5);   // centred header
   gtk_box_pack_start(GTK_BOX(vbox), stframe, FALSE, FALSE, 2);

   // ===== Miscellaneous: centred button group (UI Mode now lives in bottom bar) =====
   GtkWidget *misc = gtk_hbox_new(FALSE, 4);
   GtkWidget *center = gtk_hbox_new(FALSE, 4);
   GtkWidget *folder = gtk_button_new_with_label("Select PanDDA folder\xe2\x80\xa6");
   GtkWidget *inmtz  = gtk_button_new_with_label("Load input mtz file");
   GtkWidget *avgmap = gtk_button_new_with_label("Load average map");
   GtkWidget *unfit  = gtk_button_new_with_label("Load unfitted model\n(for comparison only)");
   g_signal_connect(folder, "clicked", G_CALLBACK(bandicoot_pandda_choose_folder), NULL);
   g_signal_connect(inmtz,  "clicked", G_CALLBACK(bandicoot_pandda_inmtz_cb), NULL);
   g_signal_connect(avgmap, "clicked", G_CALLBACK(bandicoot_pandda_avgmap_cb), NULL);
   g_signal_connect(unfit,  "clicked", G_CALLBACK(bandicoot_pandda_compare_cb), NULL);
   gtk_box_pack_start(GTK_BOX(center), folder, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(center), inmtz,  FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(center), avgmap, FALSE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(center), unfit,  FALSE, FALSE, 0);
   // centre the button group horizontally (expand TRUE, fill FALSE)
   GtkWidget *misc_row = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(misc_row), center, TRUE, FALSE, 0);
   gtk_box_pack_start(GTK_BOX(misc), misc_row, TRUE, TRUE, 0);
   // bottom spacer so the buttons aren't flush with the frame's lower edge
   GtkWidget *misc_align = gtk_alignment_new(0.0, 0.0, 1.0, 1.0);
   gtk_alignment_set_padding(GTK_ALIGNMENT(misc_align), 2, 6, 2, 2);
   gtk_container_add(GTK_CONTAINER(misc_align), misc);
   GtkWidget *miscframe = bandicoot_pandda_frame("Miscellaneous", misc_align);
   gtk_frame_set_label_align(GTK_FRAME(miscframe), 0.5, 0.5);   // centred header
   gtk_box_pack_start(GTK_BOX(vbox), miscframe, FALSE, FALSE, 2);
   bandicoot_pandda_inmtz_btn = inmtz;
   bandicoot_pandda_avgmap_btn = avgmap;
   bandicoot_pandda_compare_btn = unfit;

   // ---- shared bottom bar: [ status ][ UI Mode ][ Close ] ----
   bandicoot_pandda_build_bottom(vbox);
}

// Clear the body, build the current mode into it, and restore loaded state.
static void bandicoot_pandda_rebuild_body() {
   if (!bandicoot_pandda_body) return;
   GList *kids = gtk_container_get_children(GTK_CONTAINER(bandicoot_pandda_body));
   for (GList *l = kids; l; l = l->next)
      gtk_widget_destroy(GTK_WIDGET(l->data));
   g_list_free(kids);
   bandicoot_pandda_null_widgets();
   // Suppress widget callbacks while building: radio/combo/check widgets emit
   // toggled/changed on creation (default-active), which would otherwise write
   // default annotations over the loaded event before we refresh from disk.
   bandicoot_pandda_refreshing = TRUE;
   if (bandicoot_pandda_mode == 1)
      bandicoot_pandda_build_basic(bandicoot_pandda_body);
   else
      bandicoot_pandda_build_inspect(bandicoot_pandda_body);
   gtk_widget_show_all(bandicoot_pandda_body);
   bandicoot_pandda_refreshing = FALSE;
   // The Basic UI is more compact than the inspect UI; ask the window to
   // shrink back to the new body's natural size (GTK clamps up to the min).
   if (bandicoot_pandda_dialog_widget)
      gtk_window_resize(GTK_WINDOW(bandicoot_pandda_dialog_widget), 1, 1);
   // restore enabled state + current annotations for the freshly-built widgets
   gboolean loaded = (bandicoot_pandda_query("bandicoot_pandda.is_loaded()") == "True");
   bandicoot_pandda_set_nav_sensitive(loaded);
   if (loaded) {
      bandicoot_pandda_refresh_annotations();
      // carry the loaded-dataset status across a mode swap (don't revert to
      // the "Select a PanDDA folder to begin." placeholder).
      if (bandicoot_pandda_status_label) {
         std::string s = bandicoot_pandda_query("bandicoot_pandda.status()");
         if (!s.empty())
            gtk_label_set_text(GTK_LABEL(bandicoot_pandda_status_label), s.c_str());
      }
   }
}

extern "C" void bandicoot_pandda_dialog() {
   if (! graphics_info_t::use_graphics_interface_flag) return;
#ifdef USE_PYTHON
   // import once so the button commands are single expressions (their return
   // value reaches the status label, and they aren't double-executed).
   safe_python_command("import bandicoot_pandda");
#endif
   // Soften the global LJ epsilon so PanDDA ligand real-space refinement isn't
   // over-restrained against the protein (mirrors the shim's startup default;
   // Coot 0.9 exposes only a global epsilon). Applied once when Inspect opens.
   set_refinement_lennard_jones_epsilon(BANDICOOT_PANDDA_LJ_EPSILON);
   // Already open? Just raise it.
   if (bandicoot_pandda_dialog_widget) {
      gtk_window_present(GTK_WINDOW(bandicoot_pandda_dialog_widget));
      return;
   }
   // No transient parent: the Inspect dialog is its own top-level window so it
   // does NOT move together with the main Bandicoot window. No action-area
   // button — Close lives in the body's bottom bar (bandicoot_pandda_build_bottom).
   GtkWidget *dialog =
      gtk_dialog_new_with_buttons("PanDDA Inspect", NULL,
                                  (GtkDialogFlags) 0, (char *) NULL);
   bandicoot_pandda_dialog_widget = dialog;
   GtkWidget *vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
   gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);
   bandicoot_pandda_body = gtk_vbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox), bandicoot_pandda_body, TRUE, TRUE, 0);

   bandicoot_pandda_mode = bandicoot_load_pandda_uimode();   // pref default (0 Full)
   bandicoot_pandda_rebuild_body();

   g_signal_connect(dialog, "response", G_CALLBACK(bandicoot_pandda_dialog_response), NULL);
   g_signal_connect(dialog, "destroy",  G_CALLBACK(bandicoot_pandda_dialog_destroy), NULL);
   gtk_widget_show_all(dialog);
}

// Entry point for the --pandda launcher: open the dialog and load `dir`.
extern "C" void bandicoot_pandda_dialog_with_folder(const char *dir) {
   bandicoot_pandda_dialog();
   if (dir && *dir)
      bandicoot_pandda_load_folder(dir);
}
#endif // __APPLE__

void  do_edit_copy_molecule() {

#ifdef __APPLE__
   bandicoot_molecule_chooser_dialog("Copy Molecule", "Molecule to copy:",
                                     FALSE, NULL, BANDICOOT_CHOOSER_COPY_MOLECULE);
   return;
#endif

#ifdef USE_PYTHON
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = coot::STATE_PYTHON;
#endif
#else // python not used
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = 0;
#endif
#endif   

#ifdef USE_GUILE

   // std::string cmd = "(molecule-chooser-gui \"Molecule to Copy...\" (lambda (imol) (copy-molecule imol)))";

   std::string cmd =
      "(generic-chooser-and-entry-and-checkbutton \"Copy Molecule\" \"Selection\" \"/\" \"Move Molecule Here?\" (lambda (imol text button-state) (let ((imol-new (copy-molecule imol))) (if button-state (move-molecule-to-screen-centre imol-new) (valid-model-molecule? imol-new)))))";

   if (state_lang == coot::STATE_SCM) {
      safe_scheme_command(cmd);
   }
#else
#ifdef USE_PYTHON
   if (state_lang == coot::STATE_PYTHON) {

      std::string cmd = "molecule_chooser_gui(\"Molecule to Copy...\", lambda imol: copy_molecule(imol))";
      safe_python_command(cmd);
   }
#endif // PYTHON
#endif // GUILE

}

void  do_edit_copy_fragment() {

#ifdef __APPLE__
   bandicoot_molecule_chooser_dialog("Copy Fragment",
                                     "Copy a fragment from which molecule?",
                                     TRUE, "//A/1-10", BANDICOOT_CHOOSER_COPY_FRAGMENT);
   return;
#endif

#ifdef USE_PYTHON
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = coot::STATE_PYTHON;
#endif
#else // python not used
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = 0;
#endif
#endif   

#ifdef USE_GUILE
   std::string cmd = "(generic-chooser-and-entry-and-checkbutton \"From which molecule shall we copy the fragment?\" \"Atom selection for fragment\" \"//A/1-10\" \"Move new molecule here?\" (lambda (imol text button-state) (let ((imol (new-molecule-by-atom-selection imol text))) (if button-state (move-molecule-to-screen-centre imol)) (valid-model-molecule? imol))) #f)";

   if (state_lang == coot::STATE_SCM) {
      std::ofstream f("debug.scm");
      f.write(cmd.c_str(), cmd.size());
      f.write("\n", 1);
      f.close();
      safe_scheme_command(cmd);
   }
#else
#ifdef USE_PYTHON
   if (state_lang == coot::STATE_PYTHON) {

      // This is a tricky, long winded one. We first make a function which is
      // then executed and all has to reside within exec as we cannot have
      // multiple line statements in python... lets try
      std::string cmd = "exec(\'def atom_selection_from_fragment_func(imol, text, button_state): \\n \\t jmol = new_molecule_by_atom_selection(imol, text) \\n \\t if button_state: move_molecule_to_screen_centre(jmol) \\n \\t return valid_model_molecule_qm(jmol) \\ngeneric_chooser_and_entry_and_check_button(\"From which molecule shall we copy the fragment?\", \"Atom selection for fragment\", \"//A/1-10\", \"Move new molecule here?\", lambda imol, text, button_state: atom_selection_from_fragment_func(imol, text, button_state), False)\')";
//                         exec('def atom_selection_from_fragment_func(imol, text, button_state): \n \t jmol = new_molecule_by_atom_selection(imol, text) \n \t if button_state: move_molecule_to_screen_centre(jmol) \n \t return valid_model_molecule_qm(jmol) \ngeneric_chooser_and_entry_and_check_button("From which molecule shall we copy the fragment?", "Atom selection for fragment", "//A/1-10", "Move new molecule here?", lambda imol, text, button_state: atom_selection_from_fragment_func(imol, text, button_state), False)')
      safe_python_command(cmd);
   }
#endif // PYTHON
#endif // GUILE

}

void  do_edit_replace_residue() {

#ifdef USE_PYTHON
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = coot::STATE_PYTHON;
#endif
#else // python not used
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = 0;
#endif
#endif   

#ifdef USE_GUILE
   std::string cmd = "(generic-single-entry \"Replace this residue with residue of type:\" \"ALA\" \"Mutate\" (lambda (text) (using-active-atom (mutate-by-overlap aa-imol aa-chain-id aa-res-no text))))";
   if (state_lang == coot::STATE_SCM) {
      safe_scheme_command(cmd);
   }
#else
#ifdef USE_PYTHON
   if (state_lang == coot::STATE_PYTHON) {

      std::string cmd = "generic_single_entry(\"Replace this residue with residue of type:\", \"ALA\", \"Mutate\", lambda text: using_active_atom(mutate_by_overlap, \"aa_imol\", \"aa_chain_id\", \"aa_res_no\", text))";
      safe_python_command(cmd);
   }
#endif // PYTHON
#endif // GUILE

}

void  do_edit_replace_fragment() {


#ifdef USE_PYTHON
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = coot::STATE_PYTHON;
#endif
#else // python not used
#ifdef USE_GUILE
   short int state_lang = coot::STATE_SCM;
#else    
   short int state_lang = 0;
#endif
#endif   

#ifdef USE_GUILE
   std::string cmd =
      "(molecule-chooser-gui \"Define the molecule that needs updating\" (lambda (imol-base) (generic-chooser-and-entry \"Molecule that contains the new fragment:\" \"Atom Selection\" \"//\" (lambda (imol-fragment atom-selection-str) (replace-fragment imol-base imol-fragment atom-selection-str)))))";
   if (state_lang == coot::STATE_SCM) {
      safe_scheme_command(cmd);
   }
#else
#ifdef USE_PYTHON
   if (state_lang == coot::STATE_PYTHON) {

      std::string cmd =
         "molecule_chooser_gui(\"Define the molecule that needs updating\", lambda imol_base: generic_chooser_and_entry(\"Molecule that contains the new fragment:\", \"Atom Selection\", \"//\", lambda imol_fragment, atom_selection_str: replace_fragment(imol_base, imol_fragment, atom_selection_str)))";
      safe_python_command(cmd);
   }
#endif // PYTHON
#endif // GUILE
}


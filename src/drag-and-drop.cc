/* src/drag-and-drop.cc
 * 
 * Copyright 2010 by the University of Oxford
 * Copyright 2013 by Medical Research Council
 * Author: Paul Emsley
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

#if defined (USE_PYTHON)
#include "Python.h"  // before system includes to stop "POSIX_C_SOURCE" redefined problems
#endif

#include <iostream>
#include <gtk/gtk.h>
#include "drag-and-drop.hh"
#include "coot-utils/gemmi-coords.hh"   // classify_cif_file()
#include "graphics-info.h"

#ifdef USE_GUILE   
#include <cstdio> // for std::FILE in gmp.h for libguile.h
#include <libguile.h>		/* for SCM type (returned by safe_scheme_command) */
#endif

#include "cc-interface.hh"
#include "c-interface.h"

gboolean
on_gl_canvas_drag_drop(GtkWidget *widget,
		       GdkDragContext *context,
		       gint x, gint y,
		       guint time,
		       gpointer user_data) {

   gboolean is_valid_drop_site = TRUE;

   // The message here used to read "ERROR:: null dnd context", which was wrong
   // twice over: the context is not null (only its target list is empty), and
   // nothing ever checked for a null context despite the wording -- so if one
   // HAD arrived, context->targets would have dereferenced it. Now it checks
   // what it claims to check and says what actually happened.
   //
   // An empty target list is not an error. A drop can legitimately offer no
   // data formats we asked for, and on macOS the drop handler can fire more
   // than once for a single gesture, so this fires routinely after a drop that
   // has already been handled -- which is how it came to be printed after a
   // successful file load.
   if (! context) {
      std::cout << "WARNING:: drag-drop event with no drag context" << std::endl;
      return FALSE;
   }

   if (context->targets) {
      GdkAtom target_type =
	 GDK_POINTER_TO_ATOM(g_list_nth_data(context->targets, TARGET_STRING));

      gtk_drag_get_data(widget, context,
			target_type,    /* the target type we want (a string) */
			time);
   }
   return  is_valid_drop_site;
}

void
on_drag_data_received (GtkWidget *widget, 
		       GdkDragContext *context, 
		       gint x, gint y,
		       GtkSelectionData *selection_data, 
		       guint target_type, 
		       guint time,
		       gpointer data) {

   gboolean dnd_success = FALSE;
   gboolean delete_selection_data = FALSE;
   
   // Deal with what the source sent over
   if((selection_data != NULL) && (selection_data-> length >= 0)) {
      std::string uri_string;
      if (target_type == TEXT_URL) {
         // we have an url to deal with
         uri_string = (gchar *)selection_data-> data;
         dnd_success = handle_drag_and_drop_string(uri_string);
      }
      else if (target_type == TEXT_URI) {
         // we have a text uri (file!?)
         gchar **uris;
         gint i = 0;
         gchar *res = 0;
         uris = g_uri_list_extract_uris((gchar*)selection_data-> data);
         if (uris) {
            while (uris[i] != 0) {
               res = g_filename_from_uri(uris[i], NULL, NULL);
               i++;
               if (res != NULL) {
                  // everything fine
                  dnd_success = handle_drag_and_drop_single_item((gchar *)res);
               } else {
                  // not a file (shouldnt necessary happen - urls are dealt above and 
                  // simple strings below
                  uri_string = (gchar *)selection_data-> data;
                  dnd_success = handle_drag_and_drop_string(uri_string);
               }
            }
         }
         g_free(res);
         g_strfreev(uris);
      }
      else if (target_type == TARGET_STRING) {
         // simple string could call an extra function here too
         uri_string = (gchar *)selection_data-> data;
         dnd_success = handle_drag_and_drop_string(uri_string);
      }
      delete_selection_data = TRUE;
      
   }
   gtk_drag_finish (context, dnd_success, delete_selection_data, time);
}


//! \brief handle the string that get when an URL (or text from an url) is dropped.
//
// uri string can be a concatenation of string with \ns between them
// (and a \n to end)
// 
int handle_drag_and_drop_string(const std::string &uri_in) {

   int handled = FALSE;
   bool tried_already = false;
   std::string uri = uri_in;
   std::string url = uri_in;

   // std::cout << ":::::::::::::::: handle_drag_and_drop_string(" << uri_in << ")" << std::endl;

   if (! tried_already) {
      // OK, was it an HTTP type string?
      if (url.length() > 9) {

	 if (url.substr(0,7) == "http://" || url.substr(0,8) == "https://") {
	    tried_already = true;
	    int l = url.length();
	    if (url[l-1] == '\n') { 
	       // std::cout << "extra \\n" << std::endl;
	       url = url.substr(0, l-1);
	    }

	    l = url.length();
	    int c = url[l-1];
	    if (url[l-1] == '\r') { 
	       // std::cout << "extra \\r" << std::endl;
	       url = url.substr(0, l-1);
	    }
	    
	    
	    int status = make_directory_maybe("coot-download");
	    if (status == 0) { // OK, we made it (or had it)
	       std::string url_file_name_file = url;

	       std::string ext = coot::util::file_name_extension(url);
               
	       if (ext == ".png") {
		  // special rule - convert the url of the png to that
		  // of an accession code.
		  if (url.find("/PDBimages/")) {
		     std::pair<std::string, std::string> s =
			coot::util::split_string_on_last_slash(url);
		     std::pair<std::string, std::string> ss =
			coot::util::split_string_on_last_slash(s.first);
		     std::pair<std::string, std::string> sss =
			coot::util::split_string_on_last_slash(ss.first);
		     tried_already = true;
		     handled = FALSE;
		     if (ss.second.length() == 2) {
			if (sss.second.length() == 2) {
			   std::string code;
			   code += ss.second[0];
			   code += sss.second;
			   code += ss.second[1];
			   get_coords_for_accession_code(code.c_str());
			}
		     }
		  }

	       } else {

		  // it was coords or mtz - we presume
		  std::string::size_type pos = url.find_last_of('/');
		  if (pos != std::string::npos) {
		     // normal path
		     url_file_name_file = url.substr(pos);
		  }
		  std::string file_name =
		     coot::util::append_dir_file("coot-download", url_file_name_file);
		  coot_get_url(url.c_str(), file_name.c_str());
		  handled = handle_drag_and_drop_single_item(file_name);
	       }
	    }
	 }
      }
   }

   if (! tried_already) {
      // was it a 4-letter (accession number)?
      int l = uri_in.length();
      if (l == 4) {
	 get_coords_for_accession_code(uri_in.c_str());
	 tried_already = true;
	 handled = TRUE;
      } 
   }

   if (! tried_already) {
      std::cout << "here at the end of handle_drag_and_drop_string() " << std::endl;
      if (coot::file_exists(url)) {
	 handled = handle_drag_and_drop_single_item(url);
      } 
   } 
   return handled;
}

int handle_drag_and_drop_single_item(const std::string &file_name) {

   int handled = FALSE;
   // std::cout << "handle_drag_and_drop_single_item() " << file_name << ":" << std::endl; 

   // BANDICOOT v0.2: classify a CIF by CONTENT, not by extension.
   //
   // This used to be `if (ext == ".cif") read_cif_dictionary(...)`, taking a
   // non-zero bond count as proof the file WAS a dictionary. It is not proof:
   // every wwPDB coordinate mmCIF carries a _chem_comp_bond connectivity loop,
   // so the test succeeded on exactly the files it should have rejected, the
   // coordinate branch below was never reached, and the file's DISTANCE-LESS
   // bonds overwrote the monomer library at IMOL_ENC_ANY -- i.e. for every
   // molecule. RSR then silently made zero restraints, for the rest of the
   // session, cured only by restarting. It took two days to find.
   //
   // (The bond count is not to blame either -- c-interface.h documents
   // "> 0 can be treated as success", and this code followed that. Read it as
   // "how many bonds did I find", never as "was this a dictionary".)
   // Case-INSENSITIVE, and looking beneath a .gz: ".mmCIF" is an ordinary way
   // to name these (the wwPDB converter emits exactly that), and a
   // case-sensitive test silently skipped the classifier for such files.
   // gemmi_handles_file() already does both, so use it rather than repeating
   // the extension list here.
   std::string ext = coot::util::file_name_extension(file_name);
   if (coot::gemmi_handles_file(file_name)) {
      switch (coot::classify_cif_file(file_name)) {

      case coot::cif_flavour_t::restraints:
	 // Two very different files land here, and dropping them should not do
	 // the same thing.
	 //
	 // A dictionary you can refine with (elbow, acedrg, the Refmac monomer
	 // library) carries _chem_comp_bond.value_dist. Importing it is the whole
	 // point -- the user already has the ligand and wants restraints for it.
	 //
	 // A wwPDB/PDBe COMPONENT DEFINITION (AR6.cif and friends) carries bond
	 // orders and coordinates but NO distances. Importing that as a dictionary
	 // is what dropping it used to do, and since read_cif_dictionary()
	 // succeeded, the drop counted as "handled" and NOTHING VISIBLE HAPPENED.
	 // What the user wants from a ligand downloaded off the PDB is the ligand,
	 // so read it as coordinates instead.
	 // No dialog here about the missing restraints. handle_read_draw_molecule
	 // already looks the component up in the monomer library (CCP4's, if the
	 // environment points at one) and warns if it comes back empty -- so
	 // saying it here as well would be a second dialog for one drop, and
	 // WRONG in the case that matters: when the library does supply
	 // restraints, this file's lack of them is irrelevant.
	 if (! coot::cif_chem_comp_has_bond_distances(file_name)) {
	    std::string comp_id = coot::cif_chem_comp_id(file_name);
	    if (! comp_id.empty()) {
	       std::cout << "INFO:: " << comp_id << " is a chemical component "
			 << "definition (coordinates and bond orders, no bond "
			 << "distances) - reading it as coordinates" << std::endl;
	       handle_read_draw_molecule_with_recentre(file_name.c_str(), 0);
	       handled = TRUE;
	       break;
	    }
	 }
	 if (read_cif_dictionary(file_name.c_str()) > 0)
	    handled = TRUE;
	 break;

      case coot::cif_flavour_t::structure_factors:
	 // Phases decide which reader, not whether to read: a deposited SF file
	 // usually has none and is phased from a model.
	 if (! coot::cif_structure_factors_have_amplitudes(file_name)) {
	    // Intensities only. clipper wants F, so Coot would import nothing
	    // and render a map of zeros without a word of complaint.
	    info_dialog("This file contains reflection INTENSITIES but no "
			"amplitudes.\n\n"
			"Bandicoot needs structure-factor amplitudes (F) to "
			"calculate a map, and cannot convert intensities to "
			"amplitudes.\n\n"
			"Convert the file first (for example with gemmi or "
			"ctruncate), or use an MTZ.");
	    handled = TRUE;
	 } else if (coot::cif_structure_factors_have_phases(file_name)) {
	    if (auto_read_cif_data_with_phases(file_name.c_str()) >= 0)
	       handled = TRUE;
	 } else {
	    int imol_coords = first_coords_imol();
	    if (is_valid_model_molecule(imol_coords)) {
	       int imol_map = read_cif_data(file_name.c_str(), imol_coords);
	       if (imol_map >= 0) {
		  handled = TRUE;
		  // CHECK THE OUTCOME rather than trying to predict it.
		  //
		  // clipper's CIF importer accepts _refln.F_meas_au /
		  // F_meas_sigma_au. It silently ignores the CIF-core spelling
		  // (amplitude_meas) and intensities (intensity_meas) alike,
		  // producing a map of zeros with no complaint -- and the
		  // console's "myfsigf has N data" prints identically whether
		  // the read worked or not, so it cannot be used to tell.
		  // Enumerating every accepted vocabulary would be guesswork;
		  // an empty map is unambiguous.
		  if (is_valid_map_molecule(imol_map)) {
		     if (graphics_info_t::molecules[imol_map].map_sigma() <= 0.0) {
			std::string m = file_name;
			m += "\n\nNo usable reflection data was found in this "
			     "file, so the map is empty.\n\n"
			     "Bandicoot reads amplitudes named _refln.F_meas_au "
			     "and _refln.F_meas_sigma_au. Files using "
			     "_refln.intensity_meas or _refln.amplitude_meas are "
			     "not recognised and must be converted.";
			std::cout << "WARNING:: " << m << std::endl;
			info_dialog(m.c_str());
		     }
		  }
	       }
	    } else {
	       info_dialog("This file contains structure factors but no phases.\n\n"
			   "Load a model first, so that phases can be calculated "
			   "from it.");
	       handled = TRUE;   // reported to the user; do not fall through
	    }
	 }
	 break;

      case coot::cif_flavour_t::coordinates:
	 break;   // fall through to the coordinate branch below

      case coot::cif_flavour_t::unknown:
	 info_dialog("Cannot determine the type of this mmCIF file.\n\n"
		     "It does not contain coordinates (_atom_site), "
		     "restraints (_chem_comp_atom) or structure factors "
		     "(_refln).");
	 handled = TRUE;   // reported to the user; do not guess
	 break;
      }
   }

   if (handled == FALSE) { 
      std::string ext_tmp = coot::util::file_name_extension(file_name);
      if (file_type_coords(file_name.c_str())) {
	 int imol = read_pdb(file_name.c_str());
	 if (is_valid_model_molecule(imol))
	    handled = TRUE;
	 else
	    std::cout << "INFO:: " << file_name << " was not a coordinates file" << std::endl;
      } else { 
	 std::string ext = coot::util::file_name_extension(file_name);
	 if (ext == ".mtz") {
	    std::vector<int> imol_map = auto_read_make_and_draw_maps(file_name.c_str());
	    if (is_valid_map_molecule(imol_map.front()))
	       handled = TRUE;
	 }
      }
   }
   return handled;
}

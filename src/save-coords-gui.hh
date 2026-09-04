/* src/save-coords-gui.hh
 *
 * Bandicoot v0.2: the GTK2 half of the Save Coordinates dialog.
 *
 * THIS IS THE FILE A wx PORT REPLACES. Everything about WHAT the dialog offers
 * and what the answers MEAN lives in save-coords-options.hh, which has no
 * toolkit in it; this file only builds widgets and reads them back.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef COOT_SAVE_COORDS_GUI_HH
#define COOT_SAVE_COORDS_GUI_HH

#include <gtk/gtk.h>
#include "save-coords-options.hh"

//! Create the Save Coordinates chooser, for either of its two uses.
//
//! ONE chooser serves both "Save Coordinates" and "Save Symmetry Coordinates";
//! set initial.is_symmetry (and the operator/shift fields) for the latter. The
//! options are copied onto the widget and freed with it.
GtkWidget *coot_save_coords_chooser_new(const coot::save_coords_options_t &initial);

//! Read the options back off \a chooser, filename included, and save.
//
//! Returns 0 on success. Safe to call on a chooser that has no options
//! attached, in which case it reports the error and returns non-zero.
int coot_save_coords_chooser_execute(GtkWidget *chooser);

//! Put \a basename in the name entry, with its extension made to match the
//! currently selected format.
void coot_save_coords_chooser_set_name(GtkWidget *chooser, const char *basename);

//! Make the name already in the entry agree with the selected format.
//
//! Call after something else has set the name -- set_file_for_save_fileselection()
//! in particular -- so the entry does not open showing an extension that
//! contradicts the menu.
void coot_save_coords_chooser_sync_name(GtkWidget *chooser);

#endif // COOT_SAVE_COORDS_GUI_HH

/* src/restraints-gui.hh
 *
 * Bandicoot v0.2: the load-time ligand restraints notification.
 *
 * THIS IS THE FILE A wx PORT REPLACES. It builds widgets and reads them back;
 * the chemistry is in python/bandicoot_restraints.py and the collision tests
 * are in coot-utils/comp-id-collision.hh, neither of which knows about a
 * toolkit.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef COOT_RESTRAINTS_GUI_HH
#define COOT_RESTRAINTS_GUI_HH

#include <string>
#include <vector>

//! Report that \a imol loaded without restraints for \a comp_ids, and offer to
//! generate them.
//
//! Adds rows to the one shared notification dialog, creating it if needed and
//! raising it if it already exists. Call it once per coordinate load, from the
//! point where the lookup ladder has finished and \a comp_ids is whatever
//! survived it.
//
//! Does nothing without a graphics interface, and nothing when the user has
//! turned the ligand restraint warnings off -- the caller still writes the
//! same detail to stdout either way, so a headless or a silenced session
//! keeps its record.
void bandicoot_restraints_notify(int imol, const std::vector<std::string> &comp_ids);

//! Open (or raise) the dialog showing EVERY ligand, on demand.
//
//! Backs Modelling -> Generate Ligand Restraints. Same dialog as the load-time
//! one, switched into show-all: every ligand in every loaded molecule appears,
//! and anything that already has restraints arrives unticked, so ticking it is
//! the user asking for those restraints to be replaced.
void bandicoot_restraints_dialog();

//! Import a restraints CIF, applying it to every loaded molecule it FITS.
//
//! Backs Auto on the Import CIF dictionary dialog, and a dropped dictionary.
//! Matching is by atom names and connectivity, not by comp id, so a file still
//! finds its molecules after a placeholder rename.
//
//! Applied SCOPED to each matching molecule rather than globally, which is what
//! lets an imported CIF supersede a generated one: same scope, new read number.
//! A global import would lose to it, because the lookup tries an exact-scope
//! match first.
//
//! Several matches ask which, unless Preferences -> Others -> Ligands says to
//! apply to all. No match reads it unscoped, since a dictionary is often read
//! before its coordinates. Returns what handle_cif_dictionary_for_molecule()
//! returns, i.e. > 0 on success.
int bandicoot_import_restraints_sweep(const char *file_name, short int new_molecule_flag);

#endif // COOT_RESTRAINTS_GUI_HH

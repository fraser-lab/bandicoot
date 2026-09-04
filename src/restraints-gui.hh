/* src/restraints-gui.hh
 *
 * Bandicoot v0.2: the load-time ligand restraints notification.
 *
 * A wx port replaces this file: it builds widgets and reads them back. The
 * chemistry is in the python restraints module and the collision tests are in
 * coot-utils, neither of which knows about a toolkit.
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

//! Report that \a imol loaded without restraints for \a comp_ids, and offer
//! to generate them.
//
//! Adds rows to the one shared dialog, creating or raising it as needed. Call
//! once per coordinate load, with whatever survived the dictionary lookup.
//! Does nothing without a graphics interface or when the ligand restraint
//! warnings are switched off; the caller still reports to stdout either way.
void bandicoot_restraints_notify(int imol, const std::vector<std::string> &comp_ids);

//! Open the dialog showing every ligand, on demand.
//
//! Same dialog, switched to list every ligand in every loaded molecule rather
//! than only what lacks a dictionary. Anything already described arrives
//! unticked, so ticking it asks for those restraints to be replaced.
void bandicoot_restraints_dialog();

//! Import a restraints CIF, applying it to every loaded molecule it fits.
//
//! Matching is by atom names and connectivity rather than component id, so a
//! file still finds its molecules after a rename. Applied scoped to each
//! match, which is what lets an imported dictionary supersede a generated one.
//! Several matches ask which, unless the preferences say to apply to all; no
//! match reads it unscoped, since a dictionary is often read before its
//! coordinates.
int bandicoot_import_restraints_sweep(const char *file_name, short int new_molecule_flag);

#endif // COOT_RESTRAINTS_GUI_HH

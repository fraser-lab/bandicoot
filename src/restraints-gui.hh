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

//! Generate restraints for whatever \a imol still lacks them, on demand.
//
//! Backs Modelling -> Generate Ligand Restraints. Needs no dialog to be open,
//! and says so plainly when the molecule has nothing missing -- an explicit
//! request deserves an answer either way, unlike the load-time path where
//! silence is the good news.
void bandicoot_generate_restraints_for_molecule(int imol);

//! Open (or raise) the notification dialog on demand.
//
//! Same dialog, same live contents; it simply does not wait for a load. There
//! is no menu item for this yet -- the "Generate restraints" button per
//! molecule in the Display Manager is Scope 3 work -- but the entry point is
//! here so that adding one is a one-line change.
void bandicoot_restraints_dialog();

#endif // COOT_RESTRAINTS_GUI_HH

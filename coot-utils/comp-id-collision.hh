/* coot-utils/comp-id-collision.hh
 *
 * Copyright 2026 by Bandicoot contributors
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 3 of the License, or (at
 *    your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * ---------------------------------------------------------------------------
 * ONE ROOT CAUSE: A COMP ID DOES NOT UNIQUELY IDENTIFY A CHEMISTRY.
 *
 * Restraints in Coot are GLOBAL and keyed by comp id -- adding a dictionary for
 * a comp id replaces whatever was there before (mon_lib_add_chem_comp() in
 * geometry/cif-parse-mon-lib.cc compares read numbers and clears the old entry
 * on a new read). That is the property a user's custom CIF relies on to
 * override a generated one, and it is the same property that breaks a
 * structure holding two different molecules under one name.
 *
 * The failure has three faces, and they are the same bug seen from three
 * sides:
 *
 *   1. Two chemically different ligands are merged into one structure, both
 *      called LIG. Only the one merged second ends up correctly restrained.
 *   2. Restraint generation extracts every residue of a comp id and hands them
 *      to the generator as if they were copies of one entity, so the emitted
 *      dictionary silently describes only one of them.
 *   3. A novel ligand is given a 3-letter code that already exists in the CCD.
 *      A dictionary IS found, so the "no restraints" warning never fires, and
 *      the restraints belong to somebody else's molecule.
 *
 * All three are detectable by comparing ATOM NAMES, because that is what Coot
 * actually binds restraints with: dict_bond_restraint_t names its two atoms by
 * string, so a dictionary whose atom ids do not match the model is inert.
 *
 * ---------------------------------------------------------------------------
 * WHY THE CONFLICT TEST IS "NEITHER SET CONTAINS THE OTHER", NOT "THE SETS
 * DIFFER"
 *
 * Two copies of one ligand routinely have different atom-name sets: disorder,
 * partial occupancy and unmodelled ends all remove atoms from one copy and not
 * the other. Treating any difference as a collision would refuse to generate
 * restraints for a great many perfectly ordinary structures.
 *
 * A missing atom always makes one set a SUBSET of the other. Two different
 * chemistries under one name generally do not nest: each carries at least one
 * atom name the other lacks. So nesting is read as "same chemistry, one copy
 * less complete" and non-nesting as "different chemistries".
 *
 * This is deliberately conservative in the direction that costs least. A false
 * negative here means silently wrong restraints; a false positive means asking
 * the user to rename something. The nesting rule can miss a collision where one
 * ligand's atom names happen to be a strict subset of the other's -- so the
 * generator does not rely on this test alone, it also derives its restraints
 * from a SINGLE representative residue rather than from every copy at once.
 * ---------------------------------------------------------------------------
 */

#ifndef COOT_UTILS_COMP_ID_COLLISION_HH
#define COOT_UTILS_COMP_ID_COLLISION_HH

#include <string>
#include <vector>

#include <mmdb2/mmdb_manager.h>

#include "geometry/protein-geometry.hh"

namespace coot {

   namespace comp_id_collision {

      // ------------------------------------------------------------ placeholders

      // The wwPDB reserves these and will never issue them as real CCD ids:
      // 01-99, DRG, INH, LIG. They exist so that a ligand can be named during
      // structure determination and recognised as novel at deposition -- which
      // is exactly why they collide: every depositor is handed the same ones.
      bool is_reserved_placeholder(const std::string &comp_id);

      // Reserved placeholder codes not currently used in mol, in the order they
      // should be handed out (LIG, DRG, INH, then 01..99). Used to suggest a
      // replacement code that is guaranteed not to be a real component.
      std::vector<std::string> free_placeholder_codes(mmdb::Manager *mol);

      // ------------------------------------------------------------ residues

      // A heavy atom's name, element and position -- what the connectivity
      // comparison needs. Hydrogens are deliberately excluded: they are often
      // absent, are added by whichever program touched the file last, ride on
      // their parent, and their positions differ between copies of one
      // molecule. Including them produced a FALSE POSITIVE on a real
      // two-copy structure (a 1.88 A H-H contact read as a bond), measured
      // 2026-08-31. Heavy-atom connectivity is what defines the chemistry.
      class atom_pos_t {
      public:
         std::string name;
         std::string element;
         double x, y, z;
         atom_pos_t() : x(0), y(0), z(0) {}
      };

      // One residue of a comp id, with enough to identify it to an atom
      // selection and to compare its chemistry with another's.
      class residue_ref_t {
      public:
         std::string chain_id;
         int res_no;
         std::string ins_code;
         std::vector<std::string> atom_names; // sorted, unique, alt confs folded
         std::vector<atom_pos_t> heavy_atoms; // for the connectivity comparison
         residue_ref_t() : res_no(mmdb::MinInt4) {}
         bool ok() const { return res_no != mmdb::MinInt4; }
         // An mmdb CID for this residue, for new_molecule_by_atom_selection().
         std::string atom_selection_string() const;
         std::string label() const; // "A/101" -- for a message
      };

      std::vector<residue_ref_t> residues_of_comp_id(mmdb::Manager *mol,
                                                     const std::string &comp_id);

      // The residue to derive restraints from: the one with the most atoms.
      // Deriving from a single copy means several copies cannot be mistaken for
      // one molecule however the comparison below turns out.
      residue_ref_t most_complete_residue(mmdb::Manager *mol,
                                          const std::string &comp_id);

      // ------------------------------------------------------------ collisions

      // True when each of a and b holds at least one atom name the other does
      // not. See the header comment for why subset relations are not conflicts.
      bool atom_name_sets_conflict(const std::vector<std::string> &a,
                                   const std::vector<std::string> &b);

      // Do these two residues have different HEAVY-ATOM CONNECTIVITY over the
      // atom names they share?
      //
      // This exists because the name test above is nearly blind on exactly the
      // molecules that matter. Novel ligands get sequential auto-generated
      // names (C1..C9, N1, O1) from elbow or acedrg, so two DIFFERENT ligands
      // of similar size nest almost every time and the name test stays silent.
      // Measured on a real pair, 2026-08-31: the smaller ligand's names were a
      // strict subset of the larger's (the only difference was C10), yet 9 of
      // their 11 shared atoms had different bonded neighbours.
      //
      // Bonds are perceived from interatomic distance with element-aware
      // cutoffs and compared only over shared names, so a missing atom does not
      // by itself register as a difference.
      //
      // CONNECTIVITY, not a distance matrix: bond lengths do not change when a
      // torsion rotates, whereas through-space distances do. On the pair above
      // 58% of through-space distances differed by more than 0.5 A -- a
      // distance-matrix test would fire on every flexible ligand in two
      // conformations.
      bool connectivity_differs(const residue_ref_t &a, const residue_ref_t &b);

      // Are these two residues mirror images at some chiral centre?
      //
      // Stereoisomers have IDENTICAL names and IDENTICAL connectivity, so both
      // tests above are blind to them -- and they are not an exotic case: they
      // are the very thing the PanDDA shim's unique-comp-id strategy was
      // invented for (R -> LIR, S -> LIS). Measured 2026-08-31 on seven R/S
      // pairs from a real fragment campaign: name test silent, connectivity
      // test silent, every one of them a genuinely different molecule.
      //
      // Compares the SIGN of the chiral volume at each shared atom having at
      // least three heavy neighbours, with the neighbours name-sorted so the
      // ordering means the same thing in both residues. Near-planar centres are
      // skipped, where the sign is noise rather than handedness.
      bool chirality_differs(const residue_ref_t &a, const residue_ref_t &b);

      // The decider: name sets conflict, the shared atoms are bonded
      // differently, or they are mirror images. Use this rather than any one
      // test alone -- each is blind to what the others catch.
      //
      // NOTE: there is NO test that separates "two different molecules" from
      // "two copies, one missing an atom" in every case, and this one does not
      // claim to. It is a heuristic that must be allowed to be wrong, which is
      // why every caller offers the user a way past it.
      bool chemistries_conflict(const residue_ref_t &a, const residue_ref_t &b);

      class collision_t {
      public:
         std::string comp_id;
         residue_ref_t a;
         residue_ref_t b;
         bool ok() const { return ! comp_id.empty(); }
         std::string message() const; // one line, for a dialog or stdout
      };

      // The first conflicting pair of residues sharing comp_id, if any.
      collision_t find_collision(mmdb::Manager *mol, const std::string &comp_id);

      // The residues under one comp id, partitioned into distinct chemistries.
      //
      // A residue joins an existing group when it does NOT conflict with that
      // group's representative -- so a less complete copy of a molecule groups
      // with the fuller one, which is the same subset rule used everywhere
      // here. The representative is the group's most complete member, and
      // group[0] holds the largest chemistry found.
      //
      // One group means one chemistry, i.e. no collision. More than one is
      // what a forced rename has to separate.
      std::vector<std::vector<residue_ref_t> >
      distinct_chemistry_groups(mmdb::Manager *mol, const std::string &comp_id);

      // Every non-standard comp id in mol that describes more than one
      // chemistry. Standard residue types are skipped: an incompletely modelled
      // side chain legitimately gives two residues of one type different atom
      // sets, and in ways that do not always nest.
      std::vector<collision_t> find_collisions(mmdb::Manager *mol);

      // Would combining these two molecules put two chemistries under one comp
      // id? Checked BEFORE a merge, because afterwards the damage is done: the
      // dictionary can only describe one of them, and which one it describes
      // depends on merge order rather than on anything the user chose.
      //
      // Only comp ids present in BOTH molecules can collide this way, so this
      // is not the same as running find_collisions() on the result.
      std::vector<collision_t> find_collisions_between(mmdb::Manager *mol_a,
                                                       mmdb::Manager *mol_b);

      // --------------------------------------------- matching a dictionary
      //
      // A restraints CIF names its component, and that name can be wrong for
      // the model in front of it -- most obviously after distinct molecules
      // sharing a placeholder name have been renamed apart, when the user's own
      // LIG.cif no longer matches anything. The dictionary is then bound to
      // nothing at all.
      //
      // NOTE: THAT IS THE BETTER FAILURE, and the reason is worth keeping: Art,
      // 2026-08-31 -- "sometimes binding to nothing is better than binding to
      // the wrong thing. This way, I noticed the problem." A dictionary bound
      // to the wrong ligand is silently wrong; one bound to nothing announces
      // itself. Do not "improve" this into a silent best-guess.
      //
      // A real restraints CIF carries what is needed to find the right
      // component without trusting its name: _chem_comp_atom gives the atom
      // names and _chem_comp_bond gives the connectivity.

      // Does this dictionary describe this residue? Requires that every heavy
      // atom of the residue is named in the dictionary, and that the two agree
      // about which of those atoms are bonded.
      //
      // The model's bonds are perceived from coordinates, so a distance in the
      // ambiguous band counts as agreement either way -- the same tolerance for
      // a deformed ligand that the collision test uses.
      bool dictionary_describes_residue(const dictionary_residue_restraints_t &dict,
                                        const residue_ref_t &res);

      // Which comp ids in mol does this dictionary actually describe?
      //
      // May legitimately return more than one: two components that a dictionary
      // matches equally well are chemically indistinguishable, and are
      // therefore very likely the same molecule under two names. Art's ruling,
      // 2026-08-31: apply it to all of them.
      std::vector<std::string>
      comp_ids_matching_dictionary(mmdb::Manager *mol,
                                   const dictionary_residue_restraints_t &dict);

      // ------------------------------------------------------- dictionary cover

      // Does the dictionary found for comp_id actually describe the atoms the
      // model has? A dictionary that resolves but does not cover is worse than
      // none, because nothing further will warn about it.
      class coverage_t {
      public:
         std::string comp_id;
         std::vector<std::string> model_atoms_missing_from_dictionary;
         unsigned int n_model_atoms;
         unsigned int n_matched;
         coverage_t() : n_model_atoms(0), n_matched(0) {}
         bool covered() const { return model_atoms_missing_from_dictionary.empty(); }
         // Nothing in common at all -- the dictionary is for another molecule.
         bool disjoint() const { return n_model_atoms > 0 && n_matched == 0; }
         std::string message() const;
      };

      // imol_enc is the dictionary-scope encoding to look the comp id up with,
      // as passed to protein_geometry::get_monomer_restraints().
      coverage_t dictionary_coverage(mmdb::Manager *mol,
                                     const std::string &comp_id,
                                     const protein_geometry &geom,
                                     int imol_enc);

      // Every non-standard comp id in mol that HAS a dictionary which fails to
      // cover the model's atom names. Comp ids with no dictionary at all are not
      // reported here -- they are already reported elsewhere.
      std::vector<coverage_t> dictionary_coverage_failures(mmdb::Manager *mol,
                                                           const protein_geometry &geom,
                                                           int imol_enc);
   }
}

#endif // COOT_UTILS_COMP_ID_COLLISION_HH

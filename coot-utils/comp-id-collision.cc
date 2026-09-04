/* coot-utils/comp-id-collision.cc
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
 * See comp-id-collision.hh for what this is for and why the conflict test is
 * shaped the way it is.
 */

#include "comp-id-collision.hh"

#include "coot-coord-utils.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace {

   // Atom names are compared TRIMMED on both sides.
   //
   // The model's names come from mmdb (4 columns, PDB padding) and the
   // dictionary's from atom_id_4c, which is meant to be the same convention --
   // but padding is exactly where the readers have been seen to disagree (a
   // 3-character hydrogen name pads differently depending on who wrote it), and
   // within one residue no two atoms differ only in their padding. So trimming
   // costs no discrimination and removes a whole class of false mismatch.
   std::string trim(const std::string &s) {
      std::string::size_type b = s.find_first_not_of(" \t");
      if (b == std::string::npos) return std::string();
      std::string::size_type e = s.find_last_not_of(" \t");
      return s.substr(b, e - b + 1);
   }

   std::vector<std::string> sorted_unique(const std::set<std::string> &s) {
      return std::vector<std::string>(s.begin(), s.end());
   }

   std::string upcase(const std::string &s) {
      std::string r = s;
      for (std::string::size_type i=0; i<r.size(); i++)
         r[i] = std::toupper(static_cast<unsigned char>(r[i]));
      return r;
   }

   // Longest distance still counted as a bond, by element pair. Hydrogen is
   // absent on purpose -- it never reaches this table (see the header).
   //
   // These are deliberately tight. A generous cutoff turns a non-bonded contact
   // into a bond, and since the comparison is between two residues, a contact
   // that is 1.87 A in one copy and 2.06 A in the other reads as a chemistry
   // difference when it is nothing of the sort.
   double bond_cutoff(const std::string &e1, const std::string &e2) {

      const std::string a = (e1 < e2) ? e1 : e2;
      const std::string b = (e1 < e2) ? e2 : e1;

      if (a == "C" && b == "C")  return 1.75;
      if (a == "C" && b == "N")  return 1.65;
      if (a == "C" && b == "O")  return 1.60;
      if (a == "N" && b == "O")  return 1.55;
      if (a == "N" && b == "N")  return 1.55;
      if (a == "O" && b == "O")  return 1.60;
      if (a == "C" && b == "F")  return 1.50;
      if (a == "C" && b == "S")  return 1.90;
      if (a == "N" && b == "S")  return 1.85;
      if (a == "O" && b == "S")  return 1.70;
      if (a == "S" && b == "S")  return 2.10;
      if (a == "C" && b == "P")  return 1.95;
      if (a == "O" && b == "P")  return 1.75;
      if (a == "BR" && b == "C") return 2.05;
      if (a == "C" && b == "CL") return 1.90;
      if (a == "C" && b == "I")  return 2.25;
      // Anything else -- metals, rarer halogens, elements we have not listed.
      return 1.95;
   }

   // Water is skipped everywhere here: it is not a ligand, there are usually
   // thousands of them, and they carry no chemistry worth comparing.
   bool skip_this_comp_id(const std::string &comp_id) {
      return (comp_id == "HOH" || comp_id == "WAT" || comp_id == "DOD" ||
              comp_id.empty());
   }
}

namespace coot {
   namespace comp_id_collision {

      // ------------------------------------------------------------ placeholders

      bool is_reserved_placeholder(const std::string &comp_id) {

         const std::string c = trim(comp_id);

         // wwPDB reserves these and will never issue them as CCD ids, so that a
         // ligand named during structure determination is recognisable as novel
         // at deposition: 01-99, DRG, INH, LIG.
         if (c == "LIG" || c == "DRG" || c == "INH") return true;

         if (c.size() == 2 && isdigit(c[0]) && isdigit(c[1])) {
            // "00" is not in the reserved range; 01-99 are.
            return ! (c[0] == '0' && c[1] == '0');
         }
         return false;
      }

      std::vector<std::string> free_placeholder_codes(mmdb::Manager *mol) {

         std::vector<mmdb::Manager *> mols;
         if (mol) mols.push_back(mol);
         return free_placeholder_codes(mols);
      }

      std::vector<std::string> placeholder_comp_ids(mmdb::Manager *mol) {

         std::set<std::string> found;
         if (mol) {
            for (int imod=1; imod<=mol->GetNumberOfModels(); imod++) {
               mmdb::Model *model_p = mol->GetModel(imod);
               if (! model_p) continue;
               for (int ich=0; ich<model_p->GetNumberOfChains(); ich++) {
                  mmdb::Chain *chain_p = model_p->GetChain(ich);
                  if (! chain_p) continue;
                  for (int ires=0; ires<chain_p->GetNumberOfResidues(); ires++) {
                     mmdb::Residue *residue_p = chain_p->GetResidue(ires);
                     if (! residue_p) continue;
                     const std::string name = trim(residue_p->GetResName());
                     if (is_reserved_placeholder(name))
                        found.insert(name);
                  }
               }
            }
         }
         return std::vector<std::string>(found.begin(), found.end());
      }

      std::vector<std::string>
      free_placeholder_codes(const std::vector<mmdb::Manager *> &mols) {

         std::set<std::string> used;
         for (unsigned int im=0; im<mols.size(); im++) {
            mmdb::Manager *mol = mols[im];
            if (! mol) continue;
            for (int imod=1; imod<=mol->GetNumberOfModels(); imod++) {
               mmdb::Model *model_p = mol->GetModel(imod);
               if (! model_p) continue;
               for (int ich=0; ich<model_p->GetNumberOfChains(); ich++) {
                  mmdb::Chain *chain_p = model_p->GetChain(ich);
                  if (! chain_p) continue;
                  for (int ires=0; ires<chain_p->GetNumberOfResidues(); ires++) {
                     mmdb::Residue *residue_p = chain_p->GetResidue(ires);
                     if (residue_p)
                        used.insert(trim(residue_p->GetResName()));
                  }
               }
            }
         }

         std::vector<std::string> pool;
         pool.push_back("LIG");
         pool.push_back("DRG");
         pool.push_back("INH");
         for (int i=1; i<=99; i++) {
            std::ostringstream o;
            o.width(2);
            o.fill('0');
            o << i;
            pool.push_back(o.str());
         }

         std::vector<std::string> free_codes;
         for (unsigned int i=0; i<pool.size(); i++)
            if (used.find(pool[i]) == used.end())
               free_codes.push_back(pool[i]);
         return free_codes;
      }

      // ------------------------------------------------------------ residues

      std::string residue_ref_t::atom_selection_string() const {

         // The form used elsewhere in the tree for
         // new_molecule_by_atom_selection(): "//<chain>/<resno>".
         std::ostringstream o;
         o << "//" << chain_id << "/" << res_no;
         if (! ins_code.empty()) o << "." << ins_code;
         return o.str();
      }

      std::string residue_ref_t::label() const {
         std::ostringstream o;
         o << chain_id << "/" << res_no;
         if (! ins_code.empty()) o << "." << ins_code;
         return o.str();
      }

      std::vector<residue_ref_t> residues_of_comp_id(mmdb::Manager *mol,
                                                     const std::string &comp_id) {

         std::vector<residue_ref_t> v;
         if (! mol) return v;
         const std::string wanted = trim(comp_id);
         if (wanted.empty()) return v;

         for (int imod=1; imod<=mol->GetNumberOfModels(); imod++) {
            mmdb::Model *model_p = mol->GetModel(imod);
            if (! model_p) continue;
            for (int ich=0; ich<model_p->GetNumberOfChains(); ich++) {
               mmdb::Chain *chain_p = model_p->GetChain(ich);
               if (! chain_p) continue;
               for (int ires=0; ires<chain_p->GetNumberOfResidues(); ires++) {
                  mmdb::Residue *residue_p = chain_p->GetResidue(ires);
                  if (! residue_p) continue;
                  if (trim(residue_p->GetResName()) != wanted) continue;

                  residue_ref_t r;
                  r.chain_id = chain_p->GetChainID();
                  r.res_no   = residue_p->GetSeqNum();
                  r.ins_code = trim(residue_p->GetInsCode());

                  // Alt confs fold together: two conformers of one atom are one
                  // atom as far as a dictionary is concerned.
                  std::set<std::string> names;
                  std::set<std::string> heavy_seen;
                  mmdb::PPAtom residue_atoms = 0;
                  int n_residue_atoms = 0;
                  residue_p->GetAtomTable(residue_atoms, n_residue_atoms);
                  for (int iat=0; iat<n_residue_atoms; iat++) {
                     mmdb::Atom *at = residue_atoms[iat];
                     if (! at) continue;
                     if (at->isTer()) continue;
                     const std::string name = trim(at->name);
                     if (name.empty()) continue;
                     names.insert(name);

                     const std::string el = upcase(trim(at->element));
                     if (el == "H" || el == "D") continue;
                     // First conformer wins, so an alt-confed atom contributes
                     // one position rather than several overlapping ones.
                     if (heavy_seen.find(name) != heavy_seen.end()) continue;
                     heavy_seen.insert(name);
                     atom_pos_t p;
                     p.name = name;
                     p.element = el;
                     p.x = at->x; p.y = at->y; p.z = at->z;
                     r.heavy_atoms.push_back(p);
                  }
                  r.atom_names = sorted_unique(names);
                  v.push_back(r);
               }
            }
         }
         return v;
      }

      residue_ref_t most_complete_residue(mmdb::Manager *mol,
                                          const std::string &comp_id) {

         const std::vector<residue_ref_t> v = residues_of_comp_id(mol, comp_id);
         residue_ref_t best;
         for (unsigned int i=0; i<v.size(); i++)
            if (! best.ok() || v[i].atom_names.size() > best.atom_names.size())
               best = v[i];
         return best;
      }

      // ------------------------------------------------------------ collisions

      bool atom_name_sets_conflict(const std::vector<std::string> &a,
                                   const std::vector<std::string> &b) {

         // Conflict means NEITHER contains the other: each has at least one name
         // the other lacks. A subset relation is a less complete copy of the
         // same thing, not a different molecule. See the header.
         const std::set<std::string> sa(a.begin(), a.end());
         const std::set<std::string> sb(b.begin(), b.end());

         bool a_has_own = false;
         for (std::set<std::string>::const_iterator it=sa.begin(); it!=sa.end(); ++it)
            if (sb.find(*it) == sb.end()) { a_has_own = true; break; }
         if (! a_has_own) return false;

         for (std::set<std::string>::const_iterator it=sb.begin(); it!=sb.end(); ++it)
            if (sa.find(*it) == sa.end()) return true;
         return false;
      }

      namespace {

         std::set<std::string> shared_names(const residue_ref_t &a,
                                            const residue_ref_t &b) {
            std::set<std::string> shared;
            std::set<std::string> na(a.atom_names.begin(), a.atom_names.end());
            for (unsigned int i=0; i<b.atom_names.size(); i++)
               if (na.find(b.atom_names[i]) != na.end())
                  shared.insert(b.atom_names[i]);
            return shared;
         }

         // Bonded neighbours of each shared heavy atom, counting only bonds to
         // OTHER shared atoms. Restricting both ends is what stops a missing
         // atom from registering as a chemistry difference in its neighbours.
         // Sorted by name, so an index into the list means the same thing in
         // both residues -- which the chirality comparison depends on.
         std::map<std::string, std::vector<std::string> >
         neighbours_restricted(const std::vector<atom_pos_t> &atoms,
                               const std::set<std::string> &shared) {

            std::map<std::string, std::vector<std::string> > m;
            for (unsigned int i=0; i<atoms.size(); i++) {
               if (shared.find(atoms[i].name) == shared.end()) continue;
               std::vector<std::string> &s = m[atoms[i].name];
               for (unsigned int j=0; j<atoms.size(); j++) {
                  if (i == j) continue;
                  if (shared.find(atoms[j].name) == shared.end()) continue;
                  const double dx = atoms[i].x - atoms[j].x;
                  const double dy = atoms[i].y - atoms[j].y;
                  const double dz = atoms[i].z - atoms[j].z;
                  const double c = bond_cutoff(atoms[i].element, atoms[j].element);
                  if (dx*dx + dy*dy + dz*dz < c*c) s.push_back(atoms[j].name);
               }
               std::sort(s.begin(), s.end());
            }
            return m;
         }

         std::map<std::string, atom_pos_t>
         by_name(const std::vector<atom_pos_t> &atoms) {
            std::map<std::string, atom_pos_t> m;
            for (unsigned int i=0; i<atoms.size(); i++)
               m[atoms[i].name] = atoms[i];
            return m;
         }

         // Signed volume of the tetrahedron at centre c spanned by its first
         // three (name-sorted) neighbours. Its SIGN is the handedness.
         double chiral_volume(const atom_pos_t &c, const atom_pos_t &n1,
                              const atom_pos_t &n2, const atom_pos_t &n3) {

            const double ax = n1.x-c.x, ay = n1.y-c.y, az = n1.z-c.z;
            const double bx = n2.x-c.x, by = n2.y-c.y, bz = n2.z-c.z;
            const double dx = n3.x-c.x, dy = n3.y-c.y, dz = n3.z-c.z;
            return ax*(by*dz - bz*dy) - ay*(bx*dz - bz*dx) + az*(bx*dy - by*dx);
         }

         double distance(const atom_pos_t &p, const atom_pos_t &q) {
            const double dx = p.x-q.x, dy = p.y-q.y, dz = p.z-q.z;
            return std::sqrt(dx*dx + dy*dy + dz*dz);
         }

         // TOLERANCE FOR A DEFORMED LIGAND. Distances between the cutoff and
         // cutoff+this are "ambiguous": neither clearly a bond nor clearly not.
         //
         // Without it the test was unusably brittle. Measured 2026-08-31 on a
         // real 15-heavy-atom ligand, comparing it against a noisy copy of
         // ITSELF -- every flag is therefore a false positive:
         //
         //     coordinate RMSD    no band     with a 0.35 A band
         //          0.09 A          2.5%            0.0%
         //          0.17 A         63.0%            0.0%
         //          0.26 A         96.5%            7.0%
         //          0.34 A         98.0%           38.5%
         //
         // 63% at 0.17 A RMSD is ordinary coordinate error, not a mangled
         // ligand. The cause was a margin of only 0.17 A between that ligand's
         // longest real bond (1.55 A) and the C-C cutoff (1.75 A).
         //
         // Sensitivity is not lost: with the band, the real two-different-
         // ligands case is still caught and the real two-copies case is still
         // quiet. Past about 0.3 A RMSD the ligand is genuinely distorted and
         // reading as "different" is acceptable: it serves as a round-about
         // warning that something is off.
         const double BOND_AMBIGUOUS_BAND = 0.35;

         // Below this the centre is effectively planar and the sign is noise.
         // Measured on real stereocentres (7 R/S pairs from a fragment
         // campaign, 2026-08-31): |V| came out at 2.4-2.7 every time, so this
         // floor is far below any genuine signal.
         const double CHIRAL_VOLUME_FLOOR = 0.4;
      }

      bool connectivity_differs(const residue_ref_t &a, const residue_ref_t &b) {

         const std::set<std::string> shared = shared_names(a, b);
         if (shared.size() < 2) return false; // nothing to compare

         const std::map<std::string, atom_pos_t> pa = by_name(a.heavy_atoms);
         const std::map<std::string, atom_pos_t> pb = by_name(b.heavy_atoms);

         // Compare each shared PAIR across the two residues rather than
         // comparing two independently-perceived neighbour lists, so that one
         // distance sitting near the cutoff cannot flip the verdict.
         std::vector<std::string> ns(shared.begin(), shared.end());
         for (unsigned int i=0; i<ns.size(); i++) {
            for (unsigned int j=i+1; j<ns.size(); j++) {

               std::map<std::string, atom_pos_t>::const_iterator
                  ai = pa.find(ns[i]), aj = pa.find(ns[j]),
                  bi = pb.find(ns[i]), bj = pb.find(ns[j]);
               if (ai==pa.end() || aj==pa.end() || bi==pb.end() || bj==pb.end())
                  continue;

               const double c = bond_cutoff(ai->second.element, aj->second.element);
               const double da = distance(ai->second, aj->second);
               const double db = distance(bi->second, bj->second);

               // A pair counts as a real difference only when it is CLEARLY
               // bonded in one residue and CLEARLY non-bonded in the other.
               // Anything landing in the band between is treated as agreement.
               const bool a_bond = (da < c), a_non = (da > c + BOND_AMBIGUOUS_BAND);
               const bool b_bond = (db < c), b_non = (db > c + BOND_AMBIGUOUS_BAND);
               if ((a_bond && b_non) || (b_bond && a_non)) return true;
            }
         }
         return false;
      }

      bool chirality_differs(const residue_ref_t &a, const residue_ref_t &b) {

         const std::set<std::string> shared = shared_names(a, b);
         if (shared.size() < 4) return false;

         const std::map<std::string, std::vector<std::string> > na =
            neighbours_restricted(a.heavy_atoms, shared);
         const std::map<std::string, std::vector<std::string> > nb =
            neighbours_restricted(b.heavy_atoms, shared);
         const std::map<std::string, atom_pos_t> pa = by_name(a.heavy_atoms);
         const std::map<std::string, atom_pos_t> pb = by_name(b.heavy_atoms);

         std::map<std::string, std::vector<std::string> >::const_iterator it;
         for (it=na.begin(); it!=na.end(); ++it) {

            if (it->second.size() < 3) continue;
            std::map<std::string, std::vector<std::string> >::const_iterator jt =
               nb.find(it->first);
            if (jt == nb.end()) continue;
            if (jt->second != it->second) continue; // connectivity handles that

            const std::map<std::string, atom_pos_t>::const_iterator
               ca = pa.find(it->first), cb = pb.find(it->first);
            if (ca == pa.end() || cb == pb.end()) continue;

            const std::string &s1 = it->second[0];
            const std::string &s2 = it->second[1];
            const std::string &s3 = it->second[2];
            if (pa.find(s1)==pa.end() || pa.find(s2)==pa.end() || pa.find(s3)==pa.end())
               continue;
            if (pb.find(s1)==pb.end() || pb.find(s2)==pb.end() || pb.find(s3)==pb.end())
               continue;

            const double va = chiral_volume(ca->second, pa.find(s1)->second,
                                            pa.find(s2)->second, pa.find(s3)->second);
            const double vb = chiral_volume(cb->second, pb.find(s1)->second,
                                            pb.find(s2)->second, pb.find(s3)->second);

            if (std::fabs(va) < CHIRAL_VOLUME_FLOOR) continue;
            if (std::fabs(vb) < CHIRAL_VOLUME_FLOOR) continue;
            if ((va > 0.0) != (vb > 0.0)) return true;
         }
         return false;
      }

      bool chemistries_conflict(const residue_ref_t &a, const residue_ref_t &b) {

         if (atom_name_sets_conflict(a.atom_names, b.atom_names)) return true;
         if (connectivity_differs(a, b)) return true;
         return chirality_differs(a, b);
      }

      std::string collision_t::message() const {

         std::ostringstream o;
         o << comp_id << " describes more than one chemistry in this molecule: "
           << a.label() << " has " << a.atom_names.size() << " atoms and "
           << b.label() << " has " << b.atom_names.size() << ", ";
         // Say WHICH test objected: the two mean quite different things to
         // anyone trying to work out whether we are right.
         if (atom_name_sets_conflict(a.atom_names, b.atom_names))
            o << "with names that do not match";
         else if (connectivity_differs(a, b))
            o << "and their shared atoms are bonded differently";
         else
            o << "and they are mirror images at a chiral centre";
         return o.str();
      }

      collision_t find_collision(mmdb::Manager *mol, const std::string &comp_id) {

         collision_t c;
         const std::vector<residue_ref_t> v = residues_of_comp_id(mol, comp_id);
         for (unsigned int i=0; i<v.size(); i++) {
            for (unsigned int j=i+1; j<v.size(); j++) {
               if (chemistries_conflict(v[i], v[j])) {
                  c.comp_id = trim(comp_id);
                  c.a = v[i];
                  c.b = v[j];
                  return c;
               }
            }
         }
         return c;
      }

      std::vector<std::vector<residue_ref_t> >
      distinct_chemistry_groups(mmdb::Manager *mol, const std::string &comp_id) {

         std::vector<std::vector<residue_ref_t> > groups;
         std::vector<residue_ref_t> v = residues_of_comp_id(mol, comp_id);

         // Most complete first, so each group's first member is its
         // representative and a partial copy meets the fullest candidate
         // before any smaller one it might also nest inside.
         std::sort(v.begin(), v.end(),
                   [](const residue_ref_t &a, const residue_ref_t &b) {
                      return a.atom_names.size() > b.atom_names.size();
                   });

         for (unsigned int i=0; i<v.size(); i++) {
            bool placed = false;
            for (unsigned int g=0; g<groups.size() && ! placed; g++) {
               if (! chemistries_conflict(groups[g][0], v[i])) {
                  groups[g].push_back(v[i]);
                  placed = true;
               }
            }
            if (! placed)
               groups.push_back(std::vector<residue_ref_t>(1, v[i]));
         }
         return groups;
      }

      std::vector<collision_t> find_collisions(mmdb::Manager *mol) {

         std::vector<collision_t> v;
         if (! mol) return v;

         // Standard residue types are skipped deliberately. An incompletely
         // modelled side chain gives two residues of one type different atom
         // sets, and two differently-truncated copies do not nest -- so the
         // conflict test would report protein as colliding with itself.
         const std::vector<std::string> types =
            util::non_standard_residue_types_in_molecule(mol);

         for (unsigned int i=0; i<types.size(); i++) {
            if (skip_this_comp_id(types[i])) continue;
            const collision_t c = find_collision(mol, types[i]);
            if (c.ok()) v.push_back(c);
         }
         return v;
      }

      std::vector<collision_t> find_collisions_between(mmdb::Manager *mol_a,
                                                       mmdb::Manager *mol_b) {

         std::vector<collision_t> v;
         if (! mol_a || ! mol_b) return v;

         const std::vector<std::string> types_a =
            util::non_standard_residue_types_in_molecule(mol_a);
         std::set<std::string> types_b;
         {
            const std::vector<std::string> tb =
               util::non_standard_residue_types_in_molecule(mol_b);
            types_b.insert(tb.begin(), tb.end());
         }

         for (unsigned int i=0; i<types_a.size(); i++) {

            const std::string &comp_id = types_a[i];
            if (skip_this_comp_id(comp_id)) continue;
            if (types_b.find(comp_id) == types_b.end()) continue;

            const std::vector<residue_ref_t> ra = residues_of_comp_id(mol_a, comp_id);
            const std::vector<residue_ref_t> rb = residues_of_comp_id(mol_b, comp_id);

            bool found = false;
            for (unsigned int ia=0; ia<ra.size() && ! found; ia++) {
               for (unsigned int ib=0; ib<rb.size() && ! found; ib++) {
                  if (chemistries_conflict(ra[ia], rb[ib])) {
                     collision_t c;
                     c.comp_id = comp_id;
                     c.a = ra[ia];
                     c.b = rb[ib];
                     v.push_back(c);
                     found = true;
                  }
               }
            }
         }
         return v;
      }

      // ------------------------------------------------------- dictionary cover

      std::string coverage_t::message() const {

         std::ostringstream o;
         if (disjoint()) {
            o << comp_id << ": the dictionary found describes a different "
              << "molecule (no atom name in common with the model)";
         } else {
            o << comp_id << ": the dictionary found is missing "
              << model_atoms_missing_from_dictionary.size() << " of "
              << n_model_atoms << " atom names used in the model (";
            const unsigned int n_show =
               std::min(static_cast<size_t>(6),
                        model_atoms_missing_from_dictionary.size());
            for (unsigned int i=0; i<n_show; i++) {
               if (i) o << " ";
               o << model_atoms_missing_from_dictionary[i];
            }
            if (model_atoms_missing_from_dictionary.size() > n_show) o << " ...";
            o << ")";
         }
         return o.str();
      }

      bool dictionary_describes_residue(const dictionary_residue_restraints_t &dict,
                                        const residue_ref_t &res) {

         if (res.heavy_atoms.empty()) return false;

         // The dictionary's heavy atoms, by name.
         std::set<std::string> dict_heavy;
         for (unsigned int i=0; i<dict.atom_info.size(); i++) {
            const std::string el = upcase(trim(dict.atom_info[i].type_symbol));
            if (el == "H" || el == "D") continue;
            const std::string a = trim(dict.atom_info[i].atom_id_4c);
            if (! a.empty()) dict_heavy.insert(a);
            const std::string b = trim(dict.atom_info[i].atom_id);
            if (! b.empty()) dict_heavy.insert(b);
         }
         if (dict_heavy.empty()) return false;

         // Every heavy atom of the residue must be described. A dictionary that
         // does not name one of them cannot restrain it, so it is not a match
         // however well the rest agrees.
         for (unsigned int i=0; i<res.heavy_atoms.size(); i++)
            if (dict_heavy.find(res.heavy_atoms[i].name) == dict_heavy.end())
               return false;

         // The dictionary's bonds, restricted to atoms this residue has.
         std::set<std::string> res_names;
         for (unsigned int i=0; i<res.heavy_atoms.size(); i++)
            res_names.insert(res.heavy_atoms[i].name);

         std::set<std::pair<std::string, std::string> > dict_bonds;
         for (unsigned int i=0; i<dict.bond_restraint.size(); i++) {
            std::string a = trim(dict.bond_restraint[i].atom_id_1());
            std::string b = trim(dict.bond_restraint[i].atom_id_2());
            if (res_names.find(a) == res_names.end()) continue;
            if (res_names.find(b) == res_names.end()) continue;
            if (b < a) std::swap(a, b);
            dict_bonds.insert(std::make_pair(a, b));
         }

         // Compare against the bonds perceived from the residue's coordinates.
         // A distance in the ambiguous band agrees with either answer -- the
         // same tolerance for a deformed ligand used everywhere here, and
         // needed for the same reason: the model side is perceived, not stated.
         for (unsigned int i=0; i<res.heavy_atoms.size(); i++) {
            for (unsigned int j=i+1; j<res.heavy_atoms.size(); j++) {

               const atom_pos_t &p = res.heavy_atoms[i];
               const atom_pos_t &q = res.heavy_atoms[j];
               const double c = bond_cutoff(p.element, q.element);
               const double d = distance(p, q);

               std::string a = p.name, b = q.name;
               if (b < a) std::swap(a, b);
               const bool in_dict =
                  dict_bonds.find(std::make_pair(a, b)) != dict_bonds.end();

               if (d < c) {                      // model says bonded
                  if (! in_dict) return false;
               } else if (d > c + BOND_AMBIGUOUS_BAND) {  // model says not bonded
                  if (in_dict) return false;
               }
               // otherwise ambiguous: accept whatever the dictionary says
            }
         }
         return true;
      }

      std::vector<std::string>
      comp_ids_matching_dictionary(mmdb::Manager *mol,
                                   const dictionary_residue_restraints_t &dict) {

         std::vector<std::string> matches;
         if (! mol) return matches;

         const std::vector<std::string> types =
            util::non_standard_residue_types_in_molecule(mol);

         for (unsigned int i=0; i<types.size(); i++) {
            if (skip_this_comp_id(types[i])) continue;
            // Compare against the most complete copy: a partial one could fail
            // the "every heavy atom described" test for want of atoms rather
            // than for any disagreement.
            const residue_ref_t r = most_complete_residue(mol, types[i]);
            if (! r.ok()) continue;
            if (dictionary_describes_residue(dict, r))
               matches.push_back(types[i]);
         }
         return matches;
      }

      namespace {

         // Every non-standard comp id in mol and the union of the atom names
         // used under it, in a single pass over the structure.
         std::map<std::string, std::set<std::string> >
         atom_names_by_comp_id(mmdb::Manager *mol) {

            std::map<std::string, std::set<std::string> > m;
            if (! mol) return m;

            std::set<std::string> standards;
            {
               const std::vector<std::string> s = util::standard_residue_types();
               standards.insert(s.begin(), s.end());
            }

            for (int imod=1; imod<=mol->GetNumberOfModels(); imod++) {
               mmdb::Model *model_p = mol->GetModel(imod);
               if (! model_p) continue;
               for (int ich=0; ich<model_p->GetNumberOfChains(); ich++) {
                  mmdb::Chain *chain_p = model_p->GetChain(ich);
                  if (! chain_p) continue;
                  for (int ires=0; ires<chain_p->GetNumberOfResidues(); ires++) {
                     mmdb::Residue *residue_p = chain_p->GetResidue(ires);
                     if (! residue_p) continue;
                     const std::string comp_id = trim(residue_p->GetResName());
                     if (skip_this_comp_id(comp_id)) continue;
                     if (standards.find(comp_id) != standards.end()) continue;

                     std::set<std::string> &names = m[comp_id];
                     mmdb::PPAtom residue_atoms = 0;
                     int n_residue_atoms = 0;
                     residue_p->GetAtomTable(residue_atoms, n_residue_atoms);
                     for (int iat=0; iat<n_residue_atoms; iat++) {
                        mmdb::Atom *at = residue_atoms[iat];
                        if (! at) continue;
                        if (at->isTer()) continue;
                        const std::string name = trim(at->name);
                        if (! name.empty()) names.insert(name);
                     }
                  }
               }
            }
            return m;
         }

         // Compare one comp id's model atom names against its dictionary.
         coverage_t coverage_against(const std::string &comp_id,
                                     const std::set<std::string> &model_names,
                                     const protein_geometry &geom,
                                     int imol_enc) {

            coverage_t cov;
            cov.comp_id = comp_id;

            const std::pair<bool, dictionary_residue_restraints_t> r =
               geom.get_monomer_restraints(comp_id, imol_enc);
            if (! r.first) return cov; // no dictionary: not this function's business

            std::set<std::string> dict_names;
            for (unsigned int i=0; i<r.second.atom_info.size(); i++) {
               const std::string a = trim(r.second.atom_info[i].atom_id_4c);
               if (! a.empty()) dict_names.insert(a);
               const std::string b = trim(r.second.atom_info[i].atom_id);
               if (! b.empty()) dict_names.insert(b);
            }
            if (dict_names.empty()) return cov;

            cov.n_model_atoms = model_names.size();
            std::set<std::string>::const_iterator it;
            for (it=model_names.begin(); it!=model_names.end(); ++it) {
               if (dict_names.find(*it) == dict_names.end())
                  cov.model_atoms_missing_from_dictionary.push_back(*it);
               else
                  cov.n_matched++;
            }
            return cov;
         }
      }

      coverage_t dictionary_coverage(mmdb::Manager *mol,
                                     const std::string &comp_id,
                                     const protein_geometry &geom,
                                     int imol_enc) {

         coverage_t cov;
         cov.comp_id = trim(comp_id);
         if (! mol) return cov;

         // Union over every copy in the model: an atom name used anywhere under
         // this comp id needs to be in the dictionary, since the restraints are
         // applied to all of them.
         std::set<std::string> model_names;
         const std::vector<residue_ref_t> v = residues_of_comp_id(mol, cov.comp_id);
         for (unsigned int i=0; i<v.size(); i++)
            model_names.insert(v[i].atom_names.begin(), v[i].atom_names.end());

         return coverage_against(cov.comp_id, model_names, geom, imol_enc);
      }

      std::vector<coverage_t> dictionary_coverage_failures(mmdb::Manager *mol,
                                                           const protein_geometry &geom,
                                                           int imol_enc) {

         std::vector<coverage_t> v;
         if (! mol) return v;

         // ONE walk of the molecule, not one per comp id. This runs on every
         // coordinate load, where the user is entitled to expect a file open to
         // feel like a file open -- so it must not scale with the number of
         // component types times the size of the structure.
         const std::map<std::string, std::set<std::string> > by_comp_id =
            atom_names_by_comp_id(mol);

         std::map<std::string, std::set<std::string> >::const_iterator it;
         for (it=by_comp_id.begin(); it!=by_comp_id.end(); ++it) {
            const coverage_t cov =
               coverage_against(it->first, it->second, geom, imol_enc);
            // n_model_atoms is 0 when there is no dictionary at all, which is
            // reported elsewhere and is not a coverage failure.
            if (cov.n_model_atoms > 0 && ! cov.covered())
               v.push_back(cov);
         }
         return v;
      }
   }
}

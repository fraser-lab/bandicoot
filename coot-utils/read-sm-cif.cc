/* coot-utils/read-sm-cif.cc
 * 
 * Copyright 2011, 2012 by The University of Oxford
 * Copyright 2016 by Medical Research Council
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

#include <iostream>
#include <string>
#include <stdexcept>
#include <algorithm> // for remove_if
#include <string.h> // for strncpy

#include <map>

#include <mmdb2/mmdb_manager.h>
#include <clipper/core/clipper_util.h>
#include <clipper/core/spacegroup.h>
#include "clipper/core/clipper_instance.h" // tidy up space group cache
#include "clipper/core/resol_basisfn.h"
#include "clipper/contrib/sfcalc_obs.h"
#include "clipper/contrib/sfscale.h"
#include "clipper/contrib/sfweight.h"

#include "clipper/mmdb/clipper_mmdb.h"
#include "clipper/clipper-cif.h"
#include "clipper/contrib/sfcalc.h"

// BANDICOOT v0.2 (Interlude B): gemmi resolves a space-group name or number
// into its operators. Used only as a FALLBACK, when a file states its space
// group without listing the operators -- see
// symops_from_space_group_statement() below. gemmi rather than clipper because
// clipper's name parsing fails UNSAFE: measured, clipper::Spgr_descr turns
// "P212121" and "C2" into P 1 with a single operator, silently, and throws
// Message_fatal on a lower-case name. gemmi recognised every valid spelling
// tested and got all of them right.
#include <gemmi/symmetry.hpp>

// BANDICOOT v0.2 Phase 4: gemmi reads this format natively --
// SmallStructure is gemmi's model of a small-molecule CIF, and
// make_small_structure_from_block() is its reader.
#include <gemmi/small.hpp>
#include <gemmi/smcif.hpp>
#include <gemmi/read_cif.hpp>   // read_cif_gz
#include <gemmi/atox.hpp>       // string_to_int
#include <gemmi/math.hpp>       // u_to_b

#include "compat/coot-sysdep.h"
#include "utils/coot-utils.hh"
#include "geometry/residue-and-atom-specs.hh"

#include "read-sm-cif.hh"



// This can throw a std::runtime_error.
// 
// ---------------------------------------------------------------------------
// BANDICOOT v0.2 PHASE 4: the small-molecule COORDINATE reader, on gemmi.
//
// This was the last coordinate reader in the tree still built on mmdb's CIF
// parser (mmdb::mmcif::Data), and the charter is that all CIF input goes
// through gemmi and nothing else. The port is not a like-for-like translation
// -- gemmi already models this format, in gemmi::SmallStructure, so most of
// what was hand-written here is deleted rather than rewritten:
//
//   * get_cell()                        -> _cell_* parsing, INCLUDING the
//                                          standard uncertainty in parentheses
//                                          ("7.97537(5)"), which this file used
//                                          to strip by splitting on "(".
//   * get_space_group(symm_strings)      -> gemmi resolves the group itself.
//   * symops_from_space_group_statement()-> ... in exactly the fallback order
//                                          measured in Interlude B, which is
//                                          what "S.H2n" spells: operator loop
//                                          first, then Hall, then Hermann-
//                                          Mauguin, then the International
//                                          Tables number LAST, because a number
//                                          does not fix the setting.
//   * symbol_to_element()               -> gemmi's split_element_and_charge()
//                                          parses "Zn2+" into element + charge.
//   * the U-vs-B distinction            -> gemmi reads either and normalises.
//
// AND IT REMOVES A WHOLE CLASS OF BUG. TRAPS D6: mmdb::mmcif::Loop::GetReal
// reports SUCCESS for a tag that is not there, leaving the target untouched --
// which is how every atom in a file stating no displacement parameter came out
// with B = 789.57 (= 10 x 8pi^2, the default scaled as though it had been read).
// Every value read through that API had to be defended against by asking
// GetString first. gemmi's Table/find has no such behaviour: a column that is
// not there is not there.
//
// Two behaviours are deliberately PRESERVED rather than modernised, because
// they are observable and this is a port, not a redesign:
//   * B = 10.0 when the file states no displacement parameter at all;
//   * occupancy divided by _atom_site_symmetry_multiplicity when the file
//     states it -- and NOT by _atom_site_site_symmetry_order, which is the
//     other spelling in circulation. Accepting both would be the usual
//     nomenclature fix, but here it would CHANGE OCCUPANCIES: SHELX writes an
//     already-reduced occupancy for an atom on a special position alongside
//     site_symmetry_order 2, so honouring that tag as well would halve it
//     twice. Which of the two tags this division is actually right for is an
//     open question, recorded rather than guessed at. (Every atom in the corpus
//     file 4517425.cif has order 1, so this is untested either way.)
//
// One BUG IS FIXED by the port rather than carried across: the old aniso block
// applied `u11..u23`, the loop's last-read locals, to every matched atom
// instead of that atom's own values -- so all 16 anisotropic atoms of
// 4517425.cif came out with identical ADPs. gemmi keeps them per site.

namespace {

   // mmdb wants an element name upper-cased and right-justified in two
   // characters (" C", "ZN"); gemmi::Element::name() gives "C", "Zn".
   std::string mmdb_element_name(const gemmi::Element &e) {

      std::string s = coot::util::upcase(std::string(e.name()));
      if (s.length() == 1) s = " " + s;
      if (s.length() > 2)  s = s.substr(0, 2);
      return s;
   }

   // The charge this reader has always assigned by element. Kept as a FALLBACK
   // only: a file that states the oxidation state in its type symbol ("Zn2+")
   // is now believed instead, which the old code could not do -- it parsed the
   // number out and then threw it away, so a stated 2+ on an element missing
   // from this table came out as 0.
   int charge_for_element(const std::string &ele) {

      if (ele == "NA" || ele == " K" || ele == "LI" ||
          ele == "RU" || ele == "CS") return 1;
      if (ele == "MG" || ele == "CA" || ele == "SR") return 2;
      if (ele == " F" || ele == "CL" || ele == "BR" || ele == " I") return -1;
      return 0;
   }

   // The block holding the coordinates. A small-molecule CIF is usually one
   // block, but a multi-block file (several structures, or a header block
   // followed by the data) must not be answered with block 0 regardless.
   gemmi::cif::Block *coordinate_block(gemmi::cif::Document &doc) {

      for (gemmi::cif::Block &b : doc.blocks)
         if (b.has_any_value("_atom_site_fract_x") || b.has_any_value("_atom_site_label"))
            return &b;
      return NULL;
   }

   // Does the file state this tag at all, as a loop column or as a pair? The
   // distinction between "absent" and "present but null" is what the B-factor
   // default turns on.
   bool block_states_tag(gemmi::cif::Block &b, const char *tag) {

      return b.has_any_value(tag);
   }

   // The atoms, from the parsed structure. A free function rather than a method
   // so that gemmi types stay out of read-sm-cif.hh -- src/ includes that
   // header, and mmcif-document.hh is meant to remain the only gemmi-aware one.
   std::vector<mmdb::Atom *>
   atoms_from_small_structure(const gemmi::SmallStructure &st,
                              gemmi::cif::Block &block,
                              const clipper::Cell &cell) {

      std::vector<mmdb::Atom *> atom_vec;

      // B = 10.0 is the default this reader has always used for a file that
      // states no displacement parameter. Asked of the FILE, not of the value:
      // gemmi reports u_iso 0 both for "absent" and for "stated as zero", and
      // the difference decides whether 10.0 or 0 is right.
      bool have_adp = block_states_tag(block, "_atom_site_U_iso_or_equiv") ||
                      block_states_tag(block, "_atom_site_B_iso_or_equiv");

      // The two per-atom columns gemmi::SmallStructure does not model: the
      // symmetry multiplicity the occupancy is divided by, and the disorder
      // ASSEMBLY, which this reader has always used as the altLoc (gemmi keeps
      // the disorder GROUP, which is a different column). Keyed by label rather
      // than by row index, so a reordering cannot mis-assign them.
      std::map<std::string, int> mult_of_label;
      std::map<std::string, std::string> altloc_of_label;
      for (auto row : block.find("_atom_site_", {"label",
                                                 "?symmetry_multiplicity",
                                                 "?disorder_assembly"})) {
         std::string label = row.str(0);
         if (row.has(1) && ! gemmi::cif::is_null(row[1])) {
            int m = gemmi::string_to_int(row.str(1), false);
            if (m > 0) mult_of_label[label] = m;
         }
         if (row.has(2) && ! gemmi::cif::is_null(row[2])) {
            std::string a = coot::util::remove_whitespace(row.str(2));
            if (! a.empty()) altloc_of_label[label] = a;
         }
      }

      for (const gemmi::SmallStructure::Site &site : st.sites) {

         mmdb::Atom *at = new mmdb::Atom;

         gemmi::Position pos = st.cell.orthogonalize(site.fract);

         double occ = site.occ;
         std::map<std::string, int>::const_iterator it = mult_of_label.find(site.label);
         if (it != mult_of_label.end())
            occ /= double(it->second);

         double b_factor = (have_adp && site.u_iso > 0.0)
            ? site.u_iso * gemmi::u_to_b() : 10.0;

         at->SetCoordinates(pos.x, pos.y, pos.z, occ, b_factor);
         at->SetAtomName(site.label.c_str());

         std::string ele = mmdb_element_name(site.element);
         at->SetElementName(ele.c_str());
         // The file's own statement first ("Zn2+" -> 2), the element table only
         // as a fallback -- see charge_for_element().
         at->charge = site.charge ? int(site.charge) : charge_for_element(ele);

         std::map<std::string, std::string>::const_iterator ia =
            altloc_of_label.find(site.label);
         if (ia != altloc_of_label.end())
            strncpy(at->altLoc, ia->second.c_str(), sizeof(at->altLoc) - 1);

         at->Het = 1;   // all small-molecule cif atoms are HETATMs :)

         // The ADPs, per site -- which is the point. The old loop applied its
         // last-read locals to every matched atom, so all 16 anisotropic atoms
         // of 4517425.cif shared one set of Us.
         const gemmi::SMat33<double> &u = site.aniso;
         if (u.u11 > 0 && u.u22 > 0 && u.u33 > 0) {
            double a = cell.a();
            double b = cell.b();
            double c = cell.c();
            clipper::U_aniso_frac caf(u.u11/(a*a), u.u22/(b*b), u.u33/(c*c),
                                      u.u12/(a*b), u.u13/(a*c), u.u23/(b*c));
            clipper::U_aniso_orth cao = caf.u_aniso_orth(cell);
            at->u11 = cao(0,0);
            at->u22 = cao(1,1);
            at->u33 = cao(2,2);
            at->u12 = cao(0,1);
            at->u13 = cao(0,2);
            at->u23 = cao(1,2);
            at->WhatIsSet |= mmdb::ASET_Anis_tFac;
         }

         atom_vec.push_back(at);
      }
      return atom_vec;
   }

   // Symmetry operators -> a clipper::Spacegroup. Still needed by the
   // REFLECTION-DATA half of this file, which is not ported yet and which works
   // in clipper types throughout (HKL_info, HKL_data). The coordinate half no
   // longer uses it: gemmi resolves the group and mmdb takes the name.
   std::pair<bool, clipper::Spacegroup>
   spacegroup_from_symop_strings(const std::vector<std::string> &symm_strings) {

      bool status = false;
      std::string symmetry_ops;
      for (unsigned int isym=0; isym<symm_strings.size(); isym++) {
         symmetry_ops += symm_strings[isym];
         symmetry_ops += " ; ";
      }
      clipper::Spacegroup space_group;
      clipper::Spgr_descr spg_descr(symmetry_ops, clipper::Spgr_descr::Symops);
      if (spg_descr.spacegroup_number() == 0) {
         std::cout << "Failed to init space_group description with symop strings "
                   << symmetry_ops << std::endl;
      } else {
         space_group.init(spg_descr);
         status = true;
      }
      return std::pair<bool, clipper::Spacegroup>(status, space_group);
   }

   // The residue name. _chem_comp.id is the component's actual CCD code (AR6,
   // ADP), so a file that states it names the residue correctly AND matches the
   // wwPDB dictionary for it.
   //
   // THE NAME IS LOAD-BEARING, not cosmetic: refinement finds restraints BY
   // RESIDUE NAME, so a molecule called XXX -- which is what this reader used to
   // produce -- can never be refined, because no dictionary anywhere is keyed on
   // XXX and importing the right one cannot help either, the names not matching.
   // "LIG" is the default because it is what the tools that generate ligand
   // restraints write (acedrg, and elbow via phenix), so an imported dictionary
   // matches without the user renaming anything.
   std::string residue_name_for_block(gemmi::cif::Document &doc,
                                      gemmi::cif::Block &block) {

      const char *tags[] = { "_chem_comp.id", "_chem_comp_id", NULL };
      for (int i = 0; tags[i]; i++) {
         const std::string *v = block.find_value(tags[i]);
         if (! v)
            for (gemmi::cif::Block &b : doc.blocks)
               if ((v = b.find_value(tags[i])) != NULL)
                  break;
         if (v) {
            std::string id = coot::util::remove_whitespace(gemmi::cif::as_string(*v));
            if (! id.empty() && id != "." && id != "?")
               return id;
         }
      }
      return "LIG";
   }
}

mmdb::Manager *
coot::smcif::read_sm_cif(const std::string &file_name) const {

   mmdb::Manager *mol = NULL;

   try {
      gemmi::cif::Document doc = gemmi::read_cif_gz(file_name);
      gemmi::cif::Block *block = coordinate_block(doc);
      if (! block) {
         std::cout << "WARNING:: no atom site loop in small-molecule cif \""
                   << file_name << "\"" << std::endl;
         return NULL;
      }

      gemmi::SmallStructure st = gemmi::make_small_structure_from_block(*block);

      // The cell is REQUIRED, and this is the one place the read still refuses:
      // the coordinates in this format are FRACTIONAL, so without a cell there
      // is nothing to orthogonalise them with and the atoms would land at
      // 0-1 Angstrom of each other. gemmi's UnitCell defaults to 1,1,1,90,90,90
      // rather than to a null, so the file has to be asked directly.
      if (! block->has_any_value("_cell_length_a") ||
          ! block->has_any_value("_cell_length_b") ||
          ! block->has_any_value("_cell_length_c") ||
          st.cell.a <= 0 || st.cell.b <= 0 || st.cell.c <= 0) {
         std::cout << "WARNING:: no cell in small-molecule cif \"" << file_name
                   << "\" - fractional coordinates cannot be used without one"
                   << std::endl;
         return NULL;
      }

      clipper::Cell_descr cell_descr(st.cell.a, st.cell.b, st.cell.c,
                                     clipper::Util::d2rad(st.cell.alpha),
                                     clipper::Util::d2rad(st.cell.beta),
                                     clipper::Util::d2rad(st.cell.gamma));
      clipper::Cell cell(cell_descr);
      std::cout << "INFO:: got cell from cif: " << cell.format() << std::endl;

      // The space group, in the fallback order Interlude B measured and this
      // reader used to implement by hand. "S.H2n" is that order: Symops,
      // then "." (an operator set complete but not matching a tabulated
      // setting still gives usable cell images), then Hall, then
      // Hermann-Mauguin preferring setting 2, then the International Tables
      // Number -- last, because a number does not fix the setting (P 1 21/c 1
      // and P 1 21/n 1 are both number 14).
      st.determine_and_set_spacegroup("S.H2n");
      if (st.spacegroup) {
         const char *how = ! st.symops.empty()          ? "operator loop"
                         : ! st.spacegroup_hall.empty() ? "Hall symbol"
                         : ! st.spacegroup_hm.empty()   ? "H-M name"
                         :                                "IT number";
         std::cout << "INFO:: space group " << st.spacegroup->xhm()
                   << " (from the " << how << ")" << std::endl;
         if (st.symops.empty() && st.spacegroup_hall.empty() && st.spacegroup_hm.empty())
            std::cout << "WARNING:: the International Tables number does not "
                      << "specify the setting, so this is the standard setting "
                      << "and may not be the file's" << std::endl;
      } else {
         // Not fatal, and this is the shape of the bug that used to make this
         // reader reject whole files: the atom loop was read INSIDE the symmetry
         // branch, so one unrecognised spelling cost the molecule rather than
         // its symmetry. Coot has a non-crystallographic path for a model with
         // no space group; use it.
         std::cout << "WARNING:: no space group could be resolved for \""
                   << file_name << "\" - reading the coordinates without it"
                   << std::endl;
      }

      std::vector<mmdb::Atom *> atoms = atoms_from_small_structure(st, *block, cell);
      std::cout << "INFO:: from cif we read " << atoms.size() << " atoms" << std::endl;
      if (atoms.empty())
         return NULL;

      mol = new mmdb::Manager;
      mmdb::Model *model_p = new mmdb::Model;
      mmdb::Chain *chain_p = new mmdb::Chain;
      mmdb::Residue *residue_p = new mmdb::Residue;
      chain_p->SetChainID("");
      residue_p->seqNum = 1;

      std::string res_name = residue_name_for_block(doc, *block);
      std::cout << "INFO:: small-molecule cif: residue named " << res_name << std::endl;
      residue_p->SetResName(res_name.c_str());

      for (unsigned int iat=0; iat<atoms.size(); iat++)
         residue_p->AddAtom(atoms[iat]);
      chain_p->AddResidue(residue_p);
      model_p->AddChain(chain_p);
      mol->AddModel(model_p);

      mol->SetCell(cell.a(), cell.b(), cell.c(),
                   clipper::Util::rad2d(cell.alpha()),
                   clipper::Util::rad2d(cell.beta()),
                   clipper::Util::rad2d(cell.gamma()));
      if (st.spacegroup)
         mol->SetSpaceGroup(st.spacegroup->xhm().c_str());
   }

   catch (const std::exception &e) {
      std::cout << "ERROR:: reading small-molecule cif \"" << file_name << "\": "
                << e.what() << std::endl;
      if (mol) { delete mol; mol = NULL; }
   }

   return mol;
}


// ---------------------------------------------------------------------------
// BANDICOOT v0.2 PHASE 4, second half: the REFLECTION-DATA reader, on gemmi.
//
// Same charter as the coordinate half above, and the same parser removed. Three
// things change beyond the parser swap, all of them consequences of it:
//
//  1. ONE PARSE, not five. read_data_sm_cif() used to call get_cell_for_data(),
//     get_space_group(), get_resolution() and setup_hkls(), each of which
//     opened, parsed and closed the file for itself, and then parsed it a fifth
//     time for the data values. The document is now read once and the block
//     passed around.
//  2. A column that is not there is asked about rather than assumed. TRAPS D6
//     is the reason to distrust the old code here -- Loop::GetReal reports
//     SUCCESS for an absent tag in the 4-argument form used by the atom loop --
//     but MEASURED on a synthetic file carrying h/k/l and nothing else, the
//     3-argument form used here does report the absence, and the old reader
//     correctly returned false. So this is a hardening, NOT a bug fixed: the
//     defence is now structural (an optional column is declared optional and
//     tested) instead of resting on which overload happens to be honest.
//  3. get_space_group(Data*, symm_tag) is deleted rather than ported: it always
//     returned false and printed "Hoooray!" when it found the structure it then
//     did nothing with.
//
// Tag dialects: the SHELX .fcf spelling `_refln_*` and the powder `_pd_refln_*`
// are both accepted, as before. PDBx `_refln.*` (a wwPDB structure-factor file)
// is deliberately NOT read here -- that is a different path, and quietly
// accepting it would mean this reader answering for files it has never been
// tested on.
//
// The clipper HKL machinery below is unchanged: it is what feeds the sigma-A
// maps, and Phase 4 is about the parser, not about replacing clipper.

namespace {

   // The block, and which of the two spellings its reflection loop uses.
   struct refln_loop_t {
      gemmi::cif::Block *block = NULL;
      std::string prefix;
      bool ok() const { return block != NULL; }
   };

   refln_loop_t find_refln_loop(gemmi::cif::Document &doc) {

      refln_loop_t r;
      const char *prefixes[] = { "_refln_", "_pd_refln_", NULL };
      for (int i = 0; prefixes[i]; i++) {
         for (gemmi::cif::Block &b : doc.blocks) {
            if (b.has_any_value((std::string(prefixes[i]) + "index_h").c_str())) {
               r.block = &b;
               r.prefix = prefixes[i];
               return r;
            }
         }
      }
      return r;
   }

   clipper::Cell cell_from_block(gemmi::cif::Block &block) {

      clipper::Cell cell;
      gemmi::cif::Table t = block.find("_cell_", {"length_a", "length_b", "length_c",
                                                  "angle_alpha", "angle_beta", "angle_gamma"});
      if (! t.ok())
         return cell;    // null cell: the caller declines
      gemmi::cif::Table::Row row = t.one();
      double v[6];
      for (int i = 0; i < 6; i++) {
         if (gemmi::cif::is_null(row[i])) return cell;
         v[i] = gemmi::cif::as_number(row[i]);
         if (std::isnan(v[i])) return cell;
      }
      clipper::Cell_descr descr(v[0], v[1], v[2],
                               clipper::Util::d2rad(v[3]),
                               clipper::Util::d2rad(v[4]),
                               clipper::Util::d2rad(v[5]));
      cell.init(descr);
      return cell;
   }

   // The space group of a data file. Both spellings of the operator loop, then
   // the same name/number fallbacks gemmi resolves for the coordinate half --
   // a .fcf written by an older SHELXL states `_symmetry_equiv_pos_as_xyz`,
   // a newer one `_space_group_symop_operation_xyz`.
   std::pair<bool, clipper::Spacegroup>
   spacegroup_from_block(gemmi::cif::Block &block) {

      std::vector<std::string> symops;
      const char *tags[] = { "_symmetry_equiv_pos_as_xyz",
                             "_space_group_symop_operation_xyz", NULL };
      for (int i = 0; tags[i] && symops.empty(); i++)
         for (const std::string &v : block.find_loop(tags[i]))
            symops.push_back(gemmi::cif::as_string(v));

      if (! symops.empty()) {
         std::pair<bool, clipper::Spacegroup> s = spacegroup_from_symop_strings(symops);
         if (s.first) return s;
      }

      // No usable operator loop: let gemmi resolve a name or a number, and hand
      // clipper the operators it decides on rather than the name -- clipper's
      // own name parsing fails UNSAFE (measured: it turns "P212121" into P 1).
      gemmi::SmallStructure st = gemmi::make_small_structure_from_block(block);
      st.determine_and_set_spacegroup("S.H2n");
      if (st.spacegroup) {
         std::vector<std::string> ops;
         for (const gemmi::Op &op : st.spacegroup->operations().all_ops_sorted())
            ops.push_back(op.triplet());
         std::pair<bool, clipper::Spacegroup> s = spacegroup_from_symop_strings(ops);
         if (s.first) {
            std::cout << "INFO:: data file space group " << st.spacegroup->xhm()
                      << " (no operator loop)" << std::endl;
            return s;
         }
      }
      return std::pair<bool, clipper::Spacegroup>(false, clipper::Spacegroup());
   }

   // h k l, in file order. The resolution limit and the HKL list both come from
   // this, so it is read once and used twice -- the old code read the same loop
   // twice, in two functions, each re-parsing the file.
   std::vector<clipper::HKL> hkl_list_from_block(gemmi::cif::Block &block,
                                                const std::string &prefix) {

      std::vector<clipper::HKL> hkls;
      for (auto row : block.find(prefix, {"index_h", "index_k", "index_l"})) {
         if (gemmi::cif::is_null(row[0]) || gemmi::cif::is_null(row[1]) ||
             gemmi::cif::is_null(row[2]))
            continue;
         hkls.push_back(clipper::HKL(gemmi::cif::as_int(row[0]),
                                     gemmi::cif::as_int(row[1]),
                                     gemmi::cif::as_int(row[2])));
      }
      return hkls;
   }

   clipper::Resolution resolution_of_hkls(const std::vector<clipper::HKL> &hkls,
                                          const clipper::Cell &cell) {

      clipper::ftype slim = 0.0;
      for (unsigned int i = 0; i < hkls.size(); i++)
         slim = clipper::Util::max(slim, hkls[i].invresolsq(cell));
      if (slim <= 0.0)
         return clipper::Resolution();      // null
      return clipper::Resolution(1.0 / sqrt(slim));
   }

   // A value from an OPTIONAL column: present, non-null and numeric, or nothing.
   // This is the whole of trap D6's remedy -- ask, do not assume.
   bool value_of(gemmi::cif::Table::Row &row, int i, double *out) {

      if (! row.has(i) || gemmi::cif::is_null(row[i])) return false;
      double v = gemmi::cif::as_number(row[i]);
      if (std::isnan(v)) return false;
      *out = v;
      return true;
   }
}

bool
coot::smcif::read_data_sm_cif(const std::string &file_name) {

   bool status = false;

   try {
      gemmi::cif::Document doc = gemmi::read_cif_gz(file_name);
      refln_loop_t rl = find_refln_loop(doc);
      if (! rl.ok()) {
         std::cout << "WARNING:: no reflection loop in \"" << file_name << "\""
                   << std::endl;
         return false;
      }

      clipper::Cell cell_local = cell_from_block(*rl.block);
      std::pair<bool, clipper::Spacegroup> spg_pair = spacegroup_from_block(*rl.block);
      std::vector<clipper::HKL> hkls = hkl_list_from_block(*rl.block, rl.prefix);
      clipper::Resolution reso = resolution_of_hkls(hkls, cell_local);

      if (cell_local.is_null()) {
         std::cout << "WARNING:: no cell in \"" << file_name << "\"" << std::endl;
         return false;
      }
      if (! spg_pair.first || spg_pair.second.is_null()) {
         std::cout << "WARNING:: no space group in \"" << file_name << "\"" << std::endl;
         return false;
      }
      if (reso.is_null()) {
         std::cout << "WARNING:: no usable reflections in \"" << file_name << "\""
                   << std::endl;
         return false;
      }

      data_spacegroup = spg_pair.second;
      data_cell       = cell_local;
      data_resolution = reso;

      bool generate = true;
      mydata.init(data_spacegroup, data_cell, data_resolution, generate);
      mydata.add_hkl_list(hkls);

      // init with mydata so that cell, sampling and spacegroup are all set
      my_fsigf.init(mydata, data_cell);
      my_fphi.init( mydata, data_cell);

      // The value columns, all optional. Reading order is the order the old
      // code used, and it matters where a file states more than one: a later
      // import overwrites an earlier one for the same reflection.
      enum { kH, kK, kL, kFmeas, kFsigma, kFsqMeas, kFsqSigma,
             kAcalc, kBcalc, kFcalc, kPhaseCalc, kFsqCalc };
      for (auto row : rl.block->find(rl.prefix, {"index_h", "index_k", "index_l",
                                                 "?F_meas", "?F_sigma",
                                                 "?F_squared_meas", "?F_squared_sigma",
                                                 "?A_calc", "?B_calc",
                                                 "?F_calc", "?phase_calc",
                                                 "?F_squared_calc"})) {

         if (gemmi::cif::is_null(row[kH]) || gemmi::cif::is_null(row[kK]) ||
             gemmi::cif::is_null(row[kL]))
            continue;
         clipper::HKL hkl(gemmi::cif::as_int(row[kH]),
                          gemmi::cif::as_int(row[kK]),
                          gemmi::cif::as_int(row[kL]));

         double f, sig_f, fsq, fsq_sigma, a, b, phi;

         if (value_of(row, kFmeas, &f) && value_of(row, kFsigma, &sig_f)) {
            clipper::xtype fsigf[2] = { f, sig_f };
            my_fsigf.data_import(hkl, fsigf);
            status = true;
         }

         if (value_of(row, kFsqMeas, &fsq)) {
            if (fsq < 0) fsq = 0;
            clipper::xtype fsigf[2];
            fsigf[0] = sqrt(fsq);
            // The sigma is on F-squared, so it is propagated: sigma(F) =
            // sigma(F^2) / 2F. A missing sigma is hacked in at 1% of F^2, as
            // before -- and F == 0 is guarded, which it was not: the division
            // gave inf for every unobserved reflection in the file.
            if (fsigf[0] > 0.0) {
               if (value_of(row, kFsqSigma, &fsq_sigma))
                  fsigf[1] = 0.5 * fsq_sigma / fsigf[0];
               else
                  fsigf[1] = 0.5 * (0.01 * fsq) / fsigf[0];
            } else {
               fsigf[1] = 0.0;
            }
            my_fsigf.data_import(hkl, fsigf);
            status = true;
         }

         if (value_of(row, kAcalc, &a) && value_of(row, kBcalc, &b)) {
            clipper::xtype fphi[2];
            fphi[0] = sqrt(a*a + b*b);
            fphi[1] = atan2(b, a);
            my_fphi.data_import(hkl, fphi);
            status = true;
         }

         if (value_of(row, kFcalc, &f) && value_of(row, kPhaseCalc, &phi)) {
            clipper::xtype fphi[2] = { f, clipper::Util::d2rad(phi) };
            my_fphi.data_import(hkl, fphi);
            status = true;
         }

         if (value_of(row, kFsqCalc, &fsq) && value_of(row, kPhaseCalc, &phi)) {
            if (fsq < 0) fsq = 0;
            clipper::xtype fphi[2] = { sqrt(fsq), clipper::Util::d2rad(phi) };
            my_fphi.data_import(hkl, fphi);
            status = true;
         }
      }

      if (! status)
         std::cout << "WARNING:: \"" << file_name << "\" states no structure "
                   << "factors that this reader understands" << std::endl;
   }

   catch (const std::exception &e) {
      std::cout << "ERROR:: reading small-molecule data cif \"" << file_name
                << "\": " << e.what() << std::endl;
      return false;
   }

   return status;
}

clipper::Xmap<float>
coot::smcif::map() const {

   clipper::Xmap<float> xmap;
   if (! data_cell.is_null()) { // cell is good
      if (! data_spacegroup.is_null()) { // space group is good
         if (! data_resolution.is_null()) { // resolution is good

            clipper::Grid_sampling gs(data_spacegroup, data_cell, data_resolution);
            xmap.init(data_spacegroup, data_cell, gs);
            xmap.fft_from(my_fphi);
         }
      }
   }
   return xmap;
}

bool
coot::smcif::check_for_f_phis() const {

   bool have = false;
   clipper::HKL_info::HKL_reference_index hri;

   unsigned int n_phis = 0;
   for (hri = my_fphi.first(); !hri.last(); hri.next()) {
      if (! clipper::Util::isnan(my_fphi[hri].phi())) {
         n_phis++;
         if (false)
            std::cout << "check_for_f_phis " << hri.hkl().format() << " phi "
                      << my_fphi[hri].phi()
                      << std::endl;
      }
   }

   // std::cout << "smcif::check_for_f_phis() n_phis " << n_phis << std::endl;

   if (n_phis > 0)
      have = true;
   
   return have;
}


std::pair<clipper::Xmap<float>, clipper::Xmap<float> >
coot::smcif::sigmaa_maps_by_calc_sfs(mmdb::Atom **atom_selection, int n_selected_atoms) {

   std::pair<clipper::Xmap<float>, clipper::Xmap<float> > p;
   clipper::HKL_sampling hkl_sampling_local(mydata.cell(), data_resolution);

   if (! my_fsigf.is_null()) { // cell is good
      if (! my_fsigf.is_null()) { // space group is good
         if (! data_resolution.is_null()) { // resolution is good
   
            clipper::HKL_info::HKL_reference_index ih;

            clipper::HKL_data< clipper::datatypes::F_phi<float> > my_fphi_local(mydata.spacegroup(),
                                                                                mydata.cell(),
                                                                                hkl_sampling_local);
            clipper::HKL_data< clipper::datatypes::F_phi<float> > my_fphi_fofc(mydata.spacegroup(),
                                                                                mydata.cell(),
                                                                                hkl_sampling_local);
            clipper::HKL_data< clipper::datatypes::F_phi<float> > my_fphi_2fofc(mydata.spacegroup(),
                                                                                mydata.cell(),
                                                                                hkl_sampling_local);
            // get a list of all the atoms
            clipper::MMDBAtom_list atoms(atom_selection, n_selected_atoms);
            clipper::HKL_data< clipper::datatypes::F_phi<float> > fphidata(mydata.spacegroup(),
                                                                           mydata.cell(),
                                                                           hkl_sampling_local);
            const clipper::HKL_info& hkls = my_fsigf.hkl_info();
            // clipper::SFcalc_iso_fft<float>(my_fphi_local, atoms);
            clipper::SFcalc_aniso_fft<float>(my_fphi_local, atoms);

            int nprm = 10;
            std::vector<clipper::ftype> params_init( nprm, 1.0 );
            clipper::BasisFn_spline basis_f1f2(hkls, nprm, 2.0);
            clipper::TargetFn_scaleF1F2<clipper::datatypes::F_phi<float>, clipper::datatypes::F_sigF<float> > target_f1f2(my_fphi_local, my_fsigf);
            clipper::ResolutionFn fscale(hkls, basis_f1f2, target_f1f2, params_init);
            float multiplier = 2.0;

            for (ih=my_fsigf.first(); !ih.last(); ih.next()) {
               if (!my_fsigf[ih.hkl()].missing()) {
                  my_fphi_2fofc[ih].f() = 2.0 * my_fsigf[ih].f() - my_fphi_local[ih].f()*sqrt(fscale.f(ih));
                  my_fphi_fofc[ih].f() = my_fsigf[ih].f() - my_fphi_local[ih].f()*sqrt(fscale.f(ih));
                  my_fphi_fofc[ih].phi()  = my_fphi_local[ih].phi();
                  my_fphi_2fofc[ih].phi() = my_fphi_local[ih].phi();
               } else {
                  my_fphi_fofc[ih].f()  = 0.0;
                  my_fphi_2fofc[ih].f() = 0.0;
                  my_fphi_fofc[ih].phi()  = 0.0;
                  my_fphi_2fofc[ih].phi() = 0.0;
               }
            }

            // fft
            clipper::Xmap<float> xmap_fofc;
            clipper::Xmap<float> xmap_2fofc;
            
            clipper::Grid_sampling gs(mydata.spacegroup(),
                                      mydata.cell(), 
                                      data_resolution);
            xmap_fofc.init( mydata.spacegroup(), mydata.cell(), gs);
            xmap_2fofc.init(mydata.spacegroup(), mydata.cell(), gs);
            xmap_fofc.fft_from(my_fphi_fofc);  // generate map
            xmap_2fofc.fft_from(my_fphi_2fofc); // generate map

            p = std::pair<clipper::Xmap<float>, clipper::Xmap<float> > (xmap_2fofc, xmap_fofc);
         }
      }
   }
   return p;
}



std::pair<clipper::Xmap<float>, clipper::Xmap<float> >
coot::smcif::sigmaa_maps() {

   bool debug = false;
   clipper::Xmap<float> xmap;
   clipper::Xmap<float> xmap_diff;

   // stop early if we don't have phases
   bool f_phis = check_for_f_phis();
   if (! f_phis) {
      std::cout << "WARNING:: No (f_calc, phi_calc)s in file" << std::endl;
      return std::pair<clipper::Xmap<float>, clipper::Xmap<float> > (xmap, xmap_diff);
   }
   
   if (! data_cell.is_null()) { // cell is good
      if (! data_spacegroup.is_null()) { // space group is good
         if (! data_resolution.is_null()) { // resolution is good

            if (debug)
               std::cout << "cell, spacegroup, resolution is good" << std::endl;

            typedef clipper::HKL_data_base::HKL_reference_index HRI;
            clipper::Grid_sampling gs(data_spacegroup, data_cell, data_resolution);
            const clipper::HKL_info &hkls = mydata;

            clipper::HKL_data<clipper::datatypes::Phi_fom<float> > phiw(hkls, data_cell);
            clipper::HKL_data<clipper::datatypes::F_phi<float> >     fb(hkls, data_cell);
            clipper::HKL_data<clipper::datatypes::F_phi<float> >     fd(hkls, data_cell);
            clipper::HKL_data<clipper::datatypes::Flag>           flags(hkls, data_cell);

            clipper::HKL_info::HKL_reference_index hri;
            for (hri = flags.first(); !hri.last(); hri.next() )
               flags[hri].flag() = clipper::SFweight_spline<float>::BOTH;
            for (hri = phiw.first(); !hri.last(); hri.next() ) {
               phiw[hri].phi() = my_fphi[hri].phi();
               phiw[hri].fom() = 1;
            }

            if (debug) { 
               std::cout << "---------------- filling sigmaa_maps ... " << std::endl;
               std::cout << "spacegroup" << data_spacegroup.descr().symbol_hm()<< std::endl;

               for (hri = my_fsigf.first(); !hri.last(); hri.next()) {
                  std::cout << "my_fsigf f: " << hri.hkl().format() << " "
                            << my_fsigf[hri].f() << std::endl;
               }
               for (hri = my_fphi.first(); !hri.last(); hri.next() ) {
                  std::cout << " my_fphi f: " << hri.hkl().format() << " "
                            << my_fphi[hri].f() << " phi: " << my_fphi[hri].phi()
                            << std::endl;
               }
               for (hri = phiw.first(); !hri.last(); hri.next() ) {
                  std::cout << "   phiw: " << hri.hkl().format() << " "
                            << phiw[hri].phi() << " "
                            << phiw[hri].fom() << std::endl;
               }
            }

            // int n_refln = hkls.num_reflections();
            int n_refln = 1000;
            int n_param = 20;
            clipper::SFweight_spline<float> sfw(n_refln, n_param);
            // fb returns with f() full of -nans.  I don't understand why.
            sfw(fb, fd, phiw, my_fsigf, my_fphi, flags);

            if (debug)
               for (hri = fb.first(); !hri.last(); hri.next())
                  std::cout << "   " << hri.hkl().format() << " " 
                            << "fb f " << fb[hri].f() << " phi " << fb[hri].phi() << "\n";
            
            xmap.init(data_spacegroup, data_cell, gs);
            xmap.fft_from(fb);

            xmap_diff.init(data_spacegroup, data_cell, gs);
            xmap_diff.fft_from(fd);

            int n_points = 0;
            clipper::Xmap_base::Map_reference_index ix;

            if (debug) { 
               for (ix = xmap.first(); !ix.last(); ix.next() ) {
                  n_points++;
                  std::cout << xmap[ix] << " ";
                  if (n_points%60 == 0) std::cout << "\n";
               }
            }
         }
      }
   }
   return std::pair<clipper::Xmap<float>, clipper::Xmap<float> > (xmap, xmap_diff);
}




// int main(int argc, char **argv) {

//    if (argc > 1) {
//       mmdb::InitMatType(); // delete me when not stand-alone
//       std::string file_name = argv[1];
//       coot::smcif smcif;
//       mmdb::Manager *mol = smcif.read_sm_cif(file_name);
//    }
//    clipper::ClipperInstantiator::instance().destroy();
//    return 0;
// } 


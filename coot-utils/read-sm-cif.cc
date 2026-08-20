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

#include "compat/coot-sysdep.h"
#include "utils/coot-utils.hh"
#include "geometry/residue-and-atom-specs.hh"

#include "read-sm-cif.hh"



// This can throw a std::runtime_error.
// 
clipper::Cell
coot::smcif::get_cell(mmdb::mmcif::PData data) const {

   mmdb::pstr cell_a = NULL;
   mmdb::pstr cell_b = NULL;
   mmdb::pstr cell_c = NULL;
   mmdb::pstr cell_alpha = NULL;
   mmdb::pstr cell_beta  = NULL;
   mmdb::pstr cell_gamma = NULL;
   
   int ierr = 0;
   ierr += data->GetString (cell_a,     "" ,"_cell_length_a");
   ierr += data->GetString (cell_b,     "" ,"_cell_length_b");
   ierr += data->GetString (cell_c,     "" ,"_cell_length_c");
   ierr += data->GetString (cell_alpha, "" ,"_cell_angle_alpha");
   ierr += data->GetString (cell_beta,  "" ,"_cell_angle_beta");
   ierr += data->GetString (cell_gamma, "" ,"_cell_angle_gamma");

   clipper::Cell cell;

   if (! ierr) {
      if (false)
         std::cout << "make cell from " 
                   << cell_a << " " 
                   << cell_b << " " 
                   << cell_c << " " 
                   << cell_alpha << " " 
                   << cell_beta  << " " 
                   << cell_gamma << " " 
                   << std::endl;
      std::vector<std::string> a_v     = coot::util::split_string_no_blanks(cell_a, "(");
      std::vector<std::string> b_v     = coot::util::split_string_no_blanks(cell_b, "(");
      std::vector<std::string> c_v     = coot::util::split_string_no_blanks(cell_c, "(");
      std::vector<std::string> alpha_v = coot::util::split_string_no_blanks(cell_alpha, "(");
      std::vector<std::string> beta_v  = coot::util::split_string_no_blanks(cell_beta,  "(");
      std::vector<std::string> gamma_v = coot::util::split_string_no_blanks(cell_gamma, "(");

      double a     = coot::util::string_to_float(a_v[0]);
      double b     = coot::util::string_to_float(b_v[0]);
      double c     = coot::util::string_to_float(c_v[0]);
      double alpha = coot::util::string_to_float(alpha_v[0]);
      double beta  = coot::util::string_to_float( beta_v[0]);
      double gamma = coot::util::string_to_float(gamma_v[0]);
      clipper::Cell_descr cell_descr(a,b,c,
                                     clipper::Util::d2rad(alpha),
                                     clipper::Util::d2rad(beta),
                                     clipper::Util::d2rad(gamma));
      cell.init(cell_descr);
   } else {
      std::string mess = "failed to get cell";
      throw std::runtime_error(mess);
   } 
   // Oh dear, we are returning an empty cell, maybe sometimes
   return cell; // shouldn't happen because we throw an exception in the other path.
}


// 
std::pair<bool,clipper::Spacegroup>
coot::smcif::get_space_group(const std::vector<std::string> &symm_strings) const {

   bool status = false;
   std::string symmetry_ops;
   for (unsigned int isym=0; isym<symm_strings.size(); isym++) { 
      symmetry_ops += symm_strings[isym];
      symmetry_ops += " ; ";
   }
   clipper::Spacegroup space_group;
   clipper::Spgr_descr spg_descr(symmetry_ops, clipper::Spgr_descr::Symops);

   if (spg_descr.spacegroup_number() == 0) {
      // Failed.
      std::cout << "Failed to init space_group description with symop strings " << symmetry_ops << std::endl;
      
   } else {
      // Happy path
      space_group.init(spg_descr);
      status = true;
      if (false)
         std::cout << "DEBUG:: space group initialised with symbol \""
                   << space_group.symbol_xhm() << "\"" << std::endl;
   }
   return std::pair<bool,clipper::Spacegroup>(status, space_group);
}

namespace {

   // BANDICOOT v0.2, Interlude B nomenclature sweep.
   //
   // A CIF may state its space group WITHOUT listing the operators: the
   // operator loop is optional in the CIF core dictionary, and a name or an
   // International Tables number is a complete specification on its own. Before
   // this, such a file loaded NOTHING -- not "no symmetry", nothing at all,
   // because the atom loop was read inside the operator-loop branch. Measured
   // on a real COD file with its operator loop removed: cell parsed, 31 atoms
   // present and parseable, whole file rejected.
   //
   // ORDER MATTERS AND IT IS NOT THE OBVIOUS ONE. The number is tried LAST,
   // because an International Tables number does not fix the SETTING: P 1 21/c 1
   // and P 1 21/n 1 are both number 14, so resolving from the number alone can
   // silently produce a different space group from the one the file means. The
   // Hall symbol is unambiguous by construction, and an H-M name carries its
   // setting ("P 1 21/n 1"), so both are better evidence than the number.
   //
   // Both spellings of each tag are accepted: the `_symmetry_*` forms are the
   // CIF 1.0 names, the `_space_group_*` forms superseded them, and files in
   // circulation carry either or both -- the two COD files checked while writing
   // this use `_symmetry_space_group_name_H-M` while a third uses
   // `_space_group_name_H-M_alt`.
   std::vector<std::string>
   symops_from_space_group_statement(mmdb::mmcif::Data *data) {

      std::vector<std::string> symops;
      if (! data) return symops;

      const gemmi::SpaceGroup *sg = NULL;
      const char *how = NULL;

      // 1. Hall symbol -- unambiguous, so it is preferred.
      const char *hall_tags[] = { "_space_group_name_Hall",
                                  "_symmetry_space_group_name_Hall", NULL };
      for (int i=0; hall_tags[i] && !sg; i++) {
         mmdb::pstr S = NULL;
         if (data->GetString(S, "", hall_tags[i]) == 0 && S) {
            std::string hall = coot::util::remove_whitespace(std::string(S));
            if (! hall.empty()) {
               try {
                  gemmi::GroupOps ops = gemmi::symops_from_hall(std::string(S).c_str());
                  if (! ops.sym_ops.empty()) {
                     sg = gemmi::find_spacegroup_by_ops(ops);
                     how = hall_tags[i];
                     if (! sg) {
                        // A valid Hall symbol gemmi cannot name is still a
                        // valid operator set: use it directly.
                        for (const gemmi::Op &op : ops.all_ops_sorted())
                           symops.push_back(op.triplet());
                        std::cout << "INFO:: symmetry taken from " << hall_tags[i]
                                  << ": " << symops.size() << " operators"
                                  << std::endl;
                        return symops;
                     }
                  }
               }
               catch (const std::exception &e) { sg = NULL; }
            }
         }
      }

      // 2. Hermann-Mauguin name -- carries the setting.
      const char *hm_tags[] = { "_space_group_name_H-M_alt",
                                "_symmetry_space_group_name_H-M",
                                "_space_group_name_H-M_full", NULL };
      for (int i=0; hm_tags[i] && !sg; i++) {
         mmdb::pstr S = NULL;
         if (data->GetString(S, "", hm_tags[i]) == 0 && S) {
            std::string name(S);
            if (! coot::util::remove_whitespace(name).empty()) {
               sg = gemmi::find_spacegroup_by_name(name);
               if (sg) how = hm_tags[i];
            }
         }
      }

      // 3. International Tables number -- last resort, and say so out loud,
      //    because the setting is being assumed rather than read.
      const char *number_tags[] = { "_space_group_IT_number",
                                    "_symmetry_Int_Tables_number", NULL };
      for (int i=0; number_tags[i] && !sg; i++) {
         int n = 0;
         if (data->GetInteger(n, "", number_tags[i]) == 0 && n > 0 && n <= 230) {
            sg = gemmi::find_spacegroup_by_number(n);
            if (sg) {
               how = number_tags[i];
               std::cout << "WARNING:: space group taken from " << number_tags[i]
                         << " = " << n << " (" << sg->xhm() << "). The number does "
                         << "not specify the setting, so this is the standard one "
                         << "and may not be the file's." << std::endl;
            }
         }
      }

      if (! sg) return symops;

      for (const gemmi::Op &op : sg->operations().all_ops_sorted())
         symops.push_back(op.triplet());

      std::cout << "INFO:: no symmetry-operator loop; symmetry taken from "
                << (how ? how : "?") << " as " << sg->xhm() << " -- "
                << symops.size() << " operators" << std::endl;
      return symops;
   }
}

std::vector<mmdb::Atom *>
coot::smcif::read_coordinates(mmdb::mmcif::PData data, const clipper::Cell &cell, const clipper::Spacegroup &spg) const {

   std::vector<mmdb::Atom *> atom_vec;
   const char *loopTagsAtom[6] = { "_atom_site_label",
                                   "_atom_site_fract_x",
                                   "_atom_site_fract_y",
                                   "_atom_site_fract_z",
                        //            "_atom_site_type_symbol",
                        //            "_atom_site_disorder_assembly",
                        //            "_atom_site_disorder_group",
                                   ""};
   const char *loopTagsAniso[2] = { "_atom_site_aniso_label", ""};


   int ierr = 0;
   mmdb::pstr S = NULL;
   mmdb::mmcif::Loop *loop = data->FindLoop(loopTagsAtom);

   if (loop) {
      int ll = loop->GetLoopLength();
      if (ll >= 0) {

         char *label  = NULL;
         mmdb::realtype xf,yf,zf, occ, tf;
         int symmetry_multiplicity;
         mmdb::realtype x,y,z;
         char *disorder_assembly = NULL;
         char *disorder_group = NULL;
         std::string alt_loc;

         for (int il=0; il<ll; il++) {

            int ierr_tot = 0;
            label  = loop->GetString(loopTagsAtom[0], il, ierr);
            ierr_tot += ierr;
            loop->GetReal(xf, loopTagsAtom[1], il, ierr);
            ierr_tot += ierr;
            loop->GetReal(yf, loopTagsAtom[2], il, ierr);
            ierr_tot += ierr;
            loop->GetReal(zf, loopTagsAtom[3], il, ierr);
            ierr_tot += ierr;

            // This (the element specifier e.g "Mg+2") may not exist
            // (strangely enough)
            //
            int ierr_symbol = 0;
            std::string symbol;
            char *symbol_c = NULL;
            symbol_c = loop->GetString("_atom_site_type_symbol", il, ierr_symbol);

            if (! symbol_c) {
               // symbol_c was not set
               
               if (ierr_symbol) {
                  // this can happen: 4313232.cif
                  
                  // strip numbers from the label and use that as a symbol
                  std::string s = label;
                  s.erase(std::remove_if(s.begin(), s.end(), (int(*)(int))std::isdigit), s.end());
                  symbol = s;

               } else {
                  // something strange
                  symbol = "UNK";
               }
            } else {
               // normal path
               symbol = symbol_c;
            }

            // this may not exist
            //
            alt_loc.clear();
            disorder_group  = loop->GetString("_atom_site_disorder_assembly", il, ierr);
            if (! ierr) {
               if (disorder_group == NULL) { 
                  // std::cout << "disorder_group NULL" << std::endl;
               } else { 
                  std::cout << "disorder_group " << disorder_group << std::endl;
                  alt_loc = disorder_group;
               }
            }
            
            occ = 1; // hack
            tf = 10.0;
            symmetry_multiplicity = 1;

            // can we get a real value for tf?
            //
            // BANDICOOT v0.2, Interlude B. Two things here, and the second one
            // is a bug this sweep found rather than a nomenclature gap:
            //
            // 1. Accept B as well as U. The CIF core dictionary defines BOTH
            //    _atom_site_U_iso_or_equiv and _atom_site_B_iso_or_equiv. Only
            //    the U form is scaled; B is already what mmdb wants.
            //
            // 2. WARNING: Loop::GetReal CANNOT BE TRUSTED TO REPORT AN ABSENT TAG.
            //    Measured: on a file with no U tag at all it returns success and
            //    leaves the target untouched, so the scaling below was applied to
            //    the DEFAULT and every atom came out with B = 789.57
            //    (= 10 x 8pi^2). Both real COD files checked while writing this
            //    (1000041, 2000001) state no displacement parameter and so had
            //    every atom at 789.57. The same distrust is already applied to
            //    _atom_site_type_symbol above -- "this may not exist (strangely
            //    enough)" -- and for the same reason: GetString DOES report
            //    absence, by returning NULL.
            int ierr_str = 0;
            if (loop->GetString("_atom_site_U_iso_or_equiv", il, ierr_str)) {
               int ierr_u = 0;
               loop->GetReal(tf, "_atom_site_U_iso_or_equiv", il, ierr_u);
               if (! ierr_u) tf *= 8 * M_PI * M_PI;  // PDB-scaled
               else          tf = 10.0;
            } else {
               ierr_str = 0;
               if (loop->GetString("_atom_site_B_iso_or_equiv", il, ierr_str)) {
                  int ierr_b = 0;
                  loop->GetReal(tf, "_atom_site_B_iso_or_equiv", il, ierr_b);
                  if (ierr_b) tf = 10.0;
               }
            }
            
            // real value for occ?
            int ierr_occ = 0;
            loop->GetReal(occ, "_atom_site_occupancy", il, ierr_occ);

            int ierr_symm_mult = 0;
            loop->GetInteger(symmetry_multiplicity, "_atom_site_symmetry_multiplicity", il, ierr_symm_mult);

            if (ierr_tot == 0) {
               mmdb::Atom *at = new mmdb::Atom;
               clipper::Coord_frac cf(xf,yf,zf);
               clipper::Coord_orth co = cf.coord_orth(cell);
               mmdb::realtype occ_symm = occ;
               if (ierr_symm_mult == 0)
                  occ_symm /= float(symmetry_multiplicity);
               at->SetCoordinates(co.x(), co.y(),co.z(), occ_symm, tf);
               // label -> 4c atom name conversion? 
               at->SetAtomName(label);
               std::pair<std::string, int> ele = symbol_to_element(symbol);

               int charge = 0;
               if (ele.first == "NA") charge = 1;
               if (ele.first == "K")  charge = 1;
               if (ele.first == "LI") charge = 1;
               if (ele.first == "RU") charge = 1;
               if (ele.first == "CS") charge = 1;
               if (ele.first == "MG") charge = 2;
               if (ele.first == "CA") charge = 2;
               if (ele.first == "SR") charge = 2;
               if (ele.first == "F")  charge = -1;
               if (ele.first == "CL") charge = -1;
               if (ele.first == "BR") charge = -1;
               if (ele.first == "I")  charge = -1;

               at->charge = charge;

               if (alt_loc.length())
                  strncpy(at->altLoc, alt_loc.c_str(), (alt_loc.size()+1)); // shove.

               if (false)
                  std::cout << " found atom: \"" << label << "\" symbol: \"" << symbol
                            << "\" ele: \"" << ele.first << "\" " << ele.second << " alt-loc \""
                            << alt_loc << "\" " << cf.format() << std::endl;
               at->SetElementName(ele.first.c_str());
               at->Het = 1; // all SM cifs atoms are HETATMs :)
               atom_vec.push_back(at);
            } else {
               if (true)
                  std::cout << "WARNING:: reject atom at loop count " << il << std::endl;
            }
         }
      }
   }

   // Aniso atoms
   //
   std::vector<coot::simple_sm_u> u_aniso_vec;
   
   // loop = data->FindLoop((pstr *) loopTagsAniso);
   loop = data->FindLoop(loopTagsAniso);
   if (loop) {
      int ll = loop->GetLoopLength();
      char *label  = NULL;
      mmdb::realtype u11=-1, u22=-1, u33=-1, u12=-1, u13=-1, u23=-1;
      int ierr_tot = 0;
      for (int il=0; il<ll; il++) {
         int ierr_tot = 0;
         label  = loop->GetString(loopTagsAniso[0], il, ierr);
         ierr_tot += ierr;
         loop->GetReal(u11, "_atom_site_aniso_U_11", il, ierr);
         ierr_tot += ierr;
         loop->GetReal(u22, "_atom_site_aniso_U_22", il, ierr);
         ierr_tot += ierr;
         loop->GetReal(u33, "_atom_site_aniso_U_33", il, ierr);
         ierr_tot += ierr;
         loop->GetReal(u12, "_atom_site_aniso_U_12", il, ierr);
         ierr_tot += ierr;
         loop->GetReal(u13, "_atom_site_aniso_U_13", il, ierr);
         ierr_tot += ierr;
         loop->GetReal(u23, "_atom_site_aniso_U_23", il, ierr);
         ierr_tot += ierr;

         if (! ierr_tot) {
            // label -> atom name conversion here?
            if ((u11>0) && (u22>0) && (u33>0)) {
               coot::simple_sm_u smu(label, u11, u22, u33, u12, u13, u23);
               u_aniso_vec.push_back(smu);
            }
         }
      }

      // now put those aniso Us into the atom_vec;
      //
      double a = cell.a();
      double b = cell.b();
      double c = cell.c();
      for (unsigned int ianiso=0; ianiso<u_aniso_vec.size(); ianiso++) { 
         for (unsigned int iat=0; iat<atom_vec.size(); iat++) {
            mmdb::Atom *at = atom_vec[iat];
            if (u_aniso_vec[ianiso].label == std::string(at->GetAtomName())) {
               clipper::U_aniso_frac caf(u11/(a*a), u22/(b*b), u33/(c*c),
                                         u12/(a*b), u13/(a*c), u23/(b*c));
               clipper::U_aniso_orth cao = caf.u_aniso_orth(cell);
               at->u11 = cao(0,0);
               at->u22 = cao(1,1);
               at->u33 = cao(2,2);
               at->u12 = cao(0,1);
               at->u13 = cao(0,2);
               at->u23 = cao(1,2);
               at->WhatIsSet |= mmdb::ASET_Anis_tFac; // is anisotropic
            }
         }
      }
   }
   return atom_vec;
}

std::pair<std::string, int>
coot::smcif::symbol_to_element(const std::string &symbol) const {

   std::string s = symbol;
   std::string::size_type l = symbol.length();
   int sign_mult = 1;
   int oxidation_state = 0;
   for (std::string::size_type i=0; i<l; i++) {
      char c = symbol[i];
      if (c >= '0' && c <= '9') { 
         s[i] = ' ';
         oxidation_state = c - 48;
      }
      if (c == '+')
         s[i] = ' ';
      if (c == '-') {
         s[i] = ' ';
         sign_mult = -1; 
      } 
   }
   std::string s1 = util::upcase(util::remove_whitespace(s));
   if (s1.length() == 1)
      s1 = " " + s1;
   return std::pair<std::string, int> (s1, oxidation_state * sign_mult);
}



mmdb::Manager *
coot::smcif::read_sm_cif(const std::string &file_name) const {

   mmdb::Manager *mol = NULL;
   mmdb::pstr S = NULL;
   mmdb::mmcif::Data *data = new mmdb::mmcif::Data();
   data->SetFlag (mmdb::mmcif::CIFFL_SuggestCategories);
   int ierr = data->ReadMMCIFData (file_name.c_str());
   if (ierr) {
      std::cout << "WARNING:: Error reading small-molecule cif \"" << file_name << "\"" << std::endl;
   } else { 

// testing      
//       int ierr = data->GetString (S, "" ,"_chemical_formula_sum");
//       if (! ierr) { 
//          printf("chemical-formula-sum: %s\n", S);
//       } else {
//          printf("error getting chemical-formula-sum string.\n");
//       } 

      ierr = data->GetString (S, "", "_[local]_cod_chemical_formula_sum_orig");
      if (!ierr)
         printf("_[local]_cod_chemical_formula_sum_orig: %s\n", S);

      try { 
         clipper::Cell cell = get_cell(data);
         std::cout << "INFO:: got cell from cif: " << cell.format() << std::endl;

         std::vector<std::string> symm_strings;

         // BANDICOOT v0.2: accept BOTH spellings of the symmetry-operator loop.
         // "_symmetry_equiv_pos_as_xyz" is the CIF 1.0 name; the CIF core
         // dictionary superseded it with "_space_group_symop_operation_xyz",
         // which is what SHELXL, Olex2 and COD have emitted for years. Knowing
         // only the deprecated name meant Bandicoot could not open ANY modern
         // small-molecule CIF -- and because the atom reading below sits INSIDE
         // this branch, such a file failed to load entirely (the cell was parsed
         // and then thrown away) rather than merely losing its symmetry.
         const char *sym_tag_candidates[2] = { "_symmetry_equiv_pos_as_xyz",
                                               "_space_group_symop_operation_xyz" };
         const char *sym_tag = NULL;
         mmdb::mmcif::PLoop loop = NULL;
         for (int i_tag=0; i_tag<2 && !loop; i_tag++) {
            const char *loopTag1[2] = { sym_tag_candidates[i_tag], "" };
            loop = data->FindLoop(loopTag1);
            if (loop)
               sym_tag = sym_tag_candidates[i_tag];
         }

         if (loop) {
            int ll = loop->GetLoopLength();
            if (ll > 0) {
               for (int il=0; il<ll; il++) {

                  S = loop->GetString(sym_tag, il, ierr);
                  if (! ierr) {
                     // std::cout << "symmetry: " << S << std::endl;
                     symm_strings.push_back(S);
                  } else {
                     std::cout << "error reading " << sym_tag << " string.\n";
                  }
               }
            }
         }

         // BANDICOOT v0.2, Interlude B: no operator loop is not an error. The
         // loop is OPTIONAL in the CIF core dictionary -- a name or an
         // International Tables number specifies the group just as well.
         if (symm_strings.empty())
            symm_strings = symops_from_space_group_statement(data);

         // Resolve the space group if we can, but do NOT make the coordinates
         // depend on it.
         //
         // THE ATOM LOOP USED TO BE READ INSIDE THE SYMMETRY BRANCH, so any
         // file whose symmetry could not be resolved -- one unrecognised
         // spelling, one space group clipper would not take -- was rejected
         // ENTIRELY, having already parsed the cell and with a perfectly
         // readable atom loop in hand. That is the shape of the
         // `_symmetry_equiv_pos_as_xyz` bug: the earlier fix added the missing
         // spelling but left the nesting, so the next surprise would have failed
         // the same way. Structure beats enumeration -- an unknown symmetry
         // spelling now costs the symmetry, not the molecule, and Coot already
         // has a non-crystallographic path for a model without a space group.
         bool have_spg = false;
         clipper::Spacegroup spg;
         if (! symm_strings.empty()) {
            try {
               std::pair<bool, clipper::Spacegroup> spg_pair = get_space_group(symm_strings);
               if (spg_pair.first) {
                  spg = spg_pair.second;
                  have_spg = true;
               }
            }
            catch (const clipper::Message_base &exc) {
               // 20130710 clipper::Message_base::text() doesn't exist yet?
               std::cout << "ERROR:: Oops, trouble.  No such spacegroup " << "\n";
            }
         } else {
            std::cout << "WARNING:: no symmetry in this cif (no operator loop, "
                      << "and no space-group name or number that could be "
                      << "resolved) - reading coordinates without it" << std::endl;
         }

         // spg is unused by read_coordinates() -- it takes it and ignores it --
         // so an unresolved space group costs the atoms nothing.
         std::vector<mmdb::Atom *> atoms = read_coordinates(data, cell, spg);
         std::cout << "INFO:: from cif we read " << atoms.size() << " atoms"
                   << std::endl;

         if (atoms.size()) {

            mol = new mmdb::Manager;
            mmdb::Model *model_p = new mmdb::Model;
            mmdb::Chain *chain_p = new mmdb::Chain;
            mmdb::Residue *residue_p = new mmdb::Residue;
            chain_p->SetChainID("");
            residue_p->seqNum = 1;

            // BANDICOOT v0.2, Interlude B: name the residue after the component
            // if the file says what it is, and "LIG" otherwise -- not "XXX".
            //
            // THE NAME IS LOAD-BEARING, not cosmetic. Refinement finds restraints
            // BY RESIDUE NAME, so a molecule called XXX can never be refined: no
            // dictionary anywhere is keyed on XXX, and importing the correct one
            // for the ligand cannot help either, because the names will not
            // match. That is why RSR fails on a small-molecule CIF while the same
            // ligand as a PDB file plus its restraint CIF refines perfectly --
            // the PDB file carries a real residue name and the CIF path threw it
            // away.
            //
            // "LIG" is the default because it is what the tools that generate
            // ligand restraints use (acedrg/elbow via phenix write LIG), so an
            // imported dictionary matches without the user renaming anything.
            //
            // _chem_comp.id is preferred when present because it is the actual
            // CCD code (e.g. AR6), which both names the thing correctly and
            // matches the wwPDB dictionary for it. mmdb may present a dotted tag
            // either whole or split into category+tag depending on how it
            // suggested categories, so both forms are asked for.
            std::string res_name = "LIG";
            {
               mmdb::pstr S_id = NULL;
               int ierr_id = data->GetString(S_id, "", "_chem_comp.id");
               if (ierr_id || ! S_id)
                  ierr_id = data->GetString(S_id, "_chem_comp", "id");
               if (! ierr_id && S_id) {
                  std::string id = coot::util::remove_whitespace(std::string(S_id));
                  if (! id.empty() && id != "." && id != "?")
                     res_name = id;
               }
            }
            std::cout << "INFO:: small-molecule cif: residue named " << res_name
                      << std::endl;
            residue_p->SetResName(res_name.c_str());
            for (unsigned int iat=0; iat<atoms.size(); iat++)
               residue_p->AddAtom(atoms[iat]);
            chain_p->AddResidue(residue_p);
            model_p->AddChain(chain_p);
            mol->AddModel(model_p);

            // The cell is set either way: fractional coordinates are
            // meaningless without it, and it is what the display needs.
            mol->SetCell(cell.a(), cell.b(), cell.c(),
                         clipper::Util::rad2d(cell.alpha()),
                         clipper::Util::rad2d(cell.beta()),
                         clipper::Util::rad2d(cell.gamma()));
            if (have_spg)
               mol->SetSpaceGroup(spg.symbol_xhm().c_str());
         }

      }

      catch (const std::runtime_error &rte) {
         std::cout << "ERROR:: " << rte.what() << std::endl;
      }
   }
      
   delete data;
   // delete S;
   data = NULL;
   S = NULL;

   return mol;
}


clipper::Resolution
coot::smcif::get_resolution(const clipper::Cell &cell,
                            const std::string &file_name) const {

   clipper::HKL hkl;
   int h,k,l;
   mmdb::pstr S = NULL;
   clipper::ftype slim = 0.0;
   mmdb::mmcif::Data *data = new mmdb::mmcif::Data();
   data->SetFlag (mmdb::mmcif::CIFFL_SuggestCategories);
   int ierr = data->ReadMMCIFData (file_name.c_str());
   if (ierr) {
      std::cout << "WARNING:: Error reading small-molecule cif \"" << file_name << "\"" << std::endl;
   } else {

      const char *loopTag_data[4] = { "_refln_index_h",
                                      "_refln_index_k",
                                      "_refln_index_l",
                                      ""};
      std::string h_tag = "_refln_index_h";
      std::string k_tag = "_refln_index_k";
      std::string l_tag = "_refln_index_l";
      
      mmdb::mmcif::PLoop loop = data->FindLoop(loopTag_data);
      if (! loop) {
         const char *loopTag_data_pd[4] = { "_pd_refln_index_h",
                                            "_pd_refln_index_k",
                                            "_pd_refln_index_l",
                                            ""};
         loop = data->FindLoop(loopTag_data_pd);
         if (loop) {
            h_tag = "_pd_refln_index_h";
            k_tag = "_pd_refln_index_k";
            l_tag = "_pd_refln_index_l";
         } 
      } 
      if (loop) {
         int ll = loop->GetLoopLength();
         if (ll > 0) {
            for (int il=0; il<ll; il++) {
                int ierr_h = loop->GetInteger(h, h_tag.c_str(), il);
               int ierr_k = 0;
               int ierr_l = 0;
                if (! ierr_h) {
                   ierr_k = loop->GetInteger(k, k_tag.c_str(), il);
                }
                if (! ierr_k) {
                   ierr_l = loop->GetInteger(l, l_tag.c_str(), il);
                }
               if (!ierr_h && !ierr_k && !ierr_l) {
                  hkl = clipper::HKL(h,k,l);
                  double reso = hkl.invresolsq(cell);
                  // std::cout << "in get_resolution() " << hkl.format() << " has resolution "
                  // << reso << std::endl;
                  slim = clipper::Util::max(slim, reso);
               }
            }
         }
      }
   }
   delete data;
   double reso_A = 1/sqrt(slim);
   // std::cout << "returning clipper::Resolution( " << reso_A << " A)" << std::endl;
   return clipper::Resolution(reso_A);
}


std::pair<bool,clipper::Spacegroup> 
coot::smcif::get_space_group(const std::string &file_name) const {

   std::pair<bool,clipper::Spacegroup> s;
   mmdb::mmcif::Data *data = new mmdb::mmcif::Data();
   data->SetFlag(mmdb::mmcif::CIFFL_SuggestCategories);
   int ierr = data->ReadMMCIFData(file_name.c_str());
   if (! ierr) {
      s  = get_space_group(data);

      if (!s.first) {
         int spg_int = 0;
         ierr = data->GetInteger(spg_int, "", "_space_group_IT_number", 1);
         if (! ierr) {
            clipper::Spgr_descr spgd(spg_int);
            s.first = true;
            s.second = clipper::Spacegroup(spgd);
         }
      }

      if (!s.first) {
         mmdb::pstr S = NULL;
         ierr = data->GetString(S, "", "_symmetry_space_group_name_H-Mxx");
         if (! ierr) { 
            std::string space_group_symbol = S;
            clipper::Spgr_descr spgd(space_group_symbol);
            s.first = true;
            s.second = clipper::Spacegroup(spgd);
         }
      }
   } else {
      std::cout << "WARNING:: get_space_group():: error reading " << file_name << std::endl;
   }
   delete data;
   return s;
}

// c.f. get_cell() from a coords file
clipper::Cell
coot::smcif::get_cell_for_data(const std::string &file_name) const {

   clipper::Cell c;
   mmdb::mmcif::Data *data = new mmdb::mmcif::Data();
   data->SetFlag (mmdb::mmcif::CIFFL_SuggestCategories);
   int ierr = data->ReadMMCIFData (file_name.c_str());
   if (! ierr) {
      c = get_cell_for_data(data);
   }
   delete data;
   return c;
}


void
coot::smcif::setup_hkls(const std::string &file_name) {

   mmdb::mmcif::Data *data = new mmdb::mmcif::Data();
   data->SetFlag (mmdb::mmcif::CIFFL_SuggestCategories);

   int ierr = data->ReadMMCIFData(file_name.c_str());
   if (ierr) {
      std::cout << "WARNING:: Error reading small-molecule cif \"" << file_name
                << "\"" << std::endl;
   } else {
      std::string h_tag = "_refln_index_h";
      std::string k_tag = "_refln_index_k";
      std::string l_tag = "_refln_index_l";
      const char *loopTag_data[4] = { "_refln_index_h",
                                      "_refln_index_k",
                                      "_refln_index_l",
                                      ""};
      
      mmdb::mmcif::Loop *loop = data->FindLoop(loopTag_data);
      if (! loop) {
         const char *loopTag_data_pd[4] = { "_pd_refln_index_h",
                                            "_pd_refln_index_k",
                                            "_pd_refln_index_l",
                                            ""};
         loop = data->FindLoop(loopTag_data_pd);
         if (loop) {
            h_tag = "_pd_refln_index_h";
            k_tag = "_pd_refln_index_k";
            l_tag = "_pd_refln_index_l";
         } 

      } 
      if (loop) {
         clipper::HKL_data_base* f_sigf_input;
         int ll = loop->GetLoopLength();
         int h,k,l;
         std::vector<clipper::HKL> hkls;
         
         clipper::xtype x1[2]; 
         if (ll > 0) {
            for (int il=0; il<ll; il++) {
               ierr = loop->GetInteger(h, h_tag.c_str(), il);
               if (! ierr) {
                  ierr = loop->GetInteger(k, k_tag.c_str(), il);
               }
               if (! ierr) {
                  ierr = loop->GetInteger(l, l_tag.c_str(), il);
               }

               if (! ierr) {
                  clipper::HKL hkl(h,k,l);
                  hkls.push_back(hkl);
               } 
            }
         }
         mydata.add_hkl_list(hkls);
      }
   }
   delete data;
} 



bool
coot::smcif::read_data_sm_cif(const std::string &file_name) {

   bool status = false;
   // These functions each open and close file_name.
   // 
   clipper::Cell cell_local = get_cell_for_data(file_name); // c.f. get_cell() from a coords file
   std::pair<bool,clipper::Spacegroup> spg_pair = get_space_group(file_name);
   clipper::Resolution reso = get_resolution(cell_local, file_name);

   if (false) {
      std::cout << "in read_data_sm_cif() cell is " << cell_local.format() << std::endl;
      std::cout << "in read_data_sm_cif() spg is  " << spg_pair.second.descr().symbol_hm() << std::endl;
      std::cout << "in read_data_sm_cif() reso-limit is  " << reso.limit() << " A" << std::endl;
   }

   if (! cell_local.is_null()) {
      // cell is good
      if (! spg_pair.second.is_null()) {
         // space group is good
         if (! reso.is_null()) {
            // resolution is good

            data_spacegroup = spg_pair.second;
            data_cell = cell_local;
            data_resolution = reso;

            if (false) {
               std::cout << "in read_data_sm_cif() init mydata with spacegroup "
                         << data_spacegroup.descr().symbol_hm() << std::endl;
               std::cout << "in read_data_sm_cif() init mydata with data_cell " << data_cell.format()
                         << std::endl;
               std::cout << "in read_data_sm_cif() init mydata with data_resolution limit "
                         << reso.limit() << " A" << std::endl;
            }

            clipper::HKL_sampling hkl_sampling(data_cell, data_resolution);

            // c.f. mydata construction
            bool generate = true;
            mydata.init(data_spacegroup, data_cell, data_resolution, generate);
            // c.f. import_hkl_info into mydata
            setup_hkls(file_name);

            // to init my_fsigf so that cell_, hkl_sampling_ and spacegroup_ are set,
            // we must init with init(spacegroup, cell, sampling)
            
            my_fsigf.init(mydata, data_cell);
            my_fphi.init( mydata, data_cell);

            // Enabling these causes a crash for fcf unit test.
            // 
            // These were initally added so that (I presumed) COD data can't be used to
            // calculate structure factors.
            // 
            // my_fsigf.init(data_spacegroup, data_cell, hkl_sampling);
            // my_fphi.init( data_spacegroup, data_cell, hkl_sampling); // these may not exist
                                                                     // in the cif file.
            mmdb::mmcif::Data *data = new mmdb::mmcif::Data();
            data->SetFlag (mmdb::mmcif::CIFFL_SuggestCategories);

            int ierr = data->ReadMMCIFData (file_name.c_str());
            if (ierr) {
               std::cout << "WARNING:: Error reading small-molecule cif \"" << file_name
                         << "\"" << std::endl;
            } else {

               const char *loopTag_data[4] = { "_refln_index_h",
                                               "_refln_index_k",
                                               "_refln_index_l",
//                                                 "_refln_F_meas",
//                                                 "_refln_F_sigma",
//                                                 "_refln_F_squared_meas",
//                                                 "_refln_F_squared_sigma",
//                                                 "_refln_F_calc",
//                                                 "_refln_phase_calc",
//                                                 "_refln_A_calc",
//                                                 "_refln_B_calc",
                                               ""};
               std::string h_tag = "_refln_index_h";
               std::string k_tag = "_refln_index_k";
               std::string l_tag = "_refln_index_l";
      
               mmdb::mmcif::Loop *loop = data->FindLoop(loopTag_data);
               if (!loop) {

                  // Do these mean Powder?
                  const char *loopTag_data_pd[4] = { "_pd_refln_index_h",
                                                     "_pd_refln_index_k",
                                                     "_pd_refln_index_l",
                                                     ""};
                  loop = data->FindLoop(loopTag_data_pd);
                  if (loop) {
                     h_tag = "_pd_refln_index_h";
                     k_tag = "_pd_refln_index_k";
                     l_tag = "_pd_refln_index_l";
                  } 
               }
               if (loop) {
                  clipper::HKL_data_base* f_sigf_input;
                  int ll = loop->GetLoopLength();
                  int h,k,l;
                  mmdb::realtype F, sigF, A, B;
                  mmdb::realtype Fsqm, Fsqs;
                  mmdb::realtype fpc_f, fpc_p;
                  clipper::xtype x1[2]; 
                  if (ll > 0) {
                     for (int il=0; il<ll; il++) {
                        ierr = loop->GetInteger(h, h_tag.c_str(), il);
                        if (! ierr) {
                           ierr = loop->GetInteger(k, k_tag.c_str(), il);
                        }
                        if (! ierr) {
                           ierr = loop->GetInteger(l, l_tag.c_str(), il);
                        }

                        int ierr_fsigf = 0;
                        ierr_fsigf = loop->GetReal(F, "_refln_F_meas", il);
                        if (! ierr_fsigf) {
                           ierr_fsigf = loop->GetReal(sigF, "_refln_F_sigma", il);
                        }

                        if (! ierr && ! ierr_fsigf) {
                           x1[0] = F;
                           x1[1] = sigF;
                           clipper::HKL hkl(h,k,l);
                           my_fsigf.data_import(hkl, x1);
                           status = true;
                        }

                        int ierr_f2_1 = loop->GetReal(Fsqm, "_refln_F_squared_meas",  il);
                        int ierr_f2_2 = loop->GetReal(Fsqs, "_refln_F_squared_sigma", il);

                        if (! ierr && ! ierr_f2_1) {
                           clipper::xtype fsigf[2];
                           if (Fsqm < 0) Fsqm = 0;
                           fsigf[0] = sqrt(Fsqm);
                           if (! ierr_f2_2) {
                              fsigf[1] = 0.5 * Fsqs / fsigf[0];
                           } else {
                              // missing sigma. hack in a value
                              fsigf[1] = 0.5 * (0.01* Fsqm) / fsigf[0];
                           } 
                           clipper::HKL hkl(h,k,l);
                           my_fsigf.data_import(hkl, fsigf);
                           status = true;
                        }
                        

                        int ierr_AB_A = loop->GetReal(A, "_refln_A_calc", il);
                        int ierr_AB_B = loop->GetReal(B, "_refln_B_calc", il);

                        if (! ierr && ! ierr_AB_A && ! ierr_AB_B) {
                           clipper::xtype fphi[2];
                           clipper::xtype f = sqrt(A*A + B*B);
                           clipper::xtype phi = atan2(B,A);
                           fphi[0] = f;
                           fphi[1] = phi;
                           clipper::HKL hkl(h,k,l);
                           my_fphi.data_import(hkl, fphi);
                           status = true;
                        }

                        int ierr_f_phi_calc_1 = loop->GetReal(fpc_f, "_refln_F_calc",     il);
                        int ierr_f_phi_calc_2 = loop->GetReal(fpc_p, "_refln_phase_calc", il);

                        if (false) { 
                           std::cout << "ierr " << ierr << " ";
                           std::cout << "ierr_f_phi_calc_1 " << ierr_f_phi_calc_1 << " ";
                           std::cout << "ierr_f_phi_calc_2 " << ierr_f_phi_calc_2 << std::endl;
                        }
                           
                        if (! ierr && ! ierr_f_phi_calc_1 && ! ierr_f_phi_calc_2) {
                           clipper::xtype fphi[2];
                           fphi[0] = fpc_f;
                           fphi[1] = clipper::Util::d2rad(fpc_p);
                           clipper::HKL hkl(h,k,l);
                           my_fphi.data_import(hkl, fphi);
                           status = true;
                        }

                        ierr_f_phi_calc_1 = loop->GetReal(fpc_f, "_refln_F_squared_calc",     il);
                        ierr_f_phi_calc_2 = loop->GetReal(fpc_p, "_refln_phase_calc", il);
                        
                        if (! ierr && ! ierr_f_phi_calc_1 && ! ierr_f_phi_calc_2) {
                           clipper::xtype fphi[2];
                           if (fpc_f < 0) fpc_f = 0;
                           fphi[0] = sqrt(fpc_f);
                           fphi[1] = clipper::Util::d2rad(fpc_p);
                           clipper::HKL hkl(h,k,l);
                           my_fphi.data_import(hkl, fphi);
                           status = true;
                        }
                     }
                  }
               }
            }
         }
      }
   }

   if (false) { // debugging.
      for (clipper::HKL_info::HKL_reference_index hri = my_fsigf.first();
           !hri.last(); hri.next()) {
         std::cout << "read_data_sm_cif():: obs " << my_fsigf[hri].f() << std::endl;
      }
   }
   
   return status;
}

clipper::Cell
coot::smcif::get_cell_for_data(mmdb::mmcif::PData data) const {

   clipper::Cell cell;

   int ierr;
   mmdb::realtype a, b, c;
   mmdb::realtype alpha, beta, gamma;

   ierr = data->GetReal (a, "", "_cell_length_a");
   if (ierr) { 
      std::cout << "Bad cell length a " << std::endl;
   }

   if (! ierr) { 
      ierr = data->GetReal (b, "", "_cell_length_b");
      if (ierr) { 
         std::cout << "Bad cell length b " << std::endl;
      }
   }

   if (! ierr) { 
      ierr = data->GetReal (c, "", "_cell_length_c");
      if (ierr) { 
         std::cout << "Bad cell length c " << std::endl;
      }
   }
   
   if (! ierr) { 
      ierr = data->GetReal (alpha, "", "_cell_angle_alpha");
      if (ierr) { 
         std::cout << "Bad cell angle alpha " << std::endl;
      }
   }

   if (! ierr) { 
      ierr = data->GetReal (beta, "", "_cell_angle_beta");
      if (ierr) { 
         std::cout << "Bad cell angle beta " << std::endl;
      }
   }
   
   if (! ierr) { 
      ierr = data->GetReal (gamma, "", "_cell_angle_gamma");
      if (ierr) { 
         std::cout << "Bad cell angle gamma " << std::endl;
      }
   }

   if (! ierr) {
      clipper::Cell_descr cell_descr(a,b,c,
                                     clipper::Util::d2rad(alpha),
                                     clipper::Util::d2rad(beta),
                                     clipper::Util::d2rad(gamma));
      cell = clipper::Cell(cell_descr);
   }
   return cell;
} 


std::pair<bool,clipper::Spacegroup> 
coot::smcif::get_space_group(mmdb::mmcif::Data *data) const {

   // George is going to update shelxl (or may already have done so) to
   // output _space_group_symop_operation_xyz instead of
   // _symmetry_equiv_pos_as_xyz (in the fcf-file created with "LIST 6")

   std::string    shelxl_style = "_space_group_symop_operation_xyz";
   std::string       old_style = "_symmetry_equiv_pos_as_xyz";
   
   std::pair<bool,clipper::Spacegroup> s = get_space_group_from_loop(data, old_style);
   if (! s.first) 
      s = get_space_group_from_loop(data, shelxl_style);
   return s;
}

std::pair<bool, clipper::Spacegroup> 
coot::smcif::get_space_group(mmdb::mmcif::Data *data, const std::string &symm_tag) const {
   
   bool state = false;
   clipper::Spacegroup spg;
   mmdb::mmcif::Struct *structure = data->GetStructure(symm_tag.c_str());
   if (structure) {
      std::cout << "Hoooray! " << symm_tag << std::endl;
   } else {
      std::cout << "Failed to get structure from " << symm_tag << std::endl;
   } 
   
   return std::pair<bool, clipper::Spacegroup> (state, spg);
}

std::pair<bool,clipper::Spacegroup> 
coot::smcif::get_space_group_from_loop(mmdb::mmcif::Data *data, const std::string &symm_tag) const {

   bool state = false;
   clipper::Spacegroup spg;

   int ierr;
   mmdb::pstr S = NULL;
   std::vector<std::string> symm_strings;
      
   const char *loopTag1[2] = { symm_tag.c_str(), ""};
   int n_tags = 1;

   mmdb::mmcif::PLoop loop = data->FindLoop(loopTag1);

   if (loop) {
      int ll = loop->GetLoopLength();
      if (ll > 0) { 
         for (int il=0; il<ll; il++) {
            for (int itag=0; itag<n_tags; itag++) { 
               S = loop->GetString(loopTag1[itag], il, ierr);
               if (! ierr) {
                  // std::cout << "-------- found S " << S << std::endl;
                  symm_strings.push_back(S);
               } else {
                  std::cout << "error in " << loopTag1[itag] << " string.\n";
               }
            }
         }
      }

      // debug
      if (false) { 
         std::cout << "got these symm strings: " << symm_strings.size() << std::endl;
         for (unsigned int i=0; i<symm_strings.size(); i++) 
            std::cout << "   " << symm_strings[i] << std::endl;
      }
      
      if (symm_strings.size()) {
         std::pair<bool, clipper::Spacegroup> spg_pair = get_space_group(symm_strings);
         return spg_pair;
      }
   }
   return std::pair<bool,clipper::Spacegroup> (state, spg);
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


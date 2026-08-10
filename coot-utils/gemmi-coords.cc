/* coot-utils/gemmi-coords.cc
 *
 * Bandicoot v0.2: the gemmi read path. See gemmi-coords.hh.
 *
 * Every adjustment made here was measured before it was written, using
 * tools/gemmi-diff (which diffs this path against the mmdb reader over a
 * corpus). Each one carries the reason inline, because none of them is
 * guessable from the gemmi API alone.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#include "gemmi-coords.hh"

#include <gemmi/mmread.hpp>    // read_structure_file
#include <gemmi/mmdb.hpp>      // copy_to_mmdb
#include <gemmi/polyheur.hpp>  // setup_entities
#include <gemmi/symmetry.hpp>  // find_spacegroup

#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>

// (5) Rewrite each LINK record's atom names and altLocs to the model's own
//     spelling.
//
// gemmi's transfer_links_to_mmdb copies struct_conn atom names VERBATIM, so a
// link says "C" while the atom it refers to is stored as " C  " (mmdb pads
// names). mmdb's own Residue::GetAtom() is tolerant of that, which makes the
// mismatch easy to miss -- but Coot is not: add_link_bond_templ()
// (coords/Bond_lines.cc:1927) compares with a plain
//     std::string(at->name) == std::string(link->atName1)
// so an unpadded link name matches NOTHING. The links then sit in the table
// looking fine while no link bond is ever drawn and no link restraint is ever
// generated -- silently inert. A PDB file does not hit this because its LINK
// records carry the padded columns already.
//
// Fixing it by copying the FOUND atom's own name (rather than padding by rule)
// also guarantees the link and the model agree however mmdb chose to spell it.
static void normalise_link_atom_names(mmdb::Manager *mol) {

   for (int imod = 1; imod <= mol->GetNumberOfModels(); imod++) {
      mmdb::Model *model = mol->GetModel(imod);
      if (! model) continue;

      int n_links = model->GetNumberOfLinks();
      for (int i_link = 1; i_link <= n_links; i_link++) {
         mmdb::Link *link = model->GetLink(i_link);
         if (! link) continue;

         // partner 1
         if (mmdb::Chain *chain = model->GetChain(link->chainID1)) {
            if (mmdb::Residue *res = chain->GetResidue(link->seqNum1, link->insCode1)) {
               if (mmdb::Atom *at = res->GetAtom(link->atName1, nullptr, link->aloc1)) {
                  strncpy(link->atName1, at->name,   sizeof(link->atName1) - 1);
                  strncpy(link->aloc1,   at->altLoc, sizeof(link->aloc1)   - 1);
                  link->atName1[sizeof(link->atName1) - 1] = '\0';
                  link->aloc1[sizeof(link->aloc1) - 1]     = '\0';
               }
            }
         }

         // partner 2
         if (mmdb::Chain *chain = model->GetChain(link->chainID2)) {
            if (mmdb::Residue *res = chain->GetResidue(link->seqNum2, link->insCode2)) {
               if (mmdb::Atom *at = res->GetAtom(link->atName2, nullptr, link->aloc2)) {
                  strncpy(link->atName2, at->name,   sizeof(link->atName2) - 1);
                  strncpy(link->aloc2,   at->altLoc, sizeof(link->aloc2)   - 1);
                  link->atName2[sizeof(link->atName2) - 1] = '\0';
                  link->aloc2[sizeof(link->aloc2) - 1]     = '\0';
               }
            }
         }
      }
   }
}


bool
coot::gemmi_handles_extension(const std::string &extension) {

   std::string e;
   e.reserve(extension.size());
   for (char c : extension) e += std::tolower(static_cast<unsigned char>(c));

   return (e == ".cif" || e == ".mmcif" || e == ".mcif");
}


mmdb::Manager *
coot::read_coords_with_gemmi(const std::string &file_name, std::string *message) {

   mmdb::Manager *mol = nullptr;

   try {
      gemmi::Structure st = gemmi::read_structure_file(file_name);

      // gemmi accepts a small-molecule CIF and hands back a Structure with no
      // atoms rather than throwing, so this is the fall-back trigger. Checked
      // before any of the work below, which would otherwise operate on nothing.
      long n_atoms = 0;
      for (const gemmi::Model &model : st.models)
         for (const gemmi::Chain &chain : model.chains)
            for (const gemmi::Residue &res : chain.residues)
               n_atoms += static_cast<long>(res.atoms.size());
      if (n_atoms == 0) {
         if (message)
            *message = "no atoms - not a macromolecular coordinate file";
         return nullptr;
      }

      gemmi::setup_entities(st);

      // (1) Merge chains that share a name.
      //
      // copy_to_mmdb() calls CreateChain() per gemmi::Chain and never merges,
      // so a file where one author chain holds polymer + ligands + waters
      // becomes several mmdb chains with the SAME id -- and mmdb's
      // GetChain(id) finds only the first, hiding the rest from every by-name
      // lookup (778 of 6443 atoms, 12%, in 3nyd). merge_chain_parts() is
      // gemmi's own remedy; with the default min_sep=0 it concatenates
      // residues WITHOUT renumbering them. Must run AFTER setup_entities,
      // which is what creates the split.
      st.merge_chain_parts();

      // (2) Do not let hydrogen bonds become link restraints.
      //
      // gemmi's transfer_links_to_mmdb copies every struct_conn row and
      // ignores its type; Coot's fill_links() then hands every mmdb LINK to
      // the refinement without filtering either. A phenix-refined file can
      // carry hundreds of Hydrog rows (660 in one of ours), and an H-bond at
      // ~2.9 A must not become a link restraint pulling toward ~1.4 A.
      // Bandicoot's RSR deliberately does not restrain H-bonds: helix and
      // sheet networks are already covered by the secondary-structure
      // restraints, and a user who wants a particular one can add it with
      // Make Link. disulf/covale/metalc are kept.
      //
      // NOTE this filters only what reaches mmdb's LINK table. It is not a
      // fidelity loss: the struct_conn category itself is preserved verbatim
      // on write (Phase 3 passthrough).
      {
         std::vector<gemmi::Connection> keep;
         keep.reserve(st.connections.size());
         for (const gemmi::Connection &con : st.connections)
            if (con.type != gemmi::Connection::Hydrog)
               keep.push_back(con);
         st.connections = std::move(keep);
      }

      // (3) Normalise the space-group name before mmdb sees it.
      //
      // st.spacegroup_hm holds the RAW string from the file, and mmdb's
      // SetSpaceGroup is strict: it rejects anything non-canonical
      // ("P212121", "C2", lower case) and leaves the model with NO space
      // group, so symmetry silently goes away. gemmi recognises all those
      // spellings, so asking it for the canonical form first turns a
      // silently symmetry-less load into a correct one. Relevant because
      // these are hand-editable text files.
      if (const gemmi::SpaceGroup *sg = st.find_spacegroup())
         st.spacegroup_hm = sg->xhm();

      const bool had_cell = st.cell.is_crystal();

      mol = new mmdb::Manager;
      gemmi::copy_to_mmdb(st, mol);

      // (5) see the note above normalise_link_atom_names(). Must run AFTER
      // copy_to_mmdb, since it reconciles the LINK records against the atoms
      // that call has just created.
      normalise_link_atom_names(mol);

      // (4) Do not fabricate a unit cell.
      //
      // copy_to_mmdb calls PutCell unconditionally, and gemmi's default
      // UnitCell is 1,1,1,90,90,90 -- so a model with no cell at all (an NMR
      // ensemble, or an EM model: cell and space group are crystallographic
      // concepts) comes out carrying a fake 1 A cubic cell. mmdb leaves such
      // a model cell-less, which is correct, and Coot has a deliberate
      // non-crystallographic path keyed off it
      // (molecule_class_info_t::set_have_unit_cell_flag_maybe). Put it back
      // the way mmdb would have had it.
      if (! had_cell) {
         if (mmdb::Cryst *cryst = mol->GetCrystData()) {
            cryst->WhatIsSet &= ~mmdb::CSET_CellParams;
            cryst->CellCheck |= mmdb::CCHK_NoCell;
            cryst->a = cryst->b = cryst->c = 0.0;
            cryst->alpha = cryst->beta = cryst->gamma = 0.0;
         }
      }

      if (message)
         *message = "read by gemmi";

   } catch (const std::exception &e) {
      if (message) *message = e.what();
      delete mol;
      return nullptr;
   }

   return mol;
}

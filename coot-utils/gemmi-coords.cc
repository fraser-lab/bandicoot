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
#include "mmcif-document.hh"   // coot::mmcif_document_t (the only gemmi-aware header)

#include <gemmi/mmread.hpp>    // read_structure, BasicInput
#include <gemmi/mmdb.hpp>      // copy_to_mmdb
#include <gemmi/polyheur.hpp>  // setup_entities
#include <gemmi/symmetry.hpp>  // find_spacegroup

#include <algorithm>
#include <cstdio>
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
coot::read_coords_with_gemmi(const std::string &file_name, std::string *message,
                             std::shared_ptr<coot::mmcif_document_t> *doc_out) {

   mmdb::Manager *mol = nullptr;

   try {
      // Read, optionally keeping the parsed document (Phase 3 / D3).
      //
      // This is gemmi::read_structure_file() with one extra argument -- that
      // function is a two-line wrapper around read_structure() which simply
      // does not forward save_doc. Going one level down costs nothing and
      // means NO branching on format here: read_structure() clears save_doc up
      // front and fills it only on the mmCIF/mmjson/chemcomp paths, so a PDB
      // file leaves it empty and everything below just works. (An earlier plan
      // for this step assumed we would have to call read_cif_gz() ourselves
      // and branch, which would have broken this function for the PDB files
      // tools/gemmi-diff feeds it. Not needed.)
      gemmi::cif::Document saved_doc;
      gemmi::Structure st = gemmi::read_structure(gemmi::BasicInput(file_name),
                                                  gemmi::CoorFormat::Unknown,
                                                  doc_out ? &saved_doc : nullptr);

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

      // (6) Carry the resolution across.
      //
      // copy_to_mmdb takes nothing at all out of st.meta, so a gemmi-read
      // mmCIF has no resolution and model_resolution() returns -2 where the
      // mmdb reader used to give the real number. Unlike the rest of the
      // header (secondary structure, title, compound, ...), which mmdb never
      // managed to read from a modern mmCIF anyway because its readers are
      // keyed to obsolete ndb_* tags, this ONE field is a genuine regression:
      // mmdb's tags for it (_refine.ls_d_res_high) are still current.
      // Measured on 5E1N.cif: model_resolution 1.0 before v0.2, -2.0 after.
      //
      // There is no setter -- Title::resolution is protected and mmdb exposes
      // only GetResolution(). But GetResolution() falls back to scanning the
      // REMARK container for remark number 2, so the standard REMARK 2 line
      // gets the value in through a public API. Two things make the placement
      // matter: GetResolution() CACHES its answer (and records -1.0 on a
      // failed scan), so this has to happen before anything asks; and its scan
      // gives up at the first remark numbered > 2, so REMARK 2 has to be added
      // before any higher-numbered one. Nothing else has been added here --
      // gemmi fills st.raw_remarks only from PDB input, so for mmCIF the
      // container copy_to_mmdb left behind is empty.
      if (st.resolution > 0.0) {
         char line[81];
         snprintf(line, sizeof(line), "REMARK   2 RESOLUTION. %7.2f ANGSTROMS.",
                  st.resolution);
         mol->PutPDBString(line);
      }

      // (7) Retain the mmCIF document, minus its coordinates (Phase 3 / D3).
      //
      // See mmcif-document.hh for why this exists and why _atom_site is
      // dropped. Note this runs LAST: everything above may still decide to
      // fail the read, and a document must never outlive a read that did not
      // produce a molecule.
      //
      // saved_doc is empty for PDB input (read_structure only fills it on the
      // CIF paths), so *doc_out is simply left null there and the writer will
      // fall back to synthesizing a fresh document.
      if (doc_out && ! saved_doc.blocks.empty()) {

         // Only blocks[0] holds coordinates -- make_structure() enforces that
         // -- so this is the only block to strip. Later blocks (restraints in
         // a deposition file) are kept whole.
         gemmi::cif::Block &block = saved_doc.blocks[0];
         for (const char *cat : { "_atom_site", "_atom_site_anisotrop" }) {
            gemmi::cif::Table tab = block.find_mmcif_category(cat);
            if (tab.ok())
               tab.erase();
         }

         *doc_out = std::make_shared<coot::mmcif_document_t>(std::move(saved_doc),
                                                             file_name);
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

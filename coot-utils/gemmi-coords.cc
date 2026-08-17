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
#include "gemmi-header.hh"     // pdb_header_records_from_mmcif (adjustment 9)

#include <gemmi/mmread.hpp>    // read_structure, BasicInput
#include <gemmi/mmdb.hpp>      // copy_to_mmdb
#include <gemmi/polyheur.hpp>  // setup_entities
#include <gemmi/symmetry.hpp>  // find_spacegroup
#include <gemmi/mmread_gz.hpp> // read_structure_gz (compressed mmCIF)
#include <gemmi/read_cif.hpp>  // read_cif_gz (classify_cif_file)

#include "utils/coot-utils.hh"  // file_name_extension

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
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


// Classify a CIF by the categories it actually contains.
//
// The alternative -- trusting the ".cif" extension and trying interpretations
// in order -- is what caused the drag-and-drop dictionary bug: a coordinate
// mmCIF was read as restraints, its distance-less _chem_comp_bond overwrote the
// monomer library, and RSR silently stopped making restraints for every
// molecule in the session. See src/drag-and-drop.cc.
//
// Order matters: _atom_site wins. A file can legitimately hold BOTH coordinates
// and restraints -- phenix.refine writes the ligand dictionary into a second
// data block -- and such a file is coordinates that happen to carry restraints,
// not an ambiguous case.
coot::cif_flavour_t
coot::classify_cif_file(const std::string &file_name) {

   try {
      gemmi::cif::Document doc = gemmi::read_cif_gz(file_name);
      bool has_atom_site = false, has_chem_comp_atom = false, has_refln = false;
      for (const gemmi::cif::Block &block : doc.blocks) {
         for (const gemmi::cif::Item &it : block.items) {
            std::string tag;
            if (it.type == gemmi::cif::ItemType::Pair) tag = it.pair[0];
            else if (it.type == gemmi::cif::ItemType::Loop && !it.loop.tags.empty())
               tag = it.loop.tags[0];
            if (tag.empty()) continue;
            if (tag.compare(0, 11, "_atom_site.") == 0)      has_atom_site = true;
            else if (tag.compare(0, 16, "_chem_comp_atom.") == 0) has_chem_comp_atom = true;
            else if (tag.compare(0, 7,  "_refln.") == 0)     has_refln = true;
         }
      }
      if (has_atom_site)      return cif_flavour_t::coordinates;
      if (has_chem_comp_atom) return cif_flavour_t::restraints;
      if (has_refln)          return cif_flavour_t::structure_factors;
   } catch (const std::exception &e) {
      std::cout << "INFO:: classify_cif_file(): " << file_name << ": " << e.what()
                << std::endl;
   }
   return cif_flavour_t::unknown;
}


// Phases decide WHICH structure-factor reader to call, never whether to read.
// Deposited SF files usually carry F_meas/I_meas and no phases at all; phasing
// them from a loaded model is the ordinary workflow.
bool
coot::cif_structure_factors_have_phases(const std::string &file_name) {

   try {
      gemmi::cif::Document doc = gemmi::read_cif_gz(file_name);
      for (const gemmi::cif::Block &block : doc.blocks)
         for (const gemmi::cif::Item &it : block.items)
            if (it.type == gemmi::cif::ItemType::Loop)
               for (const std::string &tag : it.loop.tags) {
                  std::string t = gemmi::to_lower(tag);
                  if (t.find("phase") != std::string::npos) return true;
               }
   } catch (const std::exception &) {
   }
   return false;
}


// Does this structure-factor file carry amplitudes, or only intensities?
//
// Coot imports through clipper::CIFfile into HKL_data<F_sigF>. Given a file
// with only _refln.intensity_meas it finds nothing usable, and the resulting
// map is all zeros -- rendered without complaint. 3K0N-sf.cif straight from the
// PDB is exactly this case: 41,086 reflections, all intensities, and Coot
// reports "myfsigf has 2 data" then a map with mean 0 and sigma 0.
bool
coot::cif_structure_factors_have_amplitudes(const std::string &file_name) {

   try {
      gemmi::cif::Document doc = gemmi::read_cif_gz(file_name);
      for (const gemmi::cif::Block &block : doc.blocks)
         for (const gemmi::cif::Item &it : block.items)
            if (it.type == gemmi::cif::ItemType::Loop)
               for (const std::string &tag : it.loop.tags) {
                  std::string t = gemmi::to_lower(tag);
                  if (t.compare(0, 7, "_refln.") != 0) continue;
                  // Two vocabularies for the same quantity, and a converter may
                  // emit either:
                  //   PDBx/mmCIF : _refln.F_meas, F_meas_au, F_calc, pdbx_F_plus
                  //   CIF core   : _refln.amplitude_meas, F_squared_meas
                  // Missing the second set made a correctly converted file
                  // (gemmi output carrying amplitude_meas + amplitude_sigma)
                  // get reported as "intensities only".
                  if (t.find("f_meas") != std::string::npos)         return true;
                  if (t.find("f_calc") != std::string::npos)         return true;
                  if (t.find("f_squared_meas") != std::string::npos) return true;
                  if (t.find("amplitude_meas") != std::string::npos) return true;
                  if (t.find("amplitude_calc") != std::string::npos) return true;
               }
   } catch (const std::exception &) {
   }
   return false;
}


bool
coot::gemmi_handles_extension(const std::string &extension) {

   std::string e;
   e.reserve(extension.size());
   for (char c : extension) e += std::tolower(static_cast<unsigned char>(c));

   return (e == ".cif" || e == ".mmcif" || e == ".mcif");
}


bool
coot::gemmi_handles_file(const std::string &file_name) {

   // Decide from the NAME, not from the extension, because
   // coot::util::file_name_extension() returns everything after the LAST dot:
   // "foo.cif.gz" has extension ".gz", so testing the extension alone sent
   // every compressed mmCIF to the mmdb reader instead of gemmi. An I/O detail
   // must not choose a parser.
   //
   // That was cosmetic until the Phase 3 writer landed, and then it was not.
   // Backups are named from backup_compress_files_flag, which DEFAULTS TO 1,
   // so a backup is "*.cif.gz" -- and the writer now PRESERVES every category,
   // including _struct_ncs_oper, which mmdb2 cannot read at all (GitHub #9).
   // Measured: a preserved backup of SC1_2_refine_036 carries
   // _struct_ncs_oper x15 and mmdb fails to load it. Undo re-reads the backup,
   // so without this the fidelity work would have BROKEN UNDO for every
   // NCS-containing mmCIF. mmdb could read its own backups only because its
   // writer threw the troublesome categories away.
   std::string name = file_name;
   if (name.size() > 3 && name.compare(name.size() - 3, 3, ".gz") == 0)
      name.erase(name.size() - 3);

   return gemmi_handles_extension(coot::util::file_name_extension(name));
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
      // read_structure_gz rather than read_structure: it transparently
      // decompresses, which the read path now needs because compressed mmCIF
      // (every backup) is routed here rather than to mmdb. Same save_doc
      // contract -- filled only on the CIF paths, left empty for PDB.
      gemmi::Structure st = gemmi::read_structure_gz(file_name,
                                                     gemmi::CoorFormat::Unknown,
                                                     doc_out ? &saved_doc : nullptr);

      // (8) Harvest the label_* identities BEFORE anything touches st.
      //
      // mmCIF gives a residue two identities; mmdb models only the author one
      // (see mmcif-document.hh). So label_asym_id / label_entity_id /
      // label_seq_id have to be carried across in a side table or they are
      // gone the moment copy_to_mmdb runs, and the written file gets nulls --
      // which is what Art found in GUI testing on 2026-08-12.
      //
      // Done here, before setup_entities() and merge_chain_parts(), so what is
      // captured is unambiguously the FILE's own values. (setup_entities calls
      // assign_subchains with force=false, so it would not overwrite them --
      // but depending on that is a needless hostage to a gemmi change.)
      std::map<std::string, coot::residue_labels_t> harvested_labels;
      std::map<std::string, std::map<std::string, std::string> > extra_columns;
      std::vector<std::string> anisotrop_tags;
      std::vector<std::string> cis_tags;
      int atom_site_index = -1;
      if (doc_out) {
         for (const gemmi::Model &model : st.models) {
            int model_num = model.num;
            for (const gemmi::Chain &chain : model.chains) {
               for (const gemmi::Residue &res : chain.residues) {
                  if (res.subchain.empty() && !res.label_seq.has_value())
                     continue;             // nothing worth carrying
                  coot::residue_labels_t rl;
                  rl.label_asym_id = res.subchain;
                  rl.has_label_seq = res.label_seq.has_value();
                  if (rl.has_label_seq) rl.label_seq = *res.label_seq;
                  // entity id: whichever Entity claims this subchain
                  for (const gemmi::Entity &ent : st.entities)
                     if (std::find(ent.subchains.begin(), ent.subchains.end(),
                                   res.subchain) != ent.subchains.end()) {
                        rl.entity_id = ent.name;
                        break;
                     }
                  std::string icode(1, res.seqid.icode);
                  if (res.seqid.icode == ' ' || res.seqid.icode == '\0') icode.clear();
                  harvested_labels[coot::mmcif_document_t::residue_label_key(
                                      model_num, chain.name, res.seqid.num.value, icode)] = rl;
               }
            }
         }
      }

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

      // (9) Synthesize the PDB header records this mmCIF implies.
      //
      // Done HERE, before anything mutates st and while the document still
      // holds _atom_site: the label-only secondary-structure fallback in
      // gemmi-header.cc resolves label_asym_id/label_seq_id through the
      // coordinate loop, which adjustment (7) strips further down. Only the
      // records are kept; they are handed to mmdb after copy_to_mmdb, since
      // HELIX and SHEET need model 1 to exist.
      std::vector<std::string> header_records;
      if (doc_out && ! saved_doc.blocks.empty())
         header_records = coot::pdb_header_records_from_mmcif(st,
                                                              &saved_doc.blocks[0]);

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

      // (9) continued -- the rest of the header section.
      //
      // AFTER the resolution line, and that ordering is load-bearing: mmdb's
      // GetResolution() scans the REMARK container and gives up at the first
      // remark numbered above 2, so a REMARK 3 arriving first would hide the
      // REMARK 2 that adjustment (6) exists to provide.
      //
      // See gemmi-header.hh for why this is a back-fill of a capability
      // rather than a repair: mmdb's own mmCIF header readers are keyed to
      // NDB-era tag names and have returned nothing from a modern PDBx file
      // for years.
      for (const std::string &record : header_records)
         mol->PutPDBString(record.c_str());

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

         // Harvest any NON-STANDARD _atom_site column before the category is
         // stripped. gemmi regenerates _atom_site from the model on write and
         // emits only the columns it models, so an extension column -- most
         // importantly pdbx_heterogeneity_id -- would simply vanish.
         //
         // Deliberately keyed off a list of what gemmi DOES write, so anything
         // unrecognised is carried rather than requiring code per column. Over-
         // harvesting is harmless: the write side only re-adds a column that is
         // actually missing from the regenerated loop.
         {
            static const std::set<std::string> gemmi_writes = {
               "group_PDB", "id", "type_symbol", "label_atom_id", "label_alt_id",
               "label_comp_id", "label_asym_id", "label_entity_id", "label_seq_id",
               "pdbx_PDB_ins_code", "Cartn_x", "Cartn_y", "Cartn_z", "occupancy",
               "B_iso_or_equiv", "pdbx_formal_charge", "auth_seq_id", "auth_comp_id",
               "auth_asym_id", "auth_atom_id", "pdbx_PDB_model_num",
               "Cartn_x_esd", "Cartn_y_esd", "Cartn_z_esd", "occupancy_esd",
               "B_iso_or_equiv_esd", "segment_id", "calc_flag" };

            gemmi::cif::Table at = block.find_mmcif_category("_atom_site");
            if (at.ok() && at.loop_item) {
               const gemmi::cif::Loop &loop = at.loop_item->loop;
               int c_model = loop.find_tag("_atom_site.pdbx_PDB_model_num");
               int c_chain = loop.find_tag("_atom_site.auth_asym_id");
               int c_seq   = loop.find_tag("_atom_site.auth_seq_id");
               int c_ic    = loop.find_tag("_atom_site.pdbx_PDB_ins_code");
               int c_name  = loop.find_tag("_atom_site.label_atom_id");
               int c_alt   = loop.find_tag("_atom_site.label_alt_id");
               if (c_chain >= 0 && c_seq >= 0 && c_name >= 0) {
                  for (size_t col = 0; col < loop.tags.size(); col++) {
                     std::string tag = loop.tags[col];
                     std::string short_name = tag.substr(tag.find('.') + 1);
                     if (gemmi_writes.count(short_name)) continue;
                     std::map<std::string, std::string> &dest =
                        extra_columns[short_name];
                     for (size_t r = 0; r < loop.length(); r++) {
                        std::string key = coot::mmcif_document_t::atom_extra_key(
                           c_model >= 0 ? gemmi::cif::as_string(loop.val(r, c_model)) : "1",
                           gemmi::cif::as_string(loop.val(r, c_chain)),
                           gemmi::cif::as_string(loop.val(r, c_seq)),
                           c_ic  >= 0 ? gemmi::cif::as_string(loop.val(r, c_ic))  : "",
                           gemmi::cif::as_string(loop.val(r, c_name)),
                           c_alt >= 0 ? gemmi::cif::as_string(loop.val(r, c_alt)) : "");
                        // Store the RAW token, NOT as_string(): as_string
                        // turns the CIF null "." into an empty string, and
                        // writing that back emits ZERO characters -- one token
                        // short, so every value after it on the row shifts by
                        // one and the file no longer parses. That is exactly
                        // how the first water row of a renumbered file broke
                        // (gemmi: "loop _atom_site.id row 3039 data O").
                        // Keys are still built with as_string, which is right:
                        // identity comparison wants the unquoted value.
                        dest[key] = loop.val(r, col);
                     }
                     std::cout << "INFO:: carrying non-standard _atom_site column "
                               << short_name << " (" << dest.size() << " atoms)"
                               << std::endl;
                  }
               }
            }
         }

         {  // remember which anisotrop columns the file had, before stripping
            gemmi::cif::Table an = block.find_mmcif_category("_atom_site_anisotrop");
            if (an.ok() && an.loop_item)
               anisotrop_tags = an.loop_item->loop.tags;
            // and the cis-peptide columns, which gemmi also regenerates narrower
            gemmi::cif::Table ci = block.find_mmcif_category("_struct_mon_prot_cis");
            if (ci.ok() && ci.loop_item)
               cis_tags = ci.loop_item->loop.tags;
         }

         {  // remember where _atom_site sat, so the writer can put it back
            gemmi::cif::Table at2 = block.find_mmcif_category("_atom_site");
            if (at2.ok() && at2.loop_item)
               atom_site_index = static_cast<int>(at2.loop_item - block.items.data());
         }

         for (const char *cat : { "_atom_site", "_atom_site_anisotrop" }) {
            gemmi::cif::Table tab = block.find_mmcif_category(cat);
            if (tab.ok())
               tab.erase();
         }

         *doc_out = std::make_shared<coot::mmcif_document_t>(std::move(saved_doc),
                                                             file_name);
         (*doc_out)->residue_labels   = std::move(harvested_labels);
         (*doc_out)->atom_extra_columns = std::move(extra_columns);
         (*doc_out)->anisotrop_tags     = std::move(anisotrop_tags);
         (*doc_out)->cis_tags           = std::move(cis_tags);
         (*doc_out)->atom_site_item_index = atom_site_index;
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

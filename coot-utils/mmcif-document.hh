/* coot-utils/mmcif-document.hh
 *
 * Bandicoot v0.2: the mmCIF document a molecule was read from, kept alive for
 * the lifetime of that molecule.
 *
 * WHY THIS EXISTS
 *
 * The requirement for the v0.2 write path: "whatever comes in also comes
 * out, with the only changes being those made by the user during the editing
 * session." mmdb cannot meet that -- it shreds a file into fixed-field PDB-era
 * structs, discards everything that has nowhere to live, and deletes the
 * parsed CIF (65 categories in, 15 out). gemmi's update_mmcif_block() can:
 * it rewrites the categories it is told to and leaves every other one
 * BIT-IDENTICAL, because it never touches them. That gives preservation as the
 * DEFAULT, with no per-category code and no enumeration of what exists -- a
 * category from a future spec revision survives for free.
 *
 * But update_mmcif_block() needs the original document to update. So the
 * document has to outlive the read. That is all this class is.
 *
 * THIS IS THE ONLY HEADER IN THE TREE THAT SEES GEMMI.
 *
 * molecule-class-info.h (included by 36 files) gets a forward declaration and
 * a shared_ptr, nothing more. Keep it that way: the moment a widely-included
 * header pulls in <gemmi/cifdoc.hpp>, every compile in the tree pays for it.
 *
 * WHY _atom_site IS NOT IN HERE
 *
 * The coordinates are stripped before the document is retained, because the
 * atoms category is unconditionally regenerated from mmdb on write ("EDIT" in
 * the per-category policy). Two reasons, one practical and one about safety:
 *
 *  - Size. _atom_site (+ _atom_site_anisotrop) is 82-98% of the bytes of a
 *    coordinate mmCIF. Stripping takes the retained document to 0.06-1.23 MB
 *    of text; 1FFK's in-memory footprint drops from roughly 41 MB to 10 MB
 *    PER OPEN MOLECULE.
 *  - Correctness. Keeping it would mean two representations of the same
 *    coordinates in memory, one live (mmdb) and one going stale from the first
 *    edit onward. A bug that wrote the stale one would produce a file that
 *    looks entirely plausible and has pre-edit coordinates. Not holding it
 *    makes that bug unwritable.
 *
 * Nothing is lost: x/y/z/occupancy/B and the six anisotropic U values live in
 * mmdb and are written fresh on every save and every backup.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef COOT_MMCIF_DOCUMENT_HH
#define COOT_MMCIF_DOCUMENT_HH

#include <map>
#include <string>
#include <vector>

#include <gemmi/cifdoc.hpp>

namespace coot {

   //! The mmCIF label_* identity of one residue, harvested at read time.
   //
   //! mmCIF gives a residue TWO identities: the author's (auth_asym_id,
   //! auth_seq_id -- what the depositor called it, what Coot shows the user)
   //! and the label_* one (label_asym_id, label_entity_id, label_seq_id --
   //! the canonical decomposition into subchains and entities). mmdb models
   //! only the first: it is a PDB-era container and PDB has just one identity
   //! per residue. So the label_* half is destroyed the moment coordinates
   //! reach mmdb, and no amount of cleverness on the write side reconstructs
   //! it -- it has to be carried across.
   //!
   //! Note these are per-RESIDUE, not per-atom: in gemmi they are
   //! Residue::subchain and Residue::label_seq, with the entity id coming from
   //! whichever Entity owns that subchain.
   struct residue_labels_t {
      std::string label_asym_id;   //!< -> gemmi Residue::subchain
      std::string entity_id;       //!< -> name of the Entity owning that subchain
      int label_seq = 0;
      bool has_label_seq = false;  //!< false when the file said "." or "?"
   };

   //! The mmCIF document a molecule was read from, minus its coordinates.
   //
   //! Held by molecule_class_info_t for as long as the molecule exists, and
   //! consumed by the write path, which hands it to gemmi's
   //! update_mmcif_block() so that every category Bandicoot does not
   //! regenerate is written back out exactly as it came in.
   struct mmcif_document_t {

      //! The retained document. Not const: the write path updates it in place.
      gemmi::cif::Document doc;

      //! The file it was read from, for diagnostics only -- a molecule can be
      //! saved anywhere and this is NOT the save target.
      std::string source_file_name;

      //! label_* identities, keyed by the residue's AUTHOR identity.
      //
      //! Keyed with residue_label_key() below, i.e. by exactly the fields mmdb
      //! does model. That choice is what makes the invalidation policy free:
      //! renumber a residue or move it to another chain and its key changes,
      //! so the lookup misses on write and the label_* columns come out null
      //! -- which is the agreed behaviour (drop for edited residues) with no
      //! edit-tracking machinery at all.
      //!
      //! Consequence worth knowing, and worth not over-reading: a null
      //! label_* in a written file means "this residue's number or chain id
      //! changed", NOT "this atom was edited". A refined atom that moved
      //! halfway across the density keeps its label_*, because its identity
      //! never changed.
      std::map<std::string, residue_labels_t> residue_labels;

      //! Non-standard _atom_site columns, carried across the mmdb round trip.
      //
      //! Keyed: column short name (e.g. "pdbx_heterogeneity_id") -> atom key
      //! (atom_extra_key()) -> value.
      //!
      //! WHY THIS IS GENERAL rather than a pdbx_heterogeneity_id special case:
      //! _atom_site is regenerated from the model on write, so gemmi emits the
      //! columns IT knows about and any extension column simply disappears.
      //! That is TRAPS.md E1 -- "whole categories come along for free,
      //! extension COLUMNS do not". Harvesting whatever we do not recognise
      //! means a column from a future spec revision survives without anyone
      //! writing code for it, which is the same property that makes the
      //! retained document worth having.
      //!
      //! pdbx_heterogeneity_id is the one that matters today: it is the
      //! per-atom half of the encoding this whole project exists to support,
      //! and the write-side gate caught it being dropped on all four
      //! *_hierarchy.cif files.
      std::map<std::string, std::map<std::string, std::string> > atom_extra_columns;

      //! Key for atom_extra_columns: an atom's author identity.
      static std::string atom_extra_key(const std::string &model_num,
                                        const std::string &auth_chain,
                                        const std::string &auth_seq,
                                        const std::string &ins_code,
                                        const std::string &atom_name,
                                        const std::string &alt_id) {
         std::string ic = (ins_code == "?" || ins_code == ".") ? "" : ins_code;
         std::string al = (alt_id   == "?" || alt_id   == ".") ? "" : alt_id;
         return model_num + "/" + auth_chain + "/" + auth_seq + "/" + ic + "/"
              + atom_name + "/" + al;
      }

      //! The _atom_site_anisotrop columns the input file carried.
      //
      //! gemmi regenerates that category with only id/type_symbol/U[..], so the
      //! ten identifying pdbx_* columns are dropped. They are pure duplication
      //! of _atom_site and can be rebuilt by joining on the atom id -- but only
      //! the ones the file actually had should be re-added, hence this list.
      std::vector<std::string> anisotrop_tags;

      //! Where _atom_site sat among the block's items in the input file.
      //
      //! _atom_site is stripped at read time, so update_mmcif_block() re-adds it
      //! at the END of the block -- and every category that followed the
      //! coordinates in the original (in 3K0N: _pdbx_poly_seq_scheme,
      //! _pdbx_struct_assembly*, _pdbx_audit_revision_* and nine more) ends up
      //! ahead of them instead. Category order carries no meaning in CIF, but
      //! it is a gratuitous difference from the input, and this is a fidelity
      //! project. -1 when there was no _atom_site.
      int atom_site_item_index = -1;

      //! The _struct_mon_prot_cis columns the input file carried.
      std::vector<std::string> cis_tags;

      //! Key for residue_labels: the fields mmdb preserves across an edit.
      static std::string residue_label_key(int model_num,
                                           const std::string &auth_chain,
                                           int auth_seq_num,
                                           const std::string &ins_code) {
         return std::to_string(model_num) + "/" + auth_chain + "/"
              + std::to_string(auth_seq_num) + "/" + ins_code;
      }

      mmcif_document_t() {}
      explicit mmcif_document_t(gemmi::cif::Document &&d, const std::string &f = "")
         : doc(std::move(d)), source_file_name(f) {}

      bool empty() const { return doc.blocks.empty(); }

      //! The block holding the coordinates.
      //
      //! ALWAYS blocks[0], and the write path must target it explicitly rather
      //! than assuming a single-block document: gemmi's make_structure()
      //! accepts a multi-block document as long as only the first block has
      //! _atom_site, and deposition files legitimately put restraints in later
      //! blocks. Returns nullptr for an empty document.
      gemmi::cif::Block *coordinate_block() {
         return doc.blocks.empty() ? nullptr : &doc.blocks[0];
      }
      const gemmi::cif::Block *coordinate_block() const {
         return doc.blocks.empty() ? nullptr : &doc.blocks[0];
      }
   };
}

#endif // COOT_MMCIF_DOCUMENT_HH

/* coot-utils/mmcif-document.hh
 *
 * Bandicoot v0.2: the mmCIF document a molecule was read from, kept alive for
 * the lifetime of that molecule.
 *
 * WHY THIS EXISTS
 *
 * Art's requirement for the v0.2 write path: "whatever comes in also comes
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

#include <string>

#include <gemmi/cifdoc.hpp>

namespace coot {

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

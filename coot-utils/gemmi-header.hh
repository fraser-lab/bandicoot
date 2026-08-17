/* coot-utils/gemmi-header.hh
 *
 * Bandicoot v0.2: turn an mmCIF's header/annotation section into the PDB
 * header records mmdb can hold.
 *
 * WHY THIS EXISTS
 *
 * gemmi::copy_to_mmdb() is a COORDINATE pipe -- Global Phasing wrote it so
 * that gemmi-parsed atoms could feed mmdb's algorithms, and it takes exactly
 * two keys out of st.info and nothing at all out of st.meta. So after the
 * Phase 2 read swap a gemmi-read mmCIF reaches Coot with no title, no
 * compound, no authors, no journal reference and no secondary structure.
 *
 * Almost none of that is a regression: mmdb's OWN mmCIF readers are keyed to
 * NDB-era tag names that PDBx abandoned (_database rather than _database_2,
 * ndb_keywords, ndb_helix_class_pdb, and an integer _struct_conf.id where
 * modern files write "HELX_P1"), so they have been returning nothing from a
 * modern mmCIF for years -- measured in both v0.1.4.13 and v0.2.0.0. What this
 * file does is therefore mostly a NEW capability: mmCIF header reading that no
 * Coot 0.9 lineage has ever had.
 *
 * HOW
 *
 * mmdb's containers are protected and it offers no setters, but
 * Root::PutPDBString() feeds one PDB record through Title::ConvertPDBString()
 * and then Model::ConvertPDBString() -- which between them accept HEADER,
 * TITLE, COMPND, KEYWDS, EXPDTA, AUTHOR, JRNL, REMARK, HELIX and SHEET. So the
 * whole back-fill is "synthesize the PDB records this mmCIF implies, and hand
 * them to mmdb the way a PDB file would have". No new access to mmdb
 * internals, and the same public route the resolution fix already proved out.
 *
 * The synthesis is a pure function of (Structure, Block) so it can be checked
 * by printing it, and so a harvester can lift it without dragging mmdb along.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef GEMMI_HEADER_HH
#define GEMMI_HEADER_HH

#include <string>
#include <vector>

// Forward declarations only: mmcif-document.hh is the one header in the tree
// that includes gemmi, and this file does not become the second.
namespace gemmi {
   struct Structure;
   namespace cif { struct Block; }
}

namespace coot {

   //! Synthesize the PDB header records implied by an mmCIF.
   //
   //! \a block is the coordinate block of the retained document -- the
   //! bibliographic categories (_audit_author, _citation, _entity) are NOT in
   //! gemmi::Structure at all, so they can only come from the document.
   //! \a st supplies what gemmi does model: st.info, st.resolution, and the
   //! typed secondary structure in st.helices / st.sheets.
   //!
   //! Returns records in PDB order, each already laid out in its fixed
   //! columns, ready to be passed one at a time to mmdb::Manager::PutPDBString.
   //! Anything the file does not carry is simply not emitted.
   std::vector<std::string>
   pdb_header_records_from_mmcif(const gemmi::Structure &st,
                                 const gemmi::cif::Block *block);

}

#endif // GEMMI_HEADER_HH

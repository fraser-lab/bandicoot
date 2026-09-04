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
 * OBSLTE, TITLE, CAVEAT, COMPND, SOURCE, KEYWDS, EXPDTA, MDLTYPE, AUTHOR,
 * REVDAT, SPRSDE, JRNL, REMARK, DBREF, SEQADV, SEQRES, MODRES, HET, HETNAM,
 * HETSYN, FORMUL, HELIX, SHEET, TURN, LINK and CISPEP. So the whole back-fill
 * is "synthesize the PDB records this mmCIF implies, and hand them to mmdb the
 * way a PDB file would have". No new access to mmdb internals, and the same
 * public route the resolution fix already proved out.
 *
 * What is synthesized, and from where:
 *
 *   HEADER  _struct_keywords.pdbx_keywords, _pdbx_database_status, _entry.id
 *   TITLE   _struct.title
 *   COMPND  _entity (+ _entity_name_com), one MOL_ID per polymer entity
 *   SOURCE  _entity_src_nat / _entity_src_gen / _pdbx_entity_src_syn
 *   KEYWDS  _struct_keywords.text
 *   EXPDTA  _exptl.method
 *   AUTHOR  _audit_author
 *   REVDAT  _pdbx_audit_revision_history
 *   JRNL    _citation + _citation_author (the "primary" row)
 *   REMARK  _refine + _software, as a summary (REMARK 3)
 *   DBREF   _struct_ref + _struct_ref_seq
 *   HETNAM  _chem_comp.name
 *   FORMUL  _chem_comp.formula + the residue counts in model 1
 *   HELIX   _struct_conf     (st.helices, or the label_* fallback)
 *   SHEET   _struct_sheet_*  (st.sheets,  or the label_* fallback)
 *
 * Deliberately NOT synthesized: HETSYN (_chem_comp.pdbx_synonyms), HET and
 * SEQADV, and the prose REMARK sections (200/280/350/465/500). The REMARKs are
 * the reason: they are wwPDB's rendering of typed categories, so composing them
 * is reconstruction rather than translation and the wording would not match.
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

namespace mmdb { class Manager; }

namespace coot {

   //! THE OTHER DIRECTION: carry mmdb's PDB header into a gemmi Structure and
   //! an mmCIF block, for a molecule read from PDB and saved as mmCIF.
   //
   //! Needed for the same reason as its opposite, one layer down:
   //! `gemmi::copy_from_mmdb()` carries the cell, Z, space group, models,
   //! cis-peptides and links -- and nothing else. So converting a PDB wrote a
   //! file with eight categories and no title, authors, citation, keywords,
   //! method or secondary structure, all of which the input plainly had. That
   //! is NOT the accepted cross-format lossiness (an mmCIF missing what a PDB
   //! never carried); it is dropping what was in front of us.
   //!
   //! Split in two because gemmi does half the work if asked properly:
   //!
   //!  -  transfer_pdb_header_to_gemmi fills `st.info` and
   //!    `st.helices`/`st.sheets`, which gemmi's writer then emits as
   //!    `_struct`, `_struct_keywords`, `_exptl`, `_entry`,
   //!    `_pdbx_database_status`, `_struct_conf` and `_struct_sheet*`.
   //!  -  add_pdb_header_categories writes the categories gemmi has no model
   //!    for at all: `gemmi::Metadata` has no author and no citation field
   //!    anywhere, and no home for free REMARK text.
   void transfer_pdb_header_to_gemmi(mmdb::Manager *mol, gemmi::Structure &st);

   //! Add `_audit_author`, `_citation`, `_citation_author`,
   //! `_pdbx_database_remark`, `_refine.ls_d_res_high` and
   //! `_entity.pdbx_description` to a synthesized block, from mmdb's
   //! AUTHOR / JRNL / REMARK / COMPND records.
   void add_pdb_header_categories(mmdb::Manager *mol, gemmi::cif::Block &block);

   //! The resolution the PDB header states, from REMARK 2 if it has one and
   //! from REMARK 3's "RESOLUTION RANGE HIGH" line if it does not.
   //
   //! `mmdb::Manager::GetResolution()` only knows REMARK 2, which is a wwPDB
   //! deposition record: **phenix.refine output has none**, and states the
   //! resolution only in its REMARK 3 refinement account. Returns -1.0 when the
   //! header states no resolution anywhere.
   double pdb_header_resolution(mmdb::Manager *mol);

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

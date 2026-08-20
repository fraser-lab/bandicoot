/* coot-utils/gemmi-coords.hh
 *
 * Bandicoot v0.2: read coordinates through gemmi into an mmdb::Manager.
 *
 * Scope A of the gemmi conversion -- gemmi is the mmCIF I/O layer, mmdb stays
 * the model. This is the whole of the gemmi read path; the caller
 * (get_atom_selection) does nothing but ask for a model and fall back if it
 * does not get one.
 *
 * Deliberately portable: it speaks mmdb::Manager*, std::string and gemmi
 * types only -- no molecule_class_info_t, no graphics_info_t -- so the Coot 1
 * developers can lift it with minimal adjustment. Nothing in this file may
 * grow a dependency on the GUI layer.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef GEMMI_COORDS_HH
#define GEMMI_COORDS_HH

#include <memory>
#include <string>
#include <mmdb2/mmdb_manager.h>

namespace coot {

   // Forward declaration only. The definition lives in mmcif-document.hh,
   // which is the one header allowed to include gemmi -- see the note there.
   struct mmcif_document_t;

   //! Is this a file extension we route through gemmi?
   //
   //! mmCIF only. The PDB path is deliberately left on mmdb (Scope B was
   //! rejected: same risk, no gain), and SHELX .ins/.res gemmi cannot read at
   //! all.
   bool gemmi_handles_extension(const std::string &extension);

   //! What flavour of thing is this CIF?
   //
   //! One extension, several unrelated formats. Deciding between them by
   //! extension is what made a dropped mmCIF overwrite the monomer library:
   //! every wwPDB coordinate file carries a _chem_comp_bond connectivity loop,
   //! so "does it parse as restraints?" answers YES for a coordinate file.
   //! Ask what categories it actually holds instead.
   enum class cif_flavour_t {
      unknown,            //!< nothing recognised - ask the user
      coordinates,        //!< has _atom_site
      restraints,         //!< has _chem_comp_atom and no _atom_site
      structure_factors   //!< has _refln
   };

   //! Classify a CIF by CONTENT. Handles .gz. Never throws.
   cif_flavour_t classify_cif_file(const std::string &file_name);

   //! Is this CIF a CHEMICAL COMPONENT definition, and if so, which component?
   //
   //! Returns the `_chem_comp.id` (e.g. "AR6") for a wwPDB/PDBe component
   //! definition or a Refmac monomer-library entry, and "" for anything else.
   //!
   //! Such a file has NO `_atom_site` -- its coordinates live in
   //! `_chem_comp_atom.model_Cartn_*` and `pdbx_model_Cartn_*_ideal` -- so the
   //! ordinary coordinate reader finds nothing in it. It is nevertheless how a
   //! user obtains a ligand from the PDB, so it has to load.
   std::string cif_chem_comp_id(const std::string &file_name);

   //! Does the chem_comp carry BOND DISTANCES, i.e. can it restrain a model?
   //
   //! This is the difference between a dictionary you can refine with (elbow,
   //! acedrg, the Refmac monomer library: `_chem_comp_bond.value_dist`) and a
   //! wwPDB/PDBe component definition, which states bond ORDER and aromaticity
   //! but no distances at all. Importing the latter as restraints yields an
   //! entry with connectivity and no geometry -- which is the shape that caused
   //! the drag-and-drop monomer-library corruption when the file also covered
   //! standard residues.
   bool cif_chem_comp_has_bond_distances(const std::string &file_name);

   //! Do the structure factors in \a file_name carry phases?
   //
   //! Decides WHICH reader to use, not whether to read: deposited SF files
   //! usually have no phases, and phasing them from a model is the normal
   //! workflow. Only meaningful when classify_cif_file() said
   //! structure_factors.
   bool cif_structure_factors_have_phases(const std::string &file_name);

   //! Do the structure factors carry AMPLITUDES (F), as opposed to only
   //! intensities (I)?
   //
   //! Coot imports reflections through clipper::CIFfile into an
   //! HKL_data<F_sigF>, i.e. it asks for amplitudes. A deposited file carrying
   //! only _refln.intensity_meas yields NO usable data and Coot then renders a
   //! map of zeros with no error -- Coot 0.9 has no I->F (French-Wilson)
   //! conversion for CIF. Depositing I rather than F is common, so this must be
   //! reported rather than silently producing a blank map.
   bool cif_structure_factors_have_amplitudes(const std::string &file_name);

   //! Is this a FILE we route through gemmi?
   //
   //! Prefer this to gemmi_handles_extension(): it looks beneath a trailing
   //! ".gz", so a compressed mmCIF is recognised. Compression must not decide
   //! which parser runs -- see the note in the .cc.
   bool gemmi_handles_file(const std::string &file_name);

   //! Read a coordinate file with gemmi and convert it to an mmdb::Manager.
   //
   //! Returns a newly-allocated Manager the caller owns, or nullptr if the file
   //! could not be read as a macromolecular structure -- in which case the
   //! caller must fall back to the existing reader. A message suitable for the
   //! log is written to *message when non-null.
   //!
   //! nullptr is returned for a file that yields ZERO atoms as well as for one
   //! that throws. gemmi accepts a small-molecule CIF and returns an empty
   //! Structure without complaint, so "no atoms" is a fall-back condition, not
   //! a success -- otherwise opening a small-molecule CIF would produce an
   //! empty molecule with no error at all.
   //! \param doc_out when non-null and the file was mmCIF, receives the parsed
   //!        document with _atom_site stripped, to be kept alive for the
   //!        molecule's lifetime and handed back to the writer (Phase 3). Left
   //!        null for PDB input, which has no document. Passing nullptr skips
   //!        the retention entirely, which is what the diff harness wants.
   mmdb::Manager *read_coords_with_gemmi(const std::string &file_name,
                                         std::string *message = nullptr,
                                         std::shared_ptr<mmcif_document_t> *doc_out = nullptr);
}

#endif // GEMMI_COORDS_HH

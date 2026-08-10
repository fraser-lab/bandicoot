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

#include <string>
#include <mmdb2/mmdb_manager.h>

namespace coot {

   //! Is this a file extension we route through gemmi?
   //
   //! mmCIF only. The PDB path is deliberately left on mmdb (Scope B was
   //! rejected: same risk, no gain), and SHELX .ins/.res gemmi cannot read at
   //! all.
   bool gemmi_handles_extension(const std::string &extension);

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
   mmdb::Manager *read_coords_with_gemmi(const std::string &file_name,
                                         std::string *message = nullptr);
}

#endif // GEMMI_COORDS_HH

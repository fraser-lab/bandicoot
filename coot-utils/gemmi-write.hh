/* coot-utils/gemmi-write.hh
 *
 * Bandicoot v0.2: write an mmdb::Manager out as mmCIF through gemmi.
 *
 * The counterpart of gemmi-coords.hh. Where the read path's job is to get a
 * faithful model INTO mmdb, this one's job is to get the model back out
 * WITHOUT destroying everything mmdb never knew about.
 *
 * The mechanism is gemmi's update_mmcif_block(), which rewrites only the
 * categories it is told to and leaves every other one BIT-IDENTICAL because it
 * never touches them. Preservation is therefore the DEFAULT and costs no code:
 * a category nobody here has heard of -- including one from a future revision
 * of the spec -- survives a load/save round trip for free. Compare mmdb's own
 * mmCIF writer, which re-synthesises from a hard-coded tag list and turns 65
 * categories into 15.
 *
 * Deliberately portable, same rule as gemmi-coords: mmdb::Manager*,
 * std::string and gemmi types only. No molecule_class_info_t, no
 * graphics_info_t.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef GEMMI_WRITE_HH
#define GEMMI_WRITE_HH

#include <string>
#include <mmdb2/mmdb_manager.h>

namespace coot {

   // Forward declaration only -- see mmcif-document.hh, the one gemmi-aware
   // header.
   struct mmcif_document_t;

   //! Write \a mol to \a file_name as mmCIF, through gemmi.
   //
   //! \param doc the document this molecule was read from, or nullptr. When
   //!        given, every category Bandicoot does not regenerate is carried
   //!        through from it untouched -- that is the whole point. When null
   //!        (a molecule built de novo, or read from PDB and now being saved
   //!        as mmCIF) a fresh document is synthesised instead, which is
   //!        necessarily thinner: nothing can be preserved that was never read.
   //!
   //! \return true on success. On failure \a *message, when non-null, explains
   //!         why.
   //!
   //! There is NO fallback writer to reach for: this is the only mmCIF write
   //! path. A caller that cannot write must FAIL, and say so to the user --
   //! reporting success for a file written in some other format is the bug
   //! this replaced. Note the failure may leave a partial file behind, since
   //! the write can fail after the output is opened.
   //!
   //! NOTE \a doc is NOT modified. A COPY of it is updated and written, so the
   //! molecule's retained document keeps its read-time content -- in particular
   //! it stays free of _atom_site, which is what makes "wrote the stale
   //! coordinates" an unwritable bug (see mmcif-document.hh).
   //!
   //! This comment used to say the document was updated IN PLACE. That was true
   //! until 2026-08-17, when GUI testing found that a single save or
   //! make_backup() put the whole coordinate loop back into the retained
   //! document (5E1N: 476 items -> 478), giving back the memory the strip exists
   //! for and re-arming the stale-copy bug. Corrected 2026-08-25.
   //!
   //! CONSEQUENCE, and the reason this is worth stating: a caller may safely
   //! pass ANOTHER molecule's retained document. That is exactly what writing a
   //! symmetry mate wants -- it is an exact duplicate of its parent differing
   //! only in coordinates, so it should carry the parent's metadata rather than
   //! the thin synthesised document that passing nullptr would give it.
   //! \a doc is const BECAUSE the writer copies it. That is not decoration: it
   //! makes "a write mutated another molecule's retained document" a compile
   //! error rather than something to be careful about -- which matters now that
   //! saving a symmetry mate deliberately passes its PARENT's document.
   bool write_coords_with_gemmi(mmdb::Manager *mol,
                                const std::string &file_name,
                                const mmcif_document_t *doc,
                                std::string *message = nullptr);
}

#endif // GEMMI_WRITE_HH

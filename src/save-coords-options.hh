/* src/save-coords-options.hh
 *
 * Bandicoot v0.2: what the Save Coordinates dialog asks for, expressed without
 * reference to any widget toolkit.
 *
 * WHY THIS IS A SEPARATE HEADER, AND WHY IT HAS NO GTK IN IT
 *
 * The GTK2 UI is on its way out (see the future-proofing work), and the design
 * of this dialog -- which options exist, what the format choice MEANS, how the
 * chosen format interacts with the filename the user typed -- is the part worth
 * keeping. That is all here, in plain C++. A wx port reimplements only the
 * widget construction in save-coords-gui.cc and reuses this file verbatim.
 *
 * So: no <gtk/gtk.h> here, and nothing that pulls it in. The declarations below
 * are deliberately free of graphics_info_t as well; execute() is DECLARED here
 * and DEFINED in the .cc, which is where the heavyweight includes live.
 *
 * ONE WIDGET, TWO USES
 *
 * "Save Coordinates" and "Save Symmetry Coordinates" differ by exactly one
 * thing: the latter carries a symmetry operator and cell shifts. So they are
 * one options object with an is_symmetry flag, not two dialogs. Before this,
 * they were separate glade widgets, which is why the symmetry save silently
 * lacked the hydrogens and anisotropic checkboxes that the regular one had.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef COOT_SAVE_COORDS_OPTIONS_HH
#define COOT_SAVE_COORDS_OPTIONS_HH

#include <string>
#include <utility>
#include <vector>

namespace coot {

   //! Coordinate output formats offered in the dialog's File Type menu.
   //
   //! Deliberately an enum rather than a bool: "PDB or not" is how the tree got
   //! into extension-sniffing in the first place. Adding a format means adding
   //! an entry here, an extension in extension_for(), and a row in
   //! menu_entries() -- the dialog picks the new row up with no further work.
   //!
   //! SHELX is offered for a PLAIN save only -- see menu_entries(). It reaches
   //! the writer through save_coordinates(), which dispatches a .ins/.res/.hat
   //! extension before the PDB/mmCIF branch; writing one from a non-SHELX
   //! molecule is an explicitly supported path (write_synthetic_pre_atom_lines).
   enum class coord_file_format_t { PDB, MMCIF, SHELX };

   //! Everything the Save Coordinates dialog collects, for either of its uses.
   class save_coords_options_t {
   public:
      coord_file_format_t format = coord_file_format_t::PDB;
      bool hydrogens = true;
      bool aniso     = true;
      bool conect    = false;   //!< from graphics_info_t::write_conect_records_flag

      int imol = -1;

      //! False for a plain save; true for a symmetry mate, when the operator
      //! and shifts below are meaningful. This flag IS the difference between
      //! the dialog's two uses.
      bool is_symmetry = false;
      int symop = 0;
      int shift_a = 0, shift_b = 0, shift_c = 0;
      int pre_shift_a = 0, pre_shift_b = 0, pre_shift_c = 0;

      // ---- format <-> filename. Pure string work; no I/O, no toolkit. ----

      //! The format a filename currently implies.
      static coord_file_format_t format_from_filename(const std::string &file_name);

      //! ".pdb" / ".cif"
      static std::string extension_for(coord_file_format_t f);

      //! Does the File Type menu own this filename's extension?
      //
      //! True for the extensions of the formats the menu offers (SHELX's
      //! .ins/.res/.hat included, now that SHELX is one of them) and for a name
      //! with no extension at all. False for anything else: the user typed
      //! something deliberate and the menu must not overwrite it.
      static bool governs_extension(const std::string &file_name);

      //! Return \a file_name with its extension made to match \a f.
      //
      //! A trailing ".gz" is preserved: a compressed mmCIF is still an mmCIF,
      //! and dropping the .gz would silently change what gets written.
      //! Returns \a file_name unchanged when governs_extension() is false.
      static std::string apply_format_to_filename(const std::string &file_name,
                                                  coord_file_format_t f);

      //! Menu rows, in display order: {format, label}.
      //
      //! The dialog builds its File Type menu from this, so the label text, the
      //! ordering AND which formats are offered for which use are part of the
      //! portable design rather than of any particular toolkit's widget code.
      //!
      //! \a for_symmetry omits SHELX, because the symmetry writer goes through
      //! write_atom_selection_file(), which has PDB and mmCIF branches only.
      //! Offering SHELX there would write a PDB file under a .ins name -- the
      //! silent wrong-format save this project exists to stamp out.
      static std::vector<std::pair<coord_file_format_t, std::string> >
         menu_entries(bool for_symmetry = false);

      //! Glob patterns for a format, for a file-list filter: {"*.pdb", "*.ent"}.
      //
      //! Here rather than in the widget code for the same reason as
      //! menu_entries(): which extensions belong to a format is design, and a
      //! wx port should inherit it rather than re-derive it.
      static std::vector<std::string> filename_patterns_for(coord_file_format_t f);

      //! Human-readable name for a filter row, e.g. "PDB (*.pdb, *.ent)".
      static std::string filter_label_for(coord_file_format_t f);

      //! Perform the save. Returns 0 on success, non-zero on failure.
      int execute(const std::string &file_name) const;
   };

   //! Write a symmetry mate, honouring the dialog's options.
   //
   //! Defined in c-interface-build-symmetry.cc; declared here because
   //! save_coords_options_t::execute() dispatches to it. The long-standing
   //! scripting entry point save_symmetry_coords() delegates to this with
   //! today's defaults, so its behaviour is unchanged.
   int save_symmetry_coords_with_options(const save_coords_options_t &opts,
                                         const std::string &file_name);

}

#endif // COOT_SAVE_COORDS_OPTIONS_HH

/* src/save-coords-options.cc
 *
 * Bandicoot v0.2: the toolkit-independent half of the Save Coordinates dialog.
 * See save-coords-options.hh for why this is separated out.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#include "save-coords-options.hh"

#include "utils/coot-utils.hh"
#include "graphics-info.h"

namespace {

   // Split a trailing ".gz" off, so the format logic sees the real extension.
   // A compressed mmCIF is still an mmCIF -- coot::is_mmcif_filename() learned
   // this the hard way (see its comment); the same trap applies on the way out.
   std::pair<std::string, std::string> split_gz(const std::string &file_name) {
      const std::string gz = ".gz";
      if (file_name.size() > gz.size() &&
          file_name.compare(file_name.size() - gz.size(), gz.size(), gz) == 0)
         return std::make_pair(file_name.substr(0, file_name.size() - gz.size()), gz);
      return std::make_pair(file_name, std::string());
   }

   // Extensions the File Type menu is allowed to rewrite. SHELX's are included
   // now that SHELX is one of the offered formats -- coot::util::
   // extension_is_for_shelx_coords() is the authority on which those are, so
   // this does not re-list them and cannot drift from it.
   bool is_menu_owned_extension(const std::string &ext) {
      if (coot::util::extension_is_for_shelx_coords(ext)) return true;
      return (ext == ".pdb" || ext == ".ent" || ext == ".cif" || ext == ".mmcif");
   }
}

coot::coord_file_format_t
coot::save_coords_options_t::format_from_filename(const std::string &file_name) {

   // SHELX first, because that is the order save_coordinates() tests in: a
   // .ins/.res/.hat name is claimed before the PDB/mmCIF branch is reached, so
   // deciding otherwise here would make the menu disagree with what is written.
   const std::string base = split_gz(file_name).first;
   if (coot::util::extension_is_for_shelx_coords(coot::util::file_name_extension(base)))
      return coord_file_format_t::SHELX;

   // Then is_mmcif_filename() rather than re-deriving the rule: it already
   // handles the ".gz" case and it is the same function the write path uses to
   // decide the format, so again the menu cannot disagree with the outcome.
   return coot::is_mmcif_filename(file_name) ? coord_file_format_t::MMCIF
                                             : coord_file_format_t::PDB;
}

std::string
coot::save_coords_options_t::extension_for(coord_file_format_t f) {

   switch (f) {
   case coord_file_format_t::MMCIF: return ".cif";
   case coord_file_format_t::SHELX: return ".ins";
   case coord_file_format_t::PDB:   return ".pdb";
   }
   return ".pdb";
}

bool
coot::save_coords_options_t::governs_extension(const std::string &file_name) {

   const std::string base = split_gz(file_name).first;
   const std::string ext  = coot::util::file_name_extension(base);

   // No extension at all: the menu supplies one.
   if (ext.empty())
      return true;

   // Anything else the user typed deliberately -- leave it alone rather than
   // silently renaming their file.
   return is_menu_owned_extension(ext);
}

std::string
coot::save_coords_options_t::apply_format_to_filename(const std::string &file_name,
                                                      coord_file_format_t f) {

   if (! governs_extension(file_name))
      return file_name;

   const std::pair<std::string, std::string> parts = split_gz(file_name);
   const std::string &base = parts.first;
   const std::string &gz   = parts.second;

   const std::string ext = coot::util::file_name_extension(base);
   const std::string stem = ext.empty() ? base : base.substr(0, base.size() - ext.size());

   return stem + extension_for(f) + gz;
}

std::vector<std::pair<coot::coord_file_format_t, std::string> >
coot::save_coords_options_t::menu_entries(bool for_symmetry) {

   std::vector<std::pair<coord_file_format_t, std::string> > v;
   v.push_back(std::make_pair(coord_file_format_t::PDB,   std::string("PDB")));
   v.push_back(std::make_pair(coord_file_format_t::MMCIF, std::string("mmCIF")));

   // SHELX only for a plain save. The symmetry writer has PDB and mmCIF
   // branches only, so offering it there would produce a PDB file called
   // something.ins -- see the header.
   if (! for_symmetry)
      v.push_back(std::make_pair(coord_file_format_t::SHELX, std::string("SHELX")));

   return v;
}

std::vector<std::string>
coot::save_coords_options_t::filename_patterns_for(coord_file_format_t f) {

   std::vector<std::string> v;
   switch (f) {
   case coord_file_format_t::PDB:
      v.push_back("*.pdb"); v.push_back("*.ent");
      v.push_back("*.pdb.gz"); v.push_back("*.ent.gz");
      break;
   case coord_file_format_t::MMCIF:
      v.push_back("*.cif"); v.push_back("*.mmcif");
      v.push_back("*.cif.gz"); v.push_back("*.mmcif.gz");
      break;
   case coord_file_format_t::SHELX:
      // Matches coot::util::extension_is_for_shelx_coords(), upper case
      // included -- GTK's glob patterns are case sensitive.
      v.push_back("*.ins"); v.push_back("*.res"); v.push_back("*.hat");
      v.push_back("*.INS"); v.push_back("*.RES"); v.push_back("*.HAT");
      break;
   }
   return v;
}

std::string
coot::save_coords_options_t::filter_label_for(coord_file_format_t f) {

   switch (f) {
   case coord_file_format_t::PDB:   return "PDB (*.pdb, *.ent)";
   case coord_file_format_t::MMCIF: return "mmCIF (*.cif, *.mmcif)";
   case coord_file_format_t::SHELX: return "SHELX (*.ins, *.res, *.hat)";
   }
   return "Coordinates";
}

int
coot::save_coords_options_t::execute(const std::string &file_name) const {

   // graphics_info_t's own static, not the c-interface free function: this file
   // deliberately does not include c-interface.h.
   if (! graphics_info_t::is_valid_model_molecule(imol)) {
      std::cout << "ERROR:: save: molecule " << imol << " is not a valid model"
                << std::endl;
      return 1;
   }

   if (is_symmetry) {
      // Belt and braces: menu_entries(true) does not offer SHELX, so this should
      // be unreachable from the dialog. Refuse rather than fall through, because
      // falling through writes a PDB under whatever name was given.
      if (format == coord_file_format_t::SHELX) {
         std::cout << "ERROR:: SHELX output is not available for symmetry "
                   << "coordinates; nothing written to " << file_name << std::endl;
         return 1;
      }
      return save_symmetry_coords_with_options(*this, file_name);
   }

   return graphics_info_t::molecules[imol].save_coordinates(file_name, hydrogens,
                                                            aniso, conect);
}

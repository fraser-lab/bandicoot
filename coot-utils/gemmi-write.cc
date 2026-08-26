/* coot-utils/gemmi-write.cc
 *
 * Bandicoot v0.2: the gemmi mmCIF write path. See gemmi-write.hh.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#include "gemmi-write.hh"
#include "mmcif-document.hh"
#include "gemmi-header.hh"   // the PDB-header -> mmCIF carry-over

#include <gemmi/mmdb.hpp>       // copy_from_mmdb
#include <gemmi/to_mmcif.hpp>   // update_mmcif_block, make_mmcif_document
#include <gemmi/to_cif.hpp>     // write_cif_to_stream
// <gemmi/polyheur.hpp> was included here for setup_entities(). That approach
// was tried and REJECTED 2026-08-12 (see the comment further down, by the
// entity handling), the call went, and the include outlived it. Nothing in this
// file uses polyheur, and gemmi/mmdb.hpp includes it anyway for
// assign_subchains -- so removing it changes nothing, not even preprocessing.

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <cctype>
#include <set>
#include <vector>
#include <iostream>
#include <sstream>

// THE PER-CATEGORY POLICY.
//
// Art's vocabulary for this, which is more precise than gemmi's booleans:
//
//   EDIT   written out from the edited mmdb model, overwriting what was read
//   REGEN  regenerated from Bandicoot's own stored objects, not from mmdb
//   PASS   non-editable; carried through unchanged, read-in file -> written file
//   FORCE  affected by editing but passed through anyway because no mechanism
//          exists to update it. Names a known defect, not a choice.
//
// The governing rule (Art): anything editable in Bandicoot is EDIT or REGEN;
// anything non-editable is PASS. With one precondition bolted on, which is
// where the subtlety lives: EDIT REQUIRES THAT copy_from_mmdb ACTUALLY HAS THE
// DATA. Where mmdb holds nothing, "regenerate" does not mean recompute -- it
// means DELETE. So a category that is editable but has no faithful source is
// not a toggle decision, it is a bug to be fixed at the source.
//
// gemmi's MmcifOutputGroups is a plain bool per category, so EDIT and REGEN
// both map to true and PASS and FORCE both map to false. The four names still
// matter because they say WHY, and the why is what stops the next person
// flipping one of them.
static gemmi::MmcifOutputGroups bandicoot_output_groups() {

   gemmi::MmcifOutputGroups g(false);   // PASS everything, then opt in

   // EDIT -- the coordinates themselves. Covers occupancy AND anisotropy:
   // both are _atom_site content and there is no separate anisotrop toggle
   // among the 33, so they ride along with atoms. Both are explicitly edited
   // in Bandicoot (set_occupancy_residue_range, and refinement writes B).
   g.atoms = true;

   // PASS -- NOT EDIT. Corrected 2026-08-13, after the write-side gate learned
   // to compare COLUMNS: with atom_type = EDIT gemmi rebuilt the category from
   // the model, and it knows only the element symbol, so every scattering
   // column was silently dropped -- 13 of them, including the Cromer-Mann
   // coefficients and the dispersion corrections (measured on 3K0N and 1FFK).
   //
   // This is Art's own rule catching a mistake of mine: EDIT requires that
   // copy_from_mmdb actually HAS the data. mmdb has no scattering factors, so
   // there "regenerate" means DELETE.
   //
   // The cost of PASS is that an edit introducing a NEW element would leave it
   // unlisted, so the category is AUGMENTED after the update instead -- see
   // augment_atom_type(): original rows kept verbatim, any missing symbol
   // appended with nulls for the columns we cannot supply.
   g.atom_type = false;

   // EDIT -- cell and space group ARE alterable in Bandicoot:
   // set_space_group() and set_unit_cell_and_space_group()
   // (src/c-interface-build-symmetry.cc), both exposed to Python and Guile,
   // and automatic paths stamp a model's symmetry from a map's. Safe because
   // both round-trip exactly.
   //
   // GUARDED BELOW: for a file that had no cell, copy_from_mmdb yields a ZERO
   // cell and this would CREATE a _cell full of zeros where the input had
   // none -- the mirror image of the read path's "do not fabricate a cell".
   g.cell = true;
   g.symmetry = true;

   // EDIT -- cis peptides. Note write_atom_selection_file() calls
   // coot::util::remove_wrong_cis_peptides() on the model before we get here,
   // so Coot actively maintains these; leaving them PASS would mean that
   // correction never reaches the file. gemmi's bridge carries CisPep in both
   // directions (mmdb.hpp), so the EDIT precondition is satisfied.
   g.cis = true;

   // _atom_site.group_PDB is the ATOM/HETATM column. Standard, and every file
   // in the corpus has it.
   g.group_pdb = true;

   // _atom_site.auth_atom_id and auth_comp_id. gemmi defaults these OFF even
   // in MmcifOutputGroups(true), but every corpus file carries them, so
   // leaving them off would DROP two columns from a file that had them.
   // Measured: 5E1N, 3NYD_hierarchy, 2RSF all carry both.
   //
   // KNOWN DEVIATION: a file carrying only one of the pair (SC1_2_refine_036)
   // gets both written, so it gains a column. That is an addition rather than
   // a loss, and the write-side gate reports it rather than hiding it.
   g.auth_all = true;

   // Everything else stays PASS. Worth naming the ones that were argued over,
   // because each looks like it "should" be regenerated and must not be:
   //
   //   conn        - PASS for now, REGEN later. mmdb::Link HAS NO TYPE FIELD,
   //                 so ANY round trip through mmdb's LINK table collapses
   //                 covale/disulf/metalc/hydrog into an untyped LINK. On top
   //                 of that the read path filters Hydrog out before
   //                 copy_to_mmdb, so EDIT would silently drop 660 hydrogen
   //                 bonds from SC1_2_refine_036. Connectivity editing is
   //                 coming as its own piece of work; until then the file's
   //                 own struct_conn is the truth.
   //   ncs         - copy_from_mmdb does not carry NCS at all, so EDIT would
   //                 DELETE _struct_ncs_oper -- on exactly the files where
   //                 GitHub #9 lives.
   //   entity, entity_poly, entity_poly_seq
   //               - the SEQUENCE of the molecule being worked on, regardless
   //                 of how much of it is modelled. Deleting a helix does not
   //                 change the construct's sequence, only what is observed.
   //   struct_conf, struct_sheet
   //               - matches the PDB path's long-standing behaviour (mmdb
   //                 round-trips HELIX/SHEET byte-identically) and is strictly
   //                 better than mmdb's mmCIF behaviour, which is deletion.
   //   title_keywords, author, database_status, exptl, diffrn, reflns,
   //   refine, software, tls, ...
   //               - provenance. Not ours to rewrite.
   //
   // FORCE (no toggle exists, so they can only pass through however stale they
   // get): pdbx_unobs_or_zero_occ_residues, invalidated by occupancy edits;
   // pdbx_poly_seq_scheme, invalidated by any residue deletion. Fixing either
   // needs our own code. This is the one tier the governing rule cannot reach,
   // so it is written down rather than discovered.

   return g;
}


// HOW THE FILE IS LAID OUT -- and why this is NOT gemmi's Style::Pdbx preset.
//
// Style::Pdbx sets prefer_pairs, which rewrites any SINGLE-ROW LOOP as a list
// of pairs on output. That is a perfectly ordinary mmCIF formatting choice and
// it is WRONG HERE, because it reformats categories we promised to pass
// through untouched. Measured on the corpus: _citation, _software,
// _em_software, _struct_conf_type, _struct_ncs_ens and -- most pointedly --
// _pdbx_state_coexistence, one of the heterogeneity categories this whole
// project exists to preserve, all came back as pairs where the input had
// one-row loops. No data was lost, but "bit-identical because it never touches
// them" was not true of the bytes.
//
// So: keep the '#' separators that PDBx files carry, and leave loops as loops.
static gemmi::cif::WriteOptions bandicoot_write_options() {
   gemmi::cif::WriteOptions opt;
   opt.misuse_hash  = true;    // '#' between categories, as PDBx files have
   opt.prefer_pairs = false;   // a one-row loop stays a one-row loop
   // Pad to even columns the way wwPDB does, rather than single-spacing.
   // Purely cosmetic -- but these files are read by humans, and it also makes
   // a no-op save far closer to byte-identical, which is what makes `diff`
   // usable as a QA tool again. Values match gemmi's Style::Aligned.
   opt.align_pairs = 33;
   opt.align_loops = 30;
   return opt;
}


// Keep _atom_type complete when the model gained an element.
//
// _atom_type is PASS, so the original rows (with their scattering factors)
// come through untouched -- but a user who adds a ligand containing an element
// the file never had would leave that element unlisted. Append a row for each
// missing symbol, with "?" for every column we cannot supply. Over-listing is
// harmless and normal; under-listing is a validation error.
static void augment_atom_type(const gemmi::Structure &st, gemmi::cif::Block &block) {

   std::set<std::string> in_model;
   for (const gemmi::Model &model : st.models)
      for (const gemmi::Chain &chain : model.chains)
         for (const gemmi::Residue &res : chain.residues)
            for (const gemmi::Atom &atom : res.atoms) {
               std::string e = atom.element.uname();     // gemmi's canonical spelling
               if (! e.empty()) in_model.insert(e);
            }
   if (in_model.empty()) return;

   gemmi::cif::Table tab = block.find_mmcif_category("_atom_type");
   if (! tab.ok() || ! tab.loop_item) return;   // absent, or stored as pairs

   gemmi::cif::Loop &loop = tab.loop_item->loop;
   int sym_col = loop.find_tag("_atom_type.symbol");
   if (sym_col < 0) return;

   // Compared CASE-INSENSITIVELY, which is not fussiness. gemmi normalises
   // element symbols to upper case on read (TRAPS B10), so a file writing "Cl"
   // reaches us as "CL" and a literal comparison declares it missing -- adding
   // a SECOND chlorine row, with nulls for all 13 scattering columns, beside
   // the file's own complete one. Measured on SC1_2_refine_036: `C Cl H N O S`
   // came back as `C Cl H N O S CL`. The file is then wrong in a way nothing
   // downstream would question, so the case decision must not leak this far.
   auto upper = [](std::string s) {
      for (char &c : s) c = std::toupper(static_cast<unsigned char>(c));
      return s;
   };

   std::set<std::string> listed;
   for (size_t r = 0; r < loop.length(); r++)
      listed.insert(upper(gemmi::cif::as_string(loop.val(r, (size_t) sym_col))));

   for (const std::string &e : in_model) {
      if (listed.count(upper(e))) continue;
      std::vector<std::string> row(loop.width(), "?");
      row[sym_col] = e;
      loop.values.insert(loop.values.end(), row.begin(), row.end());
   }
}


// Put back any non-standard _atom_site column harvested at read time.
//
// update_mmcif_block() rebuilds _atom_site from the model, so it emits only the
// columns gemmi models -- pdbx_heterogeneity_id and anything else the file
// carried is gone. Re-add each stored column, filling it by matching each
// regenerated row back to its atom.
//
// An atom with no stored value gets "." -- which is the same drop-on-edit
// behaviour as label_*: a newly built or renumbered atom has no heterogeneity
// id, and inventing one would be worse than leaving it null.
static void restore_atom_extra_columns(const coot::mmcif_document_t *doc,
                                       gemmi::cif::Block &block) {

   if (! doc || doc->atom_extra_columns.empty()) return;

   gemmi::cif::Table tab = block.find_mmcif_category("_atom_site");
   if (! tab.ok() || ! tab.loop_item) return;
   gemmi::cif::Loop &loop = tab.loop_item->loop;

   int c_model = loop.find_tag("_atom_site.pdbx_PDB_model_num");
   int c_chain = loop.find_tag("_atom_site.auth_asym_id");
   int c_seq   = loop.find_tag("_atom_site.auth_seq_id");
   int c_ic    = loop.find_tag("_atom_site.pdbx_PDB_ins_code");
   int c_name  = loop.find_tag("_atom_site.label_atom_id");
   int c_alt   = loop.find_tag("_atom_site.label_alt_id");
   if (c_chain < 0 || c_seq < 0 || c_name < 0) return;

   for (std::map<std::string, std::map<std::string, std::string> >::const_iterator
           col = doc->atom_extra_columns.begin();
        col != doc->atom_extra_columns.end(); ++col) {

      std::string tag = "_atom_site." + col->first;
      if (loop.has_tag(tag)) continue;      // gemmi wrote it after all

      size_t nrows = loop.length(), width = loop.width();
      std::vector<std::string> rebuilt;
      rebuilt.reserve((width + 1) * nrows);
      size_t n_found = 0;
      for (size_t r = 0; r < nrows; r++) {
         for (size_t c = 0; c < width; c++)
            rebuilt.push_back(loop.val(r, c));
         std::string key = coot::mmcif_document_t::atom_extra_key(
            c_model >= 0 ? gemmi::cif::as_string(loop.val(r, (size_t) c_model)) : "1",
            gemmi::cif::as_string(loop.val(r, (size_t) c_chain)),
            gemmi::cif::as_string(loop.val(r, (size_t) c_seq)),
            c_ic  >= 0 ? gemmi::cif::as_string(loop.val(r, (size_t) c_ic))  : "",
            gemmi::cif::as_string(loop.val(r, (size_t) c_name)),
            c_alt >= 0 ? gemmi::cif::as_string(loop.val(r, (size_t) c_alt)) : "");
         std::map<std::string, std::string>::const_iterator v = col->second.find(key);
         // An EMPTY value must never be written: gemmi emits nothing for it and
         // the row comes out a token short, silently corrupting the loop. Any
         // absent or empty value becomes an explicit CIF null.
         if (v != col->second.end() && ! v->second.empty()) {
            rebuilt.push_back(v->second);
            n_found++;
         } else {
            rebuilt.push_back(".");
         }
      }
      loop.tags.push_back(tag);
      loop.values.swap(rebuilt);
      std::cout << "INFO:: restored _atom_site." << col->first << " for "
                << n_found << " of " << nrows << " atoms" << std::endl;
   }
}


// Rebuild the identifying columns of _atom_site_anisotrop.
//
// gemmi regenerates that category with id, type_symbol and the six U values --
// dropping ten pdbx_* columns that name the atom the tensor belongs to
// (measured: 18 columns in, 8 out, on 9 corpus files). The tensors and the id
// survive, so nothing is unrecoverable, but the rows stop saying WHICH atom
// they describe without following the id reference.
//
// Those columns are pure duplication of _atom_site, so they are rebuilt by
// joining on the atom id -- both loops are regenerated together from the same
// model, so the ids are consistent by construction. Only the columns the input
// actually had are re-added; a file that never carried them does not gain any.
static void restore_anisotrop_columns(const coot::mmcif_document_t *doc,
                                      gemmi::cif::Block &block) {

   if (! doc || doc->anisotrop_tags.empty()) return;

   gemmi::cif::Table an = block.find_mmcif_category("_atom_site_anisotrop");
   if (! an.ok() || ! an.loop_item) return;
   gemmi::cif::Loop &aloop = an.loop_item->loop;
   int a_id = aloop.find_tag("_atom_site_anisotrop.id");
   if (a_id < 0) return;

   gemmi::cif::Table at = block.find_mmcif_category("_atom_site");
   if (! at.ok() || ! at.loop_item) return;
   const gemmi::cif::Loop &sloop = at.loop_item->loop;
   int s_id = sloop.find_tag("_atom_site.id");
   if (s_id < 0) return;

   std::map<std::string, size_t> id_to_row;
   for (size_t r = 0; r < sloop.length(); r++)
      id_to_row[gemmi::cif::as_string(sloop.val(r, (size_t) s_id))] = r;

   // anisotrop column -> the _atom_site column it duplicates
   static const std::map<std::string, std::string> src = {
      { "pdbx_label_atom_id", "label_atom_id" },
      { "pdbx_label_alt_id",  "label_alt_id"  },
      { "pdbx_label_comp_id", "label_comp_id" },
      { "pdbx_label_asym_id", "label_asym_id" },
      { "pdbx_label_seq_id",  "label_seq_id"  },
      { "pdbx_PDB_ins_code",  "pdbx_PDB_ins_code" },
      { "pdbx_auth_seq_id",   "auth_seq_id"   },
      { "pdbx_auth_comp_id",  "auth_comp_id"  },
      { "pdbx_auth_asym_id",  "auth_asym_id"  },
      { "pdbx_auth_atom_id",  "auth_atom_id"  } };

   for (const std::string &tag : doc->anisotrop_tags) {
      if (aloop.has_tag(tag)) continue;
      std::string short_name = tag.substr(tag.find('.') + 1);
      std::map<std::string, std::string>::const_iterator m = src.find(short_name);
      if (m == src.end()) continue;               // not one we can rebuild
      int s_col = sloop.find_tag("_atom_site." + m->second);
      if (s_col < 0) continue;

      size_t nrows = aloop.length(), width = aloop.width();
      std::vector<std::string> rebuilt;
      rebuilt.reserve((width + 1) * nrows);
      for (size_t r = 0; r < nrows; r++) {
         for (size_t c = 0; c < width; c++)
            rebuilt.push_back(aloop.val(r, c));
         std::map<std::string, size_t>::const_iterator it =
            id_to_row.find(gemmi::cif::as_string(aloop.val(r, (size_t) a_id)));
         rebuilt.push_back(it == id_to_row.end() ? "."
                           : sloop.val(it->second, (size_t) s_col));
      }
      aloop.tags.push_back(tag);
      aloop.values.swap(rebuilt);
   }
}


// Rebuild columns that duplicate another column of the SAME category.
//
// Some categories gemmi regenerates come out narrower than the input, dropping
// columns whose values are pure duplication of a sibling column. The auth_*
// residue names in _struct_mon_prot_cis are the case in hand: for a residue the
// auth and label comp ids are the same string, and gemmi simply does not write
// the auth spelling.
//
// Kept as a table rather than code per category so the next such column is one
// line. Only re-adds a column the INPUT actually had.
static void restore_duplicated_columns(gemmi::cif::Block &block,
                                       const char *category,
                                       const std::vector<std::string> &wanted_tags,
                                       const std::map<std::string, std::string> &dup_of) {

   if (wanted_tags.empty()) return;
   gemmi::cif::Table tab = block.find_mmcif_category(category);
   if (! tab.ok() || ! tab.loop_item) return;
   gemmi::cif::Loop &loop = tab.loop_item->loop;

   for (const std::string &tag : wanted_tags) {
      if (loop.has_tag(tag)) continue;
      std::string short_name = tag.substr(tag.find('.') + 1);
      std::map<std::string, std::string>::const_iterator d = dup_of.find(short_name);
      if (d == dup_of.end()) continue;
      int src = loop.find_tag(std::string(category) + "." + d->second);
      if (src < 0) continue;

      size_t nrows = loop.length(), width = loop.width();
      std::vector<std::string> rebuilt;
      rebuilt.reserve((width + 1) * nrows);
      for (size_t r = 0; r < nrows; r++) {
         for (size_t c = 0; c < width; c++) rebuilt.push_back(loop.val(r, c));
         rebuilt.push_back(loop.val(r, (size_t) src));
      }
      loop.tags.push_back(tag);
      loop.values.swap(rebuilt);
   }
}


// Put the coordinate categories back where the input had them.
//
// _atom_site is stripped from the retained document at read time, so
// update_mmcif_block() re-adds it at the END of the block. Everything that
// followed the coordinates in the original file then precedes them -- twelve
// categories in 3K0N, including _pdbx_poly_seq_scheme and the whole
// _pdbx_audit_revision_* group. Category order means nothing to a CIF parser,
// but it is a difference from the input that nobody asked for.
//
// Moves _atom_site to its recorded index and _atom_site_anisotrop directly
// after it. std::rotate rather than erase+insert because cif::Item is movable
// but not copyable.
static void restore_category_order(const coot::mmcif_document_t *doc,
                                   gemmi::cif::Block &block) {

   if (! doc || doc->atom_site_item_index < 0) return;

   const char *cats[] = { "_atom_site", "_atom_site_anisotrop" };
   size_t target = static_cast<size_t>(doc->atom_site_item_index);

   for (const char *cat : cats) {
      if (target >= block.items.size()) return;
      gemmi::cif::Table tab = block.find_mmcif_category(cat);
      if (! tab.ok() || ! tab.loop_item) continue;
      size_t from = static_cast<size_t>(tab.loop_item - block.items.data());
      if (from == target) { target++; continue; }
      if (from > target)
         std::rotate(block.items.begin() + target,
                     block.items.begin() + from,
                     block.items.begin() + from + 1);
      else
         std::rotate(block.items.begin() + from,
                     block.items.begin() + from + 1,
                     block.items.begin() + target + 1);
      target++;
   }
}


// Write the document, compressing when the name says to.
//
// THIS IS NOT OPTIONAL, and it is the one thing the writer swap could not
// leave for later. make_backup() names its file from
// backup_compress_files_flag, which DEFAULTS TO 1 -- so essentially every
// backup of an mmCIF molecule is called "*.cif.gz". mmdb's WriteCIFASCII got
// compression for free because its signature defaults to io::GZM_CHECK, which
// means "compress if the filename ends in .gz". A plain std::ofstream does
// not, so writing plain text into a .gz name produces a backup that Undo
// CANNOT READ BACK -- it re-reads through the mmdb path, which tries to
// gunzip it. Losing undo on every mmCIF molecule would have been the cost.
//
// zlib rather than shelling out to gzip: no subprocess, no PATH dependency,
// and it is free -- /usr/lib/libz is a macOS system library, zlib.h ships in
// the SDK, and libgemmi_cpp already links it for read_cif_gz, so it is
// already in Bandicoot's runtime closure and bundles nothing.
static bool write_doc(const gemmi::cif::Document &doc, const std::string &file_name,
                      std::string *message) {

   const bool compress = file_name.size() > 3 &&
                         file_name.compare(file_name.size() - 3, 3, ".gz") == 0;

   if (! compress) {
      std::ofstream os(file_name.c_str());
      if (! os) {
         if (message) *message = "cannot open " + file_name + " for writing";
         return false;
      }
      gemmi::cif::write_cif_to_stream(os, doc, bandicoot_write_options());
      return static_cast<bool>(os);
   }

   std::ostringstream ss;
   gemmi::cif::write_cif_to_stream(ss, doc, bandicoot_write_options());
   const std::string text = ss.str();

   gzFile gz = gzopen(file_name.c_str(), "wb");
   if (! gz) {
      if (message) *message = "cannot open " + file_name + " for compressed writing";
      return false;
   }
   // Chunked: gzwrite takes an unsigned int, and a large structure's
   // regenerated _atom_site can run to tens of megabytes.
   const size_t chunk = 1u << 24;
   for (size_t off = 0; off < text.size(); off += chunk) {
      unsigned n = static_cast<unsigned>(std::min(chunk, text.size() - off));
      if (gzwrite(gz, text.data() + off, n) != static_cast<int>(n)) {
         gzclose(gz);
         if (message) *message = "short write to " + file_name;
         return false;
      }
   }
   if (gzclose(gz) != Z_OK) {
      if (message) *message = "error closing " + file_name;
      return false;
   }
   return true;
}


bool
coot::write_coords_with_gemmi(mmdb::Manager *mol,
                              const std::string &file_name,
                              coot::mmcif_document_t *doc,
                              std::string *message) {

   if (! mol) {
      if (message) *message = "no molecule";
      return false;
   }

   try {
      // gemmi's bridge marks an anisotropic atom with ASET_Anis_tFSigma, where
      // mmdb's own convention -- and every consumer in mmdb and Coot -- is
      // ASET_Anis_tFac. The read path translates gemmi's flag into mmdb's
      // (adjustment 10); this is the other end of that translation, putting the
      // flag gemmi looks for back just long enough to be read, and only on
      // atoms that did not already have it. Without this, copy_from_mmdb sees
      // no anisotropy at all and _atom_site_anisotrop would be dropped from
      // every mmCIF we write.
      std::vector<mmdb::Atom *> retagged;
      for (int imod = 1; imod <= mol->GetNumberOfModels(); imod++) {
         mmdb::Model *model = mol->GetModel(imod);
         if (! model) continue;
         for (int ich = 0; ich < model->GetNumberOfChains(); ich++) {
            mmdb::Chain *chain = model->GetChain(ich);
            if (! chain) continue;
            for (int ires = 0; ires < chain->GetNumberOfResidues(); ires++) {
               mmdb::Residue *res = chain->GetResidue(ires);
               if (! res) continue;
               for (int iat = 0; iat < res->GetNumberOfAtoms(); iat++) {
                  mmdb::Atom *at = res->GetAtom(iat);
                  if (! at || at->isTer()) continue;
                  if ((at->WhatIsSet & mmdb::ASET_Anis_tFac) &&
                      ! (at->WhatIsSet & mmdb::ASET_Anis_tFSigma)) {
                     at->WhatIsSet |= mmdb::ASET_Anis_tFSigma;
                     retagged.push_back(at);
                  }
               }
            }
         }
      }

      gemmi::Structure st = gemmi::copy_from_mmdb(mol);

      // Put the model back exactly as it was: an atom left carrying the sigma
      // flag would gain a SIGUIJ record on the next PDB save.
      for (mmdb::Atom *at : retagged)
         at->WhatIsSet &= ~mmdb::ASET_Anis_tFSigma;

      // (W1) Trim mmdb's atom-name padding.
      //
      // mmdb stores names in PDB's fixed columns, so nitrogen is " N  ". gemmi's
      // copy_from_mmdb copies that VERBATIM, and the mmCIF writer then has to
      // quote it because it contains spaces -- the file comes out with
      //     _atom_site.label_atom_id  ' N  '
      // where the input said  N. Legal CIF, but a name of literally " N  " is
      // not what any downstream tool expects, and it is not what was read in.
      // Found by Art in GUI testing 2026-08-12, on a save after undo.
      auto trim_name = [](std::string &s) {
         size_t b = s.find_first_not_of(' ');
         size_t e = s.find_last_not_of(' ');
         s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
      };

      for (gemmi::Model &model : st.models)
         for (gemmi::Chain &chain : model.chains)
            for (gemmi::Residue &res : chain.residues)
               for (gemmi::Atom &atom : res.atoms)
                  trim_name(atom.name);

      // AND THE LINK RECORDS THAT POINT AT THEM. Trimming the model alone left
      // st.connections holding mmdb's padded spelling (" O2A") while the atoms
      // it refers to had become "O2A", so gemmi's struct_conn writer -- which
      // resolves each partner against the model to emit label_atom_id -- found
      // nothing and wrote "?" for BOTH atom ids and the distance.
      //
      // Consequence, found by the round-trip check on pdb1aon.ent: converting a
      // PDB with LINK records to mmCIF produced a _struct_conn with no atom
      // names, so reading that file back gave 27 links of which 0 resolved --
      // no link bonds drawn, no metal restraints. Silently inert, exactly like
      // the two read-side link bugs before it (unpadded names from gemmi,
      // blank-as-a-space fields from mmdb's writer).
      //
      // Only the synthesis path could show this: with a retained document
      // struct_conn is PASS and is never rewritten, which is why mmCIF -> mmCIF
      // was unaffected and only the PDB -> mmCIF direction broke.
      for (gemmi::Connection &con : st.connections) {
         trim_name(con.partner1.atom_name);
         trim_name(con.partner2.atom_name);
      }

      // (W2) Put the label_* identities back.
      //
      // These were harvested at read time (adjustment 8 in gemmi-coords.cc)
      // because mmdb cannot hold them. Here they go back onto the Structure
      // that copy_from_mmdb just produced, keyed by the residue's AUTHOR
      // identity -- the fields mmdb DOES preserve.
      //
      // THE INVALIDATION POLICY IS FREE, and that is the point of keying it
      // this way. If the user renumbered a residue or moved it to another
      // chain, its key no longer matches, the lookup misses, and the label_*
      // columns come out null for that residue. Agreed with Art 2026-08-13:
      // drop for edited residues rather than write a stale second opinion
      // about what the residue is. Partially-populated columns are normal --
      // phenix.refine writes a null label_entity_id for every atom.
      if (doc && ! doc->residue_labels.empty()) {

         std::map<std::string, std::vector<std::string> > entity_subchains;

         for (gemmi::Model &model : st.models) {
            int model_num = model.num;
            for (gemmi::Chain &chain : model.chains) {
               for (gemmi::Residue &res : chain.residues) {
                  std::string icode(1, res.seqid.icode);
                  if (res.seqid.icode == ' ' || res.seqid.icode == '\0') icode.clear();
                  std::string key = coot::mmcif_document_t::residue_label_key(
                                       model_num, chain.name, res.seqid.num.value, icode);
                  std::map<std::string, coot::residue_labels_t>::const_iterator it =
                     doc->residue_labels.find(key);
                  if (it == doc->residue_labels.end())
                     continue;                       // edited: leave null
                  const coot::residue_labels_t &rl = it->second;
                  res.subchain = rl.label_asym_id;
                  if (rl.has_label_seq)
                     res.label_seq = rl.label_seq;
                  if (! rl.entity_id.empty() && ! rl.label_asym_id.empty()) {
                     std::vector<std::string> &v = entity_subchains[rl.entity_id];
                     if (std::find(v.begin(), v.end(), rl.label_asym_id) == v.end())
                        v.push_back(rl.label_asym_id);
                  }
               }
            }
         }

         // label_entity_id is not stored on the residue -- gemmi derives it
         // from whichever Entity owns the subchain -- so the entity list has
         // to be rebuilt too, or the column comes out null even where the
         // subchain is right.
         for (std::map<std::string, std::vector<std::string> >::const_iterator
                 e = entity_subchains.begin(); e != entity_subchains.end(); ++e) {
            gemmi::Entity ent(e->first);
            ent.subchains = e->second;
            st.entities.push_back(ent);
         }
      }

      // (W3) The label_* hierarchy is NOT regenerated when it was not carried -- see the note in
      // gemmi-write.hh. setup_entities() was tried and REJECTED 2026-08-12: it
      // does not restore the input's values, it INVENTS new ones (5E1N came
      // back with label_asym_id "Axp" and label_entity_id "A" where the file
      // said "A" and "1", and label_seq_id stayed null). Writing plausible but
      // wrong values is worse than writing none, so it is deliberately not
      // called. Leaving them null is the honest state until the read path
      // carries these columns across.

      // Do not create a cell where the input had none.
      //
      // The read path deliberately un-sets the cell for a model that has no
      // crystallographic one (an NMR ensemble, an EM model), so copy_from_mmdb
      // hands back a ZERO cell -- and writing that would put a _cell category
      // full of zeros into a file that legitimately had no _cell at all.
      // is_crystal() is false for the zero cell as well as the 1,1,1 default.
      gemmi::MmcifOutputGroups groups = bandicoot_output_groups();
      if (! st.cell.is_crystal()) {
         groups.cell = false;
         groups.symmetry = false;
      }

      if (doc && ! doc->empty()) {

         // Update a COPY of the retained document, never the document itself.
         //
         // This used to work in place, and that was a real defect rather than
         // an inefficiency: update_mmcif_block() writes _atom_site (and the
         // anisotrop rebuild writes _atom_site_anisotrop) into the block, so
         // after the first save -- or the first make_backup(), which happens on
         // every edit, from 106 call sites -- the molecule's retained document
         // carried the whole coordinate loop again. Measured on 5E1N: 476 items
         // after the read, 478 with _atom_site and _atom_site_anisotrop PRESENT
         // after one write.
         //
         // Two things that undid:
         //   - the memory saving stripping exists for (1FFK ~41 MB -> ~10 MB per
         //     open molecule) was given back permanently on the first edit; and
         //   - D3's reasoning was that not holding a second _atom_site makes
         //     "wrote the stale copy" an unwritable bug. In-place editing put
         //     the stale copy back, a snapshot from the previous write.
         // Found by Art in GUI testing 2026-08-17: the header browser showed the
         // coordinates, which is what a second copy looks like from outside.
         //
         // The copy is cheap precisely because of the strip: the retained
         // document is 0.06-1.23 MB of text across the corpus.
         gemmi::cif::Document out = doc->doc;

         // Target blocks[0] EXPLICITLY. A coordinate mmCIF may legitimately
         // carry further blocks -- phenix.refine appends the ligand restraint
         // dictionary as data_comp_XX, and deposition files put restraints in
         // later blocks. gemmi guarantees only the first block has _atom_site.
         // Writing into "the" block would corrupt a restraint dictionary.
         if (out.blocks.empty()) {
            if (message) *message = "retained document has no blocks";
            return false;
         }
         gemmi::cif::Block &block = out.blocks[0];

         gemmi::update_mmcif_block(st, block, groups);
         augment_atom_type(st, block);
         restore_atom_extra_columns(doc, block);
         restore_anisotrop_columns(doc, block);
         {  // _struct_mon_prot_cis: gemmi omits the auth_ residue names, which
            // for a residue are the same string as the label_ ones.
            static const std::map<std::string, std::string> cis_dups = {
               { "auth_comp_id",         "label_comp_id" },
               { "pdbx_auth_comp_id_2",  "pdbx_label_comp_id_2" } };
            restore_duplicated_columns(block, "_struct_mon_prot_cis",
                                       doc->cis_tags, cis_dups);
         }
         restore_category_order(doc, block);

         if (! write_doc(out, file_name, message))
            return false;
         if (message) *message = "written by gemmi (document preserved)";

      } else {

         // No retained document: built de novo, or read from PDB and now being
         // saved as mmCIF. Nothing can be preserved that was never read, so
         // synthesise. Cross-format conversion being lossy in both directions
         // is accepted -- a PDB file simply does not contain the things an
         // mmCIF is expected to have, and enriching it is a refinement
         // program's job, not Bandicoot's.
         //
         // Synthesis takes ALL groups, not our policy: with no document to
         // preserve, "PASS" would mean "write nothing", which would produce a
         // file missing categories gemmi could perfectly well have derived
         // from the model.
         // Carry across what mmdb DOES hold of the PDB header first --
         // copy_from_mmdb() takes none of it, so without this the conversion
         // writes eight categories and drops the title, authors, citation,
         // keywords, method, secondary structure and every REMARK the input
         // had. See gemmi-header.hh; this is the mirror of adjustment (9) on
         // the read side.
         coot::transfer_pdb_header_to_gemmi(mol, st);

         gemmi::cif::Document out = gemmi::make_mmcif_document(st);

         // ... then the categories gemmi has no model for: it has no author
         // field and no citation field anywhere in Metadata, and nowhere to
         // put free REMARK text.
         if (! out.blocks.empty())
            coot::add_pdb_header_categories(mol, out.blocks[0]);

         if (! write_doc(out, file_name, message))
            return false;
         if (message) *message = "written by gemmi (synthesised document)";
      }

   } catch (const std::exception &e) {
      if (message) *message = e.what();
      return false;
   }

   return true;
}

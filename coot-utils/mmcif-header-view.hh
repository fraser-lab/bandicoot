/* coot-utils/mmcif-header-view.hh
 *
 * Bandicoot v0.2: turn a retained mmCIF document into something a header
 * browser can display, category by category.
 *
 * WHY THIS SHAPE
 *
 * The old Header Browser was written for PDB: hand-built Compound / Author /
 * Journal panels plus one panel per REMARK number. That reads well and cannot
 * grow -- every field it shows exists because someone wrote code for it, so a
 * category nobody anticipated is invisible. mmCIF's whole advantage is that it
 * accommodates new categories (the hierarchical-heterogeneity work adds two,
 * and more will follow from other groups), and a browser built the old way
 * would have to be edited for each one.
 *
 * So the view is driven by WHAT THE FILE CONTAINS, not by a list of things we
 * know about: every category in the document is rendered, in one of the two
 * shapes mmCIF has (a set of tag/value pairs, or a loop). An unknown category
 * appears with no code changes at all.
 *
 * READABILITY IS THEN ADDITIVE, never subtractive. A curated table supplies the
 * familiar PDB-style wording for the tags that have it ("R value (working
 * set)"), a mechanical prettifier handles the rest, and THE RAW TAG IS ALWAYS
 * CARRIED ALONGSIDE. Nothing is ever hidden behind a label, so completeness is
 * structural and the labels are a convenience on top.
 *
 * Note there is no authoritative source for short labels: the PDBx dictionary
 * (mmcif_pdbx_v50.dic) gives a prose description per tag -- seven sentences and
 * an equation for _refine.ls_R_factor_R_work -- which is tooltip material, not
 * a column header. The short forms come from the PDB format specification, so
 * they exist only for tags with a PDB counterpart.
 *
 * Portable by design (rule 7): plain structs, no gemmi in this header, no GTK
 * anywhere. The GUI layer only lays out what this produces, so the browser
 * survives the toolkit rewrite.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef MMCIF_HEADER_VIEW_HH
#define MMCIF_HEADER_VIEW_HH

#include <string>
#include <vector>

namespace coot {

   struct mmcif_document_t;   // forward declaration; definition includes gemmi

   //! What KIND of thing a category describes. Used for panel colour, so that
   //! the window reads as groups at a glance the way the PDB browser's coloured
   //! REMARK panels do.
   //!
   //! Classified by category-name prefix rather than by an enumeration of
   //! categories, so an unfamiliar `_refine_something_new` still lands in
   //! refinement and gets the right colour with no edit here. `other` is a
   //! real answer, not a failure.
   enum class header_family_t {
      bibliography,    //!< title, authors, citation, database, audit trail
      entity,          //!< what the molecule IS: entities, sequence, components
      experiment,      //!< sample, data collection, reflections
      refinement,      //!< refinement statistics and software
      symmetry,        //!< cell, space group, matrices, assemblies
      annotation,      //!< assertions about the model: SS, links, sites, validation
      heterogeneity,   //!< the hierarchical-heterogeneity categories
      other
   };

   //! One category of an mmCIF document, ready to display.
   struct header_category_t {
      std::string name;        //!< "_refine"
      std::string label;       //!< "Refinement", or empty if we have no better word
      bool is_loop = false;    //!< a table rather than a list of values
      std::size_t n_rows = 0;  //!< rows in a loop, or fields in a pair category
      bool prominent = false;  //!< worth showing first
      //! A per-atom / per-residue bookkeeping table rather than header
      //! information: the embedded component dictionary, the sequence
      //! numbering schemes, the scattering-factor list. Hidden by default so
      //! the window keeps to what a "header browser" implies.
      //!
      //! Deliberately an EXCLUSION: a category is header unless it is on a
      //! short list of known body tables, so a category nobody has heard of --
      //! the next group's extension, or ours -- shows up without an edit here.
      //! An inclusion list would have the opposite and wrong polarity.
      bool body = false;
      header_family_t family = header_family_t::other;   //!< panel colour

      //! Column tags WITHOUT the category prefix ("ls_d_res_high"), and the
      //! label for each. Both are always populated; the caller decides which to
      //! show, and showing both is the intended default.
      std::vector<std::string> column_tags;
      std::vector<std::string> column_labels;

      //! rows[r][c] for a loop. A pair category has exactly one row, so the
      //! same structure serves both and the renderer only chooses a layout.
      std::vector<std::vector<std::string> > rows;
   };

   //! Build the display model for a molecule's retained mmCIF document.
   //
   //! Returns an empty vector for a molecule that has no document (PDB, SHELX,
   //! built de novo) -- the caller should fall back to the mmdb REMARK view,
   //! which is the right thing to show for a file that genuinely has REMARK
   //! cards and no categories.
   //!
   //! Categories come back in reading order: the prominent ones first (title,
   //! keywords, authors, citation, entity, experiment, refinement), then
   //! everything else in the order the file lists it. `_atom_site` and
   //! `_atom_site_anisotrop` are absent because the read path strips them from
   //! the retained document -- coordinates are not header information, and
   //! keeping them would put tens of thousands of rows in this window.
   std::vector<header_category_t>
   mmcif_header_view(const mmcif_document_t *doc);

   //! The display label for a tag, e.g. "_refine.ls_d_res_high" ->
   //! "Resolution range high (A)". Falls back to a prettified form of the tag
   //! itself, never to nothing. Exposed because the same wording should be used
   //! anywhere else a tag is shown to a user.
   std::string mmcif_tag_label(const std::string &category,
                               const std::string &tag_without_category);

   //! The display label for a category, e.g. "_refine" -> "Refinement".
   //! Empty when we have no better word than the category name itself.
   std::string mmcif_category_label(const std::string &category);

   //! Is this a per-atom / per-residue bookkeeping table rather than header
   //! information? See header_category_t::body for why this is an exclusion.
   //!
   //! Note the coordinates themselves never reach here at all: the read path
   //! strips _atom_site and _atom_site_anisotrop from the retained document.
   //! There is no equivalent of PDB's REMARK in mmCIF -- nothing in the file
   //! marks a category as header, and the dictionary's _category_group
   //! mechanism is vestigial (31 of ~600 categories declare one in
   //! mmcif_pdbx_v50.dic, and atom_group has a single member). So this
   //! boundary is ours to draw, and drawing it by exclusion is what keeps it
   //! from going stale.
   bool mmcif_category_is_body(const std::string &category);

   //! Which family does this category belong to? Prefix-matched, longest wins.
   header_family_t mmcif_category_family(const std::string &category);

}

#endif // MMCIF_HEADER_VIEW_HH

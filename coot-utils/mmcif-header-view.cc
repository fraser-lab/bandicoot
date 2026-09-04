/* coot-utils/mmcif-header-view.cc
 *
 * See mmcif-header-view.hh for why the browser is driven by the file's own
 * categories rather than by a list of things we know about.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#include "mmcif-header-view.hh"
#include "mmcif-document.hh"

#include <gemmi/cifdoc.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>

namespace {

   // ------------------------------------------------------------- categories

   // Categories worth opening on sight, in the order a reader wants them.
   // Everything NOT in this list still appears -- just after these, in file
   // order, collapsed. This is the only curation that affects what is shown
   // first; it never affects what is shown.
   const char *prominent_categories[] = {
      "_struct",                        // the title, first
      "_struct_keywords",
      "_audit_author",
      "_citation",
      "_citation_author",
      "_entity",                        // what is in the file
      "_exptl",
      "_refine",                        // the numbers people came for
      "_pdbx_heterogeneity_hierarchy",  // and the reason v0.2 exists
      "_pdbx_state_coexistence",
      nullptr
   };

   // The STRUCTURE BODY: per-atom, per-residue and per-component tables. These
   // are what makes an mmCIF a coordinate file rather than a header, and they
   // are also nearly all of its bulk -- in 3K0N.cif, _chem_comp_atom (390
   // rows), _chem_comp_bond (375), _pdbx_poly_seq_scheme (165),
   // _entity_poly_seq (165) and _pdbx_nonpoly_scheme (132) together dwarf
   // everything a reader opened the window for.
   //
   // mmCIF has NO equivalent of PDB's REMARK marker, and the dictionary cannot
   // supply the boundary either: _category_group exists but is vestigial (31
   // of ~600 categories declare one; atom_group has a single member). So the
   // line is drawn here, by hand, and drawn as an EXCLUSION so that a category
   // nobody has heard of counts as header and appears. The GUI offers "show
   // all" so this is a default, not a censor.
   //
   // WARNING: A BY-NAME LIST IS THE WEAK PART OF THIS DESIGN, and there is a
   // promising rule hiding in it (2026-08-17: "a solid idea and worth
   // revisiting when we touch header browser again"). Nearly every category
   // below has a row count that SCALES WITH THE MODEL -- per atom, per
   // residue, per shell, per revision -- while nearly everything shown has a
   // fixed handful of rows. That is detectable without naming anything, so it
   // would degrade sensibly on files annotated in ways nobody here has seen,
   // which is exactly where a name list fails. Not adopted yet: it needs
   // measuring against files where it might misfire (a 2-residue peptide, a
   // structure with one revision, a hierarchy table that is legitimately long).
   // Revisit when this window is next opened up.
   //
   // _atom_site and _atom_site_anisotrop are not listed because they never
   // arrive: the read path strips them from the retained document.
   const char *body_categories[] = {
      "_atom_type",                // scattering factors, one row per element
      "_atom_sites",               // fractionalisation matrix
      "_database_PDB_matrix",      // ditto, the PDB spelling
      "_chem_comp",                // the components present
      "_chem_comp_atom",           // ... and their embedded dictionary
      "_chem_comp_bond",
      "_chem_comp_angle",
      "_chem_comp_tor",
      "_chem_comp_tor_value",
      "_chem_comp_plane",
      "_chem_comp_plane_atom",
      "_entity_poly_seq",          // one row per residue of the construct
      "_pdbx_poly_seq_scheme",     // one row per residue, numbering cross-reference
      "_pdbx_nonpoly_scheme",
      "_pdbx_branch_scheme",
      "_struct_asym",              // one row per label_asym_id
      "_atom_site",                // belt and braces: the read path strips these
      "_atom_site_anisotrop",      // from the retained document, and the writer
                                   // no longer puts them back -- but if either
                                   // ever reaches here again it is coordinates,
                                   // not header, and the browser should say so
                                   // rather than showing 6000 rows.
      // Per-residue and per-atom ANNOTATION. Real header information in the PDB
      // sense (HELIX/SHEET/SITE/REMARK 465 all live in a PDB header), but one
      // row per residue or atom, so in a browser they are bulk rather than
      // summary. Hidden 2026-08-17 after reading the real
      // thing; the "show all" checkbox reaches them.
      "_struct_conf",              // helices
      "_struct_conf_type",
      "_struct_sheet",
      "_struct_sheet_order",
      "_struct_sheet_range",
      "_pdbx_struct_sheet_hbond",
      "_struct_site",              // binding sites
      "_struct_site_gen",          // ... and their residues
      "_pdbx_unobs_or_zero_occ_residues",
      "_pdbx_unobs_or_zero_occ_atoms",
      "_pdbx_distant_solvent_atoms",
      // DEPOSITION MACHINERY. True of the entry, not of the structure: which
      // dictionary version the file conforms to, when it was received, how many
      // times it has been revised and which categories each revision touched.
      // Hidden 2026-08-17 after reading the real thing.
      "_entry",
      "_audit_conform",
      "_pdbx_database_status",
      "_pdbx_audit_revision_history",
      "_pdbx_audit_revision_details",
      "_pdbx_audit_revision_group",
      "_pdbx_audit_revision_category",
      "_pdbx_audit_revision_item",
      // Cross-references and per-shell / per-restraint-class breakdowns: real
      // data, but a table of it rather than a fact about the structure.
      "_entity_name_com",
      "_struct_ref",
      "_struct_ref_seq",
      "_reflns_shell",
      "_refine_ls_restr",
      "_pdbx_refine",
      "_pdbx_struct_oper_list",
      "_pdbx_validate_torsion",
      "_pdbx_validate_rmsd_angle",
      nullptr
   };

   // Panel colour, chosen per FAMILY rather than per category, so a category
   // nobody has heard of still lands somewhere sensible. The GUI maps these to
   // actual colours; keeping the classification here means the wx rebuild
   // inherits it rather than re-deriving it.
   struct family_pair { const char *prefix; coot::header_family_t family; };

   // Matched as a PREFIX, longest first, so _struct_conf lands in annotation
   // while _struct itself lands in bibliography.
   const family_pair family_prefixes[] = {
      { "_pdbx_heterogeneity",  coot::header_family_t::heterogeneity },
      { "_pdbx_state_coexist",  coot::header_family_t::heterogeneity },
      { "_struct_conf",         coot::header_family_t::annotation },
      { "_struct_sheet",        coot::header_family_t::annotation },
      { "_pdbx_struct_sheet",   coot::header_family_t::annotation },
      { "_struct_conn",         coot::header_family_t::annotation },
      { "_struct_site",         coot::header_family_t::annotation },
      { "_struct_mon_prot_cis", coot::header_family_t::annotation },
      { "_pdbx_validate",       coot::header_family_t::annotation },
      { "_pdbx_unobs",          coot::header_family_t::annotation },
      { "_pdbx_distant_solvent", coot::header_family_t::annotation },
      { "_struct_ref",          coot::header_family_t::entity },
      { "_struct_biol",         coot::header_family_t::annotation },
      { "_struct_ncs",          coot::header_family_t::symmetry },
      { "_struct_asym",         coot::header_family_t::entity },
      { "_struct_keywords",     coot::header_family_t::bibliography },
      { "_struct",              coot::header_family_t::bibliography },
      { "_entry",               coot::header_family_t::bibliography },
      { "_audit",               coot::header_family_t::bibliography },
      { "_citation",            coot::header_family_t::bibliography },
      { "_database",            coot::header_family_t::bibliography },
      { "_pdbx_database",       coot::header_family_t::bibliography },
      { "_pdbx_audit",          coot::header_family_t::bibliography },
      { "_entity",              coot::header_family_t::entity },
      { "_pdbx_entity",         coot::header_family_t::entity },
      { "_chem_comp",           coot::header_family_t::entity },
      { "_pdbx_poly_seq",       coot::header_family_t::entity },
      { "_pdbx_nonpoly",        coot::header_family_t::entity },
      { "_pdbx_branch",         coot::header_family_t::entity },
      { "_exptl",               coot::header_family_t::experiment },
      { "_diffrn",              coot::header_family_t::experiment },
      { "_reflns",              coot::header_family_t::experiment },
      { "_phasing",             coot::header_family_t::experiment },
      { "_em_",                 coot::header_family_t::experiment },
      { "_pdbx_reflns",         coot::header_family_t::experiment },
      { "_refine",              coot::header_family_t::refinement },
      { "_pdbx_refine",         coot::header_family_t::refinement },
      { "_software",            coot::header_family_t::refinement },
      { "_computing",           coot::header_family_t::refinement },
      { "_pdbx_initial_refinement", coot::header_family_t::refinement },
      { "_cell",                coot::header_family_t::symmetry },
      { "_symmetry",            coot::header_family_t::symmetry },
      { "_atom_sites",          coot::header_family_t::symmetry },
      { "_database_PDB_matrix", coot::header_family_t::symmetry },
      { "_pdbx_struct_assembly", coot::header_family_t::symmetry },
      { "_pdbx_struct_oper",    coot::header_family_t::symmetry },
      { "_atom_type",           coot::header_family_t::other },
      { nullptr, coot::header_family_t::other }
   };

   struct label_pair { const char *key; const char *label; };

   const label_pair category_labels[] = {
      { "_struct",                      "Structure" },
      { "_struct_keywords",             "Keywords" },
      { "_entry",                       "Entry" },
      { "_audit_author",                "Authors" },
      { "_citation",                    "Citation" },
      { "_citation_author",             "Citation authors" },
      { "_entity",                      "Molecular entities" },
      { "_entity_name_com",             "Entity synonyms" },
      { "_entity_poly",                 "Polymer entities" },
      { "_entity_poly_seq",             "Polymer sequence" },
      { "_entity_src_gen",              "Source (genetically manipulated)" },
      { "_entity_src_nat",              "Source (natural)" },
      { "_exptl",                       "Experiment" },
      { "_exptl_crystal",               "Crystal" },
      { "_exptl_crystal_grow",          "Crystallisation" },
      { "_diffrn",                      "Diffraction experiment" },
      { "_diffrn_detector",             "Detector" },
      { "_diffrn_radiation",            "Radiation" },
      { "_diffrn_radiation_wavelength", "Wavelength" },
      { "_diffrn_source",               "X-ray source" },
      { "_reflns",                      "Reflections" },
      { "_reflns_shell",                "Reflections by shell" },
      { "_refine",                      "Refinement" },
      { "_refine_hist",                 "Refinement: model contents" },
      { "_refine_ls_restr",             "Refinement: restraint r.m.s.d." },
      { "_refine_ls_shell",             "Refinement by shell" },
      { "_refine_analyze",              "Refinement analysis" },
      { "_software",                    "Software" },
      { "_computing",                   "Computing" },
      { "_cell",                        "Unit cell" },
      { "_symmetry",                    "Space group" },
      { "_atom_type",                   "Atom types and scattering factors" },
      { "_atom_sites",                  "Fractionalisation matrix" },
      { "_struct_conf",                 "Secondary structure: helices" },
      { "_struct_conf_type",            "Secondary structure: types" },
      { "_struct_sheet",                "Secondary structure: sheets" },
      { "_struct_sheet_order",          "Sheet strand order" },
      { "_struct_sheet_range",          "Sheet strands" },
      { "_struct_conn",                 "Connections (links, disulphides, metal coordination)" },
      { "_struct_conn_type",            "Connection types" },
      { "_struct_ncs_oper",             "NCS operators" },
      { "_struct_asym",                 "Asymmetric units" },
      { "_struct_biol",                 "Biological assembly (legacy)" },
      { "_struct_mon_prot_cis",         "Cis peptides" },
      { "_struct_site",                 "Sites" },
      { "_struct_site_gen",             "Site contents" },
      { "_pdbx_struct_assembly",        "Biological assembly" },
      { "_pdbx_struct_assembly_gen",    "Assembly generation" },
      { "_pdbx_struct_oper_list",       "Assembly operators" },
      { "_pdbx_database_status",        "Deposition status" },
      { "_pdbx_database_related",       "Related entries" },
      { "_pdbx_audit_revision_history", "Revision history" },
      { "_pdbx_poly_seq_scheme",        "Sequence numbering scheme" },
      { "_pdbx_nonpoly_scheme",         "Non-polymer numbering scheme" },
      { "_pdbx_unobs_or_zero_occ_residues", "Unobserved / zero-occupancy residues" },
      { "_pdbx_unobs_or_zero_occ_atoms",    "Unobserved / zero-occupancy atoms" },
      { "_pdbx_validate_close_contact", "Validation: close contacts" },
      { "_pdbx_validate_torsion",       "Validation: torsion outliers" },
      { "_pdbx_validate_rmsd_angle",    "Validation: angle outliers" },
      { "_pdbx_validate_rmsd_bond",     "Validation: bond outliers" },
      { "_pdbx_validate_symm_contact",  "Validation: symmetry contacts" },
      { "_chem_comp",                   "Chemical components" },
      { "_chem_comp_atom",              "Component atoms" },
      { "_chem_comp_bond",              "Component bonds" },
      { "_database_2",                  "Database references" },
      // wwPDB's holding category for PDB REMARK text that has no typed mmCIF
      // home. Shown, not hidden (2026-08-17): it is where the PDB -> mmCIF
      // conversion parks REMARK 3, 200, 280 and the rest, and hiding it would
      // preserve them in the file while making them invisible in the one window
      // anyone would look for them.
      { "_pdbx_database_remark",        "PDB REMARK records" },
      { "_audit_conform",               "Dictionary conformance" },
      { "_struct_ref",                  "Sequence database references" },
      { "_struct_ref_seq",              "Sequence database alignment" },
      { "_struct_ref_seq_dif",          "Sequence database differences" },
      { "_pdbx_struct_sheet_hbond",     "Sheet hydrogen bonding" },
      { "_pdbx_audit_revision_details", "Revision details" },
      { "_pdbx_audit_revision_group",   "Revision groups" },
      { "_pdbx_audit_revision_category","Revised categories" },
      { "_pdbx_audit_revision_item",    "Revised items" },
      { "_pdbx_entity_nonpoly",         "Non-polymer entities" },
      { "_pdbx_initial_refinement_model", "Initial refinement model" },
      { "_pdbx_entry_details",          "Entry details" },
      { "_pdbx_refine_tls",             "TLS groups" },
      { "_pdbx_refine_tls_group",       "TLS group selections" },
      { "_pdbx_reflns_twin",            "Twinning" },
      { "_phasing_MAD",                 "Phasing (MAD)" },
      { "_phasing_MIR",                 "Phasing (MIR)" },
      { "_em_3d_fitting",               "EM fitting" },
      { "_em_entity_assembly",          "EM assembly" },
      { "_em_imaging",                  "EM imaging" },
      { "_em_experiment",               "EM experiment" },
      { "_pdbx_heterogeneity_hierarchy", "Heterogeneity hierarchy" },
      { "_pdbx_state_coexistence",      "State coexistence" },
      { "_em_software",                 "EM software" },
      { "_em_3d_reconstruction",        "EM reconstruction" },
      { nullptr, nullptr }
   };

   // ------------------------------------------------------------------ tags
   //
   // Only tags with a familiar PDB-format wording are listed. Everything else
   // gets the prettifier, which is deliberately conservative -- see below.
   const label_pair tag_labels[] = {
      // _refine -- the numbers people open this window for
      { "_refine.ls_d_res_high",              "Resolution range high (A)" },
      { "_refine.ls_d_res_low",               "Resolution range low (A)" },
      { "_refine.ls_number_reflns_obs",       "Number of reflections" },
      { "_refine.ls_number_reflns_R_free",    "Free R value test set count" },
      { "_refine.ls_percent_reflns_obs",      "Completeness for range (%)" },
      { "_refine.ls_percent_reflns_R_free",   "Free R value test set size (%)" },
      { "_refine.ls_R_factor_obs",            "R value (observed)" },
      { "_refine.ls_R_factor_all",            "R value (all reflections)" },
      { "_refine.ls_R_factor_R_work",         "R value (working set)" },
      { "_refine.ls_R_factor_R_free",         "Free R value" },
      { "_refine.B_iso_mean",                 "Mean B value (overall, A^2)" },
      { "_refine.solvent_model_details",      "Bulk solvent model" },
      { "_refine.solvent_model_param_ksol",   "Bulk solvent K_sol" },
      { "_refine.solvent_model_param_bsol",   "Bulk solvent B_sol" },
      { "_refine.pdbx_solvent_vdw_probe_radii",  "Solvent VDW probe radius" },
      { "_refine.pdbx_solvent_shrinkage_radii",  "Solvent shrinkage radius" },
      { "_refine.pdbx_ls_cross_valid_method", "Cross-validation method" },
      { "_refine.pdbx_method_to_determine_struct", "Method to determine structure" },
      { "_refine.pdbx_starting_model",        "Starting model" },
      { "_refine.pdbx_stereochemistry_target_values", "Stereochemistry target values" },
      { "_refine.pdbx_overall_phase_error",   "Phase error (degrees)" },
      { "_refine.overall_SU_ML",              "Coordinate error (maximum likelihood, A)" },
      { "_refine.pdbx_isotropic_thermal_model", "Isotropic thermal model" },
      { "_refine.pdbx_refine_id",             "Refinement of" },
      { "_refine.details",                    "Details" },
      // _refine_hist
      { "_refine_hist.number_atoms_solvent",  "Solvent atoms" },
      { "_refine_hist.number_atoms_total",    "Total atoms" },
      { "_refine_hist.pdbx_number_atoms_protein", "Protein atoms" },
      { "_refine_hist.pdbx_number_atoms_nucleic_acid", "Nucleic acid atoms" },
      { "_refine_hist.pdbx_number_atoms_ligand",  "Ligand atoms" },
      // per-shell tables: label these explicitly, or the prettifier turns
      // d_res_high into "D res high" and a search for "resolution" misses the
      // one table that is entirely about resolution. Found by filtering.
      { "_refine_ls_shell.d_res_high",        "Resolution high (A)" },
      { "_refine_ls_shell.d_res_low",         "Resolution low (A)" },
      { "_refine_ls_shell.number_reflns_all", "Reflections (all)" },
      { "_refine_ls_shell.number_reflns_obs", "Reflections (observed)" },
      { "_refine_ls_shell.number_reflns_R_work", "Reflections (working set)" },
      { "_refine_ls_shell.number_reflns_R_free", "Reflections (free set)" },
      { "_refine_ls_shell.percent_reflns_obs", "Completeness (%)" },
      { "_refine_ls_shell.R_factor_R_work",   "R value (working set)" },
      { "_refine_ls_shell.R_factor_R_free",   "Free R value" },
      { "_refine_ls_shell.R_factor_all",      "R value (all)" },
      { "_reflns_shell.d_res_high",           "Resolution high (A)" },
      { "_reflns_shell.d_res_low",            "Resolution low (A)" },
      { "_reflns_shell.percent_possible_all", "Completeness (%)" },
      { "_reflns_shell.Rmerge_I_obs",         "R-merge" },
      { "_reflns_shell.meanI_over_sigI_obs",  "<I/sigma(I)>" },
      { "_reflns_shell.pdbx_redundancy",      "Redundancy" },
      { "_refine.ls_d_res_high",              "Resolution range high (A)" },
      // _reflns
      { "_reflns.d_resolution_high",          "Resolution high (A)" },
      { "_reflns.d_resolution_low",           "Resolution low (A)" },
      { "_reflns.number_obs",                 "Number of observed reflections" },
      { "_reflns.percent_possible_obs",       "Completeness (%)" },
      { "_reflns.pdbx_Rmerge_I_obs",          "R-merge" },
      { "_reflns.pdbx_Rsym_value",            "R-sym" },
      { "_reflns.pdbx_netI_over_sigmaI",      "<I/sigma(I)>" },
      { "_reflns.pdbx_redundancy",            "Redundancy" },
      { "_reflns.B_iso_Wilson_estimate",      "Wilson B (A^2)" },
      // _cell / _symmetry
      { "_cell.length_a",                     "a (A)" },
      { "_cell.length_b",                     "b (A)" },
      { "_cell.length_c",                     "c (A)" },
      { "_cell.angle_alpha",                  "alpha (degrees)" },
      { "_cell.angle_beta",                   "beta (degrees)" },
      { "_cell.angle_gamma",                  "gamma (degrees)" },
      { "_cell.Z_PDB",                        "Z" },
      { "_symmetry.space_group_name_H-M",     "Space group (Hermann-Mauguin)" },
      { "_symmetry.Int_Tables_number",        "International Tables number" },
      // bibliography
      { "_struct.title",                      "Title" },
      { "_struct.pdbx_descriptor",            "Descriptor" },
      { "_struct_keywords.pdbx_keywords",     "Classification" },
      { "_struct_keywords.text",              "Keywords" },
      { "_audit_author.name",                 "Author" },
      { "_audit_author.pdbx_ordinal",         "Order" },
      { "_citation.title",                    "Title" },
      { "_citation.journal_abbrev",           "Journal" },
      { "_citation.journal_volume",           "Volume" },
      { "_citation.page_first",               "First page" },
      { "_citation.page_last",                "Last page" },
      { "_citation.year",                     "Year" },
      { "_citation.pdbx_database_id_PubMed",  "PubMed ID" },
      { "_citation.pdbx_database_id_DOI",     "DOI" },
      { "_citation.journal_id_ISSN",          "ISSN" },
      { "_citation_author.name",              "Author" },
      // entities and experiment
      { "_entity.pdbx_description",           "Description" },
      { "_entity.type",                       "Type" },
      { "_entity.src_method",                 "Source method" },
      { "_entity.formula_weight",             "Formula weight" },
      { "_entity.pdbx_number_of_molecules",   "Copies" },
      { "_entity.pdbx_ec",                    "EC number" },
      { "_entity.pdbx_mutation",              "Mutation" },
      { "_entity.pdbx_fragment",              "Fragment" },
      { "_exptl.method",                      "Method" },
      { "_exptl.crystals_number",             "Number of crystals" },
      { "_exptl_crystal.density_Matthews",    "Matthews coefficient" },
      { "_exptl_crystal.density_percent_sol", "Solvent content (%)" },
      { "_exptl_crystal_grow.method",         "Crystallisation method" },
      { "_exptl_crystal_grow.pH",             "pH" },
      { "_exptl_crystal_grow.temp",           "Temperature (K)" },
      { "_exptl_crystal_grow.pdbx_details",   "Conditions" },
      { "_diffrn.ambient_temp",               "Data collection temperature (K)" },
      { "_diffrn_source.pdbx_synchrotron_site", "Synchrotron" },
      { "_diffrn_source.pdbx_synchrotron_beamline", "Beamline" },
      { "_diffrn_source.pdbx_wavelength",     "Wavelength (A)" },
      { "_diffrn_detector.detector",          "Detector" },
      { "_diffrn_detector.type",              "Detector type" },
      { "_software.name",                     "Name" },
      { "_software.version",                  "Version" },
      { "_software.classification",           "Used for" },
      { "_pdbx_database_status.recvd_initial_deposition_date", "Deposited" },
      // secondary structure and connectivity
      { "_struct_conf.pdbx_PDB_helix_id",     "Helix" },
      { "_struct_conf.pdbx_PDB_helix_class",  "Helix class" },
      { "_struct_conf.pdbx_PDB_helix_length", "Length" },
      { "_struct_conn.conn_type_id",          "Type" },
      { "_struct_conn.pdbx_dist_value",       "Distance (A)" },
      { nullptr, nullptr }
   };

   // Acronyms and abbreviations that should not be sentence-cased into
   // nonsense. Applied to whole words only.
   const char *upper_words[] = {
      "id", "pdb", "pdbx", "ndb", "cif", "ec", "doi", "issn", "pubmed", "rcsb",
      "ncs", "tls", "ls", "esd", "rms", "rmsd", "b", "r", "em", "nmr", "sg",
      "asu", "adp", "wwpdb", "iso", "dna", "rna", nullptr
   };

   std::string lower(std::string s) {
      for (char &c : s) c = std::tolower(static_cast<unsigned char>(c));
      return s;
   }

   const char *lookup(const label_pair *table, const std::string &key) {
      for (const label_pair *p = table; p->key; p++)
         if (key == p->key) return p->label;
      return nullptr;
   }

   // Mechanical fallback: "pdbx_solvent_ion_probe_radii" -> "PDBx solvent ion
   // probe radii". Deliberately does no more than split and case: inventing
   // longer wording ("least-squares" for "ls") would be a guess presented as
   // knowledge, and the raw tag is displayed beside it anyway.
   std::string prettify(const std::string &tag) {
      std::string out;
      std::string word;
      bool first_word = true;
      auto flush = [&]() {
         if (word.empty()) return;
         std::string lw = lower(word);
         bool upper = false;
         for (const char **u = upper_words; *u; u++)
            if (lw == *u) { upper = true; break; }
         if (! out.empty()) out += " ";
         if (upper) {
            std::string w = word;
            for (char &c : w) c = std::toupper(static_cast<unsigned char>(c));
            if (lw == "pdbx") w = "PDBx";
            out += w;
         } else if (first_word) {
            std::string w = word;
            w[0] = std::toupper(static_cast<unsigned char>(w[0]));
            out += w;
         } else {
            out += word;
         }
         first_word = false;
         word.clear();
      };
      for (char c : tag) {
         if (c == '_') flush();
         else word += c;
      }
      flush();
      return out.empty() ? tag : out;
   }

}  // anonymous namespace


coot::header_family_t coot::mmcif_category_family(const std::string &category) {

   // Longest prefix wins, so the order of the table above does not have to be
   // maintained by hand.
   coot::header_family_t best = coot::header_family_t::other;
   size_t best_len = 0;
   for (const family_pair *p = family_prefixes; p->prefix; p++) {
      size_t n = std::strlen(p->prefix);
      if (category.compare(0, n, p->prefix) == 0 && n > best_len) {
         best = p->family;
         best_len = n;
      }
   }
   return best;
}


bool coot::mmcif_category_is_body(const std::string &category) {
   for (const char **p = body_categories; *p; p++)
      if (category == *p) return true;
   return false;
}


std::string coot::mmcif_category_label(const std::string &category) {
   const char *l = lookup(category_labels, category);
   return l ? std::string(l) : std::string();
}


std::string coot::mmcif_tag_label(const std::string &category,
                                  const std::string &tag_without_category) {
   const char *l = lookup(tag_labels, category + "." + tag_without_category);
   return l ? std::string(l) : prettify(tag_without_category);
}


std::vector<coot::header_category_t>
coot::mmcif_header_view(const coot::mmcif_document_t *doc) {

   std::vector<header_category_t> out;
   if (! doc) return out;

   const gemmi::cif::Block *block = nullptr;
   // coordinate_block() is blocks[0]; later blocks (a restraint dictionary in a
   // phenix file, say) are a different subject and are appended after it, so
   // the browser shows the whole document rather than only its first block.
   if (doc->doc.blocks.empty()) return out;

   for (size_t ib = 0; ib < doc->doc.blocks.size(); ib++) {

      block = &doc->doc.blocks[ib];
      std::string block_prefix;
      if (doc->doc.blocks.size() > 1 && ib > 0)
         block_prefix = "[" + block->name + "] ";

      // Walk items in file order, gathering pairs of the same category
      // together: mmCIF is free to write _cell.length_a and _cell.length_b as
      // separate items, and they belong in one panel.
      std::map<std::string, size_t> pair_index;   // category -> index in out

      for (const gemmi::cif::Item &item : block->items) {

         if (item.type == gemmi::cif::ItemType::Pair) {

            const std::string &tag = item.pair[0];
            size_t dot = tag.find('.');
            if (dot == std::string::npos) continue;
            std::string cat = tag.substr(0, dot);
            std::string name = tag.substr(dot + 1);

            std::string key = block_prefix + cat;
            std::map<std::string, size_t>::iterator it = pair_index.find(key);
            if (it == pair_index.end()) {
               header_category_t c;
               c.name = key;
               c.label = mmcif_category_label(cat);
               c.is_loop = false;
               c.rows.push_back(std::vector<std::string>());
               pair_index[key] = out.size();
               out.push_back(c);
               it = pair_index.find(key);
            }
            header_category_t &c = out[it->second];
            c.column_tags.push_back(name);
            c.column_labels.push_back(mmcif_tag_label(cat, name));
            c.rows[0].push_back(gemmi::cif::as_string(item.pair[1]));
            c.n_rows = c.column_tags.size();

         } else if (item.type == gemmi::cif::ItemType::Loop) {

            const gemmi::cif::Loop &loop = item.loop;
            if (loop.tags.empty()) continue;
            size_t dot = loop.tags[0].find('.');
            if (dot == std::string::npos) continue;
            std::string cat = loop.tags[0].substr(0, dot);

            header_category_t c;
            c.name = block_prefix + cat;
            c.label = mmcif_category_label(cat);
            c.is_loop = true;
            for (const std::string &t : loop.tags) {
               std::string n = t.substr(t.find('.') + 1);
               c.column_tags.push_back(n);
               c.column_labels.push_back(mmcif_tag_label(cat, n));
            }
            c.n_rows = loop.length();
            for (size_t r = 0; r < loop.length(); r++) {
               std::vector<std::string> row;
               row.reserve(loop.tags.size());
               for (size_t col = 0; col < loop.tags.size(); col++)
                  row.push_back(gemmi::cif::as_string(loop.val(r, col)));
               c.rows.push_back(row);
            }
            out.push_back(c);
         }
      }
   }

   // Float the prominent ones to the front, in the order prominent_categories
   // lists them rather than the order the file happens to use -- a reader wants
   // the title before the authors before the refinement statistics, and a
   // deposition writes them in neither order. Everything else keeps FILE order,
   // which is the only ordering we can claim to know is meaningful.
   std::map<std::string, int> rank;
   for (const char **p = prominent_categories; *p; p++)
      rank[*p] = static_cast<int>(rank.size());

   for (header_category_t &c : out) {
      std::string bare = c.name;
      size_t close = bare.find("] ");
      if (close != std::string::npos) bare = bare.substr(close + 2);
      std::map<std::string, int>::const_iterator it = rank.find(bare);
      if (it != rank.end()) c.prominent = true;
      c.body = mmcif_category_is_body(bare);
      c.family = mmcif_category_family(bare);
   }
   std::stable_sort(out.begin(), out.end(),
                    [&rank](const header_category_t &a, const header_category_t &b) {
                       auto key = [&rank](const header_category_t &c) {
                          std::string bare = c.name;
                          size_t close = bare.find("] ");
                          if (close != std::string::npos) bare = bare.substr(close + 2);
                          std::map<std::string, int>::const_iterator i = rank.find(bare);
                          // everything unlisted sorts after everything listed,
                          // and ties keep file order because the sort is stable
                          return i == rank.end() ? 1000000 : i->second;
                       };
                       return key(a) < key(b);
                    });

   return out;
}

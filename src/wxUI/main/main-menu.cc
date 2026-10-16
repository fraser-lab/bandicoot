// src/wxUI/main/main-menu.cc

#include "wxUI/main/main-menu.hh"
#include "wxUI/assets/icons.hh"
#ifdef __WXOSX__
#include "wxUI/main/mac-menu.hh"
#endif

#include <wx/menu.h>

#include <map>
#include <memory>
#include <utility>

namespace wxui {

namespace {

typedef MenuEntry::Kind Kind;

MenuEntry item(const char *label, const char *action, const char *needs = "") {
   MenuEntry e;
   e.kind = Kind::Item; e.label = label; e.action = action; e.needs = needs;
   return e;
}

MenuEntry radio(const char *label, const char *action, bool checked) {
   MenuEntry e = item(label, action);
   e.kind = Kind::Radio; e.checked = checked;
   return e;
}

MenuEntry separator() {
   MenuEntry e;
   e.kind = Kind::Separator;
   return e;
}

MenuEntry submenu(const char *label, std::vector<MenuEntry> items) {
   MenuEntry e;
   e.kind = Kind::Submenu; e.label = label; e.items = std::move(items);
   return e;
}

// filled with the current molecules when the menu is wired to the core
MenuEntry molecule_submenu(const char *label, const char *list) {
   MenuEntry e;
   e.kind = Kind::MoleculeSubmenu; e.label = label; e.action = list;
   return e;
}

// wx reads '&' in a label as a mnemonic marker
wxString menu_label(const std::string &label) {
   wxString s = wxString::FromUTF8(label);
   s.Replace("&", "&&");
   return s;
}

// Coot 0.9's menu icons: items by action, submenus by label
const std::map<std::string, const char *> item_icons = {
   { "on_about1_activate", "stock-menu-about.png" },
   { "on_hints1_activate", "stock-menu-dialog-info.png" },
   { "on_fps1_activate", "appointment-new.png" },
   { "on_ncs_maps1_activate", "calculate-ghosts.svg" },
   { "on_run_script1_activate", "stock-menu-execute.png" },
   { "on_anisotropic_atoms1_activate", "aniso-atom.svg" },
   { "on_antialiasing1_activate", "add-peptide-1.svg" },
   { "on_show_symmetry1_activate", "cell+symm.svg" },
   { "on_centre_atom_label1_activate", "stock-menu-justify-center.png" },
   { "on_crosshairs1_activate", "crosshairs.svg" },
   { "on_map_and_mol_control1_activate", "display-manager.svg" },
   { "on_generic_display_objects1_activate", "generic-display-objects.svg" },
   { "on_go_to_atom1_activate", "go-to-atom.svg" },
   { "on_ncs_ghost_control1_activate", "ghosts.svg" },
   { "on_spin_view_on_off1_activate", "spin-view.svg" },
   { "on_stereo1_activate", "stereo-view.svg" },
   { "on_undo_last_navigation1_activate", "stock-menu-undo.png" },
   { "on_bond_colours1_activate", "stock-menu-select-color.png" },
   { "on_bond_parameters1_activate", "stock-menu-preferences.png" },
   { "on_edit_change_chain_ids1_activate", "change-chain-id.svg" },
   { "on_edit_copy_fragment1_activate", "stock-menu-copy.png" },
   { "on_edit_copy_molecule1_activate", "stock-menu-copy.png" },
   { "on_font_size1_activate", "gtk-font.png" },
   { "on_density_size1_activate", "stock-menu-preferences.png" },
   { "on_edit_merge_molecules1_activate", "merge-molecules.svg" },
   { "on_pink_pointer_size1_activate", "pink-pointer.svg" },
   { "on_preferences1_activate", "stock-menu-preferences.png" },
   { "on_edit_replace_fragment1_activate", "stock-menu-refresh.png" },
   { "on_edit_renumber_residues1_activate", "renumber-residues.svg" },
   { "on_edit_replace_residue1_activate", "stock-menu-refresh.png" },
   { "on_residue_info2_activate", "residue-info.svg" },
   { "on_restraints1_activate", "radacina_handcuffs-small.svg" },
   { "on_skeleton_colour1_activate", "stock-menu-select-color.png" },
   { "on_auto_open_mtz_activate", "stock-menu-open.png" },
   { "on_close_molecule1_activate", "stock-menu-close.png" },
   { "on_exit1_activate", "stock-menu-quit.png" },
   { "on_get_pdb_and_map_using_eds1_activate", "connect-to-usf.svg" },
   { "on_get_pdb_using_code1_activate", "connect-to-oca.svg" },
   { "on_get_monomer1_activate", "connect-to-ccp4.svg" },
   { "on_import_cif_dictionary1_activate", "stock-menu-add.png" },
   { "on_open_coordinates1_activate", "stock-menu-open.png" },
   { "on_open_dataset1_activate", "stock-menu-open.png" },
   { "on_open_map1_activate", "stock-menu-open.png" },
   { "on_recover_session1_activate", "stock-menu-find-and-replace.png" },
   { "on_save_coordinates1_activate", "stock-menu-save.png" },
   { "on_save_state1_activate", "coot-save.png" },
   { "on_save_symmetry_coordinates1_activate", "coot-save-symm.png" },
   { "on_search_monomer_library1_activate", "stock-menu-index.png" },
   { "on_clear_atom_labels1_activate", "stock-menu-clear.png" },
   { "on_distances_and_angles1_activate", "stock-menu-unindent.png" },
   { "on_environment_distances1_activate", "env-distances.svg" },
   { "on_plane_distances1_activate", "plane-distances.svg" },
   { "on_pointer_distances1_activate", "pointer-distances.svg" },
   { "on_residue_info1_activate", "residue-info.svg" },
   { "on_ligand_builder1_activate", "go-to-ligand.svg" },
   { "on_other_modelling_tools1_activate", "gnome-run.png" },
   { "on_check_waters1_activate", "water-drop.svg" },
   { "on_incorrect_chiral_volumes1_activate", "chiral.svg" },
   { "on_difference_map_peaks1_activate", "diff-map.svg" },
   { "on_ramachandran_differences_plot1_activate", "kleywegt.svg" },
   { "on_unmodelled_blobs1_activate", "unknown-blob.svg" },
};

const std::map<std::string, const char *> submenu_icons = {
   { "Scripting...", "stock-menu-justify-left.png" },
   { "Screenshot", "panel-screenshot.png" },
   { "Background Colour...", "stock-menu-select-color.png" },
   { "Map Colour...", "stock-menu-select-color.png" },
   { "Skeleton Parameters", "stock-menu-preferences.png" },
   { "Sequence View", "sequence-view.svg" },
   { "Average Temp. fact. analysis", "temperature.svg" },
   { "Density fit analysis", "density-fit.svg" },
   { "Geometry analysis", "geom.svg" },
   { "GLN and ASN B-factor Outliers", "gln-asn-b-factors.svg" },
   { "Peptide omega analysis", "peptide-omega.svg" },
   { "Probe clashes", "probe-clash.svg" },
   { "Ramachandran Plot", "ramachandran.png" },
   { "Rotamer analysis", "rotamers.svg" },
   { "Temp. fact. variance analysis", "temperature.svg" },
};

wxBitmapBundle menu_icon(const std::map<std::string, const char *> &table, const std::string &key) {
   auto it = table.find(key);
   return it == table.end() ? wxBitmapBundle() : icon(it->second, wxSize(16, 16));
}

// a submenu with its icon, which has to be set before it is appended
void append_submenu(wxMenu *menu, wxMenu *sub, const std::string &label) {
   wxMenuItem *mi = new wxMenuItem(menu, wxID_ANY, menu_label(label), wxEmptyString,
                                   wxITEM_NORMAL, sub);
   wxBitmapBundle b = menu_icon(submenu_icons, label);
   if (b.IsOk()) mi->SetBitmap(b);
   menu->Append(mi);
}

struct Selection {
   std::string action, path;
   int imol = -1;  // a molecule submenu's molecule
};

void fill(wxMenu *menu, const std::vector<MenuEntry> &entries, const std::string &path,
          const MainMenuHooks &hooks, std::shared_ptr<std::map<int, Selection>> ids) {

   for (const MenuEntry &e : entries) {
      if (! e.needs.empty() && hooks.has_capability && ! hooks.has_capability(e.needs))
         continue;
      std::string here = path.empty() ? e.label : path + " > " + e.label;
      switch (e.kind) {
      case Kind::Separator:
         menu->AppendSeparator();
         break;
      case Kind::Submenu: {
         wxMenu *sub = new wxMenu;
         fill(sub, e.items, here, hooks, ids);
         append_submenu(menu, sub, e.label);
         break;
      }
      case Kind::MoleculeSubmenu: {
         wxMenu *sub = new wxMenu;
         sub->Append(wxID_ANY, "No molecules")->Enable(false);
         append_submenu(menu, sub, e.label);
         // filled with the molecules loaded when it opens
         std::string list = e.action;
         auto molecules = hooks.molecules;
         if (molecules && hooks.has_molecule_list && hooks.has_molecule_list(list)) {
            sub->Bind(wxEVT_MENU_OPEN, [sub, list, here, molecules, ids](wxMenuEvent &ev) {
               ev.Skip();
               if (ev.GetMenu() != sub) return;
               std::vector<std::pair<int, std::string>> mols = molecules(list);
               while (sub->GetMenuItemCount() > 0) {
                  wxMenuItem *old = sub->FindItemByPosition(0);
                  ids->erase(old->GetId());
                  sub->Destroy(old);
               }
               if (mols.empty())
                  sub->Append(wxID_ANY, "No molecules")->Enable(false);
               for (const auto &m : mols) {
                  wxMenuItem *mi = sub->Append(wxID_ANY, wxString::FromUTF8(m.second));
                  Selection sel{ list, here + " > " + m.second, m.first };
                  (*ids)[mi->GetId()] = sel;
               }
            });
         }
         break;
      }
      case Kind::Item:
      case Kind::Check:
      case Kind::Radio: {
         wxItemKind kind = e.kind == Kind::Check ? wxITEM_CHECK
                         : e.kind == Kind::Radio ? wxITEM_RADIO : wxITEM_NORMAL;
         wxMenuItem *mi = new wxMenuItem(menu, wxID_ANY, menu_label(e.label), wxEmptyString, kind);
         if (kind == wxITEM_NORMAL) {
            wxBitmapBundle b = menu_icon(item_icons, e.action);
            if (b.IsOk()) mi->SetBitmap(b);
         }
         menu->Append(mi);
         if (kind != wxITEM_NORMAL) mi->Check(e.checked);
         (*ids)[mi->GetId()] = Selection{ e.action, here };
         break;
      }
      }
   }
}

const std::vector<MenuEntry> entries = {
      submenu("File", {
         item("Open Coordinates...", "on_open_coordinates1_activate"),
         item("Auto Open MTZ...", "on_auto_open_mtz_activate"),
         item("Open MTZ, mmCIF, fcf or phs...", "on_open_dataset1_activate"),
         item("Open Map...", "on_open_map1_activate"),
         item("Import CIF dictionary...", "on_import_cif_dictionary1_activate"),
         separator(),
         item("Get Monomer...", "on_get_monomer1_activate"),
         item("Search Monomer Library...", "on_search_monomer_library1_activate"),
         item("Fetch PDB using Accession Code...", "on_get_pdb_using_code1_activate"),
         item("Fetch PDB & Map using EDS...", "on_get_pdb_and_map_using_eds1_activate"),
         item("Fetch PDB & Map using PDB-REDO...", "on_fetch_pdb_and_map_using_pdbredo1_activate"),
         item("Fetch Corresponding AlphaFold Model & Superpose", "on_fetch_corresponding_alphafold_model_and_superpose1_activate"),
         item("Fetch AlphaFold Model using Uniprot ID", "on_fetch_alphafold_model_using_uniprot_id1_activate"),
         separator(),
         item("Save Coordinates...", "on_save_coordinates1_activate"),
         item("Save Symmetry Coordinates...", "on_save_symmetry_coordinates1_activate"),
         item("Save State...", "on_save_state1_activate"),
         item("Recover Session...", "on_recover_session1_activate"),
         separator(),
         item("Export Map...", "on_export_map1_activate"),
         item("Export Map Fragment...", "on_export_map_fragment1_activate"),
         separator(),
         item("Delete Molecules and Maps...", "on_close_molecule1_activate"),
         separator(),
         item("Exit", "on_exit1_activate"),
      }),
      submenu("Edit", {
         item("Bond Colours...", "on_bond_colours1_activate"),
         item("Bond Parameters...", "on_bond_parameters1_activate"),
         item("Change Chain IDs...", "on_edit_change_chain_ids1_activate"),
         item("Copy Molecule...", "on_edit_copy_molecule1_activate"),
         item("Copy Fragment...", "on_edit_copy_fragment1_activate"),
         molecule_submenu("Map Colour...", "map_colour1"),
         item("Map Parameters...", "on_density_size1_activate"),
         item("Merge Molecules...", "on_edit_merge_molecules1_activate"),
         item("Replace Residue...", "on_edit_replace_residue1_activate"),
         item("Replace Fragment...", "on_edit_replace_fragment1_activate"),
         item("Renumber Residues...", "on_edit_renumber_residues1_activate"),
         item("Restraints...", "on_restraints1_activate"),
         submenu("Skeleton Parameters", {
            item("Skeleton Colour...", "on_skeleton_colour1_activate"),
            item("Skeleton Box Radius...", "on_skeleton_box_radius1_activate"),
            item("Skeletonization Level...", "on_skeletonization_level1_activate"),
         }),
         separator(),
         item("Residue Info...", "on_residue_info2_activate"),
         item("Font Size...", "on_font_size1_activate"),
         item("Pink Pointer Size...", "on_pink_pointer_size1_activate"),
         submenu("Background Colour...", {
            radio("Black", "on_background_black1_activate", true),
            radio("White", "on_background_white1_activate", false),
         }),
         separator(),
         item("Settings...", "on_edit_settings_activate"),
         item("Preferences...", "on_preferences1_activate"),
      }),
      submenu("Calculate", {
         item("Align & Mutate...", "on_align_and_mutate1_activate"),
         submenu("Fit Loop", {
            item("Fit Loop by Database Search...", "on_fit_loop_by_database_search1_activate"),
            item("Fit Loop by Rama Search...", "on_fit_loop_by_rama_search1_activate"),
         }),
         item("Frames/Sec...", "on_fps1_activate"),
         item("Load Tutorial Model and Data", "on_calculate_load_tutorial_model_and_data1_activate"),
         item("Ligand Builder", "on_ligand_builder1_activate"),
         item("LSQ Superpose...", "on_lsq_superpose1_activate"),
         item("Map Tools...", "on_calculate_map_tools_activate"),
         item("Map Sharpening/Blurring...", "on_map_sharpening1_activate"),
         item("Map Skeleton...", "on_skeleton1_activate"),
         item("Move Molecule Here...", "on_move_molecule_here1_activate"),
         item("Mutate Residue Range...", "on_mutate_molecule1_activate"),
         item("NCS Maps...", "on_ncs_maps1_activate"),
         item("Run Script...", "on_run_script1_activate"),
         item("SSM Superpose...", "on_ssm_superposition1_activate"),
         submenu("Scripting...", {
            item("Python...", "on_scripting_python1_activate"),
            item("Scheme...", "on_scripting_scheme1_activate"),
         }),
         item("Updating Maps...", "on_calculate_updating_maps1_activate"),
         item("PanDDA Inspect...", "on_pandda_inspect1_activate"),
      }),
      submenu("Modelling", {
         item("Add Hydrogen Atoms", "BMOD_ADD_HYDROGENS"),
         item("Add Hydrogens using Refmac", "BMOD_ADD_HYDROGENS_REFMAC"),
         item("Assign (force) HETATMs for this Residue", "BMOD_ASSIGN_HETATMS_RESIDUE"),
         item("Invert This Chiral Centre", "BMOD_INVERT_CHIRAL"),
         item("Phosphorylate this Residue", "BMOD_PHOSPHORYLATE"),
         item("What's this?", "BMOD_WHATS_THIS"),
         separator(),
         submenu("Glyco", {
            item("N-linked Glycan Addition...", "BGLYCO_OPEN_DIALOG"),
            separator(),
            item("Set Default N-linked CHO Atoms B-factor", "BGLYCO_SET_DEFAULT_B"),
            item("N-link add NAG, NAG, BMA", "BGLYCO_NLINK_NAG_NAG_BMA"),
            separator(),
            item("Add High Mannose", "BGLYCO_ADD_HIGH_MANNOSE"),
            item("Add Hybrid (Mammal)", "BGLYCO_ADD_HYBRID_MAMMAL"),
            item("Add Complex (Mammal)", "BGLYCO_ADD_COMPLEX_MAMMAL"),
            item("Add Complex (Plant)", "BGLYCO_ADD_COMPLEX_PLANT"),
            separator(),
            item("Delete All Carbohydrate", "BGLYCO_DELETE_ALL"),
            separator(),
            item("Torsion Fit this residue", "BGLYCO_TORSION_FIT"),
            item("Torsion Fit & Refine this residue", "BGLYCO_TORSION_FIT_REFINE"),
            separator(),
            item("Add synthetic pyranose plane restraints", "BGLYCO_SYNTH_PYRANOSE_PLANES"),
            item("Use Unimodal ring torsion restraints", "BGLYCO_UNIMODAL_RING_TORSIONS"),
            separator(),
            item("Display Extra Restraints", "BGLYCO_DISPLAY_EXTRA_REST"),
            item("Undisplay Extra Restraints", "BGLYCO_UNDISPLAY_EXTRA_REST"),
            separator(),
            item("Extract this Tree", "BGLYCO_EXTRACT_TREE"),
         }),
         separator(),
         item("Morph Fit Chain (Averaging Radius 7)", "BMOD_MORPH_FIT_7"),
         item("Morph Fit Chain (Averaging Radius 11)", "BMOD_MORPH_FIT_11"),
         item("Morph Fit Chain by Secondary Structure", "BMOD_MORPH_FIT_SS"),
         item("Find Helices", "BMOD_FIND_HELICES"),
         item("Find Strands", "BMOD_FIND_STRANDS"),
         separator(),
         item("Use \\\"Backrub\\\" Rotamers", "BMOD_BACKRUB_ON"),
         item("Don't use \\\"Backrub\\\" Rotamers", "BMOD_BACKRUB_OFF"),
         separator(),
         item("Duplicate Range (pick atoms)", "BMOD_DUPLICATE_RANGE"),
         item("Symm Shift Reference Chain Here", "BMOD_SYMM_SHIFT_REF_CHAIN"),
         separator(),
         item("Arrange Waters Around Protein...", "BMOD_ARRANGE_WATERS"),
         item("Merge Water Chains...", "BMOD_MERGE_WATER_CHAINS"),
         item("Renumber Waters...", "BMOD_RENUMBER_WATERS"),
         item("Assign HETATM to Molecule...", "BMOD_ASSIGN_HETATM_MOL"),
         item("Fix Nomenclature Errors...", "BMOD_FIX_NOMENCLATURE"),
         item("Reorder Chains...", "BMOD_REORDER_CHAINS"),
         item("Rigid Body Fit Molecule...", "BMOD_RIGID_BODY_FIT_MOL"),
         item("Rigid Body Fit Residue Range...", "BMOD_RIGID_BODY_RANGES"),
         item("Use SEGIDs...", "BMOD_USE_SEGIDS"),
         separator(),
         item("Monomer from Dictionary...", "BMOD_MONOMER_FROM_DICT"),
         item("New Molecule by Sphere...", "BMOD_NEW_MOL_SPHERE"),
         item("New Molecule from Symmetry Op...", "BMOD_NEW_MOL_SYMOP"),
         item("Residue Type Selection...", "BMOD_RESIDUE_TYPE_SEL"),
         item("Fetch PDBe Ligand Description...", "BMOD_FETCH_PDBE_LIGAND"),
         item("Fetch PDBe description for this ligand", "BMOD_FETCH_PDBE_THIS_LIGAND"),
         item("Generate Ligand Restraints...", "BMOD_GENERATE_LIGAND_RESTRAINTS"),
         separator(),
         item("Rename Residue...", "BMOD_RENAME_RESIDUE"),
         item("Add Other Solvent Molecules...", "BMOD_ADD_SOLVENT"),
         item("Superpose Ligands...", "BMOD_SUPERPOSE_LIGANDS"),
         item("Associate Sequence to Chain...", "BMOD_ASSOC_SEQUENCE"),
         item("Assign Sequence (Cootaneer)...", "BMOD_COOTANEER"),
         item("Residues with Alt Confs...", "BMOD_RES_ALT_CONFS"),
         item("Residues with Cis Peptide Bonds...", "BMOD_RES_CIS_PEPTIDES"),
         item("Residues with Missing Atoms...", "BMOD_RES_MISSING_ATOMS"),
         item("Atoms with Zero Occupancies...", "BMOD_RES_ZERO_OCC"),
         separator(),
         item("Other Modelling Tools...", "on_other_modelling_tools1_activate"),
      }),
      submenu("Ligand", {
         item("Isolated Molprobity Dots for this Ligand", "BLIG_MOLPROBITY_DOTS"),
         item("Isolated Coot Ligand Dots for this Ligand", "BLIG_COOT_LIGAND_DOTS"),
         item("Coot All-Atom Contact Dots", "BLIG_ALL_ATOM_DOTS"),
         item("Find Ligands...", "BLIG_FIND_LIGANDS"),
         item("Jiggle-Fit Ligand", "BLIG_JIGGLE_FIT"),
         item("Hydrogenate region", "BLIG_HYDROGENATE"),
         item("Contact Dots for Ligand", "BLIG_CONTACT_DOTS_LIGAND"),
         item("SMILES -> 2D", "BLIG_SMILES_2D"),
         item("SMILES -> simple 3D", "BLIG_SMILES_3D"),
         item("Residue -> 2D", "BLIG_RESIDUE_2D"),
         item("FLEV this residue", "BLIG_FLEV", "enhanced_ligand_tools"),
         item("Toggle FLEV Ligand Interactions", "BLIG_TOGGLE_FLEV", "enhanced_ligand_tools"),
         item("Solid Generic Objects", "BLIG_SOLID_OBJECTS"),
         item("Unsolid Generic Objects", "BLIG_UNSOLID_OBJECTS"),
         item("Show Chemical Features", "BLIG_CHEMICAL_FEATURES", "enhanced_ligand_tools"),
         item("Rename Atom to Reference", "BLIG_RENAME_TO_REFERENCE"),
         item("Tabulate (on terminal) Ligand Distorsions", "BLIG_TABULATE_DISTORTIONS"),
         item("Display Ligand Distortions", "BLIG_DISPLAY_DISTORTIONS"),
         item("Quick Ligand Validate", "BLIG_QUICK_VALIDATE"),
      }),
      submenu("Draw", {
         item("Additional Representation...", "on_additional_representation1_activate"),
         item("Anisotropic Atoms...", "on_anisotropic_atoms1_activate"),
         item("Cell & Symmetry...", "on_show_symmetry1_activate"),
         item("Centre Atom Label...", "on_centre_atom_label1_activate"),
         item("Clipping...", "on_clipping1_activate"),
         item("Coordinates Recentring...", "on_coordinates_recentring1_activate"),
         item("Crosshairs...", "on_crosshairs1_activate"),
         item("Display Manager...", "on_map_and_mol_control1_activate"),
         item("Display only Active", "on_display_only_active1_activate"),
         item("Dragged Map...", "on_dragged_map1_activate"),
         item("Generic Display Objects...", "on_generic_display_objects1_activate"),
         item("Go To Atom...", "on_go_to_atom1_activate"),
         item("Label Atoms in Residue", "on_label_atoms_in_residue1_activate"),
         item("Label Neighbours", "on_label_neighbours1_activate"),
         item("NCS Ghost Control...", "on_ncs_ghost_control1_activate"),
         item("Representation Tools...", "on_draw_representation_tools_activate"),
         item("Rock View On/Off", "on_rock_view_on_off1_activate"),
         item("Spin View On/Off", "on_spin_view_on_off1_activate"),
         molecule_submenu("Sequence View", "sequence_view1"),
         submenu("Screenshot", {
            item("Simple...", "on_simple1_activate"),
            item("Povray...", "on_povray1_activate"),
            item("Raster3D...", "on_raster3d1_activate"),
         }),
         item("Smooth Recentring...", "on_recentring1_activate"),
         item("Stereo...", "on_stereo1_activate"),
         item("Undo Last Navigation", "on_undo_last_navigation1_activate"),
         item("Antialiasing...", "on_antialiasing1_activate"),
      }),
      submenu("Measures", {
         item("Residue Info...", "on_residue_info1_activate"),
         item("Distances & Angles...", "on_distances_and_angles1_activate"),
         item("Environment Distances...", "on_environment_distances1_activate"),
         item("Plane Distances...", "on_plane_distances1_activate"),
         item("Pointer Distances...", "on_pointer_distances1_activate"),
         item("Clear Atom Labels", "on_clear_atom_labels1_activate"),
      }),
      submenu("Validate", {
         molecule_submenu("Ramachandran Plot", "ramachandran_plot1"),
         item("Kleywegt Plot...", "on_ramachandran_differences_plot1_activate"),
         item("Incorrect Chiral Volumes...", "on_incorrect_chiral_volumes1_activate"),
         item("Unmodelled blobs...", "on_unmodelled_blobs1_activate"),
         item("Difference Map Peaks...", "on_difference_map_peaks1_activate"),
         item("Check/Delete Waters...", "on_check_waters1_activate"),
         molecule_submenu("Geometry analysis", "geometry_analysis1"),
         molecule_submenu("Peptide omega analysis", "peptide_omega_analysis1"),
         molecule_submenu("Temp. fact. variance analysis", "temp_fact_variance_analysis1"),
         molecule_submenu("Average Temp. fact. analysis", "temp_fact_analysis1"),
         molecule_submenu("GLN and ASN B-factor Outliers", "gln_and_asn_b_factor_outliers1"),
         molecule_submenu("Rotamer analysis", "rotamer_analysis1"),
         molecule_submenu("Density fit analysis", "density_fit_analysis1"),
         molecule_submenu("Probe clashes", "probe_clashes1"),
         molecule_submenu("NCS Differences", "ncs_differences1"),
      }),
      submenu("About", {
         item("Header/Remarks Browser...", "on_remarks_browser1_activate"),
         item("Hints...", "on_hints1_activate"),
         item("About...", "on_about1_activate"),
      }),
};

} // namespace

const std::vector<MenuEntry> &main_menu_entries() {
   return entries;
}

wxMenuBar *build_main_menu(const MainMenuHooks &hooks) {

   wxMenuBar *bar = new wxMenuBar;
   auto ids = std::make_shared<std::map<int, Selection>>();
   auto on_select = hooks.on_select;
   auto on_molecule = hooks.on_molecule;
   // Events from an item reach the menus above it and then the window, so one
   // handler per top-level menu sees every item.
   auto handler = [ids, on_select, on_molecule](wxCommandEvent &ev) {
      auto it = ids->find(ev.GetId());
      if (it == ids->end()) { ev.Skip(); return; }  // not one of ours, e.g. Quit
      if (it->second.imol >= 0) {
         if (on_molecule) on_molecule(it->second.action, it->second.imol);
      } else if (on_select) {
         on_select(it->second.action, it->second.path);
      }
   };

   for (const MenuEntry &top : entries) {
      wxMenu *menu = new wxMenu;
      fill(menu, top.items, top.label, hooks, ids);
      menu->Bind(wxEVT_MENU, handler);
      bar->Append(menu, menu_label(top.label));
   }

#ifdef __WXOSX__
   // The application menu, as in the GTK build: Customize Toolbar... above the
   // standard items, and Quit named as Bandicoot names it.
   if (wxMenu *apple = bar->OSXGetAppleMenu()) {
      wxMenuItem *customize = apple->Insert(0, wxID_ANY, "Customize Toolbar...");
      apple->InsertSeparator(1);
      (*ids)[customize->GetId()] = Selection{ "customize_toolbar", "Bandicoot > Customize Toolbar..." };
      apple->Bind(wxEVT_MENU, handler);
      if (wxMenuItem *quit = apple->FindItem(wxID_EXIT))
         quit->SetItemLabel("Exit Bandicoot\tCtrl+Q");
   }
   int edit = bar->FindMenu("Edit");
   if (edit != wxNOT_FOUND)
      mac_strip_stock_edit_items(bar->GetMenu(edit));
#endif
   return bar;
}

} // namespace wxui

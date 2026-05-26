// Bandicoot's sphere refine / tandem refine / sphere regularize. The
// upstream Coot equivalents live in python/fitting.py and depend on
// Coot's SWIG Python wrappers (refine_residues_py et al.) — which can't
// be enabled until Coot's C-side PyString_FromString → PyUnicode_FromString
// port lands. So we implement them in C++ here, calling the same internal
// graphics_info_t APIs the Python wrappers eventually reach.
//
// This file lives outside bandicoot_appkit.mm because we need Coot's full
// C++ headers (graphics-info.h, coot-utils/coot-coord-utils.hh) which are
// painful to pull into an Objective-C++ unit. Each entry point is declared
// extern "C" with a GSourceFunc-compatible signature so the toolbar
// dispatch can wire them in as BANDICOOT_EXTRAS.c_callback values.

#include "compat/coot-sysdep.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <gtk/gtk.h>

#include "graphics-info.h"
#include "coot-utils/coot-coord-utils.hh"
#include "coot-utils/atom-overlaps.hh"
#include "c-interface.h"  // for is_valid_map_molecule, write_pdb_file
#include "cc-interface.hh"  // for regularize_residues
#include "c-interface-generic-objects.h"  // for close_generic_object, new_generic_object_*, etc.

extern "C" {
    gboolean bandicoot_action_sphere_refine(gpointer data);
    gboolean bandicoot_action_sphere_refine_plus(gpointer data);
    gboolean bandicoot_action_sphere_regularize(gpointer data);
    gboolean bandicoot_action_sphere_regularize_plus(gpointer data);
    gboolean bandicoot_action_refine_tandem(gpointer data);
    gboolean bandicoot_action_local_probe_dots(gpointer data);
    void bandicoot_clear_probe_dot_objects(void);
}

// Close every generic-display-object whose name matches a probe-dot
// family. Three distinct naming conventions exist:
//
//   * External probe binary (Local Probe Dots) via handle_read_draw_
//     probe_dots_unformatted — space-separated names:
//        "wide contact", "close contact", "small overlap",
//        "bad overlap", "H-bonds"
//
//   * Coot's internal atom_overlaps (coot_all_atom_contact_dots,
//     post-refine dots) — hyphen-separated names with "Molecule N: "
//     prefix. Types come from overlap_delta_to_contact_type:
//        "Molecule N: wide-contact", "Molecule N: close-contact",
//        "Molecule N: small-overlap", "Molecule N: big-overlap",
//        "Molecule N: clash", "Molecule N: H-bond",
//        "Molecule N: vdw-surface", "Molecule N: clashes"
//
//   * In-flight preview (do_interactive_coot_probe) — hyphen-separated
//     names with "Intermediate Atoms " prefix. Same type set.
//
// The substring list below covers all three. Without this helper the
// post-refine "Molecule N: wide-contact" objects survived our clear
// because they don't contain "wide contact" (space) — they have a
// hyphen. That bug let Interactive Dots accumulate on screen even
// when the user toggled it off.
// Render post-refine contact dots for the LOCAL refined region only,
// not the entire molecule. Mirrors `coot_all_atom_contact_dots(imol)`
// in c-interface-ligands.cc:3603 but operates on moving_atoms_asc->mol
// rather than molecules[imol].atom_sel.mol. Result objects use the
// same "Molecule N: <type>" naming so they slot into the existing
// clear/overwrite machinery (bandicoot_clear_probe_dot_objects).
//
// Two scenarios feed this funnel:
//
//   - Real-space refine (multi-residue): moving_atoms_asc->mol already
//     contains the refined residues PLUS their restraint neighbours,
//     so passing it straight to atom_overlaps_container_t is correct.
//
//   - Rotamer / chi edit (single residue): moving_atoms_asc->mol has
//     ONLY the rotated residue with new coords — no neighbours at all,
//     so all-atom dots would find nothing. We build a temporary mmdb
//     that combines the rotated residue (new coords from moving) with
//     its spatial neighbours from molecules[imol] (original coords),
//     then run atom_overlaps on the merged mol. The merged mol is
//     freed before return.
//
// Must be called BEFORE clear_up_moving_atoms() runs — moving_atoms_asc
// is gone after that.
//
// Defined as a static member of graphics_info_t because moving_atoms_asc
// is a private static field. Declared in graphics-info.h next to the
// existing do_interactive_coot_probe.
static int bc_count_residues_in_mol(mmdb::Manager *mol) {
    int count = 0;
    if (!mol) return 0;
    int n_models = mol->GetNumberOfModels();
    for (int imod = 1; imod <= n_models; ++imod) {
        mmdb::Model *model_p = mol->GetModel(imod);
        if (!model_p) continue;
        int n_chains = model_p->GetNumberOfChains();
        for (int ich = 0; ich < n_chains; ++ich) {
            mmdb::Chain *chain_p = model_p->GetChain(ich);
            if (chain_p) count += chain_p->GetNumberOfResidues();
        }
    }
    return count;
}

static mmdb::Residue *bc_first_residue_in_mol(mmdb::Manager *mol) {
    if (!mol) return NULL;
    int n_models = mol->GetNumberOfModels();
    for (int imod = 1; imod <= n_models; ++imod) {
        mmdb::Model *model_p = mol->GetModel(imod);
        if (!model_p) continue;
        int n_chains = model_p->GetNumberOfChains();
        for (int ich = 0; ich < n_chains; ++ich) {
            mmdb::Chain *chain_p = model_p->GetChain(ich);
            if (!chain_p) continue;
            int n_res = chain_p->GetNumberOfResidues();
            if (n_res > 0) return chain_p->GetResidue(0);
        }
    }
    return NULL;
}

void graphics_info_t::bandicoot_render_local_post_refine_dots(int imol) {
    if (!moving_atoms_asc) return;
    if (!moving_atoms_asc->mol) return;
    if (moving_atoms_asc->n_selected_atoms <= 0) return;

    graphics_info_t g;
    bool ignore_waters = true;

    // Build the mol we'll feed to atom_overlaps. Default = moving_atoms_asc->mol.
    // For the single-residue case (rotamer/chi), build a richer temp mol.
    mmdb::Manager *mol_for_dots = moving_atoms_asc->mol;
    mmdb::Manager *temp_mol = NULL;

    int n_moving_res = bc_count_residues_in_mol(moving_atoms_asc->mol);
    if (n_moving_res == 1 && imol >= 0 && imol < (int)g.molecules.size() &&
        is_valid_model_molecule(imol)) {
        mmdb::Residue *moving_res = bc_first_residue_in_mol(moving_atoms_asc->mol);
        mmdb::Manager *full_mol = g.molecules[imol].atom_sel.mol;
        if (moving_res && full_mol) {
            // Spatial neighbours within 6 Å of the rotated residue's atoms.
            // Use the moving residue itself (with NEW coords) as the query;
            // residues_near_residue measures coords, so cross-mol is fine.
            std::vector<mmdb::Residue *> neighbours =
                coot::residues_near_residue(moving_res, full_mol, 6.0f);

            // Drop any neighbour matching the moving residue's spec —
            // that's the original un-rotated copy in full_mol, which would
            // double-up with our moving_res entry.
            coot::residue_spec_t moving_spec(moving_res);
            std::vector<mmdb::Residue *> res_vec;
            res_vec.push_back(moving_res);
            for (size_t i = 0; i < neighbours.size(); ++i) {
                if (!(coot::residue_spec_t(neighbours[i]) == moving_spec)) {
                    res_vec.push_back(neighbours[i]);
                }
            }

            std::pair<bool, mmdb::Manager *> merged =
                coot::util::create_mmdbmanager_from_residue_vector(res_vec, full_mol);
            if (merged.first && merged.second) {
                temp_mol = merged.second;
                mol_for_dots = temp_mol;
            }
        }
    }

    // Spike length 0.5, ball radius 0.25 — same params as the upstream
    // all-atom variant for consistent dot density / appearance.
    coot::atom_overlaps_container_t overlaps(
        mol_for_dots,
        g.Geom_p(),
        ignore_waters,
        0.5f, 0.25f);
    coot::atom_overlaps_dots_container_t c = overlaps.all_atom_contact_dots(0.5, true);

    // Colour lookup — small enough to inline here; matches the upstream
    // colour_map in coot_all_atom_contact_dots so visual style is identical.
    std::map<std::string, coot::colour_holder> colour_map;
    const char *names[] = {
        "blue", "sky", "sea", "greentint", "green", "orange", "orangered",
        "yellow", "yellowtint", "red", "#55dd55", "hotpink", "grey",
        "magenta", "royalblue", NULL,
    };
    for (const char **n = names; *n; ++n) {
        colour_map[*n] = coot::generic_display_object_t::colour_values_from_colour_name(*n);
    }

    typedef std::map<std::string,
                     std::vector<coot::atom_overlaps_dots_container_t::dot_t> > dot_map_t;
    for (dot_map_t::const_iterator it = c.dots.begin(); it != c.dots.end(); ++it) {
        const std::string &type = it->first;
        const std::vector<coot::atom_overlaps_dots_container_t::dot_t> &v = it->second;
        std::string obj_name = "Molecule " + coot::util::int_to_string(imol) + ": " + type;
        int obj = generic_object_index(obj_name);
        if (obj == -1)
            obj = new_generic_object_number_for_molecule(obj_name, imol);
        else
            generic_object_clear(obj);
        int point_size = (type == "vdw-surface") ? 1 : 2;
        for (unsigned int i = 0; i < v.size(); ++i) {
            const std::string &col_inner = v[i].col;
            to_generic_object_add_point_internal(obj, col_inner,
                                                 colour_map[col_inner],
                                                 point_size, v[i].pos);
        }
        if (type != "vdw-surface")
            set_display_generic_object_simple(obj, 1);
    }

    // Clash spikes — single line object, hot-pink, matches upstream.
    std::string clashes_name = "Molecule " + coot::util::int_to_string(imol) + ": clashes";
    int clashes_obj = generic_object_index(clashes_name);
    if (clashes_obj == -1)
        clashes_obj = new_generic_object_number_for_molecule(clashes_name, imol);
    else
        generic_object_clear(clashes_obj);
    for (unsigned int i = 0; i < c.clashes.size(); ++i) {
        const auto &cl = c.clashes[i];
        to_generic_object_add_line(clashes_obj, "#ff59b4", 2,
                                   cl.first.x(),  cl.first.y(),  cl.first.z(),
                                   cl.second.x(), cl.second.y(), cl.second.z());
    }
    set_display_generic_object_simple(clashes_obj, 1);

    // Free the temp mol if we built one for the rotamer/chi merge case.
    if (temp_mol) delete temp_mol;
}

extern "C" void bandicoot_clear_probe_dot_objects(void) {
    static const char *kDotSubstrings[] = {
        // hyphen variants (internal atom-overlaps types)
        "wide-contact", "close-contact", "small-overlap", "big-overlap",
        "H-bond", "vdw-surface",
        // space variants (external `probe` binary types)
        "wide contact", "close contact", "small overlap", "bad overlap",
        // catches singular "clash" and plural "clashes" from both
        "clash",
        NULL,
    };
    int nobjs = static_cast<int>(graphics_info_t::generic_objects_p->size());
    for (int i = 0; i < nobjs; ++i) {
        const std::string &n = (*graphics_info_t::generic_objects_p)[i].name;
        for (const char **s = kDotSubstrings; *s; ++s) {
            if (n.find(*s) != std::string::npos) {
                close_generic_object(i);
                break;
            }
        }
    }
}

// Shared core for the four sphere_* variants.
//
// use_map=true → calls Coot's map-restrained refine; refinement map must
//                be set (Imol_Refinement_Map() > -1) or the action no-ops
//                with a status message.
// use_map=false → calls regularize_residues (geometry only, no map needed).
// expand=true  → also include residues at resno±1 of the active residue
//                (the "+" variants — covers the +/- 1 neighbour pair even
//                if they happen to lie outside the radius).
static gboolean bandicoot_sphere_generic(bool use_map, float radius, bool expand) {
    auto p = graphics_info_t::active_atom_spec();
    if (!p.first) {
        fprintf(stdout, "[bandicoot] %s %s: no active residue; click an atom first\n",
                expand ? "Expanded sphere" : "Sphere",
                use_map ? "refine" : "regularize");
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }
    int imol = p.second.first;
    coot::atom_spec_t as = p.second.second;
    coot::residue_spec_t central(as.chain_id, as.res_no, as.ins_code);

    mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;
    if (!mol) {
        fprintf(stdout, "[bandicoot] sphere: no mol for imol %d\n", imol);
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    // Gather neighbours within the radius and de-duplicate against the
    // central residue (which the helper may or may not include).
    std::vector<coot::residue_spec_t> neighbours =
        coot::residues_near_residue(central, mol, radius);

    std::vector<coot::residue_spec_t> spec_list;
    spec_list.push_back(central);
    for (const auto &n : neighbours) {
        if (!(n == central)) spec_list.push_back(n);
    }

    // Optional sequence-±1 expansion (the "Sphere Refine +" variant).
    // get_residue() returns NULL for missing spots, so we don't need to
    // worry about asking for residue 0 or past the chain end.
    if (expand) {
        coot::residue_spec_t prev(as.chain_id, as.res_no - 1, as.ins_code);
        coot::residue_spec_t next(as.chain_id, as.res_no + 1, as.ins_code);
        bool have_prev = false, have_next = false;
        for (const auto &s : spec_list) {
            if (s == prev) have_prev = true;
            if (s == next) have_next = true;
        }
        if (!have_prev) spec_list.push_back(prev);
        if (!have_next) spec_list.push_back(next);
    }

    // For map-restrained refine: call graphics_info_t::refine_residues_vec
    // (same internal entry point that refine_residues_py uses). Needs the
    // mmdb::Residue * vector, not specs.
    if (use_map) {
        graphics_info_t g;
        int imol_map = g.Imol_Refinement_Map();
        if (!is_valid_map_molecule(imol_map)) {
            fprintf(stdout, "[bandicoot] Sphere refine: refinement map not set "
                            "(use Calculate → Map > Refinement Map…)\n");
            fflush(stdout);
            return G_SOURCE_REMOVE;
        }

        std::vector<mmdb::Residue *> residues;
        for (const auto &rs : spec_list) {
            mmdb::Residue *r = graphics_info_t::molecules[imol].get_residue(rs);
            if (r) residues.push_back(r);
        }
        if (residues.empty()) {
            fprintf(stdout, "[bandicoot] Sphere refine: no residues found\n");
            fflush(stdout);
            return G_SOURCE_REMOVE;
        }

        g.residue_type_selection_was_user_picked_residue_range = false;
        g.refine_residues_vec(imol, residues, /*alt_conf*/ "", mol);
        g.conditionally_wait_for_refinement_to_finish();

        fprintf(stdout, "[bandicoot] %s refine: %zu residues around %s/%d\n",
                expand ? "Expanded sphere" : "Sphere",
                residues.size(),
                as.chain_id.c_str(), as.res_no);
    } else {
        // Geometry-only: regularize_residues takes the spec list directly.
        regularize_residues(imol, spec_list);
        fprintf(stdout, "[bandicoot] %s regularize: %zu residues around %s/%d\n",
                expand ? "Expanded sphere" : "Sphere",
                spec_list.size(),
                as.chain_id.c_str(), as.res_no);
    }
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

// 4.5 Å sphere centred on the active residue, optionally expanded by ±1.
gboolean bandicoot_action_sphere_refine(gpointer)         { return bandicoot_sphere_generic(true,  4.5f, false); }
gboolean bandicoot_action_sphere_refine_plus(gpointer)    { return bandicoot_sphere_generic(true,  4.5f, true);  }
gboolean bandicoot_action_sphere_regularize(gpointer)     { return bandicoot_sphere_generic(false, 4.5f, false); }
gboolean bandicoot_action_sphere_regularize_plus(gpointer){ return bandicoot_sphere_generic(false, 4.5f, true);  }

// Tandem Refine: sequence-only ±3 residues around the active one.
// Different from sphere refine in that it doesn't pick up spatial
// neighbours — strictly a 7-residue contiguous window.
gboolean bandicoot_action_refine_tandem(gpointer) {
    auto p = graphics_info_t::active_atom_spec();
    if (!p.first) {
        fprintf(stdout, "[bandicoot] Tandem Refine: no active residue; click an atom first\n");
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }
    int imol = p.second.first;
    coot::atom_spec_t as = p.second.second;
    mmdb::Manager *mol = graphics_info_t::molecules[imol].atom_sel.mol;

    graphics_info_t g;
    int imol_map = g.Imol_Refinement_Map();
    if (!is_valid_map_molecule(imol_map)) {
        fprintf(stdout, "[bandicoot] Tandem Refine: refinement map not set\n");
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    std::vector<mmdb::Residue *> residues;
    for (int dr = -3; dr <= 3; ++dr) {
        coot::residue_spec_t rs(as.chain_id, as.res_no + dr, as.ins_code);
        mmdb::Residue *r = graphics_info_t::molecules[imol].get_residue(rs);
        if (r) residues.push_back(r);
    }
    if (residues.empty()) {
        fprintf(stdout, "[bandicoot] Tandem Refine: no residues found in range\n");
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    g.residue_type_selection_was_user_picked_residue_range = false;
    g.refine_residues_vec(imol, residues, /*alt_conf*/ "", mol);
    g.conditionally_wait_for_refinement_to_finish();

    fprintf(stdout, "[bandicoot] Tandem Refine: %s/%d-%d (%zu residues)\n",
            as.chain_id.c_str(), as.res_no - 3, as.res_no + 3, residues.size());
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

// Local Probe Dots: one-shot probe-dot overlay for the active residue's
// neighbourhood. Coot upstream wires this through Python (generic_objects.py
// probe(), interactive_probe()) which depends on the Py3 port — disabled in
// Bandicoot. We do the same dance in C++: write a temp PDB, fork+exec
// `probe`, read its dots output back via handle_read_draw_probe_dots_
// unformatted(). The `probe` binary itself ships in <install>/bin/probe
// (BSD-3 Richardson Lab build from CCP4 9.0.014_arm, see
// scripts/bundle_external_tools.sh).
gboolean bandicoot_action_local_probe_dots(gpointer) {
    auto p = graphics_info_t::active_atom_spec();
    if (!p.first) {
        fprintf(stdout, "[bandicoot] Local Probe Dots: no active residue; click an atom first\n");
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }
    int imol = p.second.first;
    coot::atom_spec_t as = p.second.second;

    // Resolve `probe` and `reduce` next to coot-bin/bcoot — same install
    // tree. Fall back to PATH lookup if either is missing (developer
    // builds without bundling).
    const char *prefix_env = std::getenv("COOT_PREFIX");
    auto resolve_tool = [&](const char *name) -> std::string {
        if (prefix_env && *prefix_env) {
            std::string p = std::string(prefix_env) + "/bin/" + name;
            struct stat st;
            if (stat(p.c_str(), &st) == 0) return p;
        }
        return std::string(name); // PATH lookup
    };
    std::string probe_path  = resolve_tool("probe");
    std::string reduce_path = resolve_tool("reduce");

    // Stage outputs in a coot-molprobity/ subdir of cwd (matches upstream's
    // convention and keeps tmp files out of the working directory root).
    mkdir("coot-molprobity", 0755);
    std::string pdb_raw      = "coot-molprobity/bandicoot-local-probe.pdb";
    std::string pdb_reduced  = "coot-molprobity/bandicoot-local-probe-H.pdb";
    std::string dots_out     = "coot-molprobity/bandicoot-local-probe.dots";

    int wstat = write_pdb_file(imol, pdb_raw.c_str());
    if (wstat != 0) {
        fprintf(stdout, "[bandicoot] Local Probe Dots: write_pdb_file failed (status %d)\n", wstat);
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    // 1) `reduce -build` adds hydrogens. Without H, probe's default
    //    -Explicit mode finds essentially no contacts. reduce reads
    //    its heteroatom dictionary from REDUCE_HET_DICT (exported by
    //    coot.in to point at <install>/share/coot/reduce_wwPDB_het_dict.txt).
    //    -quiet suppresses chatty stderr; -allalt processes all altLocs.
    char reduce_cmd[2048];
    snprintf(reduce_cmd, sizeof(reduce_cmd),
             "'%s' -build -quiet -allalt '%s' > '%s' 2>/dev/null",
             reduce_path.c_str(), pdb_raw.c_str(), pdb_reduced.c_str());
    int rc = std::system(reduce_cmd);
    // reduce's exit code is unreliable (it returns 1 on warnings); judge
    // success by whether the output file is non-empty.
    struct stat rst;
    if (stat(pdb_reduced.c_str(), &rst) != 0 || rst.st_size == 0) {
        fprintf(stdout, "[bandicoot] Local Probe Dots: reduce produced no output "
                        "(rc=%d, %s)\n", rc, reduce_path.c_str());
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    // 2) probe with two patterns:
    //      src    = active residue (e.g. "chainA 93")
    //      target = "ALL" — probe filters bonded self-self contacts
    //               via -4H, so intra-residue rotamer clashes still
    //               appear, plus every contact with neighbouring
    //               atoms. Tried "not chainA 93" first; probe parses
    //               it without complaint but silently returns zero
    //               contacts. ALL is the form the existing wrappers
    //               in upstream Coot's generic_objects.py use.
    //    Selection syntax (Kinemage): "chainA 123". Chain prefix is
    //    required when chain_id is non-empty; case-insensitive on the
    //    chain letter itself.
    std::string chain_token;
    if (!as.chain_id.empty()) {
        chain_token = "chain" + as.chain_id + " ";
    }
    char src[256];
    snprintf(src, sizeof(src), "%s%d", chain_token.c_str(), as.res_no);

    char probe_cmd[2048];
    snprintf(probe_cmd, sizeof(probe_cmd),
             "'%s' -u -mc -quiet -both '%s' 'ALL' '%s' > '%s' 2>/dev/null",
             probe_path.c_str(), src, pdb_reduced.c_str(), dots_out.c_str());

    fprintf(stdout, "[bandicoot] Local Probe Dots: %s\n", probe_cmd);
    fflush(stdout);

    rc = std::system(probe_cmd);
    if (rc != 0) {
        fprintf(stdout, "[bandicoot] Local Probe Dots: probe exited %d "
                        "(is %s on PATH?)\n",
                rc, probe_path.c_str());
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    // Sanity-check the output. probe writes an empty file when the
    // selection matched nothing; handle_read_draw_probe_dots_unformatted
    // would render no dots but also wouldn't fail loudly.
    struct stat st;
    if (stat(dots_out.c_str(), &st) != 0 || st.st_size == 0) {
        fprintf(stdout, "[bandicoot] Local Probe Dots: empty output (no dots for %s/%d)\n",
                as.chain_id.c_str(), as.res_no);
        fflush(stdout);
        return G_SOURCE_REMOVE;
    }

    // Clear any cross-family dot objects (Interactive Dots' "Molecule N:
    // <type>") so the new dots don't overlay the previous overview.
    // handle_read_draw_probe_dots_unformatted already clears its own
    // family ("wide contact" etc.) internally; the helper handles both.
    bandicoot_clear_probe_dot_objects();

    // show_clash_gui_flag=0 → just draw the dots, don't pop the
    // clash list window (which doesn't fit our one-shot model).
    handle_read_draw_probe_dots_unformatted(dots_out.c_str(), imol, 0);
    graphics_draw();

    fprintf(stdout, "[bandicoot] Local Probe Dots: %s/%d → %lld bytes of dots\n",
            as.chain_id.c_str(), as.res_no, (long long)st.st_size);
    fflush(stdout);
    return G_SOURCE_REMOVE;
}

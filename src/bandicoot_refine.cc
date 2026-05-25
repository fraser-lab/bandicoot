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
#include <vector>

#include <gtk/gtk.h>

#include "graphics-info.h"
#include "coot-utils/coot-coord-utils.hh"
#include "c-interface.h"  // for is_valid_map_molecule
#include "cc-interface.hh"  // for regularize_residues

extern "C" {
    gboolean bandicoot_action_sphere_refine(gpointer data);
    gboolean bandicoot_action_sphere_refine_plus(gpointer data);
    gboolean bandicoot_action_sphere_regularize(gpointer data);
    gboolean bandicoot_action_sphere_regularize_plus(gpointer data);
    gboolean bandicoot_action_refine_tandem(gpointer data);
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

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
#include "c-interface.h"  // for is_valid_map_molecule, write_pdb_file
#include "cc-interface.hh"  // for regularize_residues
#include "c-interface-generic-objects.h"  // for close_generic_object

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
// family. Local Probe Dots (external `probe` via handle_read_draw_probe_
// dots_unformatted) names its objects "wide contact" / "close contact" /
// "small overlap" / "bad overlap" / "H-bonds". Interactive Dots (post-
// accept via coot_all_atom_contact_dots) prefixes those with "Molecule N: "
// and also adds "Molecule N: clashes". Without this helper the two
// families overlay each other on the canvas (confusing). Call this
// before either dot-generation path so the new dots are the only ones
// visible.
extern "C" void bandicoot_clear_probe_dot_objects(void) {
    static const char *kDotSubstrings[] = {
        "wide contact", "close contact", "small overlap",
        "bad overlap",  "H-bonds",       "clashes",
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

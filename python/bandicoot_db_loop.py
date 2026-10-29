# bandicoot_db_loop.py -- Fit Loop by Database Search without pygtk.
#
# Coot's click_protein_db_loop_gui() (coot_gui.py) asks for the number of
# residues and lists the candidate loops in pygtk windows, which cannot appear
# in Bandicoot. Here the native window asks for the number and calls
# bandicoot_db_loop_pick(n); the candidates go to a native window of buttons
# (bandicoot_button_choice_py). Loaded into __main__ by coot_load_modules, so
# coot's functions and coot_gui's helpers are in scope.


def bandicoot_db_loop_pick(n_residues):
    """Pick n_residues atoms, then offer the database's loops for that range."""

    def picked(*atom_specs):
        # each spec is [model, imol, chain, resno, ins, atom, alt]
        imol = atom_specs[0][1]
        residue_specs = [[spec[2], spec[3], spec[4]] for spec in atom_specs]
        min_max_and_chain_id = min_max_residues_from_atom_specs(atom_specs)
        if not isinstance(min_max_and_chain_id, list):
            info_dialog("WARNING:: Picked atoms not in same molecule and chain")
            return

        loop_mols = protein_db_loops(imol, residue_specs, imol_refinement_map(),
                                     10, db_loop_preserve_residue_names)
        imol_loop_orig = loop_mols[0][0]
        imol_loops_consolidated = loop_mols[0][1]
        candidates = list(loop_mols[1])
        min_resno, max_resno, chain_id = min_max_and_chain_id
        set_mol_active(imol_loops_consolidated, 1)
        if not valid_model_molecule_qm(imol_loop_orig) or not candidates:
            return

        def copy_from(imol_from):
            return ("copy_residue_range(%d, %r, %d, %r, %d, %d)"
                    % (imol, chain_id, imol_from, chain_id, min_resno, max_resno))

        buttons = [["Original loop", copy_from(imol_loop_orig)],
                   ["Toggle All Candidate Loops",
                    "(toggle_active_mol(%d), toggle_display_mol(%d))"
                    % (imol_loops_consolidated, imol_loops_consolidated)]]
        for loop_mol in candidates:
            buttons.append([str(loop_mol) + " " + molecule_name(loop_mol), copy_from(loop_mol)])

        hidden = candidates + [imol_loops_consolidated]
        close = "(" + ", ".join("set_mol_displayed(%d, 0), set_mol_active(%d, 0)" % (m, m)
                                for m in hidden) + ")"
        bandicoot_button_choice_py("Loop Candidates", buttons, "Close", close)

    user_defined_click(n_residues, picked)

# bandicoot_restraints.py
#
# Automatic generation of ligand restraints for components that have no
# dictionary.
#
# Copyright 2026 by Bandicoot contributors
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# ---------------------------------------------------------------------------
# WHY phenix.elbow IS TRIED FIRST, AND WHY acedrg CANNOT REPLACE IT
#
# A component with no dictionary usually reaches us as COORDINATES ONLY --
# element types and positions, no bonds. Deposited files rarely carry usable
# CONECT records for a ligand.
#
#   * acedrg cannot start from that. Every one of its input modes wants the
#     chemistry already worked out: -c (mmCIF "coordinates and bonds"),
#     -m (MOL), -g (MOL2), -i (SMILES).
#   * phenix.elbow perceives connectivity, bond orders and aromaticity from
#     the coordinates itself, and preserves the input atom names.
#
# Atom names are not a detail: Coot binds restraints to atoms by name string,
# so a dictionary whose atom ids do not match the model is inert. That is also
# why we never route coordinates through SMILES -- SMILES carries no names, and
# recovering them is a graph-isomorphism problem that molecular symmetry makes
# ambiguous.
#
# Measured on this design (2026-08-28): a 17-heavy-atom ligand stripped of all
# CONECT records and all hydrogens gave connectivity, bond orders and
# aromaticity identical to a reference elbow run, in about 7 seconds.
#
# elbow is used for the SMILES pathway too, so that a given molecule yields the
# same dictionary whether it arrived as coordinates or as SMILES.
# ---------------------------------------------------------------------------

import os
import subprocess
import tempfile


# elbow names the component LIG unless told otherwise, and a dictionary whose
# comp id does not match the residue name in the model will not bind to it.
DEFAULT_COMP_ID = "LIG"


def _which(program):
    """Return the full path to program, or None."""
    try:
        from shutil import which
    except ImportError:
        return None
    return which(program)


def find_restraint_generator():
    """Return (name, path) of the best available generator, or (None, None).

    Order is deliberate: elbow first, because it is the only one of the two
    that can start from bare coordinates, and using it for every pathway keeps
    the restraints for a given molecule independent of how it was read in.
    """
    for name in ("phenix.elbow", "acedrg"):
        path = _which(name)
        if path:
            return ("elbow" if name == "phenix.elbow" else "acedrg", path)
    return (None, None)


def _run(args, workdir):
    """Run args in workdir. Returns (returncode, combined_output_text)."""
    try:
        proc = subprocess.run(args, cwd=workdir,
                              stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT)
    except Exception as e:
        return (-1, "could not run %s (%s)" % (args[0], e))
    return (proc.returncode, proc.stdout.decode("utf-8", "replace"))


def _elbow(args, workdir, stub, what):
    """Shared elbow runner. Returns (pdb_path, cif_path, error_message).

    error_message is None on success. Full diagnostic detail goes to stdout,
    where it can be copy-pasted into a bug report; the returned message is the
    short form for a dialog.
    """
    elbow = _which("phenix.elbow")
    if not elbow:
        return (None, None, "phenix.elbow not found on PATH")

    status, output = _run([elbow] + args + ["--output=" + stub], workdir)

    pdb = os.path.join(workdir, stub + ".pdb")
    cif = os.path.join(workdir, stub + ".cif")

    if status != 0 or not os.path.isfile(cif):
        print("ERROR:: phenix.elbow failed for %s (status %s)" % (what, status))
        if output:
            print("ERROR:: elbow output (tail):\n" + output[-2000:])
        return (None, None, "restraint generation failed for %s" % what)

    return (pdb, cif, None)


def elbow_from_coordinates(pdb_file_name, comp_id):
    """Generate restraints from a coordinate file containing the component.

    The file needs coordinates and elements only; bonds are perceived. Returns
    (pdb_path, cif_path, error_message).
    """
    comp_id = (comp_id or DEFAULT_COMP_ID).strip() or DEFAULT_COMP_ID
    workdir = tempfile.mkdtemp(prefix="bcoot_elbow_")
    # --name sets the dictionary's comp id. Without it elbow writes LIG and the
    # restraints silently fail to bind to a residue called anything else.
    return _elbow(["--file=" + pdb_file_name, "--name=" + comp_id],
                  workdir, comp_id, comp_id)


def elbow_from_smiles(smiles, comp_id):
    """Generate a 3D ligand and its restraints from a SMILES string.

    Returns (pdb_path, cif_path, error_message).
    """
    smiles = (smiles or "").strip()
    if not smiles:
        return (None, None, "empty SMILES string")
    comp_id = (comp_id or DEFAULT_COMP_ID).strip() or DEFAULT_COMP_ID
    workdir = tempfile.mkdtemp(prefix="bcoot_elbow_")
    return _elbow(["--smiles=" + smiles, "--name=" + comp_id],
                  workdir, comp_id, "SMILES string")


def generate_restraints_for_comp_id(imol, comp_id):
    """Generate and load restraints for comp_id as it appears in molecule imol.

    Extracts ONE copy of the component from imol, hands the coordinates to
    elbow, and reads the resulting dictionary back in. Returns (ok, message);
    message is None on success.

    Two things guard against a comp id that covers more than one chemistry,
    which restraints keyed by comp id cannot represent:

      * a refusal, when the residues sharing this comp id have atom names that
        do not match each other; and
      * deriving from a SINGLE representative residue, so that several residues
        can never reach the generator as though they were one molecule. That is
        also correct for genuine copies, which need only one dictionary.

    The earlier version extracted every residue of the type at once. For real
    copies elbow does the right thing with that -- it recognises them as one
    entity -- but for two different molecules under one name it silently
    described only one of them.
    """
    comp_id = (comp_id or "").strip()
    if not comp_id:
        return (False, "no component id given")

    clash = comp_id_collision_message(imol, comp_id)
    if clash:
        print("WARNING:: " + clash)
        return (False,
                "%s names more than one molecule in this structure.\n"
                "Restraints are stored by component id, so only one of them\n"
                "could be described. Rename one of them and try again."
                % comp_id)

    selection = most_complete_residue_selection(imol, comp_id)
    if not selection:
        return (False, "could not find %s in molecule %s" % (comp_id, imol))

    imol_lig = new_molecule_by_atom_selection(imol, selection)
    if not valid_model_molecule_qm(imol_lig):
        return (False, "could not extract %s from molecule %s"
                % (comp_id, imol))

    try:
        workdir = tempfile.mkdtemp(prefix="bcoot_lig_")
        lig_pdb = os.path.join(workdir, comp_id + "-from-bandicoot.pdb")
        if write_pdb_file(imol_lig, lig_pdb) != 0 or not os.path.isfile(lig_pdb):
            return (False, "could not write coordinates for %s" % comp_id)
    finally:
        # The extraction is a working copy; it must not linger in the molecule
        # list whether or not generation succeeds.
        close_molecule(imol_lig)

    pdb, cif, err = elbow_from_coordinates(lig_pdb, comp_id)
    if err:
        return (False, err)

    print("INFO:: generated restraints for %s -> %s" % (comp_id, cif))
    read_cif_dictionary(cif)

    # The restraints were derived from one residue. If another residue shares
    # this comp id but is a different molecule -- and did so in a way the
    # collision test above could not see, because its atom names happened to be
    # a subset -- the new dictionary will not cover it. That is the last place
    # the mismatch can be caught before the user meets it as a refinement
    # failure, so say so rather than reporting a clean success.
    mismatch = dictionary_coverage_message(imol, comp_id)
    if mismatch:
        print("WARNING:: " + mismatch)
        return (True,
                "Restraints were generated, but they do not cover every copy\n"
                "of %s in this molecule. The copies are probably not the same\n"
                "chemistry. Rename them apart and generate for each." % comp_id)

    return (True, None)

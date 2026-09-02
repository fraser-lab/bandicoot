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


# ---------------------------------------------------------------------------
# FINDING THE GENERATOR WHEN BANDICOOT WAS NOT LAUNCHED FROM A SHELL
#
# Launched from a terminal, phenix.elbow and acedrg are on PATH and there is
# nothing to do. Launched from the Dock or the Finder, the process inherits a
# bare PATH (/usr/bin:/bin:/usr/sbin:/sbin) and neither tool is visible, so
# the whole feature silently does nothing.
#
# Searching a list of known install directories does NOT fix this, and that is
# worth stating because it is the obvious fix. Measured on this machine: the
# SBGrid wrapper /programs/i386-mac/system/sbgrid_bin/phenix.elbow IS found by
# such a search, and calling it by absolute path in a bare environment prints
#
#     SBGrid shell environment is not initialized! Please source
#     /programs/sbgrid.shrc ... to use the software.
#
# and exits 0 having done nothing. The tools need an ENVIRONMENT, not just a
# location -- SBGrid's dispatch variables, or phenix_env.sh, or ccp4.setup-sh.
#
# So we ask the user's own shell instead. It is the one thing that knows how
# this machine is set up, whatever the vendor and wherever it is installed,
# and it needs no per-vendor list to maintain.
#
# The shell must be INTERACTIVE (-i), not merely a login shell: SBGrid's
# installer writes "source /programs/sbgrid.shrc" into ~/.zshrc, and zsh reads
# .zshrc for interactive shells only. Measured: `zsh -lc` finds nothing,
# `zsh -ic` finds elbow. Cost is about 2 s, paid once per session and only
# when the direct lookup has already failed.
# ---------------------------------------------------------------------------

# None = not looked yet; {} = looked and got nothing (do not look again).
_shell_env = None


def _login_shell_env():
    """The environment the user's interactive shell would give us, or None.

    Cached: the shell is run at most once per session.
    """
    global _shell_env
    if _shell_env is not None:
        return _shell_env or None

    _shell_env = {}
    shell = os.environ.get("SHELL") or "/bin/bash"
    try:
        # stdin is closed so that an rc file which reads it cannot hang us, and
        # stderr is dropped because an interactive shell writes job-control and
        # prompt noise there that says nothing about success.
        proc = subprocess.run([shell, "-ic", "env -0"],
                              stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL,
                              timeout=30)
    except Exception as e:
        print("WARNING:: could not ask %s for its environment (%s)" % (shell, e))
        return None

    text = proc.stdout.decode("utf-8", "replace")
    # env -0 is the reliable split: a value may itself contain newlines (a
    # multi-line function definition exported into the environment does).
    # Fall back to lines if this shell's env has no -0.
    items = text.split("\0") if "\0" in text else text.split("\n")

    env = {}
    for item in items:
        if "=" in item:
            key, value = item.split("=", 1)
            # Reject anything that is not a plausible variable name: the first
            # lines of output can be rc-file chatter that happens to contain
            # an equals sign.
            if key and not key[0].isdigit() and " " not in key:
                env[key] = value

    if "PATH" not in env:
        print("WARNING:: %s gave no usable environment; "
              "restraint generators will be looked for on PATH only" % shell)
        return None

    _shell_env = env
    return env


def resolve_tool(program):
    """Locate program. Returns (path, env), or (None, None) if not found.

    NOTE this file is reached two ways. coot_load_modules.py.in exec()s it into
    __main__ (which is why the functions below may call coot scripting names
    bare), and bandicoot_pandda.py does a real "import bandicoot_restraints"
    for this function and elbow_from_smiles(). The two copies keep separate
    caches, so a machine needing the shell probe may pay for it twice in a
    session. Everything the imported copy touches is standard library only --
    do not reach for a bare coot call from resolve_tool() or _elbow().

    env is None when our own PATH was enough, and otherwise the environment
    the program must be RUN with -- see the note above: for a wrapper such as
    SBGrid's, the path alone is not enough to make it work.
    """
    try:
        from shutil import which
    except ImportError:
        return (None, None)

    path = which(program)
    if path:
        return (path, None)

    env = _login_shell_env()
    if env:
        path = which(program, path=env.get("PATH", ""))
        if path:
            return (path, env)

    return (None, None)


def find_restraint_generator():
    """Return (name, path) of the best available generator, or (None, None).

    Order is deliberate: elbow first, because it is the only one of the two
    that can start from bare coordinates, and using it for every pathway keeps
    the restraints for a given molecule independent of how it was read in.
    """
    for name in ("phenix.elbow", "acedrg"):
        path, env = resolve_tool(name)
        if path:
            return ("elbow" if name == "phenix.elbow" else "acedrg", path)
    return (None, None)


def _run(args, workdir, env=None):
    """Run args in workdir. Returns (returncode, combined_output_text).

    WARNING: OUTPUT GOES TO A FILE, NOT A PIPE, AND THAT IS WORTH 5 SECONDS A CALL.

    subprocess.run(stdout=PIPE) does not wait for the child; it waits for EOF
    on the pipe, which arrives only when EVERY copy of the write end is closed.
    SBGrid's dispatcher leaves a background process holding an inherited copy
    for about five seconds after the tool itself is done, so the pipe stays
    open long after there is anything to read.

    Measured on phenix.elbow --version:

        child exited after     1.26 s
        pipe reached EOF at    6.27 s      -> 5.01 s of pure waiting

    A regular file has no such reader, so the wait disappears: the same call
    with stdout redirected to a file takes 1.37 s. (This is also why the tools
    feel instant when run by hand -- a terminal is a tty, not a pipe, so nobody
    ever notices.)
    """
    if env is not None:
        # A copy, because PWD is ours to set: it came from the shell we asked
        # and would otherwise name that shell's directory, not the one the
        # tool is about to write into.
        env = dict(env)
        env["PWD"] = workdir

    out_path = os.path.join(workdir, "_bandicoot_tool_output.txt")
    try:
        with open(out_path, "w") as out_file:
            proc = subprocess.run(args, cwd=workdir, env=env,
                                  stdin=subprocess.DEVNULL,
                                  stdout=out_file,
                                  stderr=subprocess.STDOUT)
    except Exception as e:
        return (-1, "could not run %s (%s)" % (args[0], e))

    try:
        with open(out_path, "rb") as fh:
            text = fh.read().decode("utf-8", "replace")
    except Exception:
        text = ""

    return (proc.returncode, text)


def _elbow(args, workdir, stub, what):
    """Shared elbow runner. Returns (pdb_path, cif_path, error_message).

    error_message is None on success. Full diagnostic detail goes to stdout,
    where it can be copy-pasted into a bug report; the returned message is the
    short form for a dialog.
    """
    elbow, env = resolve_tool("phenix.elbow")
    if not elbow:
        return (None, None, "phenix.elbow not found")

    status, output = _run([elbow] + args + ["--output=" + stub], workdir, env)

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


# ---------------------------------------------------------------------------
# String-returning forms for the GTK dialog (src/restraints-gui.cc).
#
# The C++ side reads one string back per call rather than unpacking a tuple:
# safe_python_command_with_return() is the existing bridge and every other
# Bandicoot dialog uses it this way. Tagged so that "generated, but..." stays
# distinguishable from "failed" -- they need different reporting, and a bare
# message cannot be told apart.
# ---------------------------------------------------------------------------

def generate_restraints_status(imol, comp_id):
    """Generate restraints for comp_id in imol. Returns one of

        "ok"              generated, and the dictionary covers the model
        "warn:<message>"  generated, but something is worth saying
        "fail:<message>"  not generated
    """
    try:
        ok, message = generate_restraints_for_comp_id(imol, comp_id)
    except Exception as e:
        # An exception here would otherwise abort the whole run of a
        # multi-ligand generation. Report it as this component's failure and
        # let the rest proceed.
        print("ERROR:: restraint generation for %s raised %s" % (comp_id, e))
        return "fail:restraint generation failed for %s (see terminal)" % comp_id

    if not ok:
        return "fail:" + (message or "could not generate restraints for %s" % comp_id)
    if message:
        return "warn:" + message
    return "ok"


def restraint_generator_name():
    """"elbow", "acedrg", or "" when neither can be found."""
    name, path = find_restraint_generator()
    return name or ""

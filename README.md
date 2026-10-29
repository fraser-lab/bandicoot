# Bandicoot

Bandicoot is a macOS-native fork of [Coot](https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/) 0.9.8.95, the structural biology macromolecular model-building program. It keeps most of Coot 0.9's functionality and look-and-feel, with a rebuilt user interface and new features that Coot 0.9 does not have.

**RELEASES**: Tarballs of binary releases are found at https://github.com/fraser-lab/bandicoot/releases

**NOTE:** As of 10.29.2026 Bandicoot UI has been rebuilt in wxWidgets. This enables a port to Linux, which is forthcoming.

**NOTE** Bandicoot is a work in progress; users are welcome to log issues and issue pull requests.

## Why a fork?

The Crystallographic Object-Oriented Tool (Coot) has been the go-to suite of software for molecular modeling, used by thousands of structural biologists all over the world for over twenty years. Recently, Coot has been fully reimagined and redesigned, as well as giving the libraries and packages under the hood a much-needed upgrade. The resulting program (Coot 1) is in wide use today. However, many suites of software for structural biology rely on the old Coot 0.9 framework in order to function, making it necessary for both versions to remain in circulation. 

While Coot 0.9.8.95 is distributed alongside Coot 1 in suites such as CCP4, it has become completely unusable on the most recent MacOS version (26.x, Tahoe). Bandicoot, a fork of Coot 0.9.8.95, initially addressed each of these with macOS-specific fixes layered on top of upstream Coot. In September of 2026, Bandicoot has become a stand-alone development project, with new features continually added. See [What changed vs Coot 0.9.8.95](#what-changed-vs-coot-09895) below for a list of major changes.

## Quick start

The easiest way to install Bandicoot is to download a binary tarball (https://github.com/fraser-lab/bandicoot/releases) and install it (see [INSTALL.md](INSTALL.md)).

To build from source instead, see [BUILD.md](BUILD.md). You'll need a
handful of Homebrew packages and a Miniconda environment that supplies
Clipper, MMDB2, and a few others.


## What changed vs Coot 0.9.8.95

The changes fall into two categories: adjustments made to enable Coot 0.9 to run on MacOS 26.x Tahoe and features new in Bandicoot (or expanded from their Coot 0.9 versions). 

(**NOTE**: Known problems and limitations are listed in [OUTSTANDING_ISSUES.md](OUTSTANDING_ISSUES.md)).

### MacOS 26.x Tahoe Adjustments

- **No XQuartz dependency.** freeglut is removed entirely, so nothing starts an X11
  server.
- **wxWidgets-based user interface.** The entire user interface has been rebuilt in wxWidgets, which not only makes Bandicoot run on MacOS but also makes it portable to other operating systems, e.g. Linux
- **Python 3.** Coot's old Python 2 layer has been ported to Python 3, with the interpreter, NumPy and matplotlib bundled.
- **Native text atom labels.**
- **Retina-correct viewport and atom picking.**

### New and Updated Features

#### Coot 0.9 functionality repaired, modernised or extended.

- **mmCIF handling rebuilt on gemmi.** All mmCIF reading and writing goes through [gemmi](https://github.com/project-gemmi/gemmi), with complete fidelity. The Header Browser shows real mmCIF metadata.
- **CIF files are classified by content.** Bandicoot now distinguishes coordinate vs. restraint mmCIF files if they are drag/dropped into the main window.
- **More files open:** modern small-molecule CIFs, and Phenix-produced CIFs can now be read in.
- **Extended wwPDB identifiers.** New-style entry IDs are fetched from the current archive. (Full compatibility with the new PDB archive will be implemented when that archive goes online in 2027.)
- **Configurable atom-pick radii**, separately for ordinary, symmetry and intermediate atoms.
- **Live MolProbity probe dots preview.** With "Interactive Dots" toggled on, moving atoms during real-space refinement previews clashes.
- **"Modelling" is made into its own top menu item**.
- **"Glyco" is now an item in "Modelling".** It is also always visible and accessible.
- **Ctrl-C in the launching terminal shuts the program down.**
- **Restraint tracking by molecule.** This expands the old Coot 0.9 functionality in which ligand restraints were tracked by molecule name. The new approach allows Bandicoot to create separate, sensible restraints for multiple different molecules that have the same name, e.g. "LIG" or "DRG"
- **New and consolidated Preferences.** Several preferences were moved from the Draw menu to Preferences; also some preferences were consolidated into sensible groupings. This process is ongoing.
- **Updated and improved icons.** Many top toolbar icons and menu item icons have been improved, updated, or created. In particular, unique icons have been created for every top toolbar button, making it comprehensible in "icons only" mode.
- **Bugfixes.** Several bugs inherited from Coot 0.9 have been fixed and odd behaviours ironed out.
- **Trackpad navigation.** On a Mac laptop, Opt-Left-Click now centers on an atom, same as the middle-click with a three-button mouse. (Also works with attached keyboard and mouse.)

#### New in BANDICOOT

Capabilities with no Coot 0.9 equivalent.

- **Ligand restraints generation.** Restraints are generated for unrecognised ligands on load, through an external generator, and stored per molecule. A dialog lists the components it can describe and lets you exclude any of them; it is also reachable from Modelling -> Generate Ligand Restraints.
- **Colour by alternate conformation** is now available as an option in the Display Manager; this scheme assigns different colors to residues in alternate conformations. It is now the default bond scheme, with preferences for the scheme itself and for how far the alternate conformers differ in colour from the bulk model.
- **Scaling by occupancy** accompanies the colouring by alternate conformation; now the line thickness is coarsely proportionate to the occupancy. This works with really thick lines and is gracefully handled with thin line settings.
- **Session recording.** An optional event log of a modelling session: where the user looked and what the maps showed there, every command Coot echoes, residue-level model edits, and clicks on validation results. Off unless asked for -- start it with `BANDICOOT_RECORD=1 bcoot` or `start_session_recording()` in the scripting console. See  [SESSION_RECORDING.md](SESSION_RECORDING.md).
- **PanDDA Inspect interface**, with an HTML report, a `--pandda <dir>` option and its own launcher. Can also be invoked by running `<extracted>/bin/bandicoot.inspect` within the panDDA folder itself.
- **Ligand from SMILES.** Now an option in the Other Modelling Tools window.
- **New Toolbar Buttons.** Auto-open MTZ, Open Map and Quicksave.
- **Bulk alternate-conformation rename** from Residue Info. Allows assignment of an altloc code even if only a single alternate conformation exists. (E.g. when modeling a partially occupied water molecule that overlaps with a modeled alternate conformation of a nearby sidechain.)


## License

Bandicoot inherits the **GNU General Public License v3** from upstream
Coot. The full text of the license is in [COPYING](COPYING).

The Bandicoot-specific patches are licensed under the same GPL v3
terms.

The binary tarball also bundles a number of third-party libraries,
tools, and fonts distributed under their own (GPL-compatible) licenses.
These are enumerated in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## Credits

Bandicoot builds on the work of Paul Emsley, Bernhard Lohkamp, Kevin
Cowtan, and the many other contributors to Coot. The macOS-native
patches were developed by Art Lyubimov for use within the Fraser Lab at
UCSF.

For the upstream Coot README, see [README.coot.md](README.coot.md).

## Funding

This work was supported by Radial (ROR: https://ror.org/050rbg919) as part of the DiffUSE project.

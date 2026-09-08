# Bandicoot

Bandicoot is a macOS-native fork of [Coot](https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/) 0.9.8.95, the structural biology macromolecular model-building program. It
keeps Coot 0.9's functionality while replacing the UI elements that stop the app from
functioning in MacOS Tahoe (26.x) on Apple Silicon, and adds some capabilities Coot 0.9
does not have.

**RELEASES**: Tarballs of binary releases are found at https://github.com/fraser-lab/bandicoot/releases

**NOTE:** Bandicoot targets **macOS Tahoe (26.x) on Apple Silicon**. It is not built or tested on other macOS releases, Linux, or Windows.

**NOTE** Bandicoot is a work in progress; users are welcome to log issues and issue pull requests.

## Why a fork?

The Crystallographic Object-Oriented Tool (Coot) has been the go-to suite of software for molecular modeling, used by thousands of structural biologists all over the world for over twenty years. Recently, Coot has been fully reimagined and redesigned, as well as giving the libraries and packages under the hood a much-needed upgrade. The resulting program (Coot 1) is in wide use today. However, many suites of software for structural biology rely on the old Coot 0.9 framework in order to function, making it necessary for both versions to remain in circulation. 

While Coot 0.9.8.95 is distributed alongside Coot 1 in suites such as CCP4, it has become completely unusable on the most recent MacOS version (26.x, Tahoe). Bandicoot, a fork of Coot 0.9.8.95, addresses each of these with macOS-specific fixes layered on top of upstream Coot. See [What changed vs Coot 0.9.8.95](#what-changed-vs-coot-09895) below for a list of major changes.

## Quick start

The easiest way to install Bandicoot is to download a binary tarball (https://github.com/fraser-lab/bandicoot/releases) and install it (see [INSTALL.md](INSTALL.md)): untar it anywhere and launch `<extracted>/bin/bcoot`.

To build from source instead, see [BUILD.md](BUILD.md). You'll need a
handful of Homebrew packages and a Miniconda environment that supplies
Clipper, MMDB2, FFTW2, and a few others.

## What changed vs Coot 0.9.8.95

The changes fall into two categories: adjustments made to enable Coot 0.9 to run on MacOS 26.x Tahoe and features new in Bandicoot (or expanded from their Coot 0.9 versions). 

(**NOTE**: Known problems and limitations are listed in [OUTSTANDING_ISSUES.md](OUTSTANDING_ISSUES.md)).

### MacOS 26.x Tahoe Adjustments

- **No XQuartz.** freeglut is removed entirely, so nothing starts an X11
  server.
- **Menu bar.** Coot's menu bar is mirrored into the system menu bar, and the
  in-window menu bar is hidden.
- **Toolbars render outside the GL layer.** The top toolbar is mirrored into a
  native toolbar on the window's title bar, and the vertical model toolbar is
  moved into its own window, because widgets over the GL backing layer do not
  draw.
- **Docked panels are native.** The docked Accept/Reject bar, sequence view and
  status bar are drawn as native panels over the content area. The GTK versions
  paint correctly into a buffer that is never composited, so they were
  invisible.
- **Atom labels and axes use native text.** The GLUT text paths that Coot 0.9
  relies on are broken here and are bypassed.
- **Retina-correct GL viewport and picking**, so the framebuffer fills the
  window and a click picks the atom under the pointer rather than an offset
  one.
- **Modifier keys are read from the window system directly**, because
  GTK-Quartz does not report them to the application. Option-click acts as the
  middle mouse button on trackpads.
- **Window behaviour.** Dialogs float freely and raise reliably, windows open
  near the pointer, and external-monitor resolution is handled.
- **Application identity.** Native Dock icon and Dock Quit handling, a working
  Exit menu item, and the binary is named so the app menu and Dock show
  Bandicoot rather than a wrapper script.

### New and Updated Features

#### Coot 0.9 functionality repaired, modernised or extended.

- **Python 3.** A full replacement of the Python 2 elements of Coot 0.9 with Python 3 code.
- **mmCIF handling rebuilt on gemmi.** All mmCIF reading and writing goes through [gemmi](https://github.com/project-gemmi/gemmi), with complete fidelity. The Header Browser shows real mmCIF metadata.
- **CIF files are classified by content.** Bandicoot now distinguishes coordinate vs. restraint mmCIF files if they are drag/dropped into the main window.
- **More files open:** modern small-molecule CIFs, and Phenix-produced CIFs can now be read in.
- **Extended wwPDB identifiers.** New-style entry IDs are fetched from the current archive. (Full compatibility with the new PDB archive will be implemented when that archive goes online in 2027.)
- **Configurable atom-pick radii**, separately for ordinary, symmetry and intermediate atoms.
- **Live MolProbity probe dots preview.** With "Interactive Dots" toggled on, moving atoms during real-space refinement previews clashes.
- **"Modelling" is made into its own top menu item**.
- **"Glyco" is now an item in "Modelling".** It is also always visible and accessible.
- **Ctrl-C in the launching terminal shuts the program down.** 

#### New in BANDICOOT

Capabilities with no Coot 0.9 equivalent.

- **Ligand restraints generation.** Restraints are generated for unrecognised
  ligands on load, through an external generator, and stored per molecule. A
  dialog lists the components it can describe and lets you exclude any of
  them; it is also reachable from Modelling -> Generate Ligand Restraints.
- **Colour by alternate conformation** is now available as an option in the Display Manager; this scheme assigns different colors to residues in alternate conformations. It is now the default bond scheme, with preferences for the scheme itself and for how far the alternate conformers differ in colour from the bulk model.
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

# Bandicoot — Outstanding Issues

Working tracker for known bugs / rough edges, roughly prioritized. This is
not a changelog and not a feature backlog — just things that are wrong and
worth fixing. Newest info at the top of each entry.

**Priority key**
- **P1** — correctness / data loss / crash / blocks a workflow
- **P2** — visible bug, but a workaround exists
- **P3** — cosmetic / minor / niche

---

## P2 — visible bug, workaround exists

### Ligand > Find Ligands crashes when it searches
With a map, a protein model and some ligand molecules loaded, choosing a ligand to search
for (one not already in the model) and starting the search crashes Bandicoot. Not yet
investigated, including whether Coot 0.9 does the same.

- **Workaround:** place the ligand by hand: open it, move it to the density, and fit it
  with Jiggle-Fit Ligand or real-space refinement.

### GitHub #19 — User-defined key bindings never work
`add_key_binding()` does `from types import IntType, StringType`
(`python/coot_utils.py:1972`). Both names were removed in Python 3, so **any** user key
binding fails with `ImportError`, including a correctly written one. Inherited from
Coot 0.9, whose Python layer is Python-2 flavoured throughout; the same pattern is at
`coot_utils.py:108, :483, :1581, :2035, :2306, :3165, :4485` and at a dozen sites in
`coot_gui.py`.

- **Related symptom, different cause:** a `~/.coot-preferences/*.py` copied from a
  Coot 0.8-era setup may contain Scheme rather than Python. Bandicoot has no guile and
  execs every `.py` in that directory as Python 3, so it prints a `SyntaxError`
  traceback at startup. The traceback is caught and startup continues; deleting the
  file removes it.
- **Workaround:** none for the bindings themselves. Coot's built-in C++ navigation keys
  are unaffected.

### Python-driven dialogs open nothing (GTK build)
`import gtk` resolves to a stub because PyGTK was never ported to Python 3. Widget code
runs, creates no window and raises nothing, so the affected menu items appear to do
nothing at all. In the GTK build this covers several hundred call sites across the Python
modules. The wx build replaces the windows its menus and tools reach with native ones
(including the wwPDB validation report after a PDB fetch, and the Probe flips list).

- **Workaround:** none in the GTK build. Dialogs ported to C++ (the Modelling menu, glyco,
  the scripting console, PanDDA Inspect, restraints) are unaffected.

### mmCIF export ignores the no-hydrogens and anisotropic options
`write_cif_file()` (`src/molecule-class-info.cc:7953`) writes hydrogens and ANISOU
records unconditionally, so those export options apply to PDB output only.

- **Workaround:** save as PDB if the options matter.

### Five-character ligand codes are truncated in header records
On reading an mmCIF that uses a 5-character CCD code, HETNAM / FORMUL / HELIX / SHEET
show only the first three characters (`coot-utils/gemmi-header.cc:926, :934,
:1357-1397`), while the ATOM records keep the full code. Coordinates are unaffected;
only the header display is wrong. Saving such a model as PDB is a separate matter and is
already guarded — the save warns and offers a rename rather than truncating silently.

### GitHub #28 — Side toolbar style is not preserved between sessions
Reported by alyubimov, 2026-08-28. Every other preference persists in
`~/.coot-preferences/coot_preferences.py`, but the model ("side") toolbar always comes back
as **Icons and Text**, whatever was last chosen from the sidebar's settings popup.

- **Root cause: the style state and the widget are wired in one direction only.**
  `set_model_toolbar_style()` (`src/c-interface-gui.cc:3162`) writes
  `graphics_info_t::model_toolbar_style_state` and then activates the matching check menu
  item, whose handler applies the style to the toolbar. But when a style is chosen directly
  from the sidebar popup, those handlers — `on_model_toolbar_icons_and_text1_activate`
  (`src/callbacks.c:10502`), `on_model_toolbar_icons1_activate` (`:10520`) and
  `on_model_toolbar_text1_activate` (`:10537`) — call `gtk_toolbar_set_style()` and **never
  write back** to `model_toolbar_style_state`. There is no widget → state path.
- **Why it is always "Icons and Text" specifically:** the state variable keeps its
  compiled-in default of `2` (`src/globjects.cc:235`), the preference writer stores that
  value (`src/graphics-info-preferences.cc:453`), and startup replays
  `set-model-toolbar-style 2`. The saved setting is therefore the default, never the choice.
- **Workaround:** set the style from **Preferences > Refinement Toolbar** rather than the
  sidebar popup. That path goes through `set_model_toolbar_style()`
  (`src/callbacks.c:7872/7885/7898`), so it updates the state and does persist.
- Inherited from Coot 0.9 rather than introduced here, but more visible in Bandicoot because
  the sidebar's own settings popup is the natural place to change the style.

---

## P3 — cosmetic / minor

### Stereo display modes do not work
Stereo display is not functional; session files that ask for a stereo mode open in mono.

### Ligand > Hydrogenate region crashes
Choosing the item crashes Bandicoot. Not yet investigated, including whether Coot 0.9
does the same.

- **Workaround:** Modelling > Add Hydrogen Atoms adds hydrogens to the active residue's
  molecule.

### Restoring an older session warns "No matching molecules found" for its dictionaries
Session files saved by earlier builds read their restraint dictionaries before their
molecules, so each dictionary is checked against an empty session and raises a "No
matching molecules found for <file>" message, one per dictionary read. The dictionaries
are still read, and apply once the molecules arrive. Sessions saved now read molecules
first, and generated dictionaries are kept in `coot-restraints` rather than in a
temporary folder the system may clear.

- **Workaround:** dismiss the messages, then save the session again; the new session
  file restores cleanly.

### Simple Mutate on a nucleotide does nothing for the molecule's first atom
`do_base_mutation()` (`src/c-interface-build-side-chain.cc`) accepts the picked atom only
if its index is `> 0`, so when the atom clicked is the first atom of the molecule the
base chooser opens but choosing a base changes nothing, silently. Inherited from Coot 0.9.

- **Workaround:** click any other atom of the nucleotide.

### Opening data without map coefficients makes no map
An MTZ file without phases, opened with File > Open MTZ, mmCIF, fcf or phs... or with
Auto Open MTZ, goes to Coot's calculate-phases route, which asks for a model through a
Python dialog that was never ported, so nothing happens. An mmCIF file holding observed
structure factors only is read, found to have no calculated amplitudes, and left with a
warning on the terminal. Both inherited from Coot 0.9.

- **Workaround:** open the MTZ file written by refinement (it carries map coefficients),
  or refine first with Run Refmac.

### Some data files do nothing when opened as a dataset
File > Open MTZ, mmCIF, fcf or phs... offers `.mmcif`, `.cif.gz` and CNS files, but only
`.mtz`, `.cif`, `.fcf`, `.phs` and `.pha` are read; the others are passed over silently.
Coot 0.9's CNS route here is disabled. Inherited from Coot 0.9.

- **Workaround:** rename `.mmcif` to `.cif`, uncompress `.cif.gz`, and open CNS map
  coefficients with Auto Open MTZ.

### Search Monomer Library ignores "Include Monomers with Minimal Descriptions"
The check box has no effect: the search treats every monomer the same either way.
Inherited from Coot 0.9.

- **Workaround:** none needed; the results are the same.

### Generated restraints depend on the quality of the input geometry
elbow's target distances partly track the input coordinates, so restraints derived from
an unrefined ligand can carry targets a ring cannot satisfy, and real-space refinement
can then distort it. Generation runs elbow with `--opt`, which is five times more stable
on aromatic bonds, and falls back to plain elbow when `--opt` fails to converge — that
fallback is where poor targets can still appear.

- **A second trigger for the fallback, inside elbow itself:** with Phenix 2.0, an `--opt`
  run that ends "geometry not converged" can then crash in elbow's own
  `optimisation_manager` (`xyz_parser.movie`, `IndexError` on an empty `.elbow_opt.xyz`).
  elbow exits non-zero, the failure is detected, and the plain-elbow retry produces the
  dictionary — so the console shows an elbow traceback even though generation succeeds.
  Outside Bandicoot; whether a different elbow option avoids it is not yet known.
- **Workaround:** import a canonical dictionary if one exists, or regenerate after the
  geometry has been improved.

### 2D ligand view and ligand-interaction (FLEV) diagrams are unavailable
Both are present in the source but never draw: the enhanced ligand tools they depend on
are a compile-time option that is off, so their setup returns false and nothing is
rendered. Not a display bug. GitHub #11.

### Toolbar icons missing when the install script has not been run
The SVG icon loader is the only external image loader shipped, and its cache holds
absolute paths regenerated per install. If the bundled `setup.sh` was not run, all 23 SVG
toolbar icons are blank while the buttons still work and still show tooltips. The
application otherwise launches normally, so this does not present as an install failure.

- **Fix:** run `./setup.sh` from the unpacked tarball.

### GitHub #15 — Model toolbar cannot be repositioned (partially addressed)
Reported by alyubimov, 2026-08-06. Coot 0.9 lets the docked model toolbar sit on any
edge of the rendering window; in Bandicoot it is fixed in place (on the **right**, not
the left as the issue text says — `bandicoot_reposition_sidebar()` pins it to
`NSMaxX(usable) - width`).

- **Root cause:** repositioning works by reparenting the legacy
  `model_fit_refine_toolbar_handlebox` between the main window's frames. Bandicoot lifts
  the `GtkToolbar` OUT of that handlebox into the native pinned sidebar
  (`gtk_widget_reparent` in `bandicoot_sidebar_install`, `src/bandicoot_appkit.mm`), so
  every position control moves an empty container between hidden frames.
- **DONE 2026-08-06:** the inert right-click popup is removed — right-click on the
  toolbar now does nothing (`on_model_toolbar_button_press_event`). `toolbar_popup_menu()`
  and `set_model_toolbar_docked_position_callback()` were deleted; the popup also cast the
  toolbar's parent to `GtkHandleBox` when it is a `GtkVBox`. Dock/undock and toolbar style
  remain on the sidebar's own settings popup.
- **STILL OPEN — the same dead UI in Preferences.** `Preferences > Refinement Toolbar`
  still offers a Model Toolbar Position radio group (`src/callbacks.c:7765-7800`) calling
  the same `set_model_toolbar_docked_position()`, and it is equally inert. **Decision
  (2026-08-06): leave it.** If a user reports it we explain or remove it then; the wx
  interface rework may land first, and it is unclear how many users move the toolbar at
  all.
- **Actual repositioning is deferred to the wx interface work.** Supporting left/right on
  the native sidebar is feasible (a persisted side + x computed in
  `bandicoot_reposition_sidebar()`), but the A/R bar and docked sequence view both inset
  themselves from the sidebar's edge, so it is three pinned windows moving together.
  Top/bottom would additionally need the toolbar re-oriented and would compete with the
  sequence view for the top edge.

### Ball & Stick aromatic-ring toruses not drawing
_(pre-existing, from the v0.1.4.11 bookmark — needs re-confirmation/detail)_
Aromatic-ring torus annotations in the Ball & Stick representation don't draw.

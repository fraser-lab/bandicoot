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

### Python-driven dialogs open nothing
`import gtk` resolves to a stub (`python/coot_load_modules.py.in:163`) because PyGTK was
never ported to Python 3. Widget code runs, creates no window and raises nothing, so the
affected menu items appear to do nothing at all. Around 700 call sites across 8 Python
files are in this state; the wwPDB validation chart is a measured example — it downloads
and parses correctly and then has nowhere to draw.

- **Workaround:** none. Dialogs ported to C++ (the Modelling menu, glyco, the scripting
  console, PanDDA Inspect, restraints) are unaffected.

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

### Restraints dialog stays open after generating, when opened from the menu
Opened from **Modelling > Generate Ligand Restraints**, the dialog remains on screen
after **Generate Restraints** has finished. Opened automatically on a coordinate load it
closes as expected.

- **Root cause:** the dialog is destroyed after a run only when no rows remain
  (`src/restraints-gui.cc:646`). The menu opens it in show-all mode, which lists every
  ligand whether or not it already has restraints, so the row count is never zero. The
  rule was written for the load-time mode, where rows are only what is missing.
- **Workaround:** press Close.

### Generated restraints depend on the quality of the input geometry
elbow's target distances partly track the input coordinates, so restraints derived from
an unrefined ligand can carry targets a ring cannot satisfy, and real-space refinement
can then distort it. Generation runs elbow with `--opt`, which is five times more stable
on aromatic bonds, and falls back to plain elbow when `--opt` fails to converge — that
fallback is where poor targets can still appear.

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

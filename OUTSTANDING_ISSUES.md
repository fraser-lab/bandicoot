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

### GitHub #16 — Sequence view: docked viewer obscures the model, and right-click close leaves a ghost window
Reported by jmsprovan, 2026-08-06. Two separate problems in the docked sequence viewer:

1. **It covers the model.** The viewer docks across the top of the rendering window
   and can be neither scaled nor moved, so with a many-chain model (~10 chains) it
   hides roughly half the structure.
2. **Right-click > Close leaves an empty viewer window** at the original size (Coot
   muscle memory reaches for this). Only the embedded close button on the right-hand
   side removes the docked viewer properly.

- **Where to look:** docking is Bandicoot-native, not a GtkPaned — the sequence view is
  built as a top-level window and then pinned as a child of the main window at the top
  edge by `bandicoot_dock_sequence_view()`, fired from a one-shot idle in the `nsv`
  constructor (`src/nsv.cc:59-70`). Being an NSWindow child pinned to an edge is
  precisely why it can't be resized or dragged. The `#ifdef __APPLE__` float/dock
  decision and `make_top_level_dialog` live at `src/nsv.cc:144-145`.
- The ghost window in (2) suggests the right-click close path destroys the canvas
  contents but not the pinned child window, or unpins without destroying.
- **Workaround:** turn off the "Dock Sequence View Dialog?" preference so it floats, and
  close it with the embedded button rather than right-click.
- **Status:** reported, not yet investigated at runtime.

### GitHub #14 — Docked Accept/Reject bar hides the map contour string
Reported by alyubimov, 2026-08-06. The docked bars are overlaid on the rendering area,
so the Accept/Reject bar covers whatever is drawn beneath it. The structure can simply
be moved out from under it, but the map contour readout cannot — it is pinned. Worst in
"always show" mode, where the bar is permanently in the way.

- **Root cause (source-located):** the contour string is drawn at a hard-coded
  normalized screen position — `printString_for_density_level(..., 0.0, 0.95, -0.98)`
  at `src/draw.cc:635` — i.e. fixed at top-centre, exactly where the docked
  Accept/Reject bar sits. The string itself is built in `graphics-info.cc:3542`.
- Fix directions: move the readout when the docked bar is shown, or make its position
  configurable, or inset the GL viewport under the docked bar so nothing is overlaid.
- **Workaround:** don't run Accept/Reject in "always show" mode.
- **Status:** reported, root cause located in source, not yet fixed.

---

## P3 — cosmetic / minor

### GitHub #15 — Docked model toolbar cannot be repositioned; its popup menu does nothing
Reported by alyubimov, 2026-08-06. Coot 0.9 lets the docked model toolbar sit on any
edge of the rendering window; in Bandicoot it is fixed to the left. The right-click
popup that used to reposition it is still present but inert.

- Low impact — left is where most users want it anyway — but the popup is a dead UI
  element, which is worse than not offering it.
- **Where to look:** `set_model_toolbar_docked_position(0/1/2)` is still wired to the
  popup items (`src/callbacks.c:7773-7799`), and `model_toolbar_position_state` still
  exists (`src/globjects.cc:230`, default RIGHT). But that variable is the **legacy
  handlebox** position, and the native sidebar that replaced the handlebox does not read
  it — see the note at `src/c-interface-preferences.cc:130-135`. So the menu items set a
  value nothing acts on.
- **Status:** reported, root cause located in source, not yet fixed.

### Ball & Stick aromatic-ring toruses not drawing
_(pre-existing, from the v0.1.4.11 bookmark — needs re-confirmation/detail)_
Aromatic-ring torus annotations in the Ball & Stick representation don't draw.

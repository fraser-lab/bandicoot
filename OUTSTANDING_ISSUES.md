# Bandicoot — Outstanding Issues

Working tracker for known bugs / rough edges, roughly prioritized. This is
not a changelog and not a feature backlog — just things that are wrong and
worth fixing. Newest info at the top of each entry.

**Priority key**
- **P1** — correctness / data loss / crash / blocks a workflow
- **P2** — visible bug, but a workaround exists
- **P3** — cosmetic / minor / niche

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

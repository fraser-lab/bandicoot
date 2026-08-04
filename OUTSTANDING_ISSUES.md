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

### Hydrogen "star" duplication in Colour-by-Chain
Toggling hydrogen display off in **Colour by Atom** (a few toggles, ending
OFF) then switching to **Colour by Chain** makes hydrogens appear as
non-bonded grey star/cross atoms even though H display is OFF. Toggling H back
on then draws the normal bonded grey H sticks **and** leaves the stars
(duplication). Only affects the Colour-by-Atom ↔ Colour-by-Chain
representations.
- **Root cause (confirmed):** In Colour-by-Chain the visible crosses come from
  the non-bonded "star" loop in
  `do_colour_by_dictionary_and_by_chain_bonds_carbons_only`
  (`coords/Bond_lines.cc:6364-6395`). It decides "non-bonded" by reading the
  `"found bond"` UDD, but the chain bond builders never set those marks, so it
  reads **stale marks left over from the last *atom-mode* build** — and it has
  **no hydrogen guard** (never consults `draw_hydrogens_flag`). When the last
  atom-mode build had H OFF, H atoms are left `NO_BOND`, so the chain path draws
  a grey cross for each → stars while H display is off. Toggling H back on adds
  bonded H sticks but the stale marks still yield crosses → sticks+stars
  duplication. (The `add_atom_centres` path at `:6858` is **dead code** —
  short-circuited at `:6601` — so it and `bonds_box_type` are red herrings.)
- **Fix (ready, minimal):** guard the star loop against hydrogens — at
  `coords/Bond_lines.cc:6367`, first line inside `if (ic == NO_BOND) {`, add:
  `if (is_hydrogen(std::string(at->element))) continue;`
  Fixes both symptoms; heavy non-bonded atoms (waters, ions) still get crosses
  (they're never stale-`NO_BOND` the way toggled hydrogens are).
- **Fix alongside (leak):** `make_colour_by_chain_bonds`
  (`src/molecule-class-info.cc:3579-3582`) builds `bonds_box` twice (leaks the
  first) and omits `bonds_box.clear_up()`. Add `clear_up()` before assigning and
  drop the redundant second build. (Leak only — not the star cause.)
- **Workaround:** switch to Colour by Chain with hydrogens ON, then toggle off.
- **Status:** diagnosed, fix ready to apply; deferred (P2).

### 2D ligand-view overlay not drawing
_(pre-existing, from the v0.1.4.11 bookmark — needs re-confirmation/detail)_
The in-window 2D ligand-view overlay does not render.

---

## P3 — cosmetic / minor

### Ball & Stick aromatic-ring toruses not drawing
_(pre-existing, from the v0.1.4.11 bookmark — needs re-confirmation/detail)_
Aromatic-ring torus annotations in the Ball & Stick representation don't draw.

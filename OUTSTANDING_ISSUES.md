# Bandicoot — Outstanding Issues

Working tracker for known bugs / rough edges, roughly prioritized. This is
not a changelog and not a feature backlog — just things that are wrong and
worth fixing. Newest info at the top of each entry.

**Priority key**
- **P1** — correctness / data loss / crash / blocks a workflow
- **P2** — visible bug, but a workaround exists
- **P3** — cosmetic / minor / niche

---

## P1 — blocks a workflow

### GitHub #9 — phenix.refine mmCIF fails to load (mmdb2 demands PDB-only CIF items)
Bandicoot cannot read a `phenix.refine`-produced `.cif`:

```
INFO:: Command: handle_read_draw_molecule_with_recentre ("…/SC1_2_refine_036.cif", 0)
INFO:: Reading coordinate file: …/SC1_2_refine_036.cif
ERROR:: failed to get cell
INFO:: There was an error reading …/SC1_2_refine_036.cif.
INFO:: read error 23 READ: Expected data field not found.
       CIF ITEM: structure _cell.z_pdb data [NULL]

Spacegroup: P 21 21 21
There was a coordinates read error
```

- **ROOT CAUSE — the presence of a `_struct_ncs_oper` category**, which
  phenix.refine writes whenever NCS restraints were used. mmdb2's mmCIF reader
  cannot cope with it and fails the entire read. Isolated by bisecting the 21
  CIF categories that a working PDBTools file lacks: removing `_struct_ncs_oper`
  **alone** makes the file load, and all 20 other single removals still fail.
  **Necessary and sufficient, verified in both directions** — grafting that one
  category into the PDBTools file that loads makes it fail identically.
- **The reported `_cell.z_pdb` item is MISLEADING; don't chase it.** `_cell.Z_PDB`
  is a PDB-specific mmCIF extension phenix never writes, and its absence is
  harmless on its own — the working PDBTools file has no `Z_PDB` either (0
  occurrences in both). `mmdb::NCSMatrix` lives in the same `mmdb::Cryst`
  container as the cell (`mmdb_cryst.h`), and `_struct_ncs_oper` appears to
  "activate" that container, whereupon mmdb2 demands a whole series of PDB-only
  items the file lacks: supply `Z_PDB` and the error merely advances to
  `_struct_conf.ndb_helix_class_pdb`, then to further unnamed items. Patching in
  missing tags is a dead end.
- **Ruled out:** the `_cell.*` blocks of the working and failing files are
  identical; `_struct_conf.*` tag sets are identical; two `data_` blocks
  (`data_comp_CL`) is not it (removing the second block doesn't help); non-integer
  `_struct_ncs_oper.id` (`op_1`) is not it; `code`=`given` is not it; the
  `matrix[1][1]` bracket spelling matches what libmmdb2 itself contains.
- **`ERROR:: failed to get cell` is a RED HERRING** — it comes from a *different*
  reader: `coot-utils/read-sm-cif.cc:101`, the **small-molecule** (SHELX/CCDC)
  CIF fallback, which cannot succeed on an mmCIF because it looks for SM-CIF
  `_cell_length_a` while phenix writes `_cell.length_a`.
- **FIX APPLIED** in `coot-utils/atom-selection-container.cc`: when
  `ReadCoorFile` fails on a `.cif`/`.mmcif`/`.mcif`, retry once against a temp
  copy with `_struct_ncs_oper` stripped (new static helper
  `bandicoot_cif_copy_without_category`). Tried **before** the small-molecule
  fallback so the misleading "failed to get cell" no longer appears. The helper
  returns "" when the category isn't present, so files that never had the
  problem — including genuine small-molecule CIFs — take no extra read.
  Discarding the operators is free: Coot derives NCS ghosts by chain matching,
  not from the file ("NCS found from matching Chain B onto Chain A" either way).
  mmdb2 is a prebuilt conda dependency here, so fixing it upstream wasn't an
  option; `MMDBF_IgnoreNonCoorPDBErrors` is already set and does not help.
- **Verified:** both `SC1_2_refine_036.cif` (as reported) and
  `SC1_2_refine_031.cif` now load; the PDBTools file and the `.pdb` sibling
  still load (no regression); the retry message fires only for the 2 files that
  need it. Content is intact — the CIF-loaded model and the `.pdb` sibling both
  give **6 chains / 17962 atoms**.
- **Status:** **FIXED and CONFIRMED** (2026-08-03), unreleased. Validated in a real
  `build.sh` install: `SC1_2_refine_031.cif` loads with 6 chains, the retry path is
  genuinely exercised (the "dropping its _struct_ncs_oper records" message fires),
  and Art confirmed in the GUI that maps load and Sphere Refine runs against the
  loaded model. Note an earlier GUI attempt using `Refine_38/SC1_2_refine_038.cif`
  proved nothing — that file contains **no** `_struct_ncs_oper` (0 occurrences vs
  15 in refine_036), so it loads with or without the fix. **Use a file that
  actually contains the category when regression-testing this.**

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

### GitHub #10 — Sidebar settings menu: wrong state shown, and clipped near screen bottom
Two related sidebar/model-toolbar rough edges, filed together.

1. **Menu state doesn't mirror the sidebar.** The sidebar loads in **Main Icons**
   state, but its settings menu shows **All Icons** as the selected radio item.
   The menu should reflect the actual sidebar state.
   - **Likely root cause (source-located, not yet runtime-confirmed):**
     `src/gtk2-interface.c:2212` unconditionally calls
     `gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (model_toolbar_all_icons), TRUE)`
     as the menu is constructed — i.e. "All Icons" is hard-coded active at build
     time, independent of the toolbar's real state, and nothing appears to
     re-sync it afterwards. The radio group is
     `model_toolbar_main_icons` / `model_toolbar_all_icons` /
     `model_toolbar_user_defined1` (`gtk2-interface.c:2203-2217`); the state
     setters live at `src/c-interface-gui.cc:3007` (`show_model_toolbar_all_icons`,
     `show_model_toolbar_main_icons`).
   - Fix direction: set the active item from the current toolbar state when the
     menu is built/shown, rather than hard-coding it.

2. **Menu is compressed when it pops up near the bottom of the screen** (P3,
   cosmetic). Instead of shrinking/scrolling, it should reposition so it opens
   fully. Not investigated.

- **Status:** (1) source-located, unconfirmed at runtime; (2) not investigated.

### 2D ligand-view overlay not drawing
_(pre-existing, from the v0.1.4.11 bookmark — needs re-confirmation/detail)_
The in-window 2D ligand-view overlay does not render.

---

## P3 — cosmetic / minor

### Ball & Stick aromatic-ring toruses not drawing
_(pre-existing, from the v0.1.4.11 bookmark — needs re-confirmation/detail)_
Aromatic-ring torus annotations in the Ball & Stick representation don't draw.

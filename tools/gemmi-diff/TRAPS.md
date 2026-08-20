# Coordinate-file traps, and which of them we actually check

A catalogue of file properties and API behaviours that look ordinary and are not.
Every entry here cost real debugging time at least once, or was found by deliberate
search after a near-miss.

## Why this file exists

The same lesson has been learned three separate times on this project:

> **A clean diff is only clean on the axes it measures.**

- The Phase 1 read-side gate passed with "~250,000 atoms, ZERO differences" — and was
  structurally blind to metadata, because it only ever compared atoms.
- `mmdb::Root::GetCell` returns **1 on success and 0 on failure**, the opposite of the
  usual convention. The harness tested `== 0`, so for weeks every cell-less file was
  reported as having a cell and vice versa.
- A code audit of `copy_to_mmdb` catalogued a long list of "lost" metadata. Measurement
  later showed most of it was never read from mmCIF by mmdb either — the audit named
  what *could* be lost, not what *was*.

The remedy is not a longer checklist for a human to read. It is **assertions in the
harness that fail**. This file is the specification for those assertions, and it names
the gaps where an assertion does not exist yet, so that a gap is visible rather than
merely absent.

**Run the catalogue after each phase.** That is what makes it worth maintaining.

## The corpus

Lives **outside the repository**, at `~/sw/bandicoot-project/samples` — deliberately
(Art, 2026-08-12): these are large binary-ish files that would bloat git history
permanently and have no place in a shipped release. The harness is in the repo; its
inputs are not.

Consequence to be honest about: **a clean checkout cannot run the full catalogue.**
Most entries are PDB depositions and can be re-fetched by ID (1AON, 1FFK, 2RSF, 3K0N,
3NYD, 5E1N, 6DMH). Three groups cannot: the `SC1_2_refine_*` phenix outputs, the
`*_hierarchy.cif` files from the encoding partner, and `4517425.cif` from the COD.

Suggested follow-up, not yet done: default the harness's corpus path to
`$REPO/../samples` (which is exactly where it lives) and add a `--corpus DIR` flag, so
running the catalogue is one command rather than a remembered file list.

---

## A. File-shape traps

### A1. Multi-block mmCIF
**Property:** more than one `data_` block in one file.
**Exhibited by:** `SC1_2_refine_031.cif`, `SC1_2_refine_036.cif` — block 1 is
`data_SC1_2_refine` (the structure), block 2 is `data_comp_CL`, a monomer restraint
dictionary for the chloride ion that phenix.refine embedded rather than shipping
separately. PDB deposition files do the same with restraints.
**Rule:** gemmi's `make_structure()` **fails** if two or more blocks carry `_atom_site`,
so `blocks[0]` is always the coordinates and everything after it is something else.
**Assertion:** UNCHECKED (read side retains all blocks correctly — verified by hand
2026-08-12: 2 blocks retained).
**Breaks if regressed:** the writer targeting "the block" instead of `blocks[0]` writes
coordinates and `_struct_conn` into the middle of a ligand restraint dictionary. Looks
fine on every single-block file in the corpus.

### A2. No unit cell at all
**Property:** no `_cell.*` / `_symmetry.*` tags. Cell and space group are
crystallographic concepts; **NMR ensembles and EM models legitimately have neither.**
**Exhibited by:** `2RSF.cif` (NMR, 20 models).
**Rule:** `copy_to_mmdb` calls `PutCell` **unconditionally**, and gemmi's default
`UnitCell` is `1,1,1,90,90,90` — so a cell-less model silently acquires a fake 1 Å cubic
cell. Adjustment (4) in `gemmi-coords.cc` puts it back.
**Assertion:** CHECKED (harness compares cell presence on both paths).
**Breaks if regressed:** `have_unit_cell` resolves to 1, Coot's deliberate
non-crystallographic path never engages, and the user is offered symmetry for a model
that has none.

### A3. A PDB sibling of a cell-less file still has a CRYST1
**Property:** PDB format has no way to say "no cell", so converters emit a placeholder.
**Exhibited by:** `2RSF.pdb` — `CRYST1 1.000 1.000 1.000 90.00 90.00 90.00 P 1`.
**Assertion:** CHECKED, and it fires: the harness reports
`DIFF spacegroup A="" B="P 1"` on `2RSF.pdb`. mmdb leaves the space group unset;
**gemmi accepts the placeholder `P 1` from the CRYST1 record.**
**Breaks if regressed:** nothing directly — but it makes **PDB-vs-mmCIF comparison
asymmetric by construction** for NMR/EM models, on cell *and* space group. Any future
test expecting the two formats of one structure to agree here will report a false
difference. Know it before writing that test.

### A4. Multiple models
**Property:** an ensemble; `_atom_site.pdbx_PDB_model_num` takes many values.
**Exhibited by:** `2RSF.cif` / `2RSF.pdb` (20 models).
**Assertion:** CHECKED for model count.
**Watch:** gemmi's `copy_model_from_mmdb` calls `ensure_entities` and loops over **all**
of `st.models` from inside a per-model call. Not yet examined for a 20-model file on the
**write** path. UNCHECKED there.

### A5. Small-molecule CIF
**Property:** a valid CIF that is not a macromolecular coordinate file.
**Exhibited by:** `4517425.cif` (COD).
**Rule:** gemmi **does not throw** on these — it returns a `Structure` with **zero
atoms**. That is why `read_coords_with_gemmi` treats "no atoms" as a fall-back condition
rather than a success.
**Assertion:** UNCHECKED in the harness.
**Breaks if regressed:** opening a small-molecule CIF produces an empty molecule with no
error at all — the exact bug that was fixed once already.

### A5b. The symmetry-operator loop is OPTIONAL in a small-molecule CIF — FIXED 2026-08-19
**Property:** in the CIF core dictionary a space-group **name** or **International Tables
number** fully specifies the group; the operator loop need not be there at all.
**What it cost:** `read-sm-cif.cc` read its atom loop *inside* the operator-loop branch, so such
a file loaded **nothing** — cell parsed, 31 readable atoms in hand, whole file rejected. Measured
on the real COD file with only its operator loop removed. **Same shape as the
`_symmetry_equiv_pos_as_xyz` bug**: that fix added the missing spelling and left the nesting, so
the next surprise failed identically.
**Fixed two ways, and the structural half is the one that matters:** coordinates no longer depend
on the symmetry resolving (an unknown spelling now costs the symmetry, not the molecule — Coot has
a non-crystallographic path already), plus a name/number fallback.
**⚠ The fallback order is a correctness matter, not a preference:** Hall symbol (unambiguous),
then H-M name (carries the setting), then IT number **last and with a warning** — the number does
NOT fix the setting, since `P 1 21/c 1` and `P 1 21/n 1` are both 14.
**Resolution goes through gemmi, never clipper** — see D2: clipper silently turns `P212121` into
P 1 with one operator.

### A5c. A chemical-component definition is a ".cif" with no `_atom_site` — FIXED 2026-08-19
**Property:** a ligand downloaded from the PDB (`files.rcsb.org/ligands/download/AR6.cif`) is a
CCD entry: its coordinates live in `_chem_comp_atom.model_Cartn_*` and
`pdbx_model_Cartn_*_ideal`, and there is no `_atom_site` anywhere. A Refmac monomer-library file
(`comp_list` blocks) has the same shape with `_chem_comp_atom.x`.
**Exhibited by:** `AR6.cif`, `ADP.cif`.
**What it cost:** the coordinate reader found no `_atom_site`, fell through to the small-molecule
reader, which found no `_atom_site_fract_*` either, and produced an EMPTY molecule -- "loads but
shows nothing". Dropping the same file classified it as restraints and silently imported a
dictionary, so the drop counted as handled and nothing appeared.
**⚠ gemmi's DEFAULT `which=7` IS WRONG HERE.** `make_structure_from_chemcomp_block(block, which)`
takes a bitmask -- 1 = Refmac `x/y/z`, 2 = `model_Cartn` ("example"), 4 = `pdbx_model_Cartn_ideal`
-- and a CCD carries TWO of them, so the default returns a **TWO-MODEL, 118-atom** molecule for
AR6's 59 atoms. Choose one: this reader tries Ideal, then Example, then Xyz, matching Coot's own
Get Monomer, which asks the dictionary for idealised coordinates first.
**Also: `check_chemcomp_block_number()` is the detector** -- it returns the block index for both
CCD and monomer-library shapes, and -1 otherwise.
**The document is deliberately NOT retained** for these files: `update_mmcif_block()` would edit
`_atom_site` categories that are not there. That makes reading one and writing mmCIF a
CROSS-FORMAT conversion, which is why the harness now classifies chem_comp inputs with the PDB
inputs (`is_coordinate_mmcif()`) instead of asking mmCIF-preservation questions of them.

### A6. SHELX `.ins` / `.res`
**Property:** not a format gemmi reads; it throws `Unknown format`.
**Exhibited by:** `F1-Cu-8_anom_diffs.ins`.
**Assertion:** UNCHECKED.
**Breaks if regressed:** the SHELX branch is bypassed and the file fails to open.

### A7. Compression decides which reader runs — FIXED 2026-08-13
**Property:** a `.cif.gz` file.
**Rule:** `coot::util::file_name_extension` returns everything after the **last** dot, so
`foo.cif.gz` has extension `.gz`, `gemmi_handles_extension` is false, and **mmdb reads
it** — gemmi never sees it. Every compressed backup in `coot-backup/` is therefore
invisible to the gemmi path, and `backup_compress_files_flag` defaults to **1**.
**Assertion:** UNCHECKED, but **✅ THE DEFECT IS FIXED (2026-08-12).**
`coot::gemmi_handles_file()` looks beneath a trailing `.gz`, and the read path uses
`gemmi::read_structure_gz()`. Note `gemmi_handles_extension()` could NOT be fixed in
place: `file_name_extension()` returns only `.gz`, so the `.cif` never reaches it —
the decision has to be made from the filename.
**Why it stopped being cosmetic:** the Phase 3 writer preserves `_struct_ncs_oper`,
which **mmdb2 cannot read** (B8). Backups are `*.cif.gz` and Undo re-reads them, so
while `.gz` routed to mmdb, **preserving faithfully BROKE UNDO** for every
NCS-containing mmCIF. mmdb could read its own backups only because its writer threw
those categories away. Measured on `SC1_2_refine_036`: preserved backup carries
`_struct_ncs_oper` ×15, `mmdb ReadCoorFile` fails.
**⚠ THE WRITE-SIDE TWIN IS STILL OPEN:** `coot::is_mmcif_filename()` uses the same
`find_last_of(".")`, so `save_coordinates(imol, "x.cif.gz")` writes a **PDB file**.
Pre-existing; not caused by the Phase 3 work.

---

## B. Model-content traps

### B1. One author chain, several gemmi chains
**Property:** an author chain holding polymer + ligands + waters.
**Exhibited by:** `pdb3nyd.ent` (778 of 6443 atoms unreachable before the fix), `1AON`,
`1FFK`.
**Rule:** `copy_to_mmdb` calls `CreateChain()` per gemmi chain and **never merges**, so
several mmdb chains end up with the *same* id — and `Model::GetChain(id)` returns only
the first. Adjustment (1) calls `st.merge_chain_parts()` (after `setup_entities`).
**Assertion:** CHECKED — chain id sets **and a reachability post-condition**.
**Breaks if regressed:** 12% of a structure becomes invisible to every by-name lookup,
with no error anywhere.

### B2. Atom-name padding for short hydrogen names
**Property:** a SHORT atom name whose element is H. Originally recorded as "3-character"
(`HH1`, `HH2`, any `H??`); **`2GEW.cif` widened it 2026-08-17 -- the 2-character `HH` is
affected too** (`"HH  "` -> `" HH "`, 14 atoms), alongside 10 of `"HH2 "` -> `" HH2"`. So
the rule is about the element being hydrogen, not about a particular length.
**Exhibited by:** `3K0N.cif` (1 atom), `6DMH.cif` (6), `2RSF.cif` (60), `2GEW.cif` (24).
**Rule:** mmdb yields `"HH2 "`, gemmi yields `" HH2"`. **gemmi is right** — the PDB file's
own columns say `" HH2"`, and both readers agree when reading PDB. Today's mmdb path
names the same atom differently depending on input format.
**Assertion:** CHECKED (per-atom name comparison; this is a known, accepted difference).
**Breaks if regressed:** dictionary and restraint lookup by 4-character atom name.

### B3. LINK atom names arrive unpadded
**Property:** any file with `_struct_conn`.
**Exhibited by:** `5E1N.cif` (130 connections).
**Rule:** `gemmi::transfer_links_to_mmdb` copies `struct_conn` atom names **verbatim**, so
a link says `"C"` while the atom is stored `" C  "`. Adjustment (5) rewrites each field
from the **found atom's own spelling**.
**Assertion:** CHECKED — the link post-condition replicates
`add_link_bond_templ`'s exact matcher.
**Breaks if regressed:** every link is silently inert — 0 of 130 matched. No link bonds
drawn, no metal restraints applied, and **nothing reports an error**.
**Method rule this taught:** *verify a match with the CONSUMER's matcher, never with a
convenient API.* `mmdb::Residue::GetAtom()` pads internally and reported 130/130
"resolved" while the real consumer matched none.

### B4. mmdb writes blank single-character fields as a quoted space
**Property:** any mmCIF written by mmdb (i.e. every backup made before the fix).
**Rule:** mmdb emits `" "` rather than an mmCIF null (`?`/`.`) for
`pdbx_ptnr*_PDB_ins_code` and `pdbx_ptnr*_auth_alt_id`, and reads it back as a
one-character string, while the model carries `""`. Any exact `std::string ==` fails.
`coot::util::normalise_link_blank_fields()` fixes it at the source.
**Assertion:** CHECKED (same link post-condition as B3; path A calls the normaliser).
**Breaks if regressed:** links vanish after Undo, and sphere refine silently reverts to
unrestrained distances — the two symptoms are the same bug.

### B5. Hydrogen bonds must not become refinement restraints
**Property:** `_struct_conn` rows with `conn_type_id = hydrog`.
**Exhibited by:** `SC1_2_refine_036.cif` (660), `SC1_2_refine_031.cif` (659).
**Rule:** `transfer_links_to_mmdb` ignores `con.type` and `fill_links()` pushes every mmdb
LINK into the refinement unfiltered. An H-bond sits at ~2.9 Å; a covalent link restraint
pulls toward ~1.4 Å. Adjustment (2) filters `Hydrog` before `copy_to_mmdb`.
**Assertion:** CHECKED (link counts).
**Note:** the filter is restraint-side only. `struct_conn` is preserved **verbatim** on
write, hydrogens included — the read filter's comment promises exactly that, and D1's
`conn` = PASS is what keeps the promise.

### B6. Insertion codes
**Property:** residues with a non-blank insertion code.
**Exhibited by:** `6DMH.pdb` — **22 atoms**. (Found 2026-08-12; this had been listed as an
uncovered gap and is not one.)
**Assertion:** CHECKED per-atom.
**Gap:** whether any **LINK record** in the corpus references an insertion-coded residue
is NOT established. B4's fix was only ever exercised against blank codes. `6DMH` has both
insertion codes and links and is the file to check.

### B7. TER records live in mmdb's atom table
**Property:** any polymer.
**Rule:** `GetNumberOfAtoms()` counts TER entries, so gemmi's 3170 atoms become mmdb's
3171. Harmless downstream — `make_asc`'s selection excludes TER (measured: 58,870 real +
21 TER in the hierarchy, 0 TER in the selection) — but **every atom-count comparison must
exclude `mmdb::Atom::Ter`** or each chain shows a spurious off-by-one.
**Assertion:** CHECKED (counted and reported separately).

### B8. `_struct_ncs_oper` defeats the mmdb reader entirely
**Property:** the category written whenever NCS operators are present.
**Exhibited by:** `SC1_2_refine_031.cif`, `SC1_2_refine_036.cif` — **and `1AON.cif`
(×15), found 2026-08-12 when the harness was first run against the full corpus.**
**This matters more than it looks:** 1AON is a **genuine wwPDB deposition**, so GitHub #9
is not a phenix quirk — it is *any* mmCIF carrying NCS operators, including files
downloaded straight from the PDB. Every previous example had been phenix output, which
made the bug look narrower than it is.
**Rule:** mmdb2 **cannot read** a coordinate CIF containing it. gemmi reads them
natively, which is why the retry hack was retired.
**Assertion:** CHECKED implicitly — path A fails to load all three, and that failure is
the expected, documented result. Path B reads all three.

### B10. Element symbols: mmdb preserves the file's case, gemmi normalises to upper
**Property:** a mixed-case element symbol — `Cl`, `Br`, `Au` — which is what many
refinement programs actually write, in PDB columns 77-78 *and* mmCIF `_atom_site.type_symbol`.
**Exhibited by:** `SC1_2_refine_031.pdb` / `.cif` (5 chloride ions, written `Cl`).
**Measured:** mmdb yields `"Cl"`, gemmi yields `"CL"`. This is **live on the production
mmCIF path**, not hypothetical — the `.cif` sibling writes `Cl` too, so v0.2 stores `"CL"`
where v0.1 stored `"Cl"`.
**Assertion:** CHECKED (per-atom element comparison; reported as a field difference).
**Assessed 2026-08-12 — NEUTRAL TO BENEFICIAL, do not "fix" it.** The consumers were
audited: `coords/Bond_lines.cc:2365-2369` already tests **both** spellings (`"CL"` *and*
`"Cl"`, `"BR"` *and* `"Br"`) because the codebase long ago learned element case varies —
so that branch is unaffected. `Bond_lines.cc:2585-2593` tests **uppercase only**
(`"AU"`, `"AS"`, `"HG"`, `"MO"`), so for a file writing `Au` or `Hg` mmdb would MISS the
branch and gemmi's normalisation makes it HIT. gemmi is also the standard-conforming
answer: the PDB spec calls for right-justified uppercase symbols.
**Residual risk to watch:** any *new* comparison written against a mixed-case literal will
silently never match on the gemmi path.

### B11. Secondary structure written in `label_*` ids only is invisible to gemmi
**Property:** a `_struct_conf` / `_struct_sheet_range` loop that carries
`beg_label_asym_id` + `beg_label_seq_id` but **no `beg_auth_*` / `end_auth_*` columns**.
Both spellings are legal PDBx; wwPDB depositions carry the auth columns, **phenix.refine
does not**.
**Exhibited by:** `SC1_2_refine_031.cif` / `_036.cif` (35 `HELX_P` rows, 31 sheet ranges,
zero auth columns). Their PDB siblings carry 35 HELIX / 31 SHEET, so the same refinement
run says it in one format and loses it in the other.
**Measured, minimally:** one synthetic file with a single helix. `label_*` only →
`st.helices` is **0**; add the auth columns, change nothing else → **1**. So gemmi's readers
key on the auth columns and return **nothing at all** for the label-only spelling — silently,
with no warning on either side.
**Consequence before the fix:** every phenix-refined mmCIF reached Coot with no secondary
structure whatever, while its PDB sibling had it all. Invisible until v0.2 started using the
header (display, sequence view and ribbons all compute SS themselves; SSM's header transfer
and morph-by-SSE are the consumers that notice).
**Handled:** `coot-utils/gemmi-header.cc` falls back to resolving `label_asym_id` +
`label_seq_id` through `_atom_site` when gemmi returns nothing. The mapping is in the file
itself, so this is a resolution, not a guess. It has to run **before** the read path strips
`_atom_site` from the retained document, which is why the header records are synthesized at
read time rather than on demand.
**Known and deliberate residue:** a helix whose END residue has no observed atoms cannot be
mapped and is dropped — 3 of 35 in the SC1 files, which is why they give 32 helices where the
PDB sibling gives 35. mmdb accepts all 35 from the PDB only because a PDB reader never
validates the record against the model. **The count is printed to the console rather than
silently absorbed**, so a difference from the PDB sibling does not read as a bug.
**Assertion:** UNCHECKED. Worth adding: a post-condition that a file whose `_struct_conf`
has rows ends up with helices in the model.

### B9. Non-canonical space-group spellings
**Property:** `P212121`, `C2`, lower case — legal in hand-edited files.
**Exhibited by:** no kept file; previously tested with a hand-edited throwaway copy.
**Rule:** mmdb's `SetSpaceGroup` rejects anything non-canonical (fails *safe*: no
symmetry). clipper fails **unsafe** — `P212121` and `C2` silently become **P 1 with one
symop**, lower case throws. gemmi accepts all of them correctly, so adjustment (3)
normalises via `sg->xhm()` before mmdb sees it.
**Assertion:** UNCHECKED — **and this is the most under-covered entry in the catalogue**,
because the clipper failure mode is silent and produces wrong maps.
**Suggested corpus addition:** a hand-edited copy of `3K0N.cif` with `'P212121'`, kept
deliberately.

### B12. mmdb writes `FORMUL`'s component number at columns 9-10 and reads it at 10-11
**Property:** an upstream mmdb asymmetry, not a file property. The writer does
`sprintf("FORMUL  %2i  %3s    ", compNum, hetID)` — columns 9-10, matching wwPDB — but
`HetCompounds::ConvertFORMUL` reads it with `GetInteger(&S[9], 2)`, which is columns **10-11**.
**Consequence:** mmdb misreads its own output, and every real two-digit `FORMUL`.
`pdb1aon.ent`'s `FORMUL  22   MG` is stored as component **2**. One-digit numbers are unaffected,
which is why it survives unnoticed — most entries never reach ten components.
**Handled:** the synthesized record in `coot-utils/gemmi-header.cc` targets the READER
(right-justified in 10-11), because that line is only ever fed to `PutPDBString` and never
written to a file; mmdb's writer then puts the value back at 9-10. Verified end to end on
`3NYD_hierarchy.cif`: `FORMUL  11  HOH   *670(H2 O)`, matching the deposition.
**Do not "fix" it to match the format description** — that puts the number where mmdb cannot
read it, and the loss is silent.

### B13. mmdb writes a continuation number on every `REVDAT` after the first
**Property:** another upstream writer bug. `ClassContainer::PDBASCIIDump` hands each record its
index in the container as the continuation number `N`, and `RevData::PDBASCIIDump` renders
`N > 0` into columns 11-12. That is right for TITLE/COMPND, where every line after the first
genuinely IS a continuation, and wrong for REVDAT, where each line is a separate revision.
**Exhibited by:** any entry with more than one revision. Output reads `REVDAT   2 2 19-JUN-13`.
**Not ours:** reading the *deposited* `3K0N.pdb` and writing it straight back reproduces it
exactly, so it predates the mmCIF header work and is reachable through PDB -> PDB alone.
**Not fixable from here** — mmdb is an external dependency, not vendored. mmdb's own reader
ignores columns 11-12 so it round-trips; a stricter parser might not.

### B14. `FORMUL`'s component number is the SUBCHAIN ordinal, not the entity id
**Property:** the number in a `FORMUL` record is the 1-based position of the component's first
`label_asym_id` in `_struct_asym` — **not** `_entity.id`, which is the obvious reading and is
wrong on every file where the two can differ.
**Measured against three depositions:**

| | deposited | entity id | subchain ordinal |
|---|---|---|---|
| 5E1N | MSE 1, MPD 2, CA 4, HOH 9 | 1, 2, 3, 4 ✗ | 1, 2, 4, 9 ✓ |
| 3NYD | 3NY 3, ACT 4, SO4 7, HOH 11 | 2, 3, 4, 5 ✗ | 3, 4, 7, 11 ✓ |
| 3K0N | HOH 2 | 2 ✓ by luck | 2 ✓ |

**Why:** a component with several copies gets several subchains — 5E1N's five calciums are
`_struct_asym` D-H — so the next component's number jumps. `_struct_asym` is authoritative rather
than first-appearance order in `_atom_site` because it also lists subchains with no observed
atoms, and wwPDB counts those.
**Also:** a component can belong to a POLYMER entity (5E1N's 8 selenomethionines are subchain A,
`FORMUL   1  MSE`), so the comp -> subchain map has to come from `_atom_site`, not from
`_pdbx_entity_nonpoly`.
**The general lesson, for the third time in this file:** 3K0N alone would have confirmed the
wrong rule.

---

## C. Cross-format traps

### C1. mmdb reads no links from mmCIF, but does from PDB
**Exhibited by:** `5E1N.cif` (0 links via mmdb) vs `5E1N.pdb` (130). LINK is exactly where
`covale` and `metalc` live in PDB format.
**Consequence:** the gemmi path's "gain" of 130 links is **convergence onto behaviour the
PDB path always had**, not novel risk.

### C2. PDB format cannot express hydrogen bonds at all
**Exhibited by:** converting `SC1_2_refine_036.cif` (660 `hydrog`) to PDB yields **0** LINK
records.
**Consequence:** the B5 hazard is mmCIF-only and could never have surfaced through PDB.

### C3. The two readers are exactly complementary on `struct_conn`
| file | mmdb | gemmi |
|---|---|---|
| original `5E1N.cif` | 0 links | 130 connections |
| mmdb-written backup | 130 links | 0 connections |
**Cause:** mmdb writes `ptnr1_auth_*` partner tags only — no `ptnr1_label_*`, which is what
gemmi's reader keys on.
**Assertion:** UNCHECKED.
**Breaks:** any assumption that a file written by one reader can be read by the other.

### C4. Chain ordering differs
**Exhibited by:** `pdb1ffk.ent` — same ids, same contents, different order.
**Assertion:** CHECKED and reported as ORDER-only.
**Filed cosmetic.** Revisit only if something turns out to depend on chain ordinal.

### C5. mmdb invents an empty-id chain from some mmCIF
**Exhibited by:** `2RSF.cif`, `3NYD_hierarchy.cif`, `6DMH.cif`. Holds 0 atoms; gemmi does
not produce it.
**Assertion:** CHECKED (chain id sets).

---

## D. Environment and API traps — not file properties, and invisible to any corpus

### D1. `syminfo.lib` must be findable or every space group is null
mmdb checks `$SYMINFO`, **otherwise the current working directory**. The same file gives
`"P 21 21 21"` from the repo root (which has a tracked `syminfo.lib`) and null from
anywhere else. The installed launcher exports `SYMINFO`; standalone tools do not, so
`tools/gemmi-diff/build.sh` bakes in `-DBANDICOOT_SYMINFO_DEFAULT` and sets it.
**Before this was found, the space-group comparison silently degraded to null-vs-null**
and would have hidden a real regression.

### D2. `mmdb::Root::GetCell` returns 1 on success, 0 on failure
Inverted relative to the usual convention. Cost real time. **Check the convention on any
new mmdb call.**

### D3. mmdb's mmCIF readers are keyed to obsolete NDB-era tags
`Helix::GetCIF` wants `ndb_helix_class_pdb` / `ndb_length` / `pdb_id` where PDBx writes
`pdbx_PDB_helix_class` / `pdbx_PDB_helix_length` / `pdbx_PDB_helix_id`, and reads
`_struct_conf.id` with `CIFGetInteger` while modern files put `HELX_P1` there. `Title::GetCIF`
wants `_database` (modern: `_database_2`) and `ndb_keywords`.
**Consequence:** mmdb reads **0 helices** from a file containing 8, and an empty title
section — silently, on every modern PDB mmCIF. This is the subject of Interlude B.

### D4. gemmi 0.7.5 ships a debug `printf` on the write path
`copy_model_from_mmdb` contains `printf("Dodajemy Entity %s - %s\n", ...)`. With 106
`make_backup()` call sites, every backup prints a line per chain. **Not our bug** — know it
before hunting for it.

### D5. `st.resolution` is populated from mmCIF despite a header comment suggesting otherwise
`model.hpp:922` documents it as "simplistic resolution value from/for REMARK 2", which
reads as PDB-only. The mmCIF parse happens inside the compiled
`populate_structure_from_block`, invisible in the headers. Measured: `5E1N.cif` gives
`st.resolution = 1`, matching `_refine.ls_d_res_high`.
**General rule this implies:** for anything `GEMMI_DLL`, the headers describe the interface,
not the behaviour. **Probe, do not read.**

### D6. `mmdb::mmcif::Loop::GetReal` reports SUCCESS for an absent tag
Its own documentation promises `CIFRC_NoTag` when the tag is not found. **Measured: it returns
success and leaves the target variable untouched.**
**What it cost:** `read-sm-cif.cc` read `_atom_site_U_iso_or_equiv` this way and scaled the
result by 8π², so any small-molecule CIF stating no displacement parameter had the **hardcoded
default 10.0** scaled instead — **every atom at B = 789.57**. Both real COD files checked
(`1000041`, `2000001`) were doing exactly that.
**The idiom that works, and it was already in the same function:** test presence with
`GetString`, which *does* report absence by returning NULL, then read the number. The existing
`_atom_site_type_symbol` code does this, with the comment *"this may not exist (strangely
enough)"* — evidently written about this same quirk.
**Suspect every `ierr` from this API.** Fixed 2026-08-19.

### D7. Importing a wwPDB CCD as a dictionary DESTROYS a working library entry for that component
**Measured 2026-08-19 on `ADP.cif` downloaded from the PDB:**
```
bundled monomer library ADP : 41 bonds, 41 with distance   [' PB ',' O1B','double',1.509,0.02]
after read_cif_dictionary(the CCD) : 44 bonds,  0 with distance   [' PB ',' O1B','double',False,False]
```
**Why:** a CCD states bond ORDER and aromaticity and no `value_dist` at all, so importing it
replaces a refinable entry with connectivity-only. This is the drag-and-drop monomer-library
corruption in miniature -- scoped to one component instead of the standard residues, and therefore
much quieter.
**Consequence:** dropping `ADP.cif` used to break RSR for ADP for the rest of the session. Since
u68 drag-and-drop reads a component definition as COORDINATES and does not import it, so the
bundled entry survives and RSR works (verified: 41 bonds with distances after the coordinate read).
**The tension to respect if this is ever revisited:** importing the CCD is what makes
`Get Monomer <code>` work for a component NOT in the library (that is how AR6 used to load), and
what breaks one that IS. Any change here must key on whether a dictionary for that comp_id already
exists.

### D8. `scripting_function()`'s return value: `.i`, not `.type`
`coot::command_arg_t::coot_script_arg_type` is `{UNSET, INT, FLOAT, STRING, BOOL}`, so `.type` for
an integer return is the constant **1**. `get_monomer_molecule_by_network_and_dict_gen()` did
`imol = retval.type`, returning 1 for every successful fetch.
**Why it stayed hidden and then looked like a different bug entirely:** the caller tests
`is_valid_model_molecule(imol)` and pops "Failed to import molecule" when it fails. Models and MAPS
share one numbering in Coot, so molecule 1 is usually the map whenever a map is loaded -- i.e.
exactly when the user is about to refine. Observed 2026-08-19: Get Monomer downloaded the
dictionary, created the molecule, RSR worked on it, and the dialog said it had failed.
**Check the marshalling before blaming it:** `scripting_function()` fills `.i` correctly, and the
Py2 `PyInt_Check` resolves to `PyLong_Check` through `compat/python23-shim.hh`, so the type test
does pass. The bug was purely the assignment. Fixed 2026-08-19.

---

## E. Write-side traps — Phase 3 and later, nearly all UNCHECKED

### E1. Extension columns on `_atom_site` do not survive
Because `atoms` is regenerated (EDIT), gemmi rebuilds the `_atom_site` loop from **the
columns it knows about**. Any non-standard extra column is lost on write.
**Directly affects Phase 4:** `pdbx_heterogeneity_id` must be harvested into a typed
per-atom carrier at read time and re-emitted, *not* left at block level.
**Whole categories come along for free; extension COLUMNS do not.**

### E2. A cell-less file must not gain a `_cell` category
Mirror image of A2 on the write side: `copy_from_mmdb` on a deliberately cell-less model
yields a **zero** cell, and EDIT would then create `_cell` full of zeros where the input had
none.

### E3. `_atom_site_anisotrop` — emission unconfirmed
There is no separate toggle for it among the 33 `MmcifOutputGroups` flags, and `to_mmcif.hpp`
does not mention it. If gemmi does not emit it, then stripping anisotropic records from the
model leaves a **stale** `_atom_site_anisotrop` in the file and the user's checkbox is
defeated for mmCIF.
**Corpus:** every `*_hierarchy.cif`, `3K0N`, `5E1N`, `6DMH`, both `SC1_2_refine_*` have
anisotropic records. `1AON`, `1FFK`, `2RSF` do not — a useful negative control.

### E6. The writer preset reformats passthrough categories
**Property:** any category stored as a **single-row loop**.
**Exhibited by:** `_citation`, `_software`, `_em_software`, `_struct_conf_type`,
`_struct_ncs_ens` and — most pointedly — **`_pdbx_state_coexistence`** in
`3NYD_hierarchy.cif`.
**Rule:** gemmi's `cif::Style::Pdbx` sets `prefer_pairs`, which rewrites single-row loops
as pairs on output. No data is lost, but categories we promised to pass through untouched
come back reshaped. Bandicoot therefore uses an explicit `WriteOptions`
(`misuse_hash = true`, `prefer_pairs = false`), not the preset.
**Assertion:** CHECKED by the round-trip category/row check.
**Note the general point this settles:** the gate is **tag-and-value identity, not literal
bytes** — gemmi regenerates loop text with its own column spacing, so alignment and
trailing whitespace are normalised regardless.

### E4. The mmCIF branch ignores three write options — ✅ FIXED 2026-08-12
`write_atom_selection_file` takes `write_hydrogens`, `write_aniso_records` and
`write_conect_records`; **the mmCIF branch ignores all three.** The first two are user-facing
checkboxes. Ticking "no hydrogens" and saving as mmCIF writes the hydrogens anyway.
Also: dropping hydrogens can orphan `hydrog` partners in a passed-through `struct_conn`.

### E5. Dangling records after deletion
Deleting a residue leaves a HELIX record pointing at residues that no longer exist, and
deleting an atom leaves a dangling `struct_conn` row. **Pre-existing, both formats**, and
nothing in Coot updates either. `remove_long_links()` looks like a remedy and is **a no-op** —
both its `delete link;` statements are commented out.

---

### E7. `update_mmcif_block` skips a category it has no data for — it does not clear it
**Measured 2026-08-12**, and it corrects an assumption in the plan.
Setting `groups.ncs = true` (EDIT) does **not** delete `_struct_ncs_oper`, even though
`copy_from_mmdb` never populates `st.ncs`: the category came through intact, 15 tags in
and 15 out, on both `SC1_2_refine_036.cif` and `1AON.cif`. The plan's stated reason for
`ncs` = PASS — "EDIT would DELETE `_struct_ncs_oper`" — is therefore **not** what would
happen.
**The PASS disposition still stands**, on the sounder ground that a category must not be
rewritten from a source that does not hold it. But do not rely on the flags alone as a
safety net: for these categories the toggle turns out to be nearly inert, so a mistaken
flip would fail silently rather than loudly.
**Consequence for testing:** flipping a policy flag is NOT a valid way to prove the
write-side gate works, because for such a category nothing changes. Prove it by deleting
a category outright (see below).

### E8. Trimming atom names on the model does not trim the LINK records pointing at them
**Property:** mmdb stores atom names in PDB's fixed columns (`" O2A"`). Adjustment (W1) in
`gemmi-write.cc` trims them before writing, so the mmCIF says `O2A` rather than `' O2A'` --
but `st.connections`, filled by `transfer_links_from_mmdb`, holds the SAME padded spelling
and was not trimmed with them.
**Exhibited by:** any PDB file with LINK records saved as mmCIF -- `pdb1aon.ent` (27),
`pdb1ffk.ent` (18), `5E1N.pdb` (130).
**Measured:** gemmi's `struct_conn` writer resolves each partner against the model to emit
`label_atom_id`, finds nothing once the two spellings disagree, and writes `?` for **both
atom ids and the reported distance**. Reading that file back gives 27 links of which **0
resolve** -- no link bonds drawn, no metal restraints. Silently inert, the third instance
of exactly this failure mode (B2, B3, B4 are the others).
**Only the synthesis path can show it:** with a retained document `struct_conn` is PASS and
is never rewritten, so mmCIF -> mmCIF was unaffected and only PDB -> mmCIF broke.
**Fixed 2026-08-17** -- (W1) now trims the connection partners too.
**Assertion:** CHECKED, by `--round-trip` chain A, which is what found it. The link
post-condition (`n_links_resolved`) was already in the tool; what was missing was a mode
that fed our own output back in.

## Assertion scoreboard

| group | checked | unchecked |
|---|---|---|
| A. file shape | 4 | 3 |
| B. model content | 10 | 1 |
| C. cross-format | 2 | 3 |
| D. environment/API | n/a — documentation, not assertions | |
| E. write side | 6 | 2 |

Section E moved from 0-checked when `--write-check` landed (2026-08-12) and again when it
learned to compare COLUMNS and VALUES (2026-08-13). Still unchecked there: E5 (dangling
records after deletion) and A4's write-side half (a 20-model ensemble through the writer).

## Baselines — compare a run against these

**Read side** (`gemmi-mmdb-diff`, no arguments), `0.2.0.0-u63`:
```
summary: 6 identical, 17 with differences, 8 failed to load
```
Was `5 identical, ...` at `u49`. The one move is a corpus addition, not a code change:
`samples/` gained `3K0N-CIF2PDB.pdb` (Art's mmCIF->PDB comparison artifact), and it reads
IDENTICAL. The six identical files are `2GEW.pdb`, `3K0N-CIF2PDB.pdb`, `3K0N.pdb`, `5E1N.pdb`,
`pdb1aon.ent`, `pdb3k0n.ent`.
Was `3 identical, 16 with differences, 4 failed to load` at `u34`. Every change since is a
corpus addition, and each was checked individually rather than assumed:
the two extra `failed to load` at `u68` are `AR6.cif` and `ADP.cif`, where **path A fails
because raw mmdb cannot read a chem_comp file at all** -- correct, not a regression;
`3K0N.pdb` and `2GEW.pdb` compare IDENTICAL; `3K0N-sf.cif` and `2GEW-sf.cif` are
structure-factor files that both paths correctly refuse (zero atoms); `2GEW.cif` differs
only by B2 atom-name padding. **Before reading a changed baseline as a regression, check
whether the corpus grew** -- `ls ~/sw/bandicoot-project/samples` -- because it has three
times now.
Every line is attributable to an entry above; a NEW unexplained line is the signal. The
exit status is NOT a gate here — several differences are permanent and deliberate.

**Write side** (`gemmi-mmdb-diff --write-check`), `0.2.0.0-u63`:
```
write-side summary: 14 clean, 0 lossy, 0 write-failed, 3 read-declined, 14 skipped (not mmCIF)
```
`skipped` moved 11 -> 12 for `3K0N-CIF2PDB.pdb`, then 12 -> 14 when the corpus gained the two
chemical-component files (`AR6.cif`, `ADP.cif`), which are skipped by `is_coordinate_mmcif()` --
see A5c. **`0 lossy` has still never moved.**
Was `15 clean, 0 lossy, 0 write-failed, 1 read-declined, 9 skipped` when the column/value
work landed; the moves since are corpus additions (`3K0N.pdb`, `2GEW.pdb` skipped as not
mmCIF; `3K0N-sf.cif`, `2GEW-sf.cif` declined for having no atoms). **The number that matters,
`0 lossy`, has never moved.**

**Round trip** (`gemmi-mmdb-diff --round-trip`), `0.2.0.0-u63`:
```
  A  PDB->mmCIF->mmCIF : 9 model preserved, 5 differ
  B  mmCIF round trip  : 14 stable, 0 NOT stable   <-- the gate
  C  mmCIF->PDB->PDB   : 12 model preserved, 2 differ
```
Chain A was `8 preserved` at `u49`; the extra one is `3K0N-CIF2PDB.pdb`. Its `differ` count moved
3 -> 5 at `u68` when `AR6.cif` and `ADP.cif` joined the corpus: a chem_comp file takes the
PDB-shaped chain A, and its first hop is a cross-format synthesis, so differing is correct.
Chains B and C have not moved.
Chain B is the gate. Chain A's three differences are `2RSF.pdb` (A3) and the two
`SC1_2_refine_*.pdb` (B10); chain C's two are the same SC1 pair (C2).
**Here the exit status IS a gate** — losing a category, a column or a value is never
deliberate. Read declines (a small-molecule CIF yielding zero atoms) do not fail it, or the
gate would be permanently red and stop being read.

**Proven to fail when it should:** deliberately erasing `_struct_conf` before the write gives
`LOST : _struct_conf (279 rows)` and exit 1.

## What the column/value pass caught (2026-08-13)

Going from "categories and row counts" to "columns and values" took the corpus from a falsely
reassuring *12 clean, 0 lossy* to an honest *5 clean, 9 lossy*. Four real losses, all now fixed:

| loss | cause | fix |
|---|---|---|
| `_atom_site.pdbx_heterogeneity_id` (4 files) | `_atom_site` is EDIT, so gemmi rebuilds it from the model and emits only the columns it knows | general extension-column carrier — harvest ANY non-standard column, restore on write |
| `_atom_site_anisotrop` 10 identity columns (9 files) | same | rebuilt by joining on atom `id` |
| `_atom_type` 13 `scat_*` columns (2 files) | `atom_type` was wrongly EDIT; mmdb has no scattering factors | EDIT -> PASS, plus augmentation so a NEW element still gets listed |
| `_struct_mon_prot_cis` 2 auth names | gemmi's column set is narrower than the input's | rebuilt from their label siblings |

**Two false alarms in the checker itself, both the header lesson one level down:** comparing
rows POSITIONALLY reported 566,883 phantom B-factor changes (`merge_chain_parts` legitimately
reorders residues on read); then sorting those columns as STRINGS put `"18.42"` and `"18.420"`
in different slots and reported thousands of phantom coordinate changes. Now: per-column
multiset, sorted NUMERICALLY when both sides parse.

**Accepted deviations the gate is taught to allow** — element-symbol case (`Cl` -> `CL`; wwPDB
itself writes `CA`/`SE`/`MG` uppercase, only phenix writes mixed) and read declines.

## ⚠ The gate's remaining blind spot

**It compares OUR OUTPUT against the ORIGINAL INPUT, and never re-reads our output AS an
input.** A round trip of a round trip is unmeasured — which is exactly where the
2026-08-13 backup-corruption bug lived: a harvested value stored via `as_string()` emitted
ZERO characters for a CIF null, so the row came out one token short and every later value
shifted by one. It took a specific recipe to surface (renumber waters, save, read that file
back) and the corpus gate was green throughout.

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

### A6. SHELX `.ins` / `.res`
**Property:** not a format gemmi reads; it throws `Unknown format`.
**Exhibited by:** `F1-Cu-8_anom_diffs.ins`.
**Assertion:** UNCHECKED.
**Breaks if regressed:** the SHELX branch is bypassed and the file fails to open.

### A7. Compression decides which reader runs — A KNOWN DEFECT, NOT YET FIXED
**Property:** a `.cif.gz` file.
**Rule:** `coot::util::file_name_extension` returns everything after the **last** dot, so
`foo.cif.gz` has extension `.gz`, `gemmi_handles_extension` is false, and **mmdb reads
it** — gemmi never sees it. Every compressed backup in `coot-backup/` is therefore
invisible to the gemmi path, and `backup_compress_files_flag` defaults to **1**.
**Assertion:** UNCHECKED.
**Breaks:** already broken. An I/O detail silently selects a parser. Slated for D2, where
`gemmi::read_structure_gz(path, format, save_doc)` and `coor_format_from_ext_gz()`
(`mmread_gz.hpp`) are the natural fix.

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

### B2. Atom-name padding for 3-character hydrogens
**Property:** a 3-character atom name whose element is H (`HH1`, `HH2`, any `H??`).
**Exhibited by:** `3K0N.cif` (1 atom), `6DMH.cif` (6), `2RSF.cif` (60).
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

### E4. The mmCIF branch ignores three write options
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

## Assertion scoreboard

| group | checked | unchecked |
|---|---|---|
| A. file shape | 3 | 4 |
| B. model content | 9 | 1 |
| C. cross-format | 2 | 3 |
| D. environment/API | n/a — documentation, not assertions | |
| E. write side | 0 | 5 |

**The two highest-value gaps to close next:** B9 (non-canonical space groups — the clipper
failure is silent and yields wrong maps) and the whole of section E, which the Phase 3
byte-identity gate is meant to cover.

## Baseline: the full-corpus verdict as of 2026-08-12 (build `0.2.0.0-u12`)

`3 identical, 15 with differences, 4 failed to load` over all 22 coordinate files. **Every
one of those is accounted for by an entry above**, which is the property that makes the run
usable as a regression gate — a NEW unexplained line is the signal.

- **4 path-A failures:** `1AON.cif`, `SC1_2_refine_031.cif`, `SC1_2_refine_036.cif` (B8), and
  `4517425.cif` — which fails on BOTH paths, path B with
  `no atoms - not a macromolecular coordinate file`, exactly as A5 specifies.
- **3 identical:** `pdb1aon.ent`, `pdb3k0n.ent`, `5E1N.pdb`.
- **Differences** are B1/B2/B3/B5 (link counts), C4 (1ffk chain order), C5 (phantom empty-id
  chain), A3 (2RSF.pdb space group) and B10 (element case).

**Re-run this after any gemmi bump, any mmdb bump, and at the close of every phase.** Compare
against the line above before reading anything else.

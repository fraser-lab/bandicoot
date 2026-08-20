# gemmi-mmdb-diff

A diagnostic that answers one question: **does reading a coordinate file through
gemmi produce the same mmdb model as reading it through mmdb?**

It loads each file twice —

- **path A** — Bandicoot's own reader, the real `get_atom_selection()` linked out of
  `libcoot-coord-utils`, so the four post-read fix-ups and the `_struct_ncs_oper`
  retry (GitHub #9) are all in play;
- **path B** — `gemmi::read_structure_file` → `gemmi::copy_to_mmdb`;

— then diffs the two resulting `mmdb::Manager` models and prints a report.

The comparison is between two **in-memory models**, not between two files. Everything
downstream of `get_atom_selection()` sees only that model, so if the models agree,
nothing downstream can tell the two readers apart.

## Why it lives here

It was written for the v0.2 Phase 1 gate, but it keeps paying for itself: **re-run it
after any gemmi upgrade** and the diff says immediately whether the new version changed
the model handed to the rest of Coot. That is a question no unit test in the tree
currently answers, and one that is very easy to get wrong silently.

## `syminfo.lib` — why the tool sets `SYMINFO` for you

mmdb can only attach a space group to a model if it can find `syminfo.lib`: it checks
`$SYMINFO` and otherwise looks in the **current working directory**. The installed
launcher exports `SYMINFO` (`bin/bcoot`), but this tool runs outside it — so without help
its spacegroup comparison quietly degrades to null-vs-null and would hide a real
regression after a gemmi upgrade.

`build.sh` therefore bakes in the install's copy as a default, and the tool sets `SYMINFO`
itself when the environment does not, printing which file it used. If you see
`SYMINFO: NOT FOUND`, treat every spacegroup result in that run as meaningless.

A null `GetSpaceGroup()` is usually this, not a reader bug.

## Building

Not part of the autotools build — it links against an already-installed Bandicoot, and
gemmi is not a configure-time dependency of the tree. Keeping it standalone means it
costs the normal build nothing and cannot break it.

```sh
./tools/gemmi-diff/build.sh                       # uses ~/sw/bandicoot-0.2-install
PREFIX=$HOME/sw/bandicoot-install ./tools/gemmi-diff/build.sh
```

## Running

```sh
./tools/gemmi-diff/gemmi-mmdb-diff                      # the whole corpus
./tools/gemmi-diff/gemmi-mmdb-diff [options] <coord-file> ...
```

**With no files named it runs the entire corpus** — every `.cif`, `.ent` and `.pdb` in
the corpus directory, in sorted order. The directory is `--corpus DIR` if given, else
`$BANDICOOT_SAMPLES`, else the path baked in at build time
(`$REPO/../samples`, overridable with `SAMPLES=... ./build.sh`).

The corpus lives **outside the repository** on purpose: those files would bloat git
history permanently and have no place in a shipped release. The accepted consequence is
that a machine without that folder gets a "NO coordinate files" notice instead of a run.
It is not a build input, and it is reconstructible — see `TRAPS.md`, where most entries
are re-fetchable by PDB ID.

Naming files explicitly still works exactly as before and ignores the corpus entirely.
`.mtz` maps are skipped, and so is the SHELX `.ins` — gemmi cannot read it at all, so
including it would put a permanent "failed to load" line in every run (see `TRAPS.md` A6).

| option | effect |
|---|---|
| `--corpus DIR` | run every coordinate file in `DIR` instead of the compiled-in default |
| `--dump-chains` | print each chain's id, residue count, atom count and composition, for both paths |
| `--examples N` | show up to N example differences and distinct value-shapes per field (default 3) |
| `--no-setup-entities` | skip `gemmi::setup_entities()` on path B |
| `--no-merge-chains` | skip `Structure::merge_chain_parts()` on path B — reproduces the raw `copy_to_mmdb` behaviour, chain duplicates and all |
| `--keep-hydrog-links` | keep `Hydrog` connections in path B's LINK table — reproduces the unfiltered `transfer_links_to_mmdb` behaviour |

Exit status is 0 only if every file compared identical.

**So the exit status is NOT the gate for a corpus run**, and should not be wired into one.
Several corpus differences are permanent and deliberate — gemmi reading `struct_conn`
links mmdb never reads, gemmi declining to invent a chain mmdb invents — so a full run
always exits non-zero. **The gate is the summary line and the attribution of every
difference to a catalogue entry in `TRAPS.md`.** A new, unexplained line is the signal;
a non-zero exit on its own means nothing here.

## Reading the output

Atoms are matched by `(model, chain, seqnum+inscode, trimmed atom name, trimmed
altloc)` — **never by index**, because a single ordering difference would otherwise
report every atom as changed. Atom-name padding and altloc spelling are therefore
compared as *values* rather than being part of the key. TER entries are excluded from
every atom count: mmdb keeps them in its atom table, so counting them makes each
polymer chain look one atom long.

Two lines are worth understanding:

- `reachable via GetChain(id)  A: n/N  B: m/N` — a shortfall means that model holds two
  chains with the same id, so `Model::GetChain(id)` finds only the first and the rest of
  those atoms are invisible to every by-name chain lookup.
- `N distinct shape(s)` under a field difference — 60 differences all of one shape is a
  systematic rule; 60 differences of 40 shapes is something else entirely.

Per-atom fields include the **anisotropic ADPs** — all six `u[]` components, the
`ASET_Anis_tFac` flag and `sigU` — added 2026-08-20, and the model summary includes the
**resolution**. Both were added because the read path adjusts them at the gemmi-to-mmdb
boundary and nothing was checking that it did so correctly; between them they found traps
**B19** and **E9** on the first run.

## Why path B calls `merge_chain_parts()`

`copy_to_mmdb` does `CreateChain()` per `gemmi::Chain` and never merges, so a file where
one author chain holds polymer + ligands + waters becomes several mmdb chains sharing an
id — and `GetChain(id)` finds only the first. That hid 778 of 6,443 atoms (12%) in
`pdb3nyd.ent`. `Structure::merge_chain_parts()` is gemmi's own remedy: with the default
`min_sep=0` it concatenates residues **without renumbering**, and `copy_from_mmdb`
already applies it in the reverse direction. Path B therefore calls it, and this is the
behaviour Phase 2's read path is expected to adopt. `--no-merge-chains` reproduces the
unfixed behaviour for comparison.

## Why path B drops `Hydrog` connections

`gemmi::transfer_links_to_mmdb` copies every `struct_conn` row and ignores `con.type`,
and Coot's `fill_links()` (`ideal/link-restraints.cc:39`) then pushes every mmdb LINK
into the refinement's link vector with no filtering of its own. A phenix-refined file can
carry hundreds of hydrogen bonds that way — 660 in `SC1_2_refine_036.cif` — and an H-bond
at ~2.9 Å must not become a link restraint pulling toward ~1.4 Å. Bandicoot's RSR
deliberately does not restrain H-bonds: helix and sheet networks are already covered by
the secondary-structure restraints, and a user who wants a particular one can add it with
**Make Link**.

Nothing is lost by this. Phase 3's verbatim passthrough preserves the whole `struct_conn`
category on write, `hydrog` rows included; the filter applies only to the LINK table that
feeds refinement. `disulf`, `covale` and `metalc` are transferred.

## `--write-check` — the Phase 3 write-side gate

```sh
./tools/gemmi-diff/gemmi-mmdb-diff --write-check        # whole corpus
```

Reads each mmCIF through the production reader, writes it straight back out through the
production writer **with no edits in between**, and diffs the categories of the input
against the output. Output files are left in `$TMPDIR/bandicoot-write-check/` for
inspection.

This is the axis the read-side diff structurally cannot see — the Phase 1 gate passed with
"~250,000 atoms, zero differences" while measuring only atoms, and the metadata losses it
was blind to surfaced a day later in discussion rather than at the gate.

**"Identical" here means tag-and-value identity, not literal bytes.** gemmi regenerates
loop text with its own column spacing, so alignment and trailing whitespace are normalised
on every write. That is not data loss, and chasing byte-identity would mean
re-implementing gemmi's writer.

**Unlike the read-side mode, this exit status IS a gate.** Losing a category is never
expected or deliberate, so non-zero means a real regression. Read declines (a
small-molecule CIF legitimately yielding zero atoms) are counted separately and do *not*
fail it — otherwise the gate would be permanently red and therefore useless. Non-mmCIF
inputs are skipped: a PDB file has no document to preserve, so "categories in vs out" is
not a question that means anything.

It compares category presence, row counts, **column sets and values** — the last two added
2026-08-13 after the category-only version passed a file that had lost ten columns. Values are
compared per column as a multiset sorted NUMERICALLY, never row-by-row: `merge_chain_parts`
legitimately reorders residues on read, and a positional compare reports hundreds of thousands
of phantom differences.

Baseline (see `TRAPS.md` for the full picture):

```
write-side summary: 14 clean, 0 lossy, 0 write-failed, 3 read-declined, 11 skipped (not mmCIF)
```

**Verified to actually fail:** deliberately erasing `_struct_conf` before the write makes
it report `LOST : _struct_conf (279 rows)` and exit 1.

## `--round-trip` — read, write, read again, in all three directions

```sh
./tools/gemmi-diff/gemmi-mmdb-diff --round-trip         # whole corpus
```

Three chains, chosen so that each format conversion is exercised in both directions:

| chain | what it does | is it a gate? |
|---|---|---|
| **A** | PDB in -> mmCIF out -> mmCIF in | reported |
| **B** | mmCIF in -> mmCIF out -> mmCIF in -> mmCIF out | **yes** |
| **C** | mmCIF in -> PDB out -> PDB in | reported |

**Why this is not covered by `--write-check`.** That mode compares our output against the
original input and stops. It never re-reads its own output, so a defect that is stable
across one hop but compounds over two — or one that only appears when our output becomes
an input — is invisible to it. Chain B closes that: it writes, re-reads, writes again, and
**the two outputs must be byte-identical**.

Chains A and C are reported but do **not** set the exit status. Several of their
differences are permanent and deliberate: PDB cannot express a hydrogen-bond connection at
all, and a PDB input has no categories to compare. An exit status that is always non-zero
stops being read — the same reason the read-side mode's status is not a gate either.

Writes go through **`write_atom_selection_file()`**, the function Save Coordinates and
`make_backup()` actually call, not straight to the gemmi writer. A round-trip test that
skipped the real save path would not be testing the thing that can regress. This is why
`build.sh` links `-lcoot-coords`.

Deliberately **not** part of a default run: it writes several files per corpus entry and
takes appreciably longer. Run it after a substantial change.

Baseline:

```
  A  PDB->mmCIF->mmCIF : 8 model preserved, 3 differ
  B  mmCIF round trip  : 14 stable, 0 NOT stable   <-- the gate
  C  mmCIF->PDB->PDB   : 12 model preserved, 2 differ
```

Every difference is attributable, which is what makes the numbers usable: chain A's three
are `2RSF.pdb`, whose `CRYST1 1.000 1.000 1.000 90 90 90 P 1` placeholder mmdb rejects and
gemmi accepts (trap A3), and the two `SC1_2_refine_*.pdb`, which carry five `Cl` atoms that
gemmi normalises to `CL` (trap B10). Chain C's two are the same SC1 pair, whose 660
hydrogen-bond connections PDB cannot carry (trap C2). **A new unattributed line is the
signal.**

**It earned its place a third time the same day**, after Art tested the fix below and found
the half it could not see: `GetResolution()` reads only REMARK 2, which phenix PDB output does
not write, so a phenix PDB still converted to an mmCIF with no resolution while every wwPDB
one now kept it. The check that replaced it is a **post-condition against the input file**,
not a model-vs-model compare -- *if the input states a resolution anywhere, including inside
REMARK 3 prose, our mmCIF must carry `_refine.ls_d_res_high`* -- because comparing
`GetResolution()` on both sides makes a value mmdb cannot see on either side read as
agreement. Verified to fail when it should: with the REMARK 3 fallback disabled, chain A
prints `RESOLUTION LOST: input states 2.80, our mmCIF has no _refine.ls_d_res_high`.

**It earned its place a second time on 2026-08-20**, when the model summary learned to
compare the resolution: chain A dropped from `9 model preserved` to **`0 preserved, 14
differ`**, every PDB input reporting `DIFF resolution present  A=1 (1.390) B=0 (-2.000)`.
mmdb had the number from `REMARK 2` and our synthesized mmCIF wrote no `_refine` at all, so
converting a PDB to mmCIF lost the resolution for good (trap E9). Fixed, and chain A is back
to `9 / 5` with the resolution now genuinely compared rather than merely unmeasured.

**It earned its place on the first run:** chain A reported 27 links of which 0 resolved on
`pdb1aon.ent`, which turned out to be a real defect in our own writer — adjustment (W1)
trimmed mmdb's atom-name padding on the model but not on the LINK records pointing at it,
so gemmi wrote `_struct_conn` with no atom names and no distances. Converting a PDB with
LINK records to mmCIF produced a file whose connections were silently inert. Fixed in
`gemmi-write.cc`; only the synthesis path could show it, because with a retained document
`struct_conn` is PASS and never rewritten.

## `--header-check` — the mmCIF-to-PDB-record synthesis, against the deposition

```sh
./tools/gemmi-diff/gemmi-mmdb-diff --header-check       # whole corpus
```

Reads each coordinate mmCIF, calls **`coot::pdb_header_records_from_mmcif()`** — the same
pure function the read path calls — and compares the records it synthesizes against the
same entry's **PDB-format sibling in the corpus** (`5E1N.cif` vs `5E1N.pdb`,
`3K0N_hierarchy.cif` vs `3K0N.pdb`, ...). Where a sibling exists, the deposition is the
answer key: wwPDB wrote both files from the same deposited data, so any difference is
ours.

It compares **counts** for every record type, and **exact text** for `DBREF`, `FORMUL`,
`HELIX` and `SHEET` — the four whose columns mmdb parses positionally, where being one
column off is silently wrong rather than visibly wrong.

**Why it calls the pure function directly** rather than writing a PDB file and reading it
back: this mode exists to test *synthesis*, and going through `WritePDBASCII` would put
mmdb's own record-writing conventions (which differ from its record-*reading* conventions —
see traps B12 and B13) between the thing under test and the comparison. Three layers in
one measurement means a difference cannot be attributed to any of them.

Deliberate, reported-not-failed differences: `REVDAT` (ours states every revision the
mmCIF lists, the PDB file omits some), `JRNL` (our `"; "` author separator rewraps),
`EXPDTA` where the sibling writes none at all (phenix's PDB output does not, its mmCIF
does), and `REMARK`, which is skipped entirely — our REMARK 3 is a deliberate *summary* of
`_refine`, and the prose REMARK sections are wwPDB renderings of typed categories, so
composing them would be reconstruction rather than translation.

**It earned its place on the first run**, with three real synthesis defects that had been
shipping:

- `HELIX` and `SHEET` wrote the identifier **left**-justified in columns 12-14; wwPDB
  right-justifies it (`"  B"`). Every helix and strand of every file was one to two columns
  off (traps B16).
- `HETNAM` **truncated** at column 80 instead of wrapping onto a continuation line, so
  6DMH's long MER name lost its tail (trap B17).
- `FORMUL` wrote `1(C27 H33 N9 O15 P2)` where the PDB convention for a single molecule of
  a component is the formula **bare** (trap B18).

and one real reader defect: phenix's `label_*`-only secondary structure turned out to hold
**author** numbering under label tag names, which moved every strand in chains B/C/D of the
SC1 pair by four residues (trap B15). It also closed out what had been written up as an
accepted loss — the "3 of 35 helices cannot be mapped" note in trap B11 was a symptom of
B15, and all 35 resolve now.

`COMPND` also gained `OTHER_DETAILS`, from `_entity.details`, which `2GEW.pdb` has and we
were dropping.

Baseline:

```
header-check summary: 13 pair(s), 13 agree, 0 differ, 4 mmCIF with no PDB sibling
```

The four without a sibling are the small-molecule and chem_comp entries; `is_coordinate_mmcif()`
excludes chem_comp and PDB inputs from the mode altogether — a component definition has no
header to synthesize.

## The trap catalogue

`TRAPS.md`, beside this file, is the catalogue of coordinate-file properties and API
behaviours that look ordinary and are not — each with the corpus file that exhibits it,
**whether an assertion actually checks it**, and what breaks if it regresses. It also
records the current full-corpus baseline to compare a run against.

Read it before adding a check here, and **run the corpus at the close of every phase**.

## Known baseline (2026-08-10, gemmi 0.7.5, merge on)

> Superseded as a *run* baseline by the dated summary line in `TRAPS.md` (the corpus has
> grown since — `1AON.cif`, `1FFK.cif` and the PDB siblings were not in this pass). The
> per-category reasoning below is still accurate.

Per-atom data was identical across ~250,000 atoms: no differences in coordinates,
occupancy, B, element, altLoc, segID, het flag, residue name, residue number or
insertion code. `pdb3k0n.ent` and `pdb1aon.ent` compare **fully identical**. The
differences that remain are all at the container level:

- **atom-name padding** of 3-character hydrogens read from mmCIF (`"HH2 "` vs `" HH2"`) —
  gemmi is the one following the PDB convention here;
- **covalent `struct_conn` links**, which mmdb does not read from mmCIF at all — so a
  remaining link-count difference is now a deliberate *gain*, not a defect (`5E1N.cif`:
  45 `covale` + 85 `metalc` where mmdb reads none);
- **cell** absent from a multi-model mmCIF where mmdb synthesises one (2RSF);
- **chain ordering** on 1ffk (same ids, same contents, different order) — cosmetic.

Full write-up: `~/sw/bandicoot-project/notes/v0.2-gemmi-notes/`.

Because the tool is a *diff* and not an assertion of correctness, a difference it
reports is not automatically a bug in either reader — in the padding case above, gemmi
turned out to be the one following the PDB convention.

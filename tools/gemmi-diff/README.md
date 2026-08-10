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
./tools/gemmi-diff/gemmi-mmdb-diff [options] <coord-file> ...
```

| option | effect |
|---|---|
| `--dump-chains` | print each chain's id, residue count, atom count and composition, for both paths |
| `--examples N` | show up to N example differences and distinct value-shapes per field (default 3) |
| `--no-setup-entities` | skip `gemmi::setup_entities()` on path B |
| `--no-merge-chains` | skip `Structure::merge_chain_parts()` on path B — reproduces the raw `copy_to_mmdb` behaviour, chain duplicates and all |
| `--keep-hydrog-links` | keep `Hydrog` connections in path B's LINK table — reproduces the unfiltered `transfer_links_to_mmdb` behaviour |

Exit status is 0 only if every file compared identical.

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

## Known baseline (2026-08-10, gemmi 0.7.5, merge on)

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

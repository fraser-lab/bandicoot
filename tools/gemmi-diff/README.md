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

## Known baseline (2026-08-10, gemmi 0.7.5)

Per-atom data was identical across ~250,000 atoms: no differences in coordinates,
occupancy, B, element, altLoc, segID, het flag, residue name, residue number or
insertion code. The differences that *did* show up were all at the container level —
chain splitting by subchain, atom-name padding of 3-character hydrogens read from
mmCIF, `struct_conn` links, and cell handling on multi-model mmCIF.

Full write-up: `~/sw/bandicoot-project/notes/v0.2-gemmi-notes/`.

Because the tool is a *diff* and not an assertion of correctness, a difference it
reports is not automatically a bug in either reader — in the padding case above, gemmi
turned out to be the one following the PDB convention.

# Vendored Coot runtime data

These two trees are **runtime assets, not source**. They are checked in so that a
clean `git clone` can produce a working Bandicoot with no external data staging —
before v0.1.4.13 they were copied at build time out of a hand-built tree
(`~/sw/coot-builds/coot-deps`) that existed only on the maintainer's machine, which
made the source tree unbuildable by anyone else. See GitHub #4.

`scripts/build.sh` copies both into `$PREFIX/share/coot/` during install.

## `monomers/` — 115 CIF files, 5.2 MB

A **curated subset** of the CCP4 / REFMAC monomer library (`mon_lib`, version 5.60,
update 16/02/22 — see `monomers/list/mon_lib_list.cif`). The full CCP4 library is
~35,600 files and 677 MB; shipping it would inflate the distribution ~130× for
dictionaries almost no session touches.

The subset covers what a normal session needs without reaching for an external
dictionary:

- the 20 standard amino acids, plus common modified residues (MSE, SEP, TPO, PTR,
  CSO, CME, KCX, PCA)
- nucleotides A/C/G/U and DA/DC/DG/DT, plus PSU, OMG, 5MC, 3GP
- sugars: NAG, BMA, MAN, GAL, GLC, BGC, FUC
- cofactors and nucleotide ligands: ATP/ADP/AMP, GTP/GDP, ANP, NAD/NAP/NDP, FAD,
  FMN, COA, SAH, SAM, HEM, HEC, PLP, CLR
- ions and clusters: NA, K, CA, MG, ZN, FE, FE2, MN, CU, NI, CO, CD, CL, BR, IOD,
  SF4, FES
- buffers, cryoprotectants and solvent: GOL, EDO, PEG, PG4, PGE, PE8, 1PE, MES, EPE,
  TRS, BTB, TAM, IMD, MPD, IPA, DMS, ACT, ACY, FMT, CIT, MLI, NO3, PO4, SO4, BME,
  EBE, HOH, UNK, UNX, ACE, NH2
- `ener_lib.cif` and `list/mon_lib_list.cif`

**Refinement hard-fails without this tree** ("No dictionary group found for residue
type"), which is why `scripts/build.sh` gates on it.

**To add a monomer:** copy `<CODE>.cif` from a CCP4 installation
(`$CLIBD_MON/<lowercase-first-letter>/<CODE>.cif`) into the matching letter
directory here. Users needing a dictionary outside this set can still load one at
runtime via `File > Import CIF dictionary`, or point Bandicoot at a full CCP4
library.

## `reference-structures/` — 31 PDB files, 10 MB

Coot's reference structures, used for main-chain and side-chain reference geometry.
Taken from a Coot 0.9.8.95 installation; CCP4 9.x ships the same set under
`$CCP4/coot_py3/share/coot/reference-structures`.

## Licensing

Both trees are third-party data redistributed under their own terms — see
`THIRD_PARTY_LICENSES.md` at the repo root. Bandicoot's binary tarballs have
redistributed these exact files since v0.1.0.0; checking them in changes where they
live, not whether they are redistributed.

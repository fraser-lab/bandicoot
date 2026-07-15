# Corresponding Source — Bandicoot binary distribution

This document is the "clear directions" / written offer for the **Corresponding
Source** of the GPL- and LGPL-licensed components conveyed in the Bandicoot
binary tarball (`bandicoot-<version>-darwin-arm64.tar.gz`), as required by
GNU GPL v3 §6 and GNU LGPL v3.

License summaries for *all* bundled components (including the permissive ones
that require notices but not source) are in
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). This file covers only the
components that carry a **source-provision** obligation.

Versions below correspond to **Bandicoot 0.1.4.7** (tag `v0.1.4.7`,
commit `7708e8b`). Re-pin them whenever the build environment changes.

---

## Written offer (GPL v3 §6b)

> The Fraser Lab, University of California San Francisco, hereby offers to any
> third party who received the Bandicoot binary distribution, for a period of
> at least three (3) years, to provide the complete Corresponding Source for
> the GPL- and LGPL-licensed components it contains, for no more than the cost
> of physically performing the distribution. Requests may be directed to
> <artem.lyubimov@ucsf.edu>.

For most recipients the network locations below (GPL v3 §6d) satisfy the
obligation without invoking this offer.

---

## 1. Bandicoot's own source (GPL-3)

- **Repository:** <https://github.com/fraser-lab/bandicoot>
- **This release:** tag `v0.1.4.7` (commit `7708e8b`)
- The repository is public; it is the Corresponding Source for the
  `libcoot-*` libraries and the `coot`/`bcoot`/`coot-*` executables.

> Tag every release (`git tag v<version>`) so the exact built commit stays
> retrievable — §6 requires source that corresponds to *that* binary.

---

## 2. Bundled GPL / LGPL components — source required

Each row lists the version as built, its license, provenance in this build, and
the canonical upstream source. Where a component was built from a package
manager (conda/Homebrew), the safest compliance path is to **snapshot the
exact-version source tarball into the GitHub Release assets** rather than rely
solely on an upstream link that may rot or drop old versions.

| Component | Version (as built) | License | Provenance | Upstream source |
|---|---|---|---|---|
| **FFTW 2** | 2.1.5 *(confirm — manual build at `/opt/miniconda3/fftw2`)* | GPL-2+ | manual | <http://www.fftw.org/fftw-2.1.5.tar.gz> |
| **Clipper** | 2.1 (bioconda `2.1.20180802 h19345ea_1`) | LGPL / Clipper | conda (bioconda) | <https://github.com/bioconda/bioconda-recipes/tree/master/recipes/clipper> (recipe → upstream tarball) |
| **MMDB2** | 2.0.22 (bioconda `h697fd72_2`) | LGPL-3 | conda (bioconda) | <https://ftp.ccp4.ac.uk/opensource/mmdb2-2.0.22.tar.gz> |
| **libccp4** | 8.0.0 (bioconda `h19345ea_1`) | LGPL | conda (bioconda) | <https://ftp.ccp4.ac.uk/opensource/libccp4-8.0.0.tar.gz> |
| **SSM** | 1.4 (bioconda `haef7865_1`) | LGPL | conda (bioconda) | <https://ftp.ccp4.ac.uk/opensource/ssm-1.4.tar.gz> |
| **libart_lgpl** | 2.3.21 | LGPL-2+ | source (`~/sw/canvas-deps`) | <https://download.gnome.org/sources/libart_lgpl/2.3/libart_lgpl-2.3.21.tar.bz2> |
| **libgnomecanvas** | 2.30.3 | LGPL-2+ | source (`~/sw/canvas-deps`) | <https://download.gnome.org/sources/libgnomecanvas/2.30/libgnomecanvas-2.30.3.tar.bz2> |
| **GooCanvas** | 1.0.0 | LGPL-2 | source (`~/sw/canvas-deps`) | <https://download.gnome.org/sources/goocanvas/1.0/goocanvas-1.0.0.tar.bz2> |
| **librsvg** | 2.62.1 | LGPL-2+ | Homebrew | <https://download.gnome.org/sources/librsvg/2.62/librsvg-2.62.1.tar.xz> |
| **gdk-pixbuf** (loaders) | 2.44.6 | LGPL-2+ | Homebrew | <https://download.gnome.org/sources/gdk-pixbuf/2.44/gdk-pixbuf-2.44.6.tar.xz> |
| **GNU gettext** (libintl) | 0.25.1 | LGPL-2+ | conda | <https://ftp.gnu.org/gnu/gettext/gettext-0.25.1.tar.gz> |
| **GNU libiconv** | 1.18 | LGPL-2+ | conda | <https://ftp.gnu.org/gnu/libiconv/libiconv-1.18.tar.gz> |

Notes:
- **FFTW 2** is the only *GPL* (as opposed to LGPL) library here; 2.1.5 is the
  final FFTW-2 release. Confirm the version of the manual build under
  `/opt/miniconda3/fftw2` and pin it exactly.
- The four **bioconda** scientific libs (Clipper, MMDB2, libccp4, SSM) should be
  matched to the *exact* bioconda build that produced the shipped `.dylib`s. The
  conda package tarballs themselves are the most faithful snapshot — archive
  them alongside the release if you don't mirror upstream.
- **GNOME URLs** follow the strict `sources/<name>/<major.minor>/<name>-<ver>.tar.*`
  pattern; verify the file resolves before publishing (older point releases are
  occasionally pruned — snapshot if in doubt).

---

## 3. Permissive components — notice only, no source obligation

These are bundled but their licenses (Apache-2.0, BSD, MIT, PSF, zlib, IJG,
FreeType, libpng, font licenses) do **not** require distributing source — only
retaining their notices, which [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
and the license files inside the tarball cover. For reference (as built):

- LLVM libc++ 21.1.8 (Apache-2.0 w/ LLVM exception) — conda-forge
- CPython 3.13 (PSF) + the bundled Python packages (BSD/MIT/PSF/Apache)
- HarfBuzz 14.2.0, FreeType 2.14.2, libpng 1.6.58, libjpeg-turbo 3.1.4.1,
  libtiff 4.7.1 — Homebrew/conda
- `reduce` 4.14 / `probe` 2.26 (Apache-2.0, Richardson Lab, Duke) —
  <https://github.com/rlabduke/reduce>, <https://github.com/rlabduke/probe>

---

## 4. Maintenance checklist (per release)

1. `git tag v<version>` and push the tag (fixes §6 correspondence).
2. Re-run the version probes and update Section 2 if any dependency moved.
3. Confirm each Section 2 URL resolves, **or** attach the exact-version source
   tarballs (and/or conda package files) to the GitHub Release as assets.
4. Update the year/contact in the §6b written offer if it changes.

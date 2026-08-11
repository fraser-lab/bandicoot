# Building Bandicoot from source

These instructions cover **macOS Tahoe (26.x) on Apple Silicon**. The build has
not been validated on other configurations.

As of v0.1.4.13 a clean checkout builds on any Mac with Homebrew and Miniconda —
no hand-staged trees, and nothing that resolves into a particular user's home
directory. If a dependency is missing, `scripts/build.sh` stops before compiling
anything and prints the variable name and the command that fixes it.

## Quick version

```sh
git clone <repo> && cd bandicoot
brew install autoconf automake libtool pkg-config \
             gtk+ gtkglext freeglut gsl boost cairo libpng sqlite bzip2
conda install -c conda-forge clipper-cif clipper-contrib clipper-core \
             clipper-ccp4 clipper-mmdb clipper-minimol clipper-phs clipper-cns \
             mmdb2 ssm ccp4mg-util fftw
# FFTW 2 (see 1.3) then:
./scripts/build_canvas_deps.sh     # libart / libgnomecanvas / goocanvas
./scripts/bootstrap.sh             # autotools
./scripts/build.sh                 # configure + make + install
```

---

## 1. Prerequisites

### 1.1 Homebrew packages

```sh
brew install \
  autoconf automake libtool pkg-config \
  gtk+ gtkglext freeglut \
  gsl boost cairo libpng sqlite \
  bzip2
```

The `gtk+` formula is GTK **2** (Quartz backend). GTK 3 is not used.

If Homebrew is not at `/opt/homebrew`, pass `BREW_PREFIX=/your/prefix` to both
scripts below. Nothing hard-codes the location.

### 1.2 Miniconda packages

Bandicoot links several crystallography libraries that ship through conda
rather than Homebrew:

```sh
conda install -c conda-forge \
  clipper-cif clipper-contrib clipper-core clipper-ccp4 \
  clipper-mmdb clipper-minimol clipper-phs clipper-cns \
  mmdb2 ssm ccp4mg-util fftw
```

The embedded Python interpreter also comes from here. If your conda is not at
`/opt/miniconda3`, pass `CONDA_PREFIX=/your/conda`.

### 1.3 FFTW 2 (single-precision, legacy)

Coot needs legacy single-precision FFTW **2**; FFTW 3 does not provide it and no
current package manager carries it. Build it once:

```sh
FFTW_PREFIX=$PWD/deps/fftw2          # or anywhere you like
mkdir -p "$FFTW_PREFIX/src" && cd "$FFTW_PREFIX/src"
curl -O http://www.fftw.org/fftw-2.1.5.tar.gz
tar xf fftw-2.1.5.tar.gz && cd fftw-2.1.5
./configure --prefix="$FFTW_PREFIX" --enable-float --enable-shared
make -j8 && make install
```

`build.sh` auto-detects it at `<repo>/deps/fftw2`, `$CONDA_PREFIX/fftw2`, or
`$BREW_PREFIX/opt/fftw2`; otherwise pass `FFTW_PREFIX=...`.

### 1.4 Canvas libraries (libart / libgnomecanvas / goocanvas)

Homebrew dropped all three years ago — they are GTK2-era. Build them with the
script in this repo:

```sh
./scripts/build_canvas_deps.sh          # installs into <repo>/deps/canvas
```

It fetches pinned upstream releases (libart_lgpl 2.3.21, libgnomecanvas 2.30.3,
goocanvas 1.0.0), builds them, and writes the pkg-config shims the GTK2-Quartz
stack needs (`bzip2`, and empty `x11`/`xcb`/`xext`/`xrender` entries that cairo
and freetype2 name in their private `Requires` but that no Quartz build links
against).

Useful knobs:

| Variable | Meaning |
|---|---|
| `CANVAS_PREFIX` | install prefix (default `<repo>/deps/canvas`) |
| `CANVAS_SRC_CACHE` | directory of pre-downloaded tarballs — lets the script run **offline** |
| `CANVAS_FORCE=1` | rebuild even if already installed |
| `JOBS` | parallel make jobs |

The script is idempotent: components whose `.pc` file is already present are
skipped.

> **Why this matters:** Coot's configure sets `HAVE_GNOME_CANVAS` only if it
> finds these. Without them you get a Bandicoot with no Sequence View, no 2D
> ligand editor, and no geometry graphs (Ramachandran, density fit, rotamer
> analysis, Kleywegt) — and, before v0.1.4.13, no warning that anything was
> wrong. `build.sh` now refuses to build rather than silently dropping them.

### 1.5 Runtime data — nothing to do

The monomer dictionary and reference structures are **checked into the repo**
under `data/coot-data/` (see its `README.md`). No CCP4 installation, no
download, no staging step. `build.sh` installs them into
`$PREFIX/share/coot/`.

### 1.6 Optional: MolProbity tools

`probe` and `reduce` power Local Probe Dots. `build.sh` probes for a CCP4
installation (`$CCP4`, `/opt/ccp4/ccp4-9`, `/usr/local/ccp4/ccp4-9`,
`/Applications/ccp4-9`, SBGrid's `/programs/*/ccp4/*/ccp4-9`) and copies them if
found. **Missing tools are a warning, not an error** — everything else builds
and runs. Override with `PROBE_SRC`, `REDUCE_SRC`, `REDUCE_HET_SRC`, or point
`CCP4_ROOT` at your CCP4.

---

## 2. Bootstrap autotools

`configure` and `aclocal.m4` are not checked in. Run once after a fresh
checkout, and again after editing `configure.ac` / `Makefile.am`:

```sh
./scripts/bootstrap.sh
```

This wraps `glibtoolize`, `aclocal`, `autoconf`, and `automake` with the right
include paths. (`build.sh` runs it automatically if `configure` is absent.)

---

## 3. Configure + build + install

```sh
./scripts/build.sh
```

Every location is an environment variable, and none defaults into a home
directory except the install root:

| Variable | Default | Meaning |
|---|---|---|
| `PREFIX` | `$HOME/sw/bandicoot-install` | where the build is installed |
| `BREW_PREFIX` | `brew --prefix`, else `/opt/homebrew` | Homebrew root |
| `CONDA_PREFIX` | `/opt/miniconda3` | conda root; also the embedded Python |
| `CANVAS_PREFIX` | `<repo>/deps/canvas` | output of `build_canvas_deps.sh` |
| `FFTW_PREFIX` | auto-detected | single-precision FFTW 2 |
| `COOT_DATA_SRC` | `<repo>/data/coot-data` | monomer dictionary + reference structures |
| `PROBE_SRC` / `REDUCE_SRC` / `REDUCE_HET_SRC` | probed from CCP4 | MolProbity tools |
| `JOBS` | CPU count | parallel make jobs |
| `BANDICOOT_CLEAN=1` | — | force a clean recompile |
| `BUILD_SKIP_CHECKS=1` | — | skip the closing dependency-closure gate (don't) |

What the script does:

1. **Preflight** — checks every prefix above and fails with the variable name
   and the fix if one is unmet. Nothing is compiled until they all resolve.
2. Generates `src/bandicoot-build-id.h`
3. `configure`, `make -j`, `make install` into a staging `DESTDIR`, then moves
   the tree to `$PREFIX`
4. Prunes dev artifacts (`lib/*.a`, `lib/*.la`, `include/`) unless
   `BANDICOOT_TOOLKIT=1`
5. Rewrites Mach-O paths to `@rpath` and bundles the conda libraries, the
   gdk-pixbuf loaders, the whole Homebrew GTK2-Quartz stack, and the MolProbity
   tools, so the result has **no external runtime dependency**
6. Installs the monomer dictionary and reference structures from
   `data/coot-data/`, and the Raleigh GTK theme from Homebrew's gtk+2
7. Re-signs the tree (required — an unsigned relocated binary is `Killed: 9` by
   dyld on launch)
8. **Dependency-closure gate** — `check_install.sh` fails the build if any
   shipped Mach-O names a library that is not shipped

Launch:

```sh
$PREFIX/bin/bcoot
```

---

## 4. Verifying the build

1. Splash appears for ~2.5 s, then the main window opens; the title bar reads
   `BANDICOOT <version> (Coot 0.9.8.95)`.
2. `File > Open Coordinates…` opens a working file dialog; a PDB displays in
   the GL view.
3. Shift-click an atom — a label in Menlo appears next to it.
4. `Draw > Sequence View` opens — this is the canvas stack working. If the menu
   item does nothing, `HAVE_GNOME_CANVAS` was off at compile time.
5. Refine a residue — if this fails with "No dictionary group found for residue
   type", the monomer dictionary did not install.

---

## 5. Troubleshooting

- **`build.sh` stops at "unmet build dependencies"** — read the block: it names
  the variable, its current value, the exact missing path, and the command that
  fixes it. This is the intended behaviour, not a bug.
- **`autogen.sh` doesn't work** — expected; use `./scripts/bootstrap.sh`. The
  upstream `autogen.sh` calls `libtoolize` (named `glibtoolize` under Homebrew)
  and hard-codes a Fink macros path.
- **`pkg-config: bzip2 not found`** — `build_canvas_deps.sh` writes a `bzip2.pc`
  shim into `$CANVAS_PREFIX/lib/pkgconfig`. If Homebrew's bzip2 keg is
  elsewhere, set `BZIP2_PREFIX` and re-run it.
- **`pkg-config: x11 not found`** (or `xcb`, `xext`, `xrender`) — same fix.
  These are empty shims: cairo and freetype2 name them in private `Requires`,
  and a Quartz build never links them.
- **Canvas libraries fail to compile** with implicit-declaration or
  pointer-conversion errors — `build_canvas_deps.sh` already downgrades those to
  warnings via `CFLAGS`; if you are building them by hand, you need
  `-Wno-implicit-function-declaration -Wno-int-conversion
  -Wno-incompatible-function-pointer-types`. These packages predate the clang
  versions that made those errors fatal.
- **`Killed: 9` immediately on launch** — the tree was relocated after signing.
  Re-run `./scripts/codesign-install.sh $PREFIX`, or run the installed
  `setup.sh`.
- **`Couldn't find pixmap file: bandicoot-splash.png`** — launch through
  `bin/bcoot`, not `libexec/Bandicoot`; the wrapper exports `COOT_DATA_DIR`.
- **GL window is tiny / quarter-size** — the Retina viewport patch in
  `src/globjects.cc` isn't building its `__APPLE__` branch.

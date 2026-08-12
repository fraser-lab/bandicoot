# Building Bandicoot from source

Target: **macOS Tahoe (26.x), Apple Silicon**. Not validated elsewhere.

> Looking for the prebuilt app instead? See [INSTALL.md](INSTALL.md). The binary
> tarball is fully self-contained and needs none of this.

## 1. Install Homebrew and Miniconda

If you don't already have them: [brew.sh](https://brew.sh),
[Miniconda](https://docs.conda.io/projects/miniconda/).

## 2. Install the packaged dependencies

```sh
brew install autoconf automake libtool pkg-config \
             gtk+ gtkglext freeglut gsl boost cairo libpng sqlite bzip2

conda install -c bioconda clipper libccp4 mmdb2 ssm
```

`gtk+` is GTK **2** (Quartz backend). The conda channel is **bioconda**, not
conda-forge, and the package is `clipper` — not the `clipper-*` set some older
instructions listed.

## 3. Build the unpackaged dependencies

```sh
./scripts/build_deps.sh
```

Downloads, verifies and builds the four libraries no package manager carries —
FFTW 2, libart_lgpl, libgnomecanvas, goocanvas 1.x — into `deps/`.
Takes a few minutes. Re-runs are cached and skip what's already built.

## 4. Build Bandicoot

```sh
./scripts/bootstrap.sh     # generates ./configure (once per checkout)
./scripts/build.sh
```

Installs to `~/sw/bandicoot-install`. Launch:

```sh
~/sw/bandicoot-install/bin/bcoot
```

That's the whole build.

---

## Options

Set as environment variables on `./scripts/build.sh`:

| Variable | Default | Meaning |
|---|---|---|
| `PREFIX` | `~/sw/bandicoot-install` | install location |
| `BREW_PREFIX` | `brew --prefix` | Homebrew root |
| `CONDA_PREFIX` | `/opt/miniconda3` | conda root; also supplies the embedded Python |
| `JOBS` | CPU count | parallel make jobs |
| `BANDICOOT_CLEAN=1` | — | force a full recompile |

`build.sh` checks every dependency before compiling anything. If one is missing
it stops and tells you the variable, the missing path, and the command that
fixes it.

## Verifying it works

1. Splash, then the main window; title bar reads `BANDICOOT <version> (Coot 0.9.8.95)`.
2. `File > Open Coordinates…` — a PDB displays in the GL view.
3. `Draw > Sequence View` opens. If this does nothing, the canvas libraries from
   step 3 weren't found at configure time.
4. Refine a residue. "No dictionary group found for residue type" means the
   monomer dictionary didn't install.

## Troubleshooting

- **`conda install` says `PackagesNotFoundError`** — you're using conda-forge
  and/or the old `clipper-*` package names. Use the step 2 command as written.
- **`autogen.sh` doesn't work** — use `./scripts/bootstrap.sh`. Upstream's
  `autogen.sh` calls `libtoolize` (Homebrew names it `glibtoolize`) and
  hard-codes a Fink path.
- **`pkg-config: bzip2 not found`**, or `x11`/`xcb`/`xext`/`xrender` not found —
  re-run `./scripts/build_deps.sh`; it writes the `.pc` files these need. If
  Homebrew's bzip2 keg is somewhere unusual, set `BZIP2_PREFIX`.
- **`Killed: 9` on launch** — the tree was modified after signing. Run
  `./scripts/codesign-install.sh $PREFIX`.
- **`Couldn't find pixmap file: bandicoot-splash.png`** — launch via `bin/bcoot`,
  not `libexec/Bandicoot`; the wrapper sets `COOT_DATA_DIR`.

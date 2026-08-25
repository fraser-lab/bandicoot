# Building Bandicoot from source

Target: **macOS Tahoe (26.x), Apple Silicon**. Not validated elsewhere.

> Looking for the prebuilt app instead? See [INSTALL.md](INSTALL.md). The binary
> tarball is fully self-contained and needs none of this.

## Before you start

Make sure that Xcode Command Line Tools are installed:
```
xcode-select --install
```

## 1. Install Homebrew and Miniconda

If you don't already have them: [brew.sh](https://brew.sh),
[Miniconda](https://docs.conda.io/projects/miniconda/).

**Note**: if you use a named conda environment other than `base`, include `pip` when you create it and activate afterwards:
```
conda create -n <env_name> python=<python_version> pip
conda activate
```


## 2. Install the packaged dependencies

Install Homebrew-based dependencies:
```sh
brew install autoconf automake libtool intltool pkg-config \
             gtk+ gtkglext freeglut gsl boost cairo libpng sqlite bzip2
```

Install Conda-based dependencies:
```
conda install -c bioconda clipper libccp4 mmdb2 ssm
```

**NOTE**: `gtk+` is GTK **2** (Quartz backend). The conda channel these packages come from is **bioconda** (hence `-c bioconda`) and the Clipper library package is `clipper` — not the `clipper-*` set some older instructions listed.


## 3. Clone the Bandicoot repository

```
git clone https://github.com/fraser-lab/bandicoot.git
cd bandicoot
```


## 4. Build the unpackaged dependencies

```sh
./scripts/build_deps.sh
```

**NOTE**: This script downloads, verifies and builds the four libraries no package manager carries — FFTW 2, libart_lgpl, libgnomecanvas, goocanvas 1.x — into `deps/`. Takes a few minutes. Re-runs are cached and skip what's already built.


## 5. Build Bandicoot

```sh
./scripts/bootstrap.sh     	# generates ./configure
./scripts/build.sh			# configure, make, install, bundle all deps, strip rpaths, codesign, gate
```

**NOTE**: That's the whole build. `./scripts/build.sh` _automatically_ runs the following individual scripts:

```
make_relocatable.sh
bundle_conda_deps.sh
bundle_local_deps.sh
bundle_pixbuf_loaders.sh
bundle_gtk_immodules.sh
bundle_homebrew_deps.sh
bundle_external_tools.sh
strip_host_rpaths.sh
codesign-install.sh
check_install.sh
```

At the end of `./scripts/build.sh`, Bandicoot should be installed to ~/sw/bandicoot-install`. Launch:

```sh
~/sw/bandicoot-install/bin/bcoot
```

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
   step 4 weren't found at configure time.
4. Refine a residue. "No dictionary group found for residue type" means the
   monomer dictionary didn't install.

## Troubleshooting

- **`check_install: FAIL — <N> unresolved dep(s)`** and all libraries marked "UNRESOLVED" are `@rpath/libpython3.<X>.dylib` — you changed the conda environment's Python version between builds, leaving object files that link the previous `libpython` while the bundler ships the current one. The current Python version is noted in the build output under `embedded Python version`. Solution: `BANDICOOT_CLEAN=1 ./scripts/build.sh` which forces `make clean`.
- **`conda install` says `PackagesNotFoundError`** — you omitted `-c bioconda` or used the old `clipper-*` package names. Retry step 2 as written.
- **`autogen.sh` doesn't work** — use `./scripts/bootstrap.sh`. Upstream's `autogen.sh` calls `libtoolize` (Homebrew names it `glibtoolize`) and hard-codes a Fink path.
- **`pkg-config: bzip2 not found`**, or `x11`/`xcb`/`xext`/`xrender` not found — re-run `./scripts/build_deps.sh`; it writes the `.pc` files these need. If Homebrew's bzip2 keg is somewhere unusual, set `BZIP2_PREFIX`.
- **`Killed: 9` on launch** — the tree was modified after signing. Run `./scripts/codesign-install.sh $PREFIX`.
- **`Couldn't find pixmap file: bandicoot-splash.png`** — launch via `bin/bcoot`, not `libexec/Bandicoot`; the wrapper sets `COOT_DATA_DIR`.

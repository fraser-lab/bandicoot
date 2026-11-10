# Building Bandicoot from source

Targets:

- **macOS Tahoe (26.x), Apple Silicon**: see [macOS](#macos).
- **Linux, x86_64**: tested on Ubuntu 22.04, though buildable on 20.04; see [Linux](#linux).

> Looking for the prebuilt app instead? See [INSTALL.md](INSTALL.md). The binary
> tarballs are fully self-contained and need none of this.

# macOS

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
brew install autoconf automake libtool intltool pkg-config swig \
             gtk+ gtkglext freeglut gsl boost cairo libpng sqlite bzip2 \
             rdkit boost-python3
```

SWIG generates the Python interface (`src/coot_wrap_python.cc`, `src/coot.py`)
during the build; those files are not kept in the repository.

Install Conda-based dependencies:
```
conda install -c bioconda clipper libccp4 mmdb2 ssm
```

For the wx interface (`BANDICOOT_UI=wx`, below), also:
```sh
brew install wxwidgets
```

**NOTE**: `gtk+` is GTK **2** (Quartz backend); the wx build still links it. The conda channel these packages come from is **bioconda** (hence `-c bioconda`) and the Clipper library package is `clipper` — not the `clipper-*` set some older instructions listed.


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

At the end of `./scripts/build.sh`, Bandicoot should be installed to `~/sw/bandicoot-install`. Launch:

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
| `BANDICOOT_UI` | `gtk` | user interface toolkit: `gtk` or `wx`; give a wx build its own `PREFIX` |
| `BANDICOOT_RDKIT` | Homebrew's `rdkit` | RDKit prefix. RDKit is required: the 2D ligand view, FLEV and pyrogen are built on it |
| `BANDICOOT_CLEAN=1` | — | force a full recompile |

`build.sh` checks every dependency before compiling anything. If one is missing
it stops and tells you the variable, the missing path, and the command that
fixes it.

## Verifying it works

1. Splash, then the main window; title bar reads `BANDICOOT <version>`.
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

---

# Linux

Tested on Ubuntu 20.04, x86_64. The scripts check for Debian/Ubuntu
packages with `dpkg`; on other distributions install the equivalents and set
`BANDICOOT_SKIP_PACKAGE_CHECK=1`.

The libraries that distributions do not package, or package in a form
Bandicoot cannot use, are built from pinned, checksum-verified sources:
FFTW 2, the GNOME canvas libraries, wxWidgets (built against GTK 2), gemmi,
MMDB2, libccp4, SSM, Clipper, GSL, Python 3.13, SWIG, CMake, Boost, RDKit and
patchelf.

## 1. Install a C++20 compiler

Bandicoot and RDKit need **GCC 12 or newer**.

On Ubuntu 22.04 or newer:
```sh
sudo apt-get install gcc-12 g++-12 gobjc++-12
```

On Ubuntu 20.04, GCC 12 comes from the Ubuntu toolchain PPA:
```sh
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt-get install gcc-12 g++-12 gobjc++-12
```

`gobjc++` is needed only because `configure` probes for an Objective-C++
compiler; nothing is compiled as Objective-C++ on Linux.

## 2. Install the system packages

```sh
sudo apt-get install build-essential autoconf automake libtool pkg-config \
    intltool libxml-parser-perl cmake curl \
    libgtk2.0-dev libgail-dev libcairo2-dev libglu1-mesa-dev freeglut3-dev libxmu-dev \
    libeigen3-dev libfreetype6-dev libpng-dev libjpeg-dev libtiff-dev zlib1g-dev \
    libexpat1-dev libssl-dev libffi-dev libbz2-dev libreadline-dev libsqlite3-dev \
    liblzma-dev libncursesw5-dev uuid-dev libcurl4-openssl-dev \
    python3-dev python3-numpy python3-matplotlib
```

Both build scripts check these and print the `apt-get` line for anything
missing.

## 3. Create the dependency directory

The source-built dependencies go in `/opt/bandicoot-deps`, outside any home
directory: some of them record their install path in themselves, and a release
tarball ships them. Create it once and give it to yourself:
```sh
sudo mkdir -p /opt/bandicoot-deps
sudo chown $(id -un): /opt/bandicoot-deps
```

## 4. Clone the Bandicoot repository

```sh
git clone https://github.com/fraser-lab/bandicoot.git
cd bandicoot
```

## 5. Build the dependencies

```sh
CC=gcc-12 CXX=g++-12 ./scripts/build_deps_linux.sh
```

This downloads, verifies and builds every library listed above, and needs
network access throughout (RDKit's configure step also downloads three small
libraries of its own). Expect one to two hours on a first run; RDKit and
wxWidgets take the longest. Re-runs skip whatever is already built.

## 6. Build Bandicoot

```sh
CC=gcc-12 CXX=g++-12 ./scripts/build-linux.sh
```

This configures, compiles and installs Bandicoot to `~/sw/bandicoot-install`.
Launch:
```sh
~/sw/bandicoot-install/bin/bcoot
```

This install runs on the machine it was built on: it finds the dependencies in
`/opt/bandicoot-deps`. For a tarball that runs elsewhere, see
[Linux release tarball](#linux-release-tarball).

Both scripts ignore `PATH`, `LD_LIBRARY_PATH` and `PKG_CONFIG_PATH` from your
environment, so a site software tree on those cannot substitute its own
libraries for the system's or the dependency tree's.

## Linux options

Set as environment variables on `./scripts/build-linux.sh`:

| Variable | Default | Meaning |
|---|---|---|
| `PREFIX` | `~/sw/bandicoot-install` | install location |
| `DEPS_PREFIX` | `/opt/bandicoot-deps` | the dependencies from `build_deps_linux.sh` |
| `JOBS` | CPU count, at most one per 3 GB of RAM | parallel make jobs |
| `BANDICOOT_INCREMENTAL=1` | — | skip `make clean`, recompiling only what changed |
| `BANDICOOT_SKIP_PACKAGE_CHECK=1` | — | skip the system-package check (non-Debian systems) |
| `BANDICOOT_ASAN=1` | — | build with AddressSanitizer, for memory-error diagnosis |

`build_deps_linux.sh` takes `DEPS_PREFIX` and `JOBS` too, and `DEPS_FORCE=1`
rebuilds every dependency.

## Linux release tarball

```sh
CC=gcc-12 CXX=g++-12 ./scripts/release-linux.sh
```

builds Bandicoot in release mode, bundles every library it loads except those
that must come from the user's system (the C library, the C++ runtime,
OpenGL, X11, fontconfig and a few others), checks the result, and writes
`../releases/<version>/bandicoot-<version>-linux-x86_64.tar.gz` and its
`.sha256`. It needs a clean tree whose commit is pushed. Run it on the oldest
distribution release the tarball should support: the result runs there and on
anything newer. `--skip-build` reuses the previous run's build, for finishing a
run that stopped after compiling.

## Linux troubleshooting

- **The machine freezes during the build** — it ran out of memory. Set `JOBS`
  lower, e.g. `JOBS=2`.
- **`configure did not enable RDKit with Boost.Python`** — `config.log` shows
  the failing check; search it for `boost_python`.
- **`GCC 12 or newer is needed`** — install it as in step 1 and pass
  `CC=gcc-12 CXX=g++-12`.
- **After upgrading the distribution** — rebuild every dependency with
  `DEPS_FORCE=1 ./scripts/build_deps_linux.sh`: they were built against the
  previous release's system libraries.

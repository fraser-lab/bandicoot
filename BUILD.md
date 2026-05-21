# Building Bandicoot from source

These instructions cover **macOS Tahoe (26.x) on Apple Silicon**. The build
has not been validated on other configurations.

## 1. Prerequisites

### Homebrew packages

```sh
brew install \
  autoconf automake libtool pkg-config \
  gtk+ gtkglext freeglut \
  gsl boost cairo libpng sqlite \
  bzip2
```

The `gtk+` formula here is GTK 2 (Quartz backend). GTK 3 is *not* used
by Bandicoot.

### Miniconda packages

Bandicoot links against several scientific libraries that ship through
Miniconda (or any conda) rather than Homebrew:

```sh
# Install Miniconda to /opt/miniconda3 if you don't already have it.
# Then in a base environment, install the heavy crystallography deps:
conda install -c conda-forge \
  clipper-cif clipper-contrib clipper-core clipper-ccp4 \
  clipper-mmdb clipper-minimol clipper-phs clipper-cns \
  mmdb2 ssm ccp4mg-util fftw
```

If your conda lives somewhere other than `/opt/miniconda3`, set
`CONDA_PREFIX` when you invoke the build script.

### FFTW2 (single-precision, legacy)

Bandicoot still needs the legacy single-precision FFTW2 library; the
modern FFTW3 distributions do not provide it. The simplest way is to
build it once from source and install into a stable prefix
(`~/sw/coot-deps` by convention here):

```sh
# Adjust PREFIX as you like; pass it as FFTW_PREFIX to the Bandicoot
# build script below.
PREFIX=$HOME/sw/coot-deps
mkdir -p $PREFIX/src && cd $PREFIX/src
curl -O http://www.fftw.org/fftw-2.1.5.tar.gz
tar xf fftw-2.1.5.tar.gz && cd fftw-2.1.5
./configure --prefix=$PREFIX --enable-float --enable-shared
make -j8 && make install
```

## 2. Bootstrap autotools

The `configure` script and `aclocal.m4` are not checked in; you generate
them from the `.ac` / `.am` sources. Run once after a fresh checkout:

```sh
cd /path/to/bandicoot
./scripts/bootstrap.sh
```

This wraps `glibtoolize`, `aclocal`, `autoconf`, and `automake` with
the right include paths for Homebrew autotools.

## 3. Configure + build + install

```sh
# Customisable via environment variables, all optional:
#   PREFIX        install root          (default: $HOME/sw/bandicoot-install)
#   FFTW_PREFIX   FFTW2 install         (default: $HOME/sw/coot-deps)
#   CONDA_PREFIX  miniconda install     (default: /opt/miniconda3)
#   BREW_PREFIX   homebrew install      (default: /opt/homebrew)
./scripts/build.sh
```

The script:

1. Re-runs `configure` with the right flags
2. `make -j$(sysctl -n hw.ncpu)`
3. `make install`
4. Adds a `bcoot` symlink in `$PREFIX/bin`
5. Copies the runtime asset directories (monomer dictionaries, reference
   structures, GTK theme) from `$FFTW_PREFIX/share/coot/` into
   `$PREFIX/share/coot/` if they're not already there

After it finishes, launch with:

```sh
$PREFIX/bin/bcoot
```

## 4. Verifying the build

If you want a quick smoke test:

1. Splash should appear for ~2.5 s, then the Bandicoot window opens with
   the title bar identifying the build date.
2. `File > Open Coordinates...` should open a working file dialog;
   loading a PDB should display the structure in the GL view.
3. Shift-click an atom — a label in Menlo should appear next to it.
4. `Edit > Preferences > Font size` — small / medium / large should
   change label size.

## 5. Troubleshooting

- **`autogen.sh` doesn't work** — that's expected. Use
  `./scripts/bootstrap.sh` instead. The upstream `autogen.sh` calls
  `libtoolize` which on macOS Homebrew is named `glibtoolize`, and
  hard-codes a Fink macros path.
- **`pkg-config: bzip2 not found`** — the `bootstrap.sh` script adds
  the Homebrew macOS-version-specific pkgconfig path. If the auto-detect
  of macOS major version (`sw_vers -productVersion | cut -d. -f1`) gives
  the wrong directory, override `BREW_PC_OSDIR` in your env.
- **`Couldn't find pixmap file: ...bandicoot-splash.png`** at startup —
  the splash PNG didn't get installed. Re-run `make install` and
  confirm `$PREFIX/share/coot/pixmaps/bandicoot-splash.png` exists.
- **GL window is tiny / quarter-size** — you're missing the Retina
  viewport patch. Check that `src/globjects.cc` builds the `__APPLE__`
  branch (i.e. that you're on macOS and `__APPLE__` is defined by the
  compiler).

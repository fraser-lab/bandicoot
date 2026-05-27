#!/bin/bash
# Bandicoot: configure + build + install on macOS Tahoe / Apple Silicon.
# Customise via environment variables (all optional):
#   PREFIX        install root         (default: $HOME/sw/bandicoot-install)
#   FFTW_PREFIX   FFTW2 install        (default: $HOME/sw/coot-deps)
#   CONDA_PREFIX  miniconda root       (default: /opt/miniconda3)
#   BREW_PREFIX   homebrew root        (default: /opt/homebrew or `brew --prefix`)
#   JOBS          parallel make jobs   (default: number of CPUs)
set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

PREFIX="${PREFIX:-$HOME/sw/bandicoot-install}"
FFTW_PREFIX="${FFTW_PREFIX:-$HOME/sw/coot-deps}"
CONDA_PREFIX="${CONDA_PREFIX:-/opt/miniconda3}"
BREW_PREFIX="${BREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

# Homebrew on macOS supplies version-specific pkg-config dirs to fill in
# entries that the system SDK provides (e.g. bzip2.pc lives under
# $BREW_PREFIX/Library/Homebrew/os/mac/pkgconfig/<macOS_major>/).
MAC_MAJOR="${MAC_MAJOR:-$(sw_vers -productVersion | cut -d. -f1)}"
BREW_PC_OSDIR="${BREW_PC_OSDIR:-${BREW_PREFIX}/Library/Homebrew/os/mac/pkgconfig/${MAC_MAJOR}}"

if [ ! -x ./configure ]; then
    echo "==> ./configure not found; running bootstrap"
    ./scripts/bootstrap.sh
fi

# Python embed flags. python3-config --ldflags omits -lpython3.X since
# Python 3.8 — you have to ask for --embed explicitly. Coot's autotools
# macro pre-dates that change, so we backfill via LDFLAGS here.
PYTHON_EMBED_LDFLAGS="$(${CONDA_PREFIX:-/opt/miniconda3}/bin/python3-config --ldflags --embed 2>/dev/null)"

export CPPFLAGS="-I${CONDA_PREFIX}/include -I${PREFIX}/include -I${BREW_PREFIX}/include"
export LDFLAGS="-L${CONDA_PREFIX}/lib -L${PREFIX}/lib -L${BREW_PREFIX}/lib \
    -Wl,-rpath,${CONDA_PREFIX}/lib -Wl,-rpath,${PREFIX}/lib \
    ${PYTHON_EMBED_LDFLAGS}"
# -include compat/python23-shim.hh: force-include the Py2→Py3 macro shim
# into every TU so the ~350 PyString_*/PyInt_* call sites scattered
# through Coot's c-interface compile against Python 3 without per-file
# edits. The shim is a no-op when Python.h hasn't been pulled in.
# See compat/python23-shim.hh for details.
SHIM_INCLUDE="-include ${REPO_ROOT}/compat/python23-shim.hh"
export CXXFLAGS="-g -O2 -Wall -Wno-unused -std=c++14 ${SHIM_INCLUDE}"
export CFLAGS="-g -O2 -Wall -Wno-unused ${SHIM_INCLUDE}"

export PKG_CONFIG_PATH="\
${BREW_PREFIX}/lib/pkgconfig:\
${BREW_PREFIX}/share/pkgconfig:\
${BREW_PC_OSDIR}:\
${PREFIX}/lib/pkgconfig:\
${CONDA_PREFIX}/lib/pkgconfig"

# Bypass the libgnomecanvas check; Bandicoot doesn't need the 2D ligand
# editor.
export GNOME_CANVAS_CFLAGS=""
export GNOME_CANVAS_LIBS=""

echo "==> ./configure --prefix=${PREFIX}"
./configure \
    --prefix="${PREFIX}" \
    --with-fftw-prefix="${FFTW_PREFIX}" \
    --without-gnomecanvas \
    --with-glut-prefix="${BREW_PREFIX}" \
    --with-boost="${BREW_PREFIX}" \
    --with-python="${CONDA_PREFIX}" \
    PYTHON="${CONDA_PREFIX}/bin/python3" \
    PYTHON_CONFIG="${CONDA_PREFIX}/bin/python3-config"
# v0.1.0.0: --with-python re-enabled. Coot's C interface is Py2-flavoured
# throughout; the Py2→Py3 macro shim at compat/python23-shim.hh
# (force-included via CXXFLAGS above) handles the vast majority of
# PyString_*/PyInt_* sites. Per-file fixes only needed where return
# types differ (PyUnicode_AsUTF8 returns const char*) or semantics
# differ (PyString_Check accepted bytes on Py2, PyUnicode_Check
# doesn't on Py3). Without --with-python, Phenix's socket listener
# stays inert and pandda.inspect can't launch — see
# [[bandicoot-coot-py-broken]] for the full backstory.

echo "==> make -j${JOBS}"
make -j"${JOBS}"

echo "==> make install"
make install

# Rewrite hard-coded Mach-O paths to @rpath / @executable_path form so
# the install can be moved or packaged into a portable tarball.
echo "==> make_relocatable.sh ${PREFIX}"
"${REPO_ROOT}/scripts/make_relocatable.sh" "${PREFIX}"

# Copy clipper / mmdb2 / ssm / ccp4c / fftw2 / libc++ out of the conda
# prefix into the install tree so the tarball stands alone — users don't
# need to install those packages via conda after extracting.
echo "==> bundle_conda_deps.sh ${PREFIX}"
"${REPO_ROOT}/scripts/bundle_conda_deps.sh" "${PREFIX}" "${CONDA_PREFIX}"

# Bundle the gdk-pixbuf SVG loader (from Homebrew's librsvg) + all
# raster loaders so Coot's .svg toolbar icons render on testers'
# machines without requiring `brew install librsvg`. See
# scripts/bundle_pixbuf_loaders.sh header.
echo "==> bundle_pixbuf_loaders.sh ${PREFIX}"
"${REPO_ROOT}/scripts/bundle_pixbuf_loaders.sh" "${PREFIX}" "${BREW_PREFIX}"

# Bundle external CLI tools (currently: `probe` for Local Probe Dots).
# Override the probe source via PROBE_SRC=<path>; default targets the
# CCP4 9.0.014_arm install path.
echo "==> bundle_external_tools.sh ${PREFIX}"
"${REPO_ROOT}/scripts/bundle_external_tools.sh" "${PREFIX}" "${PROBE_SRC:-}"

# Add the bcoot symlink (the wrapper computes its prefix from $0's
# location, so a symlink in the same bin dir works).
ln -sf coot "${PREFIX}/bin/bcoot"
echo "==> created ${PREFIX}/bin/bcoot"

# Asset directories: copy from FFTW_PREFIX/share/coot/ (which is the
# Coot 0.9 install used as the dictionary / reference-structure source)
# if they're not already in PREFIX.
for d in lib/data/monomers reference-structures; do
    if [ ! -d "${PREFIX}/share/coot/${d}" ] && \
       [ -d "${FFTW_PREFIX}/share/coot/${d}" ]; then
        echo "==> copying ${d} from ${FFTW_PREFIX}/share/coot/"
        mkdir -p "${PREFIX}/share/coot/${d%/*}"
        cp -R "${FFTW_PREFIX}/share/coot/${d}" "${PREFIX}/share/coot/${d}"
    fi
done

if [ ! -d "${PREFIX}/share/themes/Raleigh" ] && \
   [ -d "${FFTW_PREFIX}/share/themes/Raleigh" ]; then
    echo "==> copying Raleigh GTK theme"
    mkdir -p "${PREFIX}/share/themes"
    cp -R "${FFTW_PREFIX}/share/themes/Raleigh" "${PREFIX}/share/themes/Raleigh"
fi

# Drop the end-user setup script + install instructions at the install
# root so the tarball ships with them. Users extract, read INSTALL.md,
# and run ./setup.sh once.
if [ -f "${REPO_ROOT}/scripts/setup-install.sh" ]; then
    cp "${REPO_ROOT}/scripts/setup-install.sh" "${PREFIX}/setup.sh"
    chmod +x "${PREFIX}/setup.sh"
    echo "==> copied setup.sh to ${PREFIX}/setup.sh"
fi
if [ -f "${REPO_ROOT}/INSTALL.md" ]; then
    cp "${REPO_ROOT}/INSTALL.md" "${PREFIX}/INSTALL.md"
    echo "==> copied INSTALL.md to ${PREFIX}/INSTALL.md"
fi

echo ""
echo "Bandicoot installed in ${PREFIX}"
echo "Launch with: ${PREFIX}/bin/bcoot"

#!/bin/bash
# Bandicoot: configure + build + install on Linux (wx interface).
# The macOS build is scripts/build.sh; both build from the same sources.
#
#     ./scripts/build_deps_linux.sh    # once: source-built dependencies
#     ./scripts/build-linux.sh
#     $PREFIX/bin/bcoot
#
# Tested target: Ubuntu 24.04, x86_64. The script checks for the system
# packages it needs and prints the apt-get line for any that are missing; it
# never runs sudo itself.
#
# The install runs where it is built: libraries are found through rpaths into
# the dependency tree and through the system, nothing is bundled. It is not a
# relocatable distribution.
#
# Customise via environment variables (all optional):
#   PREFIX          install root           (default: $HOME/sw/bandicoot-install)
#   DEPS_PREFIX     source-built deps      (default: <repo>/deps, from build_deps_linux.sh)
#   PYTHON          embedded Python        (default: /usr/bin/python3)
#   JOBS            parallel make jobs     (default: number of CPUs)
#   BANDICOOT_INCREMENTAL=1  skip `make clean`
#   BANDICOOT_SKIP_PACKAGE_CHECK=1  skip the system-package check (non-Debian systems)

set -e

if [ "$(uname)" != "Linux" ]; then
    echo "!! build-linux.sh is for Linux; on macOS use scripts/build.sh" >&2
    exit 1
fi

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

PREFIX="${PREFIX:-$HOME/sw/bandicoot-install}"
DEPS_PREFIX="${DEPS_PREFIX:-${REPO_ROOT}/deps}"
PYTHON="${PYTHON:-/usr/bin/python3}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
COOT_DATA_SRC="${REPO_ROOT}/data/coot-data"

FFTW_PREFIX="${DEPS_PREFIX}/fftw2"
CANVAS_PREFIX="${DEPS_PREFIX}/canvas"
WX_PREFIX="${DEPS_PREFIX}/wx"
GEMMI_PREFIX="${DEPS_PREFIX}/gemmi"
GSL_PREFIX="${DEPS_PREFIX}/gsl"

# --- system packages (Debian / Ubuntu) ---------------------------------------
# Crystallographic libraries come from the distribution (Debian Science);
# gobjc++ is needed only because configure probes for an Objective-C++
# compiler (no Objective-C++ is compiled on Linux).
APT_PACKAGES="
build-essential autoconf automake libtool pkg-config intltool libxml-parser-perl swig gobjc++ cmake curl
libgtk2.0-dev libgail-dev libcairo2-dev libglu1-mesa-dev freeglut3-dev libxmu-dev
libpng-dev libjpeg-dev libtiff-dev zlib1g-dev libexpat1-dev libsqlite3-dev
libboost-dev
libclipper-dev libccp4-dev libmmdb2-dev libssm-dev
python3-dev python3-numpy python3-matplotlib
"
if [ -z "${BANDICOOT_SKIP_PACKAGE_CHECK:-}" ]; then
    if command -v dpkg-query >/dev/null 2>&1; then
        _missing=""
        for p in ${APT_PACKAGES}; do
            if ! dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed"; then
                _missing="${_missing} $p"
            fi
        done
        if [ -n "${_missing}" ]; then
            echo "!! build-linux.sh: missing system packages:${_missing}" >&2
            echo "   install them with:" >&2
            echo "     sudo apt-get install${_missing}" >&2
            echo "   (BANDICOOT_SKIP_PACKAGE_CHECK=1 skips this check)" >&2
            exit 1
        fi
    else
        echo "==  build-linux.sh: no dpkg-query; skipping the system-package check" >&2
    fi
fi

# The compilers named by CC / CXX (or the defaults) must exist; configure only
# reports a missing one as "C compiler cannot create executables".
for _c in "${CC:-gcc}" "${CXX:-g++}"; do
    if ! command -v "${_c}" >/dev/null 2>&1; then
        echo "!! compiler not found: ${_c}" >&2
        echo "   install it (e.g. sudo apt-get install gcc-10 g++-10), or set CC / CXX" >&2
        exit 1
    fi
done

# --- source-built dependencies -----------------------------------------------
_preflight_fail=0
_need() {   # <path> <what>
    if [ ! -e "$1" ]; then
        echo "!! build-linux.sh: $2 not found: $1" >&2
        _preflight_fail=1
    fi
}
_need "${FFTW_PREFIX}/include/fftw.h"              "single-precision FFTW 2"
_need "${CANVAS_PREFIX}/lib/pkgconfig/goocanvas.pc" "goocanvas 1.x"
_need "${CANVAS_PREFIX}/lib/pkgconfig/libgnomecanvas-2.0.pc" "libgnomecanvas"
_need "${WX_PREFIX}/bin/wx-config"                 "wxWidgets (GTK 2)"
_need "${GEMMI_PREFIX}/include/gemmi/model.hpp"    "gemmi"
_need "${GSL_PREFIX}/bin/gsl-config"               "GSL"
_need "${COOT_DATA_SRC}/monomers/list/mon_lib_list.cif" "monomer dictionary (checked in)"
_need "${PYTHON}"                                  "Python"
if [ "${_preflight_fail}" != "0" ]; then
    echo "   fix: ./scripts/build_deps_linux.sh" >&2
    exit 1
fi

PYTHON_CONFIG="${PYTHON}-config"
[ -x "${PYTHON_CONFIG}" ] || PYTHON_CONFIG="$(command -v python3-config || true)"
[ -n "${PYTHON_CONFIG}" ] || { echo "!! python3-config not found (python3-dev)" >&2; exit 1; }

export PKG_CONFIG_PATH="${CANVAS_PREFIX}/lib/pkgconfig:${GSL_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
for mod in gtk+-2.0 clipper mmdb2 ssm; do
    if ! pkg-config --exists "${mod}"; then
        echo "!! build-linux.sh: pkg-config cannot find ${mod}" >&2
        _preflight_fail=1
    fi
done
[ "${_preflight_fail}" = "0" ] || exit 1

echo "==> Python: $("${PYTHON}" -c 'import sys; print(sys.version.split()[0])') (${PYTHON})"
echo "==> wxWidgets: $("${WX_PREFIX}/bin/wx-config" --version) ($("${WX_PREFIX}/bin/wx-config" --selected-config))"

# --- build-system freshness (as scripts/build.sh) ----------------------------
if [ ! -x ./configure ] || ! ./scripts/check_build_system_fresh.sh --quiet; then
    echo "==> (re)generating the build system"
    ./scripts/bootstrap.sh
    ./scripts/check_build_system_fresh.sh --write-stamp
fi

# --- version suffix (as scripts/build.sh) --------------------------------------
BANDICOOT_VERSION_STR="$(awk -F'"' '/^#define BANDICOOT_VERSION /{print $2}' src/bandicoot-version.h)"
COUNTER_FILE="${REPO_ROOT}/.build-counter"
BUILD_SUFFIX=""
if [ -n "${BANDICOOT_DEV:-}" ] || [ -f "${REPO_ROOT}/.bandicoot-dev" ]; then
    n=1
    if [ -f "${COUNTER_FILE}" ] && [ "$(awk '{print $1}' "${COUNTER_FILE}")" = "${BANDICOOT_VERSION_STR}" ]; then
        n=$(( $(awk '{print $2}' "${COUNTER_FILE}") + 1 ))
    fi
    echo "${BANDICOOT_VERSION_STR} ${n}" > "${COUNTER_FILE}"
    BUILD_SUFFIX="-u${n}"
fi
echo "==> building version ${BANDICOOT_VERSION_STR}${BUILD_SUFFIX}"
# The SWIG wrapper (src/coot_wrap_python.cc) is generated for one
# configuration but committed, so it reflects whichever build made it last
# (on macOS, usually one with RDKit). Make it regenerate for this one. The
# regenerated copy shows as modified in git; it is not meant to be committed
# from a Linux build.
touch src/c-interface.h

cat > src/bandicoot-build-id.h <<EOF
// Generated by scripts/build-linux.sh on each invocation. Do not edit; do not
// commit. See scripts/build.sh for the meaning of the suffix.
#ifndef BANDICOOT_BUILD_ID_H
#define BANDICOOT_BUILD_ID_H
#define BANDICOOT_BUILD_SUFFIX "${BUILD_SUFFIX}"
#endif
EOF
touch src/main.cc

# --- configure -----------------------------------------------------------------
# rpaths into the dependency tree so the installed program finds the
# source-built libraries without LD_LIBRARY_PATH
DEP_RPATHS="-Wl,-rpath,${PREFIX}/lib -Wl,-rpath,${FFTW_PREFIX}/lib -Wl,-rpath,${CANVAS_PREFIX}/lib -Wl,-rpath,${WX_PREFIX}/lib -Wl,-rpath,${GEMMI_PREFIX}/lib -Wl,-rpath,${GSL_PREFIX}/lib"
PYTHON_EMBED_LDFLAGS="$("${PYTHON_CONFIG}" --ldflags --embed 2>/dev/null || "${PYTHON_CONFIG}" --ldflags)"

export CPPFLAGS="-I${PREFIX}/include"
export LDFLAGS="-L${PREFIX}/lib ${DEP_RPATHS} ${PYTHON_EMBED_LDFLAGS}"
SHIM_INCLUDE="-include ${REPO_ROOT}/compat/python23-shim.hh"
# MMDB2 before 2.0.22 (e.g. Ubuntu 20.04's 2.0.5) #defines strchr and strrchr
# to its own functions, which breaks <cstring> in any file that includes it
# after MMDB2. Including <cstring> first in every C++ file avoids that.
export CXXFLAGS="-g -O2 -Wall -Wno-unused -std=c++20 -include cstring ${SHIM_INCLUDE}"
export CFLAGS="-g -O2 -Wall -Wno-unused ${SHIM_INCLUDE}"

echo "==> ./configure --prefix=${PREFIX} --with-ui=wx"
./configure \
    --prefix="${PREFIX}" \
    --with-ui=wx \
    WX_CONFIG="${WX_PREFIX}/bin/wx-config" \
    --with-fftw-prefix="${FFTW_PREFIX}" \
    --with-goocanvas-prefix="${CANVAS_PREFIX}" \
    --with-gemmi="${GEMMI_PREFIX}" \
    --with-gsl-prefix="${GSL_PREFIX}" \
    --with-python=/usr \
    PYTHON="${PYTHON}" \
    PYTHON_CONFIG="${PYTHON_CONFIG}"

# configure builds without refinement, quietly, when its GSL test fails
if ! grep -q "^COOT_GSL_FLAGS = -DHAVE_GSL" src/Makefile; then
    echo "!! build-linux.sh: configure did not enable GSL, so refinement would be missing" >&2
    echo "   see the GSL section of config.log" >&2
    exit 1
fi

# --- make + install -------------------------------------------------------------
if [ -z "${BANDICOOT_INCREMENTAL:-}" ]; then
    echo "==> make clean (BANDICOOT_INCREMENTAL=1 to skip)"
    make clean >/dev/null
fi
echo "==> make -j${JOBS}"
make -j"${JOBS}"

mkdir -p "${PREFIX}" || { echo "!! cannot create ${PREFIX}" >&2; exit 1; }
echo "==> make install -> ${PREFIX}"
make install

# The launcher (bin/coot) runs libexec/Bandicoot, as on macOS.
if [ -f "${PREFIX}/libexec/coot-bin" ]; then
    mv -f "${PREFIX}/libexec/coot-bin" "${PREFIX}/libexec/Bandicoot"
fi
ln -sf coot "${PREFIX}/bin/bcoot"

# Monomer dictionary and reference structures, checked into the repository
_copy_tree() {   # <src-dir> <dst-dir> <label>
    rm -rf "$2"
    mkdir -p "$2"
    cp -R "$1/." "$2/"
    echo "==> installed $3 -> $2"
}
_copy_tree "${COOT_DATA_SRC}/monomers" "${PREFIX}/share/coot/lib/data/monomers" "monomer dictionary"
_copy_tree "${COOT_DATA_SRC}/reference-structures" "${PREFIX}/share/coot/reference-structures" "reference structures"

echo ""
echo "Bandicoot ${BANDICOOT_VERSION_STR}${BUILD_SUFFIX} installed in ${PREFIX}"
echo "Launch with: ${PREFIX}/bin/bcoot"

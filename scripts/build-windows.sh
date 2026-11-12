#!/bin/bash
# Bandicoot: configure + build + install on Windows (wx interface), in the
# MSYS2 UCRT64 shell. The macOS and Linux builds are scripts/build.sh and
# scripts/build-linux.sh; all three build from the same sources.
#
#     ./scripts/build_deps_windows.sh    # once: source-built dependencies
#     ./scripts/build-windows.sh
#     $PREFIX/bin/bcoot                  # from the UCRT64 shell
#
# Development builds only: the install runs where it is built, from the
# UCRT64 shell, and uses MSYS2's own DLLs from /ucrt64/bin. It is not a
# relocatable distribution.
#
# Customise via environment variables (all optional):
#   PREFIX          install root           (default: $HOME/sw/bandicoot-install)
#   DEPS_PREFIX     source-built deps      (default: /opt/bandicoot-deps)
#   PYTHON          embedded Python        (default: /ucrt64/bin/python3)
#   JOBS            parallel make jobs     (default: CPUs, capped at one per 3 GB RAM)
#   BANDICOOT_INCREMENTAL=1  skip `make clean`
#   BANDICOOT_SKIP_PACKAGE_CHECK=1  skip the MSYS2 package check

set -e

if [ "${MSYSTEM:-}" != "UCRT64" ]; then
    echo "!! build-windows.sh must run in the MSYS2 UCRT64 shell (MSYSTEM=${MSYSTEM:-unset})" >&2
    exit 1
fi
if [ -n "${BANDICOOT_RELEASE:-}${BANDICOOT_NIGHTLY:-}" ]; then
    echo "!! build-windows.sh makes development builds only; there is no Windows release build yet" >&2
    exit 1
fi

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

# Only MSYS2 and the source-built dependencies may supply tools and libraries,
# not other software the Windows PATH points at.
_winsys="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")"
export PATH="/ucrt64/bin:/usr/bin:${_winsys}/System32:${_winsys}"
unset _winsys PKG_CONFIG_PATH

PREFIX="${PREFIX:-$HOME/sw/bandicoot-install}"
DEPS_PREFIX="${DEPS_PREFIX:-/opt/bandicoot-deps}"
PYTHON="${PYTHON:-/ucrt64/bin/python3}"
if [ -z "${JOBS:-}" ]; then
    _cpus="$(nproc 2>/dev/null || echo 2)"
    _mem_gb="$(awk '/MemTotal/ {print int($2 / 1048576)}' /proc/meminfo 2>/dev/null || echo 4)"
    _by_mem=$(( _mem_gb / 3 ))
    [ "${_by_mem}" -ge 1 ] || _by_mem=1
    JOBS=$(( _cpus < _by_mem ? _cpus : _by_mem ))
    unset _cpus _mem_gb _by_mem
fi
COOT_DATA_SRC="${REPO_ROOT}/data/coot-data"

FFTW_PREFIX="${DEPS_PREFIX}/fftw2"
CANVAS_PREFIX="${DEPS_PREFIX}/canvas"
WX_PREFIX="${DEPS_PREFIX}/wx"
GEMMI_PREFIX="${DEPS_PREFIX}/gemmi"
XTAL_PREFIX="${DEPS_PREFIX}/xtal"
RDKIT_PREFIX="${DEPS_PREFIX}/rdkit"
MSYS_PREFIX=/ucrt64

# The program is linked with the Objective-C++ compiler (macOS sources are
# listed in src/Makefile.am), and libtool only recognises a link command whose
# compiler matches one it was configured with, so OBJCXX must be CXX.
# CC/CXX are set explicitly: with --host given, configure would otherwise
# choose the target-prefixed names, which no longer match OBJCXX.
CC="${CC:-gcc}"
CXX="${CXX:-g++}"
OBJCXX="${OBJCXX:-${CXX}}"
export CC CXX OBJCXX

# --- MSYS2 packages ----------------------------------------------------------
# gcc-objc is needed only because configure probes for an Objective-C++
# compiler (no Objective-C++ is compiled on Windows).
P=mingw-w64-ucrt-x86_64
PACMAN_PACKAGES="
make curl perl intltool
${P}-gcc ${P}-gcc-objc ${P}-autotools ${P}-pkgconf
${P}-gtk2 ${P}-libart_lgpl ${P}-cairo ${P}-libpng ${P}-zlib ${P}-curl
${P}-python ${P}-boost ${P}-gsl ${P}-swig ${P}-freeglut ${P}-ntldd
"
if [ -z "${BANDICOOT_SKIP_PACKAGE_CHECK:-}" ]; then
    _missing=""
    for p in ${PACMAN_PACKAGES}; do
        pacman -Q "$p" >/dev/null 2>&1 || _missing="${_missing} $p"
    done
    if [ -n "${_missing}" ]; then
        echo "!! build-windows.sh: missing MSYS2 packages:${_missing}" >&2
        echo "   install them with:" >&2
        echo "     pacman -S --needed${_missing}" >&2
        echo "   (BANDICOOT_SKIP_PACKAGE_CHECK=1 skips this check)" >&2
        exit 1
    fi
fi

_cc="${CC:-gcc}"; _cxx_cmd="${CXX:-g++}"
for _c in "${_cc%% *}" "${_cxx_cmd%% *}"; do
    if ! command -v "${_c}" >/dev/null 2>&1; then
        echo "!! compiler not found: ${_c}" >&2
        exit 1
    fi
done

# --- dependencies ------------------------------------------------------------
_preflight_fail=0
_need() {   # <path> <what>
    if [ ! -e "$1" ]; then
        echo "!! build-windows.sh: $2 not found: $1" >&2
        _preflight_fail=1
    fi
}
_need "${FFTW_PREFIX}/include/fftw.h"              "single-precision FFTW 2"
_need "${CANVAS_PREFIX}/lib/pkgconfig/goocanvas.pc" "goocanvas 1.x"
_need "${CANVAS_PREFIX}/lib/pkgconfig/libgnomecanvas-2.0.pc" "libgnomecanvas"
_need "${WX_PREFIX}/bin/wx-config"                 "wxWidgets (MSW)"
_need "${GEMMI_PREFIX}/include/gemmi/model.hpp"    "gemmi"
_need "${XTAL_PREFIX}/lib/pkgconfig/clipper.pc"    "MMDB2 / libccp4 / SSM / Clipper"
_need "${RDKIT_PREFIX}/include/rdkit/GraphMol/ROMol.h" "RDKit"
_need "${MSYS_PREFIX}/include/boost/python.hpp"    "Boost (with Boost.Python, MSYS2)"
_need "${MSYS_PREFIX}/bin/gsl-config"              "GSL (MSYS2)"
_need "${COOT_DATA_SRC}/monomers/list/mon_lib_list.cif" "monomer dictionary (checked in)"
_need "${PYTHON}"                                  "Python"
if [ "${_preflight_fail}" != "0" ]; then
    echo "   fix: ./scripts/build_deps_windows.sh" >&2
    exit 1
fi

# the -config script must belong to the same interpreter
PY_MM="$("${PYTHON}" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
PYTHON_CONFIG=""
for _pc in "${PYTHON}-config" "$(dirname "${PYTHON}")/python${PY_MM}-config"; do
    if [ -x "${_pc}" ]; then PYTHON_CONFIG="${_pc}"; break; fi
done
if [ -z "${PYTHON_CONFIG}" ]; then
    echo "!! build-windows.sh: no python-config script beside ${PYTHON}" >&2
    exit 1
fi
PYTHON_PREFIX="$("${PYTHON}" -c 'import sys; print(sys.prefix)')"

export PKG_CONFIG_PATH="${XTAL_PREFIX}/lib/pkgconfig:${CANVAS_PREFIX}/lib/pkgconfig"
for mod in gtk+-2.0 clipper mmdb2 ssm; do
    if ! pkg-config --exists "${mod}"; then
        echo "!! build-windows.sh: pkg-config cannot find ${mod}" >&2
        _preflight_fail=1
    fi
done
[ "${_preflight_fail}" = "0" ] || exit 1

# No rpath on Windows: build-time programs (configure tests, SWIG-generated
# steps) find the source-built DLLs through PATH.
DEP_BINDIRS="${FFTW_PREFIX}/bin ${CANVAS_PREFIX}/bin ${XTAL_PREFIX}/bin ${GEMMI_PREFIX}/bin ${WX_PREFIX}/lib ${WX_PREFIX}/bin ${RDKIT_PREFIX}/bin ${RDKIT_PREFIX}/lib"
for _d in ${DEP_BINDIRS}; do PATH="${_d}:${PATH}"; done
export PATH

echo "==> SWIG: $(swig -version | awk '/SWIG Version/{print $3}') ($(command -v swig))"
echo "==> Python: $("${PYTHON}" -c 'import sys; print(sys.version.split()[0])') (${PYTHON}, ${PYTHON_CONFIG})"
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

cat > src/bandicoot-build-id.h <<EOF
// Generated by scripts/build-windows.sh on each invocation. Do not edit; do not
// commit. See scripts/build.sh for the meaning of the suffix.
#ifndef BANDICOOT_BUILD_ID_H
#define BANDICOOT_BUILD_ID_H
#define BANDICOOT_BUILD_SUFFIX "${BUILD_SUFFIX}"
#endif
EOF
touch src/main.cc

# --- configure -----------------------------------------------------------------
PYTHON_EMBED_LDFLAGS="$("${PYTHON_CONFIG}" --ldflags --embed 2>/dev/null || "${PYTHON_CONFIG}" --ldflags)"
PYTHON_EMBED_LIBS="$("${PYTHON_CONFIG}" --libs --embed 2>/dev/null || "${PYTHON_CONFIG}" --libs)"

# As on Linux: the source-built dependencies go on every compile and link,
# because some directories include their headers without asking pkg-config.
export CPPFLAGS="-I${XTAL_PREFIX}/include -I${GEMMI_PREFIX}/include -I${FFTW_PREFIX}/include"
export LDFLAGS="-L${PREFIX}/lib -L${XTAL_PREFIX}/lib -L${GEMMI_PREFIX}/lib -L${FFTW_PREFIX}/lib -L${RDKIT_PREFIX}/lib ${PYTHON_EMBED_LDFLAGS}"
SHIM_INCLUDE="-include ${REPO_ROOT}/compat/python23-shim.hh"
# windows.h reaches many files indirectly (Clipper includes it), so its API
# name macros are neutralised for every file, not per file.
SHIM_INCLUDE="${SHIM_INCLUDE} -include ${REPO_ROOT}/compat/mingw/win32-name-clashes.h"
# big-obj: the SWIG wrapper and other large files exceed the default COFF
# section limit. gnu17: current GCC defaults to C23, which the older C in the
# tree predates.
export CXXFLAGS="-g -O2 -Wall -Wno-unused -std=c++20 -Wa,-mbig-obj -include cstring ${SHIM_INCLUDE}"
export CFLAGS="-g -O2 -Wall -Wno-unused -std=gnu17 ${SHIM_INCLUDE}"

# --build/--host are explicit: configure's MinGW detection (AM_MINGW_WINDOWS)
# reads the build alias, which is empty unless given.
echo "==> ./configure --prefix=${PREFIX} --with-ui=wx (x86_64-w64-mingw32)"
./configure \
    --build=x86_64-w64-mingw32 --host=x86_64-w64-mingw32 \
    --prefix="${PREFIX}" \
    --with-ui=wx \
    WX_CONFIG="${WX_PREFIX}/bin/wx-config" \
    --with-fftw-prefix="${FFTW_PREFIX}" \
    --with-goocanvas-prefix="${CANVAS_PREFIX}" \
    --with-gemmi="${GEMMI_PREFIX}" \
    --with-gsl-prefix="${MSYS_PREFIX}" \
    --with-glut-prefix="${MSYS_PREFIX}" \
    --with-boost="${MSYS_PREFIX}" \
    --with-enhanced-ligand-tools \
    --with-rdkit-prefix="${RDKIT_PREFIX}" \
    --with-python="${PYTHON_PREFIX}" \
    PYTHON="${PYTHON}" \
    PYTHON_CONFIG="${PYTHON_CONFIG}" \
    LIBS="${PYTHON_EMBED_LIBS}" \
    OBJCXX="${OBJCXX}"

if ! grep -q "WINDOWS_MINGW" src/Makefile; then
    echo "!! build-windows.sh: configure did not detect MinGW (no -DWINDOWS_MINGW in src/Makefile)" >&2
    exit 1
fi

_cxx="$(sed -n 's/^CXX = //p' src/Makefile)"
_objcxx="$(sed -n 's/^OBJCXX = //p' src/Makefile)"
if [ "${_cxx}" != "${_objcxx}" ]; then
    echo "!! build-windows.sh: configure recorded OBJCXX='${_objcxx}' but CXX='${_cxx}'" >&2
    echo "   they must be the same compiler; set OBJCXX to the C++ compiler" >&2
    exit 1
fi
echo "==> compilers: CC=$(sed -n 's/^CC = //p' src/Makefile) CXX=${_cxx} OBJCXX=${_objcxx}"

# configure leaves RDKit out, or links it without Boost.Python, without failing
if ! grep -q "^ENHANCED_LIGAND_TOOLS_CPPFLAGS = -DMAKE_ENHANCED_LIGAND_TOOLS" src/Makefile || \
   ! grep -qE "^RDKIT_LIBS = .*-lboost_python[0-9]" src/Makefile; then
    echo "!! build-windows.sh: configure did not enable RDKit with Boost.Python; see config.log" >&2
    exit 1
fi

# configure builds without refinement, quietly, when its GSL test fails
if ! grep -q "^COOT_GSL_FLAGS = -DHAVE_GSL" src/Makefile; then
    echo "!! build-windows.sh: configure did not enable GSL, so refinement would be missing" >&2
    echo "   see the GSL section of config.log" >&2
    exit 1
fi

# --- make + install -------------------------------------------------------------
if [ -z "${BANDICOOT_INCREMENTAL:-}" ]; then
    echo "==> make clean (BANDICOOT_INCREMENTAL=1 to skip)"
    make clean >/dev/null
    # LDFLAGS searches ${PREFIX}/lib, so a previous install's libraries would
    # be linked instead of the ones being built.
    rm -f "${PREFIX}"/lib/libcoot* "${PREFIX}"/lib/libccp4mg* "${PREFIX}"/bin/libcoot* "${PREFIX}"/bin/libccp4mg*
fi
echo "==> make -j${JOBS} (set JOBS to change)"
make -j"${JOBS}"

mkdir -p "${PREFIX}" || { echo "!! cannot create ${PREFIX}" >&2; exit 1; }
# No byte-compiling at install (as on Linux); Python compiles on first import.
echo "==> make install -> ${PREFIX}"
make install py_compile=/dev/null

# The launcher (bin/coot) runs libexec/Bandicoot, as on the other platforms.
if [ -f "${PREFIX}/libexec/coot-bin.exe" ]; then
    mv -f "${PREFIX}/libexec/coot-bin.exe" "${PREFIX}/libexec/Bandicoot.exe"
fi
if [ ! -f "${PREFIX}/libexec/Bandicoot.exe" ]; then
    echo "!! build-windows.sh: ${PREFIX}/libexec/Bandicoot.exe was not installed" >&2
    exit 1
fi
cp -f "${PREFIX}/bin/coot" "${PREFIX}/bin/bcoot"

# Windows looks for a program's DLLs beside it before PATH. Bandicoot's own
# DLLs (installed into bin/ by libtool) and the source-built dependencies'
# go beside the programs in libexec/; MSYS2's come from /ucrt64/bin on PATH.
echo "==> copying DLLs into ${PREFIX}/libexec"
for _d in "${PREFIX}/bin" ${DEP_BINDIRS}; do
    for _dll in "${_d}"/*.dll; do
        [ -e "${_dll}" ] && cp -f "${_dll}" "${PREFIX}/libexec/"
    done
done

# Every DLL Bandicoot loads must resolve: from the install, or from Windows
# or MSYS2 itself.
# On Windows on ARM, ntldd rejects the system's own DLLs (ARM64X hybrids that
# x64 programs load normally) as "not found"; a DLL present in the Windows
# system directory is therefore not counted as missing.
if command -v ntldd >/dev/null 2>&1; then
    _sys32="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32"
    _unresolved=""
    while read -r _dll _rest; do
        [ -n "${_dll}" ] || continue
        [ -e "${_sys32}/${_dll}" ] && continue
        _unresolved="${_unresolved}    ${_dll}"$'\n'
    done < <(cd "${PREFIX}/libexec" && ntldd -R Bandicoot.exe 2>/dev/null | grep -i "not found" || true)
    if [ -n "${_unresolved}" ]; then
        echo "!! build-windows.sh: Bandicoot.exe has DLLs that cannot be found:" >&2
        echo "${_unresolved}" >&2
        exit 1
    fi
fi

# Monomer dictionary and reference structures, checked into the repository
_copy_tree() {   # <src-dir> <dst-dir> <label>
    rm -rf "$2"
    mkdir -p "$2"
    cp -R "$1/." "$2/"
    echo "==> installed $3 -> $2"
}
_copy_tree "${COOT_DATA_SRC}/monomers" "${PREFIX}/share/coot/lib/data/monomers" "monomer dictionary"
_copy_tree "${COOT_DATA_SRC}/reference-structures" "${PREFIX}/share/coot/reference-structures" "reference structures"
_copy_tree "${RDKIT_PREFIX}/share/RDKit/Data" "${PREFIX}/share/RDKit/Data" "RDKit data"

echo ""
echo "Bandicoot ${BANDICOOT_VERSION_STR}${BUILD_SUFFIX} installed in ${PREFIX}"
echo "Launch from the UCRT64 shell with: ${PREFIX}/bin/bcoot"

#!/bin/bash
# Windows counterpart of scripts/build_deps_linux.sh: build the dependencies
# that MSYS2 does not package (or packages in a form Bandicoot cannot use).
# Run once, in the MSYS2 UCRT64 shell, before scripts/build-windows.sh:
#
#     ./scripts/build_deps_windows.sh
#
#   fftw2           single-precision FFTW 2 (MSYS2 has only FFTW 3)
#   libgnomecanvas  GnomeCanvas widget (GTK 2); not packaged
#   goocanvas 1.x   GTK 2 cairo canvas (MSYS2's goocanvas is 3.x, GTK 3)
#   wxWidgets       for MSW, pinned to the version the other platforms use
#   gemmi           mmCIF I/O; needs libgemmi_cpp; not packaged
#   MMDB2, libccp4, SSM, Clipper
#                   the crystallographic libraries, at the versions the
#                   other platforms use; not packaged
#   RDKit           the 2D ligand view, FLEV and pyrogen; not packaged. Its
#                   configure step also downloads coordgen, maeparser and
#                   RingDecomposerLib
#
# Everything else (GTK 2, libart, Python, Boost with Boost.Python, GSL, SWIG,
# CMake, the compilers) comes from MSYS2's UCRT64 packages; the script checks
# for them and prints the pacman line for any that are missing. It never
# installs packages itself.
#
# Tarballs are downloaded from upstream and verified against the checksums
# pinned below, then cached in <prefix>/src so re-runs need no network.
#
# ENVIRONMENT (all optional)
#   DEPS_PREFIX   install prefix     (default: /opt/bandicoot-deps, inside MSYS2)
#   JOBS          parallel make jobs (default: CPUs, capped at one per 3 GB RAM)
#   DEPS_FORCE=1  rebuild even if already installed
#   BANDICOOT_SKIP_PACKAGE_CHECK=1  skip the MSYS2 package check

set -e

if [ "${MSYSTEM:-}" != "UCRT64" ]; then
    echo "!! build_deps_windows.sh must run in the MSYS2 UCRT64 shell (MSYSTEM=${MSYSTEM:-unset})" >&2
    exit 1
fi

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

# Build against MSYS2 and these dependencies only, not other software that
# the Windows PATH may point at.
_winsys="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")"
export PATH="/ucrt64/bin:/usr/bin:${_winsys}/System32:${_winsys}"
unset _winsys PKG_CONFIG_PATH

P=mingw-w64-ucrt-x86_64
# --- MSYS2 packages these builds need ------------------------------------------
PACMAN_PACKAGES="
make patch tar bzip2 xz curl perl intltool
${P}-gcc ${P}-gcc-objc ${P}-autotools ${P}-pkgconf ${P}-cmake ${P}-ninja
${P}-gtk2 ${P}-libart_lgpl ${P}-cairo ${P}-freetype ${P}-libpng ${P}-zlib ${P}-eigen3
${P}-python ${P}-boost ${P}-gsl ${P}-swig ${P}-freeglut ${P}-curl ${P}-ntldd
"
if [ -z "${BANDICOOT_SKIP_PACKAGE_CHECK:-}" ]; then
    _missing=""
    for p in ${PACMAN_PACKAGES}; do
        pacman -Q "$p" >/dev/null 2>&1 || _missing="${_missing} $p"
    done
    if [ -n "${_missing}" ]; then
        echo "!! build_deps_windows.sh: missing MSYS2 packages:${_missing}" >&2
        echo "   install them with:" >&2
        echo "     pacman -S --needed${_missing}" >&2
        echo "   (BANDICOOT_SKIP_PACKAGE_CHECK=1 skips this check)" >&2
        exit 1
    fi
fi

for _c in "${CC:-gcc}" "${CXX:-g++}"; do
    if ! command -v "${_c}" >/dev/null 2>&1; then
        echo "!! compiler not found: ${_c}" >&2
        exit 1
    fi
done

DEPS_PREFIX="${DEPS_PREFIX:-/opt/bandicoot-deps}"
if ! mkdir -p "${DEPS_PREFIX}" 2>/dev/null || [ ! -w "${DEPS_PREFIX}" ]; then
    echo "!! build_deps_windows.sh: ${DEPS_PREFIX} is not writable" >&2
    exit 1
fi
# One job per CPU, but no more than one per 3 GB of RAM: the larger C++ builds
# need several GB per compiler, and running out of memory stalls the machine
# rather than failing the build.
if [ -z "${JOBS:-}" ]; then
    _cpus="$(nproc 2>/dev/null || echo 2)"
    _mem_gb="$(awk '/MemTotal/ {print int($2 / 1048576)}' /proc/meminfo 2>/dev/null || echo 4)"
    _by_mem=$(( _mem_gb / 3 ))
    [ "${_by_mem}" -ge 1 ] || _by_mem=1
    JOBS=$(( _cpus < _by_mem ? _cpus : _by_mem ))
    unset _cpus _mem_gb _by_mem
fi

SRC_CACHE="${DEPS_PREFIX}/src"
WORK="${DEPS_PREFIX}/build"
FFTW_OUT="${DEPS_PREFIX}/fftw2"
CANVAS_OUT="${DEPS_PREFIX}/canvas"
WX_OUT="${DEPS_PREFIX}/wx"
GEMMI_OUT="${DEPS_PREFIX}/gemmi"
XTAL_OUT="${DEPS_PREFIX}/xtal"
RDKIT_OUT="${DEPS_PREFIX}/rdkit"
PCDIR="${CANVAS_OUT}/lib/pkgconfig"

mkdir -p "${SRC_CACHE}" "${WORK}" "${PCDIR}"

# Windows has no rpath: programs find DLLs beside themselves or on PATH. The
# configure tests of later dependencies run programs linked to earlier ones.
export PATH="${FFTW_OUT}/bin:${CANVAS_OUT}/bin:${XTAL_OUT}/bin:${GEMMI_OUT}/bin:${WX_OUT}/lib:${WX_OUT}/bin:${RDKIT_OUT}/bin:${RDKIT_OUT}/lib:${PATH}"

# Pinned upstream releases: name|version|url|sha256 (same pins as Linux)
DEPS_LIST="
fftw|2.1.5|https://www.fftw.org/fftw-2.1.5.tar.gz|f8057fae1c7df8b99116783ef3e94a6a44518d49c72e2e630c24b689c6022630
libgnomecanvas|2.30.3|https://download.gnome.org/sources/libgnomecanvas/2.30/libgnomecanvas-2.30.3.tar.bz2|859b78e08489fce4d5c15c676fec1cd79782f115f516e8ad8bed6abcb8dedd40
goocanvas|1.0.0|https://download.gnome.org/sources/goocanvas/1.0/goocanvas-1.0.0.tar.bz2|1c072ef88567cad241fb4addee26e9bd96741b1503ff736d1c152fa6d865711e
wxWidgets|3.3.3|https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.3/wxWidgets-3.3.3.tar.bz2|81b09d6dd9f1ed9301f8c55a968a488d0491f264dc2bab19a7e407ac67009482
gemmi|0.7.5|https://github.com/project-gemmi/gemmi/archive/refs/tags/v0.7.5.tar.gz|9e2a8a51e62c69bf43f62aadf527ca4312860de8a36c12a8747d3e8ae556f0b3
mmdb2|2.0.22|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/mmdb2-2.0.22.tar.gz|a9646933ce9f34633e1ae4901f2383af0e5318d6490851328f5b6aa06195ab51
libccp4|8.0.0|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/libccp4-8.0.0.tar.gz|cb813ae86612a0866329deab7cee96eac573d81be5b240341d40f9ad5322ff2d
ssm|1.4|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/ssm-1.4.tar.gz|56e7e64ed86d7d9ec59500fd34f26f881bdb9d541916d9a817c3bfb8cf0e9508
clipper|2.1.20180802|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/clipper-2.1.20180802.tar.gz|7c7774f224b59458e0faa104d209da906c129523fa737e81eb3b99ec772b81e0
rdkit|2026.03.6|https://github.com/rdkit/rdkit/archive/refs/tags/Release_2026_03_6.tar.gz|d4d20b3b140237084694518aab34fdba6929d44bd7f720bce69329516abef663
"

echo "==> deps prefix: ${DEPS_PREFIX}"
echo "==> compilers: $(command -v "${CC:-gcc}") / $(command -v "${CXX:-g++}"), JOBS=${JOBS}"

# --- build environment -----------------------------------------------------
export PKG_CONFIG_PATH="${PCDIR}"

# FFTW 2 is from 1999, the canvas packages 2005-2010. Current GCC defaults to
# C23 and turns implicit declarations and some pointer conversions into
# errors, which breaks them; gnu17 and the downgrades change no generated code.
export CFLAGS="${CFLAGS:-} -O2 -std=gnu17 -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-deprecated-declarations"

# --- fetch + build ---------------------------------------------------------
dep_field() { echo "${DEPS_LIST}" | grep "^$1|" | cut -d'|' -f"$2"; }

fetch() {   # <name> <url> <sha256> -> echoes the cached path
    local name="$1" url="$2" want="$3"
    local dest="${SRC_CACHE}/${name}-$(basename "${url}")"

    if [ ! -f "${dest}" ]; then
        echo "    downloading $(basename "${url}")" >&2
        curl -fSL --connect-timeout 30 -o "${dest}.part" "${url}" >&2 || {
            echo "!! download failed: ${url}" >&2
            echo "   If upstream has gone away, fetch it by hand into ${dest}" >&2
            exit 1
        }
        mv "${dest}.part" "${dest}"
    fi

    local got
    got="$(sha256sum "${dest}" | cut -d' ' -f1)"
    if [ "${got}" != "${want}" ]; then
        echo "!! checksum mismatch for ${dest}" >&2
        echo "   expected ${want}" >&2
        echo "   got      ${got}" >&2
        echo "   Delete ${dest} and re-run to re-download." >&2
        exit 1
    fi
    echo "${dest}"
}

# Replace each tarball's bundled config.guess / config.sub with MSYS2's
# current pair: the old copies do not know the mingw-w64 targets.
AUX_SRC=""
for _d in /usr/share/automake-*/ /usr/share/libtool/build-aux/ /ucrt64/share/automake-*/ /ucrt64/share/libtool/build-aux/; do
    if [ -f "${_d}config.sub" ] && [ -f "${_d}config.guess" ]; then
        AUX_SRC="${_d}"
        break
    fi
done

refresh_autotools_aux() {   # <srcdir>
    local d="$1" f found=0
    [ -n "${AUX_SRC}" ] || return 0
    for f in config.sub config.guess; do
        while IFS= read -r target; do
            cp -f "${AUX_SRC}${f}" "${target}"
            chmod +x "${target}"
            found=1
        done < <(find "${d}" -name "${f}" -type f 2>/dev/null)
    done
    [ "${found}" = "1" ] && echo "    refreshed config.sub/config.guess from ${AUX_SRC}"
    return 0
}

# Unpack <name> into ${WORK}; echoes the source directory.
unpack() {   # <name>
    local name="$1" ver url sha tarball top
    ver="$(dep_field "${name}" 2)"
    url="$(dep_field "${name}" 3)"
    sha="$(dep_field "${name}" 4)"
    [ -n "${ver}" ] || { echo "!! no pin for ${name}" >&2; exit 1; }
    tarball="$(fetch "${name}" "${url}" "${sha}")"
    top="$(tar -tf "${tarball}" | head -1 | cut -d/ -f1)"
    rm -rf "${WORK:?}/${top}"
    tar -xf "${tarball}" -C "${WORK}"
    [ -d "${WORK}/${top}" ] || { echo "!! extracted tree ${WORK}/${top} not found" >&2; exit 1; }
    echo "${WORK}/${top}"
}

skip_if_done() {   # <name> <sentinel>
    if [ "${DEPS_FORCE:-0}" != "1" ] && [ -e "$2" ]; then
        echo "==> $1-$(dep_field "$1" 2): already installed, skipping"
        return 0
    fi
    return 1
}

# libtool builds a DLL only when told the library has no undefined symbols;
# otherwise it quietly builds a static library alone. It is passed at make
# time because configure's own test links use the compiler directly, which
# does not accept the flag.
NOUNDEF="-no-undefined"

build_autotools() {   # <name> <sentinel> <prefix> [configure args...]
    local name="$1" sentinel="$2" prefix="$3"; shift 3
    skip_if_done "${name}" "${sentinel}" && return 0
    echo "==> ${name}-$(dep_field "${name}" 2)"
    local srcdir
    srcdir="$(unpack "${name}")"
    refresh_autotools_aux "${srcdir}"
    # an LDFLAGS on the make command line replaces configure's, so it is
    # given only when there is something to add
    local mk=()
    [ -n "${NOUNDEF}" ] && mk=(LDFLAGS="${LDFLAGS:-} ${NOUNDEF}")
    ( cd "${srcdir}" && ./configure --prefix="${prefix}" "$@" && \
      make -j"${JOBS}" "${mk[@]}" && make install )
    [ -e "${sentinel}" ] || { echo "!! ${name} built but ${sentinel} missing" >&2; exit 1; }
    echo "    installed ${name}"
}

build_autotools fftw "${FFTW_OUT}/include/fftw.h" "${FFTW_OUT}" \
    --enable-float --enable-shared --disable-fortran
# FFTW 2 builds can be installed with prefixed names (libsfftw, sfftw.h) or
# plain ones; the functions inside are the same. Provide both names (Clipper
# prefers the prefixed ones). MSYS2 symlinks are copies, so these are plain
# copies, refreshed on every run.
for _n in fftw rfftw; do
    for _f in "${FFTW_OUT}/lib/lib${_n}".a "${FFTW_OUT}/lib/lib${_n}".dll.a "${FFTW_OUT}/lib/lib${_n}".la; do
        [ -e "${_f}" ] || continue
        cp -f "${_f}" "${FFTW_OUT}/lib/libs${_f#${FFTW_OUT}/lib/lib}"
    done
    cp -f "${FFTW_OUT}/include/${_n}.h" "${FFTW_OUT}/include/s${_n}.h"
done

build_autotools libgnomecanvas "${PCDIR}/libgnomecanvas-2.0.pc" "${CANVAS_OUT}" \
    --disable-static --disable-gtk-doc --disable-glibtest
build_autotools goocanvas "${PCDIR}/goocanvas.pc" "${CANVAS_OUT}" \
    --disable-static --disable-gtk-doc --disable-python

# --- crystallographic libraries (the bioconda recipes' configure options) ----
XTAL_PC="${XTAL_OUT}/lib/pkgconfig"
export PKG_CONFIG_PATH="${XTAL_PC}:${PKG_CONFIG_PATH}"

# autoreconf'd source, then configure + make + install into XTAL_OUT.
# <pre-configure> is run in the source directory first (source fixes).
build_xtal() {   # <name> <sentinel> <subdir> <pre-configure> [configure args...]
    local name="$1" sentinel="$2" sub="$3" pre="$4"; shift 4
    skip_if_done "${name}" "${sentinel}" && return 0
    echo "==> ${name}-$(dep_field "${name}" 2)"
    local srcdir
    srcdir="$(unpack "${name}")/${sub}"
    local ldf="-L${XTAL_OUT}/lib -L${FFTW_OUT}/lib"
    ( cd "${srcdir}"
      autoreconf -if
      refresh_autotools_aux "${srcdir}"
      eval "${pre}"
      CPPFLAGS="-I${XTAL_OUT}/include -I${FFTW_OUT}/include" \
      LDFLAGS="${ldf}" \
      CFLAGS="-O2" CXXFLAGS="-O2 -fno-strict-aliasing -Wno-narrowing" \
        ./configure --prefix="${XTAL_OUT}" --enable-shared --disable-static \
                    --disable-dependency-tracking "$@"
      make -j"${JOBS}" LDFLAGS="${ldf} ${NOUNDEF}"
      make install )
    [ -e "${sentinel}" ] || { echo "!! ${name} built but ${sentinel} missing" >&2; exit 1; }
    echo "    installed ${name}"
}

build_xtal mmdb2 "${XTAL_PC}/mmdb2.pc" "" ":"
build_xtal libccp4 "${XTAL_PC}/ccp4c.pc" "" \
    "sed -i 's/int putenv ();/extern int putenv (char *);/' ccp4/library_utils.c" \
    --disable-fortran
# superpose.cpp writes "..."superpose_version"...", which C++11 reads as a
# user-defined literal; a space either side of the macro names fixes it.
build_xtal ssm "${XTAL_PC}/ssm.pc" "" \
    "sed -i -E 's/\"(superpose_version|superpose_date)\"/\" \\1 \"/g' superpose.cpp" \
    --enable-ccp4
build_xtal clipper "${XTAL_PC}/clipper.pc" "" \
    "sed -i 's|libccp4c|ccp4c|g' configure" \
    --enable-mmdb --enable-ccp4 --enable-cif --enable-minimol --enable-cns

# wxWidgets for MSW, with the parts Bandicoot uses (OpenGL canvas, AUI, SVG
# icons). wx links with its own rules, not libtool, so no NOUNDEF here, and
# the C flags must not carry the old-C settings above.
CFLAGS_SAVED="${CFLAGS}"
export CFLAGS="-O2"
NOUNDEF_SAVED="${NOUNDEF}"; NOUNDEF=""
build_autotools wxWidgets "${WX_OUT}/bin/wx-config" "${WX_OUT}" \
    --with-msw --with-opengl --enable-aui --enable-svg \
    --disable-webview --disable-mediactrl --disable-tests --disable-precomp-headers
NOUNDEF="${NOUNDEF_SAVED}"
export CFLAGS="${CFLAGS_SAVED}"

# gemmi (CMake): headers plus the shared libgemmi_cpp
if ! skip_if_done gemmi "${GEMMI_OUT}/include/gemmi/model.hpp"; then
    echo "==> gemmi-$(dep_field gemmi 2)"
    srcdir="$(unpack gemmi)"
    cmake -G Ninja -S "${srcdir}" -B "${srcdir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${GEMMI_OUT}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=ON \
        -DUSE_PYTHON=OFF
    cmake --build "${srcdir}/build" -j "${JOBS}"
    cmake --install "${srcdir}/build"
    echo "    installed gemmi"
fi

# RDKit: C++ libraries only, with Cairo drawing, as on the other platforms.
# Boost (with Boost.Python for MSYS2's Python) comes from MSYS2. Large
# translation units exceed the default COFF section limit, hence big-obj.
RDKIT_MARKER="${RDKIT_OUT}/.bandicoot-rdkit-$(dep_field rdkit 2)"
if ! skip_if_done rdkit "${RDKIT_MARKER}"; then
    echo "==> rdkit-$(dep_field rdkit 2)"
    srcdir="$(unpack rdkit)"
    cmake -G Ninja -S "${srcdir}" -B "${srcdir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${RDKIT_OUT}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_CXX_FLAGS="-Wa,-mbig-obj" \
        -DRDK_INSTALL_INTREE=OFF \
        -DRDK_INSTALL_STATIC_LIBS=OFF \
        -DRDK_BUILD_PYTHON_WRAPPERS=OFF \
        -DRDK_BUILD_CPP_TESTS=OFF \
        -DRDK_BUILD_CAIRO_SUPPORT=ON \
        -DRDK_BUILD_FREETYPE_SUPPORT=ON \
        -DRDK_BUILD_COORDGEN_SUPPORT=ON \
        -DRDK_BUILD_MAEPARSER_SUPPORT=ON \
        -DRDK_BUILD_INCHI_SUPPORT=OFF \
        -DRDK_BUILD_PGSQL=OFF
    cmake --build "${srcdir}/build" -j "${JOBS}"
    # a fresh directory, so nothing from another version is left beside it
    rm -rf "${RDKIT_OUT}"
    cmake --install "${srcdir}/build"
    touch "${RDKIT_MARKER}"
    echo "    installed rdkit"
fi

# --- verify ----------------------------------------------------------------
echo "==> verifying"
_fail=0
for mod in libart-2.0 libgnomecanvas-2.0 goocanvas gtk+-2.0; do
    if pkg-config --exists "${mod}"; then
        printf '    %-22s %s\n' "${mod}" "$(pkg-config --modversion "${mod}")"
    else
        echo "    ${mod}: NOT FOUND" >&2; _fail=1
    fi
done
if [ -f "${FFTW_OUT}/include/fftw.h" ]; then
    printf '    %-22s %s\n' "fftw2 (single-prec)" "$(dep_field fftw 2)"
else
    echo "    fftw2: NOT FOUND" >&2; _fail=1
fi
for mod in mmdb2 ccp4c ssm clipper; do
    if pkg-config --exists "${mod}"; then
        printf '    %-22s %s\n' "${mod}" "$(pkg-config --modversion "${mod}")"
    else
        echo "    ${mod}: NOT FOUND" >&2; _fail=1
    fi
done
if /ucrt64/bin/python3 -c 'import ssl, ctypes, sqlite3, lzma, bz2' 2>/dev/null; then
    printf '    %-22s %s\n' "Python (MSYS2)" "$(/ucrt64/bin/python3 -c 'import sys; print(sys.version.split()[0])')"
else
    echo "    Python: NOT FOUND or missing ssl/ctypes/sqlite3/lzma/bz2" >&2; _fail=1
fi
if ls /ucrt64/lib/libboost_python3*.dll.a >/dev/null 2>&1; then
    printf '    %-22s %s\n' "Boost.Python (MSYS2)" "$(basename "$(ls /ucrt64/lib/libboost_python3*.dll.a | head -1)" .dll.a | sed 's/^lib//')"
else
    echo "    Boost.Python (/ucrt64/lib/libboost_python3*.dll.a): NOT FOUND" >&2; _fail=1
fi
if ls "${RDKIT_OUT}"/lib/libRDKitMolDraw2D*.dll.a >/dev/null 2>&1 && [ -d "${RDKIT_OUT}/share/RDKit/Data" ]; then
    printf '    %-22s %s\n' "RDKit" "$(dep_field rdkit 2)"
else
    echo "    RDKit (libRDKitMolDraw2D, share/RDKit/Data): NOT FOUND" >&2; _fail=1
fi
if [ -x "${WX_OUT}/bin/wx-config" ]; then
    printf '    %-22s %s (%s)\n' "wxWidgets" "$("${WX_OUT}/bin/wx-config" --version)" \
        "$("${WX_OUT}/bin/wx-config" --selected-config)"
else
    echo "    wxWidgets: NOT FOUND" >&2; _fail=1
fi
if ls "${GEMMI_OUT}"/lib/libgemmi_cpp.* >/dev/null 2>&1; then
    printf '    %-22s %s\n' "gemmi" "$(dep_field gemmi 2)"
else
    echo "    gemmi (libgemmi_cpp): NOT FOUND" >&2; _fail=1
fi
# Static-only builds still link, but a library linked statically into several
# of Bandicoot's DLLs gets one copy of its global state in each.
echo "    DLLs built:"
for _lib in fftw rfftw gnomecanvas goocanvas mmdb2 ccp4c ssm clipper-core gemmi_cpp; do
    if ls "${DEPS_PREFIX}"/*/bin/lib${_lib}*.dll >/dev/null 2>&1; then
        printf '      %-20s yes\n' "${_lib}"
    else
        printf '      %-20s NO (static only)\n' "${_lib}"
    fi
done
[ "${_fail}" = "0" ] || { echo "!! dependency build incomplete." >&2; exit 1; }

echo ""
echo "Dependencies installed under ${DEPS_PREFIX}"
echo "Now run: ./scripts/build-windows.sh"

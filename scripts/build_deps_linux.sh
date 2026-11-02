#!/bin/bash
# Linux counterpart of scripts/build_deps.sh: download and build the
# dependencies that current Linux distributions do not package (or package in
# a form Bandicoot cannot use). Run once after a fresh checkout, before
# scripts/build-linux.sh:
#
#     ./scripts/build_deps_linux.sh
#
#   fftw2           single-precision FFTW 2 (FFTW 3 is not a substitute)
#   libart_lgpl     libgnomecanvas's only hard dependency
#   libgnomecanvas  GnomeCanvas widget (GTK 2)
#   goocanvas 1.x   GTK 2 cairo canvas (2.x is GTK 3 only)
#   wxWidgets       built against GTK 2: the core still uses GTK 2, and GTK 2
#                   and GTK 3 cannot be loaded into one process, so the
#                   distribution's wxGTK (GTK 3) cannot be used
#   gemmi           mmCIF I/O; needs libgemmi_cpp
#   MMDB2, libccp4, SSM, Clipper
#                   the crystallographic libraries, built together at the
#                   versions the macOS build uses: older distributions (e.g.
#                   Ubuntu 20.04's MMDB2 2.0.5) lack functions Bandicoot calls,
#                   and Clipper and SSM must link the same MMDB2
#   Python 3.13     the embedded interpreter, at the version the macOS build
#                   uses; Bandicoot's Python layer has only run on 3.12+
#   GSL             refinement's minimiser; built here because a distribution's
#                   libgsl-dev can be unusable when other GSL runtime versions
#                   are already installed
#   CMake           RDKit needs a newer one than some distribution releases
#                   ship (e.g. Ubuntu 20.04's 3.16); the official binary
#   Boost           with Boost.Python built for the Python above; RDKit and
#                   Bandicoot's ligand tools link it
#   RDKit           the 2D ligand view, FLEV and pyrogen; its configure step
#                   also downloads coordgen, maeparser and RingDecomposerLib
#   patchelf        sets the library search paths of a relocatable release
#                   (scripts/release-linux.sh); distribution releases can
#                   be too old (e.g. Ubuntu 20.04's 0.10)
#   SWIG            generates the Python wrapper; distribution releases that
#                   predate Python 3.12 support (e.g. Ubuntu 20.04's 4.0.1)
#                   generate code for an older Python C API
#
# Tarballs are downloaded from upstream and verified against the checksums
# pinned below, then cached in <prefix>/src so re-runs need no network.
# The system packages these builds need are listed by scripts/build-linux.sh.
#
# ENVIRONMENT (all optional)
#   DEPS_PREFIX   install prefix     (default: /opt/bandicoot-deps)
#   JOBS          parallel make jobs (default: CPU count)
#   DEPS_FORCE=1  rebuild even if already installed

set -e

if [ "$(uname)" != "Linux" ]; then
    echo "!! build_deps_linux.sh is for Linux; on macOS use scripts/build_deps.sh" >&2
    exit 1
fi

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

# Build against the system and these dependencies only, not a site software
# tree that PATH / LD_LIBRARY_PATH / PKG_CONFIG_PATH may point at.
export PATH="/usr/sbin:/usr/bin:/sbin:/bin"
unset LD_LIBRARY_PATH PKG_CONFIG_PATH

# The compilers named by CC / CXX (or the defaults) must exist; configure only
# reports a missing one as "C compiler cannot create executables".
for _c in "${CC:-gcc}" "${CXX:-g++}"; do
    if ! command -v "${_c}" >/dev/null 2>&1; then
        echo "!! compiler not found: ${_c}" >&2
        echo "   install it (e.g. sudo apt-get install gcc-10 g++-10), or set CC / CXX" >&2
        exit 1
    fi
done

# --- system packages these builds need (Debian / Ubuntu) ---------------------
APT_PACKAGES="
build-essential autoconf automake libtool pkg-config intltool libxml-parser-perl cmake curl
libgtk2.0-dev libgail-dev libcairo2-dev libglu1-mesa-dev libeigen3-dev libfreetype6-dev
libpng-dev libjpeg-dev libtiff-dev zlib1g-dev libexpat1-dev
libssl-dev libffi-dev libbz2-dev libreadline-dev libsqlite3-dev liblzma-dev libncursesw5-dev uuid-dev
"
if [ -z "${BANDICOOT_SKIP_PACKAGE_CHECK:-}" ] && command -v dpkg-query >/dev/null 2>&1; then
    _missing=""
    for p in ${APT_PACKAGES}; do
        if ! dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed"; then
            _missing="${_missing} $p"
        fi
    done
    if [ -n "${_missing}" ]; then
        echo "!! build_deps_linux.sh: missing system packages:${_missing}" >&2
        echo "   install them with:" >&2
        echo "     sudo apt-get install${_missing}" >&2
        echo "   (BANDICOOT_SKIP_PACKAGE_CHECK=1 skips this check)" >&2
        exit 1
    fi
fi

# A generic location, not under a home directory: some of these libraries
# (libpython among them) record their install path in themselves, and a
# release ships them.
DEPS_PREFIX="${DEPS_PREFIX:-/opt/bandicoot-deps}"
if ! mkdir -p "${DEPS_PREFIX}" 2>/dev/null || [ ! -w "${DEPS_PREFIX}" ]; then
    echo "!! build_deps_linux.sh: ${DEPS_PREFIX} is not writable by $(id -un)" >&2
    echo "   create it once:" >&2
    echo "     sudo mkdir -p ${DEPS_PREFIX}" >&2
    echo "     sudo chown $(id -un): ${DEPS_PREFIX}" >&2
    exit 1
fi
# One job per CPU, but no more than one per 3 GB of RAM: the larger C++ builds
# (RDKit, Boost) need several GB per compiler, and running out of memory
# freezes the machine rather than failing the build.
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
GSL_OUT="${DEPS_PREFIX}/gsl"
XTAL_OUT="${DEPS_PREFIX}/xtal"
PYTHON_OUT="${DEPS_PREFIX}/python"
SWIG_OUT="${DEPS_PREFIX}/swig"
PATCHELF_OUT="${DEPS_PREFIX}/patchelf"
CMAKE_OUT="${DEPS_PREFIX}/cmake"
BOOST_OUT="${DEPS_PREFIX}/boost"
RDKIT_OUT="${DEPS_PREFIX}/rdkit"
PCDIR="${CANVAS_OUT}/lib/pkgconfig"

mkdir -p "${SRC_CACHE}" "${WORK}" "${PCDIR}"

# Pinned upstream releases: name|version|url|sha256
DEPS_LIST="
fftw|2.1.5|https://www.fftw.org/fftw-2.1.5.tar.gz|f8057fae1c7df8b99116783ef3e94a6a44518d49c72e2e630c24b689c6022630
libart_lgpl|2.3.21|https://download.gnome.org/sources/libart_lgpl/2.3/libart_lgpl-2.3.21.tar.bz2|fdc11e74c10fc9ffe4188537e2b370c0abacca7d89021d4d303afdf7fd7476fa
libgnomecanvas|2.30.3|https://download.gnome.org/sources/libgnomecanvas/2.30/libgnomecanvas-2.30.3.tar.bz2|859b78e08489fce4d5c15c676fec1cd79782f115f516e8ad8bed6abcb8dedd40
goocanvas|1.0.0|https://download.gnome.org/sources/goocanvas/1.0/goocanvas-1.0.0.tar.bz2|1c072ef88567cad241fb4addee26e9bd96741b1503ff736d1c152fa6d865711e
wxWidgets|3.3.3|https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.3/wxWidgets-3.3.3.tar.bz2|81b09d6dd9f1ed9301f8c55a968a488d0491f264dc2bab19a7e407ac67009482
gemmi|0.7.5|https://github.com/project-gemmi/gemmi/archive/refs/tags/v0.7.5.tar.gz|9e2a8a51e62c69bf43f62aadf527ca4312860de8a36c12a8747d3e8ae556f0b3
mmdb2|2.0.22|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/mmdb2-2.0.22.tar.gz|a9646933ce9f34633e1ae4901f2383af0e5318d6490851328f5b6aa06195ab51
libccp4|8.0.0|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/libccp4-8.0.0.tar.gz|cb813ae86612a0866329deab7cee96eac573d81be5b240341d40f9ad5322ff2d
ssm|1.4|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/ssm-1.4.tar.gz|56e7e64ed86d7d9ec59500fd34f26f881bdb9d541916d9a817c3bfb8cf0e9508
clipper|2.1.20180802|https://www2.mrc-lmb.cam.ac.uk/personal/pemsley/coot/dependencies/clipper-2.1.20180802.tar.gz|7c7774f224b59458e0faa104d209da906c129523fa737e81eb3b99ec772b81e0
python|3.13.12|https://www.python.org/ftp/python/3.13.12/Python-3.13.12.tar.xz|2a84cd31dd8d8ea8aaff75de66fc1b4b0127dd5799aa50a64ae9a313885b4593
gsl|2.8|https://ftpmirror.gnu.org/gnu/gsl/gsl-2.8.tar.gz|6a99eeed15632c6354895b1dd542ed5a855c0f15d9ad1326c6fe2b2c9e423190
patchelf|0.19.3|https://github.com/NixOS/patchelf/releases/download/0.19.3/patchelf-0.19.3.tar.bz2|20dbe23e9dbc4ad769078a867ad002c04297a069ff93c95fb72242dd55d4dd6f
cmake|3.31.6|https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-linux-x86_64.tar.gz|5a1133ff103c71eb5120e2cc3de922733e7d8a26a98ae716397e8676adb367bf
boost|1.87.0|https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.bz2|af57be25cb4c4f4b413ed692fe378affb4352ea50fbe294a11ef548f4d527d89
rdkit|2026.09.1|https://github.com/rdkit/rdkit/archive/Release_2026_09_1.tar.gz|86711e340759a3a0678a2e62f54f26f7ff564c3a02de0460f60b1ce89ac1bfe9
swig|4.4.1|https://downloads.sourceforge.net/project/swig/swig/swig-4.4.1/swig-4.4.1.tar.gz|40162a706c56f7592d08fd52ef5511cb7ac191f3593cf07306a0a554c6281fcf
"

echo "==> deps prefix: ${DEPS_PREFIX}"

# --- build environment -----------------------------------------------------
export PKG_CONFIG_PATH="${PCDIR}${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"

# FFTW 2 is from 1999, the canvas packages 2005-2010. Newer compilers turn
# implicit function declarations and some pointer conversions into errors,
# which breaks them; downgrading changes no generated code.
export CFLAGS="${CFLAGS:-} -O2 -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-deprecated-declarations"

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

# Replace each tarball's bundled config.guess / config.sub with the system's
# current pair: FFTW 2.1.5's 1999 copies cannot name x86_64 or aarch64.
AUX_SRC=""
for _d in /usr/share/misc/ /usr/share/automake-*/ /usr/share/libtool/build-aux/; do
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

build_autotools() {   # <name> <sentinel> <prefix> [configure args...]
    local name="$1" sentinel="$2" prefix="$3"; shift 3
    skip_if_done "${name}" "${sentinel}" && return 0
    echo "==> ${name}-$(dep_field "${name}" 2)"
    local srcdir
    srcdir="$(unpack "${name}")"
    refresh_autotools_aux "${srcdir}"
    ( cd "${srcdir}" && ./configure --prefix="${prefix}" "$@" && make -j"${JOBS}" && make install )
    [ -e "${sentinel}" ] || { echo "!! ${name} built but ${sentinel} missing" >&2; exit 1; }
    echo "    installed ${name}"
}

build_autotools fftw "${FFTW_OUT}/include/fftw.h" "${FFTW_OUT}" \
    --enable-float --enable-shared --disable-fortran
# FFTW 2 builds can be installed with prefixed names (libsfftw, sfftw.h) or
# plain ones; the functions inside are the same. Provide both names for ours,
# so nothing links a distribution's copy instead (Clipper prefers the prefixed
# names). Runs every time, so an existing FFTW gets the aliases too.
for _n in fftw rfftw; do
    ln -sf "lib${_n}.so"   "${FFTW_OUT}/lib/libs${_n}.so"
    ln -sf "lib${_n}.so.2" "${FFTW_OUT}/lib/libs${_n}.so.2"
    ln -sf "${_n}.h"       "${FFTW_OUT}/include/s${_n}.h"
done

build_autotools libart_lgpl "${PCDIR}/libart-2.0.pc" "${CANVAS_OUT}" \
    --disable-static
build_autotools libgnomecanvas "${PCDIR}/libgnomecanvas-2.0.pc" "${CANVAS_OUT}" \
    --disable-static --disable-gtk-doc --disable-glibtest
build_autotools goocanvas "${PCDIR}/goocanvas.pc" "${CANVAS_OUT}" \
    --disable-static --disable-gtk-doc --disable-python
build_autotools gsl "${GSL_OUT}/bin/gsl-config" "${GSL_OUT}" \
    --disable-static
# PCRE is only for SWIG's %rename regex matching, which coot.i does not use
build_autotools swig "${SWIG_OUT}/bin/swig" "${SWIG_OUT}" \
    --without-pcre
build_autotools patchelf "${PATCHELF_OUT}/bin/patchelf" "${PATCHELF_OUT}"

# Python, shared (Bandicoot embeds libpython), with pip; the rpath lets the
# installed interpreter and library find each other without LD_LIBRARY_PATH
build_autotools python "${PYTHON_OUT}/bin/python3.13" "${PYTHON_OUT}" \
    --enable-shared --with-ensurepip=install \
    LDFLAGS="-Wl,-rpath,${PYTHON_OUT}/lib"

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
    ( cd "${srcdir}"
      autoreconf -if
      refresh_autotools_aux "${srcdir}"
      eval "${pre}"
      CPPFLAGS="-I${XTAL_OUT}/include -I${FFTW_OUT}/include" \
      LDFLAGS="-L${XTAL_OUT}/lib -L${FFTW_OUT}/lib -Wl,-rpath,${XTAL_OUT}/lib -Wl,-rpath,${FFTW_OUT}/lib" \
      CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fno-strict-aliasing -Wno-narrowing" \
        ./configure --prefix="${XTAL_OUT}" --enable-shared --disable-static \
                    --disable-dependency-tracking "$@"
      make -j"${JOBS}"
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

# wxWidgets on GTK 2, with the parts Bandicoot uses (OpenGL canvas, AUI, SVG
# icons). The C++ flags must not carry the old-C warning downgrades above.
CFLAGS_SAVED="${CFLAGS}"
CPPFLAGS_SAVED="${CPPFLAGS:-}"
export CFLAGS="-O2"
# Older GLib (before the gio-unix headers moved into the main include
# directory) keeps gio/gunixfdlist.h and friends under gio-unix-2.0, which wx
# includes without asking pkg-config for.
export CPPFLAGS="${CPPFLAGS_SAVED} $(pkg-config --cflags gio-unix-2.0 2>/dev/null)"
build_autotools wxWidgets "${WX_OUT}/bin/wx-config" "${WX_OUT}" \
    --with-gtk=2 --with-opengl --enable-aui --enable-svg \
    --disable-webview --disable-mediactrl --disable-tests --disable-precomp-headers
export CFLAGS="${CFLAGS_SAVED}"
export CPPFLAGS="${CPPFLAGS_SAVED}"

# gemmi (CMake): headers plus the shared libgemmi_cpp
if ! skip_if_done gemmi "${GEMMI_OUT}/include/gemmi/model.hpp"; then
    echo "==> gemmi-$(dep_field gemmi 2)"
    srcdir="$(unpack gemmi)"
    cmake -S "${srcdir}" -B "${srcdir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${GEMMI_OUT}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=ON \
        -DUSE_PYTHON=OFF
    cmake --build "${srcdir}/build" -j "${JOBS}"
    cmake --install "${srcdir}/build"
    echo "    installed gemmi"
fi

# CMake: the release binary, unpacked
if ! skip_if_done cmake "${CMAKE_OUT}/bin/cmake"; then
    echo "==> cmake-$(dep_field cmake 2)"
    _tarball="$(fetch cmake "$(dep_field cmake 3)" "$(dep_field cmake 4)")"
    rm -rf "${CMAKE_OUT}"
    mkdir -p "${CMAKE_OUT}"
    tar -xf "${_tarball}" -C "${CMAKE_OUT}" --strip-components=1
    echo "    installed cmake"
fi

# Boost, with Boost.Python for the Python built above
PY_MM="$("${PYTHON_OUT}/bin/python3.13" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
if ! skip_if_done boost "${BOOST_OUT}/lib/libboost_python${PY_MM/./}.so"; then
    echo "==> boost-$(dep_field boost 2)"
    srcdir="$(unpack boost)"
    ( cd "${srcdir}" && \
      ./bootstrap.sh --prefix="${BOOST_OUT}" --with-toolset=gcc \
          --with-libraries=python,serialization,iostreams,system \
          --with-python="${PYTHON_OUT}/bin/python${PY_MM}" --with-python-version="${PY_MM}" \
          --with-python-root="${PYTHON_OUT}" && \
      sed -i "s|^ *using gcc *;|using gcc : : ${CXX:-g++} ;|" project-config.jam && \
      ./b2 -j"${JOBS}" variant=release link=shared threading=multi runtime-link=shared \
          "linkflags=-Wl,-rpath,${BOOST_OUT}/lib" install )
    [ -e "${BOOST_OUT}/lib/libboost_python${PY_MM/./}.so" ] || \
        { echo "!! boost built but libboost_python${PY_MM/./}.so missing" >&2; exit 1; }
    echo "    installed boost"
fi

# RDKit: C++ libraries only (Bandicoot does not use its Python wrappers), with
# Cairo drawing, as the macOS build's Homebrew RDKit has
if ! skip_if_done rdkit "${RDKIT_OUT}/include/rdkit/GraphMol/ROMol.h"; then
    echo "==> rdkit-$(dep_field rdkit 2)"
    srcdir="$(unpack rdkit)"
    "${CMAKE_OUT}/bin/cmake" -S "${srcdir}" -B "${srcdir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${RDKIT_OUT}" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_INSTALL_RPATH="${RDKIT_OUT}/lib;${BOOST_OUT}/lib" \
        -DCMAKE_PREFIX_PATH="${BOOST_OUT}" \
        -DBoost_ROOT="${BOOST_OUT}" -DBoost_NO_SYSTEM_PATHS=ON \
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
    "${CMAKE_OUT}/bin/cmake" --build "${srcdir}/build" -j "${JOBS}"
    "${CMAKE_OUT}/bin/cmake" --install "${srcdir}/build"
    echo "    installed rdkit"
fi

# --- verify ----------------------------------------------------------------
echo "==> verifying"
_fail=0
for mod in libart-2.0 libgnomecanvas-2.0 goocanvas; do
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
if "${PYTHON_OUT}/bin/python3.13" -c 'import ssl, ctypes, sqlite3, lzma, bz2' 2>/dev/null; then
    printf '    %-22s %s\n' "Python" "$("${PYTHON_OUT}/bin/python3.13" -c 'import sys; print(sys.version.split()[0])')"
else
    echo "    Python 3.13: NOT FOUND or missing ssl/ctypes/sqlite3/lzma/bz2" >&2; _fail=1
fi
if [ -x "${GSL_OUT}/bin/gsl-config" ]; then
    printf '    %-22s %s\n' "GSL" "$("${GSL_OUT}/bin/gsl-config" --version)"
else
    echo "    GSL: NOT FOUND" >&2; _fail=1
fi
if [ -x "${SWIG_OUT}/bin/swig" ]; then
    printf '    %-22s %s\n' "SWIG" "$("${SWIG_OUT}/bin/swig" -version | awk '/SWIG Version/{print $3}')"
else
    echo "    SWIG: NOT FOUND" >&2; _fail=1
fi
if ls "${BOOST_OUT}"/lib/libboost_python*.so >/dev/null 2>&1; then
    printf '    %-22s %s (%s)\n' "Boost" "$(dep_field boost 2)" \
        "$(basename "$(ls "${BOOST_OUT}"/lib/libboost_python*.so | head -1)" .so | sed 's/^lib//')"
else
    echo "    Boost.Python: NOT FOUND" >&2; _fail=1
fi
if ls "${RDKIT_OUT}"/lib/libRDKitMolDraw2D.so* >/dev/null 2>&1 && [ -d "${RDKIT_OUT}/share/RDKit/Data" ]; then
    printf '    %-22s %s\n' "RDKit" "$(dep_field rdkit 2)"
else
    echo "    RDKit (libRDKitMolDraw2D, share/RDKit/Data): NOT FOUND" >&2; _fail=1
fi
if [ -x "${PATCHELF_OUT}/bin/patchelf" ]; then
    printf '    %-22s %s\n' "patchelf" "$("${PATCHELF_OUT}/bin/patchelf" --version | awk '{print $2}')"
else
    echo "    patchelf: NOT FOUND" >&2; _fail=1
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
[ "${_fail}" = "0" ] || { echo "!! dependency build incomplete." >&2; exit 1; }

echo ""
echo "Dependencies installed under ${DEPS_PREFIX}"
echo "Now run: ./scripts/build-linux.sh"

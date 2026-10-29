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
#
# Tarballs are downloaded from upstream and verified against the checksums
# pinned below, then cached in <prefix>/src so re-runs need no network.
# The system packages these builds need are listed by scripts/build-linux.sh.
#
# ENVIRONMENT (all optional)
#   DEPS_PREFIX   install prefix     (default: <repo>/deps)
#   JOBS          parallel make jobs (default: CPU count)
#   DEPS_FORCE=1  rebuild even if already installed

set -e

if [ "$(uname)" != "Linux" ]; then
    echo "!! build_deps_linux.sh is for Linux; on macOS use scripts/build_deps.sh" >&2
    exit 1
fi

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

DEPS_PREFIX="${DEPS_PREFIX:-${REPO_ROOT}/deps}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

SRC_CACHE="${DEPS_PREFIX}/src"
WORK="${DEPS_PREFIX}/build"
FFTW_OUT="${DEPS_PREFIX}/fftw2"
CANVAS_OUT="${DEPS_PREFIX}/canvas"
WX_OUT="${DEPS_PREFIX}/wx"
GEMMI_OUT="${DEPS_PREFIX}/gemmi"
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
build_autotools libart_lgpl "${PCDIR}/libart-2.0.pc" "${CANVAS_OUT}" \
    --disable-static
build_autotools libgnomecanvas "${PCDIR}/libgnomecanvas-2.0.pc" "${CANVAS_OUT}" \
    --disable-static --disable-gtk-doc --disable-glibtest
build_autotools goocanvas "${PCDIR}/goocanvas.pc" "${CANVAS_OUT}" \
    --disable-static --disable-gtk-doc --disable-python

# wxWidgets on GTK 2, with the parts Bandicoot uses (OpenGL canvas, AUI, SVG
# icons). The C++ flags must not carry the old-C warning downgrades above.
CFLAGS_SAVED="${CFLAGS}"
export CFLAGS="-O2"
build_autotools wxWidgets "${WX_OUT}/bin/wx-config" "${WX_OUT}" \
    --with-gtk=2 --with-opengl --enable-aui --enable-svg \
    --disable-webview --disable-mediactrl --disable-tests --disable-precomp-headers
export CFLAGS="${CFLAGS_SAVED}"

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

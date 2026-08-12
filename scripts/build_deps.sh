#!/bin/bash
# Download and build the four dependencies that no package manager provides.
# Run once after a fresh checkout, before scripts/build.sh:
#
#     ./scripts/build_deps.sh
#
#   fftw2           single-precision FFTW 2. Hard configure dependency; FFTW 3
#                   is not a substitute. Not in Homebrew, conda or bioconda.
#   libart_lgpl     libgnomecanvas's only hard dependency
#   libgnomecanvas  GnomeCanvas widget; sets HAVE_GNOME_CANVAS
#   goocanvas 1.x   cairo canvas for the 2D ligand view (NOT 2.x -- GTK3 only)
#
# Without the canvas trio, Coot compiles but silently loses Sequence View, the
# 2D ligand editor and all geometry graphs. build.sh refuses to proceed rather
# than produce a quietly reduced Bandicoot.
#
# Tarballs are downloaded from upstream and verified against the checksums
# pinned below, then cached in <prefix>/src so re-runs need no network.
#
# ENVIRONMENT (all optional)
#   DEPS_PREFIX   install prefix     (default: <repo>/deps)
#   BREW_PREFIX   Homebrew root      (default: brew --prefix)
#   JOBS          parallel make jobs (default: CPU count)
#   DEPS_FORCE=1  rebuild even if already installed

set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

DEPS_PREFIX="${DEPS_PREFIX:-${REPO_ROOT}/deps}"
BREW_PREFIX="${BREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

SRC_CACHE="${DEPS_PREFIX}/src"
WORK="${DEPS_PREFIX}/build"
FFTW_OUT="${DEPS_PREFIX}/fftw2"
CANVAS_OUT="${DEPS_PREFIX}/canvas"
PCDIR="${CANVAS_OUT}/lib/pkgconfig"

mkdir -p "${SRC_CACHE}" "${WORK}" "${PCDIR}"

# Pinned upstream releases: name|version|url|sha256
DEPS_LIST="
fftw|2.1.5|https://www.fftw.org/fftw-2.1.5.tar.gz|f8057fae1c7df8b99116783ef3e94a6a44518d49c72e2e630c24b689c6022630
libart_lgpl|2.3.21|https://download.gnome.org/sources/libart_lgpl/2.3/libart_lgpl-2.3.21.tar.bz2|fdc11e74c10fc9ffe4188537e2b370c0abacca7d89021d4d303afdf7fd7476fa
libgnomecanvas|2.30.3|https://download.gnome.org/sources/libgnomecanvas/2.30/libgnomecanvas-2.30.3.tar.bz2|859b78e08489fce4d5c15c676fec1cd79782f115f516e8ad8bed6abcb8dedd40
goocanvas|1.0.0|https://download.gnome.org/sources/goocanvas/1.0/goocanvas-1.0.0.tar.bz2|1c072ef88567cad241fb4addee26e9bd96741b1503ff736d1c152fa6d865711e
"

echo "==> deps prefix: ${DEPS_PREFIX}"

# --- pkg-config shims ------------------------------------------------------
# cairo and freetype2 name x11/xcb/xext/xrender in their private Requires. A
# Quartz build never links them, but pkg-config fails the whole dependency chain
# if the .pc files are absent. Empty shims satisfy it and add no flags. bzip2 is
# real (via freetype2) but Homebrew keeps it keg-only, off the default path.
for shim in x11 xcb xcb-render xcb-shm xext xrender kbproto xproto; do
    cat > "${PCDIR}/${shim}.pc" <<EOF
prefix=/
Name: ${shim}
Description: macOS Quartz shim (no real X11 used)
Version: 99.99.99
Libs:
Cflags:
EOF
done
BZIP2_PREFIX="${BZIP2_PREFIX:-${BREW_PREFIX}/opt/bzip2}"
cat > "${PCDIR}/bzip2.pc" <<EOF
prefix=${BZIP2_PREFIX}
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: bzip2
Description: A file compression library (Homebrew shim)
Version: 1.0.8
Libs: -L\${libdir} -lbz2
Cflags: -I\${includedir}
EOF
[ -d "${BZIP2_PREFIX}" ] || echo "    !! warning: ${BZIP2_PREFIX} absent (brew install bzip2)" >&2

# --- build environment -----------------------------------------------------
export PKG_CONFIG_PATH="\
${PCDIR}:\
${BREW_PREFIX}/lib/pkgconfig:\
${BREW_PREFIX}/share/pkgconfig:\
${BREW_PREFIX}/Library/Homebrew/os/mac/pkgconfig/$(sw_vers -productVersion | cut -d. -f1)"

# FFTW 2 is from 1999, the canvas packages 2005-2010. Clang 16+ makes implicit
# function declarations and some pointer conversions hard errors by default,
# which breaks all four. Downgrading changes no generated code.
export CFLAGS="${CFLAGS:-} -O2 -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-deprecated-declarations"
export LDFLAGS="${LDFLAGS:-} -L${BREW_PREFIX}/lib"
export CPPFLAGS="${CPPFLAGS:-} -I${BREW_PREFIX}/include"

# --- fetch + build ---------------------------------------------------------
dep_field() { echo "${DEPS_LIST}" | grep "^$1|" | cut -d'|' -f"$2"; }

fetch() {   # <url> <sha256> -> echoes the cached path
    local url="$1" want="$2" tarball
    tarball="$(basename "${url}")"
    local dest="${SRC_CACHE}/${tarball}"

    if [ ! -f "${dest}" ]; then
        echo "    downloading ${tarball}" >&2
        curl -fSL --connect-timeout 30 -o "${dest}.part" "${url}" >&2 || {
            echo "!! download failed: ${url}" >&2
            echo "   If upstream has gone away, fetch it by hand into ${SRC_CACHE}/" >&2
            exit 1
        }
        mv "${dest}.part" "${dest}"
    fi

    local got
    got="$(shasum -a 256 "${dest}" | cut -d' ' -f1)"
    if [ "${got}" != "${want}" ]; then
        echo "!! checksum mismatch for ${tarball}" >&2
        echo "   expected ${want}" >&2
        echo "   got      ${got}" >&2
        echo "   Delete ${dest} and re-run to re-download." >&2
        exit 1
    fi
    echo "${dest}"
}

# Replace each tarball's bundled config.guess / config.sub with a current pair.
# FFTW 2.1.5 ships the 1999 versions, which predate arm64 entirely: config.guess
# cannot name the CPU, so configure is handed "-apple-darwin25.5.0" with an empty
# CPU field and dies with "config.sub ... failed". Homebrew's automake (already a
# documented prerequisite) carries an up-to-date pair.
AUX_SRC=""
for _d in "${BREW_PREFIX}"/share/automake-*/ "${BREW_PREFIX}"/share/libtool/build-aux/; do
    if [ -f "${_d}config.sub" ] && [ -f "${_d}config.guess" ]; then
        AUX_SRC="${_d}"
        break
    fi
done

refresh_autotools_aux() {   # <srcdir>
    local d="$1" f found=0
    [ -n "${AUX_SRC}" ] || return 0
    for f in config.sub config.guess; do
        # Only replace where the package already has one, and wherever it keeps
        # it (top level for these four, but some packages use a subdirectory).
        while IFS= read -r target; do
            cp -f "${AUX_SRC}${f}" "${target}"
            chmod +x "${target}"
            found=1
        done < <(find "${d}" -name "${f}" -type f 2>/dev/null)
    done
    [ "${found}" = "1" ] && echo "    refreshed config.sub/config.guess from ${AUX_SRC}"
    return 0
}

build_component() {   # <name> <sentinel> [configure args...]
    local name="$1" sentinel="$2"; shift 2
    local ver url sha tarball srcdir prefix

    ver="$(dep_field "${name}" 2)"
    url="$(dep_field "${name}" 3)"
    sha="$(dep_field "${name}" 4)"
    [ -n "${ver}" ] || { echo "!! no pin for ${name}" >&2; exit 1; }

    if [ "${DEPS_FORCE:-0}" != "1" ] && [ -e "${sentinel}" ]; then
        echo "==> ${name}-${ver}: already installed, skipping"
        return 0
    fi

    echo "==> ${name}-${ver}"
    tarball="$(fetch "${url}" "${sha}")"

    case "${name}" in
        fftw) prefix="${FFTW_OUT}" ;;
        *)    prefix="${CANVAS_OUT}" ;;
    esac

    srcdir="${WORK}/${name}-${ver}"
    rm -rf "${srcdir}"
    tar -xf "${tarball}" -C "${WORK}"
    [ -d "${srcdir}" ] || { echo "!! extracted tree ${srcdir} not found" >&2; exit 1; }

    refresh_autotools_aux "${srcdir}"

    ( cd "${srcdir}" && ./configure --prefix="${prefix}" "$@" && make -j"${JOBS}" && make install )

    [ -e "${sentinel}" ] || { echo "!! ${name} built but ${sentinel} missing" >&2; exit 1; }
    echo "    installed ${name}-${ver}"
}

build_component fftw           "${FFTW_OUT}/include/fftw.h" \
    --enable-float --enable-shared --disable-fortran
build_component libart_lgpl    "${PCDIR}/libart-2.0.pc" \
    --disable-static
build_component libgnomecanvas "${PCDIR}/libgnomecanvas-2.0.pc" \
    --disable-static --disable-gtk-doc --disable-glibtest
build_component goocanvas      "${PCDIR}/goocanvas.pc" \
    --disable-static --disable-gtk-doc --disable-python

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
[ "${_fail}" = "0" ] || { echo "!! dependency build incomplete." >&2; exit 1; }

echo ""
echo "Dependencies installed under ${DEPS_PREFIX}"
echo "Now run: ./scripts/build.sh"

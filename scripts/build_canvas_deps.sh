#!/bin/bash
# Build the three GNOME canvas libraries Bandicoot needs but that Homebrew no
# longer carries, into a self-contained prefix.
#
# WHY THIS EXISTS
# ---------------
# Coot's configure sets HAVE_GNOME_CANVAS only when it finds libgnomecanvas-2.0
# and goocanvas. Without them the build silently drops Sequence View (Draw menu),
# the 2D ligand editor, and every geometry graph (Ramachandran, density fit,
# rotamer analysis, Kleywegt plots). All three packages were dropped by Homebrew
# years ago -- they are GTK2-era and effectively unmaintained -- so before
# v0.1.4.13 they were built by hand into ~/sw/canvas-deps, a tree that existed
# only on the maintainer's machine. That made the source tree unbuildable by
# anyone else (GitHub #4). This script reproduces that tree from pinned upstream
# releases.
#
# It also writes the pkg-config SHIMS the GTK2-Quartz stack needs on macOS:
# cairo and freetype2 declare private Requires on x11/xcb/xext/xrender, which do
# not exist in a Quartz build and are never linked against, plus a bzip2 .pc that
# Homebrew keeps out of the default search path. Without these, pkg-config fails
# the whole dependency chain and configure reports the canvas libraries missing
# even after they are installed.
#
# USAGE
#   ./scripts/build_canvas_deps.sh [prefix]
#
# ENVIRONMENT (all optional)
#   CANVAS_PREFIX      install prefix; overridden by the positional argument.
#                      Default: <repo>/deps/canvas
#   BREW_PREFIX        Homebrew root, for gtk+2/cairo/bzip2. Default: brew --prefix
#   CANVAS_SRC_CACHE   directory holding pre-downloaded tarballs, so the script
#                      can run with no network. Default: <prefix>/src
#   JOBS               parallel make jobs. Default: number of CPUs
#   CANVAS_FORCE=1     rebuild components even if already installed
#
# Idempotent: a component whose .pc file is already present is skipped unless
# CANVAS_FORCE=1. Safe to re-run.

set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

CANVAS_PREFIX="${1:-${CANVAS_PREFIX:-${REPO_ROOT}/deps/canvas}}"
mkdir -p "${CANVAS_PREFIX}"
CANVAS_PREFIX="$(cd "${CANVAS_PREFIX}" && pwd)"

BREW_PREFIX="${BREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
SRC_CACHE="${CANVAS_SRC_CACHE:-${CANVAS_PREFIX}/src}"
mkdir -p "${SRC_CACHE}"

PCDIR="${CANVAS_PREFIX}/lib/pkgconfig"
mkdir -p "${PCDIR}"

echo "==> canvas deps prefix: ${CANVAS_PREFIX}"
echo "==> homebrew prefix:    ${BREW_PREFIX}"
echo "==> source cache:       ${SRC_CACHE}"

# Pinned upstream releases. These are the exact versions the pre-v0.1.4.13
# hand-built tree carried, so a build from this script matches what shipped.
LIBART_VER="2.3.21"
LIBGNOMECANVAS_VER="2.30.3"
GOOCANVAS_VER="1.0.0"

LIBART_URL="https://download.gnome.org/sources/libart_lgpl/2.3/libart_lgpl-${LIBART_VER}.tar.bz2"
LIBGNOMECANVAS_URL="https://download.gnome.org/sources/libgnomecanvas/2.30/libgnomecanvas-${LIBGNOMECANVAS_VER}.tar.bz2"
GOOCANVAS_URL="https://download.gnome.org/sources/goocanvas/1.0/goocanvas-${GOOCANVAS_VER}.tar.bz2"

# ---------------------------------------------------------------------------
# 1. pkg-config shims
# ---------------------------------------------------------------------------
# Written FIRST: the canvas packages' own configure runs pkg-config on gtk+-2.0,
# which pulls cairo's private Requires, which name x11/xcb/xext/xrender. Those
# are absent in a Quartz build. Empty .pc files satisfy the dependency graph and
# contribute no flags, so nothing links against X11.

write_x_shim() {
    local name="$1"
    cat > "${PCDIR}/${name}.pc" <<EOF
prefix=/
Name: ${name}
Description: macOS Quartz shim (no real X11 used)
Version: 99.99.99
Libs:
Cflags:
EOF
}

echo "==> writing pkg-config shims into ${PCDIR}"
for shim in x11 xcb xcb-render xcb-shm xext xrender kbproto xproto; do
    write_x_shim "$shim"
done

# bzip2 is a REAL dependency (freetype2 pulls it) but Homebrew keeps it
# keg-only, so its .pc is not on the default search path. Point at the keg.
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
if [ ! -d "${BZIP2_PREFIX}" ]; then
    echo "    !! warning: ${BZIP2_PREFIX} does not exist (brew install bzip2)" >&2
fi
echo "    wrote 9 shim .pc files"

# ---------------------------------------------------------------------------
# 2. build environment
# ---------------------------------------------------------------------------
export PKG_CONFIG_PATH="\
${PCDIR}:\
${BREW_PREFIX}/lib/pkgconfig:\
${BREW_PREFIX}/share/pkgconfig:\
${BREW_PREFIX}/Library/Homebrew/os/mac/pkgconfig/$(sw_vers -productVersion | cut -d. -f1)"

# These packages date from 2005-2010 and were written for a C89/C99-era
# compiler. Clang 16+ promotes implicit function declarations and several
# pointer-conversion warnings to hard errors by default, which breaks all three
# builds. Downgrading them back to warnings is the standard fix; it changes no
# generated code.
export CFLAGS="${CFLAGS:-} -O2 -Wno-implicit-function-declaration -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-deprecated-declarations"
export LDFLAGS="${LDFLAGS:-} -L${BREW_PREFIX}/lib"
export CPPFLAGS="${CPPFLAGS:-} -I${BREW_PREFIX}/include"

# ---------------------------------------------------------------------------
# 3. component build
# ---------------------------------------------------------------------------

fetch() {
    local url="$1" tarball="$2"
    if [ -f "${SRC_CACHE}/${tarball}" ]; then
        echo "    using cached ${tarball}"
        return 0
    fi
    echo "    fetching ${url}"
    if command -v curl >/dev/null 2>&1; then
        curl -fSL -o "${SRC_CACHE}/${tarball}.part" "${url}"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "${SRC_CACHE}/${tarball}.part" "${url}"
    else
        echo "!! neither curl nor wget available, and ${tarball} is not cached." >&2
        echo "   Download it manually to ${SRC_CACHE}/ and re-run:" >&2
        echo "     ${url}" >&2
        exit 1
    fi
    mv "${SRC_CACHE}/${tarball}.part" "${SRC_CACHE}/${tarball}"
}

build_component() {
    local name="$1" ver="$2" url="$3" pcname="$4"
    shift 4
    local tarball="${name}-${ver}.tar.bz2"
    local srcdir="${SRC_CACHE}/${name}-${ver}"

    if [ "${CANVAS_FORCE:-0}" != "1" ] && [ -f "${PCDIR}/${pcname}.pc" ]; then
        echo "==> ${name}-${ver}: already installed (${pcname}.pc present), skipping"
        return 0
    fi

    echo "==> ${name}-${ver}"
    fetch "${url}" "${tarball}"

    rm -rf "${srcdir}"
    tar -xf "${SRC_CACHE}/${tarball}" -C "${SRC_CACHE}"
    [ -d "${srcdir}" ] || { echo "!! extracted tree ${srcdir} not found" >&2; exit 1; }

    (
        cd "${srcdir}"
        ./configure --prefix="${CANVAS_PREFIX}" "$@"
        make -j"${JOBS}"
        make install
    )

    if [ ! -f "${PCDIR}/${pcname}.pc" ]; then
        echo "!! ${name} installed but ${PCDIR}/${pcname}.pc is missing." >&2
        exit 1
    fi
    echo "    installed ${name}-${ver}"
}

# libart_lgpl: 2D vector rasteriser, the only hard dependency of
# libgnomecanvas. No GTK dependency of its own.
build_component libart_lgpl "${LIBART_VER}" "${LIBART_URL}" libart-2.0 \
    --disable-static

# libgnomecanvas: the GnomeCanvas widget. Needs gtk+-2.0, pango/pangoft2 and
# gail (all from Homebrew's gtk+2) plus libart above. gtk-doc is disabled --
# it only builds API documentation and pulls a large toolchain.
build_component libgnomecanvas "${LIBGNOMECANVAS_VER}" "${LIBGNOMECANVAS_URL}" \
    libgnomecanvas-2.0 \
    --disable-static --disable-gtk-doc --disable-glibtest

# goocanvas 1.x: the cairo-based canvas Coot uses for the 2D ligand view.
# NOT goocanvas 2.x -- that is GTK3-only and will not link here.
build_component goocanvas "${GOOCANVAS_VER}" "${GOOCANVAS_URL}" goocanvas \
    --disable-static --disable-gtk-doc --disable-python

# ---------------------------------------------------------------------------
# 4. verify
# ---------------------------------------------------------------------------
echo "==> verifying with pkg-config"
_fail=0
for mod in libart-2.0 libgnomecanvas-2.0 goocanvas; do
    if PKG_CONFIG_PATH="${PKG_CONFIG_PATH}" pkg-config --exists "${mod}"; then
        printf '    %-22s %s\n' "${mod}" \
            "$(PKG_CONFIG_PATH="${PKG_CONFIG_PATH}" pkg-config --modversion "${mod}")"
    else
        echo "    ${mod}: NOT FOUND" >&2
        _fail=1
    fi
done
if [ "${_fail}" != "0" ]; then
    echo "!! canvas dependency build incomplete." >&2
    exit 1
fi

echo ""
echo "Canvas dependencies installed in ${CANVAS_PREFIX}"
echo "Pass this to the main build:"
echo "    CANVAS_PREFIX=${CANVAS_PREFIX} ./scripts/build.sh"

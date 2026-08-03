#!/bin/sh
#
# package.sh — roll the Bandicoot install into a release tarball + sha256.
#
# Build-it-yourself release recipe (run from the repo root):
#
#     BANDICOOT_RELEASE=1 FFTW_PREFIX=/opt/miniconda3/fftw2 ./scripts/build.sh
#     ./scripts/package.sh
#
# build.sh compiles + populates the install ($HOME/sw/bandicoot-install by
# default); this script tars that install so its top-level directory is
# bandicoot-<version>/ and writes:
#
#     <releases>/<version>/bandicoot-<version>-darwin-<arch>.tar.gz
#     <releases>/<version>/bandicoot-<version>-darwin-<arch>.tar.gz.sha256
#
# The end user unpacks the tarball and runs the bundled setup.sh (ad-hoc
# codesign + gdk-pixbuf loaders.cache + Spotlight registration).
#
# Environment overrides:
#     PREFIX   install directory to package   (default: $HOME/sw/bandicoot-install)
#     OUT      output directory               (default: <repo>/../releases/<version>)
#
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- version from the single source of truth -------------------------------
VERSION_H="$REPO/src/bandicoot-version.h"
# require whitespace + opening quote after the macro name so the include guard
# (#define BANDICOOT_VERSION_H) doesn't match; take the first hit.
VERSION="$(awk -F'"' '/define[ \t]+BANDICOOT_VERSION[ \t]+"/ {print $2; exit}' "$VERSION_H")"
if [ -z "${VERSION:-}" ]; then
    echo "package.sh: could not read BANDICOOT_VERSION from $VERSION_H" >&2
    exit 1
fi

INSTALL="${PREFIX:-$HOME/sw/bandicoot-install}"
ARCH="$(uname -m)"                       # arm64 on Apple Silicon
NAME="bandicoot-${VERSION}"
TARBASE="${NAME}-darwin-${ARCH}"
OUTDIR="${OUT:-$(dirname "$REPO")/releases/${VERSION}}"
TARBALL="${OUTDIR}/${TARBASE}.tar.gz"

# --- sanity checks ---------------------------------------------------------
if [ ! -d "$INSTALL" ] || [ ! -x "$INSTALL/setup.sh" ]; then
    echo "package.sh: '$INSTALL' is not a built install (no setup.sh)." >&2
    echo "            Run scripts/build.sh first, or set PREFIX." >&2
    exit 1
fi

# --- pre-ship dependency gates ---------------------------------------------
# These catch the class of bug that shipped v0.1.4.2..v0.1.4.8 with a broken
# _ssl (a bundled Mach-O naming a dependency that isn't in the tree, which
# only fails at dlopen -- i.e. on the user's machine). codesign --verify does
# NOT catch it. Two complementary checks; set PACKAGE_SKIP_CHECKS=1 to bypass
# (e.g. a deliberately partial install), but the default is to refuse to ship.
#   1. check_install.sh   -- static: fails on any build-host (conda/canvas)
#      path leak AND on any unresolved @rpath dep.
#      v0.1.4.11: this used to pass --warn-unresolved, which downgraded the
#      UNRESOLVED class to a warning because a backlog of optional stdlib
#      extensions "dlopen fine via fallback". That fallback IS the bug -- when
#      an @rpath lookup fails, dyld tries DYLD_FALLBACK_LIBRARY_PATH, whose
#      default ends in /usr/lib, so the module silently binds to whatever the
#      host OS ships. It works on the build machine and breaks on a user's:
#      GitHub #8 (pyexpat vs the system libexpat, all XML parsing dead on any
#      macOS with expat < 2.7.2). The backlog is now bundled (libexpat,
#      libmpdec, libbz2, readline+ncurses) or removed (_tkinter), so the gate
#      is armed. Do NOT re-add --warn-unresolved to make a release go through;
#      bundle the library instead.
#   2. smoke_test_imports.sh -- runtime: launches the shipped interpreter and
#      imports every module a session relies on (ssl, sqlite3, ctypes, numpy,
#      matplotlib, coot, ...). Zero false positives.
#      NOTE the two gates are complementary and neither subsumes the other.
#      This one cannot see a dep that resolves via the /usr/lib fallback on the
#      BUILD host but not on a user's: pyexpat imports fine here (macOS 26.5
#      ships expat 2.7.4) and still broke for the #8 reporter. Only gate 1
#      catches that class, which is why it must stay armed.
if [ "${PACKAGE_SKIP_CHECKS:-0}" != "1" ]; then
    echo "==> pre-ship gate: static dependency closure"
    if ! "$SCRIPT_DIR/check_install.sh" "$INSTALL"; then
        echo "package.sh: ERROR — dependency-closure check failed (build-host leak," >&2
        echo "            or a shipped Mach-O names a dependency we do not ship)." >&2
        echo "            Bundle the missing library in bundle_conda_deps.sh /" >&2
        echo "            bundle_homebrew_deps.sh. Do not paper over it with" >&2
        echo "            --warn-unresolved; see the note above." >&2
        exit 1
    fi
    echo "==> pre-ship gate: runtime import smoke test"
    if ! "$SCRIPT_DIR/smoke_test_imports.sh" "$INSTALL"; then
        echo "package.sh: ERROR — a critical module failed to import in the shipped" >&2
        echo "            interpreter. Fix the bundling or set PACKAGE_SKIP_CHECKS=1." >&2
        exit 1
    fi
    echo "==> pre-ship gate: splash version"
    if ! "$SCRIPT_DIR/check_splash_version.sh" \
             "$INSTALL/share/coot/pixmaps/bandicoot-splash.png" "$VERSION"; then
        echo "package.sh: ERROR — the splash screen does not show release $VERSION." >&2
        echo "            Update pixmaps/bandicoot-splash.png (and rebuild) or set" >&2
        echo "            PACKAGE_SKIP_CHECKS=1 to override." >&2
        exit 1
    fi
    echo "==> pre-ship gate: runtime assets present"
    if ! "$SCRIPT_DIR/check_assets.sh" "$INSTALL"; then
        echo "package.sh: ERROR — a required runtime asset is missing from the install" >&2
        echo "            (e.g. the monomer dictionary -> refinement would fail). A" >&2
        echo "            build-host data source was absent and its copy silently" >&2
        echo "            skipped; see check_assets output. Set PACKAGE_SKIP_CHECKS=1" >&2
        echo "            to override." >&2
        exit 1
    fi
    echo "==> pre-ship gate: no build-host home paths"
    if ! "$SCRIPT_DIR/check_buildhost_paths.sh" "$INSTALL"; then
        echo "package.sh: ERROR — a shipped binary/script embeds the builder's home" >&2
        echo "            path (/Users/<name>). Rebuild clean (BANDICOOT_RELEASE=1) or" >&2
        echo "            set PACKAGE_SKIP_CHECKS=1 to override." >&2
        exit 1
    fi
fi

# Warn (don't fail) if this looks like a dev build rather than a release one.
BUILD_ID_H="$REPO/src/bandicoot-build-id.h"
if [ -f "$BUILD_ID_H" ]; then
    SUFFIX="$(awk -F'"' '/BANDICOOT_BUILD_SUFFIX/ {print $2}' "$BUILD_ID_H")"
    if [ -n "${SUFFIX:-}" ]; then
        echo "package.sh: WARNING — build suffix is '${SUFFIX}', so this is a DEV build."
        echo "            For a clean release run: BANDICOOT_RELEASE=1 ./scripts/build.sh"
    fi
fi

echo "==> packaging Bandicoot ${VERSION} (${ARCH})"
echo "    install : ${INSTALL}"
echo "    tarball : ${TARBALL}"
mkdir -p "$OUTDIR"
rm -f "$TARBALL" "${TARBALL}.sha256"

# --- bundle license + attribution docs at the tarball root -----------------
# GPLv3 requires the license (COPYING) and copyright/attribution notices to
# accompany the conveyed binary; THIRD_PARTY_LICENSES.md covers the bundled
# non-GPL components. Copy them into the install tree so they land at
# bandicoot-<version>/ inside the archive. COPYING is mandatory — fail without
# it; the rest are warned about but not fatal.
if [ ! -f "$REPO/COPYING" ]; then
    echo "package.sh: ERROR — $REPO/COPYING is missing; refusing to ship a" >&2
    echo "            GPL binary without its license text." >&2
    exit 1
fi
for doc in COPYING README.md AUTHORS THIRD_PARTY_LICENSES.md CORRESPONDING_SOURCE.md; do
    if [ -f "$REPO/$doc" ]; then
        cp -f "$REPO/$doc" "$INSTALL/$doc"
        echo "    bundling doc: $doc"
    else
        echo "package.sh: WARNING — $doc not found in repo root; not bundled." >&2
    fi
done

INSTALL_PARENT="$(dirname "$INSTALL")"
INSTALL_BASE="$(basename "$INSTALL")"

# Rename the top-level dir to bandicoot-<version>/ inside the archive without a
# copy (bsdtar -s path substitution, the macOS default). Inner symlinks (e.g.
# bin/bcoot -> coot) are preserved because we do NOT dereference. GNU tar lacks
# -s, so fall back to a staged copy (cp -RP preserves symlinks).
# Exclude the build-seeded gdk-pixbuf loaders.cache: build.sh generates it with
# THIS machine's absolute loader paths (a /Users/<builder> leak), and setup.sh
# regenerates it with the user's paths on install anyway. Keep it in the dev
# tree (so `bin/bcoot` shows icons here) but don't ship the build-path copy.
if tar --version 2>&1 | grep -qi bsdtar; then
    tar -C "$INSTALL_PARENT" --exclude '.DS_Store' --exclude '*/loaders.cache' \
        -s "|^${INSTALL_BASE}|${NAME}|" \
        -czf "$TARBALL" "$INSTALL_BASE"
else
    STAGE="$(mktemp -d)"
    trap 'rm -rf "$STAGE"' EXIT
    cp -RP "$INSTALL" "$STAGE/$NAME"
    tar -C "$STAGE" --exclude '.DS_Store' --exclude '*/loaders.cache' -czf "$TARBALL" "$NAME"
    rm -rf "$STAGE"
    trap - EXIT
fi

# --- checksum --------------------------------------------------------------
( cd "$OUTDIR" && shasum -a 256 "${TARBASE}.tar.gz" > "${TARBASE}.tar.gz.sha256" )

SIZE="$(du -h "$TARBALL" | cut -f1)"
echo "==> done"
echo "    ${TARBALL}  (${SIZE})"
echo "    sha256: $(awk '{print $1}' "${TARBALL}.sha256")"

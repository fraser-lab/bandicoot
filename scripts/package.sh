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

INSTALL_PARENT="$(dirname "$INSTALL")"
INSTALL_BASE="$(basename "$INSTALL")"

# Rename the top-level dir to bandicoot-<version>/ inside the archive without a
# copy (bsdtar -s path substitution, the macOS default). Inner symlinks (e.g.
# bin/bcoot -> coot) are preserved because we do NOT dereference. GNU tar lacks
# -s, so fall back to a staged copy (cp -RP preserves symlinks).
if tar --version 2>&1 | grep -qi bsdtar; then
    tar -C "$INSTALL_PARENT" --exclude '.DS_Store' \
        -s "|^${INSTALL_BASE}|${NAME}|" \
        -czf "$TARBALL" "$INSTALL_BASE"
else
    STAGE="$(mktemp -d)"
    trap 'rm -rf "$STAGE"' EXIT
    cp -RP "$INSTALL" "$STAGE/$NAME"
    tar -C "$STAGE" --exclude '.DS_Store' -czf "$TARBALL" "$NAME"
    rm -rf "$STAGE"
    trap - EXIT
fi

# --- checksum --------------------------------------------------------------
( cd "$OUTDIR" && shasum -a 256 "${TARBASE}.tar.gz" > "${TARBASE}.tar.gz.sha256" )

SIZE="$(du -h "$TARBALL" | cut -f1)"
echo "==> done"
echo "    ${TARBALL}  (${SIZE})"
echo "    sha256: $(awk '{print $1}' "${TARBALL}.sha256")"

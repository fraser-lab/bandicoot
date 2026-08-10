#!/bin/sh
#
# Build gemmi-mmdb-diff.
#
# This tool is deliberately NOT part of the autotools build: it links against
# an ALREADY-INSTALLED Bandicoot (for the real get_atom_selection) plus gemmi,
# and gemmi is not yet a configure-time dependency of the tree. Keeping it
# standalone means it costs the normal build nothing and cannot break it.
#
# Environment overrides (all optional):
#   PREFIX   installed Bandicoot to link against (default: ~/sw/bandicoot-0.2-install)
#   CONDA_PREFIX / BREW_PREFIX   as in scripts/build.sh
#
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

PREFIX="${PREFIX:-$HOME/sw/bandicoot-0.2-install}"
CONDA_PREFIX="${CONDA_PREFIX:-/opt/miniconda3}"
BREW_PREFIX="${BREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"

if [ ! -d "$PREFIX/lib" ]; then
    echo "build.sh: '$PREFIX' does not look like a Bandicoot install." >&2
    echo "          Build and install first, or set PREFIX." >&2
    exit 1
fi

# -std=c++17 to match the tree (configure.ac). The include order matters only in
# that $REPO must come first, so "coot-utils/..." resolves against the source
# tree rather than anything installed.
set -x
clang++ -std=c++17 -g -O1 -Wall -Wno-unused \
    -o "$HERE/gemmi-mmdb-diff" "$HERE/gemmi-mmdb-diff.cc" \
    -I"$REPO" \
    -I"$CONDA_PREFIX/include" \
    -I"$BREW_PREFIX/include" \
    -L"$PREFIX/lib" -L"$CONDA_PREFIX/lib" -L"$BREW_PREFIX/lib" \
    -lcoot-coord-utils -lcoot-utils -lmmdb2 -lgemmi_cpp \
    -Wl,-rpath,"$PREFIX/lib" -Wl,-rpath,"$CONDA_PREFIX/lib" -Wl,-rpath,"$BREW_PREFIX/lib"

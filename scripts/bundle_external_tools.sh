#!/bin/bash
# Bundle external command-line tools that Bandicoot's C++ shims fork/exec
# at runtime (`probe` and `reduce` for Local Probe Dots). Without these
# in <install>/bin/, the action degrades to "tool not found on PATH" on
# any tester who hasn't independently installed CCP4 / Phenix / MolProbity.
#
# Tools we bundle:
#   probe   — Richardson Lab MolProbity probe (BSD-3-clause)
#   reduce  — Richardson Lab MolProbity reduce (BSD-3-clause), needed to
#             add explicit hydrogens before probe (without H, probe's
#             default `-Explicit` mode finds essentially no contacts).
#   share/coot/reduce_wwPDB_het_dict.txt — reduce's heteroatom
#             dictionary, required for HETATM groups. Passed via -DB.
#
# Source binaries default to the arm64 build that ships with CCP4
# 9.0.014_arm; override with PROBE_SRC / REDUCE_SRC / REDUCE_HET_SRC.
#
# Usage:
#   ./scripts/bundle_external_tools.sh /path/to/install [probe_src]
#
# Idempotent. Existing files in <install>/bin are overwritten.

set -e

PREFIX="${1:?Usage: $0 <install-prefix> [probe-src]}"
PREFIX="$(cd "$PREFIX" && pwd)"

[ -d "$PREFIX/bin" ] || { echo "error: $PREFIX/bin missing" >&2; exit 1; }

CCP4_BIN_DEFAULT="/programs/i386-mac/ccp4/9.0.014_arm/ccp4-9/bin"
PROBE_SRC="${2:-${PROBE_SRC:-${CCP4_BIN_DEFAULT}/probe}}"
REDUCE_SRC="${REDUCE_SRC:-${CCP4_BIN_DEFAULT}/reduce}"
REDUCE_HET_SRC="${REDUCE_HET_SRC:-/programs/i386-mac/ccp4/9.0.014_arm/ccp4-9/Frameworks/Python.framework/Versions/3.9/lib/python3.9/site-packages/reduce/reduce_wwPDB_het_dict.txt}"

echo "==> bundling external tools into $PREFIX/bin/"

copy_bin() {
    local src="$1"
    local name="$(basename "$src")"
    if [ ! -x "$src" ]; then
        echo "    WARN: $name source not at $src; Local Probe Dots may fall back to PATH lookup." >&2
        return 1
    fi
    cp -f "$src" "$PREFIX/bin/$name"
    chmod +x "$PREFIX/bin/$name"
    echo "    copied $name ($(file -b "$PREFIX/bin/$name" | head -c 60)...)"
}

copy_bin "$PROBE_SRC"  || true
copy_bin "$REDUCE_SRC" || true

# Bundle reduce's heteroatom dictionary into share/coot/. coot.in
# exports REDUCE_HET_DICT to point here, so reduce -build picks it up
# without us needing to pass -DB on every invocation.
SHARE_DIR="$PREFIX/share/coot"
mkdir -p "$SHARE_DIR"
if [ -f "$REDUCE_HET_SRC" ]; then
    cp -f "$REDUCE_HET_SRC" "$SHARE_DIR/reduce_wwPDB_het_dict.txt"
    echo "    copied reduce_wwPDB_het_dict.txt ($(wc -l < "$SHARE_DIR/reduce_wwPDB_het_dict.txt") lines)"
else
    echo "    WARN: reduce_wwPDB_het_dict.txt source not at $REDUCE_HET_SRC; reduce will work for std residues but may complain about HETs." >&2
fi

echo "==> bundle_external_tools: done"

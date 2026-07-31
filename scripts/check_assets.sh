#!/bin/bash
# Bandicoot: pre-ship RUNTIME-ASSET presence gate.
#
#   check_assets.sh <INSTALL_DIR>
#
# check_install.sh proves every Mach-O dependency resolves; smoke_test_imports.sh
# proves the Python extensions load. Neither notices a MISSING DATA ASSET -- the
# monomer dictionary, reference structures, the SVG pixbuf loader, probe/reduce,
# etc. Those are copied into the install from build-host paths (coot-deps,
# homebrew, CCP4) by steps guarded with `if [ -d "$src" ]`, which SILENTLY SKIP
# when the source is absent. That is exactly how a monomer-less install shipped
# (refinement then died with "No dictionary group found for residue type").
#
# This gate is the positive complement: it asserts every essential runtime asset
# is actually PRESENT in the install, catching any silently-missing asset
# regardless of which copy step failed or why. Add an entry here whenever a new
# must-have asset is introduced.
#
# Exit: 0 if all required assets present; 1 if any is missing. Read-only.

set -u

INSTALL_DIR="${1:-}"
if [ -z "$INSTALL_DIR" ] || [ ! -d "$INSTALL_DIR" ]; then
    echo "!! check_assets.sh: usage: check_assets.sh <INSTALL_DIR>" >&2
    exit 2
fi
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd)"

missing=0
ok=0

# require <label> <test-expression...>
#   passes if the expression succeeds; otherwise reports MISSING and flags fail.
require() {
    local label="$1"; shift
    if "$@" >/dev/null 2>&1; then
        ok=$((ok + 1))
    else
        echo "  MISSING: $label" >&2
        missing=$((missing + 1))
    fi
}
# helpers usable as test expressions
has_file() { [ -f "$1" ]; }
has_dir()  { [ -d "$1" ]; }
has_exe()  { [ -x "$1" ]; }
has_glob() { [ -n "$(eval "ls -d $* 2>/dev/null" | head -1)" ]; }   # any match
has_find() { [ -n "$(find "$1" -iname "$2" -print -quit 2>/dev/null)" ]; }

I="$INSTALL_DIR"
echo "==> checking required runtime assets under $I"

# --- the application + its libraries ---------------------------------------
require "app binary (libexec/Bandicoot)"          has_file "$I/libexec/Bandicoot"
require "libcoot-*.dylib (>10)"                   has_glob "$I/lib/libcoot-*.dylib"

# --- crystallography data (refinement / building) --------------------------
require "monomer dict ALA.cif"                    has_find "$I/share/coot/lib/data/monomers" "ALA.cif"
require "monomer dict GLY.cif"                     has_find "$I/share/coot/lib/data/monomers" "GLY.cif"
require "monomer dict TYR.cif"                     has_find "$I/share/coot/lib/data/monomers" "TYR.cif"
require "reference-structures/"                    has_dir  "$I/share/coot/reference-structures"
require "syminfo.lib"                              has_file "$I/share/coot/syminfo.lib"
require "standard-residues.pdb"                    has_file "$I/share/coot/standard-residues.pdb"
require "reduce het dictionary"                    has_file "$I/share/coot/reduce_wwPDB_het_dict.txt"
require "rama-data/"                               has_dir  "$I/share/coot/rama-data"

# --- embedded Python (scripting + Phenix interface) ------------------------
require "coot Python module"                       has_file "$I/lib/python3.13/site-packages/coot/coot_utils.py"
require "python stdlib (encodings/)"               has_dir  "$I/lib/python3.13/encodings"

# --- GUI assets (icons / theme / fonts) ------------------------------------
require "pixmaps/ (icons)"                         has_dir  "$I/share/coot/pixmaps"
require "SVG pixbuf loader"                        has_file "$I/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader_svg.dylib"
require "gdk-pixbuf-query-loaders (setup.sh)"      has_exe  "$I/libexec/gdk-pixbuf-query-loaders"
require "Raleigh GTK theme"                        has_dir  "$I/share/themes/Raleigh"
require "fonts"                                    has_glob "$I/share/coot/fonts/*"

# --- bundled external tools (Local Probe Dots) -----------------------------
require "reduce binary"                            has_exe  "$I/bin/reduce"
require "probe binary"                             has_exe  "$I/bin/probe"

# --- setup / signing helpers shipped for the end user ----------------------
require "setup.sh"                                 has_file "$I/setup.sh"
require "codesign-install.sh"                      has_file "$I/codesign-install.sh"

if [ "$missing" -ne 0 ]; then
    echo "!! check_assets: FAIL — $missing required asset(s) missing ($ok present)." >&2
    echo "   A build-host data source was absent and its copy step silently skipped." >&2
    echo "   (e.g. monomers live at ~/sw/coot-builds/coot-deps; set COOT_DATA_SRC_OVERRIDE)." >&2
    exit 1
fi
echo "ok  check_assets: all $ok required runtime assets present"

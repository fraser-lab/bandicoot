#!/bin/bash
# Bandicoot: pre-ship BUILD-HOST-PATH leak gate.
#
#   check_buildhost_paths.sh <INSTALL_DIR>
#
# A shipped install must not embed the BUILDER's home path (/Users/<builder>/...).
# Those come from compile-time fallback macros baked into the Bandicoot-compiled
# binaries (-DPKGDATADIR / -DPKGPYTHONDIR, derived from --prefix) and from the
# launcher scripts. They're dead fallbacks at runtime (the wrapper exports
# COOT_DATA_DIR from the real extract location), but they leak the builder's
# identity and are a stale-object smell: if `/Users/<builder>` is present, an
# old object survived a prefix change (build.sh builds RELEASE from clean and
# configures with a generic BANDICOOT_COMPILE_PREFIX to prevent exactly this).
#
# Scans the Bandicoot-compiled Mach-O (libexec/, bin/, lib/*.dylib) via `strings`
# and the shipped shell scripts. Ignores /Users/Shared (a generic shared path)
# and loaders.cache (package.sh excludes it from the tarball; setup.sh
# regenerates it on the user's machine). Third-party wheels' Python sources are
# out of scope (conda's _sysconfigdata carries /opt/miniconda3, not /Users, and
# is build metadata, not executable path logic).
#
# Exit: 0 if clean; 1 if any /Users/<builder> path is embedded in a shipped
# binary or script. Read-only.

set -u
export LC_ALL=C   # byte-oriented sort/grep over Mach-O strings (no UTF-8 validation)

INSTALL_DIR="${1:?usage: check_buildhost_paths.sh <install>}"
[ -d "$INSTALL_DIR" ] || { echo "!! check_buildhost_paths: no such dir: $INSTALL_DIR" >&2; exit 2; }
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd)"

# Target THIS build's home directory specifically -- the builder-identity leak
# we control. NOT any /Users/<name>: third-party prebuilt libraries we bundle
# (conda's libpython, Homebrew's librsvg, CCP4's reduce, ...) legitimately carry
# their own CI build paths (/Users/runner, /Users/brew, /Users/Shared/...Jenkins)
# baked in by whoever built them -- those are unavoidable and not ours to strip.
BUILDER_HOME="${HOME%/}"
if [ -z "$BUILDER_HOME" ] || [ "$BUILDER_HOME" = "/" ]; then
    echo "==  check_buildhost_paths: \$HOME unset/root; skipping (WARN)" >&2
    exit 0
fi

leaks=0
scanned=0

report() {  # $1=file (rel)  $2=hits
    echo "  LEAK: $1" >&2
    printf '%s\n' "$2" | sed 's/^/         /' >&2
    leaks=$((leaks + 1))
}

echo "==> scanning shipped binaries + scripts for build-host home paths under $INSTALL_DIR"

# Every Mach-O under libexec/, bin/ and lib/ (RECURSIVELY -- nested loadable
# modules like the gdk-pixbuf loaders live in lib/gdk-pixbuf-2.0/.../loaders/).
# Scan RAW BYTES (tr non-printable -> newlines, then grep) rather than strings(1):
# strings misses debug-map (N_OSO) stabs and can split short runs, so it would
# not catch a builder path in the symbol table. This catches install-names, data
# strings, rpaths and debug symbols alike.
while IFS= read -r f; do
    [ -f "$f" ] || continue
    file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
    scanned=$((scanned + 1))
    hits="$(tr -c '[:print:]' '\n' < "$f" 2>/dev/null | grep -F "$BUILDER_HOME/" | sort -u | head -5)"
    [ -n "$hits" ] && report "${f#$INSTALL_DIR/}" "$hits"
done < <(
    find "$INSTALL_DIR/libexec" "$INSTALL_DIR/bin" -type f 2>/dev/null
    find "$INSTALL_DIR/lib" -type f \( -name '*.dylib' -o -name '*.so' \) 2>/dev/null
)

# Shipped shell scripts (the wrapper, setup helpers).
while IFS= read -r f; do
    case "$f" in */loaders.cache) continue ;; esac
    scanned=$((scanned + 1))
    hits="$(grep -aF "$BUILDER_HOME/" "$f" 2>/dev/null | sort -u | head -5)"
    [ -n "$hits" ] && report "${f#$INSTALL_DIR/}" "$hits"
done < <(
    find "$INSTALL_DIR" -maxdepth 1 -name '*.sh' 2>/dev/null
    find "$INSTALL_DIR/bin" -type f 2>/dev/null
)

if [ "$leaks" -ne 0 ]; then
    echo "!! check_buildhost_paths: FAIL — $leaks shipped file(s) embed a builder home path." >&2
    echo "   Rebuild clean (BANDICOOT_RELEASE=1 forces 'make clean' + a generic" >&2
    echo "   --prefix); a lingering /Users/<name> path means a stale object survived." >&2
    exit 1
fi
echo "ok  check_buildhost_paths: no builder home paths in $scanned shipped binaries/scripts"

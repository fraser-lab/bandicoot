#!/bin/bash
# Bandicoot: pre-ship dependency-closure sanity check for an installed tree.
#
#   check_install.sh <INSTALL_DIR>
#
# Walks every Mach-O in the install (bin/, libexec/, lib/**, and crucially
# the Python C extensions under lib/python3.X/**/*.so) and verifies that
# every LC_LOAD_DYLIB dependency actually RESOLVES inside the shipped tree.
#
# This closes the gap that shipped v0.1.4.2..v0.1.4.8 with a broken _ssl:
# codesign-install.sh's final `codesign --verify` proves each Mach-O's
# SIGNATURE is valid, but a library can be perfectly signed and still name a
# dependency that does not exist in the bundle (@rpath/libssl.3.dylib with no
# libssl in lib/). That only fails at dlopen -- i.e. when a user finally
# imports ssl -- so it escaped every build-time gate. This script fails the
# build instead.
#
# Two failure classes are reported:
#   1. UNRESOLVED  -- an @rpath/@loader_path/@executable_path (or absolute)
#                     dependency that resolves to no file in the tree. dyld
#                     would fail to load it at runtime. This is the _ssl class.
#   2. HOST-LEAK   -- an absolute dependency into a BUILD-only prefix (conda,
#                     anaconda, canvas-deps). Those exist on the build host but
#                     never on a user's machine, so they must not ship. The
#                     whole point of bundle_conda_deps.sh is to eliminate them.
#
# NOT flagged (by design, per INSTALL.md's dependency model):
#   * /usr/lib and /System        -- served from the dyld shared cache.
#   * /opt/homebrew/...           -- Bandicoot's binary distribution REQUIRES
#                                    Homebrew at /opt/homebrew with a documented
#                                    formula set (gtk+ gtkglext freeglut gsl
#                                    cairo libpng sqlite bzip2 boost). These are
#                                    the sanctioned runtime prereq, not leaks.
#                                    Reported as an INFO count only.
#   * /DLC/... and <dir>/.dylibs/ -- delocate sentinels in vendored PyPI wheels
#                                    (Pillow et al.); resolved relative to the
#                                    loader at runtime.
#
# Exit status: non-zero if any UNRESOLVED or HOST-LEAK finding (unless
# --warn-unresolved is passed, which downgrades UNRESOLVED to a warning and
# fails only on HOST-LEAK -- useful while a backlog of unbundled optional
# stdlib extensions is worked through).
#
# Read-only; never edits or signs anything.

set -u

WARN_UNRESOLVED=0
INSTALL_DIR=""
for a in "$@"; do
    case "$a" in
        --warn-unresolved) WARN_UNRESOLVED=1 ;;
        *) INSTALL_DIR="$a" ;;
    esac
done
if [ -z "$INSTALL_DIR" ] || [ ! -d "$INSTALL_DIR" ]; then
    echo "!! check_install.sh: usage: check_install.sh [--warn-unresolved] <INSTALL_DIR>" >&2
    exit 2
fi
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd)"
LIBDIR="$INSTALL_DIR/lib"
EXEDIR="$INSTALL_DIR/bin"          # @executable_path anchor (Bandicoot exe lives here)

# BUILD-only prefixes that must never appear as a shipped dependency path.
# (Homebrew /opt/homebrew is deliberately NOT here -- it's the documented
# runtime prerequisite, see the header.)
HOST_PREFIXES=(
    /opt/miniconda3 /opt/anaconda3 /usr/local/Cellar
    "$HOME/miniconda3" "$HOME/anaconda3" "$HOME/sw/canvas-deps"
)

unresolved=0
hostleak=0
brewrefs=0
scanned=0

# Print the LC_RPATH search paths of a Mach-O, with @loader_path expanded to
# the file's own directory and @executable_path to the install bin dir.
rpaths_of() {
    local f="$1" d
    d="$(cd "$(dirname "$f")" && pwd)"
    otool -l "$f" 2>/dev/null \
      | awk '/LC_RPATH/{i=1;next} i&&/path /{print $2;i=0}' \
      | while IFS= read -r rp; do
            rp="${rp//@loader_path/$d}"
            rp="${rp//@executable_path/$EXEDIR}"
            printf '%s\n' "$rp"
        done
}

# Does dependency $1 (as recorded in $2's load commands) resolve to a real file?
dep_resolves() {
    local dep="$1" f="$2" d rel cand rp
    d="$(cd "$(dirname "$f")" && pwd)"
    case "$dep" in
        /usr/lib/*|/System/*)            return 0 ;;                 # dyld cache
        /opt/homebrew/*)                 return 0 ;;                 # documented prereq
        /DLC/*)                                                      # delocate sentinel
            rel="${dep##*/}"
            [ -e "$d/.dylibs/$rel" ] && return 0
            [ -e "$d/$rel" ] && return 0
            return 1 ;;
        @rpath/*)
            rel="${dep#@rpath/}"
            # Flat-bundle fast path: everything lands in lib/.
            [ -e "$LIBDIR/$rel" ] && return 0
            while IFS= read -r rp; do
                [ -n "$rp" ] && [ -e "$rp/$rel" ] && return 0
            done < <(rpaths_of "$f")
            return 1 ;;
        @loader_path/*)  cand="${dep/@loader_path/$d}";      [ -e "$cand" ] && return 0; return 1 ;;
        @executable_path/*) cand="${dep/@executable_path/$EXEDIR}"; [ -e "$cand" ] && return 0; return 1 ;;
        /*)              [ -e "$dep" ] && return 0; return 1 ;;      # absolute
        *)               return 1 ;;                                 # relative/odd: treat as unresolved
    esac
}

is_host_leak() {
    local dep="$1" p
    for p in "${HOST_PREFIXES[@]}"; do
        case "$dep" in "$p"/*) return 0 ;; esac
    done
    return 1
}

echo "==> checking dependency closure of $INSTALL_DIR"

while IFS= read -r f; do
    file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
    scanned=$((scanned + 1))
    # otool -L: skip line 1 (the file's own install-id), take the dep path col.
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        if is_host_leak "$dep"; then
            echo "  HOST-LEAK  ${f#$INSTALL_DIR/}  ->  $dep"
            hostleak=$((hostleak + 1))
        elif case "$dep" in /opt/homebrew/*) true ;; *) false ;; esac; then
            brewrefs=$((brewrefs + 1))          # documented prereq; count only
        elif ! dep_resolves "$dep" "$f"; then
            echo "  UNRESOLVED ${f#$INSTALL_DIR/}  ->  $dep"
            unresolved=$((unresolved + 1))
        fi
    done < <(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}')
done < <(find "$INSTALL_DIR" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) 2>/dev/null)

echo "==> scanned $scanned Mach-O files ($brewrefs homebrew-prereq refs, not flagged)"

fail=0
[ "$hostleak" -ne 0 ] && fail=1
if [ "$unresolved" -ne 0 ] && [ "$WARN_UNRESOLVED" -eq 0 ]; then fail=1; fi

if [ "$fail" -ne 0 ]; then
    echo "!! check_install: FAIL — $hostleak build-host leak(s), $unresolved unresolved dep(s)" >&2
    exit 1
fi
if [ "$unresolved" -ne 0 ]; then
    echo "!! check_install: WARN — $unresolved unresolved dep(s) (downgraded by --warn-unresolved)" >&2
    exit 0
fi
echo "ok  check_install: no build-host leaks, all critical deps resolve"

#!/bin/bash
# ---------------------------------------------------------------------------
# INVOKED AUTOMATICALLY BY scripts/build.sh -- you do NOT need to run this by
# hand. (GitHub #24: a builder read these headers cold, concluded they were a
# manual sequence, and ran them individually -- which is what kept
# re-introducing a stale rpath.)
# ---------------------------------------------------------------------------
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
#   2. HOST-LEAK   -- an absolute dependency into a BUILD-only prefix (Homebrew,
#                     conda, anaconda, canvas-deps). Those exist on the build
#                     host but never on a user's machine, so they must not ship.
#                     bundle_homebrew_deps.sh + bundle_conda_deps.sh exist to
#                     eliminate them. Since v0.1.4.10 Homebrew is bundled too,
#                     so /opt/homebrew is a hard leak like conda (Bandicoot no
#                     longer requires Homebrew at runtime).
#
# NOT flagged (by design, per INSTALL.md's dependency model):
#   * /usr/lib and /System        -- served from the dyld shared cache.
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
# Homebrew is now bundled (bundle_homebrew_deps.sh), so /opt/homebrew is a hard
# leak like conda -- the shipped install must not depend on Homebrew at runtime.
#
# NOTE: this list is a fast path for naming the usual suspects in the output. It
# is NOT the actual test -- see is_host_leak below. A hard-coded list goes stale
# the moment a dependency tree moves: it named "$HOME/sw/canvas-deps" while the
# canvas stack lived there, and when that tree moved to <repo>/deps/canvas in
# v0.1.4.13 the checker went blind to precisely the path that had just been
# introduced. Five binaries shipped pointing at the build tree, loading a second
# complete GTK stack, and this script reported "no build-host leaks".
HOST_PREFIXES=(
    /opt/homebrew /opt/miniconda3 /opt/anaconda3 /usr/local/Cellar
    "$HOME/miniconda3" "$HOME/anaconda3" "$HOME/sw/canvas-deps"
)

# Absolute paths a shipped Mach-O is ALLOWED to name. Anything else absolute is
# a leak by definition: a self-contained install may reference only the OS and
# itself. This is an allowlist precisely so that a new build-tree location is
# caught by default rather than needing to be added to a denylist first.
ALLOWED_ABS_PREFIXES=(
    /usr/lib /System /Library/Apple
)

unresolved=0
hostleak=0
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

# The dependency paths a Mach-O actually records, i.e. LC_LOAD_DYLIB and its
# variants -- NOT the file's own LC_ID_DYLIB.
#
# v0.1.4.11: this replaces `otool -L | tail -n +2`. otool -L prints the
# "<file>:" header on line 1 and, for a dylib, its own install-name on line 2,
# so `tail -n +2` dropped the header but kept the ID and reported it as a
# dependency. That was a latent false positive for any bundled dylib whose ID
# basename is not itself present in lib/ -- e.g.
# lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader_svg.dylib (id
# @rpath/libpixbufloader_svg.dylib, but it lives in loaders/, not lib/), which
# gets reported UNRESOLVED though dyld never looks it up. Harmless while
# UNRESOLVED was only a warning; it would block arming the gate. Parsing the
# load commands directly is exact.
deps_of() {
    otool -l "$1" 2>/dev/null | awk '
        /^ *cmd LC_(LOAD_DYLIB|LOAD_WEAK_DYLIB|REEXPORT_DYLIB|LOAD_UPWARD_DYLIB)$/ { i = 1; next }
        i && /^ *name / { print $2; i = 0 }
    '
}

is_host_leak() {
    local dep="$1" p

    # Named suspects first, so the message can be specific.
    for p in "${HOST_PREFIXES[@]}"; do
        case "$dep" in "$p"/*) return 0 ;; esac
    done

    # Relative / dyld-relative forms are fine -- they are what we want.
    case "$dep" in
        @*|"") return 1 ;;
        /*)    ;;
        *)     return 1 ;;
    esac

    # Absolute: allowed only if it is the OS, or the install itself.
    case "$dep" in "$INSTALL_DIR"/*) return 1 ;; esac
    for p in "${ALLOWED_ABS_PREFIXES[@]}"; do
        case "$dep" in "$p"/*) return 1 ;; esac
    done
    return 0
}

echo "==> checking dependency closure of $INSTALL_DIR"

while IFS= read -r f; do
    file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
    scanned=$((scanned + 1))
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        if is_host_leak "$dep"; then
            echo "  HOST-LEAK  ${f#$INSTALL_DIR/}  ->  $dep"
            hostleak=$((hostleak + 1))
        elif ! dep_resolves "$dep" "$f"; then
            echo "  UNRESOLVED ${f#$INSTALL_DIR/}  ->  $dep"
            unresolved=$((unresolved + 1))
        fi
    done < <(deps_of "$f")

    # v0.1.4.13: ALSO flag LC_RPATH entries pointing into a build-only prefix.
    # Until now only dependencies were checked, so a shipped Mach-O could carry
    # e.g. an LC_RPATH of /opt/homebrew/Cellar/jpeg-turbo/<ver>/lib and pass
    # cleanly. That is a real leak: it makes dyld search the builder's Homebrew
    # first for any @rpath dependency, so on a machine that happens to have a
    # different version there the app silently binds to it -- the same class of
    # failure as GitHub #8, arriving by a different route.
    while IFS= read -r rp; do
        [ -n "$rp" ] || continue
        if is_host_leak "$rp"; then
            echo "  HOST-RPATH ${f#$INSTALL_DIR/}  ->  $rp"
            hostleak=$((hostleak + 1))
        fi
    done < <(rpaths_of "$f")
done < <(find "$INSTALL_DIR" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) 2>/dev/null)

echo "==> scanned $scanned Mach-O files"

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

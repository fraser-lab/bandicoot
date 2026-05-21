#!/bin/bash
# Make a Bandicoot install tree relocatable by rewriting absolute
# Mach-O paths to @rpath / @executable_path form. Run once after each
# `make install`; build.sh does this automatically.
#
# After running, the install can be moved or extracted anywhere -- the
# dynamic loader will resolve sibling dylibs via @executable_path
# relative to the binary location.
#
# Dependencies from external prefixes (e.g. /opt/homebrew, /opt/miniconda3)
# are NOT rewritten; those are runtime requirements the user must have
# installed at the same canonical paths.
#
# Usage:
#   ./scripts/make_relocatable.sh /path/to/install
set -e

if [ -z "$1" ]; then
    echo "Usage: $(basename "$0") <install-prefix>" >&2
    exit 1
fi

PREFIX="$(cd "$1" && pwd)"

# Resolve a few possible originating prefixes; only paths starting with
# the actual current prefix get rewritten (so this is also safe to run on
# an install that's already been relocated).
LIB_PREFIX="$PREFIX/lib"

is_macho() {
    file -b "$1" 2>/dev/null | grep -q "Mach-O"
}

# A dylib in our own lib/ gets its install_name set to @rpath/<basename>.
process_dylib_install_name() {
    local DYLIB="$1"
    local CUR
    CUR="$(otool -D "$DYLIB" 2>/dev/null | tail -n +2 | head -1)"
    case "$CUR" in
        @rpath/*) ;;  # already relocatable
        "$PREFIX"/*|*"$LIB_PREFIX"/*)
            install_name_tool -id "@rpath/$(basename "$DYLIB")" "$DYLIB" 2>/dev/null || true
            ;;
    esac
}

# Rewrite each LC_LOAD_DYLIB reference that points into our own lib/ to
# @rpath/<basename>. External refs (homebrew, miniconda) are left alone.
rewrite_dep_loads() {
    local FILE="$1"
    otool -L "$FILE" 2>/dev/null | tail -n +2 | awk '{print $1}' | while read -r DEP; do
        case "$DEP" in
            "$LIB_PREFIX"/*)
                install_name_tool -change "$DEP" "@rpath/$(basename "$DEP")" "$FILE" 2>/dev/null || true
                ;;
        esac
    done
}

# Replace any rpath entry pointing at our own lib/ with
# @executable_path/../lib (and @loader_path/../lib for dylibs in lib/
# that need to find siblings).
rewrite_rpaths() {
    local FILE="$1"
    local NEW_RPATH="$2"
    otool -l "$FILE" 2>/dev/null | awk '
        /LC_RPATH/        {in_rpath=1; next}
        in_rpath && /path / {print $2; in_rpath=0}
    ' | while read -r RP; do
        case "$RP" in
            "$LIB_PREFIX"|"$LIB_PREFIX"/*)
                install_name_tool -rpath "$RP" "$NEW_RPATH" "$FILE" 2>/dev/null || true
                ;;
        esac
    done
}

# Add an rpath if it isn't already there.
add_rpath_if_missing() {
    local FILE="$1"
    local RP="$2"
    if ! otool -l "$FILE" 2>/dev/null | grep -q "path $RP "; then
        install_name_tool -add_rpath "$RP" "$FILE" 2>/dev/null || true
    fi
}

COUNT=0

# Dylibs: set install_name to @rpath/<basename>, rewrite deps, replace
# absolute rpath with @loader_path/../lib so a dylib in lib/ can find
# siblings.
if [ -d "$LIB_PREFIX" ]; then
    while IFS= read -r DYLIB; do
        is_macho "$DYLIB" || continue
        chmod u+w "$DYLIB"
        process_dylib_install_name "$DYLIB"
        rewrite_dep_loads "$DYLIB"
        rewrite_rpaths "$DYLIB" "@loader_path"
        add_rpath_if_missing "$DYLIB" "@loader_path"
        COUNT=$((COUNT + 1))
    done < <(find "$LIB_PREFIX" -name "*.dylib" -type f)
fi

# Executables in libexec/ and bin/: rewrite deps, replace absolute
# rpath with @executable_path/../lib.
for d in libexec bin; do
    [ -d "$PREFIX/$d" ] || continue
    while IFS= read -r BIN; do
        is_macho "$BIN" || continue
        chmod u+w "$BIN"
        rewrite_dep_loads "$BIN"
        rewrite_rpaths "$BIN" "@executable_path/../lib"
        add_rpath_if_missing "$BIN" "@executable_path/../lib"
        COUNT=$((COUNT + 1))
    done < <(find "$PREFIX/$d" -type f -perm -u+x)
done

echo "make_relocatable: processed $COUNT Mach-O files in $PREFIX"

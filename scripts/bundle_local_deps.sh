#!/bin/bash
# Bundle the libraries built by scripts/build_deps.sh (canvas stack, FFTW 2) and
# rewrite every reference to them from the BUILD-TREE path to @rpath.
#
# WHY THIS EXISTS
# ---------------
# These libraries are configured with --prefix=<repo>/deps/..., so their
# LC_ID_DYLIB is that absolute build path and every consumer records it. libtool
# copies the dylibs into <install>/lib during `make install`, but it does NOT
# rewrite the consumers -- so the install ends up with BOTH:
#
#   lib/libgnomecanvas-2.0.dylib                         (bundled, @rpath)
#   libexec/Bandicoot -> /Users/.../deps/canvas/lib/...  (build tree, absolute)
#
# dyld loads both. Worse, the build-tree copies link Homebrew's GTK by absolute
# path, so loading them drags in a SECOND complete GTK/GDK/GIO/pango/cairo stack
# and macOS prints a screenful of:
#
#   objc[...]: Class GdkQuartzView is implemented in both
#   /opt/homebrew/.../libgdk-quartz-2.0.0.dylib and <install>/lib/...
#   This may cause spurious casting failures and mysterious crashes.
#
# It also means the "self-contained" install silently depends on the build tree
# still existing at its original path -- delete the repo and the app breaks.
#
# Must run AFTER make_relocatable.sh (which handles the install and compile
# prefixes) and BEFORE bundle_homebrew_deps.sh (so the Homebrew dependencies of
# whatever we bundle here get closed over in the same pass).
#
# Usage:
#   ./scripts/bundle_local_deps.sh <install-prefix> <dep-prefix> [dep-prefix...]

set -e

PREFIX="${1:?Usage: $0 <install-prefix> <dep-prefix>...}"
PREFIX="$(cd "$PREFIX" && pwd)"
shift

LIBDIR="$PREFIX/lib"
[ -d "$LIBDIR" ] || { echo "error: $LIBDIR missing" >&2; exit 1; }

# Normalise the dep prefixes we are responsible for; skip any that don't exist.
DEP_PREFIXES=()
for d in "$@"; do
    [ -n "$d" ] || continue
    [ -d "$d" ] || continue
    DEP_PREFIXES+=("$(cd "$d" && pwd)")
done
[ "${#DEP_PREFIXES[@]}" -gt 0 ] || { echo "==> bundle_local_deps: no dep prefixes present, nothing to do"; exit 0; }

echo "==> bundling local dep libraries into $LIBDIR"
for d in "${DEP_PREFIXES[@]}"; do echo "    dep prefix: $d"; done

is_local_dep() {
    local p="$1" d
    for d in "${DEP_PREFIXES[@]}"; do
        case "$p" in "$d"/*) return 0 ;; esac
    done
    return 1
}

copied=0
rewritten=0

# --- pass 1: make sure every referenced local-dep library is in lib/ ---------
while IFS= read -r f; do
    file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        is_local_dep "$dep" || continue
        base="$(basename "$dep")"
        if [ ! -f "$LIBDIR/$base" ]; then
            if [ -f "$dep" ]; then
                cp -f "$dep" "$LIBDIR/$base"
                chmod u+w "$LIBDIR/$base"
                install_name_tool -id "@rpath/$base" "$LIBDIR/$base" 2>/dev/null || true
                # Let it find its siblings from its own directory.
                install_name_tool -add_rpath "@loader_path" "$LIBDIR/$base" 2>/dev/null || true
                echo "    copied $base"
                copied=$((copied + 1))
            else
                echo "    !! referenced but missing: $dep" >&2
            fi
        fi
    done < <(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}')
done < <(find "$PREFIX" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) 2>/dev/null)

# --- pass 2: rewrite every build-tree reference to @rpath --------------------
while IFS= read -r f; do
    file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
    touched=0
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        is_local_dep "$dep" || continue
        base="$(basename "$dep")"
        chmod u+w "$f" 2>/dev/null || true
        if install_name_tool -change "$dep" "@rpath/$base" "$f" 2>/dev/null; then
            touched=1
        fi
    done < <(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}')

    # Drop LC_RPATHs pointing into the build tree -- otherwise @rpath could
    # still resolve back there in preference to the bundled copy.
    while IFS= read -r rp; do
        [ -n "$rp" ] || continue
        if is_local_dep "$rp/x" || is_local_dep "$rp"; then
            install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null || true
            touched=1
        fi
    done < <(otool -l "$f" 2>/dev/null | awk '/LC_RPATH/{i=1;next} i&&/path /{print $2;i=0}')

    [ "$touched" = "1" ] && rewritten=$((rewritten + 1))
done < <(find "$PREFIX" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) 2>/dev/null)

echo "    copied $copied librar(y/ies), rewrote $rewritten Mach-O file(s)"

# --- verify -----------------------------------------------------------------
leaks=0
while IFS= read -r f; do
    file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        if is_local_dep "$dep"; then
            echo "  LEAK ${f#$PREFIX/} -> $dep" >&2
            leaks=$((leaks + 1))
        fi
    done < <(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}')
done < <(find "$PREFIX" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) 2>/dev/null)

if [ "$leaks" -ne 0 ]; then
    echo "!! bundle_local_deps: $leaks unrewritten build-tree reference(s)" >&2
    exit 1
fi

echo "==> bundle_local_deps: done"

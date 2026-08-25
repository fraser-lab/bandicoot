#!/bin/bash
# ---------------------------------------------------------------------------
# INVOKED AUTOMATICALLY BY scripts/build.sh -- you do NOT need to run this by
# hand. (GitHub #24: a builder read these headers cold, concluded they were a
# manual sequence, and ran them individually -- which is what kept
# re-introducing a stale rpath.)
# ---------------------------------------------------------------------------
# Copy Homebrew's GTK2 input-method modules into bandicoot's lib/ and repoint
# them at the bundled GTK, so the app never dlopens anything out of /opt/homebrew.
#
# WHY THIS EXISTS
# ---------------
# The GTK2 stack we bundle IS Homebrew's build, so it carries Homebrew's
# compile-time prefix inside it:
#
#   $ strings lib/libgtk-quartz-2.0.0.dylib | grep /opt/homebrew
#   /opt/homebrew/Cellar/gtk+/2.24.33_2/lib     <- where it looks for modules
#   /opt/homebrew/Cellar/gtk+/2.24.33_2/etc     <- where it looks for gtk.immodules
#
# At startup the BUNDLED libgtk scans that Homebrew directory for input-method
# modules and dlopens im-quartz.so, which links Homebrew's libraries by ABSOLUTE
# path (/opt/homebrew/.../libgtk-quartz-2.0.0.dylib et al). The result is two
# copies of gtk/gdk/gio in one process and a screenful of:
#
#   objc[...]: Class GdkQuartzView is implemented in both
#   /opt/homebrew/.../libgdk-quartz-2.0.0.dylib and <install>/lib/...
#   This may cause spurious casting failures and mysterious crashes.
#
# This only bites on a machine that HAS Homebrew's gtk+2 -- i.e. developers. On a
# user's Mac the directory does not exist, the dlopen quietly fails and GTK falls
# back to its built-in simple input method. So it was invisible in the field, but
# it means the app is not actually isolated wherever Homebrew is present, and the
# duplicate-class condition is a real (if rare) crash risk, not just noise.
#
# The fix mirrors what bundle_pixbuf_loaders.sh already does for the image
# loaders: ship the modules, rewrite their deps to @rpath, and have coot.in point
# GTK_IM_MODULE_FILE / GTK_PATH at the bundled copies. GIO_MODULE_DIR is set in
# coot.in for the same reason -- libgio scans $BREW/lib/gio/modules, which is
# empty on most Macs but is populated by glib-networking or dconf.
#
# Usage:
#   ./scripts/bundle_gtk_immodules.sh /path/to/install [/path/to/brew]
#
# Idempotent. The gtk.immodules cache is NOT written here -- build.sh seeds one
# for the local tree and setup.sh regenerates it with the user's real paths,
# exactly as with the pixbuf loaders.cache.

set -e

PREFIX="${1:?Usage: $0 <install-prefix> [brew-prefix]}"
PREFIX="$(cd "$PREFIX" && pwd)"
BREW_PREFIX_ARG="${2:-${BREW_PREFIX:-/opt/homebrew}}"

[ -d "$PREFIX/lib" ] || { echo "error: $PREFIX/lib missing" >&2; exit 1; }

IM_SRC_DIR="$BREW_PREFIX_ARG/lib/gtk-2.0/2.10.0/immodules"
if [ ! -d "$IM_SRC_DIR" ]; then
    echo "error: Homebrew GTK2 immodules not at $IM_SRC_DIR" >&2
    echo "       (brew install gtk+)" >&2
    exit 1
fi

DEST_DIR="$PREFIX/lib/gtk-2.0/2.10.0/immodules"
mkdir -p "$DEST_DIR"

echo "==> bundling GTK2 immodules into $DEST_DIR"

# Rewrite every absolute Homebrew dependency of a Mach-O to @rpath/<basename>,
# then add the rpath that resolves @rpath back to <install>/lib. The modules sit
# three levels below lib/ (lib/gtk-2.0/2.10.0/immodules/), hence ../../.. .
repoint() {
    local f="$1"
    chmod u+w "$f"
    install_name_tool -id "@rpath/$(basename "$f")" "$f" 2>/dev/null || true
    local dep
    while IFS= read -r dep; do
        case "$dep" in
            "$BREW_PREFIX_ARG"/*)
                install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$f" 2>/dev/null || true
                ;;
        esac
    done < <(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}')
    install_name_tool -add_rpath "@loader_path/../../.." "$f" 2>/dev/null || true
}

n=0
for m in "$IM_SRC_DIR"/*.so; do
    [ -f "$m" ] || continue
    cp -f "$m" "$DEST_DIR/"
    repoint "$DEST_DIR/$(basename "$m")"
    n=$((n + 1))
done
echo "    bundled $n immodule(s)"

# Bundle gtk-query-immodules-2.0 so setup.sh can regenerate the cache on the
# user's machine (the cache stores absolute paths, so it cannot be shipped
# as-is). Same treatment as libexec/gdk-pixbuf-query-loaders.
QUERY_SRC="$BREW_PREFIX_ARG/bin/gtk-query-immodules-2.0"
if [ -x "$QUERY_SRC" ]; then
    mkdir -p "$PREFIX/libexec"
    cp -f "$QUERY_SRC" "$PREFIX/libexec/gtk-query-immodules-2.0"
    chmod u+w "$PREFIX/libexec/gtk-query-immodules-2.0"
    dep=""
    while IFS= read -r dep; do
        case "$dep" in
            "$BREW_PREFIX_ARG"/*)
                install_name_tool -change "$dep" "@rpath/$(basename "$dep")" \
                    "$PREFIX/libexec/gtk-query-immodules-2.0" 2>/dev/null || true
                ;;
        esac
    done < <(otool -L "$PREFIX/libexec/gtk-query-immodules-2.0" 2>/dev/null | tail -n +2 | awk '{print $1}')
    install_name_tool -add_rpath "@executable_path/../lib" \
        "$PREFIX/libexec/gtk-query-immodules-2.0" 2>/dev/null || true
    echo "    bundled gtk-query-immodules-2.0"
else
    echo "    !! warning: $QUERY_SRC not found; setup.sh cannot regenerate the cache" >&2
fi

echo "==> bundle_gtk_immodules: done"

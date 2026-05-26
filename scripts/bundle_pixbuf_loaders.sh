#!/bin/bash
# Copy Homebrew's gdk-pixbuf image loaders (PNG/JPEG/SVG/...) into
# bandicoot's lib/ so the install can render every icon Coot ships,
# independent of whether the user has librsvg installed via Homebrew.
#
# Without this, GTK falls back to whatever pixbuf loaders the user's
# /opt/homebrew installation happens to have. gdk-pixbuf itself ships
# the raster loaders (PNG/JPEG/BMP/...), but SVG support lives in the
# separate librsvg package — easily missed when installing Bandicoot
# prerequisites. Coot's sidebar uses .svg icons (water-drop.svg etc.),
# so testers without librsvg saw blank icon slots.
#
# What we bundle:
#   <install>/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-*.so
#   <install>/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader_svg.dylib
#   <install>/lib/librsvg-2.2.dylib
#
# The loaders.cache file is NOT generated here; setup.sh runs
# gdk-pixbuf-query-loaders at install time to write a cache with paths
# absolute to the user's extracted install location. coot.in exports
# GDK_PIXBUF_MODULEDIR / GDK_PIXBUF_MODULE_FILE so GTK uses the bundled
# loaders+cache instead of the system ones.
#
# Usage:
#   ./scripts/bundle_pixbuf_loaders.sh /path/to/install [/path/to/brew]
#
# Defaults: BREW_PREFIX env, else /opt/homebrew.

set -e

PREFIX="${1:?Usage: $0 <install-prefix> [brew-prefix]}"
PREFIX="$(cd "$PREFIX" && pwd)"
BREW_PREFIX_ARG="${2:-${BREW_PREFIX:-/opt/homebrew}}"

[ -d "$PREFIX/lib" ] || { echo "error: $PREFIX/lib missing" >&2; exit 1; }
[ -d "$BREW_PREFIX_ARG" ] || { echo "error: $BREW_PREFIX_ARG missing" >&2; exit 1; }

PIXBUF_SRC_DIR="$BREW_PREFIX_ARG/lib/gdk-pixbuf-2.0/2.10.0/loaders"
if [ ! -d "$PIXBUF_SRC_DIR" ]; then
    echo "error: Homebrew gdk-pixbuf loaders not at $PIXBUF_SRC_DIR" >&2
    echo "       (brew install gtk+ pulls gdk-pixbuf as a dep)" >&2
    exit 1
fi

# Find librsvg's loaders dir under Homebrew Cellar. We resolve via the
# `opt/librsvg/` symlink to handle version bumps without code changes.
LIBRSVG_OPT="$BREW_PREFIX_ARG/opt/librsvg"
if [ ! -d "$LIBRSVG_OPT" ]; then
    echo "error: librsvg not installed at $LIBRSVG_OPT" >&2
    echo "       (brew install librsvg)" >&2
    exit 1
fi
SVG_LOADER_SRC="$LIBRSVG_OPT/lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader_svg.dylib"
LIBRSVG_DYLIB_SRC="$LIBRSVG_OPT/lib/librsvg-2.2.dylib"
[ -f "$SVG_LOADER_SRC" ]   || { echo "error: missing $SVG_LOADER_SRC" >&2; exit 1; }
[ -f "$LIBRSVG_DYLIB_SRC" ] || { echo "error: missing $LIBRSVG_DYLIB_SRC" >&2; exit 1; }

DEST_LOADERS_DIR="$PREFIX/lib/gdk-pixbuf-2.0/2.10.0/loaders"
mkdir -p "$DEST_LOADERS_DIR"

echo "==> bundling gdk-pixbuf loaders into $DEST_LOADERS_DIR"

# Copy every raster loader Homebrew's gdk-pixbuf ships (PNG/JPEG/BMP/etc.)
for loader in "$PIXBUF_SRC_DIR"/libpixbufloader-*.so; do
    [ -f "$loader" ] || continue
    cp -f "$loader" "$DEST_LOADERS_DIR/"
    chmod u+w "$DEST_LOADERS_DIR/$(basename "$loader")"
    echo "    copied $(basename "$loader")"
done

# Copy the SVG loader (.dylib form — that's what query-loaders picks up)
cp -f "$SVG_LOADER_SRC" "$DEST_LOADERS_DIR/"
chmod u+w "$DEST_LOADERS_DIR/$(basename "$SVG_LOADER_SRC")"
echo "    copied $(basename "$SVG_LOADER_SRC")"

echo "==> bundling librsvg-2.2.dylib into $PREFIX/lib/"
cp -f "$LIBRSVG_DYLIB_SRC" "$PREFIX/lib/"
chmod u+w "$PREFIX/lib/$(basename "$LIBRSVG_DYLIB_SRC")"

echo "==> rewriting install_names"

# librsvg-2.2.dylib: set its own id to @rpath/<basename>
install_name_tool -id "@rpath/librsvg-2.2.dylib" \
    "$PREFIX/lib/librsvg-2.2.dylib" 2>/dev/null || true

# libintl is bundled by bundle_conda_deps.sh. Repoint librsvg's
# absolute /opt/homebrew/opt/gettext reference at our bundled copy so
# the loader doesn't blow up on a tester whose Homebrew gettext lives
# at a different prefix.
LIBINTL_FROM="$BREW_PREFIX_ARG/opt/gettext/lib/libintl.8.dylib"
LIBINTL_TO="@rpath/libintl.8.dylib"
install_name_tool -change "$LIBINTL_FROM" "$LIBINTL_TO" \
    "$PREFIX/lib/librsvg-2.2.dylib" 2>/dev/null || true

# libpixbufloader_svg.dylib: rewrite its self-id (it ships with a
# bizarre /opt/homebrew/opt/librsvg/.../libpixbufloader_svg.dylib id
# rather than @rpath), rewrite librsvg-2.2 to @rpath, rewrite libintl.
SVG_LOADER_DST="$DEST_LOADERS_DIR/libpixbufloader_svg.dylib"
install_name_tool -id "$SVG_LOADER_DST" "$SVG_LOADER_DST" 2>/dev/null || true
install_name_tool -change "@rpath/librsvg-2.2.dylib" \
                          "@rpath/librsvg-2.2.dylib" \
                          "$SVG_LOADER_DST" 2>/dev/null || true
# Some Homebrew builds record the absolute opt-path for librsvg-2.2
# instead of @rpath. Cover that form too.
install_name_tool -change "$LIBRSVG_OPT/lib/librsvg-2.2.dylib" \
                          "@rpath/librsvg-2.2.dylib" \
                          "$SVG_LOADER_DST" 2>/dev/null || true
install_name_tool -change "$LIBINTL_FROM" "$LIBINTL_TO" \
    "$SVG_LOADER_DST" 2>/dev/null || true

# librsvg pulls in libiconv, which conda-bundle-deps also ships as
# libiconv.2.dylib at @rpath. Homebrew librsvg links /usr/lib/libiconv,
# which on macOS is the BSD libiconv (no _libiconv symbol). If the
# loader records /usr/lib/libiconv.2.dylib we leave it — that's the
# stable system call. Only the bundled conda libintl needs GNU iconv;
# librsvg itself is happy with the BSD form.

# Add an rpath to the SVG loader so @rpath/librsvg-2.2.dylib resolves
# back to <install>/lib/. The loader is two directories deeper than
# the dylibs (lib/gdk-pixbuf-2.0/2.10.0/loaders/), so the relative
# climb is ../../../ from @loader_path.
install_name_tool -add_rpath "@loader_path/../../.." \
    "$SVG_LOADER_DST" 2>/dev/null || true

echo "==> bundle_pixbuf_loaders: done"

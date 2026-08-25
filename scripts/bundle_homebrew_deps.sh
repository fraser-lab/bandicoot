#!/bin/bash
# ---------------------------------------------------------------------------
# INVOKED AUTOMATICALLY BY scripts/build.sh -- you do NOT need to run this by
# hand. (GitHub #24: a builder read these headers cold, concluded they were a
# manual sequence, and ran them individually -- which is what kept
# re-introducing a stale rpath.)
# ---------------------------------------------------------------------------
# Bundle the Homebrew-provided runtime libraries into bandicoot's lib/ and
# rewrite every /opt/homebrew reference to @rpath/<basename>, so the install
# no longer depends on Homebrew at runtime. This removes the LAST external
# runtime prerequisite: after this the tarball runs on a Mac with no
# /opt/homebrew at all (see also bundle_conda_deps.sh, which did the same for
# the conda/crystallography stack).
#
# The bundled set is the GTK2-Quartz stack and its transitive dependencies:
# gtk+ / gdk, glib (glib/gobject/gio/gmodule), pango (+cairo/ft2), cairo,
# gdk-pixbuf, atk, gailutil, harfbuzz, fontconfig, freetype, pixman, fribidi,
# libthai, libdatrie, graphite2, pcre2, libpng, jpeg-turbo, libtiff, gettext,
# gsl/gslcblas, gtkglext/gdkglext, and any X11 libs Homebrew's cairo pulls in
# (inert on GTK-Quartz, but linked). freeglut is intentionally NOT linked on
# macOS any more (see configure.ac), so it and Mesa libGL fall out of the set.
#
# Unlike bundle_conda_deps.sh (a fixed list), the Homebrew closure is
# discovered dynamically -- it starts from every /opt/homebrew reference in the
# installed Mach-O tree and follows dependencies transitively -- so it stays
# correct as formulae and versions change.
#
# Homebrew records install names in TWO forms: /opt/homebrew/opt/<f>/lib (the
# versioned symlink) and /opt/homebrew/Cellar/<f>/<v>/lib (the real path).
# Both are rewritten, by reading each Mach-O's actual load commands.
#
# ORDERING (build.sh already arranges all of this -- recorded here only so the
# constraint is not lost if the pipeline is ever rearranged): this pass has to
# follow make_relocatable.sh, bundle_conda_deps.sh and bundle_pixbuf_loaders.sh,
# so the pixbuf loaders' Homebrew dependencies are closed over too, and it has to
# precede the codesign step, which re-signs everything this rewrites.
#
# Usage:  ./scripts/bundle_homebrew_deps.sh <install-prefix> [brew-prefix]
# Default brew-prefix: BREW_PREFIX env, else `brew --prefix`, else /opt/homebrew.
#
# NB macOS /bin/bash is 3.2 (no associative arrays), so the transitive closure
# is computed in a python3 helper; the copy/rewrite/verify passes are bash.

set -e

PREFIX="${1:?Usage: $0 <install-prefix> [brew-prefix]}"
PREFIX="$(cd "$PREFIX" && pwd)"
BREW="${2:-${BREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}}"

[ -d "$PREFIX/lib" ] || { echo "error: $PREFIX/lib missing" >&2; exit 1; }
LIBDIR="$PREFIX/lib"

echo "==> bundling Homebrew libraries (brew prefix: $BREW) into $LIBDIR"

# Every Mach-O in the install we inspect and rewrite: libexec/ + bin/
# executables, and every dylib / loadable module (e.g. gdk-pixbuf loaders,
# python lib-dynload) anywhere under lib/.
mach_o_files() {
    find "$PREFIX/libexec" "$PREFIX/bin" -type f 2>/dev/null
    find "$LIBDIR" -type f \( -name '*.dylib' -o -name '*.so' \) 2>/dev/null
}

# The /opt/homebrew (or $BREW) dependency paths recorded in a Mach-O.
brew_deps() {
    otool -L "$1" 2>/dev/null | tail -n +2 | awk '{print $1}' | \
        grep -E "^($BREW|/opt/homebrew)/" || true
}

# --- 1. Transitive closure of Homebrew dylibs, as "basename<TAB>realpath" ---
echo "==> computing Homebrew dependency closure"
CLOSURE="$(python3 - "$PREFIX" "$BREW" <<'PY'
import subprocess, os, sys
prefix, brew = sys.argv[1], sys.argv[2]
roots = (brew.rstrip('/') + '/', '/opt/homebrew/')
def deps(f):
    try:
        out = subprocess.check_output(['otool', '-L', f],
                                      stderr=subprocess.DEVNULL).decode()
    except Exception:
        return []
    res = []
    for ln in out.splitlines()[1:]:
        p = ln.strip().split(' ')[0]
        if p.startswith(roots):
            res.append(p)
    return res

files = []
for sub in ('libexec', 'bin'):
    for dp, _, fs in os.walk(os.path.join(prefix, sub)):
        files += [os.path.join(dp, fn) for fn in fs]
for dp, _, fs in os.walk(os.path.join(prefix, 'lib')):
    files += [os.path.join(dp, fn) for fn in fs
              if fn.endswith('.dylib') or fn.endswith('.so')]

seen = set()
queue = []
for f in files:
    queue += deps(f)
while queue:
    lib = queue.pop()
    if lib in seen:
        continue
    seen.add(lib)
    queue += deps(os.path.realpath(lib))

emitted = set()
for lib in sorted(seen):
    base = os.path.basename(lib)
    if base in emitted:
        continue
    emitted.add(base)
    print(base + '\t' + os.path.realpath(lib))
PY
)"

n_closure="$(printf '%s\n' "$CLOSURE" | grep -c . || true)"
echo "    $n_closure Homebrew libraries in closure"

# --- 2. Copy each closure member into lib/ (flattened basename) ---
copied=0; kept=0
while IFS="$(printf '\t')" read -r base real; do
    [ -n "$base" ] || continue
    if [ -e "$LIBDIR/$base" ]; then
        # Already bundled (e.g. libpng16 / libfreetype / libintl came from
        # bundle_conda_deps.sh). Keep that copy; Homebrew consumers will be
        # pointed at it by the rewrite step. Same soname -> ABI-compatible.
        kept=$((kept + 1)); continue
    fi
    [ -f "$real" ] || { echo "    warn: missing $real, skipping $base" >&2; continue; }
    cp -p "$real" "$LIBDIR/$base"
    chmod u+w "$LIBDIR/$base"
    install_name_tool -id "@rpath/$base" "$LIBDIR/$base" 2>/dev/null || true
    # Let a bundled lib resolve its siblings (@rpath/<x>) from its own dir,
    # in addition to coot-bin's @executable_path/../lib.
    install_name_tool -add_rpath "@loader_path" "$LIBDIR/$base" 2>/dev/null || true
    copied=$((copied + 1))
done <<EOF
$CLOSURE
EOF
echo "    copied $copied new lib(s); reused $kept already-bundled"

# --- 3. Rewrite every Homebrew reference in the tree to @rpath/<basename> ---
echo "==> rewriting Homebrew references to @rpath"
rewrite_file() {
    local f="$1" dep base
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        base="$(basename "$dep")"
        install_name_tool -change "$dep" "@rpath/$base" "$f" 2>/dev/null || true
    done < <(brew_deps "$f")
}
# mach_o_files is re-evaluated here, so it now also enumerates the libs copied
# in step 2 (their sibling refs still point at /opt/homebrew).
while IFS= read -r f; do rewrite_file "$f"; done < <(mach_o_files)

# --- 4. Verify: nothing should reference Homebrew any more ---
echo "==> verifying no Homebrew references remain"
leaks=0
while IFS= read -r f; do
    if otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | \
         grep -qE "^($BREW|/opt/homebrew)/"; then
        echo "    LEAK: $f" >&2
        otool -L "$f" 2>/dev/null | awk '{print $1}' | \
            grep -E "^($BREW|/opt/homebrew)/" | sed 's/^/         /' >&2
        leaks=$((leaks + 1))
    fi
done < <(mach_o_files)

if [ "$leaks" -gt 0 ]; then
    echo "error: $leaks Mach-O file(s) still reference Homebrew after bundling" >&2
    exit 1
fi

# v0.1.4.15: the leak check above inspects DEPENDENCIES; a copied Homebrew dylib
# can still carry its own Cellar LC_RPATH (libdbus, libguile and libjpeg all do),
# which check_install.sh reports as HOST-RPATH. build.sh runs this same pass over
# the whole tree afterwards, so the normal path was always covered -- running it
# here too means the bundler leaves a clean install when invoked on its own.
# Same script both times, so there is one implementation of the rule.
echo "==> stripping foreign rpaths from the bundled tree"
"$(dirname "$0")/strip_host_rpaths.sh" "$PREFIX" || true

echo "==> Homebrew bundling complete: install is now Homebrew-independent"

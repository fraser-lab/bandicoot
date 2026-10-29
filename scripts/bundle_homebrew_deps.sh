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

def rpath_dep_basenames(f):
    """Dependencies recorded as @rpath/<name>, or as a bare <name>.

    These are INVISIBLE to the /opt/homebrew scan above, and that matters:
    build.sh puts -L$PREFIX/lib ahead of -L$BREW_PREFIX/lib, so once a Homebrew
    library has been bundled once, the next build links against the BUNDLED
    copy -- whose install_name this script already rewrote to @rpath/<name>.
    The Homebrew origin then disappears from the recorded dependency, the
    closure misses it, and the install step (which replaces lib/) drops it. The
    first build succeeds and every later one ships a broken tree, which is
    exactly what the check_install gate caught for libgemmi_cpp.
    """
    try:
        out = subprocess.check_output(['otool', '-L', f],
                                      stderr=subprocess.DEVNULL).decode()
    except Exception:
        return []
    res = []
    for ln in out.splitlines()[1:]:
        p = ln.strip().split(' ')[0]
        if p.startswith('@rpath/'):
            res.append(os.path.basename(p))
        elif '/' not in p:
            res.append(p)
    return res

seen = set()
queue = []
for f in files:
    queue += deps(f)

# Seed the closure with any @rpath/bare dependency we do NOT already ship but
# Homebrew does provide. Anything already present under lib/ (all the coot
# libraries, and whatever bundle_conda_deps.sh placed) is correctly skipped.
for f in files:
    for base in rpath_dep_basenames(f):
        if os.path.exists(os.path.join(prefix, 'lib', base)):
            continue
        cand = os.path.join(brew, 'lib', base)
        if os.path.exists(cand):
            queue.append(cand)
while queue:
    lib = queue.pop()
    if lib in seen:
        continue
    seen.add(lib)
    real = os.path.realpath(lib)
    queue += deps(real)
    # Homebrew libraries can name their own siblings as @rpath/<name> (webp ->
    # sharpyuv does), so those must be followed during the walk as well, not
    # only for files already in the install.
    for base in rpath_dep_basenames(real):
        if os.path.exists(os.path.join(prefix, 'lib', base)):
            continue
        for d in (os.path.dirname(real), os.path.join(brew, 'lib')):
            cand = os.path.join(d, base)
            if os.path.exists(cand):
                queue.append(cand)
                break

# One library file can be reached under several names: the Homebrew version
# symlinks. Usually only one name is ever recorded, but some formulae (wx) give
# a library an install name that is a symlink while their sibling libraries
# reference the real file. Bundled as-is, that ships the same library twice with
# two install names, and the process loads both. Group every name by the file it
# resolves to -- including copies an earlier build already placed in lib/ -- and
# where a file has more than one name, bundle it once under its real name.
names_of = {}
for lib in seen:
    names_of.setdefault(os.path.realpath(lib), set()).add(os.path.basename(lib))
libdir = os.path.join(prefix, 'lib')
for base in os.listdir(libdir):
    if os.path.islink(os.path.join(libdir, base)):
        continue
    cand = os.path.join(brew, 'lib', base)
    if os.path.exists(cand):
        names_of.setdefault(os.path.realpath(cand), set()).add(base)

emitted = set()
for real in sorted(names_of):
    names = names_of[real]
    if len(names) > 1:
        canonical = os.path.basename(real)
        for alias in sorted(names - {canonical}):
            print('A\t' + alias + '\t' + canonical)
    elif real in [os.path.realpath(l) for l in seen]:
        canonical = next(iter(names))
    else:
        continue # bundled earlier under its only name; nothing to do
    if canonical in emitted:
        continue
    emitted.add(canonical)
    print('L\t' + canonical + '\t' + real)
PY
)"

# "alias<TAB>canonical" for libraries reached under more than one name
ALIASES="$(printf '%s\n' "$CLOSURE" | awk -F'\t' '$1 == "A" {print $2 "\t" $3}')"
CLOSURE="$(printf '%s\n' "$CLOSURE" | awk -F'\t' '$1 == "L" {print $2 "\t" $3}')"

# The bundled name for a library basename: its canonical name if it is an alias.
canonical_name() {
    local c
    c="$(printf '%s\n' "$ALIASES" | awk -F'\t' -v a="$1" '$1 == a {print $2; exit}')"
    printf '%s\n' "${c:-$1}"
}

n_closure="$(printf '%s\n' "$CLOSURE" | grep -c . || true)"
echo "    $n_closure Homebrew libraries in closure"
if [ -n "$ALIASES" ]; then
    echo "    libraries reached under more than one name (bundled once, as the second):"
    printf '%s\n' "$ALIASES" | sed 's/^/      /'
fi

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
        base="$(canonical_name "$(basename "$dep")")"
        install_name_tool -change "$dep" "@rpath/$base" "$f" 2>/dev/null || true
    done < <(brew_deps "$f")
    # references an earlier build recorded against a now-retired alias name
    [ -n "$ALIASES" ] || return 0
    while IFS="$(printf '\t')" read -r alias canonical; do
        [ -n "$alias" ] || continue
        if otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | \
             grep -qxF "@rpath/$alias"; then
            install_name_tool -change "@rpath/$alias" "@rpath/$canonical" "$f" \
                2>/dev/null || true
        fi
    done <<EOF
$ALIASES
EOF
}
# mach_o_files is re-evaluated here, so it now also enumerates the libs copied
# in step 2 (their sibling refs still point at /opt/homebrew).
while IFS= read -r f; do rewrite_file "$f"; done < <(mach_o_files)

# Retire the alias copies: nothing references them any more, and leaving them in
# lib/ would let the next build link against them again.
if [ -n "$ALIASES" ]; then
    while IFS="$(printf '\t')" read -r alias canonical; do
        [ -n "$alias" ] || continue
        if [ -f "$LIBDIR/$alias" ] && [ ! -L "$LIBDIR/$alias" ]; then
            rm -f "$LIBDIR/$alias"
            echo "    removed duplicate $alias (bundled as $canonical)"
        fi
    done <<EOF
$ALIASES
EOF
fi

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

# A library under two names loads twice; for wx that means two runtimes, and a
# window from one never receives events meant for the other.
dups=0
if [ -n "$ALIASES" ]; then
    echo "==> verifying each library is bundled under one name"
    while IFS="$(printf '\t')" read -r alias canonical; do
        [ -n "$alias" ] || continue
        if [ -e "$LIBDIR/$alias" ] && [ ! -L "$LIBDIR/$alias" ]; then
            echo "    DUPLICATE: lib/$alias is a second copy of $canonical" >&2
            dups=$((dups + 1))
        fi
        while IFS= read -r f; do
            if otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | \
                 grep -qxF "@rpath/$alias"; then
                echo "    DUPLICATE: $f references $alias, not $canonical" >&2
                dups=$((dups + 1))
            fi
        done < <(mach_o_files)
    done <<EOF
$ALIASES
EOF
fi
if [ "$dups" -gt 0 ]; then
    echo "error: $dups library name duplicate(s) remain after bundling" >&2
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

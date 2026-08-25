#!/bin/bash
# Bandicoot: configure + build + install on macOS Tahoe / Apple Silicon.
# Customise via environment variables (all optional):
#   PREFIX        install root         (default: $HOME/sw/bandicoot-install)
#   FFTW_PREFIX   FFTW2 install        (default: auto-detected; see below)
#   CONDA_PREFIX  miniconda root       (default: /opt/miniconda3)
#   BREW_PREFIX   homebrew root        (default: /opt/homebrew or `brew --prefix`)
#   CANVAS_PREFIX libart/libgnomecanvas/goocanvas tree, as produced by
#                 ./scripts/build_deps.sh          (default: <repo>/deps/canvas)
#   COOT_DATA_SRC monomer dictionary + reference structures
#                 (default: <repo>/data/coot-data, checked in)
#   PROBE_SRC / REDUCE_SRC / REDUCE_HET_SRC
#                 MolProbity binaries + dictionary; auto-probed from a CCP4
#                 install, and merely warned about if absent
#   JOBS          parallel make jobs   (default: number of CPUs)
#   BUILD_SKIP_CHECKS=1  skip the closing dependency-closure gate (see the end
#                 of this script). Default is to FAIL the build on an unresolved
#                 or build-host dependency, dev builds included.
set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

# Per-machine overrides: <repo>/.bandicoot-local, gitignored, sourced before any
# default is applied. This is how a developer whose dependency trees predate (or
# differ from) the in-repo defaults avoids retyping variables on every build --
# the same reasoning as the .bandicoot-dev marker: a setting you must remember to
# pass is a setting you will eventually forget. Never shipped, never committed,
# so it cannot affect anyone building from a clean checkout.
#
# Use the ":=" form so a variable given on the command line still wins:
#     : "${CANVAS_PREFIX:=$HOME/sw/canvas-deps}"
if [ -f "${REPO_ROOT}/.bandicoot-local" ]; then
    echo "==> sourcing local overrides: .bandicoot-local"
    # shellcheck source=/dev/null
    . "${REPO_ROOT}/.bandicoot-local"
fi

PREFIX="${PREFIX:-$HOME/sw/bandicoot-install}"
# Prefix baked into the binaries as compile-time fallback data paths
# (-DPKGDATADIR / -DPKGPYTHONDIR etc.). Kept GENERIC so shipped binaries never
# embed the builder's home ($HOME/sw/...). Files still install under $PREFIX
# (via `make install prefix=$PREFIX` below); the app is relocatable at runtime
# (the wrapper exports COOT_DATA_DIR / COOT_PYTHON_DIR from the real extract
# location), so this compiled value is only an unused fallback.
BANDICOOT_COMPILE_PREFIX="${BANDICOOT_COMPILE_PREFIX:-/opt/bandicoot}"
CONDA_PREFIX="${CONDA_PREFIX:-/opt/miniconda3}"
BREW_PREFIX="${BREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

# v0.1.4.13: NO dependency location may default to anything under $HOME.
#
# Until now three prefixes defaulted into the maintainer's home directory
# ($HOME/sw/canvas-deps, $HOME/sw/coot-builds/coot-deps, $HOME/sw/coot-deps).
# Those trees were hand-built and exist on exactly one machine, so this script
# could not work for anyone else -- the substance of GitHub #4 ("hard
# dependencies on brew and its location"). Every external location is now an
# explicit variable that either resolves to something installed by a package
# manager, to a tree produced by a script in THIS repo, or to data checked into
# THIS repo. Anything unresolved fails below with the variable name and the
# command that produces it, rather than silently building a broken install.

# The two trees produced by scripts/build_deps.sh from the vendored sources in
# third_party/. Neither libart/libgnomecanvas/goocanvas nor single-precision
# FFTW 2 exists in any package manager, so they are built from source into
# <repo>/deps rather than searched for on the system.
CANVAS_PREFIX="${CANVAS_PREFIX:-${REPO_ROOT}/deps/canvas}"

# FFTW2 has no pkg-config file, so it is located by path. Prefer the in-repo
# build; still probe a couple of legacy locations so an existing hand-built tree
# keeps working, and let FFTW_PREFIX override outright.
if [ -z "${FFTW_PREFIX:-}" ]; then
    for cand in \
        "${REPO_ROOT}/deps/fftw2" \
        "${CONDA_PREFIX}/fftw2" \
        "${BREW_PREFIX}/opt/fftw2"; do
        if [ -f "${cand}/include/fftw.h" ] || [ -f "${cand}/include/sfftw.h" ]; then
            FFTW_PREFIX="${cand}"
            break
        fi
    done
    FFTW_PREFIX="${FFTW_PREFIX:-${REPO_ROOT}/deps/fftw2}"
    echo "==> FFTW_PREFIX auto-detected: ${FFTW_PREFIX}"
fi

# Runtime data (monomer dictionary + reference structures) is checked into the
# repo as of v0.1.4.13 -- see data/coot-data/README.md. No external source.
COOT_DATA_SRC="${COOT_DATA_SRC:-${REPO_ROOT}/data/coot-data}"

# ---------------------------------------------------------------------------
# Preflight: fail early, name the variable, name the fix.
# ---------------------------------------------------------------------------
_preflight_fail=0
_preflight() {   # <test-path> <VARNAME> <current-value> <how-to-fix>
    if [ ! -e "$1" ]; then
        echo "!! build.sh: $2 does not resolve to a usable tree." >&2
        echo "   $2=$3" >&2
        echo "   missing: $1" >&2
        echo "   fix: $4" >&2
        echo "" >&2
        _preflight_fail=1
    fi
}

_preflight "${BREW_PREFIX}/lib/pkgconfig/gtk+-2.0.pc" BREW_PREFIX "${BREW_PREFIX}" \
    "brew install gtk+ gtkglext freeglut gsl boost cairo libpng sqlite bzip2"
_preflight "${CONDA_PREFIX}/bin/python3-config" CONDA_PREFIX "${CONDA_PREFIX}" \
    "install Miniconda, then see BUILD.md section 1 for the conda package list"
_preflight "${CANVAS_PREFIX}/lib/pkgconfig/goocanvas.pc" CANVAS_PREFIX "${CANVAS_PREFIX}" \
    "./scripts/build_deps.sh    (builds libart/libgnomecanvas/goocanvas + FFTW 2)"
_preflight "${COOT_DATA_SRC}/monomers/list/mon_lib_list.cif" COOT_DATA_SRC "${COOT_DATA_SRC}" \
    "this data is checked in -- restore it with: git checkout -- data/coot-data"

if [ ! -f "${FFTW_PREFIX}/include/fftw.h" ] && [ ! -f "${FFTW_PREFIX}/include/sfftw.h" ]; then
    echo "!! build.sh: FFTW_PREFIX does not contain single-precision FFTW 2." >&2
    echo "   FFTW_PREFIX=${FFTW_PREFIX}" >&2
    echo "   missing: include/fftw.h (or include/sfftw.h)" >&2
    echo "   fix: ./scripts/build_deps.sh    (builds FFTW 2.1.5 from third_party/)" >&2
    echo "" >&2
    _preflight_fail=1
fi

if [ "${_preflight_fail}" != "0" ]; then
    echo "!! build.sh: unmet build dependencies (see above). Nothing was built." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# v0.1.4.15: embedded-Python version advisory.
#
# Bandicoot embeds whichever python3 $CONDA_PREFIX provides -- nothing pins it,
# and nothing should: the bundler follows the interpreter (it used to hard-code
# "3.13", which is what broke GitHub #24 for a builder on a 3.14 base). But
# "any version builds" is not "any version is tested". A Python minor release
# can remove a stdlib module the tree still uses -- 3.12 removing distutils
# broke configure exactly that way -- and such a break can surface at RUNTIME,
# long after a clean build.
#
# So: warn, never fail. An untested version is a reason to test, not a reason to
# refuse to build. ADD A VERSION HERE ONLY AFTER ACTUALLY BUILDING AND RUNNING
# AGAINST IT -- ideally when each new Python is released, so the list stays
# ahead of the problem rather than behind a bug report.
# 3.13: the version releases are built against (conda base).
# 3.14: GUI-tested by Art 2026-08-24 on 0.1.4.15-u1 -- launch, mmCIF read, EDS
#       fetch of 2GEW + map, Sphere Refine with undo, and pandda.inspect on a
#       PanDDA output folder all behaved normally.
BANDICOOT_TESTED_PYTHON="${BANDICOOT_TESTED_PYTHON:-3.13 3.14}"
_py_ver="$("${CONDA_PREFIX}/bin/python3" -c \
    'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null || true)"
if [ -n "${_py_ver}" ]; then
    case " ${BANDICOOT_TESTED_PYTHON} " in
        *" ${_py_ver} "*) ;;   # tested; say nothing
        *)
            echo "==  build.sh: embedded Python is ${_py_ver}; tested versions are:" \
                 "${BANDICOOT_TESTED_PYTHON}" >&2
            echo "    The build should still work -- the bundler follows whatever" >&2
            echo "    \$CONDA_PREFIX provides -- but nothing has been verified on" >&2
            echo "    ${_py_ver}." >&2
            echo "" >&2
            echo "    To build against a tested version instead:" >&2
            echo "      conda create -n bandicoot python=${BANDICOOT_TESTED_PYTHON%% *}" >&2
            echo "      conda activate bandicoot" >&2
            echo "      conda install -c bioconda clipper libccp4 mmdb2 ssm" >&2
            echo "      CONDA_PREFIX=\"\$CONDA_PREFIX\" ./scripts/build.sh" >&2
            echo "" >&2
            echo "    To silence this after testing ${_py_ver} yourself, add it to" >&2
            echo "    BANDICOOT_TESTED_PYTHON in this script." >&2
            echo "" >&2
            ;;
    esac
fi
unset _py_ver

# Homebrew on macOS supplies version-specific pkg-config dirs to fill in
# entries that the system SDK provides (e.g. bzip2.pc lives under
# $BREW_PREFIX/Library/Homebrew/os/mac/pkgconfig/<macOS_major>/).
MAC_MAJOR="${MAC_MAJOR:-$(sw_vers -productVersion | cut -d. -f1)}"
BREW_PC_OSDIR="${BREW_PC_OSDIR:-${BREW_PREFIX}/Library/Homebrew/os/mac/pkgconfig/${MAC_MAJOR}}"

if [ ! -x ./configure ]; then
    echo "==> ./configure not found; running bootstrap"
    ./scripts/bootstrap.sh
fi

# v0.1.0.3: per-build unreleased-version counter. Each call to build.sh
# increments .build-counter and regenerates src/bandicoot-build-id.h so the
# title bar carries an unambiguous "-u<N>" suffix during dev iteration.
# Counter resets when src/bandicoot-version.h's BANDICOOT_VERSION bumps.
#
# v0.1.4.13: THE SUFFIX IS NOW OFF BY DEFAULT. It is a maintainer-iteration
# marker -- "-u7" is machine-local counter state that means nothing on anyone
# else's machine -- so somebody building from source should get a clean
# "0.1.4.13", not a number that looks like a build identity but isn't.
#
# It is enabled by the presence of a gitignored marker file, <repo>/.bandicoot-dev,
# NOT by an environment variable. That is deliberate: an env var has to be
# remembered on every invocation, and the one time it is forgotten the build
# silently claims to be a clean release -- which is the exact confusion the
# counter exists to prevent. A marker file is set once per clone and then
# always right. It can never reach a source builder because it is gitignored,
# and it cannot drift out of sync with a second copy of this script because
# there is only one script.
#
#   enable  (once, per working copy):   touch .bandicoot-dev
#   disable (once):                     rm .bandicoot-dev
#   one-off override, either way:       BANDICOOT_DEV=1 ./scripts/build.sh
#                                       BANDICOOT_DEV=0 ./scripts/build.sh
#   release build (always suppresses):  BANDICOOT_RELEASE=1 ./scripts/build.sh
BANDICOOT_VERSION_STR="$(awk -F'"' '/^#define BANDICOOT_VERSION /{print $2}' \
                             src/bandicoot-version.h)"
COUNTER_FILE=".build-counter"
DEV_MARKER="${REPO_ROOT}/.bandicoot-dev"

if [ -n "${BANDICOOT_DEV:-}" ]; then
    _dev_build="${BANDICOOT_DEV}"          # explicit override wins
    _dev_why="BANDICOOT_DEV=${BANDICOOT_DEV}"
elif [ -f "${DEV_MARKER}" ]; then
    _dev_build=1
    _dev_why=".bandicoot-dev present"
else
    _dev_build=0
    _dev_why="no .bandicoot-dev marker"
fi
# A release build is never a dev build, whatever the marker says.
[ -n "${BANDICOOT_RELEASE:-}" ] && _dev_build=0

if [ "${_dev_build}" = "1" ]; then
    if [ -f "$COUNTER_FILE" ]; then
        stored_ver=$(awk '{print $1}' "$COUNTER_FILE")
        stored_n=$(awk '{print $2}'   "$COUNTER_FILE")
        if [ "$stored_ver" = "$BANDICOOT_VERSION_STR" ]; then
            n=$((stored_n + 1))
        else
            n=1
        fi
    else
        n=1
    fi
    echo "$BANDICOOT_VERSION_STR $n" > "$COUNTER_FILE"
    BUILD_SUFFIX="-u$n"
    echo "==> dev build (${_dev_why}): ${BANDICOOT_VERSION_STR}${BUILD_SUFFIX}"
else
    BUILD_SUFFIX=""
    if [ -n "${BANDICOOT_RELEASE:-}" ]; then
        echo "==> release build (BANDICOOT_RELEASE=1): version ${BANDICOOT_VERSION_STR}"
    else
        # Deliberately says nothing about .bandicoot-dev: the marker is a
        # maintainer-only affordance and mentioning it here would just raise a
        # question a source builder has no reason to care about.
        echo "==> building version ${BANDICOOT_VERSION_STR}"
    fi
fi
cat > src/bandicoot-build-id.h <<EOF
// Generated by scripts/build.sh on each invocation. Do not edit; do not
// commit. The "-u<N>" suffix tags maintainer iteration builds so the title bar
// distinguishes them from one another and from the released tarball. It is
// empty unless <repo>/.bandicoot-dev exists (or BANDICOOT_DEV=1 is set), so a
// build from source reports a clean version number.
#ifndef BANDICOOT_BUILD_ID_H
#define BANDICOOT_BUILD_ID_H
#define BANDICOOT_BUILD_SUFFIX "${BUILD_SUFFIX}"
#endif
EOF
# Force main.cc to recompile so the new suffix lands in the binary.
touch src/main.cc

# Python embed flags. python3-config --ldflags omits -lpython3.X since
# Python 3.8 — you have to ask for --embed explicitly. Coot's autotools
# macro pre-dates that change, so we backfill via LDFLAGS here.
PYTHON_EMBED_LDFLAGS="$(${CONDA_PREFIX:-/opt/miniconda3}/bin/python3-config --ldflags --embed 2>/dev/null)"

export CPPFLAGS="-I${CONDA_PREFIX}/include -I${PREFIX}/include -I${BREW_PREFIX}/include"
export LDFLAGS="-L${CONDA_PREFIX}/lib -L${PREFIX}/lib -L${BREW_PREFIX}/lib \
    -Wl,-rpath,${CONDA_PREFIX}/lib -Wl,-rpath,${PREFIX}/lib \
    ${PYTHON_EMBED_LDFLAGS}"
# -include compat/python23-shim.hh: force-include the Py2→Py3 macro shim
# into every TU so the ~350 PyString_*/PyInt_* call sites scattered
# through Coot's c-interface compile against Python 3 without per-file
# edits. The shim is a no-op when Python.h hasn't been pulled in.
# See compat/python23-shim.hh for details.
SHIM_INCLUDE="-include ${REPO_ROOT}/compat/python23-shim.hh"
export CXXFLAGS="-g -O2 -Wall -Wno-unused -std=c++14 ${SHIM_INCLUDE}"
export CFLAGS="-g -O2 -Wall -Wno-unused ${SHIM_INCLUDE}"

export PKG_CONFIG_PATH="\
${CANVAS_PREFIX}/lib/pkgconfig:\
${BREW_PREFIX}/lib/pkgconfig:\
${BREW_PREFIX}/share/pkgconfig:\
${BREW_PC_OSDIR}:\
${PREFIX}/lib/pkgconfig:\
${CONDA_PREFIX}/lib/pkgconfig"

# v0.1.0.2: libgnomecanvas-2.0 is built from source into ${CANVAS_PREFIX}
# (PKG_CONFIG_PATH above picks it up). This enables HAVE_GNOME_CANVAS at
# compile time, which unlocks Sequence View (Draw menu), the 2D ligand editor,
# and the geometry graphs (Ramachandran etc.). ${CANVAS_PREFIX}/lib/pkgconfig
# also ships shim .pc files for bzip2 and x11/xcb/xext/xrender (those don't
# exist under Homebrew on macOS but are referenced via cairo/freetype2's
# private requires; Bandicoot doesn't actually link against any of them at
# runtime).
#
# v0.1.4.13: that tree is produced by ./scripts/build_deps.sh from the sources
# vendored in third_party/, rather than by hand, and defaults to
# <repo>/deps/canvas. The preflight above fails the build if it is absent,
# naming the script -- previously a missing canvas tree just silently dropped
# Sequence View and the ligand editor from the build.

echo "==> ./configure --prefix=${BANDICOOT_COMPILE_PREFIX} (generic compile-time fallback; files install to ${PREFIX})"
./configure \
    --prefix="${BANDICOOT_COMPILE_PREFIX}" \
    --with-fftw-prefix="${FFTW_PREFIX}" \
    --with-goocanvas-prefix="${CANVAS_PREFIX}" \
    --with-glut-prefix="${BREW_PREFIX}" \
    --with-boost="${BREW_PREFIX}" \
    --with-python="${CONDA_PREFIX}" \
    PYTHON="${CONDA_PREFIX}/bin/python3" \
    PYTHON_CONFIG="${CONDA_PREFIX}/bin/python3-config"
# v0.1.0.0: --with-python re-enabled. Coot's C interface is Py2-flavoured
# throughout; the Py2→Py3 macro shim at compat/python23-shim.hh
# (force-included via CXXFLAGS above) handles the vast majority of
# PyString_*/PyInt_* sites. Per-file fixes only needed where return
# types differ (PyUnicode_AsUTF8 returns const char*) or semantics
# differ (PyString_Check accepted bytes on Py2, PyUnicode_Check
# doesn't on Py3). Without --with-python, Phenix's socket listener
# stays inert and pandda.inspect can't launch — see
# [[bandicoot-coot-py-broken]] for the full backstory.

# RELEASE builds recompile from clean. Compile-time -D macros (PKGDATADIR etc.)
# are baked into each .o; changing a compile flag -- e.g. the generic
# BANDICOOT_COMPILE_PREFIX -- does NOT retrigger compilation of unchanged
# sources, so stale objects would keep an old prefix (this is how the builder's
# /Users/<home> path lingered in the binaries). A clean recompile guarantees the
# shipped binaries carry only the generic fallback path. Dev builds stay
# incremental (fast); force a clean one anytime with BANDICOOT_CLEAN=1.
if [ -n "${BANDICOOT_RELEASE:-}" ] || [ -n "${BANDICOOT_CLEAN:-}" ]; then
    echo "==> make clean (release/clean build)"
    make clean >/dev/null 2>&1 || true
fi
echo "==> make -j${JOBS}"
make -j"${JOBS}"

# Install to a staging DESTDIR, then relocate the tree to the real $PREFIX.
# Using DESTDIR (rather than `make install prefix=$PREFIX`) is deliberate:
# overriding `prefix` at install time makes make RE-COMPILE the prefix-dependent
# objects (main.o's -DPKGDATADIR / -DPKGPYTHONDIR) with $PREFIX, which re-injects
# the builder's home path into the binary. DESTDIR only PREPENDS to install
# paths -- no recompile -- so the binaries keep the generic
# ${BANDICOOT_COMPILE_PREFIX} fallback and carry no /Users/<builder> path. The
# install is relocatable (rpaths are @executable_path/@rpath), so moving the
# staged tree to $PREFIX is safe.
echo "==> make install (DESTDIR staging) -> ${PREFIX}"
_bcoot_destdir="$(mktemp -d)"

# v0.1.4.15: remove the staging tree on ANY exit, not just the success path.
# It holds a complete copy of the install (~300 MB), so a build that died
# between `make install` and the cleanup below -- a failed stage under `set -e`,
# or Ctrl-C -- left the whole thing behind in /var/folders, invisibly and
# forever. Found one dated eight days earlier while fixing GitHub #25.
# INT/TERM re-exit rather than cleaning up in place, so the interrupt still
# stops the build; the EXIT trap then does the removal exactly once.
_bcoot_clean_staging() {
    [ -n "${_bcoot_destdir:-}" ] && rm -rf "${_bcoot_destdir}"
}
trap _bcoot_clean_staging EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

make install DESTDIR="${_bcoot_destdir}"
# v0.1.4.15: relocate INTO the prefix, never replace the prefix itself.
#
# This used to be `rm -rf $PREFIX` + `mv <staged> $PREFIX`. Both of those
# create or delete a directory ENTRY, which needs write+execute on the
# prefix's PARENT -- not on the prefix. So a system location failed even when
# the user owned the prefix outright (GitHub #25):
#
#   drwxr-xr-x root  wheel  /usr/local              <- not writable by the user
#   drwxr-xr-x tony  staff  /usr/local/bandicoot    <- owned by the user
#   rm: /usr/local/bandicoot: Permission denied
#
# rm had in fact emptied the directory and then failed on the final rmdir, so
# the message names the prefix while the real obstacle is one level up.
#
# Working on the CONTENTS instead needs write on the prefix alone. Note both
# steps use `find`, not a `*` glob: the install root carries dotfiles (e.g.
# .bandicoot-coot-bin.entitlements) that a glob would silently leave behind.
_bcoot_staged="${_bcoot_destdir}${BANDICOOT_COMPILE_PREFIX}"

if [ ! -d "${PREFIX}" ]; then
    if ! mkdir -p "${PREFIX}" 2>/dev/null; then
        echo "!! build.sh: cannot create ${PREFIX}" >&2
        echo "   Its parent ($(dirname "${PREFIX}")) is not writable by $(id -un)." >&2
        echo "   Create it once with elevation, then re-run as yourself:" >&2
        echo "     sudo mkdir -p \"${PREFIX}\"" >&2
        echo "     sudo chown \"\$(id -u):\$(id -g)\" \"${PREFIX}\"" >&2
        exit 1
    fi
fi
if [ ! -w "${PREFIX}" ]; then
    echo "!! build.sh: ${PREFIX} exists but is not writable by $(id -un)." >&2
    echo "   fix: sudo chown \"\$(id -u):\$(id -g)\" \"${PREFIX}\"" >&2
    exit 1
fi

# Empty it, keeping the directory itself (every build is a clean install tree).
find "${PREFIX}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +

# MOVE the staged contents in rather than copying them: a copy would leave a
# second, identical set of binaries in the staging tree until it is deleted,
# and two candidate copies of the same install is exactly what we do not want.
# Same filesystem -> this is a rename per entry, so nothing is duplicated even
# momentarily. (Roughly ten top-level entries, so one mv each is fine.)
find "${_bcoot_staged}" -mindepth 1 -maxdepth 1 -exec mv -f {} "${PREFIX}/" \;
rm -rf "${_bcoot_destdir}"

# Bandicoot cleanup: Coot's `make install` ships build/dev artifacts an app never
# touches at runtime -- ~287 MB of static libs (lib/*.a), libtool archives
# (lib/*.la), and installed header copies (include/). The app links the .dylib
# twins, so these are pure dead weight in the tarball. Prune them here (after the
# link step, so the .a are available while building, then removed for shipping).
# Safe: relocation/bundling/codesign/check_install all operate on Mach-O
# .dylib/.so, never on .a/.la/headers.
#
# TOOLKIT MODE: set BANDICOOT_TOOLKIT=1 to KEEP them and ship a Coot-0.9-style
# developer toolkit (static libs + .la + headers to build against libcoot-*),
# instead of the lean end-user app bundle. Everything else is identical.
if [ "${BANDICOOT_TOOLKIT:-0}" = "1" ]; then
    echo "==> BANDICOOT_TOOLKIT=1: keeping static libs + .la + headers (toolkit build)"
else
    echo "==> pruning dev artifacts (lib/*.a, lib/*.la, include/) from the install"
    rm -f "${PREFIX}/lib/"*.a "${PREFIX}/lib/"*.la
    rm -rf "${PREFIX}/include"
fi

# Rewrite hard-coded Mach-O paths to @rpath / @executable_path form so
# the install can be moved or packaged into a portable tarball.
# v0.1.4.15: every helper script below is run BY THIS SCRIPT, automatically.
# run_stage prints an explicit "running"/"finished with no errors" pair around
# each one so that scanning the build output -- by a human, or by an assistant
# summarising it -- makes that unmistakable. GitHub #24's reporter concluded
# from those scripts' own headers that they were a manual post-build sequence,
# ran them individually, and so kept re-introducing a stale rpath that build.sh
# strips on its own. A stage that fails prints "FAILED" and aborts the build, so
# a missing "finished" line is itself the signal.
run_stage() {           # <script-name> [args...]
    local _s="$1"; shift
    echo "==> running scripts/${_s}"
    if ! "${REPO_ROOT}/scripts/${_s}" "$@"; then
        echo "!! scripts/${_s} FAILED" >&2
        return 1
    fi
    echo "==> scripts/${_s} finished with no errors"
}

# Pass BANDICOOT_COMPILE_PREFIX so the coot dylibs' install-names/deps (baked
# with the generic compile prefix, not the real install path) get rewritten to
# @rpath too -- otherwise the app can't find its own libraries.
run_stage make_relocatable.sh "${PREFIX}" "${BANDICOOT_COMPILE_PREFIX}"

# Copy clipper / mmdb2 / ssm / ccp4c / fftw2 / libc++ out of the conda
# prefix into the install tree so the tarball stands alone — users don't
# need to install those packages via conda after extracting.
run_stage bundle_conda_deps.sh "${PREFIX}" "${CONDA_PREFIX}"

# Libraries built by scripts/build_deps.sh into <repo>/deps. libtool copies these
# into lib/ at `make install` but leaves consumers pointing at the absolute build
# path, so without this step the install loads BOTH copies -- and the build-tree
# copies link Homebrew's GTK absolutely, pulling in a second complete GTK stack
# ("Class GdkQuartzView is implemented in both ..."). Runs before
# bundle_homebrew_deps.sh so that pass closes over their Homebrew dependencies.
run_stage bundle_local_deps.sh "${PREFIX}" \
    "${CANVAS_PREFIX}" "${FFTW_PREFIX}" "${REPO_ROOT}/deps"

# Bundle the gdk-pixbuf SVG loader (from Homebrew's librsvg) + all
# raster loaders so Coot's .svg toolbar icons render on testers'
# machines without requiring `brew install librsvg`. See
# scripts/bundle_pixbuf_loaders.sh header.
run_stage bundle_pixbuf_loaders.sh "${PREFIX}" "${BREW_PREFIX}"

# GTK2 input-method modules. Must run BEFORE bundle_homebrew_deps.sh so that
# script closes over whatever these modules pull in. Without them, the bundled
# libgtk dlopens Homebrew's im-quartz.so from its compiled-in module directory
# and loads a SECOND copy of gtk/gdk/gio -- see the script header.
run_stage bundle_gtk_immodules.sh "${PREFIX}" "${BREW_PREFIX}"

# Bundle the Homebrew GTK2-Quartz stack (+ transitive deps) into lib/ and
# rewrite every /opt/homebrew reference to @rpath. After this the install has
# NO external runtime dependency -- it runs on a Mac with no Homebrew at all.
# Runs after the pixbuf loaders so their Homebrew deps are closed over too.
run_stage bundle_homebrew_deps.sh "${PREFIX}" "${BREW_PREFIX}"

# Bundle external CLI tools (currently: `probe` for Local Probe Dots).
# Override the probe source via PROBE_SRC=<path>; default targets the
# CCP4 9.0.014_arm install path.
run_stage bundle_external_tools.sh "${PREFIX}" "${PROBE_SRC:-}"

# Rename the main executable coot-bin -> Bandicoot. macOS derives the
# application-menu name and Dock identity from the executable's filename
# (it ignores NSProcessInfo's mutable process name), so this is what makes
# the app present as "Bandicoot" rather than "coot-bin". Done after the
# relocation + bundling steps (which iterate Mach-O files by glob, so the
# rename is transparent to them). The coot wrapper execs libexec/Bandicoot
# and setup.sh codesigns it by this name.
if [ -f "${PREFIX}/libexec/coot-bin" ]; then
    mv -f "${PREFIX}/libexec/coot-bin" "${PREFIX}/libexec/Bandicoot"
    echo "==> renamed libexec/coot-bin -> libexec/Bandicoot"
fi

# Re-sign the Mach-O tree NOW. The relocation/bundling steps above rewrite
# load commands with install_name_tool, which invalidates each touched
# file's code signature -- and dyld SIGKILLs a process the moment it maps
# an invalidated page ("Code Signature Invalid" / bare `Killed: 9`). So a
# freshly built install crashes on launch until it is re-signed. Done here
# (after the coot-bin -> Bandicoot rename, so the entitlements are keyed to
# the final basename) rather than only in the end-user setup.sh, so a
# build.sh install runs immediately. Fails the build if anything is still
# invalid afterwards.
# RELEASE: strip debug symbols. Coot compiles with -g, which bakes every source
# file's path (/Users/<builder>/.../src/*.cc and the .o paths) into the Mach-O
# symbol table as debug-map (N_OSO) stabs -- a builder-identity leak that
# strings(1) misses but nm(1)/grep find. `strip -S` removes the debug symbols
# (and shrinks the binaries) while keeping the exported symbols dylibs need.
# MUST run before codesign (stripping invalidates signatures; codesign re-signs).
if [ -n "${BANDICOOT_RELEASE:-}" ]; then
    echo "==> stripping debug symbols (release)"
    _stripped=0
    while IFS= read -r _m; do
        file -b "$_m" 2>/dev/null | grep -q "Mach-O" || continue
        chmod u+w "$_m" 2>/dev/null || true
        strip -S "$_m" 2>/dev/null && _stripped=$((_stripped + 1)) || true
    done < <(
        find "${PREFIX}/libexec" "${PREFIX}/bin" -type f 2>/dev/null
        find "${PREFIX}/lib" -type f \( -name '*.dylib' -o -name '*.so' \) 2>/dev/null
    )
    echo "    stripped ${_stripped} Mach-O files"
fi

# v0.1.4.13: strip LC_RPATH entries pointing into a build-only prefix.
# v0.1.4.15: the implementation moved to scripts/strip_host_rpaths.sh so
# bundle_conda_deps.sh can run it too (it re-fetches wheels on every run, which
# re-introduces Pillow's CI rpath). Runs before codesign because
# install_name_tool invalidates signatures.
run_stage strip_host_rpaths.sh "${PREFIX}"

run_stage codesign-install.sh "${PREFIX}"

# Seed the gdk-pixbuf loaders.cache so a build.sh install shows its SVG toolbar
# icons immediately, without first running the end-user setup.sh. Uses the
# bundled gdk-pixbuf-query-loaders (signed just above; resolves gdk-pixbuf via
# @rpath) against the bundled loaders. The cache stores absolute loader paths,
# so setup.sh regenerates it with the correct paths on the user's machine after
# they extract the tarball -- this seed is just for the local build tree.
PIXBUF_LOADERS_DIR="${PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders"
PIXBUF_QUERY="${PREFIX}/libexec/gdk-pixbuf-query-loaders"
if [ -x "${PIXBUF_QUERY}" ] && [ -d "${PIXBUF_LOADERS_DIR}" ]; then
    echo "==> seeding gdk-pixbuf loaders.cache"
    PIXBUF_LOADER_FILES=()
    while IFS= read -r f; do PIXBUF_LOADER_FILES+=("$f"); done \
        < <(find "${PIXBUF_LOADERS_DIR}" -type f \( -name '*.so' -o -name '*.dylib' \) | sort)
    if [ "${#PIXBUF_LOADER_FILES[@]}" -gt 0 ] && \
       GDK_PIXBUF_MODULEDIR="${PIXBUF_LOADERS_DIR}" "${PIXBUF_QUERY}" \
           "${PIXBUF_LOADER_FILES[@]}" > "${PIXBUF_LOADERS_DIR}/../loaders.cache" 2>/dev/null; then
        echo "    wrote loaders.cache (${#PIXBUF_LOADER_FILES[@]} loaders)"
    else
        echo "    !! loaders.cache seed failed; setup.sh will generate it on install"
    fi
fi

# Same for the GTK2 immodules cache. Without a cache file GTK falls back to
# scanning its COMPILED-IN module directory, which is Homebrew's -- the exact
# path that pulls a second copy of gtk/gdk/gio into the process. coot.in exports
# GTK_IM_MODULE_FILE at this file, so it must exist and list only bundled paths.
IM_MODULES_DIR="${PREFIX}/lib/gtk-2.0/2.10.0/immodules"
IM_QUERY="${PREFIX}/libexec/gtk-query-immodules-2.0"
if [ -x "${IM_QUERY}" ] && [ -d "${IM_MODULES_DIR}" ]; then
    echo "==> seeding gtk.immodules"
    IM_FILES=()
    while IFS= read -r f; do IM_FILES+=("$f"); done \
        < <(find "${IM_MODULES_DIR}" -type f -name '*.so' | sort)
    if [ "${#IM_FILES[@]}" -gt 0 ] && \
       "${IM_QUERY}" "${IM_FILES[@]}" > "${IM_MODULES_DIR}/../gtk.immodules" 2>/dev/null; then
        echo "    wrote gtk.immodules (${#IM_FILES[@]} modules)"
    else
        echo "    !! gtk.immodules seed failed; setup.sh will generate it on install"
    fi
fi

# Add the bcoot symlink (the wrapper computes its prefix from $0's
# location, so a symlink in the same bin dir works).
ln -sf coot "${PREFIX}/bin/bcoot"
echo "==> created ${PREFIX}/bin/bcoot"

# Install the pandda.inspect-compatible launcher (delegates to bcoot with
# --pandda "$(pwd)"); users can alias `pandda.inspect` to it.
if [ -f "${REPO_ROOT}/scripts/bandicoot.inspect" ]; then
    cp "${REPO_ROOT}/scripts/bandicoot.inspect" "${PREFIX}/bin/bandicoot.inspect"
    chmod +x "${PREFIX}/bin/bandicoot.inspect"
    echo "==> installed ${PREFIX}/bin/bandicoot.inspect"
fi

# Asset directories: the monomer dictionary (share/coot/lib/data/monomers) and
# the reference structures (share/coot/reference-structures).
#
# v0.1.4.13: BOTH now come from data/coot-data/ INSIDE THIS REPO -- see that
# directory's README.md for provenance and the exact monomer list. Previously
# they were probed for across a list of candidate trees whose only real entry
# was ~/sw/coot-builds/coot-deps, a hand-assembled tree on one machine; every
# other candidate in the list (FFTW_PREFIX, /opt/miniconda3) carries no
# share/coot data at all, so on any other machine the probe found nothing and
# the build died at the hard gate below. Checking the data in removes the probe,
# the failure mode, and the machine dependency together.
echo "==> Coot data source: ${COOT_DATA_SRC} (checked into the repo)"

# NOTE ON THE COPY (bug fixed in v0.1.4.13): the old loop did
#     mkdir -p "${PREFIX}/share/coot/${d%/*}"
#     cp -R "${COOT_DATA_SRC}/share/coot/${d}" "${PREFIX}/share/coot/${d}"
# For d=lib/data/monomers, ${d%/*} strips to lib/data and this is correct. For
# d=reference-structures there is NO slash, so ${d%/*} leaves the string
# unchanged, mkdir created the DESTINATION, and `cp -R src dst` then copied
# INTO it -- every install since has shipped
# share/coot/reference-structures/reference-structures/*.pdb, one level too
# deep, where nothing looks for it. Copying the directory CONTENTS (src/.) into
# an explicitly pre-created destination is correct for both cases.
_copy_tree() {   # <src-dir> <dst-dir> <label>
    local src="$1" dst="$2" label="$3"
    if [ ! -d "${src}" ]; then
        echo "!! build.sh ERROR: ${label} source missing: ${src}" >&2
        exit 1
    fi
    # Re-copy even if present: a stale or half-populated destination from an
    # earlier build is exactly how the nested-directory bug survived unnoticed.
    rm -rf "${dst}"
    mkdir -p "${dst}"
    cp -R "${src}/." "${dst}/"
    echo "==> installed ${label} -> ${dst}"
}

_copy_tree "${COOT_DATA_SRC}/monomers" \
           "${PREFIX}/share/coot/lib/data/monomers" "monomer dictionary"
_copy_tree "${COOT_DATA_SRC}/reference-structures" \
           "${PREFIX}/share/coot/reference-structures" "reference structures"

# GTK theme (Raleigh) ships with Homebrew's gtk+2 itself
# ($BREW_PREFIX/share/themes/Raleigh) -- no hand-staged tree needed.
if [ ! -d "${PREFIX}/share/themes/Raleigh" ]; then
    if [ -d "${BREW_PREFIX}/share/themes/Raleigh" ]; then
        echo "==> copying Raleigh GTK theme from ${BREW_PREFIX}/share/themes/"
        mkdir -p "${PREFIX}/share/themes"
        cp -R "${BREW_PREFIX}/share/themes/Raleigh" "${PREFIX}/share/themes/Raleigh"
    else
        echo "    !! warning: Raleigh theme not found under ${BREW_PREFIX}/share/themes;" >&2
        echo "       widgets will fall back to the GTK default theme." >&2
    fi
fi

# Hard gate: refinement is dead without the monomer dictionaries -- fail the
# build rather than ship an install that can't refine.
if [ -z "$(find "${PREFIX}/share/coot/lib/data/monomers" -name '*.cif' -print -quit 2>/dev/null)" ]; then
    echo "!! build.sh ERROR: monomer dictionary library MISSING from the install." >&2
    echo "   (share/coot/lib/data/monomers/ has no .cif files -> refinement would" >&2
    echo "    fail with 'No dictionary group found for residue type'.) No probed" >&2
    echo "    source tree had the data. Point COOT_DATA_SRC_OVERRIDE=<dir> at a" >&2
    echo "    Coot 0.9 install whose share/coot/lib/data/monomers/ exists." >&2
    exit 1
fi
echo "==> monomer dictionary OK: $(find "${PREFIX}/share/coot/lib/data/monomers" -name '*.cif' | wc -l | tr -d ' ') cif files"

# Drop the end-user setup script + install instructions at the install
# root so the tarball ships with them. Users extract, read INSTALL.md,
# and run ./setup.sh once.
if [ -f "${REPO_ROOT}/scripts/setup-install.sh" ]; then
    cp "${REPO_ROOT}/scripts/setup-install.sh" "${PREFIX}/setup.sh"
    chmod +x "${PREFIX}/setup.sh"
fi
# Ship the signing helper next to setup.sh so the extracted tarball can
# re-sign itself (setup.sh delegates to it when present).
if [ -f "${REPO_ROOT}/scripts/codesign-install.sh" ]; then
    cp "${REPO_ROOT}/scripts/codesign-install.sh" "${PREFIX}/codesign-install.sh"
    chmod +x "${PREFIX}/codesign-install.sh"
fi
if [ -f "${REPO_ROOT}/INSTALL.md" ]; then
    cp "${REPO_ROOT}/INSTALL.md" "${PREFIX}/INSTALL.md"
fi
if [ -f "${REPO_ROOT}/KEY_SHORTCUTS.md" ]; then
    cp "${REPO_ROOT}/KEY_SHORTCUTS.md" "${PREFIX}/KEY_SHORTCUTS.md"
fi

# Hard gate: dependency closure. Same spirit as the monomer-dictionary gate
# above -- refuse to hand over an install that is already broken.
#
# v0.1.4.11: added because until now this check lived ONLY in package.sh, so it
# ran when rolling a tarball and never on a plain ./scripts/build.sh. Two
# consequences, both real: a dev build could sit here for weeks with a Python
# extension that cannot dlopen (that is how the missing libexpat behind GitHub
# #8 survived -- pyexpat silently bound to the host's /usr/lib copy), and a
# third party building from source and packaging their own way -- SBGrid lays
# the tree out as /programs/<platform>/bandicoot/<version>/ -- never got the
# gate at all. Running it here means anyone who builds Bandicoot gets it.
#
# This is as early as the check can be correct: the closure is not complete
# until make_relocatable + all four bundle_*.sh + codesign have run, so it
# cannot move further up. Read-only, and ~12 s on a full tree (it runs otool -l
# over ~270 Mach-O files) -- negligible next to the compile, but noticeable on
# an incremental no-op build.
#
# ARMED for dev builds too, deliberately -- an unresolved dep is a defect at
# any stage, and the whole lesson of #8 is that this class hides until a user
# on a different machine trips over it. Set BUILD_SKIP_CHECKS=1 for a knowingly
# partial tree; do not make it a habit.
if [ "${BUILD_SKIP_CHECKS:-0}" != "1" ]; then
    if ! run_stage check_install.sh "${PREFIX}"; then
        echo "!! build.sh ERROR: the install has an unresolved or build-host" >&2
        echo "   dependency (see the UNRESOLVED / HOST-LEAK lines above)." >&2
        echo "   UNRESOLVED means a shipped Mach-O names a library we do not ship." >&2
        echo "   It may still appear to work here: when an @rpath lookup fails," >&2
        echo "   dyld falls back to /usr/lib, so the module silently binds to" >&2
        echo "   whatever this Mac happens to have -- and breaks on a user's." >&2
        echo "   Fix it by bundling the library (scripts/bundle_conda_deps.sh for" >&2
        echo "   conda-provided ones, bundle_homebrew_deps.sh for Homebrew), not" >&2
        echo "   by skipping this gate." >&2
        exit 1
    fi
fi

# Report the version this build actually produced, in the SAME form the running
# app shows, so "did I just install what I'm looking at?" is answerable by eye.
# The title bar is assembled at src/main.cc:373 as
#     "BANDICOOT " BANDICOOT_VERSION BANDICOOT_BUILD_SUFFIX " (Coot " VERSION ")"
# so echo that string verbatim rather than an approximation of it. BUILD_SUFFIX is
# the "-u<N>" set by the build-counter block near the top of this script, and is
# empty for BANDICOOT_RELEASE=1 builds -- so a release prints a bare version, which
# is exactly what its title bar will read.
#
# NOTE the suffix is only bumped/regenerated by THIS script. A bare `make` reuses
# whatever src/bandicoot-build-id.h already holds, so a plain make does not advance
# -u<N> and the title bar keeps the previous number.
_bcoot_coot_ver="$(awk -F'= *' '/^VERSION = /{print $2; exit}' Makefile 2>/dev/null)"
_bcoot_full_ver="${BANDICOOT_VERSION_STR}${BUILD_SUFFIX}"

echo ""
echo "Bandicoot installed in ${PREFIX}"
echo "Launch with: ${PREFIX}/bin/bcoot"
echo ""
echo "Version built: ${_bcoot_full_ver}"
echo "  title bar will read:  BANDICOOT ${_bcoot_full_ver} (Coot ${_bcoot_coot_ver})"

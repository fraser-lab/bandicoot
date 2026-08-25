#!/bin/bash
# ---------------------------------------------------------------------------
# INVOKED AUTOMATICALLY BY scripts/build.sh -- you do NOT need to run this by
# hand. (GitHub #24: a builder read these headers cold, concluded they were a
# manual sequence, and ran them individually -- which is what kept
# re-introducing a stale rpath.)
# ---------------------------------------------------------------------------
# Copy conda-provided runtime libraries (clipper, mmdb2, ssm, ccp4c,
# fftw2, libc++) into bandicoot's lib/ and rewrite their install_names
# and inter-library references to @rpath/<basename>, so the install no
# longer depends on /opt/miniconda3/ at runtime. Also drops the conda
# rpath from coot-bin so it can't accidentally pick up an external
# version.
#
# Run once after `make install` + `make_relocatable.sh`. build.sh does
# this automatically.
#
# Usage:
#   ./scripts/bundle_conda_deps.sh /path/to/install [/path/to/conda]
#
# Defaults: CONDA_PREFIX env var, else /opt/miniconda3.

set -e

PREFIX="${1:?Usage: $0 <install-prefix> [conda-prefix]}"
PREFIX="$(cd "$PREFIX" && pwd)"
CONDA_PREFIX_ARG="${2:-${CONDA_PREFIX:-/opt/miniconda3}}"
# v0.1.0.2: separate prefix for libgnomecanvas-2.0 + libart_lgpl_2 built
# from source (archived GNOME 2 libs, not in Homebrew). Override via
# CANVAS_DEPS_PREFIX env var.
CANVAS_DEPS_PREFIX="${CANVAS_DEPS_PREFIX:-$HOME/sw/canvas-deps}"

[ -d "$PREFIX/lib" ]              || { echo "error: $PREFIX/lib missing" >&2; exit 1; }
[ -d "$CONDA_PREFIX_ARG/lib" ]    || { echo "error: $CONDA_PREFIX_ARG/lib missing" >&2; exit 1; }

# v0.1.4.15: ASK the interpreter for its version instead of hard-coding one.
# `libpython3.13.dylib` used to be a literal in TOPLEVEL_LIBS below, which meant
# a builder whose conda was on any other minor version got no libpython bundled
# at all and ~43 unresolved @rpath references (GitHub #24: current Miniforge
# bases ship 3.14). It also forced them to create a 3.13 environment and pass
# CONDA_PREFIX by hand, contradicting BUILD.md's "install into base".
# Bandicoot does not care WHICH 3.x it embeds -- only that every path agrees.
PY_VER="$("$CONDA_PREFIX_ARG/bin/python3" -c \
    'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null || true)"
if [ -z "$PY_VER" ]; then
    echo "error: cannot determine Python version from $CONDA_PREFIX_ARG/bin/python3" >&2
    exit 1
fi
echo "==> embedded Python version: $PY_VER (from $CONDA_PREFIX_ARG/bin/python3)"

# Libraries to copy from $CONDA_PREFIX/lib/ — the .dylib basenames
# bandicoot's libcoot-*.dylib and coot-bin reference via @rpath/.
TOPLEVEL_LIBS=(
    libclipper-core.2.dylib
    libclipper-ccp4.2.dylib
    libclipper-cif.2.dylib
    libclipper-cns.2.dylib
    libclipper-contrib.2.dylib
    libclipper-minimol.2.dylib
    libclipper-mmdb.2.dylib
    libclipper-phs.2.dylib
    libmmdb2.0.dylib
    libssm.2.dylib
    libccp4c.0.dylib
    libc++.1.dylib
    libintl.8.dylib
    libiconv.2.dylib
    # v0.1.1.3: previously resolved only via the /opt/miniconda3/lib rpath,
    # so launch failed for any user whose conda isn't at that exact path
    # (beta-tester crash: dyld "Library not loaded: @rpath/libpng16.16.dylib").
    # libpython3.X (embedded Python, added v0.1.0.0) has no deps but libSystem;
    # libpng16 / libfreetype6 only pull in libz (rewritten to system /usr/lib).
    libpng16.16.dylib
    libfreetype.6.dylib
    "libpython${PY_VER}.dylib"
    # v0.1.4.9: OpenSSL, needed by the stdlib _ssl and _hashlib C extensions
    # in lib-dynload/. Both record @rpath/libssl.3.dylib + @rpath/libcrypto.3.dylib
    # (lib-dynload's rpath is @loader_path/../../ = our lib/). Regression window:
    # up to v0.1.4.1 the interpreter borrowed the WHOLE stdlib from an external
    # conda via PYTHONHOME, so _ssl loaded from conda/lib-dynload/ and resolved
    # OpenSSL from conda/lib/ -- Bandicoot never shipped it. v0.1.4.2 bundled the
    # stdlib and set PYTHONHOME=$COOT_PREFIX, relocating _ssl next to our lib/,
    # which had no OpenSSL -- so every SSL-using script has crashed since 0.1.4.2
    # with dyld "Library not loaded: @rpath/libssl.3.dylib" (e.g. get_ebi_pdb()
    # fetching a PDB by accession code). conda's libssl already references
    # @rpath/libcrypto.3.dylib, so the generic id/ref rewriting below is enough.
    libssl.3.dylib
    libcrypto.3.dylib
    # v0.1.4.9: libffi, needed by the stdlib _ctypes extension (@rpath/libffi.8.dylib).
    # Same unbundled-dep class as _ssl above -- import ctypes crashed with dyld
    # "Library not loaded: @rpath/libffi.8.dylib". macOS has no on-disk libffi.8
    # to rewrite to, so bundle conda's.
    libffi.8.dylib
    # v0.1.4.9: libsqlite3, needed by the stdlib _sqlite3 extension. NOTE we do
    # NOT rewrite _sqlite3's @rpath/libsqlite3.dylib to the system /usr/lib copy
    # (the way coot-bin's C++ libsqlite3 ref is handled below): macOS's system
    # libsqlite3 is built with SQLITE_OMIT_LOAD_EXTENSION, so import sqlite3
    # against it dies with "Symbol not found: _sqlite3_enable_load_extension".
    # conda's _sqlite3.so was built against conda's libsqlite3, which exports it,
    # so bundle that and let _sqlite3 resolve it via @rpath from our lib/.
    libsqlite3.dylib
    # v0.1.4.11: libexpat, needed by the stdlib pyexpat extension. GitHub #8
    # (SBGrid, bandicoot 0.1.4.10): "Symbol not found:
    # _XML_SetAllocTrackerActivationThreshold ... Expected in:
    # /usr/lib/libexpat.1.dylib", ending in ElementTree's "No module named
    # expat; use SimpleXMLTreeBuilder instead" -- i.e. all XML parsing dead
    # (parse_wwpdb_validation_xml() in pdbe_validation_data.py).
    #
    # This one did NOT fail loudly on the build host, which is why it shipped:
    # pyexpat.so records @rpath/libexpat.1.dylib, lib-dynload's rpath is
    # @loader_path/../../ = our lib/, and we shipped no libexpat there. When an
    # @rpath lookup finds nothing, dyld falls back to DYLD_FALLBACK_LIBRARY_PATH,
    # whose default ends in /usr/lib -- so pyexpat silently bound to whatever
    # expat Apple happens to ship on that machine. XML_SetAllocTrackerActivation-
    # Threshold only appeared in expat 2.7.2, and conda's expat (2.7.4) is what
    # pyexpat.so was compiled against, so the ref is hard. macOS 26.5 ships
    # 2.7.4 (works, hence invisible here); anything older -- including earlier
    # Tahoe point releases, cf. Homebrew/homebrew-core#277330 -- errors out.
    # Bundling conda's libexpat makes the @rpath lookup succeed, so the system
    # copy is never consulted and the module no longer depends on the user's
    # macOS version. Same reasoning as the libsqlite3 note above.
    libexpat.1.dylib
    # v0.1.4.11: libmpdec, needed by the stdlib _decimal extension. Missing
    # since v0.1.4.2 and MASKED, not crashed: decimal.py does
    # `try: from _decimal import * / except ImportError: from _pydecimal import *`,
    # so `import decimal` kept working on the pure-Python fallback (~100x slower
    # arithmetic) and smoke_test_imports.sh's `decimal` probe passed anyway.
    # Unlike libexpat there is no system libmpdec to fall back to, so the C
    # accelerator has never once loaded in a shipped build.
    libmpdec.4.dylib
    # v0.1.4.11: libbz2, needed by the stdlib _bz2 extension (bz2 is CRITICAL in
    # smoke_test_imports.sh). Was resolving only via the /usr/lib dyld fallback
    # described above; bundle it so the bundled module has a bundled dep like
    # every other one, rather than depending on the host's bzip2.
    libbz2.dylib
    # v0.1.4.11: readline + its ncurses closure, needed by the stdlib readline
    # and _curses/_curses_panel extensions. These are OPTIONAL in
    # smoke_test_imports.sh and have never loaded in a shipped build, so nothing
    # regresses by fixing them -- but readline is what gives `coot
    # --no-graphics` a usable interactive Python prompt (history, line editing),
    # and the closure is ~900 kB. NOTE conda's libncursesw records an ABSOLUTE
    # /opt/miniconda3/lib/libtinfow.6.dylib, so libtinfow must be bundled too or
    # check_install.sh reports a HOST-LEAK (the step-3 rewrite below only
    # rewrites conda paths for libs listed here).
    libreadline.8.dylib
    libncursesw.6.dylib
    libtinfow.6.dylib
    libpanelw.6.dylib
)

# FFTW2 single-precision lives in a sub-prefix under conda (artefact of
# the clipper conda package layout).
FFTW_LIBS=(
    libfftw.2.dylib
    librfftw.2.dylib
)

echo "==> bundling external runtime libs into $PREFIX/lib/"

copy_lib() {
    local src="$1"
    local dst="$PREFIX/lib/$(basename "$src")"
    if [ ! -f "$src" ]; then
        echo "    WARN: $src not found; skipping" >&2
        return 1
    fi
    cp -f "$src" "$dst"
    chmod u+w "$dst"
    echo "    copied $(basename "$src")"
}

for lib in "${TOPLEVEL_LIBS[@]}"; do
    copy_lib "$CONDA_PREFIX_ARG/lib/$lib" || true
done
for lib in "${FFTW_LIBS[@]}"; do
    copy_lib "$CONDA_PREFIX_ARG/fftw2/lib/$lib" || true
done

# v0.1.0.2: libgnomecanvas-2.0 + libart_lgpl_2 from $CANVAS_DEPS_PREFIX.
CANVAS_LIBS=(
    libgnomecanvas-2.0.dylib
    libart_lgpl_2.2.dylib
    libgoocanvas.3.dylib
)
if [ -d "$CANVAS_DEPS_PREFIX/lib" ]; then
    for lib in "${CANVAS_LIBS[@]}"; do
        copy_lib "$CANVAS_DEPS_PREFIX/lib/$lib" || true
    done
fi

echo "==> rewriting install_names and inter-library references"

ALL_BUNDLED=("${TOPLEVEL_LIBS[@]}" "${FFTW_LIBS[@]}" "${CANVAS_LIBS[@]}")

for lib in "${ALL_BUNDLED[@]}"; do
    path="$PREFIX/lib/$lib"
    [ -f "$path" ] || continue
    chmod u+w "$path"

    # 1) set install_name to @rpath/<basename> (so anything linking
    #    against this lib in the future records the relocatable form).
    install_name_tool -id "@rpath/$lib" "$path" 2>/dev/null || true

    # 2) Rewrite the unusual @rpath/../fftw2/lib/<x> references that
    #    clipper-core et al. use to point into the conda fftw2 sub-prefix.
    #    Flatten them to @rpath/<x> so they resolve in our single lib/ dir.
    for fftw in "${FFTW_LIBS[@]}"; do
        install_name_tool -change "@rpath/../fftw2/lib/$fftw" \
                                  "@rpath/$fftw" "$path" 2>/dev/null || true
    done

    # 3) Catch any absolute conda paths that escaped @rpath rewriting,
    #    for either /lib/ or /fftw2/lib/.
    for sib in "${ALL_BUNDLED[@]}"; do
        install_name_tool -change "$CONDA_PREFIX_ARG/lib/$sib" \
                                  "@rpath/$sib" "$path" 2>/dev/null || true
        install_name_tool -change "$CONDA_PREFIX_ARG/fftw2/lib/$sib" \
                                  "@rpath/$sib" "$path" 2>/dev/null || true
    done

    # 4) Drop the @loader_path/../../lib/ rpath from fftw2 libs — it was
    #    pointing back at the conda /lib from inside conda's /fftw2/lib;
    #    after the move it would resolve outside the install tree.
    while otool -l "$path" 2>/dev/null | \
          awk '/LC_RPATH/{i=1;next} i && /path /{print $2; i=0}' | \
          grep -q '../../lib'; do
        install_name_tool -delete_rpath "@loader_path/../../lib/" \
                          "$path" 2>/dev/null || break
    done
done

echo "==> rewriting remaining external @rpath refs to absolute paths"

# Conda-built coot-bin records @rpath/libcurl.4.dylib, libsqlite3.dylib,
# libiconv.2.dylib as its dep paths because the conda libraries set
# their install_names that way. With the conda rpath removed those
# would fail dyld lookup, and bundling libcurl's transitive closure
# (openssl, ssh2, idn2, brotli, zstd, nghttp2, kerberos, ...) is more
# weight than is warranted. macOS ships system libcurl / libsqlite3 /
# libiconv in the dyld cache at /usr/lib/<x>, which are ABI-stable and
# the right call here.
EXTERNAL_REWRITES=(
    "libcurl.4.dylib=/usr/lib/libcurl.4.dylib"
    "libsqlite3.dylib=/usr/lib/libsqlite3.dylib"
    # bundled libpng16 / libfreetype6 reference @rpath/libz.1.dylib; macOS
    # ships an ABI-stable libz in the dyld cache, so don't bundle it.
    "libz.1.dylib=/usr/lib/libz.1.dylib"
)

rewrite_external_in() {
    local file="$1"
    file -b "$file" 2>/dev/null | grep -q "Mach-O" || return 0
    chmod u+w "$file" 2>/dev/null
    for pair in "${EXTERNAL_REWRITES[@]}"; do
        local from="${pair%%=*}"
        local to="${pair#*=}"
        install_name_tool -change "@rpath/$from" "$to" "$file" 2>/dev/null || true
    done
}

for d in libexec bin; do
    [ -d "$PREFIX/$d" ] || continue
    while IFS= read -r f; do rewrite_external_in "$f"; done \
        < <(find "$PREFIX/$d" -type f -perm -u+x)
done

# Apply to bundled dylibs too — libintl depends on @rpath/libiconv.2.dylib,
# and various libcoot-*.dylib may pull in libsqlite3 / libcurl.
for dylib in "$PREFIX"/lib/*.dylib; do
    [ -f "$dylib" ] || continue
    rewrite_external_in "$dylib"
done

# v0.1.0.2: rewrite absolute $CANVAS_DEPS_PREFIX/lib/ refs in executables
# and bundled dylibs to @rpath/<basename>. coot-bin links via the absolute
# path because we built libgnomecanvas-2.0 + libart_lgpl_2 into a custom
# prefix; without this they'd fail dyld lookup once moved off the build host.
rewrite_canvas_in() {
    local file="$1"
    file -b "$file" 2>/dev/null | grep -q "Mach-O" || return 0
    chmod u+w "$file" 2>/dev/null
    for lib in "${CANVAS_LIBS[@]}"; do
        install_name_tool -change "$CANVAS_DEPS_PREFIX/lib/$lib" \
                                  "@rpath/$lib" "$file" 2>/dev/null || true
    done
}
for d in libexec bin lib; do
    [ -d "$PREFIX/$d" ] || continue
    while IFS= read -r f; do rewrite_canvas_in "$f"; done \
        < <(find "$PREFIX/$d" -type f)
done

echo "==> stripping conda rpath from binaries in libexec/ and bin/"
# Bandicoot v0.1.1.3: libpython3.13 / libpng16 / libfreetype6 are now bundled
# in lib/ (above), so NOTHING needs to resolve from the conda lib dir at
# runtime anymore. Earlier (v0.1.0.0..v0.1.1.2) we deliberately KEPT a single
# /opt/miniconda3/lib LC_RPATH so embedded Python's libpython3.13 would load.
# That rpath silently MASKED the missing bundled libs on the build machine
# (which has conda at /opt/miniconda3) and made the app crash on every user
# whose conda lives elsewhere — the libpng16.16 "Library not loaded" report.
# Removing it makes the install genuinely conda-independent at runtime AND
# means any future unbundled conda dep fails loudly on the build host instead
# of shipping broken. Delete every copy of the conda-lib rpath (rebuilds can
# accumulate duplicates); leave @executable_path/../lib untouched.

for d in libexec bin; do
    [ -d "$PREFIX/$d" ] || continue
    while IFS= read -r f; do
        file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
        chmod u+w "$f"
        while otool -l "$f" 2>/dev/null | \
              awk '/LC_RPATH/{i=1;next} i && /path /{print $2; i=0}' | \
              grep -qx "$CONDA_PREFIX_ARG/lib"; do
            install_name_tool -delete_rpath "$CONDA_PREFIX_ARG/lib" "$f" 2>/dev/null || break
        done
    done < <(find "$PREFIX/$d" -type f -perm -u+x)
done

# The bundled/relocated dylibs in lib/ may also carry a conda rpath (libcoot-*
# do). Strip it there too so the whole tree is conda-free.
for dylib in "$PREFIX"/lib/*.dylib; do
    [ -f "$dylib" ] || continue
    chmod u+w "$dylib"
    while otool -l "$dylib" 2>/dev/null | \
          awk '/LC_RPATH/{i=1;next} i && /path /{print $2; i=0}' | \
          grep -qx "$CONDA_PREFIX_ARG/lib"; do
        install_name_tool -delete_rpath "$CONDA_PREFIX_ARG/lib" "$dylib" 2>/dev/null || break
    done
done

# ---------------------------------------------------------------------------
# v0.1.4.2: bundle the Python standard library.
#
# libpython3.13.dylib (the interpreter engine) is copied above, but the
# interpreter still needs its standard library at runtime: encodings/, os.py,
# and the compiled C extensions in lib-dynload/. Builds up to v0.1.4.1 left
# that to an external conda via the launcher's PYTHONHOME, so any user without
# a matching conda crashed at Python init with "No module named 'encodings'".
# Copy the stdlib into $PREFIX/lib/python3.X/ -- alongside Bandicoot's own
# site-packages, which is already installed there and must NOT be clobbered --
# so the launcher can set PYTHONHOME=$COOT_PREFIX and the install stands alone.
echo "==> bundling Python standard library"

PY_STDLIB_SRC=""
for _d in "$CONDA_PREFIX_ARG"/lib/python3.*; do
    [ -d "$_d/encodings" ] && PY_STDLIB_SRC="$_d" && break
done

if [ -z "$PY_STDLIB_SRC" ]; then
    echo "    WARN: no python3.* stdlib found under $CONDA_PREFIX_ARG/lib; skipping" >&2
else
    PY_VER_DIR="$(basename "$PY_STDLIB_SRC")"        # e.g. python3.13
    PY_STDLIB_DST="$PREFIX/lib/$PY_VER_DIR"
    mkdir -p "$PY_STDLIB_DST"
    # Copy the stdlib but NOT conda's site-packages (Bandicoot's own already
    # lives at the destination), and trim dev/test/unused trees to keep the
    # tarball slim. lib-dynload/ (compiled C extension modules) is essential
    # and is copied. None of the excluded packages are imported by Coot's GTK
    # Python layer. NOTE: optional C extensions in lib-dynload/ (_ssl, _hashlib,
    # _sqlite3, _lzma, _bz2, ...) still record conda dep paths; Python boots
    # fine without them (encodings is pure-Python, _codecs is built in), they
    # only fail if imported. Bundling their transitive deps (openssl, ...) is a
    # separate follow-up if any Coot script needs them at runtime.
    rsync -a \
        --exclude 'site-packages' \
        --exclude '__pycache__' \
        --exclude '*.pyc' \
        --exclude 'test' \
        --exclude 'tests' \
        --exclude 'idlelib' \
        --exclude 'tkinter' \
        --exclude 'turtledemo' \
        --exclude 'lib2to3' \
        --exclude 'ensurepip' \
        --exclude 'config-*' \
        "$PY_STDLIB_SRC/" "$PY_STDLIB_DST/"
    echo "    bundled $PY_VER_DIR stdlib -> $PY_STDLIB_DST (site-packages preserved)"
fi
unset _d PY_STDLIB_SRC PY_VER_DIR PY_STDLIB_DST

# ---------------------------------------------------------------------------
# v0.1.4.9: point the Python C extensions' libz refs at the system copy.
# binascii/zlib record @rpath/libz.1.dylib; macOS ships an ABI-stable libz in
# the dyld cache, so /usr/lib is the right target (matches how bundled png/
# freetype are handled). This does NOT use the full rewrite set: _sqlite3's
# @rpath/libsqlite3.dylib must be left alone so it resolves to the BUNDLED
# conda libsqlite3 in lib/ (the system copy lacks a symbol it needs -- see the
# libsqlite3 note in TOPLEVEL_LIBS). _ssl/_hashlib/_ctypes/pyexpat/_decimal
# @rpath refs are likewise left alone to resolve against what we bundled.
#
# v0.1.4.11 -- ORDERING FIX: this pass used to sit ~70 lines up, BEFORE the
# stdlib copy above created lib-dynload/ at all. The glob matched nothing, the
# loop body never ran, and it has been dead code since v0.1.4.9 (shipped
# 0.1.4.10's zlib/binascii still record @rpath/libz.1.dylib). It must run
# AFTER the stdlib is in place -- keep it below the rsync.
echo "==> retargeting Python C-extension libz refs at the system copy"
for so in "$PREFIX"/lib/python3.*/lib-dynload/*.so; do
    [ -f "$so" ] || continue
    file -b "$so" 2>/dev/null | grep -q "Mach-O" || continue
    chmod u+w "$so" 2>/dev/null
    install_name_tool -change "@rpath/libz.1.dylib" "/usr/lib/libz.1.dylib" "$so" 2>/dev/null || true
done

# v0.1.4.11: drop _tkinter. The stdlib copy above already excludes the `tkinter`
# package (and idlelib/turtledemo), so the extension module can never be
# imported by anything -- but it records @rpath/libtcl8.6.dylib +
# @rpath/libtk8.6.dylib, which would be the only unresolved deps left in the
# tree and would keep check_install.sh's UNRESOLVED gate disarmed. Bundling
# Tcl/Tk instead would be >10 MB for a module nothing uses (matplotlib runs on
# a non-Tk backend here). Removing it turns a dyld ImportError into a clean
# ModuleNotFoundError.
for so in "$PREFIX"/lib/python3.*/lib-dynload/_tkinter*.so; do
    [ -f "$so" ] || continue
    rm -f "$so"
    echo "    removed $(basename "$so") (tkinter package is not bundled)"
done

# ---------------------------------------------------------------------------
# v0.1.4.2: bundle numpy + matplotlib (self-contained PyPI wheels).
#
# The launcher sets PYTHONHOME=$COOT_PREFIX, so the embedded interpreter's
# site-packages is $PREFIX/lib/python3.X/site-packages. Two shipped Python
# features need third-party scientific packages there:
#   * numpy      -- REQUIRED by the PanDDA map-tiling fix (_fix_ccp4_map in
#                   bandicoot_pandda.py); without it the .ccp4 event map loads
#                   un-retiled and sits off the model.
#   * matplotlib -- drives the pathology plots.
# Up to v0.1.4.1 these resolved from the external conda via PYTHONHOME; the
# v0.1.4.2 self-contained-stdlib switch cut them off, so we bundle them here.
#
# We pull SELF-CONTAINED PyPI wheels, NOT conda's copies: conda's numpy
# @rpath-links libcblas/liblapack into the conda prefix (not relocatable),
# whereas PyPI wheels vendor OpenBLAS/libjpeg/... under <pkg>.libs/ with
# @loader_path refs, so they import with no conda present. Installing
# matplotlib pulls its closure (numpy, Pillow, contourpy, kiwisolver,
# fonttools, ...) in one resolve. --only-binary=:all: forbids source builds
# (those would link build-host libs and break relocatability). The bundled
# .so / vendored .dylib get ad-hoc-signed by setup.sh's codesign pass, which
# scans $PREFIX/lib recursively (site-packages lives under it).
echo "==> bundling numpy + matplotlib (PyPI wheels) into site-packages"
PY_SITE=""
for _d in "$PREFIX"/lib/python3.*/site-packages; do
    [ -d "$_d" ] && PY_SITE="$_d" && break
done
PIP_PY="$CONDA_PREFIX_ARG/bin/python3"
if [ -z "$PY_SITE" ]; then
    echo "    WARN: no site-packages dir under $PREFIX/lib; skipping" >&2
elif [ ! -x "$PIP_PY" ] || ! "$PIP_PY" -m pip --version >/dev/null 2>&1; then
    echo "    WARN: $PIP_PY has no usable pip; skipping numpy/matplotlib bundle" >&2
else
    "$PIP_PY" -m pip install --quiet --disable-pip-version-check \
        --target="$PY_SITE" --upgrade --only-binary=:all: \
        numpy matplotlib
    # pip --target drops console-script shims (f2py, fonttools, ...) into
    # <target>/bin with the build host's shebang -- useless inside the bundle.
    rm -rf "$PY_SITE/bin"
    # __pycache__ is regenerated on first import; don't ship it.
    find "$PY_SITE" -type d -name '__pycache__' -prune -exec rm -rf {} + 2>/dev/null || true
    echo "    bundled into $PY_SITE: $(ls "$PY_SITE" | grep -viE '^coot|\.dist-info$|\.pth$' | tr '\n' ' ')"
fi
unset _d PY_SITE PIP_PY

# ---------------------------------------------------------------------------
# v0.1.4.15: transitive-closure sweep.
#
# TOPLEVEL_LIBS above is a HAND-MAINTAINED list, and every entry after the
# original batch was added the same way: something failed on a user's machine
# and the missing library was appended (libssl/libcrypto, libffi, libsqlite3,
# libexpat, libmpdec, libbz2, the readline/ncurses closure...). GitHub #24 added
# five more -- libncurses.6 + libtinfo.6 (conda-forge's readline links the NARROW
# ncurses; the list only names the wide libncursesw/libtinfow) and the three ICU
# libraries that conda-forge's sqlite pulls in but the defaults-channel build
# does not.
#
# That last pair is the point: the correct set DEPENDS ON WHICH CONDA THE
# BUILDER USES. On this machine (Miniconda, defaults) libsqlite3 needs no ICU at
# all and libreadline wants libncursesw; on conda-forge/Miniforge both differ.
# A static list cannot be right for both, and adding names as bug reports arrive
# only ever fixes the last person's build.
#
# So: walk what is actually there. For every Mach-O in the install, any
# dependency naming a library we have not bundled but conda has gets copied in,
# and the walk repeats until nothing new appears (its dependencies count too).
# The hand list is still the starting point -- it carries the deliberate
# decisions (bundle conda's libsqlite3 rather than the system one, and so on) --
# but it is no longer the whole truth.
echo "==> sweeping up transitive conda dependencies"

# Deps deliberately resolved to the OS copy rather than bundled (EXTERNAL_REWRITES
# above already points them at /usr/lib) -- never drag them back in. libsqlite3 is
# NOT here: it is genuinely bundled for the stdlib _sqlite3 extension and only
# coot-bin's own reference is rewritten to the system copy.
SWEEP_SKIP=( libcurl.4.dylib libz.1.dylib )

# Point a referrer at the bundled copy when it names a library by full conda path.
sweep_relink() {   # <referrer> <dep-as-recorded> <basename>
    case "$2" in
        "$CONDA_PREFIX_ARG"/lib/*)
            chmod u+w "$1" 2>/dev/null || true
            install_name_tool -change "$2" "@rpath/$3" "$1" 2>/dev/null || true
            ;;
    esac
}

# One pass over the install. Progress goes to stderr; the ONLY thing on stdout
# is the number copied, so the caller can drive the fixed-point loop with it.
sweep_round() {
    local n=0 f dep name skip s
    while IFS= read -r f; do
        file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
        while IFS= read -r dep; do
            case "$dep" in
                @rpath/*)                  name="${dep#@rpath/}" ;;
                "$CONDA_PREFIX_ARG"/lib/*) name="${dep##*/}" ;;
                *) continue ;;
            esac
            [ -n "$name" ] || continue
            case "$name" in */*) continue ;; esac      # nested @rpath/../ form
            if [ -e "$PREFIX/lib/$name" ]; then
                sweep_relink "$f" "$dep" "$name"       # have it; just fix the ref
                continue
            fi
            skip=0
            for s in "${SWEEP_SKIP[@]}"; do [ "$name" = "$s" ] && skip=1; done
            [ "$skip" = 1 ] && continue
            [ -f "$CONDA_PREFIX_ARG/lib/$name" ] || continue   # not conda's to give
            cp -f "$CONDA_PREFIX_ARG/lib/$name" "$PREFIX/lib/$name"
            chmod u+w "$PREFIX/lib/$name"
            install_name_tool -id "@rpath/$name" "$PREFIX/lib/$name" 2>/dev/null || true
            # Strip any conda LC_RPATH the newcomer carries, or check_install.sh
            # reports it as a HOST-RPATH leak.
            while otool -l "$PREFIX/lib/$name" 2>/dev/null | \
                  awk '/LC_RPATH/{i=1;next} i && /path /{print $2; i=0}' | \
                  grep -qx "$CONDA_PREFIX_ARG/lib"; do
                install_name_tool -delete_rpath "$CONDA_PREFIX_ARG/lib" \
                                  "$PREFIX/lib/$name" 2>/dev/null || break
            done
            sweep_relink "$f" "$dep" "$name"
            echo "    swept in $name  (needed by ${f#$PREFIX/})" >&2
            n=$((n + 1))
        done < <(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}')
    done < <(find "$PREFIX/lib" "$PREFIX/libexec" "$PREFIX/bin" \
                  \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) \
                  -type f 2>/dev/null)
    echo "$n"
}

# Fixed point: a swept-in library brings its own dependencies, so repeat until a
# round finds nothing. The bound is a runaway guard, not an expected limit --
# the deepest real chain so far is readline -> ncurses -> tinfo (3 rounds).
_sweep_total=0
for _round in 1 2 3 4 5 6 7 8; do
    _added="$(sweep_round)"
    case "$_added" in ''|*[!0-9]*) _added=0 ;; esac
    _sweep_total=$((_sweep_total + _added))
    [ "$_added" = "0" ] && break
done
if [ "$_sweep_total" = "0" ]; then
    echo "    nothing missing -- the hand list already covered this conda"
else
    echo "    swept in $_sweep_total transitive lib(s) the hand list did not name"
fi
unset _sweep_total _added _round

# v0.1.4.15: strip foreign LC_RPATHs. The pip step above re-fetches the
# numpy/matplotlib wheels on EVERY run, and Pillow's arm64 wheel carries its CI
# machine's rpath (/Users/runner/work/Pillow/...) on the vendored
# .dylibs/libjpeg -- so re-running this script alone (while adding a missing
# library to TOPLEVEL_LIBS, say) re-introduces it. build.sh runs the same pass
# later over the whole tree; running it here as well means the bundler leaves a
# clean install whether or not build.sh drives it. Same script both times, so
# there is one implementation of the rule.
echo "==> stripping foreign rpaths from the bundled tree"
"$(dirname "$0")/strip_host_rpaths.sh" "$PREFIX" || true

echo "==> bundle_conda_deps: done"

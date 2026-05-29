#!/bin/bash
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

echo "==> normalising conda rpath on binaries in libexec/ and bin/"
# Bandicoot v0.1.0.0: we now ship with embedded Python (--with-python), so
# libpython3.13.dylib must resolve via the conda lib dir at runtime. Keep
# exactly one conda-lib LC_RPATH on every executable. Earlier versions
# stripped the rpath entirely (because Coot's other deps were all bundled
# via @rpath/@executable_path); now Python forces the conda dep back in.
# Lab members all have conda installed for Phenix/CCP4 anyway, so this
# remains acceptable.

for d in libexec bin; do
    [ -d "$PREFIX/$d" ] || continue
    while IFS= read -r f; do
        file -b "$f" 2>/dev/null | grep -q "Mach-O" || continue
        chmod u+w "$f"
        # Collapse to exactly one conda rpath: delete duplicates, then add
        # one if missing. (install_name_tool can otherwise accumulate them
        # across rebuilds.)
        while [ "$(otool -l "$f" 2>/dev/null | \
                   awk '/LC_RPATH/{i=1;next} i && /path /{print $2; i=0}' | \
                   grep -cx "$CONDA_PREFIX_ARG/lib")" -gt 1 ]; do
            install_name_tool -delete_rpath "$CONDA_PREFIX_ARG/lib" "$f" 2>/dev/null || break
        done
        # Add if entirely missing.
        if ! otool -l "$f" 2>/dev/null | \
             awk '/LC_RPATH/{i=1;next} i && /path /{print $2; i=0}' | \
             grep -qx "$CONDA_PREFIX_ARG/lib"; then
            install_name_tool -add_rpath "$CONDA_PREFIX_ARG/lib" "$f" 2>/dev/null || true
        fi
    done < <(find "$PREFIX/$d" -type f -perm -u+x)
done

echo "==> bundle_conda_deps: done"

#!/bin/bash
# Bandicoot: build a relocatable Linux release tarball, in one step.
#
#     ./scripts/release-linux.sh                       milestone release
#     BANDICOOT_NIGHTLY=1 ./scripts/release-linux.sh   nightly (commit in the name)
#     ./scripts/release-linux.sh --skip-build          reuse the last release build
#
# --skip-build starts at step 2 on the install a previous run built, for
# finishing a run that stopped after its build. It refuses unless that build
# was a release-mode build (of the same kind) of the current commit, or, for a
# milestone release, of an earlier commit that differs from it only in the
# packaging scripts (so fixes to this script need no rebuild).
#
# Run on the OLDEST distribution release the tarball should support: the
# result runs there and on anything newer. It
#   1. builds Bandicoot in release mode (scripts/build-linux.sh) into PREFIX,
#   2. adds the Python standard library, numpy and matplotlib,
#   3. adds the GTK 2 image loaders, input methods and theme,
#   4. copies in every library the program loads, except the few that must
#      come from the user's system (below), and points each binary at the
#      bundled copies through a search path relative to itself,
#   5. checks the result, and
#   6. writes <repo>/../releases/<version>/bandicoot-<version>-linux-<arch>.tar.gz
#      and its .sha256.
# The user unpacks the tarball and runs its setup.sh once.
#
# Needs: the dependencies from scripts/build_deps_linux.sh under a prefix that
# is not in a home directory (default /opt/bandicoot-deps), a clean tree whose
# HEAD is pushed, and network access for the numpy/matplotlib wheels.
#
# Environment (all optional):
#   PREFIX        install to bundle      (default: $HOME/sw/bandicoot-install)
#   DEPS_PREFIX   source-built deps      (default: /opt/bandicoot-deps)
#   OUT           output directory       (default: <repo>/../releases/<version>)
#   CC / CXX      as for build-linux.sh

set -Eeuo pipefail

SKIP_BUILD=0
for _arg in "$@"; do
    case "${_arg}" in
        --skip-build) SKIP_BUILD=1 ;;
        -h|--help)    sed -n '2,/^$/p' "$0"; exit 0 ;;
        *)            echo "!! release-linux.sh: unknown option ${_arg}" >&2; exit 2 ;;
    esac
done
# a failing command would otherwise end the script without saying where
trap 'echo "!! release-linux.sh: stopped by a failed command at line ${LINENO}" >&2' ERR

if [ "$(uname)" != "Linux" ]; then
    echo "!! release-linux.sh is for Linux; on macOS use scripts/build.sh + scripts/package.sh" >&2
    exit 1
fi

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"
export PATH="/usr/sbin:/usr/bin:/sbin:/bin"
unset LD_LIBRARY_PATH PKG_CONFIG_PATH

PREFIX="${PREFIX:-$HOME/sw/bandicoot-install}"
DEPS_PREFIX="${DEPS_PREFIX:-/opt/bandicoot-deps}"
PATCHELF="${DEPS_PREFIX}/patchelf/bin/patchelf"
PYTHON="${DEPS_PREFIX}/python/bin/python3.13"
ARCH="$(uname -m)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo "${ARCH}-linux-gnu")"
SYSLIB="/usr/lib/${MULTIARCH}"

fail() { echo "!! release-linux.sh: $*" >&2; exit 1; }

# --- preconditions ----------------------------------------------------------
case "${DEPS_PREFIX}/" in
    "${HOME%/}"/*)
        fail "DEPS_PREFIX (${DEPS_PREFIX}) is inside your home directory; some
   dependencies record their install path, so the tarball would carry it.
   Build them under /opt/bandicoot-deps (see scripts/build_deps_linux.sh)." ;;
esac
[ -x "${PATCHELF}" ] || fail "patchelf not found at ${PATCHELF}; run scripts/build_deps_linux.sh"
[ -x "${PYTHON}" ]   || fail "Python not found at ${PYTHON}; run scripts/build_deps_linux.sh"

# Every Linux build regenerates these tracked files (see build-linux.sh), so
# a tree that is otherwise clean still fails the clean-tree check. After a
# build they are expected to be modified, and nothing else may be.
_generated="src/coot.py src/coot_wrap_python.cc src/coot_wrap_python_pre.cc"
_dirty="$(git status --porcelain --untracked-files=no | awk '{print $2}')"
_only_generated=0
if [ -n "${_dirty}" ]; then
    _only_generated=1
    for _f in ${_dirty}; do
        case " ${_generated} " in *" ${_f} "*) ;; *) _only_generated=0 ;; esac
    done
fi
if [ "${SKIP_BUILD}" = "1" ]; then
    [ -z "${_dirty}" ] || [ "${_only_generated}" = "1" ] || \
        fail "the tree has changed since the build; run without --skip-build"
elif [ "${_only_generated}" = "1" ]; then
    echo "!! release-linux.sh: only the SWIG files a Linux build regenerates are modified." >&2
    echo "   Restore them, then run this again:" >&2
    echo "     git restore ${_generated}" >&2
    exit 1
fi

# Files taken from the build machine's system packages
PIXBUF_SRC="${SYSLIB}/gdk-pixbuf-2.0/2.10.0/loaders"
PIXBUF_QUERY_SRC="${SYSLIB}/gdk-pixbuf-2.0/gdk-pixbuf-query-loaders"
IM_SRC="${SYSLIB}/gtk-2.0/2.10.0/immodules"
IM_QUERY_SRC="${SYSLIB}/libgtk2.0-0/gtk-query-immodules-2.0"
THEME_SRC="/usr/share/themes/Raleigh"
for _f in "${PIXBUF_SRC}/libpixbufloader-svg.so" "${PIXBUF_QUERY_SRC}" "${IM_SRC}" \
          "${IM_QUERY_SRC}" "${THEME_SRC}/gtk-2.0/gtkrc"; do
    [ -e "${_f}" ] || fail "${_f} not found (packages: librsvg2-common, libgtk2.0-0, libgtk2.0-common)"
done

# --- 1. build -----------------------------------------------------------------
# A stamp in the install records what the build was, for --skip-build. Any
# other build replaces the install's contents, and the stamp with them.
STAMP="${PREFIX}/.bandicoot-release-stamp"
if [ -n "${BANDICOOT_NIGHTLY:-}" ]; then
    MODE=nightly
else
    MODE=release
fi
HEAD_COMMIT="$(git rev-parse HEAD)"
stamp_field() { sed -n "s/^$1=//p" "${STAMP}"; }

if [ "${SKIP_BUILD}" = "1" ]; then
    [ -f "${STAMP}" ] || fail "${PREFIX} holds no release build to reuse; run without --skip-build"
    _built="$(stamp_field commit)"
    if [ "${_built}" != "${HEAD_COMMIT}" ]; then
        # a nightly's title bar names the commit it was built from
        [ "${MODE}" = "release" ] && git merge-base --is-ancestor "${_built}" HEAD 2>/dev/null || \
            fail "the build in ${PREFIX} is of commit ${_built:0:7}, not HEAD; run without --skip-build"
        _changed="$(git diff --name-only "${_built}" HEAD | \
            grep -vxE 'scripts/(release-linux|setup-install|smoke_test_imports|check_[a-z_]+)\.sh' || true)"
        [ -z "${_changed}" ] || \
            fail "files built into Bandicoot changed since the build (${_built:0:7}):
$(echo "${_changed}" | sed 's/^/     /')
   run without --skip-build"
        echo "==> --skip-build: only packaging scripts changed since ${_built:0:7}"
    fi
    [ "$(stamp_field mode)" = "${MODE}" ] || \
        fail "the build in ${PREFIX} is a $(stamp_field mode) build, not a ${MODE} build; run without --skip-build"
    VERSION="$(stamp_field version)"
    SUFFIX="$(stamp_field suffix)"
    echo "==> --skip-build: reusing the ${MODE} build of ${HEAD_COMMIT:0:7} in ${PREFIX}"
else
    if [ "${MODE}" = "nightly" ]; then
        unset BANDICOOT_RELEASE
    else
        export BANDICOOT_RELEASE=1
    fi
    unset BANDICOOT_INCREMENTAL BANDICOOT_ASAN
    rm -f "${STAMP}"
    PREFIX="${PREFIX}" DEPS_PREFIX="${DEPS_PREFIX}" ./scripts/build-linux.sh
    VERSION="$(awk -F'"' '/^#define BANDICOOT_VERSION /{print $2}' src/bandicoot-version.h)"
    SUFFIX="$(awk -F'"' '/BANDICOOT_BUILD_SUFFIX/ {print $2}' src/bandicoot-build-id.h)"
    printf 'mode=%s\ncommit=%s\nversion=%s\nsuffix=%s\n' \
        "${MODE}" "${HEAD_COMMIT}" "${VERSION}" "${SUFFIX}" > "${STAMP}"
fi
NAME="bandicoot-${VERSION}${SUFFIX}"
LIB="${PREFIX}/lib"
PYVER="$("${PYTHON}" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
SITE="${LIB}/python${PYVER}/site-packages"

# --- 2. Python runtime ------------------------------------------------------------
echo "==> bundling the Python ${PYVER} standard library"
mkdir -p "${LIB}/python${PYVER}"
# site-packages is excluded so Bandicoot's own modules there are kept
( cd "${DEPS_PREFIX}/python/lib/python${PYVER}" && \
  tar cf - --exclude=./site-packages --exclude=test --exclude=tests \
      --exclude=idlelib --exclude=tkinter --exclude=turtledemo --exclude=ensurepip \
      --exclude='config-*' --exclude=__pycache__ . ) | \
  ( cd "${LIB}/python${PYVER}" && tar xf - )
# Tk is not shipped, and the _test* modules belong to the test suite
rm -f "${LIB}/python${PYVER}"/lib-dynload/_tkinter*.so "${LIB}/python${PYVER}"/lib-dynload/_test*.so

echo "==> bundling numpy + matplotlib (PyPI wheels)"
# --only-binary: a source build would link this machine's libraries
"${PYTHON}" -m pip install --quiet --disable-pip-version-check \
    --target="${SITE}" --upgrade --only-binary=:all: numpy matplotlib
rm -rf "${SITE}/bin"

# --- 3. GTK modules and theme -------------------------------------------------------
echo "==> bundling GTK 2 image loaders, input methods and the Raleigh theme"
mkdir -p "${LIB}/gdk-pixbuf-2.0/2.10.0/loaders" "${LIB}/gtk-2.0/2.10.0/immodules" \
         "${LIB}/gio/modules" "${PREFIX}/libexec" "${PREFIX}/share/themes"
cp -L "${PIXBUF_SRC}"/*.so "${LIB}/gdk-pixbuf-2.0/2.10.0/loaders/"
cp -L "${IM_SRC}"/*.so "${LIB}/gtk-2.0/2.10.0/immodules/"
cp -L "${PIXBUF_QUERY_SRC}" "${PREFIX}/libexec/gdk-pixbuf-query-loaders"
cp -L "${IM_QUERY_SRC}" "${PREFIX}/libexec/gtk-query-immodules-2.0"
rm -rf "${PREFIX}/share/themes/Raleigh"
cp -R "${THEME_SRC}" "${PREFIX}/share/themes/Raleigh"

# --- 4. libraries -------------------------------------------------------------------
# Left to the user's system: the C library, the C++ runtime (newer systems have
# newer copies, and the graphics drivers need those), OpenGL and the graphics
# stack, X11, and libraries tied to system configuration or services. After
# the AppImage project's exclusion list. Everything else is bundled, including
# GTK 2 and GLib, so the tarball keeps working where those are not installed.
is_system_lib() {   # <soname>
    case "$1" in
        ld-linux*|libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|libresolv.so*) return 0 ;;
        libutil.so*|libanl.so*|libnsl.so*|libBrokenLocale.so*|libcrypt.so*|libmvec.so*) return 0 ;;
        libthread_db.so*|libstdc++.so*|libgcc_s.so*) return 0 ;;
        libGL.so*|libGLX*|libEGL.so*|libGLdispatch.so*|libGLESv*|libOpenGL.so*) return 0 ;;
        libglapi.so*|libdrm*|libgbm.so*|libvulkan.so*|libwayland-*) return 0 ;;
        libX*|libxcb*|libxshmfence.so*|libICE.so*|libSM.so*) return 0 ;;
        libfontconfig.so*|libfreetype.so*|libharfbuzz.so*|libz.so*|libexpat.so*) return 0 ;;
        libzstd.so*|liblz4.so*|libelf.so*|libLLVM*|libsensors.so*) return 0 ;;
        libuuid.so*|libblkid.so*|libmount.so*|libselinux.so*|libudev.so*|libsystemd.so*) return 0 ;;
        libdbus-1.so*|libcom_err.so*|libgpg-error.so*|libgcrypt.so*|libp11-kit.so*) return 0 ;;
        libasound.so*|libbsd.so*|libmd.so*|libcap.so*) return 0 ;;
    esac
    return 1
}

is_elf() { [ -f "$1" ] && [ ! -L "$1" ] && [ "$(head -c 4 "$1" 2>/dev/null | od -An -c | tr -d ' ')" = '177ELF' ]; }

# The search path that leads from a file's directory back to lib/
origin_rpath() {   # <file>
    local rel="${1#"${PREFIX}"/}" dir up=""
    dir="$(dirname "${rel}")"
    case "${dir}" in
        bin|libexec) echo '$ORIGIN/../lib' ;;
        lib)         echo '$ORIGIN' ;;
        lib/*)       up="$(echo "${dir#lib/}" | sed -E 's#[^/]+#..#g')"
                     echo "\$ORIGIN/${up}" ;;
        *)           echo '$ORIGIN' ;;
    esac
}

# The ELF files whose search path is ours to set: everything except the
# wheels in site-packages, which carry their own
all_elves() {
    { find "${PREFIX}/bin" "${PREFIX}/libexec" -type f 2>/dev/null
      find "${LIB}" -type f -name '*.so*' 2>/dev/null; } | \
        while IFS= read -r f; do is_elf "$f" && echo "$f"; done || true
}
own_elves() { all_elves | grep -vF "${SITE}/" || true; }

set_rpaths() {
    local f n=0
    while IFS= read -r f; do
        chmod u+w "$f"
        # a file that loads no libraries needs no search path; one left over
        # from its build would point outside the bundle
        if ! readelf -d "$f" 2>/dev/null | grep -q '(NEEDED)'; then
            if readelf -d "$f" 2>/dev/null | grep -qE '\((RUN)?PATH\)'; then
                "${PATCHELF}" --remove-rpath "$f"
            fi
            continue
        fi
        # DT_RPATH rather than RUNPATH: it is searched before LD_LIBRARY_PATH,
        # so a library directory in the user's environment cannot replace a
        # bundled library, and it also serves the libraries' own dependencies
        "${PATCHELF}" --force-rpath --set-rpath "$(origin_rpath "$f")" "$f"
        n=$((n + 1))
    done < <(own_elves)
    echo "    search paths set on ${n} files"
}

# Resolve against the bundle first, then the dependency tree, then the system
DEP_LIBDIRS="${LIB}"
for _d in "${DEPS_PREFIX}"/*/lib; do DEP_LIBDIRS="${DEP_LIBDIRS}:${_d}"; done

echo "==> bundling libraries"
set_rpaths
declare -A _seen=()
_round=0
while :; do
    _round=$((_round + 1))
    _added=0
    _missing=""
    while IFS= read -r f; do
        while IFS= read -r line; do
            # "libfoo.so.1 => /path/libfoo.so.1 (0x...)" or "libfoo.so.1 => not found"
            name="$(echo "${line}" | awk '{print $1}')"
            path="$(echo "${line}" | awk '{print $3}')"
            [ -n "${name}" ] || continue
            is_system_lib "${name}" && continue
            [ -e "${LIB}/${name}" ] && continue
            case "${path}" in "${PREFIX}"/*) continue ;; esac
            if [ "${path}" = "not" ]; then
                _missing="${_missing} ${name}($(basename "$f"))"
                continue
            fi
            [ -n "${_seen[${name}]:-}" ] && continue
            _seen[${name}]=1
            cp -L "${path}" "${LIB}/${name}"
            chmod u+w "${LIB}/${name}"
            _added=$((_added + 1))
        done < <(LD_LIBRARY_PATH="${DEP_LIBDIRS}" ldd "$f" 2>/dev/null | grep '=>' || true)
    done < <(own_elves)
    echo "    round ${_round}: ${_added} libraries added"
    [ -z "${_missing}" ] || fail "unresolved libraries:${_missing}"
    [ "${_added}" -gt 0 ] || break
    set_rpaths
done

# --- 5. strip and prune -----------------------------------------------------------
echo "==> stripping debug information"
while IFS= read -r f; do strip --strip-debug "$f" 2>/dev/null || true; done < <(own_elves)
rm -f "${LIB}"/*.a "${LIB}"/*.la
rm -rf "${PREFIX}/include" "${LIB}/pkgconfig"
find "${PREFIX}" -type d -name __pycache__ -prune -exec rm -rf {} +

cp -f scripts/setup-install.sh "${PREFIX}/setup.sh"
chmod +x "${PREFIX}/setup.sh"
[ -f COPYING ] || fail "COPYING is missing; a GPL binary ships with its license text"
for _doc in COPYING README.md AUTHORS THIRD_PARTY_LICENSES.md CORRESPONDING_SOURCE.md; do
    [ -f "${_doc}" ] && cp -f "${_doc}" "${PREFIX}/${_doc}"
done

# --- 6. checks ----------------------------------------------------------------------
echo "==> check: every library resolves inside the bundle or is a system library"
_bad=0
while IFS= read -r f; do
    while IFS= read -r line; do
        name="$(echo "${line}" | awk '{print $1}')"
        path="$(echo "${line}" | awk '{print $3}')"
        is_system_lib "${name}" && continue
        case "${path}" in
            "${PREFIX}"/*) ;;
            *) echo "   ${f#"${PREFIX}"/}: ${name} -> ${path}" >&2; _bad=1 ;;
        esac
    done < <(ldd "$f" 2>/dev/null | grep '=>' || true)
done < <(all_elves)
[ "${_bad}" = "0" ] || fail "libraries above resolve outside the bundle"

echo "==> check: no search path outside the bundle"
while IFS= read -r f; do
    # a file with no dependencies has no search path
    _rp="$(readelf -d "$f" 2>/dev/null | grep -E '\((RUN)?PATH\)' | sed 's/.*\[\(.*\)\]/\1/' || true)"
    for _d in ${_rp//:/ }; do
        case "${_d}" in
            '$ORIGIN'*) ;;
            *) echo "   ${f#"${PREFIX}"/}: ${_d}" >&2; _bad=1 ;;
        esac
    done
done < <(own_elves)
[ "${_bad}" = "0" ] || fail "search paths above point outside the bundle"

echo "==> check: no file contains the build machine's home path"
_hits="$(grep -rlaF "${HOME%/}/" "${PREFIX}" 2>/dev/null | grep -v -e '/loaders.cache$' -e '/gtk.immodules$' || true)"
if [ -n "${_hits}" ]; then
    echo "${_hits}" | sed "s#^${PREFIX}/#   #" >&2
    fail "the files above contain ${HOME%/}/"
fi

echo "==> check: required files present"
for _f in libexec/Bandicoot bin/coot bin/bcoot setup.sh \
          "lib/python${PYVER}/encodings/__init__.py" "lib/python${PYVER}/site-packages/coot" \
          lib/gdk-pixbuf-2.0/2.10.0/loaders/libpixbufloader-svg.so \
          libexec/gdk-pixbuf-query-loaders libexec/gtk-query-immodules-2.0 \
          share/themes/Raleigh/gtk-2.0/gtkrc share/coot/syminfo.lib \
          share/coot/lib/data/monomers share/coot/reference-structures share/coot/pixmaps; do
    [ -e "${PREFIX}/${_f}" ] || fail "missing from the bundle: ${_f}"
done

echo "==> check: the bundled interpreter imports what a session needs"
./scripts/smoke_test_imports.sh "${PREFIX}" || fail "a module failed to import in the bundled interpreter"

./scripts/check_splash_version.sh "${PREFIX}/share/coot/pixmaps/bandicoot-splash.png" "${VERSION}" || \
    fail "the splash screen does not show release ${VERSION}"

# --- 7. tarball ----------------------------------------------------------------------
OUTDIR="${OUT:-$(dirname "${REPO_ROOT}")/releases/${VERSION}}"
TARBASE="${NAME}-linux-${ARCH}"
TARBALL="${OUTDIR}/${TARBASE}.tar.gz"
mkdir -p "${OUTDIR}"
rm -f "${TARBALL}" "${TARBALL}.sha256"
echo "==> writing ${TARBALL}"
_stage="$(mktemp -d)"
trap 'rm -rf "${_stage}"' EXIT
cp -a "${PREFIX}" "${_stage}/${NAME}"
# the caches record absolute paths and are written by setup.sh on the user's machine
tar -C "${_stage}" --exclude='*/loaders.cache' --exclude='*/gtk.immodules' \
    --exclude='*/.bandicoot-release-stamp' \
    --exclude='*/__pycache__' --owner=0 --group=0 -czf "${TARBALL}" "${NAME}"
rm -rf "${_stage}"
trap - EXIT
( cd "${OUTDIR}" && sha256sum "${TARBASE}.tar.gz" > "${TARBASE}.tar.gz.sha256" )

echo ""
echo "Release ${NAME} for Linux ${ARCH}:"
echo "    ${TARBALL}  ($(du -h "${TARBALL}" | cut -f1))"
echo "    sha256: $(awk '{print $1}' "${TARBALL}.sha256")"
echo "To test, unpack it on another machine and run setup.sh, then bin/bcoot."

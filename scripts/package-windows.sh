#!/bin/bash
# Bandicoot: turn a Windows install (scripts/build-windows.sh) into a
# self-contained installer, in the MSYS2 UCRT64 shell.
#
#     ./scripts/build-windows.sh
#     ./scripts/package-windows.sh
#
# The result needs no MSYS2: it is laid out as MSYS2's own tree is (bin/,
# lib/, share/, etc/), which the bundled GTK, Python and OpenSSL find their
# files relative to, and Bandicoot.exe sets up the rest of its environment
# from where it is installed. As with WinCoot, users run bandicoot.bat (from
# the shortcuts), which holds the settings particular to an installation. It
#   1. copies the programs into bin/ and the data and Python modules from the
#      install,
#   2. adds the Python standard library, numpy and matplotlib (MSYS2's
#      packages, built for this Python), the GTK image loaders and the
#      certificate bundle,
#   3. copies into bin/ every DLL the programs and modules load, except
#      Windows' own,
#   4. records where each bundled component came from, strips debug
#      information and checks the result, and
#   5. writes the installer (scripts/windows/bandicoot.nsi) to
#      <repo>/../releases/<version>/bandicoot-<version>-windows-x86_64-setup.exe
#      and its .sha256.
#
# Environment (all optional):
#   PREFIX        install to package     (default: $HOME/sw/bandicoot-install)
#   DEPS_PREFIX   source-built deps      (default: /opt/bandicoot-deps)
#   OUT           output directory       (default: <repo>/../releases/<version>)

set -Eeuo pipefail
trap 'echo "!! package-windows.sh: stopped by a failed command at line ${LINENO}" >&2' ERR

fail() { echo "!! package-windows.sh: $*" >&2; exit 1; }

[ "${MSYSTEM:-}" = "UCRT64" ] || fail "run this in the MSYS2 UCRT64 shell (MSYSTEM=${MSYSTEM:-unset})"

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"
_winsys="$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")"
SYS32="${_winsys}/System32"
export PATH="/ucrt64/bin:/usr/bin:${SYS32}:${_winsys}"
unset _winsys

PREFIX="${PREFIX:-$HOME/sw/bandicoot-install}"
DEPS_PREFIX="${DEPS_PREFIX:-/opt/bandicoot-deps}"
MSYS_PREFIX=/ucrt64
PYTHON="${MSYS_PREFIX}/bin/python3"
PKG=mingw-w64-ucrt-x86_64

# --- preconditions ----------------------------------------------------------
[ -f "${PREFIX}/libexec/Bandicoot.exe" ] || fail "no install at ${PREFIX}; run scripts/build-windows.sh"
_missing=""
for p in ${PKG}-nsis ${PKG}-binutils ${PKG}-python-numpy ${PKG}-python-matplotlib ${PKG}-gdk-pixbuf2 ${PKG}-ca-certificates; do
    pacman -Q "$p" >/dev/null 2>&1 || _missing="${_missing} $p"
done
[ -z "${_missing}" ] || fail "missing MSYS2 packages; install them with:
     pacman -S --needed${_missing}"

VERSION="$(awk -F'"' '/^#define BANDICOOT_VERSION /{print $2}' src/bandicoot-version.h)"
SUFFIX="$(awk -F'"' '/BANDICOOT_BUILD_SUFFIX/ {print $2}' src/bandicoot-build-id.h 2>/dev/null || true)"
NAME="bandicoot-${VERSION}${SUFFIX}"
PYVER="$("${PYTHON}" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"

# Where DLLs are found: the install, the source-built dependencies, MSYS2
DLL_DIRS="${PREFIX}/bin ${PREFIX}/libexec"
for _d in fftw2/bin canvas/bin xtal/bin gemmi/bin wx/lib wx/bin rdkit/bin rdkit/lib; do
    DLL_DIRS="${DLL_DIRS} ${DEPS_PREFIX}/${_d}"
done
DLL_DIRS="${DLL_DIRS} ${MSYS_PREFIX}/bin"

STAGE_ROOT="$(mktemp -d)"
trap 'rm -rf "${STAGE_ROOT}"' EXIT
S="${STAGE_ROOT}/${NAME}"
mkdir -p "${S}/bin" "${S}/lib" "${S}/etc"
SITE="${S}/lib/python${PYVER}/site-packages"

# --- 1. programs, data, Bandicoot's Python modules ------------------------------------
echo "==> programs"
for _f in "${PREFIX}"/libexec/*.exe "${PREFIX}"/bin/*.exe; do
    [ -e "${_f}" ] && cp -f "${_f}" "${S}/bin/"
done
# GLib starts other programs (reduce, probe) through helper programs it looks
# for beside its own DLL
for _h in gspawn-win64-helper.exe gspawn-win64-helper-console.exe; do
    [ -f "${MSYS_PREFIX}/bin/${_h}" ] || fail "${_h} not found in ${MSYS_PREFIX}/bin"
    cp -f "${MSYS_PREFIX}/bin/${_h}" "${S}/bin/"
done
# The launcher scripts need a Unix shell; Windows gets its own, with the line
# endings cmd.exe expects.
sed 's/$/\r/' scripts/windows/bandicoot.bat > "${S}/bandicoot.bat"
echo "==> data"
cp -R "${PREFIX}/share" "${S}/share"
[ -d "${PREFIX}/lib/python${PYVER}/site-packages" ] || \
    fail "no Python modules at ${PREFIX}/lib/python${PYVER}/site-packages (built for another Python?)"
mkdir -p "${SITE}"
cp -R "${PREFIX}/lib/python${PYVER}/site-packages/." "${SITE}/"
# libtool's leftovers beside pyrogen's extension module record build paths
find "${SITE}" -maxdepth 1 \( -name '*.la' -o -name '*.a' -o -name '*.dll.a' \) -delete

# --- 2. Python, numpy + matplotlib, GTK image loaders, certificates ------------------
echo "==> Python ${PYVER} standard library"
( cd "${MSYS_PREFIX}/lib/python${PYVER}" && \
  tar cf - --exclude=./site-packages --exclude=test --exclude=tests \
      --exclude=idlelib --exclude=tkinter --exclude=turtledemo --exclude=ensurepip \
      --exclude='config-*' --exclude=__pycache__ . ) | \
  ( cd "${S}/lib/python${PYVER}" && tar xf - )
rm -f "${S}/lib/python${PYVER}"/lib-dynload/_tkinter* "${S}/lib/python${PYVER}"/lib-dynload/_test*

# numpy and matplotlib with the Python packages they need: PyPI's wheels are
# built for python.org's Python, not MSYS2's. Only Python packages are
# followed, and only their files under site-packages taken; the DLLs they load,
# from whatever package, are collected in step 3.
echo "==> numpy + matplotlib (MSYS2 packages)"
declare -A _visited=()
PY_SITE="${MSYS_PREFIX}/lib/python${PYVER}/site-packages"
PY_FILES="$(mktemp)"
collect_python_files() {   # <package>
    local pkg="$1" dep
    [ -n "${_visited[${pkg}]:-}" ] && return 0
    _visited[${pkg}]=1
    pacman -Qlq "${pkg}" 2>/dev/null | grep "^${PY_SITE}/." | grep -v -e '/__pycache__/' -e '/$' | \
        sed "s#^${PY_SITE}/##" >> "${PY_FILES}" || true
    # the field may continue on indented lines
    for dep in $(pacman -Qi "${pkg}" | \
                 awk '/^Depends On/ {f = 1; sub(/^[^:]*:/, ""); print; next}
                      f && /^[^ ]/ {f = 0}
                      f {print}'); do
        dep="${dep%%[<>=]*}"
        case "${dep}" in
            "${PKG}"-python-*) collect_python_files "${dep}" ;;
        esac
    done
}
collect_python_files "${PKG}-python-numpy"
collect_python_files "${PKG}-python-matplotlib"
echo "    $(wc -l < "${PY_FILES}") files from ${#_visited[@]} packages"
tar -C "${PY_SITE}" -cf - -T "${PY_FILES}" | tar -C "${SITE}" -xf -
rm -f "${PY_FILES}"
rm -rf "${SITE}/bin"

echo "==> GTK image loaders"
mkdir -p "${S}/lib/gdk-pixbuf-2.0/2.10.0"
cp -R "${MSYS_PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders" "${S}/lib/gdk-pixbuf-2.0/2.10.0/"
# The loaders' cache names them by absolute path, so it is written on the
# user's machine: the installer runs this, which finds the loaders and the
# cache relative to where it is installed.
cp -f "${MSYS_PREFIX}/bin/gdk-pixbuf-query-loaders.exe" "${S}/bin/"

echo "==> certificates (Python's ssl and libcurl)"
cp -R "${MSYS_PREFIX}/etc/ssl" "${S}/etc/ssl"

# --- 3. DLLs ----------------------------------------------------------------------------
pe_files() { find "${S}" -type f \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.pyd' \); }
find_dll() {   # <name>: where it would be bundled from, if not Windows'
    local d
    for d in ${DLL_DIRS}; do
        [ -f "${d}/$1" ] && { echo "${d}/$1"; return 0; }
    done
    return 1
}
is_windows_dll() {   # <name>
    case "${1,,}" in api-ms-win-*|ext-ms-*) return 0 ;; esac
    [ -e "${SYS32}/$1" ]
}

# The DLLs each file imports, read once per file: objdump takes many files at
# a time, since starting programs is slow on Windows. Its output names files
# by Windows path, so they are matched to ours by order.
declare -A IMPORTS=()
scan_imports() {   # <file>...
    local -a files=("$@")
    local out count idx name f
    out="$(objdump -p "${files[@]}" 2>/dev/null | tr -d '\r' | awk '
        /:[ \t]+file format / { n++; next }
        /^[ \t]*DLL Name: / { sub(/^[ \t]*DLL Name: /, ""); print n "\t" $0 }
        END { print "count\t" n + 0 }')"
    count="$(echo "${out}" | awk -F'\t' '$1 == "count" {print $2}')"
    if [ "${count}" != "${#files[@]}" ]; then
        # a file objdump could not read upsets the order: one at a time
        [ "${#files[@]}" -gt 1 ] || { IMPORTS[${files[0]}]=""; return 0; }
        for f in "${files[@]}"; do scan_imports "$f"; done
        return 0
    fi
    for f in "${files[@]}"; do IMPORTS[$f]=""; done
    while IFS=$'\t' read -r idx name; do
        [ "${idx}" = "count" ] && continue
        f="${files[$((idx - 1))]}"
        IMPORTS[$f]+="${name}"$'\n'
    done <<< "${out}"
}
scan_new_files() {   # prints the files scanned
    local -a batch=()
    local f
    while IFS= read -r f; do
        [ -n "${IMPORTS[$f]+x}" ] && continue
        batch+=("$f")
        echo "$f"
        if [ "${#batch[@]}" -ge 100 ]; then scan_imports "${batch[@]}"; batch=(); fi
    done < <(pe_files)
    [ "${#batch[@]}" -eq 0 ] || scan_imports "${batch[@]}"
}

echo "==> DLLs"
declare -A _have=()
for _f in "${S}"/bin/*; do _n="${_f##*/}"; _have[${_n,,}]=1; done
_round=0
while :; do
    _round=$((_round + 1))
    _added=0
    _missing=""
    # scan_new_files runs here, not in a subshell, so IMPORTS keeps what it reads
    _new="$(mktemp)"
    scan_new_files > "${_new}"
    while IFS= read -r f; do
        while IFS= read -r name; do
            [ -n "${name}" ] || continue
            [ -n "${_have[${name,,}]:-}" ] && continue
            # a module's own folder, as for numpy's bundled libraries
            [ -e "${f%/*}/${name}" ] && continue
            if src="$(find_dll "${name}")"; then
                cp -f "${src}" "${S}/bin/${name}"
                _have[${name,,}]=1
                _added=$((_added + 1))
            elif ! is_windows_dll "${name}"; then
                _missing="${_missing} ${name}(${f##*/})"
            fi
        done <<< "${IMPORTS[$f]}"
    done < "${_new}"
    rm -f "${_new}"
    echo "    round ${_round}: ${_added} DLLs added"
    [ -z "${_missing}" ] || fail "DLLs not found:${_missing}"
    [ "${_added}" -gt 0 ] || break
done

# --- 4. record, strip, check ------------------------------------------------------------
echo "==> recording the origin of each bundled component"
DOCDIR="${S}/share/doc/bandicoot"
mkdir -p "${DOCDIR}/licenses"
BUNDLED_MD="${DOCDIR}/BUNDLED-WINDOWS.md"
{
    echo "# Components bundled in the Windows package"
    echo ""
    echo "Bandicoot ${VERSION}${SUFFIX}, built with MSYS2 UCRT64 (x86_64)."
    echo "MSYS2 components: source at https://packages.msys2.org/base/<package> for the"
    echo "version given; their license files are in licenses/. Source-built components:"
    echo "see CORRESPONDING_SOURCE.md at the top of the package."
    echo ""
    echo "| File | Origin | Package | Version |"
    echo "|---|---|---|---|"
} > "${BUNDLED_MD}"
# one pacman call for every file taken from MSYS2: it is slow to start
declare -A _owner=() _lic=()
_msys_files=()
for f in "${S}"/bin/*; do
    [ -e "${MSYS_PREFIX}/bin/${f##*/}" ] && _msys_files+=("${MSYS_PREFIX}/bin/${f##*/}")
done
for f in "${S}"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do
    _msys_files+=("${MSYS_PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders/${f##*/}")
done
_msys_files+=("${MSYS_PREFIX}/lib/python${PYVER}/os.py" "${MSYS_PREFIX}/etc/ssl/certs/ca-bundle.crt")
while IFS= read -r line; do
    _owner[${line%% is owned by *}]="${line##* is owned by }"
done < <(pacman -Qo "${_msys_files[@]}" 2>/dev/null | tr -d '\r')
record_msys2() {   # <file in MSYS2> <label>
    local owner="${_owner[$1]:-}" pkg ver short
    [ -n "${owner}" ] || return 1
    pkg="${owner% *}"; ver="${owner##* }"
    echo "| \`$2\` | MSYS2 | \`${pkg}\` | ${ver} |" >> "${BUNDLED_MD}"
    short="${pkg#"${PKG}-"}"
    if [ -z "${_lic[${short}]:-}" ] && [ -d "${MSYS_PREFIX}/share/licenses/${short}" ]; then
        cp -R "${MSYS_PREFIX}/share/licenses/${short}" "${DOCDIR}/licenses/${short}"
        _lic[${short}]=1
    fi
}
for f in "${S}"/bin/*; do
    n="${f##*/}"
    record_msys2 "${MSYS_PREFIX}/bin/${n}" "${n}" && continue
    _src="$(find_dll "${n}" || true)"
    case "${_src}" in
        "${PREFIX}"/*|"")
            echo "| \`${n}\` | Bandicoot (GPL-3) | | |" >> "${BUNDLED_MD}" ;;
        "${DEPS_PREFIX}"/*)
            _dep="${_src#"${DEPS_PREFIX}"/}"
            echo "| \`${n}\` | built from source | \`${_dep%%/*}\` | |" >> "${BUNDLED_MD}" ;;
        *)  # from MSYS2, but pacman did not name its package
            fail "cannot tell which MSYS2 package ${n} came from (${_src})" ;;
    esac
done
record_msys2 "${MSYS_PREFIX}/lib/python${PYVER}/os.py" "Python standard library" || true
while read -r _p _ver; do
    echo "| Python package | MSYS2 | \`${_p}\` | ${_ver} |" >> "${BUNDLED_MD}"
    _short="${_p#"${PKG}-"}"
    if [ -z "${_lic[${_short}]:-}" ] && [ -d "${MSYS_PREFIX}/share/licenses/${_short}" ]; then
        cp -R "${MSYS_PREFIX}/share/licenses/${_short}" "${DOCDIR}/licenses/${_short}"
        _lic[${_short}]=1
    fi
done < <(pacman -Q "${!_visited[@]}" | tr -d '\r')
for f in "${S}"/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll; do
    record_msys2 "${MSYS_PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders/${f##*/}" "${f##*/}" || true
done
record_msys2 "${MSYS_PREFIX}/etc/ssl/certs/ca-bundle.crt" "certificate bundle" || true
echo "    $(grep -c '^| `' "${BUNDLED_MD}") entries; $(ls "${DOCDIR}/licenses" | wc -l) license folders"

[ -f COPYING ] || fail "COPYING is missing; a GPL binary ships with its license text"
for _doc in COPYING README.md AUTHORS THIRD_PARTY_LICENSES.md CORRESPONDING_SOURCE.md; do
    [ -f "${_doc}" ] && cp -f "${_doc}" "${S}/${_doc}"
done

echo "==> stripping debug information"
while IFS= read -r f; do strip --strip-debug "$f" 2>/dev/null || true; done < <(pe_files)
find "${S}" -type d -name __pycache__ -prune -exec rm -rf {} +

echo "==> check: every DLL resolves inside the package or is Windows' own"
_bad=0
# (stripping leaves the import tables as they were read in step 3)
for f in "${!IMPORTS[@]}"; do
    while IFS= read -r name; do
        [ -n "${name}" ] || continue
        [ -n "${_have[${name,,}]:-}" ] || [ -e "${f%/*}/${name}" ] || is_windows_dll "${name}" || {
            echo "   ${f#"${S}"/}: ${name}" >&2; _bad=1; }
    done <<< "${IMPORTS[$f]}"
done
[ "${_bad}" = "0" ] || fail "the DLLs above are not in the package"

echo "==> check: no file names the build machine's home folder"
# in each of the forms a path takes on Windows
_home_m="$(cygpath -m "${HOME}")"
_prof_m="$(cygpath -m "${USERPROFILE:-${HOME}}")"
_prof_w="$(cygpath -w "${USERPROFILE:-${HOME}}")"
_hits="$(grep -rlaiF -e "${HOME%/}/" -e "${_home_m}/" -e "${_prof_m}/" -e "${_prof_w}\\" "${S}" 2>/dev/null || true)"
if [ -n "${_hits}" ]; then
    echo "${_hits}" | sed "s#^${S}/#   #" >&2
    fail "the files above contain the build machine's home folder"
fi

echo "==> check: required files present"
for _f in bandicoot.bat bin/Bandicoot.exe bin/gspawn-win64-helper-console.exe \
          bin/gdk-pixbuf-query-loaders.exe "lib/python${PYVER}/encodings/__init__.py" \
          "lib/python${PYVER}/site-packages/coot" "lib/python${PYVER}/site-packages/numpy" \
          "lib/python${PYVER}/site-packages/matplotlib" etc/ssl/certs/ca-bundle.crt \
          share/coot/syminfo.lib share/coot/lib/data/monomers share/coot/reference-structures \
          share/coot/pixmaps share/RDKit/Data share/doc/bandicoot/BUNDLED-WINDOWS.md; do
    [ -e "${S}/${_f}" ] || fail "missing from the package: ${_f}"
done

# MSYS2's DLLs are left off PATH, so one missing from the package fails here
# rather than being found where the user will not have it.
echo "==> check: the bundled interpreter imports what a session needs"
PATH="/usr/bin:${SYS32}" ./scripts/smoke_test_imports.sh "${S}" || \
    fail "a module failed to import in the packaged interpreter"

# --- 5. installer --------------------------------------------------------------------
OUTDIR="${OUT:-$(dirname "${REPO_ROOT}")/releases/${VERSION}}"
SETUPBASE="${NAME}-windows-x86_64-setup.exe"
SETUP="${OUTDIR}/${SETUPBASE}"
mkdir -p "${OUTDIR}"
rm -f "${SETUP}" "${SETUP}.sha256"
echo "==> writing ${SETUP}"
makensis -V2 -DVERSION="${VERSION}${SUFFIX}" -DSRCDIR="$(cygpath -w "${S}")" \
    -DOUTFILE="$(cygpath -w "${SETUP}")" -DICON="$(cygpath -w pixmaps/bandicoot.ico)" \
    "$(cygpath -w scripts/windows/bandicoot.nsi)"
( cd "${OUTDIR}" && sha256sum "${SETUPBASE}" > "${SETUPBASE}.sha256" )

echo ""
echo "Installer for ${NAME}, Windows x86_64:"
echo "    ${SETUP}  ($(du -h "${SETUP}" | cut -f1))"
echo "    sha256: $(awk '{print $1}' "${SETUP}.sha256")"
echo "To test, run it on a machine (or account) without MSYS2 on its PATH."

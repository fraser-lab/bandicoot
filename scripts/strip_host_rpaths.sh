#!/bin/bash
# ---------------------------------------------------------------------------
# INVOKED AUTOMATICALLY BY scripts/build.sh -- you do NOT need to run this by
# hand. (GitHub #24: a builder read these headers cold, concluded they were a
# manual sequence, and ran them individually -- which is what kept
# re-introducing a stale rpath.)
# ---------------------------------------------------------------------------
# Strip LC_RPATH entries that point into a build-only prefix, across an install.
#
# Bundling rewrites DEPENDENCIES to @rpath but leaves each Mach-O's own rpath
# list alone, so a copied Homebrew dylib can arrive carrying e.g.
#   LC_RPATH /opt/homebrew/Cellar/jpeg-turbo/3.1.4.1/lib
# and a PyPI wheel can carry its CI machine's
#   LC_RPATH /Users/runner/work/Pillow/Pillow/build/deps/darwin/lib
# Either makes dyld search a directory we do not control when resolving any
# @rpath dependency of that file -- on a machine that happens to have something
# there, the app silently binds to it instead of to the copy we ship. Same class
# of failure as GitHub #8, arriving by a different route. check_install.sh
# reports these as HOST-RPATH.
#
# Allowlist, not denylist: keep @loader_path/@executable_path forms, the OS, and
# the install itself; strip every other absolute search path. A denylist misses
# paths nobody anticipated -- which is exactly how the Pillow one got shipped.
#
# Must run before codesign (install_name_tool invalidates signatures).
#
# Usage:  strip_host_rpaths.sh <install-prefix>
# Prints one line per removed rpath, then a count. Never fails the build.
#
# v0.1.4.15: extracted from an inline block in build.sh so bundle_conda_deps.sh
# can call it too. That script re-fetches the numpy/matplotlib wheels with pip
# on EVERY run, which re-introduces Pillow's CI rpath -- so anyone iterating by
# re-running the bundler alone (as the reporter of GitHub #24 did while adding
# missing libraries to it) got the rpath back and had to delete it by hand after
# every run. One implementation, called from both places.

set -u

PREFIX="${1:?usage: $0 <install-prefix>}"
[ -d "$PREFIX" ] || { echo "strip_host_rpaths: $PREFIX is not a directory" >&2; exit 1; }
PREFIX="$(cd "$PREFIX" && pwd)"

_stripped=0
while IFS= read -r _m; do
    file -b "$_m" 2>/dev/null | grep -q "Mach-O" || continue
    while IFS= read -r _rp; do
        [ -n "$_rp" ] || continue
        case "$_rp" in
            @*)                              continue ;;
            "${PREFIX}"|"${PREFIX}"/*)       continue ;;
            /usr/lib|/usr/lib/*|/System|/System/*|/Library/Apple/*) continue ;;
            /*)
                chmod u+w "$_m" 2>/dev/null || true
                if install_name_tool -delete_rpath "$_rp" "$_m" 2>/dev/null; then
                    echo "    removed rpath $_rp from ${_m#$PREFIX/}"
                    _stripped=$((_stripped + 1))
                fi
                ;;
        esac
    done < <(otool -l "$_m" 2>/dev/null | awk '/LC_RPATH/{i=1;next} i&&/path /{print $2;i=0}')
done < <(find "$PREFIX" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) 2>/dev/null)

echo "    removed ${_stripped} build-host rpath(s)"
exit 0

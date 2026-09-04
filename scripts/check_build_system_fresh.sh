#!/bin/bash
# Bandicoot: is the GENERATED build system (configure, Makefile.in, aclocal.m4)
# current with respect to the sources it is generated FROM (configure.ac, the
# Makefile.am tree, macros/*.m4)?
#
# WHY THIS IS NOT AN mtime COMPARISON, which is the obvious implementation:
#
# make and autotools decide "up to date" by modification time, but git stamps
# every checked-out file with the CHECKOUT time, not the content time. A branch
# switch, rebase or cherry-pick therefore reorders those timestamps arbitrarily
# with respect to logical dependency, and it goes wrong in BOTH directions:
#
#   source newer than product -> make tries to re-run aclocal/automake/autoconf
#                                in the middle of a build. Noisy, but visible.
#
#   product newer than source -> make believes a stale configure is current and
#                                says NOTHING. This is the dangerous one.
#
# On 2026-08-25 the second case bit: after a rebase, ./configure predated the
# C++17 bump in configure.ac, so a v0.2 build was configured at -std=c++14 with
# no warning anywhere. build.sh's guard could not catch it because it asked
# "is there a ./configure?" -- an existence test can only see absence, never
# staleness.
#
# So this hashes the CONTENT of the inputs instead, which is immune to timestamp
# churn in both directions.
#
# Usage:
#   check_build_system_fresh.sh              compare; 0 = fresh, 1 = stale
#   check_build_system_fresh.sh --quiet      as above, no output
#   check_build_system_fresh.sh --write-stamp   record the current hash
#   check_build_system_fresh.sh --print      print the current hash
set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"
STAMP="${REPO_ROOT}/.build-system-stamp"

# ABSOLUTE paths, deliberately. On the maintainer's Mac `shasum` on PATH
# resolves to an SBGrid i386 build and `openssl` to whichever conda env happens
# to be active; neither should get to decide whether a build counts as fresh.
# Both of these ship with macOS at a fixed location.
if [ -x /usr/bin/openssl ]; then
    _hash() { /usr/bin/openssl dgst -sha256 | awk '{print $NF}'; }
elif [ -x /sbin/md5 ]; then
    _hash() { /sbin/md5 -q; }
else
    echo "check_build_system_fresh.sh: no usable hash tool (/usr/bin/openssl, /sbin/md5)" >&2
    exit 2
fi

# Every file bootstrap.sh consumes. The NAME is hashed alongside the contents so
# that adding or deleting a Makefile.am counts as a change; the list is sorted
# under LC_ALL=C so the order does not depend on the filesystem or the locale.
# deps/ and third_party/ are pruned: they are vendored builds with their own
# autotools, and bootstrap.sh does not walk them.
build_system_hash() {
    {
        find . \( -path ./deps -o -path ./third_party -o -path ./.git \) -prune \
             -o -name 'Makefile.am' -print
        find macros -name '*.m4' -print
        echo ./configure.ac
    } | LC_ALL=C sort | while IFS= read -r f; do
        printf '=== %s\n' "$f"
        cat "$f"
    done | _hash
}

case "${1:-}" in
    --print)
        build_system_hash
        exit 0
        ;;
    --write-stamp)
        build_system_hash > "$STAMP"
        exit 0
        ;;
esac

quiet=0
[ "${1:-}" = "--quiet" ] && quiet=1

current=$(build_system_hash)

# No stamp means "unknown", which must be treated as STALE rather than assumed
# fresh. Assuming fresh is exactly the mistake this script exists to prevent,
# and the cost of being wrong is one bootstrap run.
if [ ! -f "$STAMP" ]; then
    if [ "$quiet" = "0" ]; then
        echo "!! build system freshness UNKNOWN: no ${STAMP##*/}" >&2
        echo "   Treating as stale. Run: ./scripts/bootstrap.sh" >&2
    fi
    exit 1
fi

recorded=$(cat "$STAMP")

if [ "$current" = "$recorded" ]; then
    [ "$quiet" = "0" ] && echo "==> build system is current (${current:0:12})"
    exit 0
fi

if [ "$quiet" = "0" ]; then
    echo "!! BUILD SYSTEM IS STALE." >&2
    echo "   configure.ac / Makefile.am / macros have changed since the last" >&2
    echo "   bootstrap, so ./configure and the Makefile.in tree no longer match" >&2
    echo "   what they were generated from." >&2
    echo "     recorded: ${recorded}" >&2
    echo "     current:  ${current}" >&2
    echo "   fix: ./scripts/bootstrap.sh" >&2
fi
exit 1

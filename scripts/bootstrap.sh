#!/bin/bash
# Bandicoot: regenerate autotools (configure, Makefile.in's) from .ac / .am.
# Run once after fresh checkout, or after editing configure.ac / Makefile.am.
set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$PWD"

# Homebrew installs libtool as glibtoolize on macOS to avoid clashing
# with the system one. Pick whichever exists.
if command -v glibtoolize >/dev/null 2>&1; then
    LIBTOOLIZE=glibtoolize
elif command -v libtoolize >/dev/null 2>&1; then
    LIBTOOLIZE=libtoolize
else
    echo "ERROR: neither glibtoolize nor libtoolize found in PATH." >&2
    echo "       brew install libtool"                              >&2
    exit 1
fi

# intltool: the tree carries po/Makefile.in.in, so any autotools regeneration
# that walks it -- `autoreconf -i` in particular, which is the reflex reaching
# for when bootstrap.sh isn't known about -- runs intltoolize and dies with the
# famously unhelpful "Your intltool is too old" when intltool is simply absent
# (IT_PROG_INTLTOOL, intltool.m4). bootstrap.sh itself does not call it, but the
# dependency is undeclared either way and was missing from BUILD.md's brew list
# until v0.1.4.15 -- it is installed on the maintainer's Mac, so nobody noticed
# until GitHub #24 built on a clean one. Name it here rather than let the
# version-number red herring send anyone hunting for a newer intltool.
if ! command -v intltoolize >/dev/null 2>&1; then
    echo "WARNING: intltoolize not found in PATH."                        >&2
    echo "         brew install intltool"                                 >&2
    echo "         (bootstrap.sh does not need it, but autoreconf does,"  >&2
    echo "          and reports it as \"Your intltool is too old\".)"     >&2
fi

BREW_PREFIX="${BREW_PREFIX:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
ACLOCAL_M4_DIR="${BREW_PREFIX}/share/aclocal"

echo "==> $LIBTOOLIZE --copy --no-warn"
$LIBTOOLIZE --copy --no-warn

echo "==> aclocal -I macros -I ${ACLOCAL_M4_DIR}"
aclocal -I macros -I "${ACLOCAL_M4_DIR}"

echo "==> autoconf"
autoconf

echo "==> automake --add-missing --copy"
automake --add-missing --copy

touch configure   # ensure newer than aclocal.m4 for stat-based make rules
echo "==> bootstrap complete"

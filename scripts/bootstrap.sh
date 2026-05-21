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

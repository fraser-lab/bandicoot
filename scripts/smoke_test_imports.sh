#!/bin/bash
# Bandicoot: runtime import smoke test for a built/installed tree.
#
#   smoke_test_imports.sh <INSTALL_DIR>
#
# Launches the SHIPPED interpreter (the embedded Python inside the Bandicoot
# binary, driven headless via `coot --no-graphics --script`) and imports the
# modules that a Coot session / bundled script actually relies on. Unlike the
# static check_install.sh, this exercises real dlopen in the real launcher
# environment, so it has no false positives -- if a module's dependency chain
# is broken (the _ssl / _ctypes / _sqlite3 class of bug), the import fails here
# exactly as it would for a user.
#
# This is the check that would have caught the v0.1.4.2..v0.1.4.8 SSL
# regression at build time: `import ssl` simply fails when libssl isn't
# bundled. It also verifies the two modules whose C library needs a specific
# build (sqlite3's load-extension symbol) by actually using them.
#
# Exits non-zero if any CRITICAL module fails to import. Optional modules
# (readline/curses/tkinter -- terminal/Tk, irrelevant to a GUI app) are probed
# for information only and never fail the run.
#
# Read-only; runs in a scratch cwd so no state files land in the tree.

set -u

INSTALL_DIR="${1:-}"
if [ -z "$INSTALL_DIR" ] || [ ! -d "$INSTALL_DIR" ]; then
    echo "!! smoke_test_imports.sh: usage: smoke_test_imports.sh <INSTALL_DIR>" >&2
    exit 2
fi
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd)"

COOT="$INSTALL_DIR/bin/coot"
if [ ! -x "$COOT" ]; then
    echo "!! smoke_test_imports.sh: no launcher at $COOT" >&2
    exit 2
fi

# Modules whose failure must fail the build. ssl/hashlib/ctypes/sqlite3 are the
# ones with external C-lib deps that have historically broken; zlib/bz2/lzma/
# decimal round out the compression/precision extensions; numpy/matplotlib are
# the bundled scientific stack; coot is the app's own Python module.
CRITICAL="ssl hashlib zlib bz2 lzma sqlite3 ctypes decimal numpy matplotlib coot"
# Probed but non-fatal (not needed by the GUI app).
OPTIONAL="readline curses _tkinter"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/bandicoot-smoke.XXXXXX")"
RES="$WORK/result.txt"
PY="$WORK/smoke.py"
trap 'rm -rf "$WORK"' EXIT

cat > "$PY" <<PYEOF
res = r"""$RES"""
critical = "$CRITICAL".split()
optional = "$OPTIONAL".split()
lines = []
def probe(m):
    try:
        mod = __import__(m)
        # Exercise the two modules whose shared lib must be the right build.
        if m == "ssl":     mod._create_unverified_context()
        if m == "sqlite3": mod.connect(":memory:").execute("create table t(x)")
        return "OK", ""
    except Exception as e:
        return "FAIL", "%s: %s" % (type(e).__name__, str(e)[:80])
for m in critical:
    st, msg = probe(m); lines.append("CRIT %-14s %s %s" % (m, st, msg))
for m in optional:
    st, msg = probe(m); lines.append("OPT  %-14s %s %s" % (m, st, msg))
open(res, "w").write("\n".join(lines) + "\n")
PYEOF

echo "==> import smoke test via $COOT (headless)"
( cd "$WORK" && "$COOT" --no-graphics --no-state-script --script "$PY" ) \
    >"$WORK/run.log" 2>&1 &
pid=$!
# Poll for completion (macOS base has no `timeout`); cap at ~120s.
i=0
while kill -0 "$pid" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -ge 120 ] && { echo "!! smoke test timed out after ${i}s; killing" >&2; kill "$pid" 2>/dev/null; break; }
    sleep 1
done

if [ ! -f "$RES" ]; then
    echo "!! smoke_test_imports: interpreter produced no result (startup crash?). See:" >&2
    tail -20 "$WORK/run.log" >&2
    exit 1
fi

crit_fail=0
while IFS= read -r line; do
    echo "   $line"
    case "$line" in
        CRIT*FAIL*) crit_fail=$((crit_fail + 1)) ;;
    esac
done < "$RES"

if [ "$crit_fail" -ne 0 ]; then
    echo "!! smoke_test_imports: FAIL — $crit_fail critical module(s) failed to import" >&2
    exit 1
fi
echo "ok  smoke_test_imports: all critical modules import"

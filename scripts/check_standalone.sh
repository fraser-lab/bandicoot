#!/bin/bash
# Bandicoot: pre-ship STANDALONE gate.
#
#   check_standalone.sh [--static-only] <INSTALL_DIR>
#
# "Standalone" means the install never reaches into a package manager or a
# build-only dependency tree (Homebrew, MacPorts, Fink, conda, Linuxbrew,
# /usr/local, the Linux deps prefix, MSYS2). The operating system, the
# user's own files and the external programs Bandicoot deliberately runs are
# outside this check.
#
# Static part (all platforms):
#   * every symlink resolves to a file inside the install;
#   * no command line of a shipped launcher or setup script names one of
#     those prefixes (comments are ignored).
# Runtime part (macOS and Linux; Windows has no tracer here):
#   * the app is started through bin/bcoot in a clean environment (temporary
#     HOME and working directory, system-only PATH) and every file access
#     under one of those prefixes is reported. macOS blocks the prefixes with
#     sandbox-exec and reads the denials from the system log; Linux records
#     accesses with strace. The GUI opens for STANDALONE_TRACE_SECONDS
#     (default 30) and is then killed.
#
# Environment:
#   STANDALONE_TRACE_SECONDS  how long the traced app runs (default 30)
#   STANDALONE_SKIP_RUNTIME=1 skip the runtime part (prints a warning)
#   DEPS_PREFIX               Linux build-dependency prefix
#                             (default /opt/bandicoot-deps)
#
# Exit: 0 clean, 1 a finding, 2 usage or the check could not run.
# Never edits the install.

set -u

STATIC_ONLY=0
INSTALL_DIR=""
for a in "$@"; do
    case "$a" in
        --static-only) STATIC_ONLY=1 ;;
        *) INSTALL_DIR="$a" ;;
    esac
done
if [ -z "$INSTALL_DIR" ] || [ ! -d "$INSTALL_DIR" ]; then
    echo "!! check_standalone.sh: usage: check_standalone.sh [--static-only] <INSTALL_DIR>" >&2
    exit 2
fi
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd -P)"
TRACE_SECONDS="${STANDALONE_TRACE_SECONDS:-30}"

case "$(uname -s)" in
    Darwin) PLATFORM=macos ;;
    Linux)  PLATFORM=linux ;;
    MINGW*|MSYS*|CYGWIN*) PLATFORM=windows ;;
    *) PLATFORM=other ;;
esac

# Prefixes that belong to a package manager or a build-only dependency tree.
FORBIDDEN=(
    /opt/homebrew /usr/local /opt/local /sw
    /home/linuxbrew/.linuxbrew
    /opt/miniconda3 /opt/anaconda3 /opt/miniforge3 /opt/mambaforge /opt/conda
    "${DEPS_PREFIX:-/opt/bandicoot-deps}"
    /mingw64 /ucrt64 /clang64 /msys64
)
if [ -n "${HOME:-}" ] && [ "${HOME%/}" != "" ]; then
    FORBIDDEN+=( "$HOME/miniconda3" "$HOME/anaconda3" "$HOME/miniforge3"
                 "$HOME/mambaforge" "$HOME/.linuxbrew" )
fi
if [ -n "${CONDA_PREFIX:-}" ]; then FORBIDDEN+=( "$CONDA_PREFIX" ); fi

# PREFIX_ALT: the prefixes as one regex alternation.
# TEXT_RE: a prefix used as a path inside a line of text (followed by "/", a
#          quote, whitespace or end of line), or a Windows MSYS2 root.
# PATH_RE: a whole path that is, or lies under, a prefix.
PREFIX_ALT=""
for p in "${FORBIDDEN[@]}"; do
    q=$(printf '%s' "$p" | sed 's/[.[\*^$()+?{|]/\\&/g')
    PREFIX_ALT="${PREFIX_ALT:+$PREFIX_ALT|}${q}"
done
TEXT_RE="(${PREFIX_ALT})(/|\"|'|[[:space:]]|\$)|[A-Za-z]:[/\\\\]msys64"
PATH_RE="^(${PREFIX_ALT})(/|\$)"
unset p q

findings=0
finding() { echo "  FINDING: $*"; findings=$((findings + 1)); }

echo "==> standalone check (${PLATFORM}): $INSTALL_DIR"

# --- static: symlinks -------------------------------------------------------
echo "    symlinks resolve inside the install"
while IFS= read -r -d '' link; do
    target="$(readlink "$link")"
    case "$target" in
        /*) finding "absolute symlink ${link#$INSTALL_DIR/} -> $target"; continue ;;
    esac
    if [ ! -e "$link" ]; then
        finding "broken symlink ${link#$INSTALL_DIR/} -> $target"
        continue
    fi
    resolved_dir="$(cd "$(dirname "$link")" && cd "$(dirname "$target")" && pwd -P)"
    case "$resolved_dir/" in
        "$INSTALL_DIR"/*) ;;
        *) finding "symlink leaves the install: ${link#$INSTALL_DIR/} -> $target" ;;
    esac
done < <(find "$INSTALL_DIR" -type l -print0)

# --- static: launcher and setup scripts --------------------------------------
echo "    launcher and setup scripts name no package-manager prefix"
scripts=()
for f in "$INSTALL_DIR"/bin/* "$INSTALL_DIR"/libexec/* "$INSTALL_DIR"/*.sh \
         "$INSTALL_DIR"/*.bat "$INSTALL_DIR"/bin/*.bat; do
    [ -f "$f" ] && [ ! -L "$f" ] || continue
    # grep -I treats a file with binary content as non-matching: text only.
    grep -Iq . "$f" 2>/dev/null && scripts+=( "$f" )
done
for f in ${scripts[@]+"${scripts[@]}"}; do
    # Comment lines are skipped: "#" for shell, "rem" and "::" for batch.
    hits=$(grep -nE "$TEXT_RE" "$f" 2>/dev/null | \
           grep -vE '^[0-9]+:[[:space:]]*(#|[Rr][Ee][Mm]([[:space:]]|$)|::)')
    if [ -n "$hits" ]; then
        while IFS= read -r h; do
            finding "${f#$INSTALL_DIR/}:${h}"
        done <<< "$hits"
    fi
done

# --- runtime ------------------------------------------------------------------
run_runtime=1
if [ "$STATIC_ONLY" = 1 ]; then
    run_runtime=0
elif [ "${STANDALONE_SKIP_RUNTIME:-0}" = 1 ]; then
    echo "    WARNING: runtime trace SKIPPED (STANDALONE_SKIP_RUNTIME=1); a fallback"
    echo "             into a package-manager prefix would not be detected."
    run_runtime=0
elif [ "$PLATFORM" != macos ] && [ "$PLATFORM" != linux ]; then
    echo "    runtime trace: not available on ${PLATFORM}; static checks only"
    run_runtime=0
fi

LAUNCHER="$INSTALL_DIR/bin/bcoot"
if [ "$run_runtime" = 1 ] && [ ! -x "$LAUNCHER" ]; then
    echo "!! check_standalone: no launcher at $LAUNCHER" >&2
    exit 2
fi

# Starts the launcher in its own process group under the given wrapper, waits
# TRACE_SECONDS, and confirms the app was running before killing the group.
# Sets RUN_OK=1 if the app was still up when the time ran out.
run_traced() {
    local workdir="$1"; shift
    local log="$workdir/app-output.log"
    RUN_OK=0
    set -m
    ( cd "$workdir/cwd" && exec "$@" > "$log" 2>&1 ) &
    local pgid=$!
    set +m
    sleep "$TRACE_SECONDS"
    if pgrep -g "$pgid" -f "libexec/Bandicoot" > /dev/null 2>&1; then
        RUN_OK=1
    fi
    kill -9 -- "-$pgid" 2>/dev/null
    wait "$pgid" 2>/dev/null
    if [ "$RUN_OK" != 1 ]; then
        echo "!! check_standalone: the app was not running after ${TRACE_SECONDS}s;" >&2
        echo "   last lines of its output:" >&2
        tail -15 "$log" >&2
    fi
}

clean_env() {
    # A clean environment: temporary home, system PATH, display passed through.
    local home="$1"
    local e=( HOME="$home" USER="${USER:-bandicoot}" TMPDIR="${TMPDIR:-/tmp}"
              PATH=/usr/bin:/bin:/usr/sbin:/sbin )
    [ -n "${DISPLAY:-}" ]          && e+=( DISPLAY="$DISPLAY" )
    [ -n "${WAYLAND_DISPLAY:-}" ]  && e+=( WAYLAND_DISPLAY="$WAYLAND_DISPLAY" )
    [ -n "${XDG_RUNTIME_DIR:-}" ]  && e+=( XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" )
    [ -n "${XAUTHORITY:-}" ]       && e+=( XAUTHORITY="$XAUTHORITY" )
    printf '%s\n' "${e[@]}"
}

if [ "$run_runtime" = 1 ]; then
    work="$(mktemp -d "${TMPDIR:-/tmp}/bandicoot-standalone.XXXXXX")"
    mkdir -p "$work/home" "$work/cwd"
    env_list=()
    while IFS= read -r kv; do env_list+=( "$kv" ); done < <(clean_env "$work/home")

    if [ "$PLATFORM" = macos ]; then
        echo "    runtime trace: sandbox-exec, ${TRACE_SECONDS}s (a Bandicoot window opens)"
        # The tag is appended to each denial this profile logs, which singles
        # out this run's processes (children included) in the system log.
        tag="bandicoot-standalone-$$-$RANDOM"
        profile="$work/deny.sb"
        {
            echo '(version 1)'
            echo '(allow default)'
            echo '(deny file-read* file-write* file-map-executable'
            for p in "${FORBIDDEN[@]}"; do printf '  (subpath "%s")\n' "$p"; done
            printf '  (with message "%s"))\n' "$tag"
        } > "$profile"
        t0="$(date '+%Y-%m-%d %H:%M:%S')"
        run_traced "$work" env -i "${env_list[@]}" \
            /usr/bin/sandbox-exec -f "$profile" "$LAUNCHER"
        [ "$RUN_OK" = 1 ] || { rm -rf "$work"; exit 2; }
        sleep 3   # let the kernel's denials reach the log
        if ! /usr/bin/log show --start "$t0" --style compact --info \
                 --predicate "eventMessage CONTAINS \"$tag\"" \
                 > "$work/denials" 2>"$work/log.err"; then
            echo "!! check_standalone: could not read the system log:" >&2
            cat "$work/log.err" >&2
            rm -rf "$work"
            exit 2
        fi
        while IFS= read -r what; do
            finding "runtime access $what"
        done < <(grep -E "Sandbox: .* deny\([0-9]+\) " "$work/denials" | \
                 sed -E 's/.*Sandbox: ([^(]*)\([0-9]+\) deny\([0-9]+\) /\1: /' | sort -u)
    else
        if ! command -v strace > /dev/null 2>&1; then
            echo "!! check_standalone: strace is required for the runtime trace" >&2
            echo "   (sudo apt install strace), or set STANDALONE_SKIP_RUNTIME=1." >&2
            rm -rf "$work"
            exit 2
        fi
        if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
            echo "!! check_standalone: the runtime trace needs a display (none set)," >&2
            echo "   or set STANDALONE_SKIP_RUNTIME=1." >&2
            rm -rf "$work"
            exit 2
        fi
        echo "    runtime trace: strace, ${TRACE_SECONDS}s (a Bandicoot window opens)"
        run_traced "$work" env -i "${env_list[@]}" \
            strace -f -qq -e trace=%file -o "$work/strace.log" "$LAUNCHER"
        [ "$RUN_OK" = 1 ] || { rm -rf "$work"; exit 2; }
        # strace quotes every path argument; report each distinct one.
        grep -oE "\"[^\"]*\"" "$work/strace.log" 2>/dev/null | tr -d '"' | \
            grep -E "$PATH_RE" | sort -u > "$work/hits"
        while IFS= read -r h; do finding "runtime access $h"; done < "$work/hits"
    fi
    rm -rf "$work"
fi

if [ "$findings" -ne 0 ]; then
    echo "!! check_standalone: FAIL -- $findings finding(s) above. The install reaches"
    echo "   into a package manager or build-only dependency tree."
    exit 1
fi
echo "    OK"
exit 0

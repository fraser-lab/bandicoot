#!/bin/bash
# Bandicoot one-time install setup. Run after extracting the tarball:
#
#   cd bandicoot-0.0.0.1
#   ./setup.sh
#
# Does the friendly post-install steps so the user doesn't have to:
#   1. Strips com.apple.quarantine recursively so macOS doesn't gate every
#      binary on first launch.
#   2. Ad-hoc-codesigns the Mach-O files so Gatekeeper sees a (locally)
#      valid signature instead of an unsigned binary.
#   3. Checks Homebrew + Miniconda prerequisites and reports clearly if
#      anything is missing.
#   4. Installs .desktop / appdata.xml into ~/.local/share/applications/
#      so Spotlight / Launchpad can find Bandicoot.
#   5. Optionally adds the install's bin/ to PATH by writing a tagged
#      `export PATH=...` line to your shell rc (use --add-to-path).
#
# Idempotent: re-running is safe. Never uses sudo.
#
# Exit code is 0 on success; non-zero if a prerequisite was missing
# (Bandicoot may still launch but some features won't work).

set -u

INSTALL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARROW="==>"
WARN="!!"
CHECK="ok"

# ----------------------------------------------------------------------
# argument parsing
# ----------------------------------------------------------------------

ADD_TO_PATH=0
SKIP_SIGN=0
for arg in "$@"; do
    case "$arg" in
        --add-to-path)  ADD_TO_PATH=1 ;;
        --no-codesign)  SKIP_SIGN=1 ;;
        -h|--help)
            sed -n '2,18p' "$0"
            echo "Flags:"
            echo "  --add-to-path   add Bandicoot's bin/ to PATH via your shell rc"
            echo "  --no-codesign   skip the ad-hoc codesign step"
            echo "  -h, --help      this message"
            exit 0
            ;;
        *)
            echo "$WARN unknown argument: $arg (try --help)" >&2
            exit 2
            ;;
    esac
done

PROBLEMS=0
note_problem() { PROBLEMS=$(( PROBLEMS + 1 )); }

# ----------------------------------------------------------------------
# sanity: macOS + arch
# ----------------------------------------------------------------------

if [ "$(uname)" != "Darwin" ]; then
    echo "$WARN this setup script is for macOS only. Skipping." >&2
    exit 1
fi
if [ "$(uname -m)" != "arm64" ]; then
    echo "$WARN this build is for Apple Silicon (arm64). Detected: $(uname -m)." >&2
    note_problem
fi

echo "$ARROW Bandicoot install at: $INSTALL_DIR"

# ----------------------------------------------------------------------
# 1. quarantine
# ----------------------------------------------------------------------

echo "$ARROW Stripping com.apple.quarantine (so Gatekeeper doesn't prompt)..."
xattr -dr com.apple.quarantine "$INSTALL_DIR" 2>/dev/null || true
echo "    $CHECK quarantine cleared"

# ----------------------------------------------------------------------
# 2. ad-hoc codesign (local signature; not Apple Developer notarization)
# ----------------------------------------------------------------------

if [ "$SKIP_SIGN" -eq 0 ]; then
    if command -v codesign >/dev/null 2>&1; then
        echo "$ARROW Ad-hoc-codesigning Mach-O files (suppresses repeat Gatekeeper prompts)..."
        SIGNED=0
        for d in "$INSTALL_DIR/libexec" "$INSTALL_DIR/lib" "$INSTALL_DIR/bin"; do
            [ -d "$d" ] || continue
            while IFS= read -r f; do
                if file -b "$f" 2>/dev/null | grep -q "Mach-O"; then
                    codesign --force --sign - "$f" >/dev/null 2>&1 && \
                        SIGNED=$(( SIGNED + 1 )) || true
                fi
            done < <(find "$d" -type f)
        done
        echo "    $CHECK signed $SIGNED Mach-O files"
    else
        echo "$WARN codesign not found in PATH; skipping signing step." >&2
    fi
fi

# ----------------------------------------------------------------------
# 3. prerequisite checks
# ----------------------------------------------------------------------

echo "$ARROW Checking Homebrew at /opt/homebrew..."
HOMEBREW_PREFIX="/opt/homebrew"
REQUIRED_BREWS="gtk+ gtkglext freeglut gsl cairo libpng sqlite bzip2 boost"
if [ ! -x "$HOMEBREW_PREFIX/bin/brew" ]; then
    echo "    $WARN Homebrew not found at $HOMEBREW_PREFIX/bin/brew" >&2
    echo "      Install from https://brew.sh, then run:" >&2
    echo "        brew install $REQUIRED_BREWS" >&2
    note_problem
else
    MISSING_BREWS=""
    for pkg in $REQUIRED_BREWS; do
        if ! "$HOMEBREW_PREFIX/bin/brew" list --formula --versions "$pkg" >/dev/null 2>&1; then
            MISSING_BREWS="$MISSING_BREWS $pkg"
        fi
    done
    if [ -n "$MISSING_BREWS" ]; then
        echo "    $WARN missing Homebrew packages:$MISSING_BREWS" >&2
        echo "      To install:  brew install$MISSING_BREWS" >&2
        note_problem
    else
        echo "    $CHECK all required Homebrew packages present"
    fi
fi

echo "$ARROW Checking Miniconda at /opt/miniconda3..."
CONDA_PREFIX="/opt/miniconda3"
REQUIRED_CONDA="clipper-cif clipper-contrib clipper-core clipper-ccp4 \
                clipper-mmdb clipper-minimol clipper-phs clipper-cns \
                mmdb2 ssm"
if [ ! -d "$CONDA_PREFIX/lib" ]; then
    echo "    $WARN Miniconda lib dir not found at $CONDA_PREFIX/lib" >&2
    echo "      Install Miniconda from https://docs.conda.io/, then:" >&2
    echo "        conda install -c conda-forge $REQUIRED_CONDA" >&2
    note_problem
else
    # Bandicoot dynamically loads lib<pkg>.dylib at runtime, so the
    # check is "is the dylib physically on disk", not "is conda's package
    # database aware of it". Some clipper/mmdb installs land the libs at
    # /opt/miniconda3/lib without a tracked conda package (e.g. installed
    # into a different env, or copied in by hand). Prefer the file check.
    MISSING_CONDA=""
    INSTALLED_LIST=""
    if [ -x "$CONDA_PREFIX/bin/conda" ]; then
        INSTALLED_LIST="$("$CONDA_PREFIX/bin/conda" list 2>/dev/null | awk '{print $1}')"
    fi
    for pkg in $REQUIRED_CONDA; do
        if [ -f "$CONDA_PREFIX/lib/lib${pkg}.dylib" ]; then
            continue   # dylib present on disk — good enough
        fi
        if echo "$INSTALLED_LIST" | grep -qx "$pkg"; then
            continue   # tracked by conda — good enough
        fi
        MISSING_CONDA="$MISSING_CONDA $pkg"
    done
    if [ -n "$MISSING_CONDA" ]; then
        echo "    $WARN missing conda packages (no lib<pkg>.dylib found):$MISSING_CONDA" >&2
        echo "      To install:  conda install -c conda-forge$MISSING_CONDA" >&2
        note_problem
    else
        echo "    $CHECK all required conda libraries present"
    fi
fi

# ----------------------------------------------------------------------
# 4. .desktop + appdata.xml in user-local applications dir
# ----------------------------------------------------------------------

echo "$ARROW Registering app metadata for Spotlight / Launchpad..."
USER_APPS="$HOME/.local/share/applications"
USER_APPDATA="$HOME/.local/share/appdata"
mkdir -p "$USER_APPS" "$USER_APPDATA"
if [ -f "$INSTALL_DIR/share/applications/coot.desktop" ]; then
    cp -f "$INSTALL_DIR/share/applications/coot.desktop" "$USER_APPS/bandicoot.desktop"
    # Patch Exec= to absolute path of bcoot so users don't need PATH set
    if [ -x "$INSTALL_DIR/bin/bcoot" ]; then
        sed -i.bak "s|^Exec=.*|Exec=$INSTALL_DIR/bin/bcoot|" "$USER_APPS/bandicoot.desktop" && \
            rm -f "$USER_APPS/bandicoot.desktop.bak"
    fi
    echo "    $CHECK $USER_APPS/bandicoot.desktop"
fi
if [ -f "$INSTALL_DIR/share/appdata/coot.appdata.xml" ]; then
    cp -f "$INSTALL_DIR/share/appdata/coot.appdata.xml" "$USER_APPDATA/bandicoot.appdata.xml"
    echo "    $CHECK $USER_APPDATA/bandicoot.appdata.xml"
fi

# ----------------------------------------------------------------------
# 5. optional PATH-via-shell-rc
# ----------------------------------------------------------------------

if [ "$ADD_TO_PATH" -eq 1 ]; then
    # Pick the right rc file from the user's login shell. On macOS the
    # bash login shell reads ~/.bash_profile (not ~/.bashrc); zsh reads
    # ~/.zshrc since macOS Catalina made zsh the default.
    case "$(basename "${SHELL:-/bin/bash}")" in
        zsh)  RC_FILE="$HOME/.zshrc" ;;
        bash) RC_FILE="$HOME/.bash_profile" ;;
        *)    RC_FILE="" ;;
    esac

    # Tagged line — grep target for idempotent re-runs and a hint for
    # users grepping their rc later to understand what added this.
    PATH_TAG="# bandicoot:path-entry"
    PATH_LINE="export PATH=\"$INSTALL_DIR/bin:\$PATH\"  $PATH_TAG"

    if [ -z "$RC_FILE" ]; then
        echo "$WARN Couldn't auto-detect your shell ($SHELL). Add this line to your" >&2
        echo "       shell rc manually:" >&2
        echo "         $PATH_LINE" >&2
        note_problem
    else
        # Remove any prior tagged line (handles re-runs after a move/reinstall).
        if [ -f "$RC_FILE" ] && grep -q "$PATH_TAG" "$RC_FILE"; then
            tmp="$RC_FILE.bandicoot-tmp"
            grep -v "$PATH_TAG" "$RC_FILE" > "$tmp" && mv "$tmp" "$RC_FILE"
            echo "$ARROW Replacing prior bandicoot PATH entry in $RC_FILE"
        else
            echo "$ARROW Adding Bandicoot to PATH via $RC_FILE"
        fi
        printf '\n%s\n' "$PATH_LINE" >> "$RC_FILE"
        echo "    $CHECK appended: $PATH_LINE"
        echo "    Open a new terminal, or run:  source $RC_FILE"
    fi
fi

# ----------------------------------------------------------------------
# summary
# ----------------------------------------------------------------------

echo ""
if [ "$PROBLEMS" -eq 0 ]; then
    echo "Setup complete. Launch with:"
    echo "    $INSTALL_DIR/bin/bcoot"
    exit 0
else
    echo "Setup finished with $PROBLEMS problem(s) above. Bandicoot may still launch,"
    echo "but please resolve the warnings before reporting issues."
    exit 1
fi

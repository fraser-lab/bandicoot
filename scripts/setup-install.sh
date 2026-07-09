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
#   3. Checks Homebrew prerequisites and reports clearly if any are
#      missing. (Clipper / mmdb2 / ssm / fftw2 ship inside the tarball
#      now, so Miniconda is no longer a runtime dependency.)
#   4. Regenerates the gdk-pixbuf loaders.cache for the bundled image
#      loaders so .svg icons (water-drop, etc.) render correctly.
#   5. Installs .desktop / appdata.xml into ~/.local/share/applications/
#      so Spotlight / Launchpad can find Bandicoot.
#   6. Optionally adds the install's bin/ to PATH by writing a tagged
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
            sed -n '2,20p' "$0"
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
    if [ -x "$INSTALL_DIR/codesign-install.sh" ]; then
        # Delegate to the shared signing helper shipped alongside this
        # script -- the single source of truth for the signing policy. See
        # its header for why the tree must be re-signed after the
        # install_name_tool relocation the build performs.
        "$INSTALL_DIR/codesign-install.sh" "$INSTALL_DIR" || note_problem
    elif command -v codesign >/dev/null 2>&1; then
        echo "$ARROW Ad-hoc-codesigning Mach-O files (suppresses repeat Gatekeeper prompts)..."

        # Bandicoot v0.1.0.0: coot-bin embeds Python and dlopens
        # conda-shipped libraries (libpython3.13, etc.). For that to
        # work under macOS's hardened runtime, coot-bin needs three
        # entitlements:
        #   disable-library-validation -- allow loading libs signed
        #     by anyone (not just our ad-hoc signer).
        #   allow-unsigned-executable-memory + allow-jit -- some
        #     Python extensions (ctypes, cffi) generate executable
        #     memory at runtime.
        # Without them, dyld silently SIGKILLs the process. Generated
        # plist is dropped here so we don't depend on ship-time state.
        ENT_PLIST="$INSTALL_DIR/.bandicoot-coot-bin.entitlements"
        cat > "$ENT_PLIST" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>com.apple.security.cs.disable-library-validation</key><true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key><true/>
  <key>com.apple.security.cs.allow-jit</key><true/>
</dict>
</plist>
EOF

        SIGNED=0
        for d in "$INSTALL_DIR/libexec" "$INSTALL_DIR/lib" "$INSTALL_DIR/bin"; do
            [ -d "$d" ] || continue
            while IFS= read -r f; do
                if file -b "$f" 2>/dev/null | grep -q "Mach-O"; then
                    case "$(basename "$f")" in
                        Bandicoot|coot-bin)
                            # The main executable (installed as "Bandicoot",
                            # formerly "coot-bin"). Both matched for safety.
                            # Apply with hardened runtime + entitlements.
                            codesign --force --sign - \
                                --entitlements "$ENT_PLIST" \
                                --options runtime \
                                "$f" >/dev/null 2>&1 && \
                                SIGNED=$(( SIGNED + 1 )) || true
                            ;;
                        *)
                            # Other Mach-O files (bundled dylibs, helper
                            # binaries): plain ad-hoc, no entitlements.
                            codesign --force --sign - "$f" >/dev/null 2>&1 && \
                                SIGNED=$(( SIGNED + 1 )) || true
                            ;;
                    esac
                fi
            done < <(find "$d" -type f)
        done
        echo "    $CHECK signed $SIGNED Mach-O files (Bandicoot gets Python-enabling entitlements)"
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

# clipper / mmdb2 / ssm / ccp4c / fftw2 / libc++ are now bundled into
# the install tree (lib/), so this script no longer needs to verify
# that the user has them installed via conda. Miniconda is still the
# build environment for those libraries, but end users don't need it.

# ----------------------------------------------------------------------
# 4. regenerate gdk-pixbuf loaders.cache for the bundled loaders
# ----------------------------------------------------------------------
#
# Bandicoot ships its own copy of the gdk-pixbuf loaders (PNG / JPEG /
# SVG / ...) under lib/gdk-pixbuf-2.0/2.10.0/loaders/. The companion
# loaders.cache file lists each loader's absolute path, so it has to be
# (re)generated on the user's machine — wherever they extracted the
# tarball. coot.in points GTK at this cache via GDK_PIXBUF_MODULE_FILE.

PIXBUF_LOADERS_DIR="$INSTALL_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders"
PIXBUF_CACHE="$INSTALL_DIR/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache"
QUERY_BIN="$HOMEBREW_PREFIX/bin/gdk-pixbuf-query-loaders"

if [ -d "$PIXBUF_LOADERS_DIR" ]; then
    echo "$ARROW Generating gdk-pixbuf loaders.cache..."
    if [ ! -x "$QUERY_BIN" ]; then
        echo "    $WARN $QUERY_BIN not found." >&2
        echo "      Install Homebrew's gtk+ (it pulls gdk-pixbuf):" >&2
        echo "        brew install gtk+" >&2
        note_problem
    else
        # Enumerate the bundled loaders explicitly so the cache lists
        # the install-relative paths and not whatever is on $PATH.
        LOADER_FILES=()
        while IFS= read -r f; do
            LOADER_FILES+=("$f")
        done < <(find "$PIXBUF_LOADERS_DIR" -type f \( -name '*.so' -o -name '*.dylib' \) | sort)
        if [ "${#LOADER_FILES[@]}" -eq 0 ]; then
            echo "    $WARN no loaders found under $PIXBUF_LOADERS_DIR" >&2
            note_problem
        else
            # GDK_PIXBUF_MODULEDIR is informational here — the explicit
            # file args determine which loaders enter the cache.
            if GDK_PIXBUF_MODULEDIR="$PIXBUF_LOADERS_DIR" \
               "$QUERY_BIN" "${LOADER_FILES[@]}" > "$PIXBUF_CACHE.tmp" 2>/dev/null; then
                mv "$PIXBUF_CACHE.tmp" "$PIXBUF_CACHE"
                echo "    $CHECK wrote $PIXBUF_CACHE (${#LOADER_FILES[@]} loaders)"
            else
                rm -f "$PIXBUF_CACHE.tmp"
                echo "    $WARN gdk-pixbuf-query-loaders failed" >&2
                note_problem
            fi
        fi
    fi
fi

# ----------------------------------------------------------------------
# 5. .desktop + appdata.xml in user-local applications dir
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
# 6. optional PATH-via-shell-rc
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

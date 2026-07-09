#!/bin/bash
# Bandicoot: ad-hoc codesign the installed Mach-O tree.
#
#   codesign-install.sh <INSTALL_DIR>
#
# Why this is its own script (and why it must run at BUILD time, not only
# from the user's setup.sh):
#
# The relocation/bundling steps (make_relocatable.sh, bundle_conda_deps.sh,
# bundle_pixbuf_loaders.sh) rewrite Mach-O load commands with
# install_name_tool. install_name_tool INVALIDATES the code signature of
# every file it edits -- the on-disk pages no longer match the signature's
# page hashes. When dyld later maps such a page, the kernel SIGKILLs the
# process with "Code Signature Invalid" (a bare `Killed: 9`, often partway
# through startup as the bad library is lazily loaded). So every Mach-O we
# touch has to be RE-signed after the last install_name_tool pass.
#
# build.sh calls this immediately after relocation so a freshly built
# install runs without waiting for the end-user setup.sh. setup-install.sh
# also calls it (via the copy dropped in the install root) so the shipped
# tarball is self-healing after extraction.
#
# Signing policy (matches the historical setup.sh behaviour):
#   * The main executable (installed as "Bandicoot", formerly "coot-bin")
#     is signed with the hardened runtime + Python-enabling entitlements
#     (disable-library-validation so it can dlopen conda-signed libs;
#     allow-unsigned-executable-memory / allow-jit for ctypes/cffi).
#   * Every other Mach-O (bundled dylibs, .so pixbuf loaders, helper
#     binaries) gets a plain ad-hoc signature.
#
# Ends with a verify pass: if any Mach-O still fails codesign --verify the
# script exits non-zero, so a broken sign step fails the build loudly
# instead of shipping a binary that SIGKILLs on launch.
#
# Idempotent and never uses sudo.

set -u

INSTALL_DIR="${1:-}"
if [ -z "$INSTALL_DIR" ] || [ ! -d "$INSTALL_DIR" ]; then
    echo "!! codesign-install.sh: usage: codesign-install.sh <INSTALL_DIR>" >&2
    exit 2
fi
INSTALL_DIR="$(cd "$INSTALL_DIR" && pwd)"

ARROW="==>"
WARN="!!"
CHECK="ok"

if ! command -v codesign >/dev/null 2>&1; then
    echo "$WARN codesign not found in PATH; cannot sign $INSTALL_DIR" >&2
    exit 1
fi

echo "$ARROW Ad-hoc-codesigning Mach-O files in $INSTALL_DIR ..."

# Entitlements for the main binary. Written into the install root (not a
# temp file) so the signature's referenced plist is reproducible and the
# state doesn't depend on ship-time cleanup.
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
                    codesign --force --sign - \
                        --entitlements "$ENT_PLIST" \
                        --options runtime \
                        "$f" >/dev/null 2>&1 && \
                        SIGNED=$(( SIGNED + 1 )) || \
                        echo "    $WARN failed to sign $f" >&2
                    ;;
                *)
                    codesign --force --sign - "$f" >/dev/null 2>&1 && \
                        SIGNED=$(( SIGNED + 1 )) || \
                        echo "    $WARN failed to sign $f" >&2
                    ;;
            esac
        fi
    done < <(find "$d" -type f)
done
echo "    $CHECK signed $SIGNED Mach-O files"

# Verify pass: nothing should still fail its signature. Report and fail if
# it does -- a broken sign step must not ship silently.
echo "$ARROW Verifying signatures ..."
BAD=0
for d in "$INSTALL_DIR/libexec" "$INSTALL_DIR/lib" "$INSTALL_DIR/bin"; do
    [ -d "$d" ] || continue
    while IFS= read -r f; do
        if file -b "$f" 2>/dev/null | grep -q "Mach-O"; then
            if ! codesign --verify --strict "$f" >/dev/null 2>&1; then
                echo "    $WARN INVALID signature: $f" >&2
                BAD=$(( BAD + 1 ))
            fi
        fi
    done < <(find "$d" -type f)
done
if [ "$BAD" -ne 0 ]; then
    echo "    $WARN $BAD Mach-O file(s) still have invalid signatures" >&2
    exit 1
fi
echo "    $CHECK all Mach-O signatures valid"

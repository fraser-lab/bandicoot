#!/bin/bash
# Verify the shipped splash PNG shows the release version number.
#
# The splash PNG is hand-edited with each version's number (e.g.
# "v0.1.4.10"). Nothing else enforces that it was actually bumped, so a stale
# splash (still "v0.1.4.9" while releasing 0.1.4.10) can ship unnoticed. The
# version lives as PIXELS in the PNG, so this OCRs it and fails the release if
# BANDICOOT_VERSION is not present on the splash.
#
# OCR uses macOS's built-in Vision framework via `swift` -- no external tool
# (tesseract etc.) required. If `swift` is unavailable, the check WARNs and
# passes (never blocks a release on missing tooling).
#
# Usage:  check_splash_version.sh <splash.png> <version>
# Exit:   0 = version found (or skipped);  1 = mismatch / missing splash.

set -u

SPLASH="${1:?usage: check_splash_version.sh <splash.png> <version>}"
VERSION="${2:?usage: check_splash_version.sh <splash.png> <version>}"

if [ ! -f "$SPLASH" ]; then
    echo "!! splash check: FAIL — splash not found: $SPLASH" >&2
    exit 1
fi

if ! command -v swift >/dev/null 2>&1; then
    echo "==  splash check: swift not found; skipping OCR version check (WARN)" >&2
    exit 0
fi

OCR_SWIFT="$(mktemp -t bcsplashocr)" || { echo "!! splash check: mktemp failed (WARN)" >&2; exit 0; }
mv "$OCR_SWIFT" "$OCR_SWIFT.swift"; OCR_SWIFT="$OCR_SWIFT.swift"
trap 'rm -f "$OCR_SWIFT"' EXIT

cat > "$OCR_SWIFT" <<'SWIFT'
import Foundation
import Vision
import AppKit
guard CommandLine.arguments.count > 1 else { exit(2) }
guard let img = NSImage(contentsOfFile: CommandLine.arguments[1]),
      let tiff = img.tiffRepresentation,
      let bmp = NSBitmapImageRep(data: tiff),
      let cg = bmp.cgImage else {
    FileHandle.standardError.write("cannot load image\n".data(using: .utf8)!); exit(2)
}
let req = VNRecognizeTextRequest()
req.recognitionLevel = .accurate
req.usesLanguageCorrection = false           // don't "correct" a version string
let handler = VNImageRequestHandler(cgImage: cg, options: [:])
do { try handler.perform([req]) } catch {
    FileHandle.standardError.write("OCR failed\n".data(using: .utf8)!); exit(2)
}
for obs in (req.results ?? []) {
    if let t = obs.topCandidates(1).first { print(t.string) }
}
SWIFT

OCR_OUT="$(swift "$OCR_SWIFT" "$SPLASH" 2>/dev/null)"
if [ $? -ne 0 ]; then
    echo "==  splash check: OCR did not run; skipping (WARN)" >&2
    exit 0
fi

# Reduce to bare version digits so "v0.1.4.10" / "V0.1.4.10" / spacing all match.
# Compare PER LINE (never concatenate across lines -- that could fabricate a
# match from two adjacent numbers).
#
# Normalise the classic OCR letter->digit look-alikes (O->0, I/L->1, S->5, B->8,
# Z->2, G->6) BEFORE discarding non-digits. Vision reads the leading "0" of a
# "V0.1.4.11" splash as the letter "O" (the digit sits right after the V), so
# without this the "0" is stripped as a letter and the version never matches.
# Only letters become digits here; existing digits are untouched, and the full
# dotted version still has to appear, so this can't fabricate a wrong-version
# pass.
strip_to_version() { tr '[:lower:]' '[:upper:]' | tr 'OILSBZG' '0115826' | tr -cd '0-9.'; }
NEEDLE="$(printf '%s' "$VERSION" | strip_to_version)"

found=0
while IFS= read -r line; do
    ln="$(printf '%s' "$line" | strip_to_version)"
    case "$ln" in *"$NEEDLE"*) found=1; break ;; esac
done <<EOF
$OCR_OUT
EOF

if [ "$found" -eq 1 ]; then
    echo "ok  splash check: bandicoot-splash.png shows version $VERSION"
    exit 0
fi

echo "!! splash check: FAIL — splash PNG does not show release version $VERSION" >&2
echo "   OCR read: [$(printf '%s' "$OCR_OUT" | tr '\n' ' ' | sed 's/  */ /g')]" >&2
echo "   Update pixmaps/bandicoot-splash.png to show $VERSION, rebuild, and retry." >&2
exit 1

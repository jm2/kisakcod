#!/usr/bin/env bash
# Package the macOS release dmg (ADR-0012): KisakCOD.app from bundle.sh, the
# headless dedicated server, README, LICENSE and THIRD-PARTY-NOTICES.
#
# Usage:
#   package_dmg.sh --app <KisakCOD.app> --dedi <KisakCOD-dedi> --out <dir>
#                  [--version <version>]
#
# Writes <dir>/kisakcod-<version>-macos-arm64.dmg, then mounts it read-only and
# checks what a player will copy out of it: the app's signature and closure,
# and a dedicated server that links only the OS.
set -euo pipefail

APP="" DEDI="" OUT="" VERSION="0.1.0"
while [ $# -gt 0 ]; do
    case "$1" in
        --app) APP="$2"; shift 2 ;;
        --dedi) DEDI="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        *) echo "dmg: unknown argument $1" >&2; exit 2 ;;
    esac
done
for v in APP DEDI OUT; do
    [ -n "${!v}" ] || { echo "dmg: --$(echo "$v" | tr '[:upper:]' '[:lower:]') is required" >&2; exit 2; }
done
HERE="$(cd "$(dirname "$0")" && pwd -P)"
ROOT="$(cd "$HERE/../../.." && pwd -P)"
NOTICES="$APP/Contents/Resources/THIRD-PARTY-NOTICES"
for f in "$APP/Contents/MacOS/KisakCOD-mp" "$NOTICES" "$DEDI" "$ROOT/LICENSE"; do
    [ -f "$f" ] || { echo "dmg: missing $f" >&2; exit 1; }
done
command -v hdiutil >/dev/null || { echo "dmg: hdiutil not found" >&2; exit 1; }

# The dedicated server is a single file: it must link only the OS.
check_dedi() {
    local dep
    while read -r dep; do
        case "$dep" in
            /usr/lib/*|/System/Library/*) ;;
            *) echo "dmg: $1 links $dep" >&2; return 1 ;;
        esac
    done < <(python3 "$HERE/native_closure.py" imports "$1")
    if [ -n "$(python3 "$HERE/native_closure.py" rpaths "$1")" ]; then
        echo "dmg: $1 carries run paths" >&2; return 1
    fi
}
check_dedi "$DEDI"

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/kisakcod-dmg.XXXXXX")"
MOUNT="" DMG=""
# A dmg that failed a check is removed, so a bad image is never left to ship.
cleanup() {
    local status=$?
    [ -z "$MOUNT" ] || hdiutil detach "$MOUNT" -quiet 2>/dev/null || true
    rm -rf "$STAGE"
    [ $status -eq 0 ] || [ -z "$DMG" ] || rm -f "$DMG"
}
trap cleanup EXIT
VOLUME="$STAGE/KisakCOD $VERSION"
mkdir -p "$VOLUME/Dedicated Server"
ditto "$APP" "$VOLUME/KisakCOD.app"
ln -s /Applications "$VOLUME/Applications"
cp "$DEDI" "$VOLUME/Dedicated Server/KisakCOD-dedi"
chmod 755 "$VOLUME/Dedicated Server/KisakCOD-dedi"
sed "s/@VERSION@/$VERSION/g" "$HERE/dmg-README.txt" > "$VOLUME/README.txt"
cp "$ROOT/LICENSE" "$VOLUME/LICENSE.txt"
cp "$NOTICES" "$VOLUME/THIRD-PARTY-NOTICES.txt"

mkdir -p "$OUT"
DMG="$OUT/kisakcod-$VERSION-macos-arm64.dmg"
rm -f "$DMG"
hdiutil create -quiet -volname "KisakCOD $VERSION" -srcfolder "$VOLUME" \
    -fs HFS+ -format UDZO -ov "$DMG"
hdiutil verify -quiet "$DMG"

# Check the copy a player gets, not the staging tree.
MOUNT="$STAGE/mnt"
mkdir -p "$MOUNT"
hdiutil attach -quiet -readonly -nobrowse -mountpoint "$MOUNT" "$DMG"
for f in KisakCOD.app/Contents/MacOS/KisakCOD-mp "Dedicated Server/KisakCOD-dedi" \
         README.txt LICENSE.txt THIRD-PARTY-NOTICES.txt; do
    [ -e "$MOUNT/$f" ] || { echo "dmg: image lacks $f" >&2; exit 1; }
done
[ "$(readlink "$MOUNT/Applications")" = /Applications ] || {
    echo "dmg: image lacks the Applications link" >&2; exit 1; }
codesign --verify --deep --strict "$MOUNT/KisakCOD.app" || {
    echo "dmg: app signature does not verify on the image" >&2; exit 1; }
python3 "$HERE/native_closure.py" bundle "$MOUNT/KisakCOD.app" >/dev/null || {
    echo "dmg: app closure fails on the image" >&2; exit 1; }
bash "$HERE/check_licenses.sh" "$MOUNT/KisakCOD.app" >/dev/null || {
    echo "dmg: app license texts fail on the image" >&2; exit 1; }
check_dedi "$MOUNT/Dedicated Server/KisakCOD-dedi"
hdiutil detach -quiet "$MOUNT"
MOUNT=""
echo "dmg: $DMG ok ($(du -h "$DMG" | cut -f1 | tr -d ' '), sha256 $(shasum -a 256 "$DMG" | cut -d' ' -f1))"

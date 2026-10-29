#!/usr/bin/env bash
# package_dmg.sh (ADR-0012): builds a tiny signed app and server with clang,
# packages them, and checks a broken input fails without leaving a dmg.
# macOS only: it needs clang, codesign and hdiutil.
set -euo pipefail

repo_root=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
packager="$repo_root/scripts/platform/macos/package_dmg.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/kisak-dmg-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
failures=0

echo 'int main(void) { return 0; }' > "$work/main.c"
link() { clang -mmacosx-version-min=26.0 "$work/main.c" -o "$1" "${@:2}"; }

make_app() {
    local app="$work/$1/KisakCOD.app"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/Resources/licenses/zstd"
    # One framework, linked the way bundle.sh leaves the real ones.
    echo 'int zstd_probe(void) { return 1; }' > "$work/lib.c"
    clang -mmacosx-version-min=26.0 -dynamiclib "$work/lib.c" \
        -install_name @rpath/libzstd.1.dylib -o "$app/Contents/Frameworks/libzstd.1.dylib"
    echo "zstd license" > "$app/Contents/Resources/licenses/zstd/LICENSE"
    link "$app/Contents/MacOS/KisakCOD-mp" "$app/Contents/Frameworks/libzstd.1.dylib" \
        -Wl,-rpath,@executable_path/../Frameworks
    cat > "$app/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>KisakCOD-mp</string>
<key>CFBundleIdentifier</key><string>org.kisakcod.client.test</string>
<key>LSMinimumSystemVersion</key><string>26.0</string>
</dict></plist>
PLIST
    echo "notices" > "$app/Contents/Resources/THIRD-PARTY-NOTICES"
    codesign --force --sign - "$app" >/dev/null 2>&1
    printf '%s' "$app"
}

check() {
    local name=$1 want=$2 pattern=$3 out status=0
    shift 3
    rm -rf "$work/out"
    out=$(bash "$packager" "$@" --out "$work/out" --version 9.9.9 2>&1) || status=$?
    local dmg="$work/out/kisakcod-9.9.9-macos-arm64.dmg"
    if [ "$want" = pass ] && { [ $status -ne 0 ] || [ ! -s "$dmg" ]; }; then
        echo "FAIL $name: expected a dmg, got status $status"; echo "$out"; failures=$((failures + 1))
    elif [ "$want" = fail ] && { [ $status -eq 0 ] || [ -e "$dmg" ]; }; then
        echo "FAIL $name: expected a failure and no dmg"; echo "$out"; failures=$((failures + 1))
    elif ! grep -q -- "$pattern" <<<"$out"; then
        echo "FAIL $name: output lacks '$pattern'"; echo "$out"; failures=$((failures + 1))
    else
        echo "ok   $name"
    fi
}

good=$(make_app good)
link "$work/KisakCOD-dedi"
check complete pass "dmg: .* ok" --app "$good" --dedi "$work/KisakCOD-dedi"

link "$work/dedi-rpath" -Wl,-rpath,/opt/homebrew/lib
check dedi-run-path fail "carries run paths" --app "$good" --dedi "$work/dedi-rpath"

no_notices=$(make_app no-notices)
rm "$no_notices/Contents/Resources/THIRD-PARTY-NOTICES"
check no-notices fail "missing .*THIRD-PARTY-NOTICES" --app "$no_notices" --dedi "$work/KisakCOD-dedi"

# Signed, then changed: the check on the mounted image must catch it.
tampered=$(make_app tampered)
echo "changed" >> "$tampered/Contents/Info.plist"
check tampered-app fail "signature does not verify" --app "$tampered" --dedi "$work/KisakCOD-dedi"

[ $failures -eq 0 ] || { echo "$failures case(s) failed"; exit 1; }
echo "all package_dmg cases passed"

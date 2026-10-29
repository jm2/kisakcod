#!/usr/bin/env bash
# check_licenses.sh (ADR-0011): a KisakCOD-mp.app passes only when every
# bundled framework ships its component's license text. Builds fake .app trees
# (empty files named as the real frameworks), so it runs on any POSIX host.
set -euo pipefail

repo_root=$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
checker="$repo_root/scripts/platform/macos/check_licenses.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/kisak-licenses.XXXXXX")
trap 'rm -rf "$work"' EXIT
failures=0

# The nine frameworks the bundle ships today.
frameworks="libSDL3.0.dylib libavcodec.63.dylib libavformat.63.dylib libavutil.61.dylib
libdxvk_d3d9.0.dylib libopenal.1.dylib libvulkan.1.dylib libvulkan_kosmickrisp.dylib libzstd.1.dylib"
components="dxvk ffmpeg mesa openal-soft SDL3 spirv-tools vulkan-loader zstd"

make_app() {
    local app="$work/$1.app"
    mkdir -p "$app/Contents/Frameworks" "$app/Contents/Resources/licenses"
    for f in $frameworks; do : > "$app/Contents/Frameworks/$f"; done
    for c in $components; do
        mkdir -p "$app/Contents/Resources/licenses/$c"
        echo "license text for $c" > "$app/Contents/Resources/licenses/$c/LICENSE"
    done
    printf '%s' "$app"
}

check() {
    local name=$1 want=$2 pattern=$3 app=$4 status=0 out
    out=$(bash "$checker" "$app" 2>&1) || status=$?
    if [ "$status" -ne "$want" ] || ! grep -Eq -- "$pattern" <<<"$out"; then
        printf 'FAIL %s: exit %s (want %s), output:\n%s\n' "$name" "$status" "$want" "$out" >&2
        failures=$((failures + 1))
    else
        printf 'ok   %s\n' "$name"
    fi
}

check complete 0 'ships its license text' "$(make_app complete)"

app=$(make_app no-openal)
rm -r "$app/Contents/Resources/licenses/openal-soft"
check missing-component 1 'libopenal.1.dylib needs a license text in Resources/licenses/openal-soft/' "$app"

app=$(make_app empty-text)
: > "$app/Contents/Resources/licenses/ffmpeg/LICENSE"
check empty-text 1 'libavcodec.63.dylib needs a license text in Resources/licenses/ffmpeg/' "$app"

app=$(make_app no-spirv)
rm -r "$app/Contents/Resources/licenses/spirv-tools"
check driver-needs-spirv-tools 1 'libvulkan_kosmickrisp.dylib needs a license text in Resources/licenses/spirv-tools/' "$app"

app=$(make_app unmapped)
: > "$app/Contents/Frameworks/libnewdep.1.dylib"
check unmapped-framework 1 'libnewdep.1.dylib has no license mapping' "$app"

[ "$failures" -eq 0 ] || { echo "$failures check(s) failed" >&2; exit 1; }

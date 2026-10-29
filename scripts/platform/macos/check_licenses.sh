#!/usr/bin/env bash
# Check that every framework in a KisakCOD.app ships its component's
# license text (docs/decisions/0011): each Contents/Frameworks/*.dylib maps to
# a component, and Contents/Resources/licenses/<component>/ must hold at least
# one non-empty file. A framework with no mapping is an error too, so a new
# dependency can't ship without one.
#
# Usage: check_licenses.sh <KisakCOD.app>
set -euo pipefail

APP="${1:?usage: check_licenses.sh <KisakCOD.app>}"
FRAMEWORKS="$APP/Contents/Frameworks"
LICENSES="$APP/Contents/Resources/licenses"
[ -d "$FRAMEWORKS" ] || { echo "licenses: no $FRAMEWORKS" >&2; exit 1; }

# The components a framework's code comes from. KosmicKrisp links
# SPIRV-Tools statically, so the driver needs both texts.
components_for() {
    case "$1" in
        libvulkan.*) echo "vulkan-loader" ;;
        libvulkan_kosmickrisp.*) echo "mesa spirv-tools" ;;
        libSDL3.*) echo "SDL3" ;;  # the name SDL3 installs its own text under
        libopenal.*) echo "openal-soft" ;;
        libavcodec.*|libavformat.*|libavutil.*|libswresample.*) echo "ffmpeg" ;;
        libzstd.*) echo "zstd" ;;
        libdxvk_d3d9.*) echo "dxvk" ;;
        *) echo "" ;;
    esac
}

bad=0
shopt -s nullglob
for lib in "$FRAMEWORKS"/*.dylib; do
    name="$(basename "$lib")"
    comps="$(components_for "$name")"
    if [ -z "$comps" ]; then
        echo "licenses: $name has no license mapping in check_licenses.sh" >&2
        bad=1
        continue
    fi
    for comp in $comps; do
        found=0
        for text in "$LICENSES/$comp"/*; do
            [ -f "$text" ] && [ -s "$text" ] && found=1
        done
        if [ $found -eq 0 ]; then
            echo "licenses: $name needs a license text in Resources/licenses/$comp/" >&2
            bad=1
        fi
    done
done
[ $bad -eq 0 ] || exit 1
echo "licenses: every framework in $APP ships its license text"

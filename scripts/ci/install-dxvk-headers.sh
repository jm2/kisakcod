#!/usr/bin/env bash
# install-dxvk-headers.sh <prefix>: dxvk-native's D3D9 headers, as its meson
# install lays them out, plus a dxvk-d3d9.pc with no library, for CI's test
# builds (#914). The renderer and client tests compile against <d3d9.h> but
# call no dxvk export, so building dxvk itself isn't needed. The dxvk commit is
# build.sh's DXVK_NATIVE_COMMIT; the DirectX headers are its pinned submodule.
set -euo pipefail

prefix=${1:?usage: install-dxvk-headers.sh <prefix>}
root=$(cd "$(dirname "$0")/../.." && pwd)
commit=$(sed -n 's/^readonly DXVK_NATIVE_COMMIT=\([0-9a-f]\{40\}\)$/\1/p' "$root/build.sh")
if [ -z "$commit" ]; then
    echo "install-dxvk-headers.sh: no DXVK_NATIVE_COMMIT in build.sh" >&2
    exit 1
fi

src=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/var/tmp}}/dxvk-headers.XXXXXX")
trap 'rm -rf "$src"' EXIT

# One commit of a repository, without its history.
fetch() {
    mkdir -p "$1"
    git -C "$1" init -q
    git -C "$1" fetch -q --depth 1 "$2" "$3"
    git -C "$1" checkout -q FETCH_HEAD
}

fetch "$src/dxvk" https://github.com/doitsujin/dxvk.git "$commit"
directx=$(git -C "$src/dxvk" ls-tree HEAD include/native/directx | awk '$2 == "commit" { print $3 }')
fetch "$src/directx" https://github.com/Joshua-Ashton/mingw-directx-headers.git "$directx"

# dxvk's include/native/meson.build: directx/ and windows/ into include/dxvk,
# the WSI headers into include/dxvk/wsi.
inc=$prefix/include/dxvk
mkdir -p "$inc/wsi" "$prefix/lib/pkgconfig"
git -C "$src/directx" archive HEAD | tar -x -C "$inc"
git -C "$src/dxvk" archive HEAD include/native/windows | tar -x -C "$inc" --strip-components=3
for wsi in native_wsi.h native_sdl3.h native_sdl2.h native_glfw.h; do
    cp "$src/dxvk/include/native/wsi/$wsi" "$inc/wsi/"
done

cat > "$prefix/lib/pkgconfig/dxvk-d3d9.pc" <<EOF
prefix=$prefix
includedir=\${prefix}/include

Name: dxvk_d3d9
Description: dxvk-native's D3D9 headers only (scripts/ci/install-dxvk-headers.sh)
Version: ${commit:0:12}
Cflags: -I\${includedir}/dxvk
EOF
echo "dxvk-native $commit headers (directx $directx) in $inc"

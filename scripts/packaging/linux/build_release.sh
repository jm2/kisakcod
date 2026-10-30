#!/bin/sh
# Builds one Linux release leg (ADR-0012, NOW row 74): the pinned runtime, the
# client against it, the headless server, split debug symbols, the notices and
# every package format. release.yml runs it on the Ubuntu runners; a
# ubuntu:24.04 container with the same packages runs it too.
#
#   build_release.sh VERSION ARCH BUILD OUT EPOCH   # ARCH: amd64 or arm64
#
# OUT receives the packages (.tar.xz, .deb, .rpm, .pkg.tar.zst) and symbols/
# with KisakCOD-mp.debug and KisakCOD-dedi.debug. BUILD holds the build trees.
set -eu
version=$1
arch=$2
build=$3
out=$4
epoch=$5
src=$(cd "$(dirname "$0")/../../.." && pwd)
jobs=$(nproc)
prefix=$build/runtime/prefix

cmake -S "$src/scripts/platform/linux/runtime" -B "$build/runtime"
cmake --build "$build/runtime" -j "$jobs"

mkdir -p "$build/bin" "$out/symbols"
# The client links the runtime above and finds it in $ORIGIN/lib once packaged.
PKG_CONFIG_PATH=$prefix/lib/pkgconfig cmake --preset "linux-$arch-client" -B "$build/client" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DKISAK_LINUX_PACKAGE=ON -DCMAKE_PREFIX_PATH="$prefix" \
    -DCMAKE_EXE_LINKER_FLAGS=-Wl,--build-id=sha1
cmake --build "$build/client" --target KisakCOD-mp -j "$jobs"
cp "$src/bin/KisakCOD-mp" "$build/bin/"

# release.yml's linux-headless configure: the server needs nothing beyond libc.
cmake -S "$src" -B "$build/dedi" -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DKISAK_BUILD_MP=OFF -DKISAK_BUILD_DEDICATED=ON -DKISAK_BUILD_SP=OFF \
    -DKISAK_DEDI_HEADLESS=ON -DKISAK_ENABLE_STEAM=OFF \
    -DKISAK_ALLOW_UNSUPPORTED_64BIT=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_EXE_LINKER_FLAGS=-Wl,--build-id=sha1
cmake --build "$build/dedi" --target KisakCOD-dedi -j "$jobs"
cp "$src/bin/KisakCOD-dedi" "$build/bin/"

for name in KisakCOD-mp KisakCOD-dedi; do
    objcopy --only-keep-debug "$build/bin/$name" "$out/symbols/$name.debug"
    strip --strip-debug --strip-unneeded "$build/bin/$name"
    objcopy --add-gnu-debuglink="$out/symbols/$name.debug" "$build/bin/$name"
done

python3 "$src/scripts/packaging/third_party_notices.py" --out "$build/THIRD-PARTY-NOTICES" \
    --client --licenses "$prefix/share/licenses"
python3 "$src/scripts/packaging/linux/package_linux.py" \
    --client "$build/bin/KisakCOD-mp" --dedi "$build/bin/KisakCOD-dedi" \
    --runtime-prefix "$prefix" --notices "$build/THIRD-PARTY-NOTICES" \
    --version "$version" --arch "$arch" --out "$out" --epoch "$epoch"
"$src/scripts/packaging/linux/build_native.sh" "$out" "$epoch"
rm -rf "$out/rpm" "$out/arch"

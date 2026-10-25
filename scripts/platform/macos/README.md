# macOS app bundle runtime

Row 66 (ADR-0011, gate G6). The macOS client ships its graphics and media
stack inside `KisakCOD-mp.app` so a clean machine with no Homebrew can play:
`otool -L` on every image in the bundle must show no `/opt/homebrew` or
`/usr/local` reference.

`bundle.sh` does the assembly and the verification. This file records what the
runtime prefix it consumes must contain and how those pieces are built, because
the build is not yet automated in CMake (see "Open work").

## Contract with bundle.sh

`--runtime-prefix <dir>` must provide:

| path | source |
| --- | --- |
| `lib/libvulkan.1.dylib` | Khronos Vulkan-Loader |
| `lib/libopenal.1.dylib` | OpenAL Soft |
| `lib/libSDL3.0.dylib`, `lib/cmake/SDL3` | SDL3 |
| `lib/libavformat.63.dylib`, `libavcodec.63`, `libavutil.61` | FFmpeg, bink only |
| `lib/libzstd.1.dylib` | zstd (Mesa's KosmicKrisp links it) |
| `lib/pkgconfig/*.pc` | the same set, for the client configure |

`--mesa-prefix <dir>` must provide `lib/libvulkan_kosmickrisp.dylib` and
`share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json`.

`--dxvk-lib <file>` is the `libdxvk_d3d9.0.dylib` produced by the pinned
jm2/dxvk fork (`scripts/extern/dxvk_macos.cmake`).

Every image must be built at `MACOSX_DEPLOYMENT_TARGET=26.0`. `bundle.sh`
verifies the closure afterwards; a 27.0 framework from a stale build directory
is the failure this catches.

## Pinned sources

SHA-256 of the tarballs used for the verified build:

```
bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9  mesa-26.2.4.tar.xz
941eff558fc74f248745fb7df367640cf89a8457f216dae498a74ae14b6ba6f7  Vulkan-Loader-1.4.363.0.tar.gz
e6c83a215538fbfd265ccf398d3877985055df1273dbed73912bd05307664688  SPIRV-Tools-1.4.363.0.tar.gz
9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3  SDL3-3.4.18.tar.gz
1dbaac44e7579d5bc8847ca8db4b2e8b9fd3961041f35ee20def4958301e1089  openal-soft-1.25.2.tar.bz2
8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e  ffmpeg-9.0.2.tar.xz
eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3  zstd-1.5.7.tar.gz
```

Nothing from Homebrew is shipped. A Homebrew LLVM is acceptable as a build
tool as long as the result is linked against the bundled copies only.

## Non-obvious build requirements

1. **Mesa**: `-Dshared-llvm=enabled`. With `-Dshared-llvm=disabled`, `mesa_clc`
   carries a static LLVM while the SPIR-V translator dylib loads
   `libLLVM.23.1.dylib`; two LLVM copies in one process crash in
   `runSpirvWriterPasses` (pointer authentication, exit 139) while generating
   `libpoly.spv`/`libkk.spv`.
2. **zstd**: its sub-make ignores `CFLAGS`/`LDFLAGS`. Pass
   `MOREFLAGS="-mmacosx-version-min=26.0"` (otherwise the dylib is stamped with
   the host version) and pass `PREFIX=` to `make` itself, not only to
   `make install` — the generated `.pc` bakes `/usr/local`, and CMake then
   resolves the wrong dylib.
3. **OpenAL Soft**: clang 21 rejects `-Werror=function-effects` on a `noexcept`
   CoreAudio lambda (`alc/backends/coreaudio.cpp`). Downgrade to
   `-Wfunction-effects`; the warning still prints, the build proceeds.
4. **FFmpeg**: the decoder is `bink`, not `binkvideo`. Minimal LGPL build:
   `--enable-demuxer=bink --enable-decoder=bink,binkaudio_dct,binkaudio_rdft,adpcm_ima_wav`
   `--enable-parser=bink --enable-protocol=file,pipe --disable-everything`
   `--disable-gpl --disable-nonfree --enable-shared`; no autodetect, so no
   OpenSSL/Vulkan/x264 finds its way in.
5. **Meson caches** found dependencies. After changing `PKG_CONFIG_PATH`, use a
   fresh build directory; `--reconfigure` keeps pointing at Homebrew.
6. **ccache** must be told a writable directory under the sandbox, e.g.
   `CCACHE_DIR=$TMPDIR/kisakcod-ccache`.

## Client configure

```
cmake -S . -B build-macos-bundle26 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 \
  -DCMAKE_PREFIX_PATH=<runtime-prefix> \
  -DSDL3_DIR=<runtime-prefix>/lib/cmake/SDL3 \
  -DKISAK_ALLOW_UNSUPPORTED_64BIT=ON -DKISAK_ENABLE_STEAM=OFF \
  -DKISAK_CLIENT_SDL3=ON -DKISAK_DXVK_MACOS=ON -DKISAK_MACOS_BUNDLE=ON \
  -DKISAK_EXPERIMENTAL_POSIX_CLIENT=ON -DKISAK_BUILD_DEDICATED=OFF
```

with `PKG_CONFIG_PATH=<runtime-prefix>/lib/pkgconfig`.

`KISAK_MACOS_BUNDLE=ON` stops `scripts/client_media.cmake` injecting Homebrew
OpenAL into `PKG_CONFIG_PATH`, so the configure binds the bundled copy.

Then:

```
scripts/platform/macos/bundle.sh \
  --app dist/KisakCOD-mp.app --client build-macos-bundle26/bin/KisakCOD-mp \
  --runtime-prefix <runtime-prefix> --mesa-prefix <mesa-prefix> \
  --dxvk-lib <dxvk>/lib/libdxvk_d3d9.0.dylib --licenses <dir>
```

The client sets `VK_DRIVER_FILES` to the bundled ICD at startup when the
variable is unset (`Posix_DefaultBundledVulkanDriver`, `posix_client_main.cpp`),
so the bundle needs no environment from the user while an explicit
`VK_DRIVER_FILES` still wins.

## Verification performed

On macOS 26.0 arm64, bundle of 35 MB with nine frameworks:

- `codesign --verify --deep --strict`: valid, satisfies its designated requirement.
- `otool -L`: 0 non-system absolute dependencies across all ten Mach-O images;
  the client links `@rpath/...` and each dylib carries `@loader_path`.
- every image reports `LC_BUILD_VERSION minos 26.0`.
- the ICD JSON in `Contents/Resources/vulkan/icd.d/` points at
  `../../../Frameworks/libvulkan_kosmickrisp.dylib`.
- a headless launch of the bundle resolves its own `libdxvk_d3d9` (it
  reports the pinned `v3.1.1-72-gdac6ce62`) and then aborts before a Vulkan
  instance exists, so the ICD lookup is proven statically only. Closing that
  gap is part of the K6 done-test, not of this check.

## Open work

- Turn the recipe above into `scripts/extern/*.cmake` pins and an
  `add_custom_target(bundle ...)`, so the bundle builds from the repo alone.
- K6 done-test: a clean macOS 26 account with no Homebrew joining a LAN round.
  It needs a display and has not been run.

## Licensing

FFmpeg is LGPL 2.1+ and dynamically linked; its license text and a sources note
are written into `Contents/Resources/licenses/` (`SOURCES.txt` lists every pin).
Mesa is used as a Vulkan device driver on Metal; no GPU driver is bundled.

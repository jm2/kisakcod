# Builds the Linux client's bundled runtime from pinned sources (ADR-0012), the
# Linux counterpart of scripts/platform/macos/runtime (ADR-0011). Every Linux
# package carries these in /opt/kisakcod/lib, so a package depends only on
# glibc, libstdc++ and the Vulkan loader, and THIRD-PARTY-NOTICES covers
# everything it ships.
#
#   cmake -S scripts/platform/linux/runtime -B build-runtime
#   cmake --build build-runtime -j
#   PKG_CONFIG_PATH=$PWD/build-runtime/prefix/lib/pkgconfig \
#     cmake --preset linux-amd64-client -DKISAK_LINUX_PACKAGE=ON \
#       -DCMAKE_PREFIX_PATH=$PWD/build-runtime/prefix
#
# The SDL3, OpenAL Soft and FFmpeg pins match scripts/extern/macos_runtime_*;
# bump them together. dxvk-native matches build.sh's DXVK_NATIVE_COMMIT.
cmake_minimum_required(VERSION 3.24)
project(kisakcod_linux_runtime NONE)

include(ExternalProject)
include(ProcessorCount)
ProcessorCount(KISAK_JOBS)
if (KISAK_JOBS EQUAL 0)
    set(KISAK_JOBS 4)
endif()

set(KISAK_RUNTIME_PREFIX "${CMAKE_BINARY_DIR}/prefix" CACHE PATH
    "where SDL3, OpenAL Soft, FFmpeg and dxvk-native install")
set(KISAK_SOURCE_CACHE "${CMAKE_BINARY_DIR}/src" CACHE PATH "tarball download cache")
find_program(KISAK_MESON meson REQUIRED)
find_program(KISAK_NINJA ninja REQUIRED)

# Every library is found at run time through the client's $ORIGIN/lib runpath.
set(_kisak_cmake_common
    -DCMAKE_INSTALL_PREFIX=${KISAK_RUNTIME_PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_INSTALL_RPATH=\$ORIGIN")

# Video and audio backends (X11, Wayland, PipeWire, PulseAudio, ALSA) are
# dlopen()ed from the host at run time, so their headers are build-time only.
ExternalProject_Add(kisak_runtime_sdl3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CMAKE_ARGS ${_kisak_cmake_common}
        -DSDL_SHARED=ON -DSDL_STATIC=OFF
        -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF
        -DSDL_INSTALL_TESTS=OFF -DSDL_INSTALL_DOCS=OFF
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libSDL3.so.0
    LOG_BUILD ON
    LOG_INSTALL ON
)

ExternalProject_Add(kisak_runtime_openal
    URL https://openal-soft.org/openal-releases/openal-soft-1.25.2.tar.bz2
        https://github.com/kcat/openal-soft/releases/download/1.25.2/openal-soft-1.25.2.tar.bz2
    URL_HASH SHA256=1dbaac44e7579d5bc8847ca8db4b2e8b9fd3961041f35ee20def4958301e1089
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CMAKE_ARGS ${_kisak_cmake_common}
        -DLIBTYPE=SHARED
        -DALSOFT_EXAMPLES=OFF -DALSOFT_UTILS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libopenal.so.1
    LOG_BUILD ON
    LOG_INSTALL ON
)

# The engine needs Bink video and audio, the streamed sounds' WAV and MP3, and
# the file protocol (LGPL build).
ExternalProject_Add(kisak_runtime_ffmpeg
    URL https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
    URL_HASH SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        --prefix=${KISAK_RUNTIME_PREFIX}
        --enable-shared --disable-static --disable-programs --disable-doc
        --disable-network --disable-autodetect --disable-everything
        --enable-avformat --enable-avcodec --enable-avutil
        --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample
        # Bink for cinematics; WAV (16-bit PCM) and MP3 for streamed sounds and music (#942).
        --enable-demuxer=bink,wav,mp3
        --enable-decoder=bink,binkaudio_dct,binkaudio_rdft,adpcm_ima_wav,pcm_s16le,mp3float
        --enable-parser=bink,mpegaudio
        --enable-protocol=file,pipe
        --disable-gpl --disable-nonfree --enable-pic
        # Bink decoding is light; skip the nasm build dependency.
        --disable-x86asm
    # No runpath: every FFmpeg library another one needs is also one the client
    # links directly, so the client's $ORIGIN/lib finds it.
    BUILD_COMMAND make -j ${KISAK_JOBS}
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libavformat.so
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

# The built FFmpeg must read what the engine streams (#942): build and run
# scripts/platform/ffmpeg_check against the prefix, or fail the runtime.
ExternalProject_Add(kisak_runtime_ffmpeg_check
    DEPENDS kisak_runtime_ffmpeg
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../ffmpeg_check
    CMAKE_ARGS -DKISAK_FFMPEG_PREFIX=${KISAK_RUNTIME_PREFIX}
    INSTALL_COMMAND ""
    TEST_COMMAND <BINARY_DIR>/ffmpeg_stream_check
)

# dxvk-native's D3D9 with only the SDL3 window-system backend, against the
# SDL3 above. Its native build links libstdc++ statically, so an error it
# throws out of Direct3DCreate9 can't be caught in the client, which aborts
# (#970). The patch catches it inside dxvk, logs it and returns null; build.sh
# applies the same one. The checkout makes the step safe to run again.
ExternalProject_Add(kisak_runtime_dxvk
    DEPENDS kisak_runtime_sdl3
    GIT_REPOSITORY https://github.com/doitsujin/dxvk.git
    GIT_TAG b1a1c99ab52b687cf950d62c88bc2fa316b41663 # v3.1.1
    GIT_SUBMODULES_RECURSE ON
    UPDATE_COMMAND ""
    PATCH_COMMAND git checkout -- src/d3d9/d3d9_main.cpp
        COMMAND git apply ${CMAKE_CURRENT_SOURCE_DIR}/dxvk-create-d3d9-catch.patch
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env PKG_CONFIG_PATH=${KISAK_RUNTIME_PREFIX}/lib/pkgconfig
        ${KISAK_MESON} setup <BINARY_DIR> <SOURCE_DIR>
        --buildtype=release --prefix=${KISAK_RUNTIME_PREFIX} --libdir=lib
        -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled
        -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false
    BUILD_COMMAND ${KISAK_NINJA} -C <BINARY_DIR>
    INSTALL_COMMAND ${KISAK_NINJA} -C <BINARY_DIR> install
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libdxvk_d3d9.so.0
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

# With no Vulkan driver, the built dxvk must fail Direct3DCreate9 with a reason
# instead of aborting the client (#970): build and run the client's wrapper
# test, scripts/platform/dxvk_check, against the prefix, or fail the runtime.
ExternalProject_Add(kisak_runtime_dxvk_check
    DEPENDS kisak_runtime_dxvk
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../dxvk_check
    CMAKE_ARGS -DKISAK_DXVK_PREFIX=${KISAK_RUNTIME_PREFIX}
    INSTALL_COMMAND ""
    TEST_COMMAND ${CMAKE_COMMAND} -E env
        LD_LIBRARY_PATH=${KISAK_RUNTIME_PREFIX}/lib
        DXVK_WSI_DRIVER=SDL3 SDL_VIDEO_DRIVER=offscreen
        VK_DRIVER_FILES=<BINARY_DIR>/no-such-icd.json VK_ICD_FILENAMES=<BINARY_DIR>/no-such-icd.json
        <BINARY_DIR>/posix_direct3d_check
)

# Each component's license texts, into <prefix>/share/licenses/<component>/,
# where the notices generator and the packager pick them up.
set(_kisak_license_dir "${KISAK_RUNTIME_PREFIX}/share/licenses")
set(_kisak_license_commands "")
foreach (_kisak_pair
        "SDL3|kisak_runtime_sdl3|LICENSE.txt"
        "openal-soft|kisak_runtime_openal|COPYING"
        "ffmpeg|kisak_runtime_ffmpeg|LICENSE.md;COPYING.LGPLv2.1"
        "dxvk|kisak_runtime_dxvk|LICENSE")
    string(REPLACE "|" ";" _kisak_fields "${_kisak_pair}")
    list(GET _kisak_fields 0 _kisak_component)
    list(GET _kisak_fields 1 _kisak_project)
    list(SUBLIST _kisak_fields 2 -1 _kisak_files)
    ExternalProject_Get_Property(${_kisak_project} SOURCE_DIR)
    list(APPEND _kisak_license_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_kisak_license_dir}/${_kisak_component})
    foreach (_kisak_file IN LISTS _kisak_files)
        list(APPEND _kisak_license_commands
            COMMAND ${CMAKE_COMMAND} -E copy ${SOURCE_DIR}/${_kisak_file} ${_kisak_license_dir}/${_kisak_component}/)
    endforeach()
endforeach()
add_custom_target(kisak_runtime_licenses ALL ${_kisak_license_commands}
    DEPENDS kisak_runtime_sdl3 kisak_runtime_openal kisak_runtime_ffmpeg kisak_runtime_dxvk)

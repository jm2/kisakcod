# Client sound and cinematics (docs/design/CLIENT.md). The engine calls the
# Miles and Bink APIs it shipped with; every client answers them with OpenAL
# Soft (sound/snd_openal.cpp) and FFmpeg's Bink decoder
# (gfx_d3d/r_bink_ffmpeg.cpp), so no build links mss32 or binkw32. Windows
# takes both libraries from vcpkg (vcpkg.json), whose toolchain copies their
# DLLs next to the executable; Linux and macOS take them from pkg-config.
if (WIN32)
    find_package(OpenAL CONFIG QUIET)
    find_package(FFMPEG QUIET)
    if (NOT OpenAL_FOUND OR NOT FFMPEG_FOUND)
        message(FATAL_ERROR
            "Windows clients take OpenAL Soft and FFmpeg from vcpkg (vcpkg.json). "
            "Run build-win.ps1 -InstallDeps, or set VCPKG_ROOT to a vcpkg checkout, "
            "then configure a fresh build directory.")
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE OpenAL::OpenAL)
    target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})
    # Engine code calls Miles and Bink through their dllimport declarations;
    # the linker binds those calls to the shims' definitions (LNK4217/4286).
    target_link_options(${PROJECT_NAME} PRIVATE /IGNORE:4217,4286)
else()
    # Homebrew's openal-soft is keg-only, so add its pkg-config dir.
    find_package(PkgConfig REQUIRED)
    foreach(_kisak_openal_prefix /opt/homebrew/opt/openal-soft /usr/local/opt/openal-soft)
        if (APPLE AND EXISTS ${_kisak_openal_prefix}/lib/pkgconfig)
            set(ENV{PKG_CONFIG_PATH} "${_kisak_openal_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        endif()
    endforeach()
    pkg_check_modules(KISAK_FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil)
    pkg_check_modules(KISAK_OPENAL IMPORTED_TARGET openal)
    if (NOT KISAK_FFMPEG_FOUND OR NOT KISAK_OPENAL_FOUND)
        message(FATAL_ERROR
            "The client plays sound through OpenAL Soft and cinematics through FFmpeg. "
            "Install their development files (./build.sh --install-deps; macOS: brew install "
            "openal-soft ffmpeg; Debian/Ubuntu: libopenal-dev libavformat-dev libavcodec-dev "
            "libavutil-dev; Fedora: openal-soft-devel libavformat-free-devel libavcodec-free-devel libavutil-free-devel).")
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::KISAK_FFMPEG PkgConfig::KISAK_OPENAL)
endif()
target_sources(${PROJECT_NAME} PRIVATE
    ${SRC_DIR}/gfx_d3d/r_bink_ffmpeg.cpp
    ${SRC_DIR}/sound/snd_openal.cpp
    ${SRC_DIR}/sound/snd_openal_adpcm.cpp
)

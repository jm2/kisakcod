cmake_minimum_required(VERSION 3.16)

# The oldest macOS the macOS builds run on (owner, 2026-09-30). It must be set
# before project(); other platforms ignore it. CI builds against it, so an API
# newer than this fails there rather than on a user's older Mac.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

project(KisakCOD LANGUAGES C CXX)

# Set the default C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KISAK_BUILD_MP "Build the multiplayer client" ON)
option(KISAK_BUILD_DEDICATED "Build the multiplayer dedicated server" ON)
option(KISAK_BUILD_SP "Build the incomplete single-player client" OFF)
option(KISAK_DEDI_HEADLESS "Build the dedicated server without client/render/audio/cinema source groups" OFF)
option(KISAK_ALLOW_UNSUPPORTED_64BIT "Allow configuration of the in-progress 64-bit ABI port" OFF)
option(KISAK_MEASURE_FX_ARCHIVE_STACK "Measure source-scoped FX archive stack frames with MSVC analysis" OFF)
option(KISAK_CLIENT_SDL3 "Use SDL3 for the MP client window and input (fetches SDL 3.4)" OFF)
set(_KISAK_DEFAULT_X86_MMX_SKINNING OFF)
if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm|aarch|AARCH)")
    set(_KISAK_DEFAULT_X86_MMX_SKINNING ON)
endif()
option(KISAK_ENABLE_X86_MMX_SKINNING "Compile the legacy x86/MMX renderer skinning implementation" ${_KISAK_DEFAULT_X86_MMX_SKINNING})
# Steam is an optional identity/anti-spoof layer (not an ownership gate). Valve ships no
# aarch64 Steamworks library, so the ARM targets default it off and fall back to a
# self-generated persistent cl_guid. Everywhere else it defaults on to preserve the
# existing ticket-based client<->dedicated handshake.
set(_KISAK_DEFAULT_STEAM ON)
if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm|aarch|AARCH)")
    set(_KISAK_DEFAULT_STEAM OFF)
endif()
option(KISAK_ENABLE_STEAM "Link the Steamworks SDK for client<->server identity/auth" ${_KISAK_DEFAULT_STEAM})
# Miles (mss32) and Bink (binkw32) ship 32-bit libraries only. 64-bit Windows
# client targets link silent stubs instead (no sound, no cinematics) until the
# OpenAL Soft / FFmpeg backends land; Win32 always links the real libraries.
set(_KISAK_DEFAULT_MEDIA_STUBS OFF)
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_KISAK_DEFAULT_MEDIA_STUBS ON)
endif()
option(KISAK_MEDIA_STUBS "Link silent Miles/Bink stubs instead of the 32-bit-only libraries" ${_KISAK_DEFAULT_MEDIA_STUBS})
if (KISAK_MEDIA_STUBS AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "KISAK_MEDIA_STUBS is for 64-bit targets; Win32 links the real Miles and Bink libraries.")
endif()
include(CTest)

if (WIN32)
    set(_KISAK_DEFAULT_PLATFORM win32)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_KISAK_DEFAULT_PLATFORM linux)
elseif (APPLE)
    set(_KISAK_DEFAULT_PLATFORM macos)
else()
    message(FATAL_ERROR "Unsupported host platform: ${CMAKE_SYSTEM_NAME}")
endif()
set(KISAK_PLATFORM "${_KISAK_DEFAULT_PLATFORM}" CACHE STRING "KisakCOD platform backend")
set_property(CACHE KISAK_PLATFORM PROPERTY STRINGS win32 linux macos)

if (NOT KISAK_PLATFORM STREQUAL _KISAK_DEFAULT_PLATFORM)
    message(FATAL_ERROR
        "KISAK_PLATFORM='${KISAK_PLATFORM}' does not match host platform "
        "'${_KISAK_DEFAULT_PLATFORM}'. Cross-compilation toolchains must set "
        "CMAKE_SYSTEM_NAME consistently.")
endif()

# The headless dedicated build is the one engine target a non-Win32 platform
# may compose: its source set is the portable engine plus the POSIX platform
# services and the POSIX entry/console/localization files (NOW row 13,
# docs/design/PLATFORM_POSIX.md). Every other engine target still requires the
# Win32 backend until the client renderer and the remaining POSIX services land.
if (NOT KISAK_PLATFORM STREQUAL "win32"
    AND (KISAK_BUILD_MP OR KISAK_BUILD_SP
         OR (KISAK_BUILD_DEDICATED AND NOT KISAK_DEDI_HEADLESS)))
    message(FATAL_ERROR
        "The ${KISAK_PLATFORM} backend is not buildable yet. The platform selection "
        "and native time/synchronization services are present, but 64-bit runtime/fast-file "
        "widening, the remaining POSIX services, portable headless source composition, and "
        "the client Vulkan renderer must land before engine targets can compile. "
        "Configure -DKISAK_BUILD_DEDICATED=ON -DKISAK_DEDI_HEADLESS=ON for the "
        "POSIX headless dedicated server.")
endif()

if (CMAKE_SIZEOF_VOID_P EQUAL 8
    AND (KISAK_BUILD_MP OR KISAK_BUILD_DEDICATED OR KISAK_BUILD_SP)
    AND NOT KISAK_ALLOW_UNSUPPORTED_64BIT)
    message(FATAL_ERROR
        "The runtime still embeds the 32-bit ABI in fast-file and script VM layouts. "
        "Use a Win32 generator today. Set KISAK_ALLOW_UNSUPPORTED_64BIT=ON only when "
        "working on the disk32/runtime64 conversion.")
endif()

if (KISAK_ENABLE_X86_MMX_SKINNING
    AND (NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm|aarch|AARCH)"))
    message(FATAL_ERROR
        "KISAK_ENABLE_X86_MMX_SKINNING is only valid for 32-bit x86 builds. "
        "64-bit and ARM targets must use the scalar path until SSE2/NEON "
        "skinning replacements land.")
endif()

if (KISAK_ENABLE_STEAM AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm|aarch|AARCH)")
    message(FATAL_ERROR
        "KISAK_ENABLE_STEAM is set but Valve ships no aarch64 Steamworks library. "
        "Configure with -DKISAK_ENABLE_STEAM=OFF on ARM targets; the engine falls "
        "back to a self-generated persistent cl_guid identity.")
endif()

# Toolchain policy (docs/design/PLATFORM_POSIX.md): every non-MSVC target is
# clang with -fms-extensions, because the decompiled engine still spells MSVC
# dialect (__cdecl, __int32, __declspec). GCC is out until those spellings are
# gone. Without the flag the non-Windows engine targets fail on the first
# __cdecl, so it is applied here rather than left to each build invocation.
# Test-only configurations (every KISAK_BUILD_* off) compile no engine target
# and keep their own compilers, GCC included.
if (NOT MSVC AND (KISAK_BUILD_MP OR KISAK_BUILD_DEDICATED OR KISAK_BUILD_SP))
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR
            "The ${CMAKE_CXX_COMPILER_ID} compiler is not supported: build with "
            "clang and -fms-extensions (see docs/design/PLATFORM_POSIX.md).")
    endif()
    add_compile_options(-fms-extensions)
endif()
if (NOT MSVC)
    # Position-independent executables: the distribution default, and every
    # data address then lies above 4 GiB, so a pointer truncated to 32 bits
    # faults instead of working by accident (the headless smoke relies on it).
    include(CheckPIESupported)
    check_pie_supported()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Set Address Sanitizer
# NOTE: If you use this you will need to copy to the game's directory
# clang_rt.asan_dbg_dynamic-i386.dll and clang_rt.asan_dynamic-i386.dll from the MSVC C directory located below (or similar)
# C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x86
set(USE_ASAN OFF)

# Reproducible reference builds (M0 byte-parity gate). /Brepro pins the
# embedded timestamp, PDB GUID, and __DATE__/__TIME__ inputs so the shared
# retail reference translation units compile and link byte-identically across
# the MP/SP/dedicated KISAK_BUILD_* variants. Off by default; the hosted
# windows-x86-parity CI job opts in for the three baseline builds it compares.
option(KISAK_REPRODUCIBLE_BUILD "Use MSVC /Brepro so reference builds are byte-deterministic across KISAK_BUILD_* variants" OFF)
if (KISAK_REPRODUCIBLE_BUILD)
    if (NOT MSVC)
        message(FATAL_ERROR "KISAK_REPRODUCIBLE_BUILD currently requires the MSVC toolchain (/Brepro)")
    endif()
    string(APPEND CMAKE_CXX_FLAGS " /Brepro")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " /BREPRO")
endif()

# Set the gloabl directory variables
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
set(PLATFORM_DIR ${SCRIPTS_DIR}/platform)

# Set the output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})

# Include global scripts/tricks
include(${SCRIPTS_DIR}/extern/increment_build.cmake)
include(${SCRIPTS_DIR}/disk32_codegen.cmake)

# Include platform overrides
include(${SCRIPTS_DIR}/platform_override.cmake)

# Log out the KISAK_PLATFORM
message("Building KISAK_PLATFORM: ${KISAK_PLATFORM}")

# Include the requested subprojects.
if (KISAK_BUILD_MP)
    add_subdirectory(${SCRIPTS_DIR}/mp)
endif()
if (KISAK_BUILD_DEDICATED)
    add_subdirectory(${SCRIPTS_DIR}/dedi)
endif()
if (KISAK_BUILD_SP)
    add_subdirectory(${SCRIPTS_DIR}/sp)
endif()
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

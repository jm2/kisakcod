cmake_minimum_required(VERSION 3.16)

# The oldest macOS the macOS builds run on (owner, 2026-09-30). It must be set
# before project(); other platforms ignore it. CI builds against it, so an API
# newer than this fails there rather than on a user's older Mac.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

# Windows clients take OpenAL Soft and FFmpeg from vcpkg (vcpkg.json,
# docs/design/CLIENT.md). A configure that builds a client and names no
# toolchain uses the first vcpkg found: VCPKG_ROOT, the checkout
# build-win.ps1 -InstallDeps makes under .deps/vcpkg, or the GitHub runner's
# VCPKG_INSTALLATION_ROOT. Headless servers need neither. MP and the full
# dedicated server, both on by default below, build the client sources.
if (CMAKE_HOST_WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE
    AND ((NOT DEFINED KISAK_BUILD_MP OR KISAK_BUILD_MP) OR KISAK_BUILD_SP
         OR ((NOT DEFINED KISAK_BUILD_DEDICATED OR KISAK_BUILD_DEDICATED) AND NOT KISAK_DEDI_HEADLESS)))
    foreach(_kisak_vcpkg_root "$ENV{VCPKG_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}/.deps/vcpkg" "$ENV{VCPKG_INSTALLATION_ROOT}")
        if (_kisak_vcpkg_root AND EXISTS "${_kisak_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "${_kisak_vcpkg_root}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
            break()
        endif()
    endforeach()
    set(VCPKG_MANIFEST_FEATURES client CACHE STRING "vcpkg.json features")
    # scripts/post_build.cmake copies the DLLs once for every executable.
    set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Copy vcpkg DLLs per executable")
endif()

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
option(KISAK_DXVK_MACOS "macOS client: build the pinned jm2/dxvk fork for KosmicKrisp instead of a pkg-config dxvk-d3d9" OFF)
option(KISAK_LINUX_PACKAGE "Linux client: link with only $ORIGIN/lib as its runpath, for the release packages (ADR-0012)" OFF)
option(KISAK_MACOS_BUNDLE "macOS client: take the runtime from a pinned local prefix and bundle it into the .app (ADR-0011)" OFF)
set(KISAK_BUNDLE_RUNTIME_PREFIX "" CACHE PATH
    "KISAK_MACOS_BUNDLE: runtime prefix built by scripts/platform/macos/runtime")
set(KISAK_BUNDLE_MESA_PREFIX "" CACHE PATH
    "KISAK_MACOS_BUNDLE: mesa prefix holding libvulkan_kosmickrisp.dylib")
set(KISAK_BUNDLE_DXVK_LIB "" CACHE FILEPATH
    "KISAK_MACOS_BUNDLE: libdxvk_d3d9.0.dylib to bundle")
set(KISAK_BUNDLE_DEDI "" CACHE FILEPATH
    "KISAK_MACOS_BUNDLE: headless KisakCOD-dedi (its own build) for the dmg target")
set(KISAK_PACKAGE_VERSION "0.1.0" CACHE STRING "Version in the release package names (ADR-0012)")
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
# KISAK_EXPERIMENTAL_POSIX_CLIENT lets the MP client configure on Linux/macOS
# for bring-up (SDL3 window/input, dxvk-native D3D9). It does not build yet;
# it exists so the platform sessions can measure and fix the client sources.
option(KISAK_EXPERIMENTAL_POSIX_CLIENT
    "Configure the in-progress Linux/macOS MP client for bring-up (does not build yet)" OFF)
if (NOT KISAK_PLATFORM STREQUAL "win32"
    AND ((KISAK_BUILD_MP AND NOT KISAK_EXPERIMENTAL_POSIX_CLIENT) OR KISAK_BUILD_SP
         OR (KISAK_BUILD_DEDICATED AND NOT KISAK_DEDI_HEADLESS)))
    message(FATAL_ERROR
        "The ${KISAK_PLATFORM} backend is not buildable yet. The platform selection "
        "and native time/synchronization services are present, but 64-bit runtime/fast-file "
        "widening, the remaining POSIX services, portable headless source composition, and "
        "the client Vulkan renderer must land before engine targets can compile. "
        "Configure -DKISAK_BUILD_DEDICATED=ON -DKISAK_DEDI_HEADLESS=ON for the "
        "POSIX headless dedicated server, or add -DKISAK_EXPERIMENTAL_POSIX_CLIENT=ON "
        "to bring up the MP client.")
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
    # Plain char is signed under MSVC, and the decompiled engine relies on it
    # (usercmd_s forwardmove/rightmove hold negative moves in a char). AArch64
    # and other ABIs make char unsigned, so every non-MSVC target, tests
    # included, uses MSVC's signed char; x86 and x64 already do.
    add_compile_options(-fsigned-char)
    # Position-independent executables: the distribution default, and every
    # data address then lies above 4 GiB, so a pointer truncated to 32 bits
    # faults instead of working by accident (the headless smoke relies on it).
    include(CheckPIESupported)
    check_pie_supported()
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Set Address Sanitizer
# USE_ASAN adds MSVC /fsanitize=address to the MP client (scripts/mp/CMakeLists.txt).
# The matching runtime DLL sits beside link.exe in the toolchain: copy
# clang_rt.asan_dbg_dynamic-<arch>.dll and clang_rt.asan_dynamic-<arch>.dll
# (i386 for Win32, aarch64 for ARM64) next to the exe. Off by default, so
# builds that do not ask for it compile unchanged.
option(USE_ASAN "Build the MP client with MSVC /fsanitize=address (copy its runtime DLL beside the exe)" OFF)

# Reproducible reference builds (the x86 byte-parity check). /Brepro pins the
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

# ADR-0011: assemble the macOS .app with its runtime as one command.
# The runtime prefix is built separately (scripts/platform/macos/runtime) and
# handed in; nothing here downloads or compiles a dependency.
if (KISAK_MACOS_BUNDLE AND APPLE)
    foreach (_kisak_var KISAK_BUNDLE_RUNTIME_PREFIX KISAK_BUNDLE_MESA_PREFIX KISAK_BUNDLE_DXVK_LIB)
        if (NOT ${_kisak_var})
            message(FATAL_ERROR "${_kisak_var} is required with KISAK_MACOS_BUNDLE=ON")
        endif()
    endforeach()
    set(_kisak_bundle_app "${CMAKE_BINARY_DIR}/dist/KisakCOD.app")
    get_property(_kisak_dxvk_license GLOBAL PROPERTY KISAK_DXVK_MACOS_LICENSE)
    if (NOT _kisak_dxvk_license)
        message(FATAL_ERROR "KISAK_MACOS_BUNDLE=ON needs the pinned dxvk (KISAK_DXVK_MACOS) for its license text")
    endif()
    add_custom_target(bundle
        COMMAND ${CMAKE_COMMAND} -E env
            bash ${CMAKE_SOURCE_DIR}/scripts/platform/macos/bundle.sh
            --app ${_kisak_bundle_app}
            --client $<TARGET_FILE:KisakCOD-mp>
            --runtime-prefix ${KISAK_BUNDLE_RUNTIME_PREFIX}
            --mesa-prefix ${KISAK_BUNDLE_MESA_PREFIX}
            --dxvk-lib ${KISAK_BUNDLE_DXVK_LIB}
            --dxvk-license ${_kisak_dxvk_license}
            --version ${KISAK_PACKAGE_VERSION}
        DEPENDS KisakCOD-mp
        COMMENT "Bundling ${_kisak_bundle_app}"
        VERBATIM)
    message("macOS bundle target: ${_kisak_bundle_app}")
    # ADR-0012: the release dmg holds the app and the headless server.
    if (KISAK_BUNDLE_DEDI)
        add_custom_target(dmg
            COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/platform/macos/package_dmg.sh
                --app ${_kisak_bundle_app} --dedi ${KISAK_BUNDLE_DEDI}
                --out ${CMAKE_BINARY_DIR}/dist --version ${KISAK_PACKAGE_VERSION}
            DEPENDS bundle
            COMMENT "Packaging kisakcod-${KISAK_PACKAGE_VERSION}-macos-arm64.dmg"
            VERBATIM)
    endif()
endif()

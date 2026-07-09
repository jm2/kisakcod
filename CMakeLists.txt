cmake_minimum_required(VERSION 3.16)

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
set(_KISAK_DEFAULT_X86_MMX_SKINNING OFF)
if (CMAKE_SIZEOF_VOID_P EQUAL 4 AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|arm|aarch|AARCH)")
    set(_KISAK_DEFAULT_X86_MMX_SKINNING ON)
endif()
option(KISAK_ENABLE_X86_MMX_SKINNING "Compile the legacy x86/MMX renderer skinning implementation" ${_KISAK_DEFAULT_X86_MMX_SKINNING})
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

if (NOT KISAK_PLATFORM STREQUAL "win32"
    AND (KISAK_BUILD_MP OR KISAK_BUILD_DEDICATED OR KISAK_BUILD_SP))
    message(FATAL_ERROR
        "The ${KISAK_PLATFORM} backend is not buildable yet. The platform selection "
        "and source override infrastructure is present, but the headless source split, "
        "POSIX system services, and client Vulkan renderer must land before engine "
        "targets can compile.")
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

# Set Address Sanitizer
# NOTE: If you use this you will need to copy to the game's directory
# clang_rt.asan_dbg_dynamic-i386.dll and clang_rt.asan_dynamic-i386.dll from the MSVC C directory located below (or similar)
# C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x86
set(USE_ASAN OFF)

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

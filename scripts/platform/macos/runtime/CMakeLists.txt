# Builds the macOS client's bundled runtime from pinned sources (ADR-0011).
#
# Separate project on purpose: the client's configure needs the prefix to exist
# (SDL3's CMake config, pkg-config files), so build the runtime first and hand it
# to the client:
#
#   cmake -S scripts/platform/macos/runtime -B build-runtime
#   cmake --build build-runtime -j
#   cmake -S . -B build-client -DKISAK_MACOS_BUNDLE=ON \
#         -DKISAK_BUNDLE_RUNTIME_PREFIX=$PWD/build-runtime/prefix ...
#   cmake --build build-client --target bundle
#
# Only libraries reach the .app; every tool here is build-time.
cmake_minimum_required(VERSION 3.24)
project(kisakcod_macos_runtime NONE)

include(ExternalProject)
include(ProcessorCount)
ProcessorCount(KISAK_JOBS)
if (KISAK_JOBS EQUAL 0)
    set(KISAK_JOBS 4)
endif()

set(KISAK_DEPLOYMENT_TARGET "26.0" CACHE STRING
    "MACOSX_DEPLOYMENT_TARGET for every bundled library")
set(KISAK_RUNTIME_PREFIX "${CMAKE_BINARY_DIR}/prefix" CACHE PATH
    "where the loader, SDL3, OpenAL, FFmpeg and zstd install")
set(KISAK_MESA_PREFIX "${CMAKE_BINARY_DIR}/mesa-prefix" CACHE PATH
    "where the KosmicKrisp Vulkan ICD installs")
set(KISAK_SOURCE_CACHE "${CMAKE_BINARY_DIR}/src" CACHE PATH
    "tarball download cache")

find_program(KISAK_MESON meson REQUIRED)
find_program(KISAK_NINJA ninja REQUIRED)

# Mesa's meson needs a Python with the `packaging` module. Python 3.12 dropped
# distutils and Homebrew's 3.14 ships neither, so a venv is the usual answer.
# Check here rather than 90 seconds into the build; point this at <venv>/bin
# when the default interpreter does not satisfy it (README: build-host
# toolchain).
find_program(KISAK_PYTHON3 python3 REQUIRED)
execute_process(COMMAND ${KISAK_PYTHON3} -c "import packaging"
    RESULT_VARIABLE KISAK_PYTHON_HAS_PACKAGING OUTPUT_QUIET ERROR_QUIET)
get_filename_component(KISAK_PYTHON3_DIR "${KISAK_PYTHON3}" DIRECTORY)
if(NOT KISAK_PYTHON_HAS_PACKAGING EQUAL 0)
    message(WARNING
        "python3 at ${KISAK_PYTHON3} has no packaging module; mesa's meson "
        "requires it. Reconfigure with -DKISAK_MESA_PYTHON_BIN_DIR=<venv>/bin.")
endif()
set(KISAK_MESA_PYTHON_BIN_DIR "${KISAK_PYTHON3_DIR}" CACHE PATH
    "directory holding the python3 that mesa's meson runs")

# Homebrew's LLVM is keg-only, so llvm-config is not on PATH by default and
# mesa's meson reports "LLVM found: NO". Locate it explicitly and hand its bin
# directory to the mesa step. -Dshared-llvm=enabled then links the bundled
# KosmicKrisp ICD against libLLVM.dylib rather than carrying a second copy.
find_program(KISAK_LLVM_CONFIG llvm-config
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
if(KISAK_LLVM_CONFIG)
    get_filename_component(KISAK_LLVM_BIN_DIR "${KISAK_LLVM_CONFIG}" DIRECTORY)
else()
    set(KISAK_LLVM_BIN_DIR "")
    message(WARNING
        "llvm-config not found; mesa's meson needs it (README: build-host toolchain).")
endif()
set(KISAK_LLVM_BIN_DIR "${KISAK_LLVM_BIN_DIR}" CACHE PATH
    "directory holding llvm-config for mesa's meson")

# meson picks up ccache from PATH. Its default cache is under ~/Library, which
# is not writable here, so give it a directory inside the build tree.
set(KISAK_CCACHE_DIR "${CMAKE_BINARY_DIR}/ccache" CACHE PATH
    "CCACHE_DIR for the mesa build")

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/macos_runtime_vulkan_headers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/macos_runtime_vulkan_loader.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/macos_runtime_sdl3.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/macos_runtime_openal.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/macos_runtime_ffmpeg.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/macos_runtime_zstd.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../extern/macos_runtime_mesa.cmake)

add_custom_target(runtime ALL
    DEPENDS kisak_runtime_vulkan_loader kisak_runtime_spirv_tools
            kisak_runtime_sdl3 kisak_runtime_openal kisak_runtime_ffmpeg
            kisak_runtime_mesa
    COMMENT "macOS runtime prefix: ${KISAK_RUNTIME_PREFIX}, mesa: ${KISAK_MESA_PREFIX}")

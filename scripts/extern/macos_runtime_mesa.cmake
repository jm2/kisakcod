##### Mesa KosmicKrisp (ADR-0011) #####
# The Vulkan driver: Mesa's KosmicKrisp on Metal. -Dshared-llvm=enabled is not
# cosmetic. With the static LLVM, mesa_clc carries one copy while the SPIR-V
# translator dylib loads libLLVM, and generating libpoly.spv/libkk.spv dies with
# a pointer-authentication fault in runSpirvWriterPasses (exit 139).
#
# -Dplatforms=macos builds the Metal WSI: VK_KHR_surface and
# VK_EXT_metal_surface, which the client's SDL3 window needs. With no platform
# the driver renders but can't present, and the client can't start.
#
# Build-host prerequisites, not pins: LLVM, glslang and bison must be on PATH
# (see README). Only the ICD dylib is shipped; the .spv it generates follows
# those tools byte for byte, so it is a build artifact and never committed.
ExternalProject_Add(kisak_runtime_mesa
    URL https://archive.mesa3d.org/mesa-26.2.4.tar.xz
    URL_HASH SHA256=bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    DEPENDS kisak_runtime_zstd kisak_runtime_spirv_tools
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
        PATH=${KISAK_MESA_PYTHON_BIN_DIR}:${KISAK_LLVM_BIN_DIR}:$ENV{PATH}
        CCACHE_DIR=${KISAK_CCACHE_DIR}
        MACOSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        PKG_CONFIG_PATH=${KISAK_RUNTIME_PREFIX}/lib/pkgconfig
        ${KISAK_MESON} setup <BINARY_DIR> <SOURCE_DIR>
        --buildtype=release --prefix=${KISAK_MESA_PREFIX} --libdir=lib
        -Dplatforms=macos -Dgallium-drivers= -Dvulkan-drivers=kosmickrisp
        -Dopengl=false -Degl=disabled -Dglx=disabled -Dgbm=disabled
        -Dllvm=enabled -Dshared-llvm=enabled -Dzstd=auto -Dwerror=false
    BUILD_COMMAND ${CMAKE_COMMAND} -E env
        PATH=${KISAK_MESA_PYTHON_BIN_DIR}:${KISAK_LLVM_BIN_DIR}:$ENV{PATH}
        CCACHE_DIR=${KISAK_CCACHE_DIR}
        MACOSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        ${KISAK_NINJA} -C <BINARY_DIR>
    INSTALL_COMMAND ${CMAKE_COMMAND} -E env
        PATH=${KISAK_MESA_PYTHON_BIN_DIR}:${KISAK_LLVM_BIN_DIR}:$ENV{PATH}
        CCACHE_DIR=${KISAK_CCACHE_DIR}
        MACOSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        ${KISAK_NINJA} -C <BINARY_DIR> install
    BUILD_BYPRODUCTS ${KISAK_MESA_PREFIX}/lib/libvulkan_kosmickrisp.dylib
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

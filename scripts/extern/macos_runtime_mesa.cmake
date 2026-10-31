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
#
# The patch fixes two leaks, about 1.5 KB a frame (#971).
# mtl_new_render_pass_descriptor calls init on a descriptor `new` already
# initialized, so every render pass leaks the first color attachment array.
# kk_queue_submit presents the drawable kk_encode_drawable_present retained,
# then drops it without a release, so every frame leaks a CAMetalDrawable.
# The greps fail the build if an edit stops applying. ExternalProject splits
# commands on ";", so a pattern spells it $<SEMICOLON>. Drop the patch once a
# Mesa bump carries both fixes.
ExternalProject_Add(kisak_runtime_mesa
    URL https://archive.mesa3d.org/mesa-26.2.4.tar.xz
    URL_HASH SHA256=bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    PATCH_COMMAND sed -i "" "s/\\[\\[MTL4RenderPassDescriptor new\\] init\\]/[MTL4RenderPassDescriptor new]/" src/kosmickrisp/bridge/mtl_render_state.m
        COMMAND sed -i "" "s/mtl_drawable_present(cmd_buffer->drawable)$<SEMICOLON>$/& mtl_release(cmd_buffer->drawable)$<SEMICOLON>/" src/kosmickrisp/vulkan/kk_queue.c
        COMMAND grep -q "return \\[MTL4RenderPassDescriptor new\\]$<SEMICOLON>" src/kosmickrisp/bridge/mtl_render_state.m
        COMMAND grep -q "mtl_drawable_present(cmd_buffer->drawable)$<SEMICOLON> mtl_release(cmd_buffer->drawable)$<SEMICOLON>" src/kosmickrisp/vulkan/kk_queue.c
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

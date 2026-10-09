##### dxvk-native D3D9 for the macOS client (KISAK_DXVK_MACOS) #####
# The macOS client renders through dxvk-native on KosmicKrisp, Mesa's
# conformant Vulkan driver on Metal (docs/design/CLIENT.md). Upstream DXVK
# needs a few macOS fixes and three relaxed device requirements, so this
# builds the jm2/dxvk fork's kisakcod-macos branch, pinned by commit (see
# KISAK_MACOS.md in the fork). Mesa is not bundled: users install Homebrew
# `mesa` (macOS 26+) and the Vulkan loader picks KosmicKrisp at run time,
# e.g. VK_DRIVER_FILES=$(brew --prefix mesa)/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json.
include(ExternalProject)

find_program(KISAK_MESON meson REQUIRED)
find_program(KISAK_NINJA ninja REQUIRED)

set(KISAK_DXVK_MACOS_PREFIX ${CMAKE_BINARY_DIR}/dxvk-macos)
ExternalProject_Add(kisak_dxvk_macos
    GIT_REPOSITORY https://github.com/jm2/dxvk.git
    # kisakcod-macos branch; bump deliberately.
    GIT_TAG dac6ce62dada1f9fd2a3c347cd66580deecd6bca
    GIT_SUBMODULES_RECURSE ON
    # A pinned commit never needs updating; don't re-run git and meson on every build.
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ${KISAK_MESON} setup <BINARY_DIR> <SOURCE_DIR>
        --buildtype=release --prefix=${KISAK_DXVK_MACOS_PREFIX} --libdir=lib
        -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled
        -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false
        -Dnative_macos=true
    BUILD_COMMAND ${KISAK_NINJA} -C <BINARY_DIR>
    INSTALL_COMMAND ${KISAK_NINJA} -C <BINARY_DIR> install
    BUILD_BYPRODUCTS ${KISAK_DXVK_MACOS_PREFIX}/lib/libdxvk_d3d9.dylib
)

# The headers exist only after the build; the imported target needs the
# directory at configure time.
file(MAKE_DIRECTORY ${KISAK_DXVK_MACOS_PREFIX}/include/dxvk)
add_library(kisak_dxvk_d3d9 SHARED IMPORTED GLOBAL)
set_target_properties(kisak_dxvk_d3d9 PROPERTIES
    IMPORTED_LOCATION ${KISAK_DXVK_MACOS_PREFIX}/lib/libdxvk_d3d9.dylib
    INTERFACE_INCLUDE_DIRECTORIES ${KISAK_DXVK_MACOS_PREFIX}/include/dxvk)
add_dependencies(kisak_dxvk_d3d9 kisak_dxvk_macos)

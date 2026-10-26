##### Vulkan loader (ADR-0011) #####
# The bundle carries its own loader so the KosmicKrisp ICD is found by
# VK_DRIVER_FILES without the host having the SDK installed.
set(KISAK_VULKAN_SDK_TAG vulkan-sdk-1.4.363.0)
ExternalProject_Add(kisak_runtime_vulkan_loader
    URL https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/${KISAK_VULKAN_SDK_TAG}.tar.gz
    URL_HASH SHA256=941eff558fc74f248745fb7df367640cf89a8457f216dae498a74ae14b6ba6f7
    DOWNLOAD_NAME Vulkan-Loader-1.4.363.0.tar.gz
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    DEPENDS kisak_runtime_vulkan_headers
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${KISAK_RUNTIME_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        -DVULKAN_HEADERS_INSTALL_DIR=${KISAK_RUNTIME_PREFIX}
        -DBUILD_TESTS=OFF
        -DBUILD_WERROR=OFF
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libvulkan.1.dylib
    LOG_BUILD ON
    LOG_INSTALL ON
)

##### SPIRV-Tools (ADR-0011) #####
# Mesa links these statically; SPIRV-Headers must come from the pin above, never
# from Homebrew, or the shader tooling is built against unpinned headers.
set(KISAK_SPIRV_HEADERS_SOURCE_DIR
    ${CMAKE_BINARY_DIR}/kisak_runtime_spirv_headers-prefix/src/kisak_runtime_spirv_headers)
ExternalProject_Add(kisak_runtime_spirv_tools
    URL https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/${KISAK_VULKAN_SDK_TAG}.tar.gz
    URL_HASH SHA256=e6c83a215538fbfd265ccf398d3877985055df1273dbed73912bd05307664688
    DOWNLOAD_NAME SPIRV-Tools-1.4.363.0.tar.gz
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    DEPENDS kisak_runtime_spirv_headers
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${KISAK_RUNTIME_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        -DSPIRV-Headers_SOURCE_DIR=${KISAK_SPIRV_HEADERS_SOURCE_DIR}
        -DSPIRV_SKIP_TESTS=ON
        -DBUILD_SHARED_LIBS=ON
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libSPIRV-Tools.dylib
    LOG_BUILD ON
    LOG_INSTALL ON
)

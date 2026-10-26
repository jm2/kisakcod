##### Vulkan-Headers, for the bundled loader (ADR-0011) #####
# Header-only source dependency of Vulkan-Loader. Pinned by tarball hash because
# the headers decide what the shipped loader is compiled against.
ExternalProject_Add(kisak_runtime_vulkan_headers
    URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
    URL_HASH SHA256=4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e
    DOWNLOAD_NAME Vulkan-Headers-vulkan-sdk-1.4.363.0.tar.gz
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory
        <SOURCE_DIR>/include ${KISAK_RUNTIME_PREFIX}/include
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/include/vulkan/vulkan.h
    LOG_INSTALL ON
)

##### SPIRV-Headers, for SPIRV-Tools in Mesa's shader compiler path (ADR-0011) #####
ExternalProject_Add(kisak_runtime_spirv_headers
    URL https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
    URL_HASH SHA256=5b3a0d099f5c7649e29c0938476c462e71d66ad89a0bde42fa1ba3eb476956c9
    DOWNLOAD_NAME SPIRV-Headers-vulkan-sdk-1.4.363.0.tar.gz
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory
        <SOURCE_DIR>/include ${KISAK_RUNTIME_PREFIX}/include
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/include/spirv/unified1/spirv.h
    LOG_INSTALL ON
)

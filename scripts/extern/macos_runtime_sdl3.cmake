##### SDL3 (ADR-0011) #####
# The client's window, input and audio-thread glue. Static SDL must stay off or
# the closure grows a second copy of everything SDL links.
ExternalProject_Add(kisak_runtime_sdl3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${KISAK_RUNTIME_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        -DSDL_SHARED=ON -DSDL_STATIC=OFF
        -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF
        -DSDL_INSTALL_TESTS=OFF -DSDL_INSTALL_DOCS=OFF
        -DSDL_INSTALL_CMAKEDIR_ROOT=lib/cmake/SDL3
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libSDL3.0.dylib
    LOG_BUILD ON
    LOG_INSTALL ON
)

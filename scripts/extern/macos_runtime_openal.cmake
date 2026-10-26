##### OpenAL Soft (ADR-0011) #####
# clang 21 rejects -Werror=function-effects on a noexcept CoreAudio lambda
# (alc/backends/coreaudio.cpp). Downgrade the flag: the warning still shows, the
# build proceeds. Revisit when the toolchain stops making that call.
ExternalProject_Add(kisak_runtime_openal
    URL https://github.com/kcat/openal-soft/releases/download/1.25.2/openal-soft-1.25.2.tar.bz2
    URL_HASH SHA256=1dbaac44e7579d5bc8847ca8db4b2e8b9fd3961041f35ee20def4958301e1089
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    PATCH_COMMAND sed -i "" "s/-Werror=function-effects/-Wfunction-effects/" CMakeLists.txt
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${KISAK_RUNTIME_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        -DLIBTYPE=SHARED
        -DALSOFT_EXAMPLES=OFF
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libopenal.1.dylib
    LOG_BUILD ON
    LOG_INSTALL ON
)

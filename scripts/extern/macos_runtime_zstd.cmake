##### zstd, for the bundled macOS runtime (ADR-0011) #####
# Mesa's KosmicKrisp links zstd, so the app has to carry it.
#
# zstd builds with a hand-written Makefile, not CMake, and its sub-make ignores
# CFLAGS/LDFLAGS. Two consequences the next builder will otherwise rediscover:
# the deployment target has to travel in MOREFLAGS, and PREFIX must be given to
# `make` itself - passing it only to `make install` leaves /usr/local baked into
# the generated .pc, and CMake then resolves the wrong dylib.
set(KISAK_ZSTD_VERSION 1.5.7)

ExternalProject_Add(kisak_runtime_zstd
    URL https://github.com/facebook/zstd/releases/download/v${KISAK_ZSTD_VERSION}/zstd-${KISAK_ZSTD_VERSION}.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE ON
    BUILD_COMMAND ${CMAKE_COMMAND} -E env MACOSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        make -j ${KISAK_JOBS} PREFIX=${KISAK_RUNTIME_PREFIX}
        MOREFLAGS=-mmacosx-version-min=${KISAK_DEPLOYMENT_TARGET}
    INSTALL_COMMAND ${CMAKE_COMMAND} -E env MACOSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        make install PREFIX=${KISAK_RUNTIME_PREFIX}
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libzstd.1.dylib
    LOG_BUILD ON
    LOG_INSTALL ON
)

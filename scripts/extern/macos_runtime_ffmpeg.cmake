##### FFmpeg, LGPL, bink only (ADR-0011) #####
# The cinematic decoder. Everything else is off: no autodetect, so no OpenSSL,
# Vulkan or x264 can be found and the license stays LGPL 2.1+. The decoder is
# called `bink` in FFmpeg 9, not `binkvideo`.
ExternalProject_Add(kisak_runtime_ffmpeg
    URL https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz
    URL_HASH SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
    DOWNLOAD_DIR ${KISAK_SOURCE_CACHE}
    DOWNLOAD_NO_PROGRESS ON
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env MACOSX_DEPLOYMENT_TARGET=${KISAK_DEPLOYMENT_TARGET}
        <SOURCE_DIR>/configure
        --prefix=${KISAK_RUNTIME_PREFIX}
        --enable-shared --disable-static --disable-programs --disable-doc
        --disable-network --disable-autodetect --disable-everything
        --enable-avformat --enable-avcodec --enable-avutil --enable-swresample
        --enable-demuxer=bink
        --enable-decoder=bink,binkaudio_dct,binkaudio_rdft,adpcm_ima_wav
        --enable-parser=bink
        --enable-protocol=file,pipe
        --disable-gpl --disable-nonfree --enable-pic
        --extra-cflags=-mmacosx-version-min=${KISAK_DEPLOYMENT_TARGET}
        --extra-ldflags=-mmacosx-version-min=${KISAK_DEPLOYMENT_TARGET}
    BUILD_COMMAND make -j ${KISAK_JOBS}
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS ${KISAK_RUNTIME_PREFIX}/lib/libavformat.63.dylib
    LOG_CONFIGURE ON
    LOG_BUILD ON
    LOG_INSTALL ON
)

# Builds and runs ffmpeg_stream_check against a runtime prefix's FFmpeg (#942).
# The Linux and macOS runtime projects add it as an ExternalProject after
# FFmpeg, so a prefix whose FFmpeg can't read the game's streams fails to build.
cmake_minimum_required(VERSION 3.20)
project(kisakcod_ffmpeg_check C)

set(KISAK_FFMPEG_PREFIX "" CACHE PATH "the runtime prefix holding FFmpeg's lib/ and include/")
if (NOT KISAK_FFMPEG_PREFIX)
    message(FATAL_ERROR "Set KISAK_FFMPEG_PREFIX to the runtime prefix")
endif()

set(_kisak_ffmpeg_libs "")
foreach (_kisak_lib avformat avcodec avutil)
    find_library(KISAK_LIB_${_kisak_lib} ${_kisak_lib} PATHS "${KISAK_FFMPEG_PREFIX}/lib"
        NO_DEFAULT_PATH REQUIRED)
    list(APPEND _kisak_ffmpeg_libs "${KISAK_LIB_${_kisak_lib}}")
endforeach()

add_executable(ffmpeg_stream_check ffmpeg_stream_check.c)
target_include_directories(ffmpeg_stream_check PRIVATE "${KISAK_FFMPEG_PREFIX}/include")
target_link_libraries(ffmpeg_stream_check PRIVATE ${_kisak_ffmpeg_libs})
set_target_properties(ffmpeg_stream_check PROPERTIES C_STANDARD 11 BUILD_RPATH "${KISAK_FFMPEG_PREFIX}/lib")

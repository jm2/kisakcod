# Helpers shared by more than one test area.
# Included from tests/CMakeLists.txt.

set(_platform_socket_sources ${KISAK_PLATFORM_SERVICE_SOURCES})
list(FILTER _platform_socket_sources INCLUDE REGEX "[/\\\\]sys_socket\\.cpp$")
list(LENGTH _platform_socket_sources _platform_socket_source_count)
if (NOT _platform_socket_source_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one selected platform sys_socket.cpp, found "
        "${_platform_socket_source_count}")
endif()

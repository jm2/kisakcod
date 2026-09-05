cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path "${SOURCE_ROOT}/src/qcommon/sys_socket.h")
set(_posix_source_path "${SOURCE_ROOT}/src/_platform/posix/sys_socket.cpp")
set(_win32_source_path "${SOURCE_ROOT}/src/_platform/win32/sys_socket.cpp")
set(_linux_platform_cmake
    "${SOURCE_ROOT}/scripts/platform/linux/platform.cmake")
set(_macos_platform_cmake
    "${SOURCE_ROOT}/scripts/platform/macos/platform.cmake")
set(_win32_platform_cmake
    "${SOURCE_ROOT}/scripts/platform/win32/platform.cmake")
set(_socket_tests_path "${SOURCE_ROOT}/tests/platform_socket_tests.cpp")
set(_tests_cmake_path "${SOURCE_ROOT}/tests/CMakeLists.txt")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_posix_source_path}"
    "${_win32_source_path}"
    "${_linux_platform_cmake}"
    "${_macos_platform_cmake}"
    "${_win32_platform_cmake}"
    "${_socket_tests_path}"
    "${_tests_cmake_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing platform-socket source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_posix_source_path}" _posix_source)
file(READ "${_win32_source_path}" _win32_source)
file(READ "${_linux_platform_cmake}" _linux_platform)
file(READ "${_macos_platform_cmake}" _macos_platform)
file(READ "${_win32_platform_cmake}" _win32_platform)
file(READ "${_socket_tests_path}" _socket_tests)
file(READ "${_tests_cmake_path}" _tests_cmake)

function(require_contains _content _needle _message)
    if(NOT _content MATCHES "${_needle}")
        message(FATAL_ERROR "${_message}")
    endif()
endfunction()

function(require_not_contains _content _needle _message)
    if(_content MATCHES "${_needle}")
        message(FATAL_ERROR "${_message}")
    endif()
endfunction()

# The portable header owns the contract without importing any platform type.
foreach(_marker IN ITEMS
    "struct SysSocket;"
    "inline constexpr std::uint32_t SysSocketMaxDatagramBytes"
    "SysSocketOpenStatus KISAK_CDECL Sys_SocketOpenUdp\\("
    "SysSocketCloseStatus KISAK_CDECL Sys_SocketClose\\(SysSocketHandle \\*handle\\);"
    "SysSocketSendStatus KISAK_CDECL Sys_SocketSendTo\\("
    "SysSocketRecvStatus KISAK_CDECL Sys_SocketRecvFrom\\("
    "Truncated,"
    "SysSocketOptionStatus KISAK_CDECL Sys_SocketEnableBroadcast\\("
    "bool KISAK_CDECL Sys_SocketGetLocalAddress\\("
    "bool KISAK_CDECL Sys_SocketMakeLoopbackAddress\\("
    "bool KISAK_CDECL Sys_SocketMakeAnyAddress\\("
    "bool KISAK_CDECL Sys_SocketAddressIsEqual\\(")
    require_contains("${_header}" "${_marker}"
        "portable socket header owns its declared contract: ${_marker}")
endforeach()
foreach(_forbidden IN ITEMS
    "Windows\\.h"
    "winsock"
    "sys/socket\\.h"
    "sockaddr"
    "SOCKET "
    "WSAStartup"
    "#include <Windows")
    require_not_contains("${_header}" "${_forbidden}"
        "portable socket header must not leak platform socket types: ${_forbidden}")
endforeach()

# The Win32 backend uses the canonical Winsock primitives and never the
# POSIX socket API.
foreach(_marker IN ITEMS
    "socket\\(AF_INET, SOCK_DGRAM, IPPROTO_UDP\\)"
    "ioctlsocket\\("
    "FIONBIO"
    "SO_BROADCAST"
    "closesocket\\("
    "WSAStartup"
    "WSAEWOULDBLOCK"
    "WSAEMSGSIZE")
    require_contains("${_win32_source}" "${_marker}"
        "Win32 socket backend must use the canonical Winsock primitive: ${_marker}")
endforeach()
# Exclusive bind ownership is contract: a port-sharing option would let a
# second open of a held nonzero port succeed and silently compete for its
# datagrams instead of reporting SystemFailure.
foreach(_forbidden IN ITEMS
    "sys/socket\\.h"
    "<unistd\\.h>"
    "fcntl\\.h"
    "recvfrom\\(.*MSG_"
    "SO_REUSEADDR")
    require_not_contains("${_win32_source}" "${_forbidden}"
        "Win32 socket backend must not import the POSIX socket API or share bound ports: ${_forbidden}")
endforeach()

# The POSIX backend uses the canonical BSD socket primitives and never the
# Winsock API. Sockets are created close-on-exec and oversized datagrams
# are detected through MSG_TRUNC so truncation is explicit on both
# platforms.
foreach(_marker IN ITEMS
    "socket\\(AF_INET, SOCK_DGRAM, IPPROTO_UDP\\)"
    "SOCK_CLOEXEC"
    "FD_CLOEXEC"
    "recvmsg\\("
    "MSG_TRUNC"
    "O_NONBLOCK"
    "SO_BROADCAST"
    "EAGAIN \\|\\| errno == EWOULDBLOCK"
    "getsockname\\("
    "close\\(")
    require_contains("${_posix_source}" "${_marker}"
        "POSIX socket backend must use the canonical BSD primitive: ${_marker}")
endforeach()
# Exclusive bind ownership is contract: a port-sharing option would let a
# second open of a held nonzero port succeed and silently compete for its
# datagrams instead of reporting SystemFailure.
foreach(_forbidden IN ITEMS
    "Windows\\.h"
    "winsock"
    "ioctlsocket"
    "WSAStartup"
    "SO_REUSEADDR"
    "SO_REUSEPORT")
    require_not_contains("${_posix_source}" "${_forbidden}"
        "POSIX socket backend must not import Winsock or share bound ports: ${_forbidden}")
endforeach()

# Each platform source set registers exactly its own backend.
require_contains("${_win32_platform}"
    "_platform/win32/sys_socket\\.cpp"
    "the Win32 service set must register the Winsock socket backend")
require_contains("${_linux_platform}"
    "_platform/posix/sys_socket\\.cpp"
    "the Linux service set must register the POSIX socket backend")
require_contains("${_macos_platform}"
    "_platform/posix/sys_socket\\.cpp"
    "the macOS service set must register the POSIX socket backend")
require_not_contains("${_linux_platform}" "_platform/win32/sys_socket\\.cpp"
    "the Linux service set must not register the Winsock backend")
require_not_contains("${_macos_platform}" "_platform/win32/sys_socket\\.cpp"
    "the macOS service set must not register the Winsock backend")

# The runtime suite proves the loopback datagram contract end to end,
# including the explicit oversized-datagram truncation result and the
# exclusive-ownership rejection of a second open of a held port.
foreach(_marker IN ITEMS
    "Sys_SocketOpenUdp\\(0, true, "
    "SysSocketRecvStatus::WouldBlock"
    "SysSocketRecvStatus::Truncated"
    "SysSocketMaxDatagramBytes \\+ 1"
    "MessageTooLarge"
    "Sys_SocketEnableBroadcast\\("
    "second open of a held port reports SystemFailure"
    "Sys_SocketClose\\(&first\\) == SysSocketCloseStatus::Closed")
    require_contains("${_socket_tests}" "${_marker}"
        "socket runtime coverage: ${_marker}")
endforeach()

# The suite registration and seals stay wired.
foreach(_marker IN ITEMS
    "kisakcod-platform-socket-tests"
    "platform-socket-contracts"
    "platform-socket-source-invariants"
    "platform_socket_source_test\\.cmake"
    "platform_socket_tests\\.cpp")
    require_contains("${_tests_cmake}" "${_marker}"
        "socket test registration: ${_marker}")
endforeach()
require_contains("${_tests_cmake}" "ws2_32"
    "the Winsock backend must be linked on Win32 test targets")

# Every test target that compiles the full platform service set links the
# Winsock import library on Win32: the set now contains the socket backend,
# so a full-set target without ws2_32 fails at the Windows linker. Filtered
# per-service set(_platform_*_sources ...) assignments are stripped first so
# only real full-set add_executable blocks are checked.
string(REGEX REPLACE
    "set\\(_platform_[a-z_]+_sources \\$\\{KISAK_PLATFORM_SERVICE_SOURCES\\}\\)\n"
    "" _full_set_tests "${_tests_cmake}")
string(REPLACE "add_executable(" ";add_executable(" _full_set_tests
    "${_full_set_tests}")
foreach(_chunk IN LISTS _full_set_tests)
    if(_chunk MATCHES "^add_executable\\(" AND _chunk MATCHES "KISAK_PLATFORM_SERVICE_SOURCES")
        require_contains("${_chunk}" "ws2_32"
            "full-service-set test target must link the Winsock import library on Win32")
    endif()
endforeach()

message(STATUS "platform-socket source invariants passed")

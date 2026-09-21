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
set(_net_chan_source_path "${SOURCE_ROOT}/src/qcommon/net_chan_mp.cpp")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_posix_source_path}"
    "${_win32_source_path}"
    "${_linux_platform_cmake}"
    "${_macos_platform_cmake}"
    "${_win32_platform_cmake}"
    "${_socket_tests_path}"
    "${_tests_cmake_path}"
    "${_net_chan_source_path}")
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
file(READ "${_net_chan_source_path}" _net_chan_source)

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
    "bool KISAK_CDECL Sys_SocketAddressIsEqual\\("
    "SysSocketResolveStatus KISAK_CDECL Sys_SocketResolveHost\\("
    "SysSocketResolveStatus KISAK_CDECL Sys_SocketResolveErrorStatus\\("
    "NotFound,")
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
    "WSAEMSGSIZE"
    "getaddrinfo\\("
    "defined\\(EAI_NODATA\\)"
    "defined\\(EAI_ADDRFAMILY\\)"
    "KISAK_SOCKET_TEST_HOOKS"
    "Kisak_SocketSetResolveTestHook")
    require_contains("${_win32_source}" "${_marker}"
        "Win32 socket backend must use the canonical Winsock primitive: ${_marker}")
endforeach()
# The receive length is clamped to the datagram bound before the signed
# conversion: the public uint32 capacity converted directly to the Winsock
# int length wraps negative at capacities of 2^31 and above, turning a
# valid reserved receive window into an invalid request.
require_contains("${_win32_source}" "static_cast<int>\\(recvLength\\)"
    "Win32 socket backend must convert the clamped receive length, not the raw capacity")
require_not_contains("${_win32_source}" "static_cast<int>\\(bufferCapacity\\)"
    "Win32 socket backend must not convert the raw receive capacity to the signed length")
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
    "getaddrinfo\\("
    "close\\("
    "defined\\(EAI_NODATA\\)"
    "defined\\(EAI_ADDRFAMILY\\)"
    "KISAK_SOCKET_TEST_HOOKS"
    "Kisak_SocketSetResolveTestHook")
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

# Production enrollment: NET_StringToAdr resolves hostnames through the
# portable socket-service resolver instead of the platform gethostbyname
# path, while the shapes whose retail semantics are pinned (21-character
# IPX-format strings and digit-leading numeric literals with inet_addr's
# partial-form rules) stay on the legacy platform helper. A resolver
# failure maps to the same observable NA_BAD result the legacy path
# produced.
#
# These are ordered multi-line relationship windows, not independent
# markers: each window binds the dispatch and failure constructs into one
# match, so sending ordinary names to the legacy helper, sending legacy
# shapes to the portable resolver, or dropping the NA_BAD failure mapping
# breaks the window. CR characters are stripped first so the windows match
# under either checkout EOL convention.
string(REGEX REPLACE "\r" "" _net_chan_norm "${_net_chan_source}")

# The bounded host length and the portable service include are single
# constructs; the relationships below carry the dispatch structure.
require_contains("${_net_chan_norm}" "#include \"sys_socket.h\""
    "NET_StringToAdr must include the portable socket service header")
require_contains("${_net_chan_norm}"
    "const size_t baseLength = strnlen\\(base, sizeof\\(base\\)\\);"
    "NET_StringToAdr must measure the host through the bounded strnlen")

# Dispatch relationship: the legacy-shape predicate feeds one selection in
# which the legacy branch alone owns Sys_StringToAdr and the ordinary
# hostname branch alone owns Sys_SocketResolveHost with its status gate --
# all in one ordered block.
require_contains("${_net_chan_norm}"
    "const bool legacyPlatformShape =\n[^\n]*\\(baseLength == 21 && base\\[8\\] == '\\.'\\)\n[^\n]*\\|\\| \\(base\\[0\\] >= '0' && base\\[0\\] <= '9'\\);\n[^\n]*bool resolved = false;\n[^\n]*if \\(legacyPlatformShape\\)\n[^\n]*\\{\n[^\n]*resolved = Sys_StringToAdr\\(base, a\\) != 0;\n[^\n]*\\}\n[^\n]*else\n[^\n]*\\{\n[^\n]*SysSocketAddress socketAddress;\n[^\n]*if \\(Sys_SocketResolveHost\\(base, 0, &socketAddress\\)\n[^\n]*== SysSocketResolveStatus::Resolved\\)"
    "NET_StringToAdr must dispatch legacy shapes to Sys_StringToAdr and ordinary hostnames to the portable resolver, in that order")

# Publication relationship: only inside the Resolved gate is the address
# published as NA_IP through the bounded four-octet element copy, the port
# reset, and the success flag -- in that order.
require_contains("${_net_chan_norm}"
    "== SysSocketResolveStatus::Resolved\\)\n[^\n]*\\{\n[^\n]*a->type = NA_IP;\n.*static_assert\\(sizeof\\(a->ip\\) == sizeof\\(socketAddress\\.address\\),\n[^\n]*\"address octet width mismatch\"\\);\n[^\n]*for \\(size_t octet = 0; octet < sizeof\\(a->ip\\); \\+\\+octet\\)\n[^\n]*\\{\n[^\n]*a->ip\\[octet\\] = socketAddress\\.address\\[octet\\];\n[^\n]*\\}\n[^\n]*a->port = 0;\n[^\n]*resolved = true;"
    "NET_StringToAdr must publish a resolved address only inside the Resolved gate via the bounded octet copy")

# Failure relationship: a resolved broadcast address maps to NA_BAD with an
# immediate failure return, and an unresolved host falls through the
# resolved branch to the same NA_BAD result -- both bound to their
# surrounding control flow.
require_contains("${_net_chan_norm}"
    "if \\(resolved\\)\n[^\n]*\\{\n[^\n]*if \\(a->ip\\[0\\] == 255 && a->ip\\[1\\] == 255 && a->ip\\[2\\] == 255 && a->ip\\[3\\] == 255\\)\n[^\n]*\\{\n[^\n]*a->type = NA_BAD;\n[^\n]*return 0;\n[^\n]*\\}"
    "NET_StringToAdr must map a resolved broadcast address to NA_BAD with a failure return")
require_contains("${_net_chan_norm}"
    "a->port = v5;\n[^\n]*return 1;\n[^\n]*\\}\n[^\n]*\\}\n[^\n]*a->type = NA_BAD;\n[^\n]*return 0;"
    "NET_StringToAdr must map an unresolved host to NA_BAD as the dispatch fallthrough")
# The portable header stays the only resolver contract in qcommon: the
# enrollment must not re-declare platform resolver primitives locally.
foreach(_forbidden IN ITEMS
    "gethostbyname"
    "getaddrinfo"
    "addrinfo"
    "hostent")
    require_not_contains("${_net_chan_source}" "${_forbidden}"
        "NET_StringToAdr must resolve through the portable service, not raw resolver primitives: ${_forbidden}")
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
# including the explicit oversized-datagram truncation result, the
# exclusive-ownership rejection of a second open of a held port, and the
# receive-capacity boundary regression through a reserved 2-GiB window.
foreach(_marker IN ITEMS
    "Sys_SocketOpenUdp\\(0, true, "
    "SysSocketRecvStatus::WouldBlock"
    "SysSocketRecvStatus::Truncated"
    "SysSocketMaxDatagramBytes \\+ 1"
    "MessageTooLarge"
    "Sys_SocketEnableBroadcast\\("
    "second open of a held port reports SystemFailure"
    "oversize capacity receives the datagram"
    "Sys_SocketResolveHost\\("
    "unresolvable host does not resolve"
    "Sys_SocketResolveErrorStatus\\("
    "addressless hostname maps to NotFound"
    "address-family no-address maps to NotFound"
    "Kisak_SocketSetResolveTestHook\\(FailResolveQuery\\)"
    "forced system resolver failure does not resolve"
    "forced address-family no-address does not resolve"
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
    "target_compile_definitions\\(kisakcod-platform-socket-tests PRIVATE KISAK_SOCKET_TEST_HOOKS=1\\)"
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

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

# The HTTP download transport must route every network byte through the
# portable Sys_Socket* stream service and every disk byte through the
# engine file service, with the pure protocol helpers in dl_http.cpp /
# dl_http_url.cpp / dl_http_parse.cpp kept dependency-free. These
# invariants pin that layering so a later edit cannot quietly reintroduce
# raw sockets, raw stdio, or platform leaks into the download path.

set(_dl_main_source_path "${SOURCE_ROOT}/src/qcommon/dl_main.cpp")
set(_dl_main_header_path "${SOURCE_ROOT}/src/qcommon/dl_main.h")
set(_dl_main_internal_path "${SOURCE_ROOT}/src/qcommon/dl_main_internal.h")
set(_dl_main_pump_path "${SOURCE_ROOT}/src/qcommon/dl_main_pump.cpp")
set(_dl_http_source_path "${SOURCE_ROOT}/src/qcommon/dl_http.cpp")
set(_dl_http_header_path "${SOURCE_ROOT}/src/qcommon/dl_http.h")
set(_dl_http_internal_path "${SOURCE_ROOT}/src/qcommon/dl_http_internal.h")
set(_dl_http_url_path "${SOURCE_ROOT}/src/qcommon/dl_http_url.cpp")
set(_dl_http_parse_path "${SOURCE_ROOT}/src/qcommon/dl_http_parse.cpp")
set(_socket_header_path "${SOURCE_ROOT}/src/qcommon/sys_socket.h")
set(_posix_source_path "${SOURCE_ROOT}/src/_platform/posix/sys_socket.cpp")
set(_win32_source_path "${SOURCE_ROOT}/src/_platform/win32/sys_socket.cpp")
set(_cl_main_source_path "${SOURCE_ROOT}/src/client_mp/cl_main_mp.cpp")
set(_cl_parse_source_path "${SOURCE_ROOT}/src/client_mp/cl_parse_mp.cpp")
set(_tests_cmake_path "${SOURCE_ROOT}/tests/CMakeLists.txt")

foreach(_path IN ITEMS
    "${_dl_main_source_path}"
    "${_dl_main_header_path}"
    "${_dl_main_internal_path}"
    "${_dl_main_pump_path}"
    "${_dl_http_source_path}"
    "${_dl_http_header_path}"
    "${_dl_http_internal_path}"
    "${_dl_http_url_path}"
    "${_dl_http_parse_path}"
    "${_socket_header_path}"
    "${_posix_source_path}"
    "${_win32_source_path}"
    "${_cl_main_source_path}"
    "${_cl_parse_source_path}"
    "${_tests_cmake_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing download transport source: ${_path}")
    endif()
endforeach()

file(READ "${_dl_main_source_path}" _dl_main_source)
file(READ "${_dl_main_header_path}" _dl_main_header)
file(READ "${_dl_main_internal_path}" _dl_main_internal)
file(READ "${_dl_main_pump_path}" _dl_main_pump)
file(READ "${_dl_http_source_path}" _dl_http_source)
file(READ "${_dl_http_header_path}" _dl_http_header)
file(READ "${_dl_http_internal_path}" _dl_http_internal)
file(READ "${_dl_http_url_path}" _dl_http_url)
file(READ "${_dl_http_parse_path}" _dl_http_parse)
file(READ "${_socket_header_path}" _socket_header)
file(READ "${_posix_source_path}" _posix_source)
file(READ "${_win32_source_path}" _win32_source)
file(READ "${_cl_main_source_path}" _cl_main_source)
file(READ "${_cl_parse_source_path}" _cl_parse_source)
file(READ "${_tests_cmake_path}" _tests_cmake)

# The transport split (state machine + pumps) and the protocol split
# (formatting + URL parsing + head parsing) are checked as unions: the
# invariants constrain the transport and the protocol unit as wholes, not
# the file layout that happens to hold them today.
set(_dl_transport_source "${_dl_main_source}${_dl_main_pump}")
set(_dl_protocol_source "${_dl_http_source}${_dl_http_url}${_dl_http_parse}")

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

# The portable header owns the stream contract without platform types.
foreach(_marker IN ITEMS
    "enum class SysSocketStreamOpenStatus"
    "enum class SysSocketStreamConnectStatus"
    "enum class SysSocketStreamPollStatus"
    "enum class SysSocketStreamSendStatus"
    "enum class SysSocketStreamRecvStatus"
    "SysSocketStreamOpenStatus KISAK_CDECL Sys_SocketOpenStream\\("
    "SysSocketStreamConnectStatus KISAK_CDECL Sys_SocketConnectStream\\("
    "SysSocketStreamPollStatus KISAK_CDECL Sys_SocketPollConnected\\("
    "SysSocketStreamSendStatus KISAK_CDECL Sys_SocketSendStream\\("
    "SysSocketStreamRecvStatus KISAK_CDECL Sys_SocketRecvStream\\("
    "InProgress,"
    "WouldBlock,"
    "Disconnected,")
    require_contains("${_socket_header}" "${_marker}"
        "portable socket header must own the stream contract: ${_marker}")
endforeach()

# The POSIX backend implements the stream extension with the canonical BSD
# primitives: close-on-exec stream sockets, poll/write readiness with
# SO_ERROR, and the nonblocking in-progress handshake.
foreach(_marker IN ITEMS
    "SOCK_STREAM \\| SOCK_CLOEXEC"
    "poll\\("
    "POLLOUT"
    "SO_ERROR"
    "O_NONBLOCK"
    "EINPROGRESS"
    "EISCONN"
    "EAGAIN"
    "EPIPE"
    "ECONNRESET")
    require_contains("${_posix_source}" "${_marker}"
        "POSIX socket backend must implement the stream contract with BSD primitives: ${_marker}")
endforeach()
require_not_contains("${_posix_source}" "ioctlsocket"
    "POSIX socket backend must not use the Winsock control API")

# The Win32 backend implements the stream extension with the canonical
# Winsock primitives; POSIX headers and port-sharing remain forbidden.
foreach(_marker IN ITEMS
    "socket\\(AF_INET, SOCK_STREAM, IPPROTO_TCP\\)"
    "FIONBIO"
    "SO_ERROR"
    "WSAEWOULDBLOCK"
    "WSAEINPROGRESS"
    "WSAEISCONN"
    "WSAECONNRESET")
    require_contains("${_win32_source}" "${_marker}"
        "Win32 socket backend must implement the stream contract with Winsock primitives: ${_marker}")
endforeach()
foreach(_forbidden IN ITEMS
    "sys/socket\\.h"
    "<unistd\\.h>"
    "SO_REUSEADDR")
    require_not_contains("${_win32_source}" "${_forbidden}"
        "Win32 socket backend must not import the POSIX socket API or share ports: ${_forbidden}")
endforeach()

# The download transport routes all network I/O through the stream service
# and all disk I/O through the engine file service.
foreach(_marker IN ITEMS
    "Sys_SocketResolveHost\\("
    "Sys_SocketOpenStream\\("
    "Sys_SocketConnectStream\\("
    "Sys_SocketPollConnected\\("
    "Sys_SocketSendStream\\("
    "Sys_SocketRecvStream\\("
    "Sys_SocketClose\\("
    "Dl_ParseRedirectUrl\\("
    "Dl_FormatGetRequest\\("
    "Dl_ParseResponseHead\\("
    "Sys_Milliseconds\\("
    "FS_FileOpenWriteBinary\\("
    "FS_FileWrite\\("
    "FS_FileClose\\("
    "DL_BytesRead\\("
    "DL_InProgress\\("
    "DL_DLIsMotd\\("
    "DL_BeginDownload\\("
    "DL_CancelDownload\\("
    "DL_InitDownload\\("
    "DL_DownloadLoop\\(")
    require_contains("${_dl_transport_source}" "${_marker}"
        "download transport must own the pinned service call: ${_marker}")
endforeach()
# Raw socket primitives, resolver calls, and stdio are banned in the
# transport: the platform backends own sockets, com_files owns files.
foreach(_forbidden IN ITEMS
    "socket\\("
    "connect\\("
    "send\\("
    "recv\\("
    "select\\("
    "poll\\("
    "gethostbyname"
    "getaddrinfo"
    "inet_addr"
    "WSAStartup"
    "#include <winsock"
    "sys/socket\\.h"
    "fopen\\("
    "fwrite\\("
    "fread\\("
    "fclose\\("
    "legacyHacks")
    require_not_contains("${_dl_transport_source}" "${_forbidden}"
        "download transport must not bypass the service layer: ${_forbidden}")
endforeach()

# The transport entry points stay declared with the engine-wide convention.
foreach(_marker IN ITEMS
    "dlStatus_t"
    "DL_InProgress\\("
    "DL_BytesRead\\("
    "DL_DLIsMotd\\("
    "DL_BeginDownload\\("
    "DL_CancelDownload\\("
    "DL_DownloadLoop\\(")
    require_contains("${_dl_main_header}" "${_marker}"
        "download header must declare the pinned entry point: ${_marker}")
endforeach()

# The protocol helpers stay a pure unit: no engine logging, no sockets,
# no engine headers beyond their own.
foreach(_marker IN ITEMS
    "Dl_ParseRedirectUrl\\("
    "Dl_FormatGetRequest\\("
    "Dl_ParseResponseHead\\("
    "Authorization: Basic ")
    require_contains("${_dl_protocol_source}" "${_marker}"
        "download protocol unit must own the pinned helper: ${_marker}")
endforeach()
foreach(_forbidden IN ITEMS
    "Com_Printf"
    "Com_DPrintf"
    "Com_Error"
    "socket\\("
    "sys/socket\\.h"
    "winsock"
    "qcommon/qcommon\\.h")
    require_not_contains("${_dl_protocol_source}" "${_forbidden}"
        "download protocol unit must stay dependency-free: ${_forbidden}")
endforeach()

# The client keeps feeding the legacy progress meter from the transport
# accessor, and the parse path keeps masking embedded credentials out of
# the display name.
require_contains("${_cl_main_source}"
    "legacyHacks\\.cl_downloadCount = DL_BytesRead\\(\\);"
    "client main must feed the legacy progress meter from DL_BytesRead")
foreach(_marker IN ITEMS
    "Dl_ParseRedirectUrl\\(cls\\.downloadName"
    "I_strncpyz\\(legacyHacks\\.cl_downloadName"
    "http://\\*:\\*")
    require_contains("${_cl_parse_source}" "${_marker}"
        "client parse must preserve the masked download display name: ${_marker}")
endforeach()

# The later WWW failure paths route the download name through the same
# sanitizer: cls.downloadName may still carry user:password@ when the
# transfer fails, and no logged or dropped message may show it.
require_contains("${_cl_main_source}"
    "CL_SanitizeDownloadUrl\\(cls\\.downloadName"
    "client main must route WWW failure messages through the URL sanitizer")
require_not_contains("${_cl_main_source}"
    "Download failure while getting %s\", cls\\.downloadName"
    "client main must not log the raw download name on WWW failure")

# The split units stay wired to their shared internal headers, and the
# transport/protocol unions stay dependency-free through those headers.
function(require_internal_include _content _needle)
    require_contains("${_content}" "${_needle}"
        "split download unit must include its shared internal header: ${_needle}")
endfunction()
require_internal_include("${_dl_main_source}" "dl_main_internal\\.h")
require_internal_include("${_dl_main_pump}" "dl_main_internal\\.h")
require_internal_include("${_dl_http_source}" "dl_http_internal\\.h")
require_internal_include("${_dl_http_url}" "dl_http_internal\\.h")
require_internal_include("${_dl_http_parse}" "dl_http_internal\\.h")

# The protocol unit and both test binaries stay registered in the test
# build, and the source invariants run in ctest.
foreach(_marker IN ITEMS
    "dl_http\\.cpp"
    "dl_http_url\\.cpp"
    "dl_http_parse\\.cpp"
    "dl_http_tests\\.cpp"
    "platform_socket_stream_tests\\.cpp"
    "dl-download-source-invariants"
    "dl-http-protocol-contracts"
    "platform-socket-stream-contracts")
    require_contains("${_tests_cmake}" "${_marker}"
        "test build must register the download transport coverage: ${_marker}")
endforeach()

message(STATUS "dl-download source invariants: OK")

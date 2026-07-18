cmake_minimum_required(VERSION 3.16)

function(read_repo_file RELATIVE_PATH OUT_VAR)
    file(READ "${SOURCE_ROOT}/${RELATIVE_PATH}" _text)
    set(${OUT_VAR} "${_text}" PARENT_SCOPE)
endfunction()

function(require_contains TEXT_VAR NEEDLE DESCRIPTION)
    string(FIND "${${TEXT_VAR}}" "${NEEDLE}" _position)
    if (_position EQUAL -1)
        message(FATAL_ERROR "Missing console invariant: ${DESCRIPTION}")
    endif()
endfunction()

function(forbid_contains TEXT_VAR NEEDLE DESCRIPTION)
    string(FIND "${${TEXT_VAR}}" "${NEEDLE}" _position)
    if (NOT _position EQUAL -1)
        message(FATAL_ERROR "Forbidden console regression: ${DESCRIPTION}")
    endif()
endfunction()

read_repo_file("src/qcommon/sys_console.h" _public_header)
foreach(_public_contract IN ITEMS
    "enum class SysConsoleOutputStream"
    "enum class SysConsoleIoStatus"
    "enum class SysConsoleReadStatus"
    "SYS_CONSOLE_MAX_LINE_LENGTH = 511"
    "Sys_ConsoleWrite("
    "Sys_ConsoleFlush("
    "Sys_ConsoleIsRedirected("
    "Sys_ConsoleTryReadLine(")
    require_contains(
        _public_header
        "${_public_contract}"
        "platform-neutral public API token ${_public_contract}")
endforeach()
foreach(_native_token IN ITEMS
    "Windows.h"
    "HANDLE"
    "DWORD"
    "unistd.h"
    "pthread"
    "pollfd")
    forbid_contains(
        _public_header
        "${_native_token}"
        "native token ${_native_token} leaked into the public header")
endforeach()

read_repo_file("src/qcommon/sys_console.cpp" _common_source)
foreach(_parser_contract IN ITEMS
    "std::array<char, SYS_CONSOLE_MAX_LINE_LENGTH> line{};"
    "CONSOLE_INPUT_READ_BUDGET = 4096"
    "if (byte == 0)"
    "inputState.invalid || inputState.truncated"
    "inputState.length >= outputCapacity"
    "SysConsoleReadStatus::EndOfFile")
    require_contains(
        _common_source
        "${_parser_contract}"
        "bounded line parser token ${_parser_contract}")
endforeach()
foreach(_allocation_token IN ITEMS
    "std::vector"
    "std::string"
    "malloc("
    "calloc("
    "realloc("
    "new ")
    forbid_contains(
        _common_source
        "${_allocation_token}"
        "line parser allocation token ${_allocation_token}")
endforeach()

read_repo_file("src/_platform/posix/sys_console.cpp" _posix_source)
foreach(_posix_contract IN ITEMS
    "poll(&input, 1, 0)"
    "write(descriptor, bytes, request)"
    "fstat(descriptor, &status)"
    "isatty(descriptor) == 0"
    "errno == EINTR"
    "pthread_sigmask(SIG_BLOCK"
    "pthread_sigmask(SIG_SETMASK"
    "pendingBefore_"
    "sigpending(&pending)"
    "if (membership == 0)"
    "sigwait(&blocked_"
    "consumeGenerated(writeError)"
    "writeError != EPIPE")
    require_contains(
        _posix_source
        "${_posix_contract}"
        "POSIX backend token ${_posix_contract}")
endforeach()

read_repo_file("tests/CMakeLists.txt" _test_manifest)
foreach(_manifest_contract IN ITEMS
    "kisakcod-platform-console-tests PRIVATE Threads::Threads"
    "platform-console-invalid-eof-contracts"
    "platform-console-sigpipe-default-contracts"
    "platform-console-sigpipe-ignore-contracts")
    require_contains(
        _test_manifest
        "${_manifest_contract}"
        "console test registration token ${_manifest_contract}")
endforeach()
foreach(_unsafe_sigpipe_token IN ITEMS
    "signal(SIGPIPE"
    "sigaction(SIGPIPE"
    "MSG_NOSIGNAL"
    "SO_NOSIGPIPE")
    forbid_contains(
        _posix_source
        "${_unsafe_sigpipe_token}"
        "process-global or socket-only SIGPIPE workaround ${_unsafe_sigpipe_token}")
endforeach()

read_repo_file("src/_platform/win32/sys_console.cpp" _win32_source)
foreach(_win32_contract IN ITEMS
    "GetStdHandle("
    "WriteFile("
    "FlushFileBuffers("
    "PeekNamedPipe("
    "ReadFile("
    "type == FILE_TYPE_CHAR"
    "type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE"
    "ERROR_PIPE_NOT_CONNECTED"
    "error == ERROR_MORE_DATA && readCount == 1"
    "SysConsoleRawReadStatus::NoData")
    require_contains(
        _win32_source
        "${_win32_contract}"
        "Win32 backend token ${_win32_contract}")
endforeach()

read_repo_file("tests/platform_console_tests.cpp" _runtime_tests)
foreach(_runtime_contract IN ITEMS
    "Sys_ConsoleFlush(stream)"
    "std::string rejected(4097, 'q')"
    "--invalid-eof"
    "--sigpipe-default"
    "--sigpipe-ignore"
    "default SIGPIPE containment"
    "ignored SIGPIPE containment"
    "message pipe preserves ERROR_MORE_DATA bytes")
    require_contains(
        _runtime_tests
        "${_runtime_contract}"
        "console runtime regression token ${_runtime_contract}")
endforeach()

read_repo_file("src/win32/win_syscon.cpp" _win_syscon)
foreach(_integration_contract IN ITEMS
    "Sys_ConsoleTryReadLine("
    "Sys_ConsoleWrite("
    "Sys_ConsoleIsRedirected("
    "if ( s_wcd.consoleText[0] == 0 )"
    "Conbuf_AppendHeadlessHistory(msg);")
    require_contains(
        _win_syscon
        "${_integration_contract}"
        "Windows console integration token ${_integration_contract}")
endforeach()
foreach(_obsolete_helper IN ITEMS
    "Conbuf_WriteProcessHandle"
    "Conbuf_IsRedirectedHandle"
    "GetStdHandle("
    "WriteFile("
    "FlushFileBuffers(")
    forbid_contains(
        _win_syscon
        "${_obsolete_helper}"
        "duplicated Win32 console I/O token ${_obsolete_helper}")
endforeach()

read_repo_file("src/win32/win_main.cpp" _win_main)
foreach(_fatal_contract IN ITEMS
    "Sys_ConsoleWrite("
    "Sys_ConsoleFlush("
    "Sys_ConsoleIsRedirected("
    "OutputDebugStringA("
    "ExitProcess(EXIT_FAILURE);")
    require_contains(
        _win_main
        "${_fatal_contract}"
        "Windows fatal-path token ${_fatal_contract}")
endforeach()
foreach(_obsolete_helper IN ITEMS
    "Win_WriteProcessHandle"
    "Win_IsRedirectedHandle"
    "GetStdHandle(STD_ERROR_HANDLE)"
    "FlushFileBuffers(stderrHandle)")
    forbid_contains(
        _win_main
        "${_obsolete_helper}"
        "duplicated Windows fatal-output token ${_obsolete_helper}")
endforeach()

foreach(_source_list IN ITEMS
    "scripts/common_files.cmake"
    "scripts/platform/win32/platform.cmake"
    "scripts/platform/linux/platform.cmake"
    "scripts/platform/macos/platform.cmake")
    read_repo_file("${_source_list}" _source_list_text)
    require_contains(
        _source_list_text
        "sys_console.cpp"
        "${_source_list} must select a console implementation")
endforeach()

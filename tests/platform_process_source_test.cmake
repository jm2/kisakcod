cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path "${SOURCE_ROOT}/src/qcommon/sys_process.h")
set(_linux_source_path
    "${SOURCE_ROOT}/src/_platform/posix/sys_process.cpp")
set(_macos_mach_source_path
    "${SOURCE_ROOT}/src/_platform/macos/sys_mach_crash.cpp")
set(_win32_source_path
    "${SOURCE_ROOT}/src/_platform/win32/sys_process.cpp")
set(_linux_platform_cmake
    "${SOURCE_ROOT}/scripts/platform/linux/platform.cmake")
set(_macos_platform_cmake
    "${SOURCE_ROOT}/scripts/platform/macos/platform.cmake")
set(_win32_platform_cmake
    "${SOURCE_ROOT}/scripts/platform/win32/platform.cmake")
set(_process_tests_path
    "${SOURCE_ROOT}/tests/platform_process_tests.cpp")
set(_crash_tests_path
    "${SOURCE_ROOT}/tests/platform_crash_tests.cpp")
set(_tests_cmake_path "${SOURCE_ROOT}/tests/CMakeLists.txt")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_linux_source_path}"
    "${_macos_mach_source_path}"
    "${_win32_source_path}"
    "${_linux_platform_cmake}"
    "${_macos_platform_cmake}"
    "${_win32_platform_cmake}"
    "${_process_tests_path}"
    "${_crash_tests_path}"
    "${_tests_cmake_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing platform-process source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_linux_source_path}" _linux_source)
file(READ "${_macos_mach_source_path}" _macos_mach_source)
file(READ "${_win32_source_path}" _win32_source)
file(READ "${_linux_platform_cmake}" _linux_platform)
file(READ "${_macos_platform_cmake}" _macos_platform)
file(READ "${_win32_platform_cmake}" _win32_platform)
file(READ "${_process_tests_path}" _process_tests)
file(READ "${_crash_tests_path}" _crash_tests)
file(READ "${_tests_cmake_path}" _tests_cmake)

foreach(_var IN ITEMS
    _header
    _linux_source
    _macos_mach_source
    _win32_source
    _linux_platform
    _macos_platform
    _win32_platform
    _process_tests
    _crash_tests
    _tests_cmake)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing platform-process invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(forbid_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden platform-process regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

# Public header must expose the portable process API and must not leak any
# native handle type or library include.
foreach(_public_contract IN ITEMS
    "enum class SysProcessLaunchStatus"
    "enum class SysProcessWaitStatus"
    "enum class SysProcessTerminateStatus"
    "enum class SysSignalParkStatus"
    "enum class SysProcessFreezeStatus"
    "Sys_ProcessLaunch("
    "Sys_ProcessWait("
    "Sys_ProcessTerminate("
    "Sys_ProcessGetExitCode("
    "Sys_ProcessRelease("
    "Sys_SignalPark("
    "Sys_SignalUnPark("
    "Sys_ProcessFreezeForCrash(")
    require_contains(
        _header
        "${_public_contract}"
        "public API token ${_public_contract}")
endforeach()

foreach(_native_token IN ITEMS
    "Windows.h"
    "HANDLE"
    "DWORD"
    "pid_t"
    "fork("
    "execve("
    "posix_spawn("
    "task_for_pid("
    "thread_suspend(")
    forbid_contains(
        _header
        "${_native_token}"
        "native token ${_native_token} leaked into the public header")
endforeach()

# POSIX backend must use posix_spawnp + waitpid + pthread_sigmask and must
# delegate the macOS Mach freeze to the dedicated TU.
foreach(_posix_contract IN ITEMS
    "posix_spawnp("
    "waitpid("
    "pthread_sigmask("
    "Sys_SignalPark"
    "Sys_SignalUnPark"
    "Sys_ProcessFreezeForCrash")
    require_contains(
        _linux_source
        "${_posix_contract}"
        "posix backend contract ${_posix_contract}")
endforeach()

require_contains(
    _linux_source
    "__APPLE__"
    "posix backend contract must gate mach freeze on __APPLE__")
require_contains(
    _linux_source
    "Sys_MachProcessFreezeForCrash"
    "posix backend must delegate macOS freeze to dedicated TU")

# macOS Mach TU must install exception ports and park the calling thread.
foreach(_macos_contract IN ITEMS
    "task_set_exception_ports"
    "thread_suspend("
    "sigsuspend("
    "Sys_MachProcessFreezeForCrash")
    require_contains(
        _macos_mach_source
        "${_macos_contract}"
        "macos Mach backend contract ${_macos_contract}")
endforeach()

# Win32 backend must wrap CreateProcessW + WaitForSingleObject + TerminateProcess
# and must explicitly mark signal-park and Mach freeze as Unsupported.
foreach(_win32_contract IN ITEMS
    "CreateProcessW("
    "WaitForSingleObject("
    "TerminateProcess("
    "SysSignalParkStatus::Unsupported"
    "SysProcessFreezeStatus::Unsupported")
    require_contains(
        _win32_source
        "${_win32_contract}"
        "win32 backend contract ${_win32_contract}")
endforeach()

# All three platform.cmake files must list sys_process.cpp in their service
# source set; macOS must also include sys_mach_crash.cpp.
foreach(_cmake_contract IN ITEMS
    "sys_process.cpp")
    require_contains(
        _linux_platform
        "${_cmake_contract}"
        "linux platform.cmake missing ${_cmake_contract}")
    require_contains(
        _macos_platform
        "${_cmake_contract}"
        "macos platform.cmake missing ${_cmake_contract}")
    require_contains(
        _win32_platform
        "${_cmake_contract}"
        "win32 platform.cmake missing ${_cmake_contract}")
endforeach()
require_contains(
    _macos_platform
    "sys_mach_crash.cpp"
    "macos platform.cmake missing Mach crash TU")

# Tests/CMakeLists.txt must build both targets and register their ctest
# entries; tests themselves must include the sys_process public header.
foreach(_cmake_test_contract IN ITEMS
    "kisakcod-platform-process-tests"
    "kisakcod-platform-crash-tests"
    "platform-process-launch-contracts"
    "platform-crash-freeze-contracts")
    require_contains(
        _tests_cmake
        "${_cmake_test_contract}"
        "tests CMakeLists missing ${_cmake_test_contract}")
endforeach()

require_contains(
    _process_tests
    "sys_process.h"
    "platform_process_tests.cpp missing public header include")
require_contains(
    _crash_tests
    "sys_process.h"
    "platform_crash_tests.cpp missing public header include")

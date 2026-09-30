# Platform service and headless-profile tests.
# Included from tests/CMakeLists.txt.

add_executable(kisakcod-worker-queue-atomic-tests
    worker_queue_atomic_tests.cpp
)
target_include_directories(kisakcod-worker-queue-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-worker-queue-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-worker-queue-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-worker-queue-atomic-tests)
set_target_properties(kisakcod-worker-queue-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME worker-queue-atomic-protocols
    COMMAND kisakcod-worker-queue-atomic-tests
)
set_tests_properties(worker-queue-atomic-protocols PROPERTIES TIMEOUT 30)

foreach(_profile mp sp)
    set(_target "kisakcod-platform-service-${_profile}-tests")
    string(TOUPPER "${_profile}" _profile_upper)
    add_executable(${_target} platform_service_contract_tests.cpp)
    target_include_directories(${_target} PRIVATE ${SRC_DIR})
    target_compile_features(${_target} PRIVATE cxx_std_20)
    target_compile_definitions(${_target} PRIVATE "KISAK_${_profile_upper}")
    kisakcod_test_warnings(${_target})
    set_target_properties(${_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(
        NAME "platform-service-${_profile}-contracts"
        COMMAND ${_target}
    )
endforeach()

set(_platform_console_sources ${KISAK_PLATFORM_SERVICE_SOURCES})
list(FILTER _platform_console_sources INCLUDE REGEX "[/\\\\]sys_console\\.cpp$")
list(LENGTH _platform_console_sources _platform_console_source_count)
if (NOT _platform_console_source_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one selected platform sys_console.cpp, found "
        "${_platform_console_source_count}")
endif()

add_executable(kisakcod-platform-console-tests
    platform_console_tests.cpp
    ${SRC_DIR}/qcommon/sys_console.cpp
    ${_platform_console_sources}
)
target_include_directories(kisakcod-platform-console-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-console-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-platform-console-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-platform-console-tests)
set_target_properties(kisakcod-platform-console-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-console-runtime-contracts
    COMMAND kisakcod-platform-console-tests
)
add_test(
    NAME platform-console-invalid-eof-contracts
    COMMAND kisakcod-platform-console-tests --invalid-eof
)
set(_platform_console_runtime_tests
    platform-console-runtime-contracts
    platform-console-invalid-eof-contracts
)
if (NOT KISAK_PLATFORM STREQUAL "win32")
    add_test(
        NAME platform-console-sigpipe-default-contracts
        COMMAND kisakcod-platform-console-tests --sigpipe-default
    )
    add_test(
        NAME platform-console-sigpipe-ignore-contracts
        COMMAND kisakcod-platform-console-tests --sigpipe-ignore
    )
    list(APPEND _platform_console_runtime_tests
        platform-console-sigpipe-default-contracts
        platform-console-sigpipe-ignore-contracts
    )
endif()
set_tests_properties(${_platform_console_runtime_tests} PROPERTIES TIMEOUT 20)

add_executable(kisakcod-platform-service-runtime-tests
    db_script_string_adapter_tests.cpp
    platform_service_runtime_tests.cpp
    ${SRC_DIR}/database/db_script_string_adapter.cpp
    ${SRC_DIR}/database/db_script_string_journal.cpp
    ${SRC_DIR}/database/db_script_string_transaction.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${SRC_DIR}/qcommon/sys_worker_gate.cpp
    ${KISAK_PLATFORM_SERVICE_SOURCES}
)
target_include_directories(kisakcod-platform-service-runtime-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-service-runtime-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-platform-service-runtime-tests PRIVATE KISAK_MP)
target_link_libraries(kisakcod-platform-service-runtime-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(
        kisakcod-platform-service-runtime-tests PRIVATE winmm ws2_32)
endif()
kisakcod_test_warnings(kisakcod-platform-service-runtime-tests)
set_target_properties(kisakcod-platform-service-runtime-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-service-runtime-contracts
    COMMAND kisakcod-platform-service-runtime-tests
)
set_tests_properties(platform-service-runtime-contracts PROPERTIES TIMEOUT 20)

add_executable(kisakcod-worker-thread-lifecycle-tests
    worker_thread_lifecycle_tests.cpp
    ${SRC_DIR}/qcommon/threads.cpp
    ${SRC_DIR}/qcommon/sys_worker_gate.cpp
    ${KISAK_PLATFORM_SERVICE_SOURCES}
)
target_include_directories(kisakcod-worker-thread-lifecycle-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-worker-thread-lifecycle-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-worker-thread-lifecycle-tests PRIVATE KISAK_MP KISAK_DEDI_HEADLESS)
target_link_libraries(kisakcod-worker-thread-lifecycle-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(kisakcod-worker-thread-lifecycle-tests PRIVATE winmm ws2_32)
endif()
kisakcod_test_warnings(kisakcod-worker-thread-lifecycle-tests)
set_target_properties(kisakcod-worker-thread-lifecycle-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME worker-thread-lifecycle-shutdown-reinit
    COMMAND kisakcod-worker-thread-lifecycle-tests
)
set_tests_properties(worker-thread-lifecycle-shutdown-reinit PROPERTIES TIMEOUT 120)

set(_platform_memory_sources ${KISAK_PLATFORM_SERVICE_SOURCES})
list(FILTER _platform_memory_sources INCLUDE REGEX "[/\\\\]sys_memory\\.cpp$")
list(LENGTH _platform_memory_sources _platform_memory_source_count)
if (NOT _platform_memory_source_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one selected platform sys_memory.cpp, found "
        "${_platform_memory_source_count}")
endif()

add_executable(kisakcod-platform-memory-tests
    platform_memory_tests.cpp
    ${_platform_memory_sources}
)
target_include_directories(kisakcod-platform-memory-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-memory-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-platform-memory-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-platform-memory-tests)
set_target_properties(kisakcod-platform-memory-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-virtual-memory-contracts
    COMMAND kisakcod-platform-memory-tests
)
set_tests_properties(platform-virtual-memory-contracts PROPERTIES TIMEOUT 20)

set(_platform_file_sources ${KISAK_PLATFORM_SERVICE_SOURCES})
list(FILTER _platform_file_sources INCLUDE REGEX "[/\\\\]sys_file\\.cpp$")
list(LENGTH _platform_file_sources _platform_file_source_count)
if (NOT _platform_file_source_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one selected platform sys_file.cpp, found "
        "${_platform_file_source_count}")
endif()

add_executable(kisakcod-platform-file-tests
    platform_file_tests.cpp
    ${_platform_file_sources}
)
target_include_directories(kisakcod-platform-file-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-file-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-platform-file-tests)
set_target_properties(kisakcod-platform-file-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-file-read-contracts
    COMMAND kisakcod-platform-file-tests
)
set_tests_properties(platform-file-read-contracts PROPERTIES TIMEOUT 20)

set(_platform_filesystem_sources ${KISAK_PLATFORM_SERVICE_SOURCES})
list(FILTER _platform_filesystem_sources INCLUDE REGEX "[/\\\\]sys_filesystem\\.cpp$")
list(LENGTH _platform_filesystem_sources _platform_filesystem_source_count)
if (NOT _platform_filesystem_source_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one selected platform sys_filesystem.cpp, found "
        "${_platform_filesystem_source_count}")
endif()

add_executable(kisakcod-platform-filesystem-tests
    platform_filesystem_tests.cpp
    ${_platform_filesystem_sources}
)
target_include_directories(kisakcod-platform-filesystem-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-filesystem-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-platform-filesystem-tests PRIVATE KISAK_FILESYSTEM_TEST_HOOKS=1)
kisakcod_test_warnings(kisakcod-platform-filesystem-tests)
set_target_properties(kisakcod-platform-filesystem-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-filesystem-path-contracts
    COMMAND kisakcod-platform-filesystem-tests
)
# The timeout must exceed the deletion-failure probe's bounded 30s wait
# (WaitForPathGone in platform_filesystem_tests.cpp): that wait only
# exhausts on the walk-fails-before-the-signal path — the very failure
# this probe exists to report — so the diagnostic must land before CTest
# kills the process. The passing path finishes in seconds; 90 keeps
# several multiples of the worst-case wait plus fixture overhead while
# still bounding a wedged run.
set_tests_properties(platform-filesystem-path-contracts PROPERTIES TIMEOUT 90)

# Sys_RemoveDirTree, the engine entry point (universal/win_common.cpp) over
# the service above; Com_DeletePlayerProfile calls it (NOW row 13). Linux and
# clang only: win_common.cpp is an engine TU (see the dvar test in misc.cmake).
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(kisakcod-win-common-remove-tree-tests
        win_common_remove_tree_tests.cpp
        ${SRC_DIR}/universal/win_common.cpp
        ${_platform_filesystem_sources}
    )
    target_include_directories(kisakcod-win-common-remove-tree-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-win-common-remove-tree-tests PRIVATE cxx_std_20)
    target_compile_definitions(kisakcod-win-common-remove-tree-tests PRIVATE KISAK_MP)
    target_compile_options(kisakcod-win-common-remove-tree-tests PRIVATE -fms-extensions)
    set_target_properties(kisakcod-win-common-remove-tree-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(NAME win-common-remove-tree-contracts COMMAND kisakcod-win-common-remove-tree-tests)
    set_tests_properties(win-common-remove-tree-contracts PROPERTIES TIMEOUT 20)
endif()

# fuzz_sys_filesystem: production-path fuzz fixture for the no-follow
# file read service. Links the same selected platform sys_filesystem.cpp
# as the contract tests above; named 'fuzz|filesystem' so the ctest
# named-regex filters in CI pick it up with the other fuzz entries.
add_executable(fuzz_sys_filesystem
    fuzz_sys_filesystem.cpp
    ${_platform_filesystem_sources}
)
target_include_directories(fuzz_sys_filesystem PRIVATE ${SRC_DIR})
target_compile_features(fuzz_sys_filesystem PRIVATE cxx_std_20)
kisakcod_test_warnings(fuzz_sys_filesystem)
set_target_properties(fuzz_sys_filesystem PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME fuzz-sys-filesystem-seeds
    COMMAND fuzz_sys_filesystem seeds)
set_tests_properties(fuzz-sys-filesystem-seeds PROPERTIES TIMEOUT 120)
add_test(
    NAME fuzz-sys-filesystem-random
    COMMAND fuzz_sys_filesystem random 20000)
set_tests_properties(fuzz-sys-filesystem-random PROPERTIES TIMEOUT 120)

# sys_local portable-surface contracts (the win_local.h split): the portable
# header must parse and keep its declarations on every host, and the moved
# declarations must still match the production implementations. The engine
# TUs (win_common.cpp plus the platform filesystem service it drives) compile
# at the engine's own warning level in a dedicated object library -- the
# decompiled engine headers are not -Wall clean (inline helpers in
# com_memory.h) -- and target-level options keep that downgrade scoped to
# exactly these sources, mirroring kisakcod-net-chan-production-objects in
# net.cmake. The strict set stays on the test's own TUs.
add_library(kisakcod-sys-local-engine-objects OBJECT
    ${SRC_DIR}/universal/win_common.cpp
    ${_platform_filesystem_sources}
)
target_include_directories(
    kisakcod-sys-local-engine-objects SYSTEM PUBLIC ${SRC_DIR} ${DEPS_DIR})
target_compile_features(
    kisakcod-sys-local-engine-objects PUBLIC cxx_std_20)
target_compile_definitions(
    kisakcod-sys-local-engine-objects PUBLIC KISAK_MP KISAK_DEDI_HEADLESS)
target_compile_options(kisakcod-sys-local-engine-objects PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)
add_executable(kisakcod-sys-local-contract-tests
    sys_local_contract_tests.cpp
    sys_local_test_stubs.cpp
)
target_include_directories(
    kisakcod-sys-local-contract-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-sys-local-contract-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-sys-local-contract-tests PRIVATE KISAK_MP KISAK_DEDI_HEADLESS)
target_link_libraries(kisakcod-sys-local-contract-tests PRIVATE
    kisakcod-sys-local-engine-objects Threads::Threads)
kisakcod_test_warnings(kisakcod-sys-local-contract-tests)
set_target_properties(kisakcod-sys-local-contract-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME sys-local-portable-contracts
    COMMAND kisakcod-sys-local-contract-tests
)
set_tests_properties(sys-local-portable-contracts PROPERTIES TIMEOUT 20)

set(_platform_process_sources ${KISAK_PLATFORM_SERVICE_SOURCES})
list(FILTER _platform_process_sources INCLUDE REGEX "[/\\\\]sys_process\\.cpp$")
if (KISAK_PLATFORM STREQUAL "macos")
    list(FILTER _platform_process_sources INCLUDE REGEX "posix/sys_process\\.cpp$")
endif()
list(LENGTH _platform_process_sources _platform_process_source_count)
if (NOT _platform_process_source_count EQUAL 1)
    message(FATAL_ERROR
        "Expected exactly one selected platform sys_process.cpp, found "
        "${_platform_process_source_count}")
endif()

set(_platform_process_mach_sources "")
if (KISAK_PLATFORM STREQUAL "macos")
    set(_platform_process_mach_sources ${KISAK_PLATFORM_SERVICE_SOURCES})
    list(FILTER _platform_process_mach_sources INCLUDE REGEX
        "[/\\\\]sys_mach_crash\\.cpp$")
    list(LENGTH _platform_process_mach_sources
        _platform_process_mach_source_count)
    if (NOT _platform_process_mach_source_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one selected macOS sys_mach_crash.cpp, found "
            "${_platform_process_mach_source_count}")
    endif()
endif()

add_executable(kisakcod-platform-process-tests
    platform_process_tests.cpp
    ${_platform_process_sources}
    ${_platform_process_mach_sources}
)
target_include_directories(kisakcod-platform-process-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-process-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-platform-process-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(kisakcod-platform-process-tests PRIVATE winmm)
endif()
kisakcod_test_warnings(kisakcod-platform-process-tests)
set_target_properties(kisakcod-platform-process-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-process-launch-contracts
    COMMAND kisakcod-platform-process-tests
)
set_tests_properties(platform-process-launch-contracts PROPERTIES TIMEOUT 60)
add_test(
    NAME platform-process-signal-park-contracts
    COMMAND kisakcod-platform-process-tests signal-park-lifecycle
)
set_tests_properties(platform-process-signal-park-contracts PROPERTIES TIMEOUT 20)
add_test(
    NAME platform-process-freeze-contracts
    COMMAND kisakcod-platform-process-tests freeze-unsupported
)
set_tests_properties(platform-process-freeze-contracts PROPERTIES TIMEOUT 20)

add_executable(kisakcod-platform-socket-tests
    platform_socket_tests.cpp
    ${_platform_socket_sources}
)
target_include_directories(kisakcod-platform-socket-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-socket-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-platform-socket-tests PRIVATE KISAK_SOCKET_TEST_HOOKS=1)
target_link_libraries(kisakcod-platform-socket-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(kisakcod-platform-socket-tests PRIVATE ws2_32)
endif()
kisakcod_test_warnings(kisakcod-platform-socket-tests)
set_target_properties(kisakcod-platform-socket-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-socket-contracts
    COMMAND kisakcod-platform-socket-tests
)
set_tests_properties(platform-socket-contracts PROPERTIES TIMEOUT 30)

add_executable(kisakcod-platform-socket-stream-tests
    platform_socket_stream_harness.cpp
    platform_socket_stream_tests.cpp
    ${_platform_socket_sources}
)
target_include_directories(kisakcod-platform-socket-stream-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-socket-stream-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-platform-socket-stream-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(kisakcod-platform-socket-stream-tests PRIVATE ws2_32)
endif()
kisakcod_test_warnings(kisakcod-platform-socket-stream-tests)
set_target_properties(kisakcod-platform-socket-stream-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-socket-stream-contracts
    COMMAND kisakcod-platform-socket-stream-tests
)
set_tests_properties(platform-socket-stream-contracts PROPERTIES TIMEOUT 30)

add_executable(kisakcod-platform-crash-tests
    platform_crash_tests.cpp
    ${_platform_process_sources}
    ${_platform_process_mach_sources}
)
target_include_directories(kisakcod-platform-crash-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-crash-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-platform-crash-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(kisakcod-platform-crash-tests PRIVATE winmm)
endif()
kisakcod_test_warnings(kisakcod-platform-crash-tests)
set_target_properties(kisakcod-platform-crash-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-crash-freeze-contracts
    COMMAND kisakcod-platform-crash-tests
)
set_tests_properties(platform-crash-freeze-contracts PROPERTIES TIMEOUT 15)
add_test(
    NAME platform-crash-signal-park-contracts
    COMMAND kisakcod-platform-crash-tests signal-park-disjoint-stacking
)
set_tests_properties(platform-crash-signal-park-contracts PROPERTIES TIMEOUT 15)

add_test(
    NAME dedi-headless-client-media-include-debt
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/headless_include_debt.allow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_include_debt_test.cmake
)

kisakcod_ilp32(kisakcod-platform-console-tests
    platform-console-invalid-eof-contracts
    platform-console-runtime-contracts)

kisakcod_ilp32(kisakcod-platform-file-tests
    platform-file-read-contracts)

kisakcod_ilp32(kisakcod-platform-service-runtime-tests
    platform-service-runtime-contracts)

# POSIX termios console contracts (NOW row 13): the text cleaner and the
# engine output entry point of _platform/posix/posix_syscon.cpp, linked with
# the portable console boundary and its POSIX backend, the same set the
# headless dedicated build links.
if (NOT KISAK_PLATFORM STREQUAL "win32")
    set(_posix_syscon_platform_console ${KISAK_PLATFORM_SERVICE_SOURCES})
    list(FILTER _posix_syscon_platform_console INCLUDE REGEX "[/\\\\]sys_console\\.cpp$")
    list(FILTER _posix_syscon_platform_console EXCLUDE REGEX "qcommon")
    add_executable(kisakcod-posix-syscon-tests
        posix_syscon_tests.cpp
        ${SRC_DIR}/_platform/posix/posix_syscon.cpp
        ${SRC_DIR}/qcommon/sys_console.cpp
        ${_posix_syscon_platform_console}
    )
    target_include_directories(kisakcod-posix-syscon-tests PRIVATE ${SRC_DIR})
    target_compile_features(kisakcod-posix-syscon-tests PRIVATE cxx_std_20)
    target_compile_definitions(kisakcod-posix-syscon-tests PRIVATE KISAK_MP)
    target_link_libraries(kisakcod-posix-syscon-tests PRIVATE Threads::Threads)
    kisakcod_test_warnings(kisakcod-posix-syscon-tests)
    set_target_properties(kisakcod-posix-syscon-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(NAME posix-syscon-contracts COMMAND kisakcod-posix-syscon-tests)
    set_tests_properties(posix-syscon-contracts PROPERTIES TIMEOUT 20)
endif()

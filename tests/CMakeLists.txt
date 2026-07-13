function(kisakcod_test_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# Utility-only configurations do not enter an engine subdirectory, so select
# the host platform source sets here before compiling the service backends.
include("${SCRIPTS_DIR}/platform_override.cmake")
include("${SCRIPTS_DIR}/common_files.cmake")
include("${PLATFORM_DIR}/${KISAK_PLATFORM}/platform.cmake")
kisakcod_require_platform_source_sets()
find_package(Threads REQUIRED)

add_executable(kisakcod-huffman-tests
    huffman_tests.cpp
    ${SRC_DIR}/qcommon/huffman.cpp
)
target_include_directories(kisakcod-huffman-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-huffman-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-huffman-tests)
set_target_properties(kisakcod-huffman-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME huffman-bounds-and-pointer-width COMMAND kisakcod-huffman-tests)

add_executable(kisakcod-disk32-tests disk32_tests.cpp)
target_include_directories(kisakcod-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-disk32-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-disk32-tests)
set_target_properties(kisakcod-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME disk32-pointer-token-bounds COMMAND kisakcod-disk32-tests)

add_executable(kisakcod-db-relocation-tests
    db_relocation_tests.cpp
    ${SRC_DIR}/database/db_relocation.cpp
)
target_include_directories(kisakcod-db-relocation-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-db-relocation-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-relocation-tests)
set_target_properties(kisakcod-db-relocation-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-relocation-alias-provenance
    COMMAND kisakcod-db-relocation-tests
)

add_executable(kisakcod-identity-tests identity_tests.cpp)
target_include_directories(kisakcod-identity-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-identity-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-identity-tests)
set_target_properties(kisakcod-identity-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME identity-format-and-overflow COMMAND kisakcod-identity-tests)

add_executable(kisakcod-db-validation-tests db_validation_tests.cpp)
target_include_directories(kisakcod-db-validation-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-db-validation-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-validation-tests)
set_target_properties(kisakcod-db-validation-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME database-checked-arithmetic COMMAND kisakcod-db-validation-tests)

add_executable(kisakcod-db-asset-mode-tests db_asset_mode_tests.cpp)
target_include_directories(kisakcod-db-asset-mode-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-db-asset-mode-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-asset-mode-tests)
set_target_properties(kisakcod-db-asset-mode-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME database-build-mode-asset-policy COMMAND kisakcod-db-asset-mode-tests)

add_executable(kisakcod-abi-atomics-tests abi_atomics_tests.cpp)
target_include_directories(kisakcod-abi-atomics-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-abi-atomics-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-abi-atomics-tests)
set_target_properties(kisakcod-abi-atomics-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME abi-atomics-and-layout-macros COMMAND kisakcod-abi-atomics-tests)

add_executable(kisakcod-fx-atomic-layout-tests
    fx_atomic_layout_tests.cpp
)
target_include_directories(kisakcod-fx-atomic-layout-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-atomic-layout-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-atomic-layout-tests)
set_target_properties(kisakcod-fx-atomic-layout-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fixed-width-atomic-layouts
    COMMAND kisakcod-fx-atomic-layout-tests
)

add_executable(kisakcod-fx-runtime-blob-tests
    fx_runtime_blob_tests.cpp
)
target_include_directories(kisakcod-fx-runtime-blob-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-runtime-blob-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-runtime-blob-tests)
set_target_properties(kisakcod-fx-runtime-blob-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-runtime-blob-layout
    COMMAND kisakcod-fx-runtime-blob-tests
)

add_executable(kisakcod-fx-visibility-atomic-tests
    fx_visibility_atomic_tests.cpp
)
target_include_directories(kisakcod-fx-visibility-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-visibility-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-fx-visibility-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-visibility-atomic-tests)
set_target_properties(kisakcod-fx-visibility-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-visibility-publication
    COMMAND kisakcod-fx-visibility-atomic-tests
)
set_tests_properties(effectscore-visibility-publication PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-load-atomic-tests db_load_atomic_tests.cpp)
target_include_directories(kisakcod-db-load-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-db-load-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-db-load-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-db-load-atomic-tests)
set_target_properties(kisakcod-db-load-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-load-atomic-protocols
    COMMAND kisakcod-db-load-atomic-tests
)
set_tests_properties(database-load-atomic-protocols PROPERTIES TIMEOUT 20)

add_executable(kisakcod-skel-memory-atomic-tests
    skel_memory_atomic_tests.cpp
)
target_include_directories(kisakcod-skel-memory-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-skel-memory-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-skel-memory-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-skel-memory-atomic-tests)
set_target_properties(kisakcod-skel-memory-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME skeleton-memory-atomic-protocols
    COMMAND kisakcod-skel-memory-atomic-tests
)
set_tests_properties(skeleton-memory-atomic-protocols PROPERTIES TIMEOUT 20)

add_executable(kisakcod-cg-pose-atomic-tests
    cg_pose_atomic_tests.cpp
)
target_include_directories(kisakcod-cg-pose-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-cg-pose-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-cg-pose-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-cg-pose-atomic-tests)
set_target_properties(kisakcod-cg-pose-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME cgame-pose-atomic-protocols
    COMMAND kisakcod-cg-pose-atomic-tests
)
set_tests_properties(cgame-pose-atomic-protocols PROPERTIES TIMEOUT 20)

add_executable(kisakcod-renderer-reservation-atomic-tests
    renderer_reservation_atomic_tests.cpp
)
target_include_directories(kisakcod-renderer-reservation-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-renderer-reservation-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-renderer-reservation-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-renderer-reservation-atomic-tests)
set_target_properties(kisakcod-renderer-reservation-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME renderer-bounded-reservation-protocols
    COMMAND kisakcod-renderer-reservation-atomic-tests
)
set_tests_properties(renderer-bounded-reservation-protocols PROPERTIES TIMEOUT 20)

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

add_executable(kisakcod-model-surface-stream-tests
    model_surface_stream_tests.cpp
)
target_include_directories(kisakcod-model-surface-stream-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-model-surface-stream-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-model-surface-stream-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-model-surface-stream-tests)
set_target_properties(kisakcod-model-surface-stream-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME renderer-model-surface-stream-contracts
    COMMAND kisakcod-model-surface-stream-tests
)
set_tests_properties(renderer-model-surface-stream-contracts PROPERTIES TIMEOUT 20)

add_executable(kisakcod-script-string-atomic-tests
    script_string_atomic_tests.cpp
)
target_include_directories(kisakcod-script-string-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-script-string-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-script-string-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-script-string-atomic-tests)
set_target_properties(kisakcod-script-string-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME script-string-packed-atomic-contracts
    COMMAND kisakcod-script-string-atomic-tests
)
set_tests_properties(script-string-packed-atomic-contracts PROPERTIES TIMEOUT 20)

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

add_executable(kisakcod-platform-service-runtime-tests
    platform_service_runtime_tests.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${SRC_DIR}/qcommon/sys_worker_gate.cpp
    ${KISAK_PLATFORM_SERVICE_SOURCES}
)
target_include_directories(kisakcod-platform-service-runtime-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-platform-service-runtime-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-platform-service-runtime-tests PRIVATE KISAK_MP)
target_link_libraries(kisakcod-platform-service-runtime-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(kisakcod-platform-service-runtime-tests PRIVATE winmm)
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
kisakcod_test_warnings(kisakcod-platform-filesystem-tests)
set_target_properties(kisakcod-platform-filesystem-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME platform-filesystem-path-contracts
    COMMAND kisakcod-platform-filesystem-tests
)
set_tests_properties(platform-filesystem-path-contracts PROPERTIES TIMEOUT 20)

add_test(
    NAME dedi-headless-source-profile
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_profile_test.cmake
)

add_test(
    NAME dedi-headless-client-media-include-debt
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/headless_include_debt.allow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/headless_include_debt_test.cmake
)

add_test(
    NAME pointer-truncation-tripwire
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/pointer_truncation.allow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/pointer_truncation_test.cmake
)

add_test(
    NAME abi-sizeof-debt-tripwire
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -DALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/abi_sizeof_debt.allow
        -DFORMULA_ALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/abi_sizeof_formula_debt.allow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/abi_sizeof_debt_test.cmake
)

add_test(
    NAME abi-sizeof-scanner-fixture
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/abi_sizeof
        -DALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/abi_sizeof/direct.allow
        -DFORMULA_ALLOWLIST=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/abi_sizeof/formula.allow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/abi_sizeof_debt_test.cmake
)

add_test(
    NAME security-source-regressions
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/security_regression_test.cmake
)

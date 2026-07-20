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

add_executable(kisakcod-actor-grenade-prediction-cache-tests
    actor_grenade_prediction_cache_tests.cpp
)
target_include_directories(
    kisakcod-actor-grenade-prediction-cache-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-actor-grenade-prediction-cache-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-actor-grenade-prediction-cache-tests)
set_target_properties(
    kisakcod-actor-grenade-prediction-cache-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME actor-grenade-prediction-cache-contracts
    COMMAND kisakcod-actor-grenade-prediction-cache-tests
)

add_executable(kisakcod-actor-grenade-safety-tests
    actor_grenade_safety_tests.cpp
)
target_include_directories(
    kisakcod-actor-grenade-safety-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-actor-grenade-safety-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-actor-grenade-safety-tests)
set_target_properties(
    kisakcod-actor-grenade-safety-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME actor-grenade-safe-target-radius-contracts
    COMMAND kisakcod-actor-grenade-safety-tests
)

add_executable(kisakcod-target-table-tests
    target_table_tests.cpp
)
target_include_directories(
    kisakcod-target-table-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-target-table-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-target-table-tests)
set_target_properties(
    kisakcod-target-table-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME target-table-parse-and-layout-contracts
    COMMAND kisakcod-target-table-tests
)

foreach(_missile_variant IN ITEMS MP SP)
    string(TOLOWER "${_missile_variant}" _missile_variant_lower)
    set(_missile_layout_target
        "kisakcod-missile-layout-${_missile_variant_lower}-compile-tests")
    add_executable(${_missile_layout_target}
        missile_layout_compile_tests.cpp
    )
    target_include_directories(
        ${_missile_layout_target} SYSTEM PRIVATE ${SRC_DIR})
    target_compile_features(
        ${_missile_layout_target} PRIVATE cxx_std_20)
    target_compile_definitions(
        ${_missile_layout_target} PRIVATE "KISAK_${_missile_variant}")
    kisakcod_test_warnings(${_missile_layout_target})
    set_target_properties(
        ${_missile_layout_target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(
        NAME "missile-layout-${_missile_variant_lower}-compile-contracts"
        COMMAND ${_missile_layout_target}
    )
endforeach()

# Exercise the production MemoryFile reader instead of an extracted model.
# The inherited zlib 1.1.4 and memfile translation units predate the warning
# policy used by the portable tests, so isolate those legacy diagnostics while
# keeping the test harness itself warning-clean.
set(_kisakcod_memfile_zlib_sources
    ${DEPS_DIR}/zlib/adler32.c
    ${DEPS_DIR}/zlib/deflate.c
    ${DEPS_DIR}/zlib/trees.c
    ${DEPS_DIR}/zlib/zutil.c
    ${DEPS_DIR}/zlib/inflate.c
    ${DEPS_DIR}/zlib/infblock.c
    ${DEPS_DIR}/zlib/infcodes.c
    ${DEPS_DIR}/zlib/inffast.c
    ${DEPS_DIR}/zlib/inftrees.c
    ${DEPS_DIR}/zlib/infutil.c
)
add_library(kisakcod-memfile-test-subject STATIC
    ${SRC_DIR}/universal/memfile.cpp
    ${_kisakcod_memfile_zlib_sources}
)
target_include_directories(kisakcod-memfile-test-subject PRIVATE
    ${SRC_DIR}
    ${DEPS_DIR}
)
target_compile_features(kisakcod-memfile-test-subject PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-memfile-test-subject PRIVATE KISAK_MP)
if (MSVC)
    target_compile_options(kisakcod-memfile-test-subject PRIVATE /W0)
else()
    target_compile_options(kisakcod-memfile-test-subject PRIVATE -w)
endif()

add_executable(kisakcod-memfile-tests memfile_tests.cpp)
target_include_directories(kisakcod-memfile-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-memfile-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-memfile-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-memfile-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(kisakcod-memfile-tests)
set_target_properties(kisakcod-memfile-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME universal-memfile-bounded-read
    COMMAND kisakcod-memfile-tests
)
set_tests_properties(universal-memfile-bounded-read PROPERTIES TIMEOUT 20)

function(kisakcod_fx_helper_stack_budget target)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _stack_build_type_upper)
    set(_stack_active_cxx_flags
        "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_stack_build_type_upper}}")
    if(NOT MSVC
        AND _stack_active_cxx_flags MATCHES "(^|[ ;])-fsanitize=")
        # Sanitizer redzones intentionally inflate otherwise bounded helper
        # frames. Runtime sanitizer fixtures still execute these paths; the
        # 4 KiB static budget is enforced on every production-style build.
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /analyze
            /analyze:autolog-
            "SHELL:/analyze:stacksize 4096"
            "/analyze:ruleset${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_stack.ruleset")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            -Wstack-usage=4096
            -fstack-usage)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        target_compile_options(${target} PRIVATE
            -Wframe-larger-than=4096
            -Walloca
            -fstack-usage)
    endif()
endfunction()

add_library(kisakcod-fx-effect-table-restore-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_effect_table_restore.cpp
)
target_include_directories(
    kisakcod-fx-effect-table-restore-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-restore-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-restore-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-restore-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-effect-table-restore-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-effect-table-restore-subject)

add_library(kisakcod-fx-effect-table-save-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_effect_table_save.cpp
)
target_include_directories(
    kisakcod-fx-effect-table-save-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-save-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-save-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-save-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-effect-table-save-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-effect-table-save-subject)

add_executable(kisakcod-fx-effect-table-restore-tests
    fx_effect_table_restore_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-effect-table-restore-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-restore-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-restore-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-restore-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-effect-table-restore-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-effect-table-restore-tests)
set_target_properties(kisakcod-fx-effect-table-restore-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-effect-table-transactional-restore
    COMMAND kisakcod-fx-effect-table-restore-tests
)
set_tests_properties(
    effectscore-effect-table-transactional-restore PROPERTIES TIMEOUT 30)

add_executable(kisakcod-fx-effect-table-save-tests
    fx_effect_table_save_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-save-subject>
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-effect-table-save-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-effect-table-save-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-effect-table-save-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-effect-table-save-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-effect-table-save-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-effect-table-save-tests)
set_target_properties(kisakcod-fx-effect-table-save-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-effect-table-bounded-save
    COMMAND kisakcod-fx-effect-table-save-tests
)
set_tests_properties(
    effectscore-effect-table-bounded-save PROPERTIES TIMEOUT 60)

string(TOUPPER "${CMAKE_BUILD_TYPE}" _kisakcod_build_type_upper)
set(_kisakcod_active_cxx_flags
    "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_kisakcod_build_type_upper}}")
if(NOT MSVC AND NOT _kisakcod_active_cxx_flags MATCHES "(^|[ ;])-fsanitize=")
    # Sanitizer instrumentation intentionally introduces dynamic helper frames.
    # Its runtime remains covered above with a bounded 1 MiB worker stack; the
    # static .su contract measures the production, non-instrumented objects.
    set(_kisakcod_fx_save_stack_report_dir
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/kisakcod-fx-effect-table-save-subject.dir")
    set(_kisakcod_fx_restore_stack_report_dir
        "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/kisakcod-fx-effect-table-restore-subject.dir")
    add_test(
        NAME effectscore-effect-table-stack-usage
        COMMAND ${CMAKE_COMMAND}
            "-DSTACK_USAGE_FILES=${_kisakcod_fx_save_stack_report_dir};${_kisakcod_fx_restore_stack_report_dir}"
            -DSTACK_USAGE_LIMIT_BYTES=4096
            -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_stack_usage_report_test.cmake
    )
endif()

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

add_executable(kisakcod-db-xasset-disk32-tests
    db_xasset_disk32_tests.cpp
    ${SRC_DIR}/database/db_xasset_disk32.cpp
)
target_include_directories(
    kisakcod-db-xasset-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-xasset-disk32-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-xasset-disk32-tests)
set_target_properties(kisakcod-db-xasset-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-xasset-disk32-envelope
    COMMAND kisakcod-db-xasset-disk32-tests
)
set_tests_properties(
    database-xasset-disk32-envelope PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-script-string-disk32-tests
    db_script_string_disk32_tests.cpp
    ${SRC_DIR}/database/db_xasset_disk32.cpp
)
target_include_directories(
    kisakcod-db-script-string-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-script-string-disk32-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-script-string-disk32-tests)
set_target_properties(
    kisakcod-db-script-string-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-script-string-disk32-walk
    COMMAND kisakcod-db-script-string-disk32-tests
)
set_tests_properties(
    database-script-string-disk32-walk PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-zone-load-context-tests
    db_zone_load_context_tests.cpp
    ${SRC_DIR}/database/db_zone_load_context.cpp
)
target_include_directories(
    kisakcod-db-zone-load-context-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-load-context-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-zone-load-context-tests)
set_target_properties(
    kisakcod-db-zone-load-context-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-load-context-lifecycle
    COMMAND kisakcod-db-zone-load-context-tests
)
set_tests_properties(
    database-zone-load-context-lifecycle PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-zone-stream-ownership-tests
    db_zone_stream_ownership_tests.cpp
    ${SRC_DIR}/database/db_zone_stream_ownership.cpp
    ${SRC_DIR}/database/db_zone_load_context.cpp
    ${SRC_DIR}/database/db_relocation.cpp
    ${SRC_DIR}/database/db_stream.cpp
)
target_include_directories(
    kisakcod-db-zone-stream-ownership-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-stream-ownership-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-zone-stream-ownership-tests)
set_target_properties(
    kisakcod-db-zone-stream-ownership-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-stream-ownership-runtime-contracts
    COMMAND kisakcod-db-zone-stream-ownership-tests
)
set_tests_properties(
    database-zone-stream-ownership-runtime-contracts PROPERTIES TIMEOUT 30)

add_executable(kisakcod-db-zone-stream-ownership-production-seal-tests
    db_zone_stream_ownership_production_seal_tests.cpp
)
target_include_directories(
    kisakcod-db-zone-stream-ownership-production-seal-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-stream-ownership-production-seal-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(
    kisakcod-db-zone-stream-ownership-production-seal-tests)
set_target_properties(
    kisakcod-db-zone-stream-ownership-production-seal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-stream-ownership-production-test-access-sealed
    COMMAND kisakcod-db-zone-stream-ownership-production-seal-tests
)

add_executable(kisakcod-db-zone-pending-copy-ledger-tests
    db_zone_pending_copy_ledger_tests.cpp
    ${SRC_DIR}/database/db_zone_pending_copy_ledger.cpp
    ${SRC_DIR}/database/db_zone_load_context.cpp
)
target_include_directories(
    kisakcod-db-zone-pending-copy-ledger-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-pending-copy-ledger-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-pending-copy-ledger-tests PRIVATE
    KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING=1)
kisakcod_test_warnings(kisakcod-db-zone-pending-copy-ledger-tests)
set_target_properties(
    kisakcod-db-zone-pending-copy-ledger-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-pending-copy-ledger
    COMMAND kisakcod-db-zone-pending-copy-ledger-tests
)
set_tests_properties(
    database-zone-pending-copy-ledger PROPERTIES TIMEOUT 30)

add_executable(kisakcod-db-zone-pending-copy-ledger-production-seal-tests
    db_zone_pending_copy_ledger_production_seal_tests.cpp
)
target_include_directories(
    kisakcod-db-zone-pending-copy-ledger-production-seal-tests
    PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-pending-copy-ledger-production-seal-tests
    PRIVATE cxx_std_20)
kisakcod_test_warnings(
    kisakcod-db-zone-pending-copy-ledger-production-seal-tests)
set_target_properties(
    kisakcod-db-zone-pending-copy-ledger-production-seal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-pending-copy-production-test-access-sealed
    COMMAND kisakcod-db-zone-pending-copy-ledger-production-seal-tests
)
set_tests_properties(
    database-zone-pending-copy-production-test-access-sealed
    PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-script-string-journal-tests
    db_script_string_journal_tests.cpp
    ${SRC_DIR}/database/db_script_string_journal.cpp
    ${SRC_DIR}/database/db_zone_load_context.cpp
)
target_include_directories(
    kisakcod-db-script-string-journal-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-script-string-journal-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-script-string-journal-tests PRIVATE
    KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING=1)
kisakcod_test_warnings(kisakcod-db-script-string-journal-tests)
set_target_properties(
    kisakcod-db-script-string-journal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-script-string-transaction-journal
    COMMAND kisakcod-db-script-string-journal-tests
)
set_tests_properties(
    database-script-string-transaction-journal PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-zone-script-string-ownership-tests
    db_zone_script_string_ownership_tests.cpp
    ${SRC_DIR}/database/db_zone_script_string_ownership.cpp
    ${SRC_DIR}/database/db_script_string_adapter.cpp
    ${SRC_DIR}/database/db_script_string_journal.cpp
    ${SRC_DIR}/database/db_script_string_transaction.cpp
    ${SRC_DIR}/database/db_zone_load_context.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${KISAK_PLATFORM_SERVICE_SOURCES}
)
target_include_directories(
    kisakcod-db-zone-script-string-ownership-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-script-string-ownership-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-script-string-ownership-tests PRIVATE
    KISAK_MP
    KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1)
target_link_libraries(
    kisakcod-db-zone-script-string-ownership-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(
        kisakcod-db-zone-script-string-ownership-tests PRIVATE winmm)
endif()
kisakcod_test_warnings(kisakcod-db-zone-script-string-ownership-tests)
set_target_properties(
    kisakcod-db-zone-script-string-ownership-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-script-string-ownership-controller
    COMMAND kisakcod-db-zone-script-string-ownership-tests
)
set_tests_properties(
    database-zone-script-string-ownership-controller PROPERTIES TIMEOUT 30)

add_executable(kisakcod-db-registry-ownership-coordinator-tests
    db_registry_ownership_coordinator_tests.cpp
    ${SRC_DIR}/database/db_registry_ownership_coordinator.cpp
)
target_include_directories(
    kisakcod-db-registry-ownership-coordinator-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-registry-ownership-coordinator-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-registry-ownership-coordinator-tests PRIVATE
    KISAK_MP
    KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING=1)
target_link_libraries(
    kisakcod-db-registry-ownership-coordinator-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-db-registry-ownership-coordinator-tests)
set_target_properties(
    kisakcod-db-registry-ownership-coordinator-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-registry-ownership-coordinator
    COMMAND kisakcod-db-registry-ownership-coordinator-tests
)
set_tests_properties(
    database-registry-ownership-coordinator PROPERTIES TIMEOUT 30)

add_executable(
    kisakcod-db-registry-ownership-coordinator-production-seal-tests
    db_registry_ownership_coordinator_production_seal_tests.cpp
)
target_include_directories(
    kisakcod-db-registry-ownership-coordinator-production-seal-tests
    SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-registry-ownership-coordinator-production-seal-tests
    PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-registry-ownership-coordinator-production-seal-tests
    PRIVATE KISAK_MP)
kisakcod_test_warnings(
    kisakcod-db-registry-ownership-coordinator-production-seal-tests)
set_target_properties(
    kisakcod-db-registry-ownership-coordinator-production-seal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-registry-ownership-production-test-access-sealed
    COMMAND
        kisakcod-db-registry-ownership-coordinator-production-seal-tests
)
set_tests_properties(
    database-registry-ownership-production-test-access-sealed
    PROPERTIES TIMEOUT 20)

# Compile the facade's production surface on every portable host without the
# test-access macro.  OBJECT keeps this as a compile-only gate while ensuring
# it remains part of the default build.
add_library(kisakcod-db-zone-runtime-facade-macro-off OBJECT
    ${SRC_DIR}/database/db_zone_runtime_facade.cpp)
target_include_directories(
    kisakcod-db-zone-runtime-facade-macro-off SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-facade-macro-off PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-facade-macro-off PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-db-zone-runtime-facade-macro-off)

add_executable(kisakcod-db-zone-runtime-facade-tests
    db_zone_runtime_facade_tests.cpp
    ${SRC_DIR}/database/db_zone_runtime_facade.cpp
)
target_include_directories(
    kisakcod-db-zone-runtime-facade-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-facade-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-facade-tests PRIVATE
    KISAK_MP
    KISAK_DB_ZONE_RUNTIME_FACADE_TESTING=1)
target_link_libraries(
    kisakcod-db-zone-runtime-facade-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-db-zone-runtime-facade-tests)
set_target_properties(
    kisakcod-db-zone-runtime-facade-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-zone-runtime-facade
    COMMAND kisakcod-db-zone-runtime-facade-tests)
set_tests_properties(database-zone-runtime-facade PROPERTIES TIMEOUT 30)

add_executable(kisakcod-db-zone-runtime-facade-production-seal-tests
    db_zone_runtime_facade_production_seal_tests.cpp)
target_include_directories(
    kisakcod-db-zone-runtime-facade-production-seal-tests
    SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-facade-production-seal-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-facade-production-seal-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(
    kisakcod-db-zone-runtime-facade-production-seal-tests)
set_target_properties(
    kisakcod-db-zone-runtime-facade-production-seal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-zone-runtime-facade-production-test-access-sealed
    COMMAND kisakcod-db-zone-runtime-facade-production-seal-tests)
set_tests_properties(
    database-zone-runtime-facade-production-test-access-sealed
    PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-zone-runtime-table-tests
    db_zone_runtime_table_tests.cpp
    ${SRC_DIR}/database/db_zone_runtime_table.cpp
    ${SRC_DIR}/database/db_zone_runtime_storage.cpp
    ${SRC_DIR}/database/db_zone_stream_ownership.cpp
    ${SRC_DIR}/database/db_zone_pending_copy_ledger.cpp
    ${SRC_DIR}/database/db_zone_script_string_ownership.cpp
    ${SRC_DIR}/database/db_script_string_adapter.cpp
    ${SRC_DIR}/database/db_script_string_journal.cpp
    ${SRC_DIR}/database/db_script_string_transaction.cpp
    ${SRC_DIR}/database/db_zone_load_context.cpp
    ${SRC_DIR}/database/db_relocation.cpp
    ${SRC_DIR}/database/db_stream.cpp
    ${SRC_DIR}/EffectsCore/fx_zone_runtime_storage_bridge.cpp
    ${SRC_DIR}/universal/physicalmemory.cpp
    ${SRC_DIR}/universal/physicalmemory_checked.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
    ${KISAK_PLATFORM_SERVICE_SOURCES}
)
target_include_directories(
    kisakcod-db-zone-runtime-table-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-table-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-table-tests PRIVATE
    KISAK_MP
    KISAK_DB_ZONE_LOAD_CONTEXT_TESTING=1
    KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1
    KISAK_DB_ZONE_RUNTIME_TABLE_TESTING=1)
target_link_libraries(
    kisakcod-db-zone-runtime-table-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(
        kisakcod-db-zone-runtime-table-tests PRIVATE winmm)
endif()
if (MSVC)
    # Several adversarial lifecycle tests intentionally retain multiple
    # durable tables in one frame. The passive receipt composition grows each
    # table to almost 64 KiB on Win32, so keep the fixture away from the
    # platform's small default test-process stack.
    target_link_options(
        kisakcod-db-zone-runtime-table-tests PRIVATE
        "LINKER:/STACK:8388608")
endif()
kisakcod_test_warnings(kisakcod-db-zone-runtime-table-tests)
set_target_properties(
    kisakcod-db-zone-runtime-table-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-runtime-table-ownership
    COMMAND kisakcod-db-zone-runtime-table-tests
)
set_tests_properties(
    database-zone-runtime-table-ownership PROPERTIES TIMEOUT 30)
foreach(_zone_runtime_unsafe_boundary RANGE 0 6)
    add_test(
        NAME database-zone-runtime-table-unload-unsafe-${_zone_runtime_unsafe_boundary}
        COMMAND kisakcod-db-zone-runtime-table-tests
            --unsafe-live-unload ${_zone_runtime_unsafe_boundary}
    )
    set_tests_properties(
        database-zone-runtime-table-unload-unsafe-${_zone_runtime_unsafe_boundary}
        PROPERTIES TIMEOUT 30)
endforeach()
foreach(_zone_runtime_mutation_unsafe_kind IN ITEMS backend postcondition)
    add_test(
        NAME database-zone-runtime-table-mutation-unsafe-${_zone_runtime_mutation_unsafe_kind}
        COMMAND kisakcod-db-zone-runtime-table-tests
            --unsafe-mutable ${_zone_runtime_mutation_unsafe_kind}
    )
    set_tests_properties(
        database-zone-runtime-table-mutation-unsafe-${_zone_runtime_mutation_unsafe_kind}
        PROPERTIES TIMEOUT 30)
endforeach()
add_test(
    NAME database-zone-runtime-table-unload-invalid-missing-value
    COMMAND kisakcod-db-zone-runtime-table-tests --unsafe-live-unload)
add_test(
    NAME database-zone-runtime-table-unload-invalid-extra-value
    COMMAND kisakcod-db-zone-runtime-table-tests
        --unsafe-live-unload 0 extra)
add_test(
    NAME database-zone-runtime-table-unload-invalid-unknown-option
    COMMAND kisakcod-db-zone-runtime-table-tests --unknown-option 0)
add_test(
    NAME database-zone-runtime-table-mutation-invalid-missing-value
    COMMAND kisakcod-db-zone-runtime-table-tests --unsafe-mutable)
add_test(
    NAME database-zone-runtime-table-mutation-invalid-extra-value
    COMMAND kisakcod-db-zone-runtime-table-tests
        --unsafe-mutable backend extra)
add_test(
    NAME database-zone-runtime-table-mutation-invalid-kind
    COMMAND kisakcod-db-zone-runtime-table-tests
        --unsafe-mutable unknown)
set_tests_properties(
    database-zone-runtime-table-unload-invalid-missing-value
    database-zone-runtime-table-unload-invalid-extra-value
    database-zone-runtime-table-unload-invalid-unknown-option
    database-zone-runtime-table-mutation-invalid-missing-value
    database-zone-runtime-table-mutation-invalid-extra-value
    database-zone-runtime-table-mutation-invalid-kind
    PROPERTIES TIMEOUT 30 WILL_FAIL TRUE)

# Compile production's runtime-table header without its test-access opt-in.
# Dependent access checks make this a normal positive contract: restoring
# either same-name friendship fails the target's static assertions.
add_executable(
    kisakcod-db-zone-runtime-table-production-seal-tests
    db_zone_runtime_table_production_seal_tests.cpp
)
target_include_directories(
    kisakcod-db-zone-runtime-table-production-seal-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-table-production-seal-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-table-production-seal-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(
    kisakcod-db-zone-runtime-table-production-seal-tests)
set_target_properties(
    kisakcod-db-zone-runtime-table-production-seal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-runtime-table-production-test-access-sealed
    COMMAND kisakcod-db-zone-runtime-table-production-seal-tests
)

add_executable(kisakcod-fx-archive-disk32-tests
    fx_archive_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
)
target_include_directories(kisakcod-fx-archive-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-archive-disk32-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-disk32-tests)
set_target_properties(kisakcod-fx-archive-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-disk32-codec
    COMMAND kisakcod-fx-archive-disk32-tests
)
set_tests_properties(effectscore-archive-disk32-codec PROPERTIES TIMEOUT 10)

add_library(kisakcod-fx-archive-body-state-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_body_state_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-body-state-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-body-state-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-body-state-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-body-state-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-body-state-disk32-subject)

add_executable(kisakcod-fx-archive-body-state-disk32-tests
    fx_archive_body_state_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-body-state-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-body-state-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-body-state-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-body-state-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-body-state-disk32-tests)
set_target_properties(
    kisakcod-fx-archive-body-state-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-body-state-disk32-codec
    COMMAND kisakcod-fx-archive-body-state-disk32-tests
)
set_tests_properties(
    effectscore-archive-body-state-disk32-codec PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-archive-system-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_system_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-system-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-system-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-system-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-system-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-system-disk32-subject)

add_executable(kisakcod-fx-archive-system-disk32-tests
    fx_archive_system_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-system-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-system-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-system-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-system-disk32-tests)
set_target_properties(kisakcod-fx-archive-system-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-system-disk32-codec
    COMMAND kisakcod-fx-archive-system-disk32-tests
)
set_tests_properties(
    effectscore-archive-system-disk32-codec PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-archive-buffers-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_buffers_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-buffers-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-buffers-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-buffers-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-buffers-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-buffers-disk32-subject)

add_executable(kisakcod-fx-archive-buffers-disk32-tests
    fx_archive_buffers_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-buffers-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-buffers-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-buffers-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-buffers-disk32-tests)
set_target_properties(kisakcod-fx-archive-buffers-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-buffers-disk32-codec
    COMMAND kisakcod-fx-archive-buffers-disk32-tests
)
set_tests_properties(
    effectscore-archive-buffers-disk32-codec PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-archive-native-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_native_disk32.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_semantics.cpp
)
target_include_directories(
    kisakcod-fx-archive-native-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-native-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-native-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-native-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-native-disk32-subject)

add_executable(kisakcod-fx-archive-native-disk32-tests
    fx_archive_native_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-archive-native-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-native-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-native-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-native-disk32-tests)
set_target_properties(kisakcod-fx-archive-native-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-native-disk32-codec
    COMMAND kisakcod-fx-archive-native-disk32-tests
)
set_tests_properties(
    effectscore-archive-native-disk32-codec PROPERTIES TIMEOUT 30)

add_library(kisakcod-fx-archive-reader-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_reader_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-reader-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-reader-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-archive-reader-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-reader-disk32-subject)

add_executable(kisakcod-fx-archive-reader-disk32-tests
    fx_archive_reader_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-body-state-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-reader-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-reader-disk32-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-archive-reader-disk32-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-archive-reader-disk32-tests)
set_target_properties(
    kisakcod-fx-archive-reader-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-reader-disk32
    COMMAND kisakcod-fx-archive-reader-disk32-tests
)
set_tests_properties(
    effectscore-archive-reader-disk32 PROPERTIES TIMEOUT 60)

add_library(kisakcod-fx-archive-restore-candidate-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_archive_restore_candidate_disk32.cpp
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-subject PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-subject SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-restore-candidate-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-restore-candidate-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(
    kisakcod-fx-archive-restore-candidate-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-archive-restore-candidate-disk32-subject)

add_executable(kisakcod-fx-archive-restore-candidate-disk32-tests
    fx_archive_restore_candidate_disk32_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_disk32.cpp
    $<TARGET_OBJECTS:kisakcod-fx-archive-system-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-buffers-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-body-state-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-reader-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-archive-restore-candidate-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-effect-table-restore-subject>
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE
        ${SRC_DIR}/EffectsCore
)
target_include_directories(
    kisakcod-fx-archive-restore-candidate-disk32-tests SYSTEM PRIVATE
        ${SRC_DIR}
)
target_compile_features(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-fx-archive-restore-candidate-disk32-tests PRIVATE
        kisakcod-memfile-test-subject
        Threads::Threads)
kisakcod_test_warnings(
    kisakcod-fx-archive-restore-candidate-disk32-tests)
set_target_properties(
    kisakcod-fx-archive-restore-candidate-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-restore-candidate-disk32
    COMMAND kisakcod-fx-archive-restore-candidate-disk32-tests
)
set_tests_properties(
    effectscore-archive-restore-candidate-disk32 PROPERTIES TIMEOUT 60)

add_executable(kisakcod-fx-fastfile-disk32-tests
    fx_fastfile_disk32_tests.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-disk32-tests)
set_target_properties(kisakcod-fx-fastfile-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-disk32-schema
    COMMAND kisakcod-fx-fastfile-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-disk32-schema PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-fastfile-native-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_native_disk32.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-native-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-fastfile-native-disk32-subject)

add_executable(kisakcod-fx-fastfile-native-disk32-tests
    fx_fastfile_native_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-native-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-disk32-tests)
set_target_properties(kisakcod-fx-fastfile-native-disk32-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-native-disk32-conversion
    COMMAND kisakcod-fx-fastfile-native-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-native-disk32-conversion PROPERTIES TIMEOUT 60)

add_library(kisakcod-fx-fastfile-impact-native-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_impact_native_disk32.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-impact-native-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-impact-native-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-impact-native-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-impact-native-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-fastfile-impact-native-disk32-subject)

add_executable(kisakcod-fx-fastfile-impact-native-disk32-tests
    fx_fastfile_impact_native_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-impact-native-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-impact-native-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-impact-native-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-impact-native-disk32-tests)
set_target_properties(
    kisakcod-fx-fastfile-impact-native-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-impact-native-disk32-conversion
    COMMAND kisakcod-fx-fastfile-impact-native-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-impact-native-disk32-conversion PROPERTIES TIMEOUT 60)

add_library(kisakcod-fx-fastfile-native-arena-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_native_arena.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-native-arena-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-arena-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-arena-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-arena-subject)
kisakcod_fx_helper_stack_budget(kisakcod-fx-fastfile-native-arena-subject)

add_executable(kisakcod-fx-fastfile-native-arena-tests
    fx_fastfile_native_arena_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-native-arena-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-native-arena-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-native-arena-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-native-arena-tests)
set_target_properties(kisakcod-fx-fastfile-native-arena-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-native-arena
    COMMAND kisakcod-fx-fastfile-native-arena-tests
)
set_tests_properties(
    effectscore-fastfile-native-arena PROPERTIES TIMEOUT 20)

add_library(kisakcod-fx-fastfile-zone-adapter-disk32-subject OBJECT
    ${SRC_DIR}/EffectsCore/fx_fastfile_zone_adapter_disk32.cpp
)
target_include_directories(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-zone-adapter-disk32-subject)
kisakcod_fx_helper_stack_budget(
    kisakcod-fx-fastfile-zone-adapter-disk32-subject)

add_executable(kisakcod-fx-fastfile-zone-adapter-disk32-tests
    fx_fastfile_zone_adapter_disk32_tests.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-fx-fastfile-zone-adapter-disk32-tests)
set_target_properties(
    kisakcod-fx-fastfile-zone-adapter-disk32-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-fastfile-zone-adapter-disk32
    COMMAND kisakcod-fx-fastfile-zone-adapter-disk32-tests
)
set_tests_properties(
    effectscore-fastfile-zone-adapter-disk32 PROPERTIES TIMEOUT 60)

add_executable(kisakcod-db-zone-runtime-storage-tests
    db_zone_runtime_storage_tests.cpp
    ${SRC_DIR}/database/db_zone_runtime_storage.cpp
    ${SRC_DIR}/database/db_script_string_journal.cpp
    ${SRC_DIR}/EffectsCore/fx_zone_runtime_storage_bridge.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-db-zone-runtime-storage-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-storage-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-storage-tests PRIVATE
        KISAK_MP
        KISAK_DB_SCRIPT_STRING_JOURNAL_TESTING=1)
kisakcod_test_warnings(kisakcod-db-zone-runtime-storage-tests)
set_target_properties(
    kisakcod-db-zone-runtime-storage-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-zone-runtime-storage-layout
    COMMAND kisakcod-db-zone-runtime-storage-tests
)
set_tests_properties(
    database-zone-runtime-storage-layout PROPERTIES TIMEOUT 30)

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

add_executable(kisakcod-db-referenced-fastfile-tests db_referenced_fastfile_tests.cpp)
target_include_directories(kisakcod-db-referenced-fastfile-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-db-referenced-fastfile-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-referenced-fastfile-tests)
set_target_properties(kisakcod-db-referenced-fastfile-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME database-referenced-fastfile-format
    COMMAND kisakcod-db-referenced-fastfile-tests
)

add_executable(kisakcod-server-file-compare-tests
    server_file_compare_tests.cpp
)
target_include_directories(
    kisakcod-server-file-compare-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-server-file-compare-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-server-file-compare-tests)
set_target_properties(kisakcod-server-file-compare-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME qcommon-server-file-compare
    COMMAND kisakcod-server-file-compare-tests
)

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

add_executable(kisakcod-fx-missing-effect-alias-tests
    fx_missing_effect_alias_tests.cpp
)
target_include_directories(kisakcod-fx-missing-effect-alias-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-missing-effect-alias-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-missing-effect-alias-tests)
set_target_properties(kisakcod-fx-missing-effect-alias-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-missing-effect-alias
    COMMAND kisakcod-fx-missing-effect-alias-tests
)

add_executable(kisakcod-fx-physics-sidecar-tests
    fx_physics_sidecar_tests.cpp
)
target_include_directories(kisakcod-fx-physics-sidecar-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-physics-sidecar-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-physics-sidecar-tests)
set_target_properties(kisakcod-fx-physics-sidecar-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-physics-body-sidecar
    COMMAND kisakcod-fx-physics-sidecar-tests
)

# Keep the test-only corruption helper out of production's namespace and
# friendship surface. Dependent access checks make this a positive compile
# contract: restoring the public-name friendship fails the static assertions.
add_executable(
    kisakcod-fx-physics-sidecar-production-seal-tests
    fx_physics_sidecar_production_seal_tests.cpp
)
target_include_directories(
    kisakcod-fx-physics-sidecar-production-seal-tests
    PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-physics-sidecar-production-seal-tests
    PRIVATE cxx_std_20)
kisakcod_test_warnings(
    kisakcod-fx-physics-sidecar-production-seal-tests)
set_target_properties(
    kisakcod-fx-physics-sidecar-production-seal-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME effectscore-physics-sidecar-production-test-access-sealed
    COMMAND kisakcod-fx-physics-sidecar-production-seal-tests
)

add_executable(kisakcod-fx-archive-capacity-tests
    fx_archive_capacity_tests.cpp
)
target_include_directories(kisakcod-fx-archive-capacity-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-archive-capacity-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-capacity-tests)
set_target_properties(kisakcod-fx-archive-capacity-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-capacity-planning
    COMMAND kisakcod-fx-archive-capacity-tests
)
set_tests_properties(effectscore-archive-capacity-planning PROPERTIES TIMEOUT 10)

add_executable(kisakcod-fx-archive-restore-control-tests
    fx_archive_restore_control_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_restore_control.cpp
)
target_include_directories(kisakcod-fx-archive-restore-control-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-archive-restore-control-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-restore-control-tests)
set_target_properties(kisakcod-fx-archive-restore-control-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-restore-control
    COMMAND kisakcod-fx-archive-restore-control-tests
)
set_tests_properties(effectscore-archive-restore-control PROPERTIES TIMEOUT 10)

add_executable(kisakcod-fx-archive-physics-batch-control-tests
    fx_archive_physics_batch_control_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_physics_batch_control.cpp
)
target_include_directories(
    kisakcod-fx-archive-physics-batch-control-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-physics-batch-control-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-physics-batch-control-tests)
set_target_properties(kisakcod-fx-archive-physics-batch-control-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-physics-batch-control
    COMMAND kisakcod-fx-archive-physics-batch-control-tests
)
set_tests_properties(effectscore-archive-physics-batch-control PROPERTIES TIMEOUT 10)

add_executable(kisakcod-ode-fixed-pool-occupancy-tests
    ode_fixed_pool_occupancy_tests.cpp
    ${SRC_DIR}/universal/pool_allocator.cpp
    ${SRC_DIR}/physics/phys_resource_pair.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_physics_batch_control.cpp
)
target_include_directories(
    kisakcod-ode-fixed-pool-occupancy-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-ode-fixed-pool-occupancy-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-ode-fixed-pool-occupancy-tests)
set_target_properties(kisakcod-ode-fixed-pool-occupancy-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME physics-ode-fixed-pool-occupancy
    COMMAND kisakcod-ode-fixed-pool-occupancy-tests
)
set_tests_properties(physics-ode-fixed-pool-occupancy PROPERTIES TIMEOUT 20)

add_executable(kisakcod-fx-archive-gate-control-tests
    fx_archive_gate_control_tests.cpp
    ${SRC_DIR}/EffectsCore/fx_archive_gate_control.cpp
)
target_include_directories(
    kisakcod-fx-archive-gate-control-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-gate-control-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-gate-control-tests)
set_target_properties(kisakcod-fx-archive-gate-control-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-gate-control
    COMMAND kisakcod-fx-archive-gate-control-tests
)
set_tests_properties(effectscore-archive-gate-control PROPERTIES TIMEOUT 10)

add_executable(kisakcod-fx-archive-restore-workspace-tests
    fx_archive_restore_workspace_tests.cpp
)
target_include_directories(
    kisakcod-fx-archive-restore-workspace-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-archive-restore-workspace-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-fx-archive-restore-workspace-tests)
set_target_properties(kisakcod-fx-archive-restore-workspace-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-archive-restore-workspace
    COMMAND kisakcod-fx-archive-restore-workspace-tests
)

add_executable(kisakcod-phys-resource-pair-tests
    phys_resource_pair_tests.cpp
    ${SRC_DIR}/physics/phys_resource_pair.cpp
)
target_include_directories(kisakcod-phys-resource-pair-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-phys-resource-pair-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-phys-resource-pair-tests)
set_target_properties(kisakcod-phys-resource-pair-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME physics-resource-pair-rollback
    COMMAND kisakcod-phys-resource-pair-tests
)

add_executable(kisakcod-phys-user-geom-storage-tests
    phys_user_geom_storage_tests.cpp
)
target_include_directories(kisakcod-phys-user-geom-storage-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-phys-user-geom-storage-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-phys-user-geom-storage-tests)
set_target_properties(kisakcod-phys-user-geom-storage-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME physics-native-user-geom-storage
    COMMAND kisakcod-phys-user-geom-storage-tests
)

add_executable(kisakcod-pool-allocator-tests
    pool_allocator_tests.cpp
    ${SRC_DIR}/universal/pool_allocator.cpp
)
target_include_directories(kisakcod-pool-allocator-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-pool-allocator-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-pool-allocator-tests)
set_target_properties(kisakcod-pool-allocator-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME pool-native-pointer-freelist
    COMMAND kisakcod-pool-allocator-tests
)
set_tests_properties(pool-native-pointer-freelist PROPERTIES TIMEOUT 10)

add_executable(kisakcod-physicalmemory-legacy-tests
    physicalmemory_legacy_tests.cpp
    ${SRC_DIR}/universal/physicalmemory.cpp
    ${SRC_DIR}/universal/physicalmemory_checked.cpp
)
target_include_directories(
    kisakcod-physicalmemory-legacy-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-physicalmemory-legacy-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-physicalmemory-legacy-tests PRIVATE
    KISAK_MP
    KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING)
kisakcod_test_warnings(kisakcod-physicalmemory-legacy-tests)
set_target_properties(kisakcod-physicalmemory-legacy-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME universal-physicalmemory-legacy-layout-and-indexing
    COMMAND kisakcod-physicalmemory-legacy-tests
)
set_tests_properties(
    universal-physicalmemory-legacy-layout-and-indexing PROPERTIES TIMEOUT 20)

add_executable(kisakcod-physicalmemory-runtime-tests
    physicalmemory_runtime_tests.cpp
    ${SRC_DIR}/universal/physicalmemory.cpp
    ${SRC_DIR}/universal/physicalmemory_checked.cpp
)
target_include_directories(
    kisakcod-physicalmemory-runtime-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-physicalmemory-runtime-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-physicalmemory-runtime-tests PRIVATE
    KISAK_MP
    KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING)
target_link_libraries(
    kisakcod-physicalmemory-runtime-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-physicalmemory-runtime-tests)
set_target_properties(kisakcod-physicalmemory-runtime-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME universal-physicalmemory-runtime-control
    COMMAND kisakcod-physicalmemory-runtime-tests
)
set_tests_properties(
    universal-physicalmemory-runtime-control PROPERTIES TIMEOUT 60)

# Compile the real implementation without the testing macro and retain its
# object for the runtime seal's symbol-surface inspection. This is deliberately
# an object library: the implementation's engine service references need not be
# replaced by permissive fixture stubs merely to inspect its linkage.
add_library(kisakcod-physicalmemory-runtime-production-object OBJECT
    ${SRC_DIR}/universal/physicalmemory.cpp
)
target_include_directories(
    kisakcod-physicalmemory-runtime-production-object SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-physicalmemory-runtime-production-object PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-physicalmemory-runtime-production-object PRIVATE KISAK_MP)
kisakcod_test_warnings(
    kisakcod-physicalmemory-runtime-production-object)

# This normal macro-off target proves production callers cannot name the
# hidden global PMem testing seam. Its CTest wrapper also inspects the actual
# macro-off production object for local state and absent test-access symbols.
add_executable(kisakcod-physicalmemory-runtime-production-seal-tests
    physicalmemory_runtime_production_seal_tests.cpp
)
target_include_directories(
    kisakcod-physicalmemory-runtime-production-seal-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-physicalmemory-runtime-production-seal-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(
    kisakcod-physicalmemory-runtime-production-seal-tests)
set_target_properties(
    kisakcod-physicalmemory-runtime-production-seal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_dependencies(
    kisakcod-physicalmemory-runtime-production-seal-tests
    kisakcod-physicalmemory-runtime-production-object)
add_test(
    NAME universal-physicalmemory-runtime-production-test-access-sealed
    COMMAND ${CMAKE_COMMAND}
        "-DSEAL_EXECUTABLE=$<TARGET_FILE:kisakcod-physicalmemory-runtime-production-seal-tests>"
        "-DPRODUCTION_OBJECTS=$<JOIN:$<TARGET_OBJECTS:kisakcod-physicalmemory-runtime-production-object>,|>"
        "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DNM_TOOL=${CMAKE_NM}"
        "-DREADELF_TOOL=${CMAKE_READELF}"
        "-DLINKER_TOOL=${CMAKE_LINKER}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/physicalmemory_runtime_production_seal_test.cmake
)
set_tests_properties(
    universal-physicalmemory-runtime-production-test-access-sealed
    PROPERTIES TIMEOUT 20)

add_executable(kisakcod-physicalmemory-checked-tests
    physicalmemory_checked_tests.cpp
    ${SRC_DIR}/universal/physicalmemory_checked.cpp
)
target_include_directories(
    kisakcod-physicalmemory-checked-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-physicalmemory-checked-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-physicalmemory-checked-tests)
set_target_properties(kisakcod-physicalmemory-checked-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME universal-physicalmemory-checked-scopes
    COMMAND kisakcod-physicalmemory-checked-tests
)
set_tests_properties(
    universal-physicalmemory-checked-scopes PROPERTIES TIMEOUT 20)

# This is a normal positive compile/run target. Dependent access checks prove
# that production callers cannot copy, reset, or reach receipt authority.
add_executable(kisakcod-physicalmemory-checked-api-seal-tests
    physicalmemory_checked_api_seal_tests.cpp
)
target_include_directories(
    kisakcod-physicalmemory-checked-api-seal-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-physicalmemory-checked-api-seal-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-physicalmemory-checked-api-seal-tests)
set_target_properties(
    kisakcod-physicalmemory-checked-api-seal-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME universal-physicalmemory-checked-api-sealed
    COMMAND kisakcod-physicalmemory-checked-api-seal-tests
)
set_tests_properties(
    universal-physicalmemory-checked-api-sealed PROPERTIES TIMEOUT 20)

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

add_executable(kisakcod-fx-iterator-atomic-tests
    fx_iterator_atomic_tests.cpp
)
target_include_directories(kisakcod-fx-iterator-atomic-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-iterator-atomic-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-fx-iterator-atomic-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-iterator-atomic-tests)
set_target_properties(kisakcod-fx-iterator-atomic-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-iterator-atomic-protocol
    COMMAND kisakcod-fx-iterator-atomic-tests
)
set_tests_properties(effectscore-iterator-atomic-protocol PROPERTIES TIMEOUT 20)

add_executable(kisakcod-fx-snapshot-publication-tests
    fx_snapshot_publication_tests.cpp
)
target_include_directories(
    kisakcod-fx-snapshot-publication-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-fx-snapshot-publication-tests PRIVATE cxx_std_20)
target_link_libraries(
    kisakcod-fx-snapshot-publication-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-snapshot-publication-tests)
set_target_properties(kisakcod-fx-snapshot-publication-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-snapshot-publication-coherence
    COMMAND kisakcod-fx-snapshot-publication-tests
)
set_tests_properties(
    effectscore-snapshot-publication-coherence PROPERTIES TIMEOUT 30)

add_executable(kisakcod-fx-pool-tests
    fx_pool_tests.cpp
)
target_include_directories(kisakcod-fx-pool-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-fx-pool-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-fx-pool-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-fx-pool-tests)
set_target_properties(kisakcod-fx-pool-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME effectscore-pool-and-handle-contracts
    COMMAND kisakcod-fx-pool-tests
)
set_tests_properties(effectscore-pool-and-handle-contracts PROPERTIES TIMEOUT 30)

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

add_library(kisakcod-script-memorytree-lease-api-seal OBJECT
    script_memorytree_lease_api_seal_compile_tests.cpp
)
target_include_directories(
    kisakcod-script-memorytree-lease-api-seal SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-script-memorytree-lease-api-seal PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-script-memorytree-lease-api-seal)

add_executable(kisakcod-script-memorytree-try-tests
    script_memorytree_try_tests.cpp
)
target_include_directories(
    kisakcod-script-memorytree-try-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-script-memorytree-try-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-script-memorytree-try-tests PRIVATE
    KISAK_MP
    KISAK_MEMORY_TREE_VALIDATION_TESTING=1
    KISAK_SCRIPT_STRING_PERF_TESTING=1)
target_link_libraries(kisakcod-script-memorytree-try-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-script-memorytree-try-tests)
set_target_properties(kisakcod-script-memorytree-try-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME script-memorytree-try-contracts
    COMMAND kisakcod-script-memorytree-try-tests
)
set_tests_properties(script-memorytree-try-contracts PROPERTIES TIMEOUT 30)

# Include the production string-list translation unit directly so this
# white-box fixture can inspect its file-local persistent state without
# copying any ownership algorithm. The allocator, coordinator, transaction,
# and fast-lock implementations remain separately linked production
# translation units, matching the engine boundaries.
add_executable(kisakcod-script-string-ownership-tests
    script_string_ownership_tests.cpp
    ${SRC_DIR}/database/db_registry_ownership_coordinator.cpp
    ${SRC_DIR}/database/db_script_string_transaction.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${SRC_DIR}/script/scr_memorytree.cpp
)
target_include_directories(
    kisakcod-script-string-ownership-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-script-string-ownership-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-script-string-ownership-tests PRIVATE
    KISAK_MP
    KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING=1
    KISAK_MEMORY_TREE_VALIDATION_TESTING=1
    KISAK_SCRIPT_STRING_PERF_TESTING=1)
target_link_libraries(
    kisakcod-script-string-ownership-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-script-string-ownership-tests)
set_target_properties(kisakcod-script-string-ownership-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME script-string-report-free-ownership-contracts
    COMMAND kisakcod-script-string-ownership-tests
)
set_tests_properties(
    script-string-report-free-ownership-contracts PROPERTIES TIMEOUT 30)

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

add_executable(kisakcod-net-wire-contract-tests
    net_wire_contract_tests.cpp
)
# The legacy engine headers (q_shared.h / qcommon.h) are decompiled code and are not
# -Wall -Wextra clean. SYSTEM keeps the strict warning set on this test's own code while
# not failing the build on warnings originating inside those headers.
target_include_directories(kisakcod-net-wire-contract-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-net-wire-contract-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-net-wire-contract-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-net-wire-contract-tests)
set_target_properties(kisakcod-net-wire-contract-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME net-wire-format-contracts
    COMMAND kisakcod-net-wire-contract-tests
)

add_executable(kisakcod-vehicle-material-time-tests
    vehicle_material_time_tests.cpp
)
target_include_directories(
    kisakcod-vehicle-material-time-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-vehicle-material-time-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-vehicle-material-time-tests)
set_target_properties(kisakcod-vehicle-material-time-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME vehicle-material-time-contracts
    COMMAND kisakcod-vehicle-material-time-tests
)

add_executable(kisakcod-upstream-sort-tests
    native_sort_contract_tests.cpp
)
target_include_directories(
    kisakcod-upstream-sort-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-upstream-sort-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-upstream-sort-tests)
set_target_properties(kisakcod-upstream-sort-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME upstream-sort-contracts
    COMMAND kisakcod-upstream-sort-tests
)

add_executable(kisakcod-upstream-angle-math-tests
    upstream_angle_math_tests.cpp
    ${SRC_DIR}/universal/com_angle.cpp
)
target_include_directories(
    kisakcod-upstream-angle-math-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-upstream-angle-math-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-upstream-angle-math-tests)
set_target_properties(kisakcod-upstream-angle-math-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME upstream-reconciliation-angle-math-contracts
    COMMAND kisakcod-upstream-angle-math-tests
)

add_executable(kisakcod-upstream-aim-safety-tests
    upstream_aim_safety_tests.cpp
)
target_include_directories(
    kisakcod-upstream-aim-safety-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-upstream-aim-safety-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-upstream-aim-safety-tests)
set_target_properties(kisakcod-upstream-aim-safety-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME upstream-reconciliation-aim-safety-contracts
    COMMAND kisakcod-upstream-aim-safety-tests
)

add_executable(kisakcod-upstream-command-dispatch-tests
    upstream_command_dispatch_tests.cpp
)
target_include_directories(
    kisakcod-upstream-command-dispatch-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-upstream-command-dispatch-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-upstream-command-dispatch-tests)
set_target_properties(kisakcod-upstream-command-dispatch-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME upstream-reconciliation-command-dispatch-contracts
    COMMAND kisakcod-upstream-command-dispatch-tests
)
foreach(_hudelem_variant IN ITEMS MP SP)
    string(TOLOWER "${_hudelem_variant}" _hudelem_variant_lower)
    set(_hudelem_sort_target
        "kisakcod-hudelem-sort-${_hudelem_variant_lower}-tests")
    add_executable(${_hudelem_sort_target}
        hudelem_sort_tests.cpp
    )
    target_include_directories(
        ${_hudelem_sort_target} SYSTEM PRIVATE ${SRC_DIR})
    target_compile_features(${_hudelem_sort_target} PRIVATE cxx_std_20)
    target_compile_definitions(
        ${_hudelem_sort_target} PRIVATE "KISAK_${_hudelem_variant}")
    kisakcod_test_warnings(${_hudelem_sort_target})
    set_target_properties(${_hudelem_sort_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(
        NAME "hudelem-sort-${_hudelem_variant_lower}-contracts"
        COMMAND ${_hudelem_sort_target}
    )
endforeach()

add_test(
    NAME vehicle-material-time-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/vehicle_material_time_source_test.cmake
)

add_test(
    NAME platform-console-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/platform_console_source_test.cmake
)

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
    NAME target-table-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/target_table_source_test.cmake
)

add_test(
    NAME target-client-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/target_client_source_test.cmake
)

add_test(
    NAME actor-grenade-safe-target-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/actor_grenade_safety_source_test.cmake
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
    NAME physics-transaction-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/physics_transaction_source_test.cmake
)

add_test(
    NAME physics-rollback-recipe-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/physics_rollback_recipe_source_test.cmake
)

add_test(
    NAME effectscore-archive-physics-transaction
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_physics_transaction_test.cmake
)

add_test(
    NAME effectscore-effect-table-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_effect_table_restore_source_test.cmake
)

add_test(
    NAME effectscore-effect-table-save-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_effect_table_save_source_test.cmake
)

add_test(
    NAME effectscore-live-physics-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_live_physics_source_test.cmake
)

add_test(
    NAME effectscore-archive-snapshot-publication-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_snapshot_publication_source_test.cmake
)

add_test(
    NAME effectscore-archive-system-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_system_disk32_source_test.cmake
)

add_test(
    NAME effectscore-archive-buffers-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_buffers_disk32_source_test.cmake
)

add_test(
    NAME effectscore-archive-native-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_native_disk32_source_test.cmake
)

add_test(
    NAME effectscore-archive-body-state-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_body_state_disk32_source_test.cmake
)

add_test(
    NAME effectscore-archive-reader-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_archive_reader_disk32_source_test.cmake
)

add_test(
    NAME effectscore-fastfile-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_fastfile_disk32_source_test.cmake
)

add_test(
    NAME effectscore-fastfile-zone-adapter-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/fx_fastfile_zone_adapter_source_test.cmake
)

add_test(
    NAME database-xasset-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_xasset_disk32_source_test.cmake
)

add_test(
    NAME database-script-string-disk32-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/db_script_string_disk32_source_test.cmake
)

add_test(
    NAME database-zone-load-context-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_load_context_source_test.cmake
)

add_test(
    NAME database-zone-stream-ownership-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_stream_ownership_source_test.cmake
)

add_test(
    NAME database-zone-pending-copy-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_pending_copy_ledger_source_test.cmake
)

add_test(
    NAME database-script-string-journal-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/db_script_string_journal_source_test.cmake
)

add_test(
    NAME database-script-string-ownership-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/db_script_string_ownership_source_test.cmake
)

add_test(
    NAME database-zone-script-string-ownership-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_script_string_ownership_source_test.cmake
)

add_test(
    NAME database-registry-ownership-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/db_registry_ownership_coordinator_source_test.cmake
)

add_test(
    NAME database-zone-runtime-table-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_runtime_table_source_test.cmake
)

add_test(
    NAME database-zone-runtime-facade-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_runtime_facade_source_test.cmake
)

add_test(
    NAME database-zone-runtime-storage-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_runtime_storage_source_test.cmake
)

add_test(
    NAME database-zone-slot-range-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/db_zone_slot_range_source_test.cmake
)

add_test(
    NAME upstream-correctness-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/upstream_correctness_source_test.cmake
)

add_test(
    NAME upstream-sort-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/upstream_sort_source_test.cmake
)

add_test(
    NAME hudelem-sort-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/hudelem_sort_source_test.cmake
)

add_executable(
    kisakcod-actor-navigation-geometry-tests
    actor_navigation_geometry_tests.cpp
)
target_include_directories(
    kisakcod-actor-navigation-geometry-tests PRIVATE ${SRC_DIR}
)
target_compile_features(
    kisakcod-actor-navigation-geometry-tests PRIVATE cxx_std_20
)
kisakcod_test_warnings(kisakcod-actor-navigation-geometry-tests)
set_target_properties(
    kisakcod-actor-navigation-geometry-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME actor-navigation-geometry
    COMMAND kisakcod-actor-navigation-geometry-tests
)
add_test(
    NAME actor-navigation-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/actor_navigation_source_test.cmake
)

add_test(
    NAME missile-union-layout-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/missile_layout_source_test.cmake
)

add_test(
    NAME upstream-safe-fixlets-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/upstream_safe_fixlets_source_test.cmake
)

add_test(
    NAME upstream-reconciliation-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/upstream_reconciliation_source_test.cmake
)

add_test(
    NAME sound-dry-send-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/sound_dry_send_source_test.cmake
)

add_test(
    NAME pool-native-pointer-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/pool_allocator_source_test.cmake
)

add_test(
    NAME universal-physicalmemory-checked-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/physicalmemory_checked_source_test.cmake
)

add_test(
    NAME universal-physicalmemory-legacy-source-invariants
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/physicalmemory_legacy_source_test.cmake
)

add_test(
    NAME security-source-regressions
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/security_regression_test.cmake
)

# Fast-file database and zone runtime tests.
# Included from tests/CMakeLists.txt.

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
        kisakcod-db-zone-script-string-ownership-tests PRIVATE winmm ws2_32)
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

add_executable(kisakcod-db-zone-runtime-callback-context-tests
    db_zone_runtime_callback_context_tests.cpp
    ${SRC_DIR}/database/db_zone_runtime_callback_context.cpp)
target_include_directories(
    kisakcod-db-zone-runtime-callback-context-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-callback-context-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-callback-context-tests PRIVATE
    KISAK_MP
    KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING=1)
kisakcod_test_warnings(kisakcod-db-zone-runtime-callback-context-tests)
set_target_properties(
    kisakcod-db-zone-runtime-callback-context-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-zone-runtime-callback-context
    COMMAND kisakcod-db-zone-runtime-callback-context-tests)
set_tests_properties(
    database-zone-runtime-callback-context PROPERTIES TIMEOUT 20)

add_executable(kisakcod-db-zone-runtime-table-tests
    db_zone_runtime_table_tests.cpp
    ${SRC_DIR}/database/db_zone_runtime_callback_context.cpp
    ${SRC_DIR}/database/db_zone_runtime_table.cpp
    ${SRC_DIR}/database/db_fx_zone_adapter_wiring.cpp
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
    KISAK_PHYSICAL_MEMORY_RUNTIME_TESTING=1
    KISAK_DB_ZONE_RUNTIME_CALLBACK_CONTEXT_TESTING=1
    KISAK_DB_ZONE_LOAD_CONTEXT_TESTING=1
    KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING=1
    KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1
    KISAK_DB_ZONE_RUNTIME_TABLE_TESTING=1)
target_link_libraries(
    kisakcod-db-zone-runtime-table-tests PRIVATE Threads::Threads)
if (KISAK_PLATFORM STREQUAL "win32")
    target_link_libraries(
        kisakcod-db-zone-runtime-table-tests PRIVATE winmm ws2_32)
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
foreach(_zone_runtime_stable_context_kind IN ITEMS
        legacy-descriptors
        unused-busy
        managed-key
        bank-key
        terminal-phase
        claimed-neighbor
        unused-neighbor)
    add_test(
        NAME database-zone-runtime-table-stable-context-${_zone_runtime_stable_context_kind}
        COMMAND kisakcod-db-zone-runtime-table-tests
            --stable-context ${_zone_runtime_stable_context_kind})
    set_tests_properties(
        database-zone-runtime-table-stable-context-${_zone_runtime_stable_context_kind}
        PROPERTIES TIMEOUT 30)
endforeach()
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
foreach(_zone_runtime_pending_copy_unsafe_kind IN ITEMS
        malformed-record
        postauth-drift
        count-drift
        duplicate-marker
        duplicate-unrelated-markers
        unknown-marker
        lifecycle-drift
        callback-lifecycle-drift)
    add_test(
        NAME database-zone-runtime-table-pending-copy-unsafe-${_zone_runtime_pending_copy_unsafe_kind}
        COMMAND kisakcod-db-zone-runtime-table-tests
            --unsafe-pending-copy ${_zone_runtime_pending_copy_unsafe_kind}
    )
    set_tests_properties(
        database-zone-runtime-table-pending-copy-unsafe-${_zone_runtime_pending_copy_unsafe_kind}
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
add_test(
    NAME database-zone-runtime-table-pending-copy-invalid-missing-value
    COMMAND kisakcod-db-zone-runtime-table-tests --unsafe-pending-copy)
add_test(
    NAME database-zone-runtime-table-pending-copy-invalid-extra-value
    COMMAND kisakcod-db-zone-runtime-table-tests
        --unsafe-pending-copy malformed-record extra)
add_test(
    NAME database-zone-runtime-table-pending-copy-invalid-kind
    COMMAND kisakcod-db-zone-runtime-table-tests
        --unsafe-pending-copy unknown)
set_tests_properties(
    database-zone-runtime-table-unload-invalid-missing-value
    database-zone-runtime-table-unload-invalid-extra-value
    database-zone-runtime-table-unload-invalid-unknown-option
    database-zone-runtime-table-mutation-invalid-missing-value
    database-zone-runtime-table-mutation-invalid-extra-value
    database-zone-runtime-table-mutation-invalid-kind
    database-zone-runtime-table-pending-copy-invalid-missing-value
    database-zone-runtime-table-pending-copy-invalid-extra-value
    database-zone-runtime-table-pending-copy-invalid-kind
    PROPERTIES TIMEOUT 30 WILL_FAIL TRUE)

# Literal macro-off production chain: facade -> table -> typed stable callback
# context -> ownership controller -> registry coordinator -> real script-string
# registry. The fixture supplies only deterministic platform/reporting seams.
add_executable(kisakcod-db-zone-runtime-stable-context-integration-tests
    db_zone_runtime_stable_context_integration_tests.cpp
    ${SRC_DIR}/database/db_zone_runtime_facade.cpp
    ${SRC_DIR}/database/db_zone_runtime_callback_context.cpp
    ${SRC_DIR}/database/db_zone_runtime_table.cpp
    ${SRC_DIR}/database/db_fx_zone_adapter_wiring.cpp
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
    ${SRC_DIR}/database/db_registry_ownership_coordinator.cpp
    ${SRC_DIR}/EffectsCore/fx_zone_runtime_storage_bridge.cpp
    ${SRC_DIR}/universal/physicalmemory.cpp
    ${SRC_DIR}/universal/physicalmemory_checked.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${SRC_DIR}/script/scr_memorytree.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-db-zone-runtime-stable-context-integration-tests
    SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-zone-runtime-stable-context-integration-tests
    PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-zone-runtime-stable-context-integration-tests
    PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-db-zone-runtime-stable-context-integration-tests
    PRIVATE Threads::Threads)
if (MSVC)
    # The fixture's aborting Com_Error seam makes two legacy post-fatal
    # scr_stringlist returns provably unreachable on MSVC ARM64. Keep the
    # resulting C4702 suppression local to this direct-include test target.
    target_compile_options(
        kisakcod-db-zone-runtime-stable-context-integration-tests
        PRIVATE /wd4702)
    target_link_options(
        kisakcod-db-zone-runtime-stable-context-integration-tests
        PRIVATE "LINKER:/STACK:8388608")
endif()
kisakcod_test_warnings(
    kisakcod-db-zone-runtime-stable-context-integration-tests)
set_target_properties(
    kisakcod-db-zone-runtime-stable-context-integration-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-zone-runtime-stable-context-integration
    COMMAND kisakcod-db-zone-runtime-stable-context-integration-tests)
add_test(
    NAME database-zone-runtime-stable-context-forgotten-finish
    COMMAND kisakcod-db-zone-runtime-stable-context-integration-tests
            --omit-finish)
set_tests_properties(
    database-zone-runtime-stable-context-integration
    database-zone-runtime-stable-context-forgotten-finish
    PROPERTIES TIMEOUT 30)

# End-to-end runtime bridge: drive the bridge against the real
# registry/facade/coord chain.  The fixture supplies deterministic
# platform/reporting seams so the test does not require the full game binary.
add_executable(kisakcod-db-load-legacy-bridge-tests
    db_load_legacy_bridge_tests.cpp
    ${SRC_DIR}/database/db_load_legacy_bridge.cpp
    ${SRC_DIR}/database/db_zone_runtime_facade.cpp
    ${SRC_DIR}/database/db_zone_runtime_callback_context.cpp
    ${SRC_DIR}/database/db_zone_runtime_table.cpp
    ${SRC_DIR}/database/db_fx_zone_adapter_wiring.cpp
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
    ${SRC_DIR}/database/db_registry_ownership_coordinator.cpp
    ${SRC_DIR}/EffectsCore/fx_zone_runtime_storage_bridge.cpp
    ${SRC_DIR}/universal/physicalmemory.cpp
    ${SRC_DIR}/universal/physicalmemory_checked.cpp
    ${SRC_DIR}/qcommon/sys_sync.cpp
    ${SRC_DIR}/script/scr_memorytree.cpp
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>
    $<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>
)
target_include_directories(
    kisakcod-db-load-legacy-bridge-tests
    SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-load-legacy-bridge-tests
    PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-db-load-legacy-bridge-tests
    PRIVATE KISAK_MP)
target_link_libraries(
    kisakcod-db-load-legacy-bridge-tests PRIVATE Threads::Threads)
if (MSVC)
    # The fixture's aborting Com_Error seam makes two legacy post-fatal
    # scr_stringlist returns provably unreachable on MSVC ARM64. Keep the
    # resulting C4702 suppression local to this direct-include test target.
    target_compile_options(
        kisakcod-db-load-legacy-bridge-tests
        PRIVATE /wd4702)
    target_link_options(
        kisakcod-db-load-legacy-bridge-tests
        PRIVATE "LINKER:/STACK:8388608")
endif()
kisakcod_test_warnings(kisakcod-db-load-legacy-bridge-tests)
set_target_properties(
    kisakcod-db-load-legacy-bridge-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-load-legacy-bridge
    COMMAND kisakcod-db-load-legacy-bridge-tests)
set_tests_properties(database-load-legacy-bridge PROPERTIES TIMEOUT 30)

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

# fuzz_fastfile: bounded cursor-primitive fuzz harness. It feeds synthetic
# XAsset-shaped byte streams (xmodel pieces, xanim parts, fx archive body
# state) through the bounded BufCursor read primitives in
# src/xanim/buf_cursor.cpp. It is NOT production-parser coverage: the real
# DB loader, production XModel/XAnim parsers and FX restore composition are
# intentionally not linked here (no FS_ReadFile, no Hunk, no Com_PrintError),
# so a green run pins the bounded-read contract only. Real parser/loader
# enrollment is tracked under #125 (A03). CTest entries match 'fuzz|fastfile'
# so the named-regex filter in the bead's build/test commands resolves.
add_executable(fuzz_fastfile
    fuzz_fastfile.cpp
    ${SRC_DIR}/xanim/buf_cursor.cpp)
target_include_directories(fuzz_fastfile PRIVATE ${SRC_DIR})
target_compile_features(fuzz_fastfile PRIVATE cxx_std_20)
if (MSVC)
    # The CHECK / CHECK_RC macros funnel every failure path through the
    # same return-statement shape, which MSVC's flow analyzer flags as
    # C4702 unreachable code on the first post-macro statement. Keep
    # the suppression local to this harness so the rest of the test
    # targets still see /W4 /WX.
    target_compile_options(fuzz_fastfile PRIVATE /wd4702)
endif()
kisakcod_test_warnings(fuzz_fastfile)
set_target_properties(
    fuzz_fastfile PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME fuzz-fastfile-cursor
    COMMAND fuzz_fastfile seeds)
# The manifest-checked corpus is generated, not hand-maintained. Wire the
# generation as a real CTest setup fixture so the corpus test cannot run
# before the seeds exist. The previous build-target wiring was invisible to
# CTest, so a fresh build passed fuzz-fastfile-corpus with no corpus by
# falling back to the inline seeds.
add_test(
    NAME fuzz-fastfile-corpus-setup
    COMMAND fuzz_fastfile genseeds ${CMAKE_CURRENT_BINARY_DIR}/fuzz_seeds)
set_tests_properties(fuzz-fastfile-corpus-setup PROPERTIES
    FIXTURES_SETUP fuzz_fastfile_corpus_seeds)
add_test(
    NAME fuzz-fastfile-corpus
    COMMAND fuzz_fastfile corpus ${CMAKE_CURRENT_BINARY_DIR}/fuzz_seeds)
set_tests_properties(fuzz-fastfile-corpus PROPERTIES
    FIXTURES_REQUIRED fuzz_fastfile_corpus_seeds)
# Fail-closed negative gates: an absent corpus must be rejected instead of
# silently falling back to the inline seeds, and the manifest contract must
# reject empty / unreadable / manifest-mismatched corpora.
add_test(
    NAME fuzz-fastfile-corpus-rejects-missing
    COMMAND fuzz_fastfile corpus ${CMAKE_CURRENT_BINARY_DIR}/fuzz_seeds_absent)
set_tests_properties(fuzz-fastfile-corpus-rejects-missing PROPERTIES
    WILL_FAIL TRUE)
add_test(
    NAME fuzz-fastfile-corpus-gate
    COMMAND fuzz_fastfile corpus-gate ${CMAKE_CURRENT_BINARY_DIR}/fuzz_corpus_gate)
# Deterministic setup-failure regression: a failed negative-case setup
# (copy under a regular-file path) must be recorded as a gate failure
# instead of silently skipping the case and exiting green. Positive
# polarity: the probe exits 0 only when the failure is propagated.
add_test(
    NAME fuzz-fastfile-corpus-gate-setup-failure
    COMMAND fuzz_fastfile corpus-gate-failing-setup
            ${CMAKE_CURRENT_BINARY_DIR}/fuzz_corpus_gate_setup_failure)

# M5 exit (ki-msb): retail fast-file parity harness. Runs on the native64
# host against an unmodified retail fast-file and emits the capture digest
# consumed by scripts/ci/run-retail-fastfile-parity.sh; --self-test exercises
# the full pipeline on synthesized fixtures so CI covers the instrument
# without retail assets.
add_executable(kisakcod-retail-fastfile-parity-harness
    retail_fastfile_parity_harness.cpp
    ${SRC_DIR}/database/db_graph_hash.cpp)
target_include_directories(
    kisakcod-retail-fastfile-parity-harness PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-retail-fastfile-parity-harness PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-retail-fastfile-parity-harness)
set_target_properties(kisakcod-retail-fastfile-parity-harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME retail-fastfile-parity-harness-self-test
    COMMAND kisakcod-retail-fastfile-parity-harness --self-test)
set_tests_properties(retail-fastfile-parity-harness-self-test PROPERTIES
    TIMEOUT 30 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

kisakcod_ilp32(kisakcod-db-referenced-fastfile-tests
    database-referenced-fastfile-format)

kisakcod_ilp32(kisakcod-db-registry-ownership-coordinator-tests
    database-registry-ownership-coordinator)

kisakcod_ilp32(kisakcod-db-script-string-disk32-tests
    database-script-string-disk32-walk)

kisakcod_ilp32(kisakcod-db-script-string-journal-tests
    database-script-string-transaction-journal)

kisakcod_ilp32(kisakcod-db-xasset-disk32-tests
    database-xasset-disk32-envelope)

kisakcod_ilp32(kisakcod-db-zone-load-context-tests
    database-zone-load-context-lifecycle)

kisakcod_ilp32(kisakcod-db-zone-pending-copy-ledger-tests
    database-zone-pending-copy-ledger)

kisakcod_ilp32(kisakcod-db-zone-runtime-callback-context-tests
    database-zone-runtime-callback-context)

kisakcod_ilp32(kisakcod-db-zone-runtime-facade-tests
    database-zone-runtime-facade)

kisakcod_ilp32(kisakcod-db-zone-runtime-stable-context-integration-tests
    database-zone-runtime-stable-context-forgotten-finish
    database-zone-runtime-stable-context-integration)

kisakcod_ilp32(kisakcod-db-zone-runtime-storage-tests
    database-zone-runtime-storage-layout)

kisakcod_ilp32(kisakcod-db-zone-runtime-table-tests
    database-zone-runtime-table-mutation-invalid-extra-value
    database-zone-runtime-table-mutation-invalid-kind
    database-zone-runtime-table-mutation-invalid-missing-value
    database-zone-runtime-table-mutation-unsafe-backend
    database-zone-runtime-table-mutation-unsafe-postcondition
    database-zone-runtime-table-ownership
    database-zone-runtime-table-stable-context-bank-key
    database-zone-runtime-table-stable-context-claimed-neighbor
    database-zone-runtime-table-stable-context-legacy-descriptors
    database-zone-runtime-table-stable-context-managed-key
    database-zone-runtime-table-stable-context-terminal-phase
    database-zone-runtime-table-stable-context-unused-busy
    database-zone-runtime-table-stable-context-unused-neighbor
    database-zone-runtime-table-unload-invalid-extra-value
    database-zone-runtime-table-unload-invalid-missing-value
    database-zone-runtime-table-unload-invalid-unknown-option
    database-zone-runtime-table-unload-unsafe-0
    database-zone-runtime-table-unload-unsafe-1
    database-zone-runtime-table-unload-unsafe-2
    database-zone-runtime-table-unload-unsafe-3
    database-zone-runtime-table-unload-unsafe-4
    database-zone-runtime-table-unload-unsafe-5
    database-zone-runtime-table-unload-unsafe-6)

kisakcod_ilp32(kisakcod-db-zone-script-string-ownership-tests
    database-zone-script-string-ownership-controller)

kisakcod_ilp32(kisakcod-db-zone-stream-ownership-tests
    database-zone-stream-ownership-runtime-contracts)

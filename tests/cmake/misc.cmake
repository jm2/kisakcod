# Gameplay safety contracts, tripwires and engine call-site pins.
# Included from tests/CMakeLists.txt.

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

add_executable(kisakcod-ui-safety-tests
    ui_safety_tests.cpp
)
target_include_directories(
    kisakcod-ui-safety-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-ui-safety-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-ui-safety-tests)
set_target_properties(
    kisakcod-ui-safety-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME ui-safety-runtime-contracts
    COMMAND kisakcod-ui-safety-tests
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

add_executable(kisakcod-identity-tests identity_tests.cpp)
target_include_directories(kisakcod-identity-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-identity-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-identity-tests)
set_target_properties(kisakcod-identity-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME identity-format-and-overflow COMMAND kisakcod-identity-tests)

add_executable(kisakcod-abi-atomics-tests abi_atomics_tests.cpp)
target_include_directories(kisakcod-abi-atomics-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-abi-atomics-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-abi-atomics-tests)
set_target_properties(kisakcod-abi-atomics-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME abi-atomics-and-layout-macros COMMAND kisakcod-abi-atomics-tests)

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

add_executable(kisakcod-weapon-model-safety-tests
    weapon_model_safety_tests.cpp
)
target_include_directories(
    kisakcod-weapon-model-safety-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-weapon-model-safety-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-weapon-model-safety-tests)
set_target_properties(kisakcod-weapon-model-safety-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME weapon-model-safety-contracts
    COMMAND kisakcod-weapon-model-safety-tests
)

add_executable(kisakcod-weapon-input-safety-tests
    weapon_input_safety_tests.cpp
)
target_include_directories(
    kisakcod-weapon-input-safety-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-weapon-input-safety-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-weapon-input-safety-tests)
set_target_properties(kisakcod-weapon-input-safety-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME weapon-input-safety-contracts
    COMMAND kisakcod-weapon-input-safety-tests
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

# tagInfo_s <-> tagInfoDisk32_s save-record converter. The header is header
# only so the test compiles standalone against the production wire image;
# g_save.cpp pulls in the heavier game dependency tree and is built for the
# SP target only.
add_executable(save_taginfo_test
    save_taginfo_tests.cpp
)
target_include_directories(save_taginfo_test PRIVATE ${SRC_DIR})
target_compile_features(save_taginfo_test PRIVATE cxx_std_20)
kisakcod_test_warnings(save_taginfo_test)
set_target_properties(save_taginfo_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME save-taginfo-disk32-converter
    COMMAND save_taginfo_test
)
set_tests_properties(save-taginfo-disk32-converter PROPERTIES TIMEOUT 10)

# Production-path coverage for the tagInfo save record: the real
# game/taginfo_save.cpp record module (which g_save.cpp's SF_TYPE_TAG_INFO
# branches delegate to) is compiled as a subject and driven through the real
# memfile stream primitives with a fake entity arena and string table. This
# is the coverage the header-only converter test cannot provide — entity
# indices must come from the FULL native pointers, not 32-bit truncations.
add_library(kisakcod-taginfo-save-subject OBJECT
    ${SRC_DIR}/game/taginfo_save.cpp
)
target_include_directories(kisakcod-taginfo-save-subject PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-taginfo-save-subject PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-taginfo-save-subject PRIVATE KISAK_SP)
kisakcod_test_warnings(kisakcod-taginfo-save-subject)

add_executable(save_taginfo_production_test
    save_taginfo_production_tests.cpp
    $<TARGET_OBJECTS:kisakcod-taginfo-save-subject>
)
target_include_directories(save_taginfo_production_test PRIVATE ${SRC_DIR})
target_compile_features(save_taginfo_production_test PRIVATE cxx_std_20)
target_compile_definitions(save_taginfo_production_test PRIVATE KISAK_SP)
kisakcod_test_warnings(save_taginfo_production_test)
target_link_libraries(save_taginfo_production_test PRIVATE
    kisakcod-memfile-test-subject)
set_target_properties(save_taginfo_production_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME save-taginfo-production-path
    COMMAND save_taginfo_production_test
)
set_tests_properties(save-taginfo-production-path PROPERTIES TIMEOUT 10)

# M10 architecture-neutral scalar determinism: pins the exact semantics the
# x86_64 and AArch64 legs must share for float-to-int producers, total-order
# FP comparisons, packed little-endian reference fields, and sign-carrying
# compressed/trail-byte decoding.
add_executable(kisakcod-runtime-scalar-determinism-tests
    runtime_scalar_determinism_tests.cpp
)
target_include_directories(kisakcod-runtime-scalar-determinism-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-runtime-scalar-determinism-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-runtime-scalar-determinism-tests)
set_target_properties(kisakcod-runtime-scalar-determinism-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME runtime-scalar-determinism-contracts
    COMMAND kisakcod-runtime-scalar-determinism-tests
)

# MSVC formatted-print boundary: _snprintf/_vsnprintf truncation must
# report -1 (the contract the decompiled callers were compiled against),
# never the POSIX required-length return. On MSVC hosts the real CRT
# functions are asserted; on POSIX hosts the shim in
# universal/msvc_printf_shim.h is asserted.
add_executable(kisakcod-msvc-printf-shim-tests
    msvc_printf_shim_tests.cpp
)
target_include_directories(kisakcod-msvc-printf-shim-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-msvc-printf-shim-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-msvc-printf-shim-tests)
set_target_properties(kisakcod-msvc-printf-shim-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME msvc-printf-shim-contracts
    COMMAND kisakcod-msvc-printf-shim-tests
)
# M5 exit (ki-msb): canonical widened-runtime-graph capture. The parity
# digest is a domain-separated SHA-256 over a framed, typed, little-endian
# stream; pointer-bearing values never enter it, so a 32-bit reference walk
# and a widened 64-bit walk of one logical graph produce one digest.
add_executable(kisakcod-db-graph-hash-tests
    db_graph_hash_tests.cpp
    ${SRC_DIR}/database/db_graph_hash.cpp)
target_include_directories(
    kisakcod-db-graph-hash-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-db-graph-hash-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-db-graph-hash-tests)
set_target_properties(kisakcod-db-graph-hash-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-graph-hash-canonical
    COMMAND kisakcod-db-graph-hash-tests)
set_tests_properties(database-graph-hash-canonical PROPERTIES TIMEOUT 20)

# Engine call sites that no test compiles: each pin is the only evidence that
# the engine calls the named fork helper (tests/engine_call_site_test.cmake).
function(kisakcod_engine_call_site NAME FILE LITERAL)
    add_test(
        NAME call-site-${NAME}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
            -DFILE=${FILE}
            "-DLITERAL=${LITERAL}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/engine_call_site_test.cmake)
    set_tests_properties(call-site-${NAME} PROPERTIES TIMEOUT 30)
endfunction()
kisakcod_engine_call_site(download-authorization src/server_mp/ucmds.cpp
    "server_file_compare::IsPermittedServerDownloadRequest(")
kisakcod_engine_call_site(download-url-masking src/client_mp/cl_main_mp.cpp
    "CL_SanitizeDownloadUrl(cls.downloadName")
kisakcod_engine_call_site(weapon-model-lookup src/game/g_weapon.cpp
    "bg::weapon_model::CheckedLookup(")
kisakcod_engine_call_site(zone-runtime-table-init src/database/db_registry.cpp
    "db::zone_runtime::TryInitializeZoneRuntimeTable(")
kisakcod_engine_call_site(referenced-fastfile-names src/database/db_registry.cpp
    "db::referenced_fastfile::FormatReferencedFastFileNames(")
kisakcod_engine_call_site(legacy-bridge-user-transfer src/database/db_registry.cpp
    "DbLoadLegacyBridge::TryTransferUsers4To8()")
kisakcod_engine_call_site(script-string-reset src/script/scr_main.cpp
    "SL_TryResetCanonicalStringState(")
kisakcod_engine_call_site(path-sort src/universal/com_files.cpp
    "Sys_FileSystemSortPathPointers(")
kisakcod_engine_call_site(console-read-line src/win32/win_syscon.cpp
    "Sys_ConsoleTryReadLine(")
kisakcod_engine_call_site(vehicle-material-time src/game_mp/g_vehicles_mp.cpp
    "bg::vehicle_material_time::Advance(")

kisakcod_ilp32(kisakcod-actor-grenade-prediction-cache-tests
    actor-grenade-prediction-cache-contracts)

kisakcod_ilp32(kisakcod-actor-grenade-safety-tests
    actor-grenade-safe-target-radius-contracts)

kisakcod_ilp32(kisakcod-hudelem-sort-mp-tests
    hudelem-sort-mp-contracts)

kisakcod_ilp32(kisakcod-hudelem-sort-sp-tests
    hudelem-sort-sp-contracts)

kisakcod_ilp32(kisakcod-missile-layout-mp-compile-tests
    missile-layout-mp-compile-contracts)

kisakcod_ilp32(kisakcod-missile-layout-sp-compile-tests
    missile-layout-sp-compile-contracts)

kisakcod_ilp32(kisakcod-target-table-tests
    target-table-parse-and-layout-contracts)

kisakcod_ilp32(kisakcod-ui-safety-tests
    ui-safety-runtime-contracts)

kisakcod_ilp32(kisakcod-weapon-input-safety-tests
    weapon-input-safety-contracts)

kisakcod_ilp32(kisakcod-weapon-model-safety-tests
    weapon-model-safety-contracts)

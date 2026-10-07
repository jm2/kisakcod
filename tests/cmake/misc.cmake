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

# native64_runtime_layout_test: contract tests for the bead-5 (ki-4omyh)
# RUNTIME_SIZE migration of the runtime-only structs. The headers included
# for real here are the ones whose include chain compiles in a portable TU;
# the production-bound ones (game_mp/g_public_mp.h, game/game_public.h,
# DynEntity/DynEntity_client.h, aim_assist/aim_assist.h) are mirrored
# member-for-member in the TU. Engine includes are SYSTEM so the production
# headers' GNU extensions and unused-parameter warnings do not fight the
# strict test-warning gate on this target.
add_executable(kisakcod-native64-runtime-layout-tests
    native64_runtime_layout_test.cpp)
target_include_directories(
    kisakcod-native64-runtime-layout-tests SYSTEM PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-native64-runtime-layout-tests PRIVATE cxx_std_20)
target_compile_definitions(
    kisakcod-native64-runtime-layout-tests PRIVATE KISAK_MP)
kisakcod_test_warnings(kisakcod-native64-runtime-layout-tests)
set_target_properties(kisakcod-native64-runtime-layout-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME native64-runtime-layout-contracts
    COMMAND kisakcod-native64-runtime-layout-tests)

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

# MSVC CRT spellings the decompiled engine calls directly: _strlwr and the
# basename rename (PLATFORM_POSIX.md's "MSVC CRT names" list). On MSVC hosts
# these are the real CRT names and universal/msvc_crt_compat.h stays guarded
# out; on POSIX hosts the compat header is asserted. The contracts are
# identical.
add_executable(kisakcod-msvc-crt-compat-tests
    msvc_crt_compat_tests.cpp
)
target_include_directories(kisakcod-msvc-crt-compat-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-msvc-crt-compat-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-msvc-crt-compat-tests)
set_target_properties(kisakcod-msvc-crt-compat-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME msvc-crt-compat-contracts
    COMMAND kisakcod-msvc-crt-compat-tests
)
# Engine-owned MSVC-compatible RNG (DETERMINISM.md, bead 9): the production
# universal/com_math.cpp against MSVC's rand stream on every leg. The strict
# warnings apply to the test's own sources; the decompiled TU keeps the engine
# surface. com_math.cpp reaches ode/common.h, which includes <malloc.h> and
# <memory.h>; tests/compat supplies them where the host lacks them (macOS).
add_executable(kisakcod-msvc-rand-shim-tests
    msvc_rand_shim_tests.cpp
    com_math_test_stubs.cpp
    ${SRC_DIR}/universal/com_math.cpp
)
target_include_directories(kisakcod-msvc-rand-shim-tests SYSTEM PRIVATE
    ${SRC_DIR} ${DEPS_DIR})
include(CheckIncludeFileCXX)
check_include_file_cxx("malloc.h" KISAK_HAVE_MALLOC_H)
check_include_file_cxx("memory.h" KISAK_HAVE_MEMORY_H)
if (NOT KISAK_HAVE_MALLOC_H OR NOT KISAK_HAVE_MEMORY_H)
    target_include_directories(kisakcod-msvc-rand-shim-tests SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/compat)
endif()
target_compile_features(kisakcod-msvc-rand-shim-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-msvc-rand-shim-tests PRIVATE Threads::Threads)
if (MSVC)
    set_source_files_properties(msvc_rand_shim_tests.cpp
        com_math_test_stubs.cpp PROPERTIES
        COMPILE_OPTIONS "/W4;/WX")
else()
    set_source_files_properties(msvc_rand_shim_tests.cpp
        com_math_test_stubs.cpp PROPERTIES
        COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
endif()
set_target_properties(kisakcod-msvc-rand-shim-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME msvc-rand-shim-contracts
    COMMAND kisakcod-msvc-rand-shim-tests
)
# The same checks plus the production game_mp G_rand/G_irand helpers
# (g_utils_mp.cpp). Linux and clang only, like the dvar test below: engine
# code. --gc-sections drops what the checks never reach, so no stubs.
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(kisakcod-g-rand-mp-tests msvc_rand_shim_tests.cpp
        ${SRC_DIR}/universal/com_math.cpp ${SRC_DIR}/game_mp/g_utils_mp.cpp)
    target_include_directories(kisakcod-g-rand-mp-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-g-rand-mp-tests PRIVATE cxx_std_20)
    target_compile_definitions(kisakcod-g-rand-mp-tests PRIVATE
        KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS KISAK_RAND_TEST_GAME_MP)
    target_compile_options(kisakcod-g-rand-mp-tests PRIVATE
        -fms-extensions -ffunction-sections -fdata-sections)
    target_link_options(kisakcod-g-rand-mp-tests PRIVATE -Wl,--gc-sections)
    if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address")
        # Otherwise ASan's global registration keeps every global alive.
        target_compile_options(kisakcod-g-rand-mp-tests PRIVATE -fsanitize-address-globals-dead-stripping)
        target_link_options(kisakcod-g-rand-mp-tests PRIVATE -Wl,-z,start-stop-gc)
    endif()
    target_link_libraries(kisakcod-g-rand-mp-tests PRIVATE Threads::Threads)
    set_target_properties(kisakcod-g-rand-mp-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME g-rand-mp-contracts COMMAND kisakcod-g-rand-mp-tests)
    set_tests_properties(g-rand-mp-contracts PROPERTIES TIMEOUT 20)
endif()

# Byte-order helpers of universal/q_shared.h (issue #231: BigShort was
# declared for every target but defined only under WIN32, so the POSIX
# headless server could not link). The constexpr Big*/Little* forms are
# pinned to the retail ShortSwap/LongSwap values on the little-endian
# targets the ABI header accepts.
add_executable(kisakcod-q-shared-byteorder-tests
    q_shared_byteorder_tests.cpp
)
target_include_directories(kisakcod-q-shared-byteorder-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-q-shared-byteorder-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-q-shared-byteorder-tests)
set_target_properties(kisakcod-q-shared-byteorder-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME q-shared-byteorder-contracts
    COMMAND kisakcod-q-shared-byteorder-tests
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

# 64-bit dvar pointer round trips (NOW row 11): the production dvar system
# with strings and enum lists placed above 4 GiB, so a pointer truncated to
# 32 bits is always a wrong pointer. Linux only: dvar.cpp is engine code, and
# the engine compiles off Windows only on Linux so far (macOS stops in the
# Miles and ODE headers until the G3 mac64 work). Clang only: the engine
# follows the clang + -fms-extensions toolchain policy (PLATFORM_POSIX.md).
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(kisakcod-dvar-pointer-tests
        dvar_pointer_tests.cpp
        ${SRC_DIR}/universal/dvar.cpp
    )
    target_include_directories(kisakcod-dvar-pointer-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-dvar-pointer-tests PRIVATE cxx_std_20)
    target_compile_definitions(kisakcod-dvar-pointer-tests PRIVATE KISAK_MP)
    target_compile_options(kisakcod-dvar-pointer-tests PRIVATE -fms-extensions)
    set_target_properties(kisakcod-dvar-pointer-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(NAME dvar-pointer-round-trips COMMAND kisakcod-dvar-pointer-tests)
    set_tests_properties(dvar-pointer-round-trips PROPERTIES TIMEOUT 20)
endif()

# World-sector link lists: the production cm_world.cpp with the Linux headless
# server's defines; --gc-sections drops what the two link calls never reach.
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(kisakcod-cm-world-link-tests cm_world_link_tests.cpp ${SRC_DIR}/qcommon/cm_world.cpp)
    target_include_directories(kisakcod-cm-world-link-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-cm-world-link-tests PRIVATE cxx_std_20)
    target_compile_definitions(kisakcod-cm-world-link-tests PRIVATE
        KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS UNIX)
    target_compile_options(kisakcod-cm-world-link-tests PRIVATE -fms-extensions -ffunction-sections -fdata-sections)
    target_link_options(kisakcod-cm-world-link-tests PRIVATE -Wl,--gc-sections)
    if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address")
        target_compile_options(kisakcod-cm-world-link-tests PRIVATE -fsanitize-address-globals-dead-stripping)
        target_link_options(kisakcod-cm-world-link-tests PRIVATE -Wl,-z,start-stop-gc)
    endif()
    set_target_properties(kisakcod-cm-world-link-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME cm-world-sector-links COMMAND kisakcod-cm-world-link-tests)
    set_tests_properties(cm-world-sector-links PROPERTIES TIMEOUT 20)
endif()

# The adapters the MP GSC builtin table uses (g_public_mp.h). Linux and
# clang only, as above: the game headers are engine code.
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(kisakcod-gsc-function-table-tests gsc_function_table_tests.cpp)
    target_include_directories(kisakcod-gsc-function-table-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-gsc-function-table-tests PRIVATE cxx_std_20)
    target_compile_definitions(kisakcod-gsc-function-table-tests PRIVATE
        KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS UNIX)
    target_compile_options(kisakcod-gsc-function-table-tests PRIVATE -fms-extensions)
    set_target_properties(kisakcod-gsc-function-table-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME gsc-function-table-types COMMAND kisakcod-gsc-function-table-tests)
    set_tests_properties(gsc-function-table-types PROPERTIES TIMEOUT 20)
endif()

# configure_mp.csv parsing and checksum: the production com_playerprofile.cpp
# with the Linux headless server's defines. Linux and clang only, as above;
# --gc-sections drops the profile code no check reaches, so only the
# tokenizer, dvar and error boundary is stubbed.
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_executable(kisakcod-configure-csv-tests
        configure_csv_tests.cpp
        ${SRC_DIR}/qcommon/com_playerprofile.cpp
    )
    target_include_directories(kisakcod-configure-csv-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
    target_compile_features(kisakcod-configure-csv-tests PRIVATE cxx_std_20)
    target_compile_definitions(kisakcod-configure-csv-tests PRIVATE
        KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS UNIX)
    target_compile_options(kisakcod-configure-csv-tests PRIVATE -fms-extensions -ffunction-sections -fdata-sections)
    target_link_options(kisakcod-configure-csv-tests PRIVATE -Wl,--gc-sections)
    if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address")
        target_compile_options(kisakcod-configure-csv-tests PRIVATE -fsanitize-address-globals-dead-stripping)
        target_link_options(kisakcod-configure-csv-tests PRIVATE -Wl,-z,start-stop-gc)
    endif()
    set_target_properties(kisakcod-configure-csv-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
    add_test(NAME configure-csv-parse-and-checksum COMMAND kisakcod-configure-csv-tests)
    set_tests_properties(configure-csv-parse-and-checksum PROPERTIES TIMEOUT 20)
endif()

# game_mp 64-bit layout hazards (NOW row 10, #216): the production game_mp
# TUs at 64-bit, one executable per subject group (see the test's header).
# Linux and clang only, for the reasons the dvar test above gives; the
# defines are the Linux headless server's. --gc-sections drops the engine
# code no check reaches, so only its boundary needs stubs.
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    function(kisakcod_game_mp_hazard_test TEST_NAME SUBJECT)
        set(_target kisakcod-${TEST_NAME}-tests)
        add_executable(${_target} game_mp_hazard_tests.cpp ${SRC_DIR}/universal/com_math.cpp ${ARGN})
        target_include_directories(${_target} SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
        target_compile_features(${_target} PRIVATE cxx_std_20)
        target_compile_definitions(${_target} PRIVATE
            KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS GAME_MP_HAZARD_SUBJECT=${SUBJECT})
        target_compile_options(${_target} PRIVATE -fms-extensions -ffunction-sections -fdata-sections)
        target_link_options(${_target} PRIVATE -Wl,--gc-sections)
        if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address")
            # Otherwise ASan's global registration keeps every global alive.
            target_compile_options(${_target} PRIVATE -fsanitize-address-globals-dead-stripping)
            target_link_options(${_target} PRIVATE -Wl,-z,start-stop-gc)
        endif()
        set_target_properties(${_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME ${TEST_NAME} COMMAND ${_target})
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 20)
    endfunction()
    kisakcod_game_mp_hazard_test(game-mp-layout-hazards 1
        ${SRC_DIR}/game_mp/g_spawn_mp.cpp
        ${SRC_DIR}/game_mp/g_client_script_cmd_mp.cpp
        ${SRC_DIR}/game_mp/g_combat_mp.cpp
        ${SRC_DIR}/game_mp/g_player_corpse_mp.cpp
        ${SRC_DIR}/game_mp/g_vehicles_mp.cpp
        ${SRC_DIR}/game/g_scr_vehicle.cpp)
    kisakcod_game_mp_hazard_test(game-mp-trigger-dispatch 2
        ${SRC_DIR}/game_mp/g_active_mp.cpp)
    kisakcod_game_mp_hazard_test(game-mp-game-data-strides 3
        ${SRC_DIR}/game_mp/g_utils_mp.cpp
        ${SRC_DIR}/game_mp/g_main_mp.cpp
        ${SRC_DIR}/server/sv_game.cpp)
endif()

# 64-bit pointer truncations on headless server paths (WS-3 silent hazards):
# the production TUs at 64-bit, one executable per subject (see the test's
# header). Linux, clang and 64-bit only, for the reasons the dvar test gives;
# the defines are the Linux headless server's.
if (KISAK_PLATFORM STREQUAL "linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
    AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    function(kisakcod_pointer_truncation_test TEST_NAME SUBJECT)
        set(_target kisakcod-${TEST_NAME}-tests)
        add_executable(${_target} pointer_truncation_hazard_tests.cpp ${ARGN})
        target_include_directories(${_target} SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
        target_compile_features(${_target} PRIVATE cxx_std_20)
        target_compile_definitions(${_target} PRIVATE
            KISAK_MP KISAK_DEDICATED DEDICATED KISAK_DEDI_HEADLESS POINTER_TRUNCATION_SUBJECT=${SUBJECT})
        target_compile_options(${_target} PRIVATE -fms-extensions -ffunction-sections -fdata-sections)
        target_link_options(${_target} PRIVATE -Wl,--gc-sections)
        if (CMAKE_CXX_FLAGS MATCHES "-fsanitize=[^ ]*address")
            # Otherwise ASan's global registration keeps every global alive.
            target_compile_options(${_target} PRIVATE -fsanitize-address-globals-dead-stripping)
            target_link_options(${_target} PRIVATE -Wl,-z,start-stop-gc)
        endif()
        set_target_properties(${_target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        add_test(NAME ${TEST_NAME} COMMAND ${_target})
        set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT 20)
    endfunction()
    kisakcod_pointer_truncation_test(truncation-info-validate 1
        ${SRC_DIR}/universal/q_shared.cpp)
endif()

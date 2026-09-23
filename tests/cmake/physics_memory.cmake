# Physics, memory and pool allocator tests.
# Included from tests/CMakeLists.txt.

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

add_executable(kisakcod-phys-obj-id-tests
    phys_obj_id_tests.cpp
    ${SRC_DIR}/bgame/bg_phys_obj_id_tables.cpp
)
target_include_directories(kisakcod-phys-obj-id-tests PRIVATE ${SRC_DIR})
target_compile_features(kisakcod-phys-obj-id-tests PRIVATE cxx_std_20)
target_link_libraries(kisakcod-phys-obj-id-tests PRIVATE Threads::Threads)
kisakcod_test_warnings(kisakcod-phys-obj-id-tests)
set_target_properties(kisakcod-phys-obj-id-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME phys-obj-id-sidecar-contracts
    COMMAND kisakcod-phys-obj-id-tests
)
set_tests_properties(phys-obj-id-sidecar-contracts PROPERTIES TIMEOUT 20)

kisakcod_ilp32(kisakcod-pool-allocator-tests
    pool-native-pointer-freelist)

kisakcod_ilp32(kisakcod-physicalmemory-checked-tests
    universal-physicalmemory-checked-scopes)

kisakcod_ilp32(kisakcod-physicalmemory-legacy-tests
    universal-physicalmemory-legacy-layout-and-indexing)

kisakcod_ilp32(kisakcod-physicalmemory-runtime-tests
    universal-physicalmemory-runtime-control)

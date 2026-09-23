# Renderer value-encoding and shader cache tests.
# Included from tests/CMakeLists.txt.

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

include("${CMAKE_CURRENT_SOURCE_DIR}/renderer_enum_contracts.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/renderer_image_contracts.cmake")
add_executable(kisakcod-renderer-value-encoding-tests
    renderer_value_encoding_tests.cpp
    renderer_enum_contracts.cpp
    renderer_image_contracts.cpp
)
target_include_directories(kisakcod-renderer-value-encoding-tests PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/renderer-enums")
target_include_directories(
    kisakcod-renderer-value-encoding-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-renderer-value-encoding-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-renderer-value-encoding-tests)
set_target_properties(kisakcod-renderer-value-encoding-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(
    NAME renderer-value-encoding-contracts
    COMMAND kisakcod-renderer-value-encoding-tests
)

# A09 / #131 (ki-y49k): content-addressed derived shader cache/sidecar core.
# Original retail bytecode is identified by content hash, the sidecar is
# versioned by format and converter identity, and stale or corrupt evidence is
# regenerated from the untouched original inputs. The portable core is
# exercised on the Linux host. Build enrollment is the database/engine source
# manifest; wiring it into the D3D9 shader creation boundary is future
# renderer integration.
add_executable(kisakcod-shader-cache-tests
    shader_cache_tests.cpp
    ${SRC_DIR}/database/shader_cache.cpp
    ${SRC_DIR}/database/db_graph_hash.cpp)
target_include_directories(
    kisakcod-shader-cache-tests PRIVATE ${SRC_DIR})
target_compile_features(
    kisakcod-shader-cache-tests PRIVATE cxx_std_20)
kisakcod_test_warnings(kisakcod-shader-cache-tests)
set_target_properties(kisakcod-shader-cache-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(
    NAME database-derived-shader-cache-contracts
    COMMAND kisakcod-shader-cache-tests)
set_tests_properties(database-derived-shader-cache-contracts PROPERTIES TIMEOUT 20)

kisakcod_ilp32(kisakcod-shader-cache-tests
    database-derived-shader-cache-contracts)

kisakcod_ilp32(kisakcod-renderer-value-encoding-tests
    renderer-value-encoding-contracts)

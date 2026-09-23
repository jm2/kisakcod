# Tests for curated upstream fixes.
# Included from tests/CMakeLists.txt.

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

# Run both profiles of the curated production regressions in the existing
# upstream reconciliation gate, including its portable/sanitizer/Win32 legs.
include("${CMAKE_CURRENT_SOURCE_DIR}/upstream_b3199_tests.cmake")
foreach(profile IN ITEMS mp sp)
    string(TOUPPER "${profile}" profile_upper)
    add_library(kisakcod-upstream-b3199-${profile}-contracts OBJECT upstream_b3199_contracts.cpp)
    target_include_directories(kisakcod-upstream-b3199-${profile}-contracts PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/upstream-b3199")
    target_compile_definitions(kisakcod-upstream-b3199-${profile}-contracts PRIVATE KISAK_${profile_upper})
    target_compile_features(kisakcod-upstream-b3199-${profile}-contracts PRIVATE cxx_std_20)
    kisakcod_test_warnings(kisakcod-upstream-b3199-${profile}-contracts)
    target_sources(kisakcod-upstream-angle-math-tests PRIVATE $<TARGET_OBJECTS:kisakcod-upstream-b3199-${profile}-contracts>)
endforeach()

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

kisakcod_ilp32(kisakcod-upstream-aim-safety-tests
    upstream-reconciliation-aim-safety-contracts)

kisakcod_ilp32(kisakcod-upstream-angle-math-tests
    upstream-reconciliation-angle-math-contracts)

kisakcod_ilp32(kisakcod-upstream-command-dispatch-tests
    upstream-reconciliation-command-dispatch-contracts)

kisakcod_ilp32(kisakcod-upstream-sort-tests
    upstream-sort-contracts)

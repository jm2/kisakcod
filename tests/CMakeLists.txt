function(kisakcod_test_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

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
    NAME security-source-regressions
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/security_regression_test.cmake
)

# The generator on a test-only schema (tests/disk32_generator.schema): the kinds
# the families in flight need, compiled and run with the fixture's checks.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_disk32_test_dir "${CMAKE_CURRENT_BINARY_DIR}/disk32_generator")
set(_disk32_test_headers
    "${_disk32_test_dir}/database/db_disk32_mirrors.h" "${_disk32_test_dir}/database/db_disk32_loaders.h")
add_custom_command(
    OUTPUT ${_disk32_test_headers}
    COMMAND "${Python3_EXECUTABLE}" "${SCRIPTS_DIR}/gen_disk32.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/disk32_generator.schema" ${_disk32_test_headers}
    DEPENDS "${SCRIPTS_DIR}/gen_disk32.py" "${CMAKE_CURRENT_SOURCE_DIR}/disk32_generator.schema"
    COMMENT "Generating the disk32 generator test headers"
    VERBATIM)
# Only the generated inline code runs, so no engine source links in.
add_executable(kisakcod-db-disk32-generator-tests db_disk32_generator_tests.cpp ${_disk32_test_headers})
target_include_directories(kisakcod-db-disk32-generator-tests PRIVATE "${_disk32_test_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(kisakcod-db-disk32-generator-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-db-disk32-generator-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-db-disk32-generator-tests PRIVATE KISAK_MP)
target_compile_options(kisakcod-db-disk32-generator-tests PRIVATE -fms-extensions -Wall -Wextra -Werror)
set_target_properties(kisakcod-db-disk32-generator-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME database-disk32-generator COMMAND kisakcod-db-disk32-generator-tests)
set_tests_properties(database-disk32-generator PROPERTIES TIMEOUT 20)

# The generated pointer steps on test-only families (tests/disk32_generator_steps.schema),
# with the production stream code, so a record body that fails without raising
# shows what the step itself does.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_disk32_steps_dir "${CMAKE_CURRENT_BINARY_DIR}/disk32_steps")
set(_disk32_steps_headers
    "${_disk32_steps_dir}/database/db_disk32_mirrors.h" "${_disk32_steps_dir}/database/db_disk32_loaders.h")
add_custom_command(
    OUTPUT ${_disk32_steps_headers}
    COMMAND "${Python3_EXECUTABLE}" "${SCRIPTS_DIR}/gen_disk32.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/disk32_generator_steps.schema" ${_disk32_steps_headers}
    DEPENDS "${SCRIPTS_DIR}/gen_disk32.py" "${CMAKE_CURRENT_SOURCE_DIR}/disk32_generator_steps.schema"
    COMMENT "Generating the disk32 pointer-step test headers"
    VERBATIM)
add_executable(kisakcod-db-disk32-steps-tests db_disk32_steps_tests.cpp disk32_fixture.cpp ${_disk32_steps_headers}
    ${SRC_DIR}/database/db_stream_load.cpp ${SRC_DIR}/database/db_stream.cpp ${SRC_DIR}/database/db_relocation.cpp
    ${SRC_DIR}/database/db_zone_stream_ownership.cpp ${SRC_DIR}/database/db_zone_load_context.cpp)
target_include_directories(kisakcod-db-disk32-steps-tests PRIVATE "${_disk32_steps_dir}")
target_include_directories(kisakcod-db-disk32-steps-tests SYSTEM PRIVATE ${SRC_DIR} ${DEPS_DIR})
target_compile_features(kisakcod-db-disk32-steps-tests PRIVATE cxx_std_20)
target_compile_definitions(kisakcod-db-disk32-steps-tests PRIVATE KISAK_MP)
target_compile_options(kisakcod-db-disk32-steps-tests PRIVATE -fms-extensions -Wall -Wextra -Werror)
set_target_properties(kisakcod-db-disk32-steps-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME database-disk32-steps COMMAND kisakcod-db-disk32-steps-tests)
set_tests_properties(database-disk32-steps PROPERTIES TIMEOUT 20)

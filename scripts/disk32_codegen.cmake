# Disk32 mirrors generated from src/database/db_disk32.schema by
# scripts/gen_disk32.py (docs/design/FASTFILE_LOADER.md). The header is built
# into the build tree and never committed (AGENTS.md rule 8). Only 64-bit
# targets and the Linux loader tests use it, so the Windows x86 build needs no
# Python.
set(KISAK_DISK32_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

function(kisakcod_use_disk32_mirrors TARGET_NAME)
    if (NOT TARGET kisakcod-disk32-mirrors)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(_header "${KISAK_DISK32_GENERATED_DIR}/database/db_disk32_mirrors.h")
        add_custom_command(
            OUTPUT "${_header}"
            COMMAND "${Python3_EXECUTABLE}" "${SCRIPTS_DIR}/gen_disk32.py"
                "${SRC_DIR}/database/db_disk32.schema" "${_header}"
            DEPENDS "${SCRIPTS_DIR}/gen_disk32.py" "${SRC_DIR}/database/db_disk32.schema"
            COMMENT "Generating disk32 mirrors from db_disk32.schema"
            VERBATIM)
        add_custom_target(kisakcod-disk32-mirrors DEPENDS "${_header}")
    endif()
    add_dependencies(${TARGET_NAME} kisakcod-disk32-mirrors)
    target_include_directories(${TARGET_NAME} PRIVATE "${KISAK_DISK32_GENERATED_DIR}")
endfunction()

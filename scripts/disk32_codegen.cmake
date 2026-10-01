# Disk32 mirrors and 64-bit family loaders generated from
# src/database/db_disk32.schema and the per-family files it includes
# (src/database/disk32/*.schema) by scripts/gen_disk32.py
# (docs/design/FASTFILE_LOADER.md). The headers are built into the build tree
# and never committed (AGENTS.md rule 8). Only 64-bit targets and the Linux
# loader tests use them, so the Windows x86 build needs no Python.
set(KISAK_DISK32_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

function(kisakcod_use_disk32_mirrors TARGET_NAME)
    if (NOT TARGET kisakcod-disk32-mirrors)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        set(_mirrors "${KISAK_DISK32_GENERATED_DIR}/database/db_disk32_mirrors.h")
        set(_loaders "${KISAK_DISK32_GENERATED_DIR}/database/db_disk32_loaders.h")
        # The root schema's `include disk32/*.schema`; a new family file reconfigures.
        file(GLOB _schemas CONFIGURE_DEPENDS "${SRC_DIR}/database/disk32/*.schema")
        add_custom_command(
            OUTPUT "${_mirrors}" "${_loaders}"
            COMMAND "${Python3_EXECUTABLE}" "${SCRIPTS_DIR}/gen_disk32.py"
                "${SRC_DIR}/database/db_disk32.schema" "${_mirrors}" "${_loaders}"
            DEPENDS "${SCRIPTS_DIR}/gen_disk32.py" "${SRC_DIR}/database/db_disk32.schema" ${_schemas}
            COMMENT "Generating disk32 mirrors and loaders from db_disk32.schema"
            VERBATIM)
        add_custom_target(kisakcod-disk32-mirrors DEPENDS "${_mirrors}" "${_loaders}")
    endif()
    add_dependencies(${TARGET_NAME} kisakcod-disk32-mirrors)
    target_include_directories(${TARGET_NAME} PRIVATE "${KISAK_DISK32_GENERATED_DIR}")
endfunction()

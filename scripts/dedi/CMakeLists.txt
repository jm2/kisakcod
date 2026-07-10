message("----- KisakCOD Dedicated(MP) -----")

project(KisakCOD-dedi)

# Include all common files between mp/sp/dedi
include(${SCRIPTS_DIR}/common_files.cmake)

# Include all files specific to mp
include(${SCRIPTS_DIR}/mp/mp_files.cmake)
include(dedi_files.cmake)
include(dedi_sources.cmake)

# Handle platform configs and overrides
include (${PLATFORM_DIR}/${KISAK_PLATFORM}/platform.cmake)

# Create source groups after platform selection so they reflect overrides and
# the active platform-owned source sets.
include(${SCRIPTS_DIR}/source_groups.cmake)

if (KISAK_DEDI_HEADLESS)
    message(STATUS "Configuring experimental headless dedicated server source profile")
    set(KISAK_TARGET_NEEDS_CLIENT_MEDIA OFF)
else()
    set(KISAK_TARGET_NEEDS_CLIENT_MEDIA ON)
endif()

kisakcod_get_dedi_sources(KISAK_DEDI_SOURCES)
kisakcod_assert_headless_dedi_sources(${KISAK_DEDI_SOURCES})

# Add the new executable
add_executable(${PROJECT_NAME} ${KISAK_DEDI_SOURCES})

target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_MP KISAK_DEDICATED DEDICATED)
if (KISAK_DEDI_HEADLESS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_DEDI_HEADLESS)
endif()

# We need all of the pre/post-build commands
include(${SCRIPTS_DIR}/pre_build.cmake)
include(${SCRIPTS_DIR}/post_build.cmake)

# Tracey
include(${SCRIPTS_DIR}/extern/tracy.cmake)

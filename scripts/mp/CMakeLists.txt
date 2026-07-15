message("----- KisakCOD Client(MP) -----")

# Set project name to mp
project(KisakCOD-mp)

# Include all common files between mp/sp/dedi
include(${SCRIPTS_DIR}/common_files.cmake)

# Include all files specific to mp
include(mp_files.cmake)

# message(${SRC_DIR})
include (${PLATFORM_DIR}/${KISAK_PLATFORM}/platform.cmake)

# Create source groups after platform selection so they reflect overrides and
# the active platform-owned source sets.
include(${SCRIPTS_DIR}/source_groups.cmake)
kisakcod_require_platform_source_sets()

# Add the new executable
add_executable(${PROJECT_NAME} 
    ${SRCROOT} 
    ${DYNENTITY} 
    ${EFFECTSCORE} 
    ${AIM_ASSIST} 
    ${BGAME} 
    ${BINKLIB} 
    ${CGAME} 
    ${CGAME_MP} 
    ${CLIENT} 
    ${CLIENT_MP} 
    ${COMMON} 
    ${DATABASE} 
    ${DEVGUI} 
    ${GAME} 
    ${GAME_MP} 
    ${GFX_D3D} 
    ${GROUPVOICE} 
    ${GROUPVOICE_SPEEX} 
    ${MSSLIB} 
    ${ODE} 
    ${PHYSICS} 
    ${PHYSICS_ODE} 
    ${QCOMMON} 
    ${RAGDOLL} 
    ${SCRIPT} 
    ${SERVER} 
    ${SERVER_MP} 
    ${SOUND} 
    ${SPEEX} 
    ${STRINGED} 
    ${UI} 
    ${UI_MP} 
    ${UNIVERSAL} 
    ${KISAK_PLATFORM_SOURCES}
    ${KISAK_PLATFORM_SERVICE_SOURCES}
    ${XANIM} 
    ${ZLIB} 
    ${STEAM_HEADERS}
)

if (KISAK_MEASURE_FX_ARCHIVE_STACK)
    include(${SCRIPTS_DIR}/stack_budget.cmake)
    kisakcod_enable_fx_archive_stack_measurement(${PROJECT_NAME})
endif()

# Add multiplayer flag
target_compile_definitions(${PROJECT_NAME} PUBLIC KISAK_MP CINEMA USE_SEPARATE_BLIT_TEXTURE)
set(KISAK_TARGET_NEEDS_CLIENT_MEDIA ON)

# We need all of the pre/post-build commands
include(${SCRIPTS_DIR}/pre_build.cmake)
include(${SCRIPTS_DIR}/post_build.cmake)

if (USE_ASAN)
    target_compile_options(${PROJECT_NAME} PRIVATE /fsanitize=address)
    target_link_options(${PROJECT_NAME} PRIVATE /fsanitize=address /LARGEADDRESSAWARE)
else()
# Tracy
    include(${SCRIPTS_DIR}/extern/tracy.cmake)
endif()

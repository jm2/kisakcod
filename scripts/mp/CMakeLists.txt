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

# The DirectSound voice backend is Win32's: only win32/win_voice.cpp calls it.
if (NOT KISAK_PLATFORM STREQUAL "win32")
    list(REMOVE_ITEM GROUPVOICE
        "${SRC_DIR}/groupvoice/play_dsound.cpp"
        "${SRC_DIR}/groupvoice/record_dsound.cpp")
endif()

# Off Win32 the client takes the portable datagram layer and the remote-debug
# stub, as the headless server does (scripts/dedi/dedi_sources.cmake).
if (NOT KISAK_PLATFORM STREQUAL "win32")
    list(APPEND QCOMMON
        "${SRC_DIR}/qcommon/net_local.cpp"
        "${SRC_DIR}/qcommon/net_debug_stub.cpp"
        # Persistent stats (LiveStorage_*): portable engine code on the FS_*
        # API, kept beside its header in win32/ where the shared code includes it.
        "${SRC_DIR}/win32/win_storage.cpp")
endif()

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

# Render in physical pixels on high-DPI displays (#918). Retail declares no DPI
# awareness; the client deviates at the owner's request. The dedi stays out.
if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE "${SCRIPTS_DIR}/platform/win32/dpi_aware.manifest")
endif()

# The macOS client renders on KosmicKrisp, which needs macOS 26+, so the
# client targets 26.0; the server keeps the global CMAKE_OSX_DEPLOYMENT_TARGET
# (13.0). The later -mmacosx-version-min wins on clang's command line.
if (APPLE)
    target_compile_options(${PROJECT_NAME} PRIVATE -mmacosx-version-min=26.0)
    target_link_options(${PROJECT_NAME} PRIVATE -mmacosx-version-min=26.0)
endif()
# The engine bundles zlib and speex and has globals such as g_log. On ELF, a
# definition in the executable that a linked shared library also references is
# exported and interposes the system copy for every library in the process (a
# distro FFmpeg pulls in zlib, and through it glib; libdecor-gtk's g_log() call
# then jumps into the engine's g_log variable). Export nothing.
if (UNIX AND NOT APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,--version-script=${SCRIPTS_DIR}/platform/client_exports.map")
    # clang links its sanitizer runtime into the executable, where the map
    # above hides its malloc and operator new from shared libraries: libstdc++
    # then allocates from glibc and ASan reports the free as a bad free. The
    # shared runtime exports them itself.
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
        target_link_options(${PROJECT_NAME} PRIVATE -shared-libsan -frtlib-add-rpath)
    endif()
endif()
set(KISAK_TARGET_NEEDS_CLIENT_MEDIA ON)

if (KISAK_CLIENT_SDL3)
    include(${SCRIPTS_DIR}/extern/sdl3.cmake)
endif()

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

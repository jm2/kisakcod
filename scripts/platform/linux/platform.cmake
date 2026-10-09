set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/linux")

include("${SCRIPTS_DIR}/platform_sources.cmake")

foreach(_source_set CLIENT_MP SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

# The Linux engine backend is incomplete: explicit source sets keep it from
# inheriting Win32 files, and the MP client configures only with
# KISAK_EXPERIMENTAL_POSIX_CLIENT. Portable services remain independently
# buildable and runtime-tested.
# The engine (client) set: the system layer, console and language selection
# shared with the headless server. The client's entry point, event pump,
# window and input come with the SDL3 client layer (KISAK_CLIENT_SDL3).
set(PLATFORM_LINUX
    "${SRC_DIR}/_platform/posix/posix_localize.cpp"
    "${SRC_DIR}/_platform/posix/posix_sys.cpp"
    "${SRC_DIR}/_platform/posix/posix_sys.h"
    "${SRC_DIR}/_platform/posix/posix_syscon.cpp"
)
# The POSIX headless dedicated composition: entry point and frame loop,
# termios console, language selection and CPU description. The datagram layer
# and the remote-debug stub are shared with every platform and come from
# scripts/dedi/dedi_sources.cmake (docs/design/PLATFORM_POSIX.md, NOW row 13).
set(PLATFORM_LINUX_DEDI_HEADLESS
    "${SRC_DIR}/_platform/posix/posix_localize.cpp"
    "${SRC_DIR}/_platform/posix/posix_main.cpp"
    "${SRC_DIR}/_platform/posix/posix_sys.cpp"
    "${SRC_DIR}/_platform/posix/posix_sys.h"
    "${SRC_DIR}/_platform/posix/posix_syscon.cpp"
)
set(PLATFORM_LINUX_SERVICES
    "${SRC_DIR}/_platform/posix/sys_console.cpp"
    "${SRC_DIR}/_platform/posix/sys_event.cpp"
    "${SRC_DIR}/_platform/posix/sys_file.cpp"
    "${SRC_DIR}/_platform/posix/sys_filesystem.cpp"
    "${SRC_DIR}/_platform/posix/sys_memory.cpp"
    "${SRC_DIR}/_platform/posix/sys_process.cpp"
    "${SRC_DIR}/_platform/posix/sys_socket.cpp"
    "${SRC_DIR}/_platform/posix/sys_sync.cpp"
    "${SRC_DIR}/_platform/posix/sys_thread.cpp"
    "${SRC_DIR}/_platform/posix/sys_time.cpp"
)
kisakcod_select_platform_source_sets(
    PLATFORM linux
    ENGINE_VAR PLATFORM_LINUX
    DEDI_HEADLESS_VAR PLATFORM_LINUX_DEDI_HEADLESS
    SERVICES_VAR PLATFORM_LINUX_SERVICES
    COMPLETE FALSE
)

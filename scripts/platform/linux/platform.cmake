set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/linux")

include("${SCRIPTS_DIR}/platform_sources.cmake")

foreach(_source_set CLIENT_MP SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

# The Linux engine backend is intentionally incomplete.  Empty, explicit
# source sets prevent it from inheriting Win32 files while the top-level engine
# configuration gate remains in force. Portable services remain independently
# buildable and runtime-tested.
set(PLATFORM_LINUX "")
# The POSIX headless dedicated composition: entry point and frame loop,
# termios console, language selection and CPU description. The datagram layer
# and the remote-debug stub are shared with every platform and come from
# scripts/dedi/dedi_sources.cmake (docs/design/PLATFORM_POSIX.md, NOW row 13).
set(PLATFORM_LINUX_DEDI_HEADLESS
    "${SRC_DIR}/_platform/posix/posix_localize.cpp"
    "${SRC_DIR}/_platform/posix/posix_main.cpp"
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

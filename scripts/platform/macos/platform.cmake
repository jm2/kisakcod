set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/macos")

include("${SCRIPTS_DIR}/platform_sources.cmake")

foreach(_source_set CLIENT_MP SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

# The macOS engine backend is intentionally incomplete.  Empty, explicit
# source sets prevent it from inheriting Win32 files while the top-level engine
# configuration gate remains in force. Portable services remain independently
# buildable and runtime-tested.
set(PLATFORM_MACOS "")
# The POSIX headless dedicated composition, shared with Linux: entry point and
# frame loop, termios console, language selection and CPU description
# (docs/design/PLATFORM_POSIX.md, NOW row 13).
set(PLATFORM_MACOS_DEDI_HEADLESS
    "${SRC_DIR}/_platform/posix/posix_localize.cpp"
    "${SRC_DIR}/_platform/posix/posix_main.cpp"
    "${SRC_DIR}/_platform/posix/posix_syscon.cpp"
)
set(PLATFORM_MACOS_SERVICES
    "${SRC_DIR}/_platform/macos/sys_mach_crash.cpp"
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
    PLATFORM macos
    ENGINE_VAR PLATFORM_MACOS
    DEDI_HEADLESS_VAR PLATFORM_MACOS_DEDI_HEADLESS
    SERVICES_VAR PLATFORM_MACOS_SERVICES
    COMPLETE FALSE
)

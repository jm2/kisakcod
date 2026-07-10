set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/linux")

include("${SCRIPTS_DIR}/platform_sources.cmake")

foreach(_source_set CLIENT_MP SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

# The Linux engine backend is intentionally incomplete.  Empty, explicit
# source sets prevent it from inheriting Win32 files while the top-level engine
# configuration gate remains in force.
set(PLATFORM_LINUX "")
set(PLATFORM_LINUX_DEDI_HEADLESS "")
set(PLATFORM_LINUX_SERVICES
    "${SRC_DIR}/_platform/posix/sys_sync.cpp"
    "${SRC_DIR}/_platform/posix/sys_time.cpp"
)
kisakcod_select_platform_source_sets(
    PLATFORM linux
    ENGINE_VAR PLATFORM_LINUX
    DEDI_HEADLESS_VAR PLATFORM_LINUX_DEDI_HEADLESS
    SERVICES_VAR PLATFORM_LINUX_SERVICES
    COMPLETE FALSE
)

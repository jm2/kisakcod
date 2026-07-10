set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/macos")

include("${SCRIPTS_DIR}/platform_sources.cmake")

foreach(_source_set CLIENT_MP SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

# The macOS engine backend is intentionally incomplete.  Empty, explicit
# source sets prevent it from inheriting Win32 files while the top-level engine
# configuration gate remains in force.
set(PLATFORM_MACOS "")
set(PLATFORM_MACOS_DEDI_HEADLESS "")
set(PLATFORM_MACOS_SERVICES "")
kisakcod_select_platform_source_sets(
    PLATFORM macos
    ENGINE_VAR PLATFORM_MACOS
    DEDI_HEADLESS_VAR PLATFORM_MACOS_DEDI_HEADLESS
    SERVICES_VAR PLATFORM_MACOS_SERVICES
    COMPLETE FALSE
)

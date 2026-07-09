set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/macos")

foreach(_source_set CLIENT_MP PLATFORM_WIN32 SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

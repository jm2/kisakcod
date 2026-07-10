# Set the platform override directory
set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/win32")

include("${SCRIPTS_DIR}/platform_sources.cmake")

# Apply overrides to platform-coupled source sets. Missing overrides retain the
# original source, allowing the tree to be converted incrementally.
foreach(_source_set CLIENT_MP PLATFORM_WIN32 SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

# Keep the currently buildable Windows lists exact.  Platform service backends
# will move into the explicit service set as their contracts are extracted.
set(PLATFORM_WIN32_SERVICES "")
kisakcod_select_platform_source_sets(
    PLATFORM win32
    ENGINE_VAR PLATFORM_WIN32
    DEDI_HEADLESS_VAR PLATFORM_WIN32_DEDI_HEADLESS
    SERVICES_VAR PLATFORM_WIN32_SERVICES
    COMPLETE TRUE
)

if (MSVC)
    add_compile_options(/MP /W3 /permissive-)
endif()

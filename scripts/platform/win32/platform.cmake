# Set the platform override directory
set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/win32")

include("${SCRIPTS_DIR}/platform_sources.cmake")

# Apply overrides to platform-coupled source sets. Missing overrides retain the
# original source, allowing the tree to be converted incrementally.
foreach(_source_set CLIENT_MP PLATFORM_WIN32 SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

# Keep the currently buildable Windows engine lists exact while selecting the
# native event, synchronization, thread-lifecycle, and time implementations explicitly.
set(PLATFORM_WIN32_SERVICES
    "${SRC_DIR}/_platform/win32/sys_event.cpp"
    "${SRC_DIR}/_platform/win32/sys_memory.cpp"
    "${SRC_DIR}/_platform/win32/sys_sync.cpp"
    "${SRC_DIR}/_platform/win32/sys_thread.cpp"
    "${SRC_DIR}/_platform/win32/sys_time.cpp"
)
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

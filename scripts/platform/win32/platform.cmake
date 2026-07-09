# Set the platform override directory
set(PLATFORM_OVERRIDE_DIR "${SRC_DIR}/_platform/win32")

# Apply overrides to platform-coupled source sets. Missing overrides retain the
# original source, allowing the tree to be converted incrementally.
foreach(_source_set CLIENT_MP PLATFORM_WIN32 SOUND GFX_D3D GROUPVOICE)
    apply_platform_overrides(${_source_set} "${PLATFORM_OVERRIDE_DIR}")
endforeach()

if (MSVC)
    add_compile_options(/MP /W3 /permissive-)
endif()

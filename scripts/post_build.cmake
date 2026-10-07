# The executables share one output directory (CMAKE_RUNTIME_OUTPUT_DIRECTORY),
# so the runtime DLLs are copied by one custom target per dependency set that
# every executable needing them depends on. Per-executable POST_BUILD copies
# into the same directory raced when KisakCOD-mp and KisakCOD-dedi linked in
# parallel.
get_property(_kisak_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (_kisak_multi_config)
    set(_kisak_runtime_dir "${BIN_DIR}/$<CONFIG>")
else()
    set(_kisak_runtime_dir "${BIN_DIR}")
endif()

if (WIN32 AND KISAK_TARGET_NEEDS_CLIENT_MEDIA)
    if (NOT TARGET kisakcod-copy-miles)
        add_custom_target(kisakcod-copy-miles
            # Keep the CMake 3.16-compatible copy primitive. Upstream's
            # copy_directory_if_different requires a newer CMake than this
            # project declares.
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DEPS_DIR}/msslib/dlls"
                "${_kisak_runtime_dir}"
            COMMENT "Copying Miles dependencies"
        )
    endif()
    add_dependencies(${PROJECT_NAME} kisakcod-copy-miles)
endif()

if (WIN32 AND KISAK_TARGET_ENABLE_STEAM)
    if (NOT TARGET kisakcod-copy-steam)
        add_custom_target(kisakcod-copy-steam
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_kisak_runtime_dir}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DEPS_DIR}/steamsdk/steam_api.dll"
                "${_kisak_runtime_dir}"
            COMMENT "Copying Steam dependency"
        )
    endif()
    add_dependencies(${PROJECT_NAME} kisakcod-copy-steam)
endif()

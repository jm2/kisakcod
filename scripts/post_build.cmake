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

# The client's OpenAL Soft and FFmpeg DLLs, from vcpkg (scripts/client_media.cmake).
# vcpkg's own per-executable copy is off (CMakeLists.txt) for the same race.
if (WIN32 AND KISAK_TARGET_NEEDS_CLIENT_MEDIA)
    if (NOT TARGET kisakcod-copy-media)
        add_custom_target(kisakcod-copy-media
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
                "${_kisak_runtime_dir}"
            COMMENT "Copying OpenAL Soft and FFmpeg"
        )
    endif()
    add_dependencies(${PROJECT_NAME} kisakcod-copy-media)
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

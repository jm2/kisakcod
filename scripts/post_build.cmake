if (WIN32 AND KISAK_TARGET_NEEDS_CLIENT_MEDIA)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        # Keep the CMake 3.16-compatible copy primitive. Upstream's
        # copy_directory_if_different requires a newer CMake than this
        # project declares and is therefore deferred to a dedicated,
        # compatibility-preserving post-build synchronization change.
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DEPS_DIR}/msslib/dlls"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying Miles dependencies"
    )

endif()

if (WIN32 AND KISAK_TARGET_ENABLE_STEAM)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DEPS_DIR}/steamsdk/steam_api.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying Steam dependency"
    )
endif()

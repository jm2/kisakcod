if (WIN32 AND KISAK_TARGET_NEEDS_CLIENT_MEDIA)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        # KISAK (ki-gu2, upstream 35e8c797): copy_directory_if_different, NOT
        # copy_directory - every target (mp/sp/dedi) runs this into the same bin
        # dir, so unconditional copies race each other during parallel builds
        # (and fail outright if the game is running with the DLLs loaded).
        # Skipping unchanged files avoids the write entirely.
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
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

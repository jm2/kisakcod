if (WIN32)
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DEPS_DIR}/msslib/dlls"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying Miles dependencies"
    )

    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DEPS_DIR}/steamsdk/steam_api.dll"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        COMMENT "Copying Steam dependency"
    )
endif()

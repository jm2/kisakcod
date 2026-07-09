if (WIN32)
  set(SCRIPT_EXT .cmd)
else()
  set(SCRIPT_EXT .sh)
endif()

# Get the current git commit count and save it to GIT_COMMIT_COUNT
execute_process(
  COMMAND git rev-list --count HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_COUNT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if (NOT GIT_COMMIT_COUNT MATCHES "^[0-9]+$")
  set(GIT_COMMIT_COUNT 0)
endif()

# Add a custom target to increment the build number
add_custom_target(
  update_build_number
  COMMAND "${SCRIPTS_DIR}/increment_build${SCRIPT_EXT}" "${SRC_DIR}" "${GIT_COMMIT_COUNT}"
  COMMENT "Running build number script..."
)

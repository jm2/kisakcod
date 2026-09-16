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

# Resolve the immutable source commit. In a checkout this is HEAD; in a `git
# archive` source tree without `.git` it is the substituted
# src/source_identity.txt, so archive builds keep a recoverable revision
# identity instead of silently losing it. Passed through to the generated
# src/buildnumber.h as KISAK_SOURCE_COMMIT.
include("${SCRIPTS_DIR}/extern/resolve_source_identity.cmake")
kisak_resolve_source_identity("${CMAKE_SOURCE_DIR}" KISAK_RESOLVED_SOURCE_COMMIT)

# Add a custom target to increment the build number
add_custom_target(
  update_build_number
  COMMAND "${SCRIPTS_DIR}/increment_build${SCRIPT_EXT}" "${SRC_DIR}" "${GIT_COMMIT_COUNT}" "${KISAK_RESOLVED_SOURCE_COMMIT}"
  COMMENT "Running build number script..."
)

# Build-time stamping for the update_build_number target.
#
# The build number and the source identity must describe the tree AS IT IS when
# the stamp target runs, not when CMake last configured. A reused build
# directory after a commit or checkout that changes no CMake input never
# re-runs CMake, so resolving the identity only at configure time stamps
# freshly built binaries with a stale revision. add_custom_target is always
# out-of-date, so CMake executes this script on every build and both values are
# resolved here, at build time:
#   - BUILD_NUMBER: `git rev-list --count HEAD` of the source tree.
#   - KISAK_SOURCE_COMMIT: scripts/extern/resolve_source_identity.cmake, whose
#     precedence is unchanged: an explicit KISAK_SOURCE_COMMIT override (cache
#     variable or environment) wins, then the working checkout's HEAD, then the
#     substituted src/source_identity.txt carrier of a git-free source archive.
# The platform increment_build script writes buildnumber.h. A stamp failure
# must fail the build rather than silently leave a stale header in place.
#
# Inputs (passed by scripts/extern/increment_build.cmake, script mode has no
# project context of its own):
#   KISAK_STAMP_SOURCE_DIR - the source tree to resolve the identity against.
#   KISAK_STAMP_SRC_DIR    - the directory that receives buildnumber.h.
#   KISAK_STAMP_SCRIPTS_DIR - this project's scripts directory.
#   KISAK_STAMP_PUBLISH_HEADER - the published buildnumber.h the publish edge
#     owns; see the coarse-clock guard at the end of this script.

if(NOT DEFINED KISAK_STAMP_SOURCE_DIR OR KISAK_STAMP_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR
        "stamp_build_number.cmake requires KISAK_STAMP_SOURCE_DIR")
endif()
if(NOT DEFINED KISAK_STAMP_SRC_DIR OR KISAK_STAMP_SRC_DIR STREQUAL "")
    message(FATAL_ERROR
        "stamp_build_number.cmake requires KISAK_STAMP_SRC_DIR")
endif()
if(NOT DEFINED KISAK_STAMP_SCRIPTS_DIR OR KISAK_STAMP_SCRIPTS_DIR STREQUAL "")
    message(FATAL_ERROR
        "stamp_build_number.cmake requires KISAK_STAMP_SCRIPTS_DIR")
endif()

if(CMAKE_HOST_WIN32)
    set(SCRIPT_EXT .cmd)
else()
    set(SCRIPT_EXT .sh)
endif()

execute_process(
    COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY "${KISAK_STAMP_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _count_result
)
if(NOT _count_result EQUAL 0 OR NOT GIT_COMMIT_COUNT MATCHES "^[0-9]+$")
    set(GIT_COMMIT_COUNT 0)
endif()

include("${KISAK_STAMP_SCRIPTS_DIR}/extern/resolve_source_identity.cmake")
kisak_resolve_source_identity("${KISAK_STAMP_SOURCE_DIR}" KISAK_RESOLVED_SOURCE_COMMIT)

if(SCRIPT_EXT STREQUAL ".cmd")
    # .cmd files are shell scripts: Windows must run them through cmd.exe.
    execute_process(
        COMMAND cmd /c call
            "${KISAK_STAMP_SCRIPTS_DIR}/increment_build.cmd"
            "${KISAK_STAMP_SRC_DIR}" "${GIT_COMMIT_COUNT}" "${KISAK_RESOLVED_SOURCE_COMMIT}"
        RESULT_VARIABLE _stamp_result
    )
else()
    execute_process(
        COMMAND "${KISAK_STAMP_SCRIPTS_DIR}/increment_build.sh"
            "${KISAK_STAMP_SRC_DIR}" "${GIT_COMMIT_COUNT}" "${KISAK_RESOLVED_SOURCE_COMMIT}"
        RESULT_VARIABLE _stamp_result
    )
endif()
if(NOT _stamp_result EQUAL 0)
    message(FATAL_ERROR
        "increment_build${SCRIPT_EXT} failed with exit code ${_stamp_result}; "
        "buildnumber.h was not updated")
endif()

# Coarse-clock publish guard. Build tools do not agree on timestamp
# resolution: Apple's GNU Make 3.81 (the default generator on macOS CI)
# compares whole seconds, so when the stamp rewrites the staged header in the
# same wall-clock second in which the previous build published the old
# revision, the publish edge's mtime comparison ties and the copy is skipped -
# the stale revision survives one ordinary build. Remove the published header
# when the stamped content differs from it: a missing output forces the
# publish edge to run on every generator, and the recreated file re-dirties
# compiled consumers in the same ordinary build regardless of clock
# resolution. An unchanged stamp compares equal and removes nothing, so
# unchanged rebuilds still leave the published header - and every consumer -
# untouched.
if(DEFINED KISAK_STAMP_PUBLISH_HEADER AND NOT KISAK_STAMP_PUBLISH_HEADER STREQUAL "")
    if(EXISTS "${KISAK_STAMP_PUBLISH_HEADER}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${KISAK_STAMP_SRC_DIR}/buildnumber.h"
                "${KISAK_STAMP_PUBLISH_HEADER}"
            RESULT_VARIABLE _publish_compare_result
        )
        if(NOT _publish_compare_result EQUAL 0)
            file(REMOVE "${KISAK_STAMP_PUBLISH_HEADER}")
        endif()
    endif()
endif()

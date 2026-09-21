cmake_minimum_required(VERSION 3.16)

# Source-archive identity contract.
#
# The build must be able to record which revision produced it. A `git archive`
# source tarball has no `.git`, so the revision travels in the substituted
# src/source_identity.txt (src/.gitattributes marks it `export-subst`), and
# scripts/extern/resolve_source_identity.cmake prefers an explicit override,
# then a working checkout, then that carrier. These checks execute the resolver
# against controlled trees, pin the wiring that carries the resolved commit into
# the generated src/buildnumber.h, and prove the accessor's revision bytes
# survive release dead-code elimination in a compiled artifact.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_resolver "${SOURCE_ROOT}/scripts/extern/resolve_source_identity.cmake")
if(NOT EXISTS "${_resolver}")
    message(FATAL_ERROR "Missing source-identity resolver: ${_resolver}")
endif()
include("${_resolver}")

function(read_normalized PATH OUT_VARIABLE DESCRIPTION)
    if(NOT EXISTS "${PATH}")
        message(FATAL_ERROR
            "Missing source-identity input (${DESCRIPTION}): ${PATH}")
    endif()
    file(READ "${PATH}" _source)
    string(REGEX REPLACE "[ \t\r\n]+" " " _source "${_source}")
    set(${OUT_VARIABLE} "${_source}" PARENT_SCOPE)
endfunction()

function(require_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing source-identity invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VARIABLE NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VARIABLE}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Rejected source-identity pattern (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

# The ASCII-hex encoding of a commit string, matching ``file(READ ... HEX)`` so
# a compiled artifact can be searched for the exact revision bytes without
# decoding the whole binary. Commits are lowercase ``[0-9a-f]``, so each ASCII
# byte encodes as ``3``+digit or ``6``+letter.
function(commit_bytes_hex COMMIT OUT_VARIABLE)
    set(_lookup "0123456789abcdef")
    string(TOLOWER "${COMMIT}" _commit)
    set(_hex "")
    string(LENGTH "${_commit}" _length)
    math(EXPR _last "${_length} - 1")
    foreach(_index RANGE 0 ${_last})
        string(SUBSTRING "${_commit}" ${_index} 1 _character)
        string(FIND "${_lookup}" "${_character}" _nibble)
        if(_nibble LESS 10)
            # '0'..'9' are ASCII 0x30..0x39: high nibble 3, low nibble the digit.
            set(_high "3")
            set(_low "${_nibble}")
        else()
            # 'a'..'f' are ASCII 0x61..0x66: high nibble 6, low nibble 1..6.
            set(_high "6")
            math(EXPR _low "${_nibble} - 9")
        endif()
        string(SUBSTRING "${_lookup}" ${_low} 1 _low_character)
        string(APPEND _hex "${_high}${_low_character}")
    endforeach()
    set(${OUT_VARIABLE} "${_hex}" PARENT_SCOPE)
endfunction()

# Require (@PRESENT TRUE) or forbid (@PRESENT FALSE) the exact revision bytes in
# a linked artifact.
function(require_identity_bytes PATH COMMIT PRESENT DESCRIPTION)
    file(READ "${PATH}" _artifact_hex HEX)
    commit_bytes_hex("${COMMIT}" _needle)
    string(FIND "${_artifact_hex}" "${_needle}" _position)
    if(PRESENT)
        if(_position EQUAL -1)
            message(FATAL_ERROR
                "The release-linked artifact lost its source identity "
                "(${DESCRIPTION}): '${COMMIT}' is absent from ${PATH}")
        endif()
    elseif(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "The negative-retention control kept its source identity "
            "(${DESCRIPTION}): '${COMMIT}' is present in ${PATH}")
    endif()
endfunction()

# Prove the resolved commit reaches a compiled artifact from a git-free source
# tree, and that release dead-code elimination cannot discard it. The real
# src/buildnumber.cpp is compiled against a header stamped the same way
# increment_build.sh/.cmd stamp it, with the release optimization surface the
# shipped binaries use: /Gy plus /OPT:REF on MSVC, and
# -ffunction-sections/-fdata-sections plus --gc-sections on ELF. The fixture
# caller deliberately uses only the production consumers (getBuildNumber and
# getBuildNumberAsInt); it never calls getSourceCommit(), so the identity can
# only survive through the retention mechanism defined beside the accessor. A
# second fixture built from a copy with the retention markers removed is the
# negative control: under the same optimization it must lose the bytes, which
# proves the presence assertion is sensitive to the fix.
function(check_compiled_identity)
    if(NOT DEFINED KISAK_TEST_CXX_COMPILER OR KISAK_TEST_CXX_COMPILER STREQUAL "")
        message(STATUS "No C++ compiler provided; skipping the compiled-identity case")
        return()
    endif()
    set(_tree "${_test_root}/compile-tree")
    file(MAKE_DIRECTORY "${_tree}/src/universal")
    file(COPY "${SOURCE_ROOT}/src/buildnumber.cpp" DESTINATION "${_tree}/src")
    file(COPY "${SOURCE_ROOT}/src/universal/platform_compat.h"
        DESTINATION "${_tree}/src/universal")
    file(WRITE "${_tree}/src/source_identity.txt" "commit=${_archive_commit}\n")
    kisak_resolve_source_identity("${_tree}" _resolved)
    if(NOT _resolved STREQUAL "${_archive_commit}")
        message(FATAL_ERROR
            "The git-free compile tree did not resolve its carrier: expected "
            "'${_archive_commit}', found '${_resolved}'")
    endif()
    # The stripped copy is the negative control: identical translation unit with
    # every retention mechanism removed.
    file(READ "${SOURCE_ROOT}/src/buildnumber.cpp" _buildnumber_source)
    foreach(_retention_marker IN ITEMS
        "__attribute__((retain))"
        "#pragma comment(linker, \"/include:_getSourceCommit\")"
        "#pragma comment(linker, \"/include:getSourceCommit\")")
        string(REPLACE "${_retention_marker}" "" _buildnumber_source
            "${_buildnumber_source}")
    endforeach()
    file(WRITE "${_tree}/src/buildnumber_stripped.cpp" "${_buildnumber_source}")
    file(WRITE "${_tree}/src/buildnumber.h"
        "#pragma once\n"
        "#define BUILD_NUMBER 1\n"
        "#define KISAK_SOURCE_COMMIT \"${_resolved}\"\n"
        "\n"
        "char* getBuildNumber();\n"
        "int getBuildNumberAsInt();\n"
        "extern \"C\" const char* getSourceCommit();\n")
    file(WRITE "${_tree}/main.cpp"
        "#include <cstdio>\n"
        "#include \"buildnumber.h\"\n"
        "// Production consumers only: getSourceCommit() is deliberately not\n"
        "// called, so the identity can only survive through the production\n"
        "// retention mechanism, never a test-only reference.\n"
        "int main() {\n"
        "    std::printf(\"%d %s\\n\", getBuildNumberAsInt(), getBuildNumber());\n"
        "    return 0;\n"
        "}\n")
    # Build the fixture through the configured CMake toolchain/generator rather
    # than executing the compiler directly. A Visual Studio generator drives
    # cl.exe through MSBuild, which initializes the compiler's INCLUDE/LIB
    # environment; a bare cl.exe spawned from `cmake -P` inherits neither and
    # fails with C1083 (cannot open <stdio.h>). The nested project compiles the
    # same real src/buildnumber.cpp, so the exact byte assertion still runs
    # against a genuine compiled consumer on every platform.
    file(WRITE "${_tree}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.16)\n"
        "project(kisak_archive_identity_fixture CXX)\n"
        "add_executable(identity-retained\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/src/buildnumber.cpp\"\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/main.cpp\")\n"
        "add_executable(identity-stripped\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/src/buildnumber_stripped.cpp\"\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/main.cpp\")\n"
        "foreach(_fixture IN ITEMS identity-retained identity-stripped)\n"
        "    target_include_directories(\${_fixture} PRIVATE\n"
        "        \"\${CMAKE_CURRENT_SOURCE_DIR}/src\")\n"
        "    set_target_properties(\${_fixture} PROPERTIES\n"
        "        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)\n"
        "    if(MSVC)\n"
        "        target_compile_options(\${_fixture} PRIVATE \"\$<\$<CONFIG:Release>:/Gy>\")\n"
        "        target_link_options(\${_fixture} PRIVATE\n"
        "            \"\$<\$<CONFIG:Release>:/OPT:REF>\"\n"
        "            \"\$<\$<CONFIG:Release>:/OPT:ICF>\")\n"
        "    else()\n"
        "        target_compile_options(\${_fixture} PRIVATE\n"
        "            \"\$<\$<CONFIG:Release>:-ffunction-sections>\"\n"
        "            \"\$<\$<CONFIG:Release>:-fdata-sections>\")\n"
        "        if(NOT APPLE)\n"
        "            target_link_options(\${_fixture} PRIVATE\n"
        "                \"\$<\$<CONFIG:Release>:-Wl,--gc-sections>\")\n"
        "        endif()\n"
        "    endif()\n"
        "endforeach()\n")
    set(_build "${_tree}/build")
    set(_configure_args "-S" "${_tree}" "-B" "${_build}")
    if(DEFINED KISAK_TEST_GENERATOR AND NOT KISAK_TEST_GENERATOR STREQUAL "")
        list(APPEND _configure_args "-G" "${KISAK_TEST_GENERATOR}")
        if(DEFINED KISAK_TEST_GENERATOR_PLATFORM
                AND NOT KISAK_TEST_GENERATOR_PLATFORM STREQUAL "")
            list(APPEND _configure_args "-A" "${KISAK_TEST_GENERATOR_PLATFORM}")
        endif()
        if(DEFINED KISAK_TEST_GENERATOR_TOOLSET
                AND NOT KISAK_TEST_GENERATOR_TOOLSET STREQUAL "")
            list(APPEND _configure_args "-T" "${KISAK_TEST_GENERATOR_TOOLSET}")
        endif()
    endif()
    set(_fixture_vs FALSE)
    if(KISAK_TEST_CXX_COMPILER_ID STREQUAL "MSVC"
            AND (NOT DEFINED KISAK_TEST_GENERATOR
                OR KISAK_TEST_GENERATOR STREQUAL ""
                OR KISAK_TEST_GENERATOR MATCHES "^Visual Studio"))
        set(_fixture_vs TRUE)
    endif()
    if(NOT _fixture_vs)
        # Single-config generators need the build type and the exact configured
        # compiler; a multi-config Visual Studio generator selects both itself.
        list(APPEND _configure_args
            "-DCMAKE_BUILD_TYPE=Release"
            "-DCMAKE_CXX_COMPILER=${KISAK_TEST_CXX_COMPILER}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${_configure_args}
        RESULT_VARIABLE _fixture_configure_result
        OUTPUT_VARIABLE _fixture_configure_stdout
        ERROR_VARIABLE _fixture_configure_stderr)
    if(NOT _fixture_configure_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to configure the archive-build identity fixture: "
            "${_fixture_configure_stdout} ${_fixture_configure_stderr}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${_build}" --config Release
        RESULT_VARIABLE _fixture_build_result
        OUTPUT_VARIABLE _fixture_build_stdout
        ERROR_VARIABLE _fixture_build_stderr)
    if(NOT _fixture_build_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to compile the archive-build source-identity consumer: "
            "${_fixture_build_stdout} ${_fixture_build_stderr}")
    endif()
    # The executable suffix follows the host platform, not the compiler id:
    # every Windows toolchain (MSVC, MinGW GNU, clang-cl) produces
    # ``identity-retained.exe``, and keying on MSVC alone would miss the MinGW
    # and clang-cl fixtures even though they built correctly.
    if(CMAKE_HOST_WIN32)
        set(_retained_name "identity-retained.exe")
        set(_stripped_name "identity-stripped.exe")
    else()
        set(_retained_name "identity-retained")
        set(_stripped_name "identity-stripped")
    endif()
    file(GLOB_RECURSE _retained_candidates "${_build}/${_retained_name}")
    list(FILTER _retained_candidates EXCLUDE REGEX "/CMakeFiles/")
    if(NOT _retained_candidates)
        message(FATAL_ERROR
            "The compiled archive-build identity fixture produced no ${_retained_name}")
    endif()
    list(GET _retained_candidates 0 _retained_exe)
    file(GLOB_RECURSE _stripped_candidates "${_build}/${_stripped_name}")
    list(FILTER _stripped_candidates EXCLUDE REGEX "/CMakeFiles/")
    if(NOT _stripped_candidates)
        message(FATAL_ERROR
            "The compiled archive-build identity fixture produced no ${_stripped_name}")
    endif()
    list(GET _stripped_candidates 0 _stripped_exe)
    execute_process(
        COMMAND "${_retained_exe}"
        OUTPUT_VARIABLE _compiled_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _run_result)
    if(NOT _run_result EQUAL 0)
        message(FATAL_ERROR
            "The compiled archive-build identity fixture did not run: "
            "${_compiled_output}")
    endif()
    require_identity_bytes("${_retained_exe}" "${_archive_commit}" TRUE
        "getSourceCommit is retained through release dead stripping")
    # The negative control is meaningful only where the fixture's own flags
    # deterministically dead-strip the unreferenced accessor and its literal.
    # GNU on ELF splits the literal into its own section, so --gc-sections
    # removes it once the function is dropped. Clang merges string literals
    # into one .rodata.str1.1 section that surviving consumers keep alive,
    # Darwin's default Release link does not dead-strip, and MSVC only discards
    # the data as a COMDAT (not cl.exe's default), so the assertion is
    # restricted to the GNU/ELF leg where it is deterministic. The positive
    # retention assertion above still runs on every platform.
    if(KISAK_TEST_CXX_COMPILER_ID STREQUAL "GNU"
            AND CMAKE_HOST_UNIX AND NOT CMAKE_HOST_APPLE)
        require_identity_bytes("${_stripped_exe}" "${_archive_commit}" FALSE
            "removing the retention markers drops the identity under release dead stripping")
    endif()
endfunction()

# Prove the published buildnumber.h schedules its compiled consumer in the SAME
# ordinary build. Ninja validates the dependency graph up front, so a header
# rewritten mid-build by an order dependency on an always-run utility target
# does not re-dirty translation units the graph already judged fresh: after a
# commit that changed no CMake input, one ordinary build relinked the consumer
# with a buildnumber.cpp object still compiled against the previous revision,
# and only a second build produced the current one. Configure and build a real
# compiled identity consumer at commit A, advance to B by changing only
# main.cpp, run ONE ordinary build, and require the linked artifact to carry B
# and no A.
#
# An optional first argument pins SOURCE_DATE_EPOCH to a reproducible-build
# epoch for the entire A/B cycle. CMake's string(TIMESTAMP) substitutes that
# epoch for the current time, so an aging step that derives its clock from
# string(TIMESTAMP) backdates the freshly published header years into the
# past: existing consumer objects are then strictly newer at whole-second
# resolution, make skips their recompilation, and the relink carries A. The
# pinned run asserts the SAME one-ordinary-build outcome end to end, and
# additionally requires the published header's mtime to postdate a pre-build
# wall-clock lower bound, naming the backdating defect directly.
function(check_incremental_stamp_schedules_consumer)
    set(_fixture_sde "${ARGV0}")
    set(_fixture_name incremental-tree)
    if(NOT _fixture_sde STREQUAL "")
        set(_fixture_name incremental-tree-source-date-epoch)
    endif()
    if(NOT KISAK_TEST_GIT_EXECUTABLE)
        message(STATUS
            "git not found; skipping the incremental stamp fixture")
        return()
    endif()
    if(NOT DEFINED KISAK_TEST_CXX_COMPILER OR KISAK_TEST_CXX_COMPILER STREQUAL "")
        message(STATUS
            "No C++ compiler provided; skipping the incremental stamp fixture")
        return()
    endif()
    set(_tree "${_test_root}/${_fixture_name}")
    set(_build "${_tree}/build")
    set(_stamp_dir "${_build}/stamped-src")
    file(MAKE_DIRECTORY "${_tree}/consumer/universal")
    file(COPY "${SOURCE_ROOT}/src/buildnumber.cpp"
        DESTINATION "${_tree}/consumer")
    file(COPY "${SOURCE_ROOT}/src/universal/platform_compat.h"
        DESTINATION "${_tree}/consumer/universal")
    # The compiled identity consumer mirrors the production wiring: the real
    # src/buildnumber.cpp plus a caller, ordered with scripts/pre_build.cmake's
    # add_dependencies(${PROJECT_NAME} update_build_number).
    file(WRITE "${_tree}/main.cpp"
        "#include <cstdio>\n"
        "#include \"buildnumber.h\"\n"
        "// revision A\n"
        "int main() {\n"
        "    std::printf(\"%s\\n%d\\n\", getSourceCommit(), getBuildNumberAsInt());\n"
        "    return 0;\n"
        "}\n")
    file(WRITE "${_tree}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.16)\n"
        "project(kisak_incremental_identity CXX)\n"
        "set(SCRIPTS_DIR \"${SOURCE_ROOT}/scripts\")\n"
        "set(SRC_DIR \"\${CMAKE_CURRENT_BINARY_DIR}/stamped-src\")\n"
        "file(MAKE_DIRECTORY \"\${SRC_DIR}\")\n"
        "include(\"\${SCRIPTS_DIR}/extern/increment_build.cmake\")\n"
        "add_executable(identity-consumer\n"
        "    consumer/buildnumber.cpp\n"
        "    main.cpp)\n"
        "target_include_directories(identity-consumer PRIVATE\n"
        "    \"\${SRC_DIR}\"\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/consumer\")\n"
        "# Production ordering: scripts/pre_build.cmake wires the stamp into\n"
        "# the project target with add_dependencies(\${PROJECT_NAME}\n"
        "# update_build_number).\n"
        "add_dependencies(identity-consumer update_build_number)\n")
    if(NOT _fixture_sde STREQUAL "")
        # Export the reproducible-build epoch to every configure and build
        # child process, and take a whole-second wall-clock lower bound BEFORE
        # any build runs: the aged publication must strictly postdate it, so
        # the post-build assertion cannot pass a backdated header no matter
        # how slowly the builds execute.
        set(_saved_source_date_epoch "$ENV{SOURCE_DATE_EPOCH}")
        set(ENV{SOURCE_DATE_EPOCH} "${_fixture_sde}")
        file(TOUCH "${_tree}/clock-probe")
        file(TIMESTAMP "${_tree}/clock-probe" _sde_prebuild_epoch "%s")
    endif()
    foreach(_leg IN ITEMS a b)
        if(_leg STREQUAL "b")
            file(WRITE "${_tree}/main.cpp"
                "#include <cstdio>\n"
                "#include \"buildnumber.h\"\n"
                "// revision B\n"
                "int main() {\n"
                "    std::printf(\"%s\\n%d\\n\", getSourceCommit(), getBuildNumberAsInt());\n"
                "    return 0;\n"
                "}\n")
            execute_process(
                COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_tree}"
                    -c user.email=identity@example.invalid
                    -c user.name=identity commit -qam "revision B"
                RESULT_VARIABLE _commit_result
                ERROR_QUIET)
        else()
            execute_process(
                COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" init -q "${_tree}"
                RESULT_VARIABLE _init_result
                ERROR_QUIET)
            execute_process(
                COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_tree}" add -A
                RESULT_VARIABLE _add_result
                ERROR_QUIET)
            execute_process(
                COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_tree}"
                    -c user.email=identity@example.invalid
                    -c user.name=identity commit -qm "revision A"
                RESULT_VARIABLE _commit_result
                ERROR_QUIET)
        endif()
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_tree}" rev-parse HEAD
            OUTPUT_VARIABLE _head_${_leg}
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _head_result
            ERROR_QUIET)
        if(_leg STREQUAL "a")
            if(NOT (_init_result EQUAL 0 AND _add_result EQUAL 0
                    AND _commit_result EQUAL 0 AND _head_result EQUAL 0))
                message(FATAL_ERROR
                    "Failed to build the incremental stamp fixture at A")
            endif()
            set(_configure_args "-S" "${_tree}" "-B" "${_build}")
            if(DEFINED KISAK_TEST_GENERATOR AND NOT KISAK_TEST_GENERATOR STREQUAL "")
                list(APPEND _configure_args "-G" "${KISAK_TEST_GENERATOR}")
                if(DEFINED KISAK_TEST_GENERATOR_PLATFORM
                        AND NOT KISAK_TEST_GENERATOR_PLATFORM STREQUAL "")
                    list(APPEND _configure_args "-A" "${KISAK_TEST_GENERATOR_PLATFORM}")
                endif()
                if(DEFINED KISAK_TEST_GENERATOR_TOOLSET
                        AND NOT KISAK_TEST_GENERATOR_TOOLSET STREQUAL "")
                    list(APPEND _configure_args "-T" "${KISAK_TEST_GENERATOR_TOOLSET}")
                endif()
            endif()
            set(_fixture_vs FALSE)
            if(KISAK_TEST_CXX_COMPILER_ID STREQUAL "MSVC"
                    AND (NOT DEFINED KISAK_TEST_GENERATOR
                        OR KISAK_TEST_GENERATOR STREQUAL ""
                        OR KISAK_TEST_GENERATOR MATCHES "^Visual Studio"))
                set(_fixture_vs TRUE)
            endif()
            if(NOT _fixture_vs)
                # Single-config generators need the build type and the exact
                # configured compiler; a multi-config Visual Studio generator
                # selects both itself.
                list(APPEND _configure_args
                    "-DCMAKE_BUILD_TYPE=Release"
                    "-DCMAKE_CXX_COMPILER=${KISAK_TEST_CXX_COMPILER}")
            endif()
            execute_process(
                COMMAND "${CMAKE_COMMAND}" ${_configure_args}
                RESULT_VARIABLE _configure_result
                OUTPUT_VARIABLE _configure_stdout
                ERROR_VARIABLE _configure_stderr)
            if(NOT _configure_result EQUAL 0)
                message(FATAL_ERROR
                    "Failed to configure the incremental stamp fixture: "
                    "${_configure_stdout} ${_configure_stderr}")
            endif()
        elseif(NOT (_commit_result EQUAL 0 AND _head_result EQUAL 0)
                OR _head_b STREQUAL _head_a)
            message(FATAL_ERROR
                "Failed to advance the incremental stamp fixture to B")
        endif()
        # ONE ordinary build per leg: no reconfigure, no target selection.
        # The second leg is the entire defect surface.
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${_build}" --config Release
            RESULT_VARIABLE _build_result
            OUTPUT_VARIABLE _build_stdout
            ERROR_VARIABLE _build_stderr)
        if(NOT _build_result EQUAL 0)
            message(FATAL_ERROR
                "The incremental stamp fixture failed to build at ${_leg}: "
                "${_build_stdout} ${_build_stderr}")
        endif()
        file(GLOB_RECURSE _consumer_candidates "${_build}/identity-consumer*")
        list(FILTER _consumer_candidates EXCLUDE REGEX "/CMakeFiles/")
        if(NOT _consumer_candidates)
            message(FATAL_ERROR
                "The incremental stamp fixture produced no identity-consumer "
                "executable at ${_leg}")
        endif()
        list(GET _consumer_candidates 0 _consumer_exe)
        execute_process(
            COMMAND "${_consumer_exe}"
            OUTPUT_VARIABLE _run_output_${_leg}
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _run_result)
        if(NOT _run_result EQUAL 0)
            message(FATAL_ERROR
                "The incremental stamp fixture did not run at ${_leg}: "
                "${_run_output_${_leg}}")
        endif()
    endforeach()
    # Leg A sanity: the first ordinary build carries its own revision.
    string(FIND "${_run_output_a}" "${_head_a}" _a_position)
    if(_a_position EQUAL -1)
        message(FATAL_ERROR
            "The first ordinary build did not carry its own revision: "
            "expected '${_head_a}' from the consumer, got '${_run_output_a}'")
    endif()
    # The defect surface: ONE ordinary build after a commit that changed only
    # main.cpp must relink the consumer with B.
    string(FIND "${_run_output_b}" "${_head_b}" _b_position)
    if(_b_position EQUAL -1)
        # Failure-path forensics only: everything below augments the failure
        # message; the assertions here and below are untouched. Hosted macOS
        # CI has seen this leg link revision A while Linux passes; these
        # diagnostics record whether the always-run stamp target and the
        # publish edge executed in leg B and what make considered current.
        set(_diag_stamped "${_build}/stamped-src/buildnumber.h")
        set(_diag_staged "${_build}/buildnumber-stamp/buildnumber.h")
        set(_diag_marker "${_build}/buildnumber-stamp/buildnumber.h.superseded")
        set(_diag_objects "")
        file(GLOB_RECURSE _diag_object_candidates
            "${_build}/CMakeFiles/identity-consumer.dir/*buildnumber*")
        if(_diag_object_candidates)
            set(_diag_objects "${_diag_object_candidates}")
        endif()
        set(_diag "head a=${_head_a} head b=${_head_b}
--- leg B build stdout ---
${_build_stdout}
--- leg B build stderr ---
${_build_stderr}
")
        foreach(_diag_path IN ITEMS "${_diag_stamped}" "${_diag_staged}"
                "${_diag_marker}" ${_diag_objects})
            if(EXISTS "${_diag_path}")
                file(TIMESTAMP "${_diag_path}" _diag_mtime "%s")
                string(APPEND _diag "exists ${_diag_path} mtime=${_diag_mtime}
")
            else()
                string(APPEND _diag "MISSING ${_diag_path}
")
            endif()
        endforeach()
        if(EXISTS "${_diag_stamped}")
            file(READ "${_diag_stamped}" _diag_published_content)
            string(APPEND _diag "--- published header ---
${_diag_published_content}
")
        endif()
        if(EXISTS "${_diag_staged}")
            file(READ "${_diag_staged}" _diag_staged_content)
            string(APPEND _diag "--- staged header ---
${_diag_staged_content}
")
        endif()
        message(FATAL_ERROR
            "One ordinary build after a commit that changed only main.cpp "
            "still linked the previous revision: expected '${_head_b}' from "
            "the consumer, got '${_run_output_b}' (the published header did "
            "not reschedule its compiled consumer in the same build). "
            "Forensics: ${_diag}")
    endif()
    string(FIND "${_run_output_b}" "${_head_a}" _stale_position)
    if(NOT _stale_position EQUAL -1)
        message(FATAL_ERROR
            "The rebuilt consumer still carries the stale revision "
            "'${_head_a}'")
    endif()
    # The consumer output proves the accessor call, the byte search proves the
    # revision itself is in the linked artifact.
    require_identity_bytes("${_consumer_exe}" "${_head_b}" TRUE
        "one ordinary build after a commit publishes the new revision into the linked artifact")
    require_identity_bytes("${_consumer_exe}" "${_head_a}" FALSE
        "the linked artifact must not keep the previous revision after one ordinary build")
    if(NOT _fixture_sde STREQUAL "")
        # Restore the caller's reproducible-build environment first: the
        # assertions below must read real mtimes, not the pinned epoch.
        if(_saved_source_date_epoch STREQUAL "")
            unset(ENV{SOURCE_DATE_EPOCH})
        else()
            set(ENV{SOURCE_DATE_EPOCH} "${_saved_source_date_epoch}")
        endif()
        # The aged publication must strictly postdate the pre-build wall-clock
        # bound. An aging step clocked from string(TIMESTAMP) would read the
        # pinned SOURCE_DATE_EPOCH itself - years in the past - and this
        # assertion names that backdating directly, independently of the
        # linked-artifact checks above.
        set(_published_header "${_build}/stamped-src/buildnumber.h")
        file(TIMESTAMP "${_published_header}" _sde_header_epoch "%s")
        if(_sde_header_epoch STREQUAL ""
                OR NOT _sde_header_epoch GREATER "${_sde_prebuild_epoch}")
            message(FATAL_ERROR
                "With SOURCE_DATE_EPOCH=${_fixture_sde} the published header "
                "was not aged to the real wall clock: mtime epoch "
                "'${_sde_header_epoch}' does not postdate the pre-build bound "
                "'${_sde_prebuild_epoch}' (a reproducible-build timestamp "
                "input backdated the publication below its already-built "
                "consumers)")
        endif()
    endif()
endfunction()

# A tree with a substituted carrier but no `.git` must resolve from the carrier.
if(DEFINED CONTRACT_CASE AND CONTRACT_CASE STREQUAL "invalid_override")
    set(KISAK_SOURCE_COMMIT "not-a-commit-hash")
    kisak_resolve_source_identity("${SOURCE_ROOT}" _resolved)
    message(STATUS "invalid override accepted: ${_resolved}")
    return()
endif()

read_normalized(
    "${SOURCE_ROOT}/src/.gitattributes" _attributes "export-subst attribute")
read_normalized(
    "${SOURCE_ROOT}/src/source_identity.txt" _carrier "identity carrier")
read_normalized(
    "${SOURCE_ROOT}/src/buildnumber.cpp" _buildnumber_cpp
    "compiled source-identity consumer")
read_normalized(
    "${SOURCE_ROOT}/scripts/extern/increment_build.cmake" _cmake
    "build-number CMake wiring")
read_normalized(
    "${SOURCE_ROOT}/scripts/extern/stamp_build_number.cmake" _stamp
    "build-time stamp script")
read_normalized(
    "${SOURCE_ROOT}/scripts/extern/schedule_publish_header.cmake" _aging
    "publish-edge aging script")
read_normalized(
    "${SOURCE_ROOT}/scripts/increment_build.sh" _sh "POSIX build-number script")
read_normalized(
    "${SOURCE_ROOT}/scripts/increment_build.cmd" _cmd "Windows build-number script")
read_normalized(
    "${SOURCE_ROOT}/tests/CMakeLists.txt" _tests "portable test registration")
read_normalized(
    "${SOURCE_ROOT}/scripts/ci/release-requirements.json" _release_requirements
    "release requirements contract")
read_normalized(
    "${SOURCE_ROOT}/scripts/ci/release_provenance.py" _verifier
    "release provenance verifier")
read_normalized(
    "${SOURCE_ROOT}/scripts/ci/release_provenance_archive.py" _archive_verifier
    "release provenance archive verifier")

# Mutation mode is used only by the self-checks at the end of this file.
if(DEFINED CONTRACT_MUTATION AND NOT CONTRACT_MUTATION STREQUAL "")
    if(CONTRACT_MUTATION STREQUAL "strip_export_subst")
        string(REPLACE
            "export-subst"
            ""
            _attributes "${_attributes}")
    elseif(CONTRACT_MUTATION STREQUAL "sh_header_macro")
        string(REPLACE
            "#define KISAK_SOURCE_COMMIT \"$SOURCE_COMMIT\""
            ""
            _sh "${_sh}")
    elseif(CONTRACT_MUTATION STREQUAL "cmd_header_macro")
        string(REPLACE
            "#define KISAK_SOURCE_COMMIT \"!SOURCE_COMMIT!\""
            ""
            _cmd "${_cmd}")
    elseif(CONTRACT_MUTATION STREQUAL "cmake_resolver_call")
        string(REPLACE
            "kisak_resolve_source_identity(\"\${KISAK_STAMP_SOURCE_DIR}\" KISAK_RESOLVED_SOURCE_COMMIT)"
            ""
            _stamp "${_stamp}")
    elseif(CONTRACT_MUTATION STREQUAL "stamp_delegation")
        string(REPLACE
            "-P \"\${SCRIPTS_DIR}/extern/stamp_build_number.cmake\""
            ""
            _cmake "${_cmake}")
    elseif(CONTRACT_MUTATION STREQUAL "stamp_override_forward")
        string(REPLACE
            "\"-DKISAK_SOURCE_COMMIT=\${KISAK_SOURCE_COMMIT}\""
            ""
            _cmake "${_cmake}")
    elseif(CONTRACT_MUTATION STREQUAL "cpp_consumer")
        string(REPLACE
            "KISAK_SOURCE_COMMIT"
            ""
            _buildnumber_cpp "${_buildnumber_cpp}")
    elseif(CONTRACT_MUTATION STREQUAL "cpp_getter")
        string(REPLACE
            "getSourceCommit"
            ""
            _buildnumber_cpp "${_buildnumber_cpp}")
    elseif(CONTRACT_MUTATION STREQUAL "cpp_retention")
        string(REPLACE
            "__attribute__((retain))"
            ""
            _buildnumber_cpp "${_buildnumber_cpp}")
        string(REPLACE
            "KISAK_SOURCE_IDENTITY_RETAIN const char"
            ""
            _buildnumber_cpp "${_buildnumber_cpp}")
        string(REPLACE
            "/include:getSourceCommit"
            ""
            _buildnumber_cpp "${_buildnumber_cpp}")
    elseif(CONTRACT_MUTATION STREQUAL "sh_header_getter")
        string(REPLACE
            "const char* getSourceCommit();"
            ""
            _sh "${_sh}")
    elseif(CONTRACT_MUTATION STREQUAL "cmd_header_getter")
        string(REPLACE
            "const char ^*__cdecl getSourceCommit^(^)^;"
            ""
            _cmd "${_cmd}")
    elseif(CONTRACT_MUTATION STREQUAL "test_registration")
        string(REPLACE
            "NAME source-archive-identity-contracts"
            ""
            _tests "${_tests}")
    elseif(CONTRACT_MUTATION STREQUAL "carrier_grammar")
        string(REPLACE
            "if line.startswith(\"commit=\")"
            "if line.strip().startswith(\"commit=\")"
            _archive_verifier "${_archive_verifier}")
    elseif(CONTRACT_MUTATION STREQUAL "publish_edge")
        string(REPLACE
            "OUTPUT \"\${SRC_DIR}/buildnumber.h\""
            ""
            _cmake "${_cmake}")
    elseif(CONTRACT_MUTATION STREQUAL "stamp_publish_guard")
        string(REPLACE
            "file(REMOVE \"\${KISAK_STAMP_PUBLISH_HEADER}\")"
            ""
            _stamp "${_stamp}")
    elseif(CONTRACT_MUTATION STREQUAL "publish_aging")
        string(REPLACE
            "-P \"\${SCRIPTS_DIR}/extern/schedule_publish_header.cmake\""
            ""
            _cmake "${_cmake}")
    elseif(CONTRACT_MUTATION STREQUAL "publish_clock_source")
        string(REPLACE
            "file(TIMESTAMP \"\${KISAK_PUBLISH_CLOCK_PROBE}\""
            "string(TIMESTAMP \"\${KISAK_PUBLISH_CLOCK_PROBE}\""
            _aging "${_aging}")
    elseif(CONTRACT_MUTATION STREQUAL "publish_marker_early")
        # The rejected ordering: consume the marker before aging runs, so a
        # mid-script clock or touch failure destroys the forensic record.
        # Remove the post-aging consumption and re-insert it before the
        # aging guards; the ordering contract must reject the result.
        # Needles operate on the whitespace-normalized source.
        string(REPLACE
            "# Aging succeeded: only now consume the marker; every failure path above # aborts the script with the marker still on disk as forensic state, and # the next publish edge retries the aging instead of losing the record. file(REMOVE \"\${KISAK_PUBLISH_SUPERSEDED_MARKER}\")"
            ""
            _aging "${_aging}")
        string(REPLACE
            "if(NOT EXISTS \"\${KISAK_PUBLISH_HEADER}\")"
            "file(REMOVE \"\${KISAK_PUBLISH_SUPERSEDED_MARKER}\") if(NOT EXISTS \"\${KISAK_PUBLISH_HEADER}\")"
            _aging "${_aging}")
    elseif(CONTRACT_MUTATION STREQUAL "stamp_superseded_marker")
        string(REPLACE
            "\"\${KISAK_STAMP_SUPERSEDED_MARKER}\""
            ""
            _stamp "${_stamp}")
    elseif(CONTRACT_MUTATION STREQUAL "stamp_marker_cleanup")
        # The rejected ordering: let the stamp consume a leftover marker when
        # the current stamp does not supersede. An interrupted supersede must
        # survive to the publish edge's aging step in the same build; a
        # stamp-side cleanup races that publish edge and can leave the
        # recreated header un-aged against whole-second consumer ties. The
        # mutation re-inserts the cleanup at the stamp's tail; the
        # never-consume invariant must reject the result. Needles operate on
        # the whitespace-normalized source.
        string(REPLACE
            "file(REMOVE \"\${KISAK_STAMP_PUBLISH_HEADER}\") endif() endif() endif()"
            "file(REMOVE \"\${KISAK_STAMP_PUBLISH_HEADER}\") endif() if(NOT EXISTS \"\${KISAK_STAMP_SUPERSEDED_MARKER}\") file(REMOVE \"\${KISAK_STAMP_SUPERSEDED_MARKER}\") endif() endif() endif()"
            _stamp "${_stamp}")
    else()
        message(FATAL_ERROR
            "Unknown source-identity mutation: ${CONTRACT_MUTATION}")
    endif()
endif()

# The carrier is only substituted because git is told to substitute it.
require_contains(
    _attributes "source_identity.txt export-subst"
    "src/.gitattributes marks the carrier export-subst")
require_contains(
    _carrier "commit=$Format:%H$"
    "the carrier holds the git archive commit placeholder")

# Stamping must resolve the identity when the update_build_number target RUNS,
# not when CMake configures: a reused build directory after a commit or
# checkout that changes no CMake input never re-runs CMake, so a
# configure-time resolution stamps freshly built binaries with a stale
# revision. Pin the delegation wiring and the build-time resolution separately.
require_contains(
    _cmake "\"-DKISAK_STAMP_SOURCE_DIR=\${CMAKE_SOURCE_DIR}\""
    "increment_build.cmake points the build-time stamp at the source tree")
require_contains(
    _cmake "\"-DKISAK_SOURCE_COMMIT=\${KISAK_SOURCE_COMMIT}\""
    "increment_build.cmake forwards a configure-time override to the build-time stamp")
require_contains(
    _cmake "-P \"\${SCRIPTS_DIR}/extern/stamp_build_number.cmake\""
    "increment_build.cmake delegates stamping to the build-time script")
require_contains(
    _stamp "include(\"\${KISAK_STAMP_SCRIPTS_DIR}/extern/resolve_source_identity.cmake\")"
    "the build-time stamp script includes the resolver")
require_contains(
    _stamp "kisak_resolve_source_identity(\"\${KISAK_STAMP_SOURCE_DIR}\" KISAK_RESOLVED_SOURCE_COMMIT)"
    "the build-time stamp script resolves the identity when the build runs")
require_contains(
    _stamp "\"\${KISAK_RESOLVED_SOURCE_COMMIT}\""
    "the build-time stamp script passes the resolved commit to the stamp script")

# Stamping through an order dependency alone cannot update compiled consumers:
# Ninja validates the graph up front, so a header rewritten mid-build cannot
# re-dirty translation units the graph already judged fresh, and one ordinary
# build after a commit that changed no CMake input would relink a stale
# buildnumber.cpp object. The header must therefore be published as a declared
# build edge output from a staging copy, so Ninja's restat reschedules its
# consumers in the same ordinary build, and the publish must be
# content-comparing so unchanged stamps rebuild nothing.
require_contains(
    _cmake "\"-DKISAK_STAMP_SRC_DIR=\${KISAK_STAMP_STAGE_DIR}\""
    "increment_build.cmake stamps into a staging directory")
require_contains(
    _cmake "OUTPUT \"\${SRC_DIR}/buildnumber.h\""
    "the published buildnumber.h is a declared build edge output")
require_contains(
    _cmake "-E copy_if_different"
    "publication is content-comparing so restat can hold consumers clean")
require_contains(
    _cmake "BYPRODUCTS \"\${KISAK_STAMP_STAGE_DIR}/buildnumber.h\""
    "the stamp declares the staged header as a generator-tracked byproduct")
require_contains(
    _cmake "DEPENDS \"\${KISAK_STAMP_STAGE_DIR}/buildnumber.h\" update_build_number_stamp"
    "the publish edge is driven by the staged header and runs after the stamp")
require_contains(
    _cmake "update_build_number DEPENDS \"\${SRC_DIR}/buildnumber.h\""
    "the public stamp target pulls the published header into ordinary builds")

# Timestamp resolution is not uniform across generators: Apple's GNU Make
# 3.81, the default generator on macOS CI, compares whole seconds. A stamp
# that rewrites the staged header within the same wall-clock second in which
# the previous build published the old revision ties the publish edge's mtime
# comparison and the copy is skipped - the stale revision survives one
# ordinary build. The stamp must therefore remove a published header whose
# content it has superseded: a missing output forces the publish edge to run
# on every generator. Removal alone still leaves the consumer-shaped half of
# the same defect: the recreated header can land in the same wall-clock
# second in which the previous build compiled its consumers, and a
# whole-second make then ties the header against those objects, skips their
# recompilation, and relinks the previous revision's identity. The stamp must
# therefore record each supersede in a build-directory marker, and the
# publish edge must age the recreated header one second into the future, so
# it is strictly newer than any existing consumer at whole-second resolution
# - without sleeps, retries, or a second build. An unchanged stamp compares
# equal and removes nothing, so clean rebuilds stay clean. The marker's
# lifetime ends at the publish edge: the stamp never consumes it, because a
# stamp-side cleanup races the publish edge that may still run in the same
# build after an interrupted supersede.
require_contains(
    _cmake "\"-DKISAK_STAMP_PUBLISH_HEADER=\${SRC_DIR}/buildnumber.h\""
    "increment_build.cmake tells the stamp which header the publish edge owns")
require_contains(
    _stamp "file(REMOVE \"\${KISAK_STAMP_PUBLISH_HEADER}\")"
    "the stamp removes a published header its content has superseded")
require_contains(
    _cmake "\"-DKISAK_STAMP_SUPERSEDED_MARKER=\${KISAK_STAMP_SUPERSEDED_MARKER}\""
    "increment_build.cmake tells the stamp where to record superseded publications")
require_contains(
    _stamp "\"\${KISAK_STAMP_SUPERSEDED_MARKER}\""
    "the stamp records superseded publications for the publish edge's aging step")
# The marker is consumed by the aging step and by nothing else: a stamp-side
# cleanup would race the publish edge that is still about to run after an
# interrupted supersede, and would destroy the forensic record that makes the
# next publish edge retry the aging instead of silently publishing an
# un-aged, tie-prone header.
require_not_contains(
    _stamp "file(REMOVE \"\${KISAK_STAMP_SUPERSEDED_MARKER}\")"
    "the stamp never consumes the superseded marker; the aging step does")
require_contains(
    _cmake "\"-DKISAK_PUBLISH_SUPERSEDED_MARKER=\${KISAK_STAMP_SUPERSEDED_MARKER}\""
    "increment_build.cmake hands the superseded marker to the aging step")
require_contains(
    _cmake "-P \"\${SCRIPTS_DIR}/extern/schedule_publish_header.cmake\""
    "the publish edge ages a recreated header past whole-second resolution")
require_contains(
    _aging "if(NOT EXISTS \"\${KISAK_PUBLISH_SUPERSEDED_MARKER}\")"
    "the aging step stays inert when the stamp published identical content")
require_contains(
    _aging "file(REMOVE \"\${KISAK_PUBLISH_SUPERSEDED_MARKER}\")"
    "the aging step consumes the superseded marker it acts on")
# Consumption must not precede aging: a mid-script failure must leave the
# marker on disk as forensic state for the next publish edge, and the
# Windows no-op path - a successful publication with nothing to age - must
# still consume it instead of leaking it forever. Needles are matched
# against whitespace-normalized source, so they pin the ORDER of the
# normalized tokens.
require_contains(
    _aging "if(NOT UNIX OR CMAKE_HOST_WIN32) file(REMOVE \"\${KISAK_PUBLISH_SUPERSEDED_MARKER}\") return()"
    "the Windows no-op path consumes the superseded marker because the no-op is a successful publication")
require_contains(
    _aging "already-built consumers\") endif() # Aging succeeded: only now consume the marker"
    "the aging step consumes the superseded marker only after POSIX aging succeeds")
require_contains(
    _aging "\"\${KISAK_PUBLISH_NOW_EPOCH} + 1\""
    "the aging step derives the future mtime from the wall clock")
require_contains(
    _aging "file(TIMESTAMP \"\${KISAK_PUBLISH_CLOCK_PROBE}\" KISAK_PUBLISH_NOW_EPOCH \"%s\")"
    "the aging step reads the true wall clock from a touched probe file, not string(TIMESTAMP)")
require_contains(
    _aging "KISAK_PUBLISH_NOW_EPOCH MATCHES \"[^0-9]\""
    "the aging step fails closed when the wall-clock probe is unreadable")
require_contains(
    _aging "-t \"\${KISAK_PUBLISH_FUTURE_STAMP}\""
    "the aging step sets the recreated header's mtime into the future")

# Both stamp scripts must forward the commit into the generated header.
require_contains(
    _sh "SOURCE_COMMIT=\"\${3:-}\""
    "the POSIX stamp script accepts the source commit")
require_contains(
    _sh "#define KISAK_SOURCE_COMMIT \"$SOURCE_COMMIT\""
    "the POSIX stamp script records the source commit")
require_contains(
    _cmd "set \"SOURCE_COMMIT=%~3\""
    "the Windows stamp script accepts the source commit")
require_contains(
    _cmd "#define KISAK_SOURCE_COMMIT \"!SOURCE_COMMIT!\""
    "the Windows stamp script records the source commit")

# The generated header must expose the accessor and the compiled TU must
# reference the macro, or the resolved commit is only written to a header and
# never emitted into the released binary.
require_contains(
    _sh "getSourceCommit"
    "the POSIX stamp script declares the source-commit accessor")
require_contains(
    _cmd "getSourceCommit"
    "the Windows stamp script declares the source-commit accessor")
# The definition uses C linkage so the retention directive can name a stable
# linker symbol; the generated declarations must match or the accessor does not
# link.
require_contains(
    _sh "extern \"C\" const char* getSourceCommit();"
    "the POSIX stamp script declares the source-commit accessor with C linkage")
require_contains(
    _cmd "extern \"C\" const char ^*__cdecl getSourceCommit^(^)^;"
    "the Windows stamp script declares the source-commit accessor with C linkage")
require_contains(
    _buildnumber_cpp "KISAK_SOURCE_COMMIT"
    "the compiled build-number TU references the source commit macro")
require_contains(
    _buildnumber_cpp "getSourceCommit"
    "the compiled build-number TU defines the source-commit accessor")
# Referencing the macro is not enough under release optimization: the compiled
# TU must also force the accessor into the linked image, or the linker discards
# it together with its revision string.
require_contains(
    _buildnumber_cpp "__attribute__((retain))"
    "the compiled build-number TU marks the source-commit accessor retain")
require_contains(
    _buildnumber_cpp "KISAK_SOURCE_IDENTITY_RETAIN const char"
    "the compiled build-number TU applies the retention attribute to the accessor")
require_contains(
    _buildnumber_cpp "/include:getSourceCommit"
    "the compiled build-number TU forces the source-commit accessor into the image")

# The contract must run in the portable suite it is written for.
require_contains(
    _tests "NAME source-archive-identity-contracts"
    "portable test registration")
require_contains(
    _tests "source_archive_identity_test.cmake"
    "portable test registration points at this script")

# The release verifier must check the very carrier the resolver reads, not only
# the JSON identity member, or an archive could pass verification while a
# rebuild without `.git` loses the verified revision.
require_contains(
    _release_requirements "src/source_identity.txt"
    "release requirements pin the build-consumed identity carrier")
require_contains(
    _verifier "source.get(\"carrier\""
    "release verifier reads the configured build-consumed identity carrier")
# The verifier must apply the resolver's own column-zero grammar, not a stripped
# one, or an indented carrier passes verification while the build resolves
# nothing.
require_contains(
    _archive_verifier "line.startswith(\"commit=\")"
    "release verifier applies the resolver's column-zero carrier grammar")

if(NOT DEFINED CONTRACT_MUTATION AND NOT DEFINED CONTRACT_CASE)
    # --- Resolver behavior on controlled trees -----------------------------
    unset(ENV{KISAK_SOURCE_COMMIT})
    unset(KISAK_SOURCE_COMMIT)

    if(DEFINED CMAKE_BINARY_DIR AND NOT CMAKE_BINARY_DIR STREQUAL "")
        set(_test_root "${CMAKE_BINARY_DIR}/source-archive-identity-test")
    else()
        set(_test_root "${CMAKE_CURRENT_LIST_DIR}/source-archive-identity-test")
    endif()
    file(REMOVE_RECURSE "${_test_root}")
    file(MAKE_DIRECTORY "${_test_root}")

    # No `.git` and no usable carrier: no fabricated identity.
    set(_empty_tree "${_test_root}/empty-tree")
    file(MAKE_DIRECTORY "${_empty_tree}")
    kisak_resolve_source_identity("${_empty_tree}" _empty_identity)
    if(NOT _empty_identity STREQUAL "")
        message(FATAL_ERROR
            "A tree without .git or a carrier resolved a fabricated identity: "
            "'${_empty_identity}'")
    endif()

    # A normal checkout of the placeholder carrier must not masquerade.
    set(_placeholder_tree "${_test_root}/placeholder-tree")
    file(MAKE_DIRECTORY "${_placeholder_tree}/src")
    file(WRITE "${_placeholder_tree}/src/source_identity.txt"
        "commit=\$Format:%H\$\n")
    kisak_resolve_source_identity("${_placeholder_tree}" _placeholder_identity)
    if(NOT _placeholder_identity STREQUAL "")
        message(FATAL_ERROR
            "An unsubstituted export-subst placeholder was accepted as an "
            "identity: '${_placeholder_identity}'")
    endif()

    # A substituted archive carrier resolves without any `.git`.
    set(_archive_commit "0123456789abcdef0123456789abcdef01234567")
    set(_archive_tree "${_test_root}/archive-tree")
    file(MAKE_DIRECTORY "${_archive_tree}/src")
    file(WRITE "${_archive_tree}/src/source_identity.txt"
        "commit=${_archive_commit}\n")
    kisak_resolve_source_identity("${_archive_tree}" _archive_identity)
    if(NOT _archive_identity STREQUAL "${_archive_commit}")
        message(FATAL_ERROR
            "The substituted carrier did not resolve the archived commit: "
            "expected '${_archive_commit}', found '${_archive_identity}'")
    endif()

    # Leading whitespace is not the carrier grammar: the resolver matches
    # `^commit=` at column zero, so an indented line must not resolve. This pins
    # the grammar the release verifier must mirror.
    set(_indented_tree "${_test_root}/indented-tree")
    file(MAKE_DIRECTORY "${_indented_tree}/src")
    file(WRITE "${_indented_tree}/src/source_identity.txt"
        "  commit=${_archive_commit}\n")
    kisak_resolve_source_identity("${_indented_tree}" _indented_identity)
    if(NOT _indented_identity STREQUAL "")
        message(FATAL_ERROR
            "An indented commit= line was accepted as an identity: "
            "'${_indented_identity}'")
    endif()

    # An explicit override wins over both a checkout and a carrier.
    set(_override_commit "abcdefabcdefabcdefabcdefabcdefabcdefabcd")
    set(KISAK_SOURCE_COMMIT "${_override_commit}")
    kisak_resolve_source_identity("${_archive_tree}" _override_identity)
    unset(KISAK_SOURCE_COMMIT)
    if(NOT _override_identity STREQUAL "${_override_commit}")
        message(FATAL_ERROR
            "The explicit override did not win: expected "
            "'${_override_commit}', found '${_override_identity}'")
    endif()

    # A working checkout resolves HEAD.
    find_program(KISAK_TEST_GIT_EXECUTABLE git)
    if(KISAK_TEST_GIT_EXECUTABLE)
        set(_git_tree "${_test_root}/git-tree")
        file(MAKE_DIRECTORY "${_git_tree}")
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" init -q "${_git_tree}"
            RESULT_VARIABLE _git_init_result
            ERROR_QUIET)
        file(WRITE "${_git_tree}/tracked.txt" "identity\n")
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_git_tree}" add -A
            RESULT_VARIABLE _git_add_result
            ERROR_QUIET)
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_git_tree}"
                -c user.email=identity@example.invalid
                -c user.name=identity commit -qm "identity"
            RESULT_VARIABLE _git_commit_result
            ERROR_QUIET)
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_git_tree}"
                rev-parse HEAD
            OUTPUT_VARIABLE _git_head
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _git_head_result
            ERROR_QUIET)
        if(NOT (_git_init_result EQUAL 0 AND _git_add_result EQUAL 0
                AND _git_commit_result EQUAL 0 AND _git_head_result EQUAL 0))
            message(FATAL_ERROR "Failed to build the checkout identity fixture")
        endif()
        kisak_resolve_source_identity("${_git_tree}" _checkout_identity)
        if(NOT _checkout_identity STREQUAL "${_git_head}")
            message(FATAL_ERROR
                "A checkout did not resolve HEAD: expected '${_git_head}', "
                "found '${_checkout_identity}'")
        endif()

        # An archive extracted inside an unrelated repository must take its
        # identity from the carrier, not the enclosing repository's HEAD.
        set(_nested_tree "${_git_tree}/extracted")
        file(MAKE_DIRECTORY "${_nested_tree}/src")
        file(WRITE "${_nested_tree}/src/source_identity.txt"
            "commit=${_archive_commit}\n")
        kisak_resolve_source_identity("${_nested_tree}" _nested_identity)
        if(NOT _nested_identity STREQUAL "${_archive_commit}")
            message(FATAL_ERROR
                "An extracted archive inherited the enclosing repository's "
                "HEAD: expected '${_archive_commit}', found '${_nested_identity}'")
        endif()

        # Regression: the stamp target must resolve the identity when it RUNS.
        # Configure once at commit A, advance the tree to commit B without
        # touching any CMake input, rebuild the stamp target, and require B —
        # not A, into the freshly written header. CMake never re-runs on a pure
        # commit/checkout, so a configure-time resolution would keep stamping
        # the stale revision A into newly built binaries.
        set(_stamp_repo "${_test_root}/stamp-repo")
        set(_stamp_build "${_stamp_repo}/build")
        set(_stamp_src_dir "${_stamp_build}/stamped-src")
        file(MAKE_DIRECTORY "${_stamp_repo}")
        file(WRITE "${_stamp_repo}/CMakeLists.txt"
            "cmake_minimum_required(VERSION 3.16)\n"
            "project(kisak_stamp_freshness NONE)\n"
            "set(SCRIPTS_DIR \"${SOURCE_ROOT}/scripts\")\n"
            "set(SRC_DIR \"\${CMAKE_CURRENT_BINARY_DIR}/stamped-src\")\n"
            "file(MAKE_DIRECTORY \"\${SRC_DIR}\")\n"
            "include(\"\${SCRIPTS_DIR}/extern/increment_build.cmake\")\n")
        file(WRITE "${_stamp_repo}/tracked.txt" "revision A\n")
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" init -q "${_stamp_repo}"
            RESULT_VARIABLE _stamp_init_result
            ERROR_QUIET)
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_stamp_repo}" add -A
            RESULT_VARIABLE _stamp_add_result
            ERROR_QUIET)
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_stamp_repo}"
                -c user.email=identity@example.invalid
                -c user.name=identity commit -qm "stamp A"
            RESULT_VARIABLE _stamp_commit_a_result
            ERROR_QUIET)
        execute_process(
            COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_stamp_repo}"
                rev-parse HEAD
            OUTPUT_VARIABLE _stamp_head_a
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _stamp_head_a_result
            ERROR_QUIET)
        if(NOT (_stamp_init_result EQUAL 0 AND _stamp_add_result EQUAL 0
                AND _stamp_commit_a_result EQUAL 0 AND _stamp_head_a_result EQUAL 0))
            message(FATAL_ERROR "Failed to build the stamp-freshness fixture at A")
        endif()
        set(_stamp_configure_args "-S" "${_stamp_repo}" "-B" "${_stamp_build}")
        if(DEFINED KISAK_TEST_GENERATOR AND NOT KISAK_TEST_GENERATOR STREQUAL "")
            list(APPEND _stamp_configure_args "-G" "${KISAK_TEST_GENERATOR}")
            if(DEFINED KISAK_TEST_GENERATOR_PLATFORM
                    AND NOT KISAK_TEST_GENERATOR_PLATFORM STREQUAL "")
                list(APPEND _stamp_configure_args "-A" "${KISAK_TEST_GENERATOR_PLATFORM}")
            endif()
            if(DEFINED KISAK_TEST_GENERATOR_TOOLSET
                    AND NOT KISAK_TEST_GENERATOR_TOOLSET STREQUAL "")
                list(APPEND _stamp_configure_args "-T" "${KISAK_TEST_GENERATOR_TOOLSET}")
            endif()
        endif()
        execute_process(
            COMMAND "${CMAKE_COMMAND}" ${_stamp_configure_args}
            RESULT_VARIABLE _stamp_configure_result
            OUTPUT_VARIABLE _stamp_configure_stdout
            ERROR_VARIABLE _stamp_configure_stderr)
        if(NOT _stamp_configure_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to configure the stamp-freshness fixture: "
                "${_stamp_configure_stdout} ${_stamp_configure_stderr}")
        endif()
        # Build the stamp target at A, then again after advancing to B. The
        # second build must not reconfigure: that is the entire defect surface.
        foreach(_stamp_leg IN ITEMS a b)
            if(_stamp_leg STREQUAL "b")
                file(WRITE "${_stamp_repo}/tracked.txt" "revision B\n")
                execute_process(
                    COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_stamp_repo}"
                        -c user.email=identity@example.invalid
                        -c user.name=identity commit -qam "stamp B"
                    RESULT_VARIABLE _stamp_commit_b_result
                    ERROR_QUIET)
                execute_process(
                    COMMAND "${KISAK_TEST_GIT_EXECUTABLE}" -C "${_stamp_repo}"
                        rev-parse HEAD
                    OUTPUT_VARIABLE _stamp_head_b
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE _stamp_head_b_result
                    ERROR_QUIET)
                if(NOT (_stamp_commit_b_result EQUAL 0 AND _stamp_head_b_result EQUAL 0)
                        OR _stamp_head_b STREQUAL _stamp_head_a)
                    message(FATAL_ERROR
                        "Failed to advance the stamp-freshness fixture to B")
                endif()
            endif()
            execute_process(
                COMMAND "${CMAKE_COMMAND}" --build "${_stamp_build}"
                    --target update_build_number
                RESULT_VARIABLE _stamp_build_result
                OUTPUT_VARIABLE _stamp_build_stdout
                ERROR_VARIABLE _stamp_build_stderr)
            if(NOT _stamp_build_result EQUAL 0)
                message(FATAL_ERROR
                    "The stamp-freshness fixture failed to build at ${_stamp_leg}: "
                    "${_stamp_build_stdout} ${_stamp_build_stderr}")
            endif()
            string(TOUPPER "${_stamp_leg}" _stamp_leg_upper)
            set(_stamp_header_var _stamp_header_${_stamp_leg})
            file(READ "${_stamp_src_dir}/buildnumber.h" ${_stamp_header_var})
        endforeach()
        string(FIND "${_stamp_header_a}" "${_stamp_head_a}" _stamp_a_position)
        if(_stamp_a_position EQUAL -1)
            message(FATAL_ERROR
                "The stamp-freshness fixture did not stamp its configure-time "
                "revision: '${_stamp_head_a}' is absent from the first header")
        endif()
        string(FIND "${_stamp_header_b}" "${_stamp_head_b}" _stamp_b_position)
        if(_stamp_b_position EQUAL -1)
            message(FATAL_ERROR
                "Rebuilding the stamp target after a commit that changed no "
                "CMake input kept the stale revision: expected "
                "'${_stamp_head_b}' in the rebuilt header, found "
                "'${_stamp_head_a}' (configure-time identity resolution "
                "regressed")
        endif()
        string(FIND "${_stamp_header_b}" "${_stamp_head_a}" _stamp_stale_position)
        if(NOT _stamp_stale_position EQUAL -1)
            message(FATAL_ERROR
                "The rebuilt stamp header still carries the stale revision "
                "'${_stamp_head_a}'")
        endif()
    else()
        message(STATUS "git not found; skipping the checkout identity case")
    endif()

    check_compiled_identity()
    check_incremental_stamp_schedules_consumer()
    # The same A/B cycle under a reproducible-build epoch: string(TIMESTAMP)
    # would substitute 2023-11-14 for the current time and backdate the aged
    # publication below its already-built consumers, so one ordinary build
    # would relink A. The pinned run must behave identically to the unpinned
    # one.
    check_incremental_stamp_schedules_consumer("1700000000")

    file(REMOVE_RECURSE "${_test_root}")

    # Reject a malformed explicit override instead of recording it.
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${SOURCE_ROOT}"
            "-DCONTRACT_CASE=invalid_override"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _invalid_result
        OUTPUT_VARIABLE _invalid_stdout
        ERROR_VARIABLE _invalid_stderr)
    if(_invalid_result EQUAL 0)
        message(STATUS "Invalid override stdout: ${_invalid_stdout}")
        message(FATAL_ERROR "A malformed KISAK_SOURCE_COMMIT override was accepted")
    endif()
    if(NOT _invalid_stderr MATCHES "not a git commit hash")
        message(FATAL_ERROR
            "The invalid-override rejection failed for the wrong reason: "
            "${_invalid_stderr}")
    endif()

    # --- Mutation self-checks ---------------------------------------------
    foreach(_mutation IN ITEMS
        strip_export_subst
        sh_header_macro
        cmd_header_macro
        cmake_resolver_call
        stamp_delegation
        stamp_override_forward
        publish_edge
        stamp_publish_guard
        publish_aging
        publish_clock_source
        publish_marker_early
        stamp_superseded_marker
        stamp_marker_cleanup
        cpp_consumer
        cpp_getter
        cpp_retention
        sh_header_getter
        cmd_header_getter
        test_registration
        carrier_grammar)
        execute_process(
            COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_ROOT=${SOURCE_ROOT}"
                "-DCONTRACT_MUTATION=${_mutation}"
                -P "${CMAKE_CURRENT_LIST_FILE}"
            RESULT_VARIABLE _mutation_result
            OUTPUT_VARIABLE _mutation_stdout
            ERROR_VARIABLE _mutation_stderr)
        if(_mutation_result EQUAL 0)
            message(STATUS "Mutation stdout: ${_mutation_stdout}")
            message(STATUS "Mutation stderr: ${_mutation_stderr}")
            message(FATAL_ERROR
                "Source-identity contract accepted mutation: ${_mutation}")
        endif()
    endforeach()
endif()

message(STATUS "Source-archive identity contract passed")

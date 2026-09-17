cmake_minimum_required(VERSION 3.16)

# Source-archive identity contract.
#
# The build must be able to record which revision produced it. A `git archive`
# source tarball has no `.git`, so the revision travels in the substituted
# src/source_identity.txt (src/.gitattributes marks it `export-subst`), and
# scripts/extern/resolve_source_identity.cmake prefers an explicit override,
# then a working checkout, then that carrier. These checks execute the resolver
# against controlled trees and pin the wiring that carries the resolved commit
# into the generated src/buildnumber.h.

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

# Prove the resolved commit reaches a compiled artifact from a git-free source
# tree. The real src/buildnumber.cpp is compiled against a header stamped the
# same way increment_build.sh/.cmd stamp it, and the linked binary must print
# the carrier's commit. Before the accessor existed the macro was written to
# the generated header but never referenced, so the released binary carried no
# source identity at all.
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
    file(WRITE "${_tree}/src/buildnumber.h"
        "#pragma once\n"
        "#define BUILD_NUMBER 1\n"
        "#define KISAK_SOURCE_COMMIT \"${_resolved}\"\n"
        "\n"
        "char* getBuildNumber();\n"
        "int getBuildNumberAsInt();\n"
        "const char* getSourceCommit();\n")
    file(WRITE "${_tree}/main.cpp"
        "#include <cstdio>\n"
        "#include \"buildnumber.h\"\n"
        "int main() { std::puts(getSourceCommit()); return 0; }\n")
    # Build the fixture through the configured CMake toolchain/generator rather
    # than executing the compiler directly. A Visual Studio generator drives
    # cl.exe through MSBuild, which initializes the compiler's INCLUDE/LIB
    # environment; a bare cl.exe spawned from `cmake -P` inherits neither and
    # fails with C1083 (cannot open <stdio.h>). The nested project compiles the
    # same real src/buildnumber.cpp accessor, so the exact identity assertion
    # still runs against a genuine compiled consumer on every platform.
    file(WRITE "${_tree}/CMakeLists.txt"
        "cmake_minimum_required(VERSION 3.16)\n"
        "project(kisak_archive_identity_fixture CXX)\n"
        "add_executable(identity-check\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/src/buildnumber.cpp\"\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/main.cpp\")\n"
        "target_include_directories(identity-check PRIVATE\n"
        "    \"\${CMAKE_CURRENT_SOURCE_DIR}/src\")\n"
        "set_target_properties(identity-check PROPERTIES\n"
        "    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)\n")
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
    # ``identity-check.exe``, and keying on MSVC alone would miss the MinGW and
    # clang-cl fixtures even though they built correctly.
    if(CMAKE_HOST_WIN32)
        set(_exe_name "identity-check.exe")
    else()
        set(_exe_name "identity-check")
    endif()
    file(GLOB_RECURSE _exe_candidates "${_build}/${_exe_name}")
    list(FILTER _exe_candidates EXCLUDE REGEX "/CMakeFiles/")
    if(NOT _exe_candidates)
        message(FATAL_ERROR
            "The compiled archive-build identity fixture produced no ${_exe_name}")
    endif()
    list(GET _exe_candidates 0 _exe)
    execute_process(
        COMMAND "${_exe}"
        OUTPUT_VARIABLE _compiled_identity
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _run_result)
    if(NOT _run_result EQUAL 0)
        message(FATAL_ERROR "The compiled archive-build identity check did not run")
    endif()
    if(NOT _compiled_identity STREQUAL "${_archive_commit}")
        message(FATAL_ERROR
            "The compiled archive build did not record its source commit: "
            "expected '${_archive_commit}', found '${_compiled_identity}'")
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
            "kisak_resolve_source_identity(\"\${CMAKE_SOURCE_DIR}\" KISAK_RESOLVED_SOURCE_COMMIT)"
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

# Configure resolves the identity and passes it through to the stamp script.
require_contains(
    _cmake "include(\"\${SCRIPTS_DIR}/extern/resolve_source_identity.cmake\")"
    "increment_build.cmake includes the resolver")
require_contains(
    _cmake "kisak_resolve_source_identity(\"\${CMAKE_SOURCE_DIR}\" KISAK_RESOLVED_SOURCE_COMMIT)"
    "increment_build.cmake resolves CMAKE_SOURCE_DIR")
require_contains(
    _cmake "\"\${KISAK_RESOLVED_SOURCE_COMMIT}\""
    "increment_build.cmake passes the resolved commit to the stamp script")

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
require_contains(
    _buildnumber_cpp "KISAK_SOURCE_COMMIT"
    "the compiled build-number TU references the source commit macro")
require_contains(
    _buildnumber_cpp "getSourceCommit"
    "the compiled build-number TU defines the source-commit accessor")

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
    else()
        message(STATUS "git not found; skipping the checkout identity case")
    endif()

    check_compiled_identity()

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
        cpp_consumer
        cpp_getter
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

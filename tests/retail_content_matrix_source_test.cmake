cmake_minimum_required(VERSION 3.16)

# Guards the A11 retail-content / MP mod compatibility regression matrix
# (docs/RETAIL_CONTENT_MATRIX.md, fork issue #133). The document carries a
# machine-readable axis/case/disposition index; this test fails closed if a
# required target, commercial profile, configuration mode, case family or
# upstream (#89/#40) disposition is dropped.
#
# When run as the primary test it additionally exercises its own negative
# paths by mutating copies of the document and asserting the validator
# rejects them, so a silently-weakened contract cannot pass.

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

if(DEFINED MATRIX_DOC AND NOT MATRIX_DOC STREQUAL "")
    set(_matrix_doc "${MATRIX_DOC}")
else()
    set(_matrix_doc "${SOURCE_ROOT}/docs/RETAIL_CONTENT_MATRIX.md")
endif()

if(NOT EXISTS "${_matrix_doc}")
    message(FATAL_ERROR "Missing retail-content matrix document: ${_matrix_doc}")
endif()

file(READ "${_matrix_doc}" _matrix_text)

function(validate_retail_content_matrix DOC_PATH DOC_TEXT)
    set(_begin_marker "<!-- retail-content-matrix:v1")
    set(_end_marker "-->")
    string(FIND "${DOC_TEXT}" "${_begin_marker}" _begin_pos)
    if(_begin_pos EQUAL -1)
        message(FATAL_ERROR
            "Missing '${_begin_marker}' index marker in ${DOC_PATH}")
    endif()
    string(SUBSTRING "${DOC_TEXT}" ${_begin_pos} -1 _tail)
    string(FIND "${_tail}" "${_end_marker}" _end_pos)
    if(_end_pos LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing '${_end_marker}' index terminator in ${DOC_PATH}")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end_pos} _block)

    string(REPLACE "\r\n" "\n" _block "${_block}")
    # Escape semicolons so free-text evidence notes survive CMake list storage.
    string(REPLACE ";" "\\;" _block "${_block}")
    string(REPLACE "\n" ";" _index_lines "${_block}")

    set(_targets "")
    set(_profiles "")
    set(_profile_kinds "")
    set(_modes "")
    set(_cases "")
    set(_families "")
    set(_dispositions "")

    foreach(_line IN LISTS _index_lines)
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES "^<!--" OR _line MATCHES "^#")
            continue()
        endif()
        if(_line MATCHES "^target[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _targets "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^profile[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _profiles "${CMAKE_MATCH_1}")
            list(APPEND _profile_kinds "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^mode[ \t]+([^ \t]+)$")
            list(APPEND _modes "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^case[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _cases "${CMAKE_MATCH_1}")
            list(APPEND _families "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^disposition[ \t]+([^ \t]+)[ \t]+([^ \t]+).*$")
            list(APPEND _dispositions "${CMAKE_MATCH_1}")
        else()
            message(FATAL_ERROR
                "Unrecognized retail-content matrix index line in ${DOC_PATH}: '${_line}'")
        endif()
    endforeach()

    set(_required_targets
        win-amd64 win-arm64 linux-amd64 linux-arm64 macos-arm64)
    foreach(_target IN LISTS _required_targets)
        list(FIND _targets "${_target}" _target_index)
        if(_target_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing required target '${_target}' in ${DOC_PATH}")
        endif()
    endforeach()
    list(FIND _targets "win-x86" _reference_target_index)
    if(_reference_target_index EQUAL -1)
        message(FATAL_ERROR
            "Retail-content matrix is missing the win-x86 reference platform in ${DOC_PATH}")
    endif()

    set(_required_profiles original-commercial-1.7 steam-commercial-1.8)
    foreach(_profile IN LISTS _required_profiles)
        list(FIND _profiles "${_profile}" _profile_index)
        if(_profile_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing required commercial profile '${_profile}' in ${DOC_PATH}")
        endif()
        list(GET _profile_kinds ${_profile_index} _profile_kind)
        if(NOT _profile_kind STREQUAL "commercial")
            message(FATAL_ERROR
                "Retail-content matrix profile '${_profile}' must be class 'commercial', found '${_profile_kind}' in ${DOC_PATH}")
        endif()
    endforeach()

    foreach(_mode IN ITEMS listen dedicated)
        list(FIND _modes "${_mode}" _mode_index)
        if(_mode_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing required configuration mode '${_mode}' in ${DOC_PATH}")
        endif()
    endforeach()

    set(_required_families
        stock-map fastfile-mod raw-mod download-pure demo upstream-89 upstream-40)
    foreach(_family IN LISTS _required_families)
        list(FIND _families "${_family}" _family_index)
        if(_family_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix has no case in required family '${_family}' in ${DOC_PATH}")
        endif()
    endforeach()

    foreach(_upstream IN ITEMS upstream-89 upstream-40)
        list(FIND _dispositions "${_upstream}" _disposition_index)
        if(_disposition_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing the '${_upstream}' reproduction disposition in ${DOC_PATH}")
        endif()
    endforeach()

    list(LENGTH _cases _case_count)
    list(REMOVE_DUPLICATES _cases)
    list(LENGTH _cases _unique_case_count)
    if(NOT _case_count EQUAL _unique_case_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate case ids in ${DOC_PATH}")
    endif()

    # The document must keep its non-claim language: licensed references are
    # unavailable and a missing reference is a blocker, not a pass.
    foreach(_needle "unmodified" "blocker" "not available in this checkout")
        string(FIND "${DOC_TEXT}" "${_needle}" _needle_pos)
        if(_needle_pos EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing required non-claim phrase '${_needle}' in ${DOC_PATH}")
        endif()
    endforeach()
endfunction()

validate_retail_content_matrix("${_matrix_doc}" "${_matrix_text}")

if(DEFINED MATRIX_SELFTEST_ONLY)
    # Negative-test recursion: the caller has already checked the exit code.
    return()
endif()

# Negative self-tests: each mutation must be rejected by the validator.
if(DEFINED WORK_DIR AND NOT WORK_DIR STREQUAL "")
    set(_scratch_root "${WORK_DIR}")
else()
    set(_scratch_root "${CMAKE_CURRENT_LIST_DIR}")
endif()
set(_scratch_dir "${_scratch_root}/retail-content-matrix-selftest")
file(MAKE_DIRECTORY "${_scratch_dir}")

function(expect_rejected CASE_NAME MUTATED_TEXT)
    set(_mutated_path "${_scratch_dir}/${CASE_NAME}.md")
    file(WRITE "${_mutated_path}" "${MUTATED_TEXT}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_ROOT=${SOURCE_ROOT}"
            "-DMATRIX_DOC=${_mutated_path}"
            "-DMATRIX_SELFTEST_ONLY=1"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _rc
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    if(_rc EQUAL 0)
        message(FATAL_ERROR
            "Negative self-test '${CASE_NAME}' unexpectedly passed validation")
    endif()
endfunction()

string(REPLACE "target linux-amd64 production\n" "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-target' did not apply")
endif()
expect_rejected("drop-target" "${_mutated}")

string(REPLACE
    "disposition upstream-89 blocked unavailable named-mod and licensed retail fixtures, no reproduction claimed\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-upstream-89' did not apply")
endif()
expect_rejected("drop-upstream-89" "${_mutated}")

string(REPLACE "case MOD-01 fastfile-mod\n" "" _mutated "${_matrix_text}")
string(REPLACE "case MOD-02 fastfile-mod\n" "" _mutated "${_mutated}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-fastfile-family' did not apply")
endif()
expect_rejected("drop-fastfile-family" "${_mutated}")

file(REMOVE_RECURSE "${_scratch_dir}")

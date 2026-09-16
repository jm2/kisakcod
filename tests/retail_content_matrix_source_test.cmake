cmake_minimum_required(VERSION 3.16)

# Guards the A11 retail-content / MP mod compatibility regression matrix
# (docs/RETAIL_CONTENT_MATRIX.md, fork issue #133). The document carries a
# machine-readable axis/case/direction/disposition index; this test fails closed
# if a required target (with its production/reference role), commercial profile,
# configuration mode, commercial session direction, named case (with its
# intended family), case×direction child record or aggregate completeness policy
# is dropped, if the §4 catalog and the index disagree about which cases exist,
# or if an unavailable-evidence upstream disposition is promoted away from
# 'blocked'.
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
    set(_target_roles "")
    set(_profiles "")
    set(_profile_kinds "")
    set(_modes "")
    set(_directions "")
    set(_direction_kinds "")
    set(_cases "")
    set(_families "")
    set(_outcome_pairs "")
    set(_completeness_keys "")
    set(_completeness_values "")
    set(_dispositions "")
    set(_disposition_statuses "")

    foreach(_line IN LISTS _index_lines)
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES "^<!--" OR _line MATCHES "^#")
            continue()
        endif()
        if(_line MATCHES "^target[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _targets "${CMAKE_MATCH_1}")
            list(APPEND _target_roles "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^profile[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _profiles "${CMAKE_MATCH_1}")
            list(APPEND _profile_kinds "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^mode[ \t]+([^ \t]+)$")
            list(APPEND _modes "${CMAKE_MATCH_1}")
        elseif(_line MATCHES "^direction[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _directions "${CMAKE_MATCH_1}")
            list(APPEND _direction_kinds "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^case[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _cases "${CMAKE_MATCH_1}")
            list(APPEND _families "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^outcome[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _outcome_pairs "${CMAKE_MATCH_1} ${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^completeness[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _completeness_keys "${CMAKE_MATCH_1}")
            list(APPEND _completeness_values "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^disposition[ \t]+([^ \t]+)[ \t]+([^ \t]+).*$")
            list(APPEND _dispositions "${CMAKE_MATCH_1}")
            list(APPEND _disposition_statuses "${CMAKE_MATCH_2}")
        else()
            message(FATAL_ERROR
                "Unrecognized retail-content matrix index line in ${DOC_PATH}: '${_line}'")
        endif()
    endforeach()

    set(_required_production_targets
        win-amd64 win-arm64 linux-amd64 linux-arm64 macos-arm64)
    foreach(_target IN LISTS _required_production_targets)
        list(FIND _targets "${_target}" _target_index)
        if(_target_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing required target '${_target}' in ${DOC_PATH}")
        endif()
        list(GET _target_roles ${_target_index} _target_role)
        if(NOT _target_role STREQUAL "production")
            message(FATAL_ERROR
                "Retail-content matrix target '${_target}' must have role 'production', found '${_target_role}' in ${DOC_PATH}")
        endif()
    endforeach()
    list(FIND _targets "win-x86" _reference_target_index)
    if(_reference_target_index EQUAL -1)
        message(FATAL_ERROR
            "Retail-content matrix is missing the win-x86 reference platform in ${DOC_PATH}")
    endif()
    list(GET _target_roles ${_reference_target_index} _reference_target_role)
    if(NOT _reference_target_role STREQUAL "reference")
        message(FATAL_ERROR
            "Retail-content matrix target 'win-x86' must have role 'reference', found '${_reference_target_role}' in ${DOC_PATH}")
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

    # Session directions. The two commercial directions are required and must be
    # classed commercial; kc-kc is supplemental and never satisfies a commercial
    # cell. Without this, the outcome key can silently collapse back to an
    # aggregate target/mode/profile cell.
    set(_required_commercial_directions
        kc-server-commercial-client kc-client-commercial-server)
    foreach(_direction IN LISTS _required_commercial_directions)
        list(FIND _directions "${_direction}" _direction_index)
        if(_direction_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing required session direction '${_direction}' in ${DOC_PATH}")
        endif()
        list(GET _direction_kinds ${_direction_index} _direction_kind)
        if(NOT _direction_kind STREQUAL "commercial")
            message(FATAL_ERROR
                "Retail-content matrix direction '${_direction}' must be class 'commercial', found '${_direction_kind}' in ${DOC_PATH}")
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

    # Required named cases: family presence alone is not enough, because a named
    # map-cycle/download/demo case can vanish while its family still has other
    # members (e.g. dropping SM-03 leaves stock-map populated by SM-01/SM-02).
    # Each required case id must be present AND carry its intended family.
    set(_required_cases
        "SM-01 stock-map"
        "SM-02 stock-map"
        "SM-03 stock-map"
        "MOD-01 fastfile-mod"
        "MOD-02 fastfile-mod"
        "MOD-03 raw-mod"
        "PC-01 download-pure"
        "PC-02 download-pure"
        "PC-03 download-pure"
        "PC-04 download-pure"
        "DEMO-01 demo"
        "DEMO-02 demo"
        "DEMO-03 demo"
        "UP89-01 upstream-89"
        "UP89-02 upstream-89"
        "UP40-01 upstream-40")
    foreach(_entry IN LISTS _required_cases)
        string(REPLACE " " ";" _parts "${_entry}")
        list(GET _parts 0 _required_case_id)
        list(GET _parts 1 _required_case_family)
        list(FIND _cases "${_required_case_id}" _case_index)
        if(_case_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing required named case '${_required_case_id}' in ${DOC_PATH}")
        endif()
        list(GET _families ${_case_index} _case_family)
        if(NOT _case_family STREQUAL "${_required_case_family}")
            message(FATAL_ERROR
                "Retail-content matrix case '${_required_case_id}' must belong to family '${_required_case_family}', found '${_case_family}' in ${DOC_PATH}")
        endif()
    endforeach()

    # Case×direction child records. §2.4 requires each direction to be recorded
    # separately and §4 declares the case ids to be the outcome keys, so an
    # aggregate cell must not be able to pass while a required case or direction
    # child is absent. Every required named case must declare BOTH commercial
    # directions, and every declared child must reference a known case and a
    # known direction exactly once.
    list(LENGTH _outcome_pairs _pair_count)
    if(_pair_count EQUAL 0)
        message(FATAL_ERROR
            "Retail-content matrix declares no case×direction outcome child records in ${DOC_PATH}")
    endif()
    list(REMOVE_DUPLICATES _outcome_pairs)
    list(LENGTH _outcome_pairs _unique_pair_count)
    if(NOT _pair_count EQUAL _unique_pair_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate case×direction outcome child records in ${DOC_PATH}")
    endif()
    foreach(_pair IN LISTS _outcome_pairs)
        string(REPLACE " " ";" _pair_parts "${_pair}")
        list(GET _pair_parts 0 _pair_case)
        list(GET _pair_parts 1 _pair_direction)
        list(FIND _cases "${_pair_case}" _pair_case_index)
        if(_pair_case_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix outcome child references unknown case '${_pair_case}' in ${DOC_PATH}")
        endif()
        list(FIND _directions "${_pair_direction}" _pair_direction_index)
        if(_pair_direction_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix outcome child references unknown direction '${_pair_direction}' in ${DOC_PATH}")
        endif()
    endforeach()
    foreach(_entry IN LISTS _required_cases)
        string(REPLACE " " ";" _parts "${_entry}")
        list(GET _parts 0 _required_case_id)
        foreach(_direction IN LISTS _required_commercial_directions)
            list(FIND _outcome_pairs "${_required_case_id} ${_direction}" _required_pair_index)
            if(_required_pair_index EQUAL -1)
                message(FATAL_ERROR
                    "Retail-content matrix is missing required case×direction child record '${_required_case_id} ${_direction}' in ${DOC_PATH}")
            endif()
        endforeach()
    endforeach()

    # Explicit aggregate completeness policy: an aggregate target/mode/profile
    # cell is Pass only when every required case×direction child is Pass. This
    # keeps a missing case or direction from ever counting as a pass.
    list(FIND _completeness_keys "aggregate" _completeness_index)
    if(_completeness_index EQUAL -1)
        message(FATAL_ERROR
            "Retail-content matrix is missing the aggregate completeness policy in ${DOC_PATH}")
    endif()
    list(GET _completeness_values ${_completeness_index} _aggregate_policy)
    if(NOT _aggregate_policy STREQUAL "pass-requires-all-case-directions")
        message(FATAL_ERROR
            "Retail-content matrix aggregate completeness policy must be 'pass-requires-all-case-directions', found '${_aggregate_policy}' in ${DOC_PATH}")
    endif()

    # Upstream dispositions must stay blocked while the licensed references are
    # unavailable: the status token is required, not just the upstream id, so it
    # cannot be silently promoted to 'pass'.
    foreach(_upstream IN ITEMS upstream-89 upstream-40)
        list(FIND _dispositions "${_upstream}" _disposition_index)
        if(_disposition_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix is missing the '${_upstream}' reproduction disposition in ${DOC_PATH}")
        endif()
        list(GET _disposition_statuses ${_disposition_index} _disposition_status)
        if(NOT _disposition_status STREQUAL "blocked")
            message(FATAL_ERROR
                "Retail-content matrix '${_upstream}' disposition must stay 'blocked' while the licensed reference is unavailable, found '${_disposition_status}' in ${DOC_PATH}")
        endif()
    endforeach()

    list(LENGTH _cases _case_count)
    list(REMOVE_DUPLICATES _cases)
    list(LENGTH _cases _unique_case_count)
    if(NOT _case_count EQUAL _unique_case_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate case ids in ${DOC_PATH}")
    endif()

    # Catalog/index membership cross-check: the §4 case catalog and the
    # machine-readable index must name exactly the same cases. Without this, a
    # case can be dropped from one side (e.g. the catalog row removed) while the
    # other side still lists it, so the contract shrinks unnoticed.
    string(FIND "${DOC_TEXT}" "## 4." _catalog_begin)
    string(FIND "${DOC_TEXT}" "## 5." _catalog_end)
    if(_catalog_begin EQUAL -1 OR _catalog_end EQUAL -1 OR _catalog_end LESS_EQUAL _catalog_begin)
        message(FATAL_ERROR
            "Retail-content matrix is missing the §4 catalog or §5 section in ${DOC_PATH}")
    endif()
    math(EXPR _catalog_length "${_catalog_end} - ${_catalog_begin}")
    string(SUBSTRING "${DOC_TEXT}" ${_catalog_begin} ${_catalog_length} _catalog_text)
    string(REGEX MATCHALL "`[A-Z][A-Z0-9]*-[0-9]+`" _catalog_tokens "${_catalog_text}")
    set(_catalog_cases "")
    foreach(_token IN LISTS _catalog_tokens)
        string(REPLACE "`" "" _catalog_case "${_token}")
        list(APPEND _catalog_cases "${_catalog_case}")
    endforeach()
    list(REMOVE_DUPLICATES _catalog_cases)
    list(SORT _catalog_cases)
    list(SORT _cases)
    if(NOT "${_catalog_cases}" STREQUAL "${_cases}")
        message(FATAL_ERROR
            "Retail-content matrix catalog cases [${_catalog_cases}] do not match index cases [${_cases}] in ${DOC_PATH}")
    endif()

    # The document must keep its non-claim language and the case×direction
    # outcome schema: licensed references are unavailable, a missing reference is
    # a blocker, and an aggregate cell may never exceed its weakest required
    # child.
    foreach(_needle
        "unmodified"
        "blocker"
        "not available in this checkout"
        "child record"
        "weakest required child"
        "pass-requires-all-case-directions")
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

# Remove exactly ONE member of a multi-case family. stock-map still has SM-01
# and SM-02, so a family-presence-only guard would accept the shrink; the
# required-named-case check must reject it.
string(REPLACE "case SM-03 stock-map\n" "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-stock-map-member' did not apply")
endif()
expect_rejected("drop-stock-map-member" "${_mutated}")

# Flip a target's role while keeping its id. The win-x86 reference platform
# must not be relabelled production (nor a production target relabelled
# reference); the role check must reject it.
string(REPLACE "target win-x86 reference\n" "target win-x86 production\n" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'change-target-role' did not apply")
endif()
expect_rejected("change-target-role" "${_mutated}")

# Rename a case id in the §4 catalog only, leaving the index untouched: the
# catalog/index membership cross-check must reject the divergence.
string(REPLACE
    "`SM-03` | `map`/`map_rotate`"
    "`SM-99` | `map`/`map_rotate`"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'catalog-index-mismatch' did not apply")
endif()
expect_rejected("catalog-index-mismatch" "${_mutated}")

# Promote the unavailable-evidence upstream dispositions to 'pass'. The guard
# must store and require the status token, not just the upstream id, or a
# blocked commercial reference could be silently certified by editing one word.
string(REPLACE
    "disposition upstream-89 blocked"
    "disposition upstream-89 pass"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-upstream-89' did not apply")
endif()
expect_rejected("promote-upstream-89" "${_mutated}")

string(REPLACE
    "disposition upstream-40 blocked"
    "disposition upstream-40 pass"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-upstream-40' did not apply")
endif()
expect_rejected("promote-upstream-40" "${_mutated}")

# Drop one commercial session direction. The outcome key must not collapse back
# to an aggregate target/mode/profile cell.
string(REPLACE
    "direction kc-client-commercial-server commercial\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-commercial-direction' did not apply")
endif()
expect_rejected("drop-commercial-direction" "${_mutated}")

# Drop the aggregate completeness policy: an aggregate pass would then no longer
# be tied to all required case×direction children.
string(REPLACE
    "completeness aggregate pass-requires-all-case-directions\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-completeness-policy' did not apply")
endif()
expect_rejected("drop-completeness-policy" "${_mutated}")

# Drop ONE case×direction child record. The case and direction still exist
# elsewhere, so only the per-case child-coverage check can catch the shrink.
string(REPLACE
    "outcome SM-03 kc-server-commercial-client\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-case-direction-child' did not apply")
endif()
expect_rejected("drop-case-direction-child" "${_mutated}")

# Remove the weakest-child bound from the §6 schema text: the aggregate cell
# could then be read as standalone evidence again.
string(REPLACE
    "weakest required child"
    "aggregate summary"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-aggregate-bound' did not apply")
endif()
expect_rejected("drop-aggregate-bound" "${_mutated}")

file(REMOVE_RECURSE "${_scratch_dir}")

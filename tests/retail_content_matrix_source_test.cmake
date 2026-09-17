cmake_minimum_required(VERSION 3.16)

# Guards the A11 retail-content / MP mod compatibility regression matrix
# (docs/RETAIL_CONTENT_MATRIX.md, fork issue #133). The document carries a
# machine-readable axis/case/applicability/direction/disposition index; this test
# fails closed if a required target (with its production/reference role),
# target-role capability, commercial profile, configuration mode, commercial
# session direction, named case (with its intended family), case-mode
# applicability, applicable case×mode×direction child record, §6.2 child ledger
# case coverage or per-direction result/evidence cell, §6.1 full-key child
# record (exact `(target, mode, profile, case, direction)` coverage, uniqueness,
# axis applicability and `Blocked / none` state, with the pinned record count
# and the child-ledger completeness policy), §6.3 aggregate
# cell/direction scope or per-profile result/evidence cell (commercial cells
# held at 'Blocked / none', the non-derived kisakcod-self supplemental
# annotation held at 'Supplemental / none' and never enrolled in the
# commercial ledger), aggregate completeness policy, or disposition uniqueness
# is dropped or an inapplicable mode/role requirement is introduced, if the
# §4 catalog rows and the index disagree about which cases exist (membership
# is read from the first cell of actual catalog table rows, so a prose
# cross-reference cannot supply a deleted row's id), if a §6.2 displayed
# Modes cell narrows, widens, duplicates or otherwise diverges from the
# case's canonical case-mode declaration (the per-case view must display
# exactly the applicability the index declares), or if an
# unavailable-evidence upstream disposition, commercial §6.2 child cell,
# full-key child record, §6.3 aggregate cell or §4.5 visible upstream
# disposition label is promoted away from 'Blocked / none' / '**Blocked**'.
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
    set(_target_role_targets "")
    set(_target_role_values "")
    set(_profiles "")
    set(_profile_kinds "")
    set(_modes "")
    set(_directions "")
    set(_direction_kinds "")
    set(_cases "")
    set(_families "")
    set(_case_mode_cases "")
    set(_case_mode_values "")
    set(_outcome_pairs "")
    set(_child_records "")
    set(_child_statuses "")
    set(_child_evidences "")
    set(_child_count_declared "")
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
        elseif(_line MATCHES "^target-role[ \t]+([^ \t]+)[ \t]+(.+)$")
            list(APPEND _target_role_targets "${CMAKE_MATCH_1}")
            list(APPEND _target_role_values "${CMAKE_MATCH_2}")
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
        elseif(_line MATCHES "^case-mode[ \t]+([^ \t]+)[ \t]+(.+)$")
            list(APPEND _case_mode_cases "${CMAKE_MATCH_1}")
            list(APPEND _case_mode_values "${CMAKE_MATCH_2}")
        elseif(_line MATCHES "^outcome[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            list(APPEND _outcome_pairs "${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3}")
        elseif(_line MATCHES "^child[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
            # §6.1 full-key child record: the canonical per-child unit of
            # evidence, keyed by every declared axis. Fields after the key are
            # the current status and evidence-ref.
            list(APPEND _child_records
                "${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4} ${CMAKE_MATCH_5}")
            list(APPEND _child_statuses "${CMAKE_MATCH_6}")
            list(APPEND _child_evidences "${CMAKE_MATCH_7}")
        elseif(_line MATCHES "^child-count[ \t]+([0-9]+)$")
            set(_child_count_declared "${CMAKE_MATCH_1}")
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

    # Target-role applicability. Each target declares the session roles it can
    # carry; an inapplicable direction must not be required. The declarations are
    # required, known-valued and unique, encode the §2.1 roles exactly (all six
    # targets, including macos-arm64, are dual-role and must stay so), and drive
    # the derived direction scope checked against the §6.3 aggregate table.
    foreach(_target IN LISTS _targets)
        list(FIND _target_role_targets "${_target}" _trole_index)
        if(_trole_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix target '${_target}' has no target-role applicability line in ${DOC_PATH}")
        endif()
        list(GET _target_role_values ${_trole_index} _trole_string)
        string(REPLACE " " ";" _troles "${_trole_string}")
        list(LENGTH _troles _trole_count)
        list(REMOVE_DUPLICATES _troles)
        list(LENGTH _troles _trole_unique_count)
        if(NOT _trole_count EQUAL _trole_unique_count)
            message(FATAL_ERROR
                "Retail-content matrix target '${_target}' has duplicate target-role capabilities in ${DOC_PATH}")
        endif()
        if(_trole_count EQUAL 0)
            message(FATAL_ERROR
                "Retail-content matrix target '${_target}' has no target-role capabilities in ${DOC_PATH}")
        endif()
        foreach(_cap IN LISTS _troles)
            if(NOT _cap STREQUAL "client" AND NOT _cap STREQUAL "server")
                message(FATAL_ERROR
                    "Retail-content matrix target '${_target}' has unknown target-role capability '${_cap}' in ${DOC_PATH}")
            endif()
        endforeach()
    endforeach()
    foreach(_trole_target IN LISTS _target_role_targets)
        list(FIND _targets "${_trole_target}" _trole_known_index)
        if(_trole_known_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix target-role line references unknown target '${_trole_target}' in ${DOC_PATH}")
        endif()
    endforeach()
    list(LENGTH _target_role_targets _trole_line_count)
    set(_target_role_targets_unique ${_target_role_targets})
    list(REMOVE_DUPLICATES _target_role_targets_unique)
    list(LENGTH _target_role_targets_unique _trole_line_unique_count)
    if(NOT _trole_line_count EQUAL _trole_line_unique_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate target-role applicability lines in ${DOC_PATH}")
    endif()
    # Hardcoded §2.1 role invariants: losing a dual-role capability is exactly
    # the commercial-interoperability coverage reduction this guard must reject.
    # Every requested target — including macos-arm64, which delivers the MoltenVK
    # client and the headless server — must keep both capabilities; demoting any
    # of them to client-only would drop the original-client → native-server
    # direction and is rejected.
    foreach(_target IN ITEMS win-amd64 win-arm64 linux-amd64 linux-arm64 macos-arm64 win-x86)
        list(FIND _target_role_targets "${_target}" _rdr_index)
        list(GET _target_role_values ${_rdr_index} _rdr_string)
        string(REPLACE " " ";" _rdr_caps "${_rdr_string}")
        list(FIND _rdr_caps "client" _rdr_client)
        list(FIND _rdr_caps "server" _rdr_server)
        if(_rdr_client EQUAL -1 OR _rdr_server EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix dual-role target '${_target}' must have both 'client' and 'server' capabilities in ${DOC_PATH}")
        endif()
    endforeach()

    # Derived direction scope per target from its declared capabilities.
    set(_has_server_target FALSE)
    set(_has_client_target FALSE)
    foreach(_target IN LISTS _targets)
        list(FIND _target_role_targets "${_target}" _ds_index)
        list(GET _target_role_values ${_ds_index} _ds_string)
        string(REPLACE " " ";" _ds_caps "${_ds_string}")
        set(_ds_dirs "")
        list(FIND _ds_caps "server" _ds_server)
        list(FIND _ds_caps "client" _ds_client)
        if(NOT _ds_server EQUAL -1)
            set(_has_server_target TRUE)
            list(APPEND _ds_dirs "kc-server-commercial-client")
        endif()
        if(NOT _ds_client EQUAL -1)
            set(_has_client_target TRUE)
            list(APPEND _ds_dirs "kc-client-commercial-server")
        endif()
        set(_target_scope_${_target} "${_ds_dirs}")
    endforeach()

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

    # Case-mode applicability. Each case declares the configuration modes it
    # applies to (e.g. SM-01 listen, SM-02 dedicated). A case contributes a
    # child only in its declared modes, so an aggregate dedicated cell never
    # requires the listen-only SM-01 and a listen cell never requires the
    # dedicated-only SM-02. Declarations are required, known-valued and unique.
    set(_required_case_modes
        "SM-01 listen"
        "SM-02 dedicated"
        "SM-03 listen dedicated"
        "MOD-01 listen dedicated"
        "MOD-02 listen dedicated"
        "MOD-03 listen dedicated"
        "PC-01 listen dedicated"
        "PC-02 listen dedicated"
        "PC-03 listen dedicated"
        "PC-04 listen dedicated"
        "DEMO-01 listen dedicated"
        "DEMO-02 listen dedicated"
        "DEMO-03 listen dedicated"
        "UP89-01 listen"
        "UP89-02 listen"
        "UP40-01 listen dedicated")
    foreach(_cm_case IN LISTS _case_mode_cases)
        list(FIND _cases "${_cm_case}" _cm_known_index)
        if(_cm_known_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix case-mode line references unknown case '${_cm_case}' in ${DOC_PATH}")
        endif()
    endforeach()
    list(LENGTH _case_mode_cases _cm_line_count)
    set(_case_mode_cases_unique ${_case_mode_cases})
    list(REMOVE_DUPLICATES _case_mode_cases_unique)
    list(LENGTH _case_mode_cases_unique _cm_line_unique_count)
    if(NOT _cm_line_count EQUAL _cm_line_unique_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate case-mode applicability lines in ${DOC_PATH}")
    endif()
    foreach(_entry IN LISTS _required_case_modes)
        string(REPLACE " " ";" _rcm_parts "${_entry}")
        list(GET _rcm_parts 0 _rcm_case)
        list(REMOVE_AT _rcm_parts 0)
        list(SORT _rcm_parts)
        list(FIND _case_mode_cases "${_rcm_case}" _rcm_index)
        if(_rcm_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix case '${_rcm_case}' has no case-mode applicability line in ${DOC_PATH}")
        endif()
        list(GET _case_mode_values ${_rcm_index} _rcm_actual_string)
        string(REPLACE " " ";" _rcm_actual "${_rcm_actual_string}")
        foreach(_rcm_mode IN LISTS _rcm_actual)
            list(FIND _modes "${_rcm_mode}" _rcm_mode_index)
            if(_rcm_mode_index EQUAL -1)
                message(FATAL_ERROR
                    "Retail-content matrix case '${_rcm_case}' declares unknown mode '${_rcm_mode}' in ${DOC_PATH}")
            endif()
        endforeach()
        list(SORT _rcm_actual)
        if(NOT "${_rcm_actual}" STREQUAL "${_rcm_parts}")
            message(FATAL_ERROR
                "Retail-content matrix case '${_rcm_case}' applicable modes [${_rcm_actual}] must be [${_rcm_parts}] in ${DOC_PATH}")
        endif()
    endforeach()

    # Applicable case×mode×direction child records. A child is required only when
    # the case applies to the mode and a target capable of the direction's role
    # exists. The declared outcome set must equal exactly this derived set: an
    # applicable child must not be omitted, and an inapplicable mode or an
    # impossible target-role direction must not be required.
    set(_required_outcomes "")
    foreach(_entry IN LISTS _required_cases)
        string(REPLACE " " ";" _parts "${_entry}")
        list(GET _parts 0 _required_case_id)
        list(FIND _case_mode_cases "${_required_case_id}" _ro_cm_index)
        list(GET _case_mode_values ${_ro_cm_index} _ro_modes_string)
        string(REPLACE " " ";" _ro_modes "${_ro_modes_string}")
        foreach(_mode IN LISTS _ro_modes)
            if(_has_server_target)
                list(APPEND _required_outcomes "${_required_case_id} ${_mode} kc-server-commercial-client")
            endif()
            if(_has_client_target)
                list(APPEND _required_outcomes "${_required_case_id} ${_mode} kc-client-commercial-server")
            endif()
        endforeach()
    endforeach()

    list(LENGTH _outcome_pairs _pair_count)
    if(_pair_count EQUAL 0)
        message(FATAL_ERROR
            "Retail-content matrix declares no case×mode×direction outcome child records in ${DOC_PATH}")
    endif()
    list(REMOVE_DUPLICATES _outcome_pairs)
    list(LENGTH _outcome_pairs _unique_pair_count)
    if(NOT _pair_count EQUAL _unique_pair_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate case×mode×direction outcome child records in ${DOC_PATH}")
    endif()
    foreach(_pair IN LISTS _outcome_pairs)
        string(REPLACE " " ";" _pair_parts "${_pair}")
        list(GET _pair_parts 0 _pair_case)
        list(GET _pair_parts 1 _pair_mode)
        list(GET _pair_parts 2 _pair_direction)
        list(FIND _cases "${_pair_case}" _pair_case_index)
        if(_pair_case_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix outcome child references unknown case '${_pair_case}' in ${DOC_PATH}")
        endif()
        list(FIND _modes "${_pair_mode}" _pair_mode_index)
        if(_pair_mode_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix outcome child references unknown mode '${_pair_mode}' in ${DOC_PATH}")
        endif()
        list(FIND _directions "${_pair_direction}" _pair_direction_index)
        if(_pair_direction_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix outcome child references unknown direction '${_pair_direction}' in ${DOC_PATH}")
        endif()
        list(FIND _case_mode_cases "${_pair_case}" _pair_cm_index)
        list(GET _case_mode_values ${_pair_cm_index} _pair_case_modes_string)
        string(REPLACE " " ";" _pair_case_modes "${_pair_case_modes_string}")
        list(FIND _pair_case_modes "${_pair_mode}" _pair_mode_applicable)
        if(_pair_mode_applicable EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix outcome child '${_pair}' requires an inapplicable mode for case '${_pair_case}' in ${DOC_PATH}")
        endif()
        if(_pair_direction STREQUAL "kc-server-commercial-client" AND NOT _has_server_target)
            message(FATAL_ERROR
                "Retail-content matrix outcome child '${_pair}' requires a server-capable target that does not exist in ${DOC_PATH}")
        endif()
        if(_pair_direction STREQUAL "kc-client-commercial-server" AND NOT _has_client_target)
            message(FATAL_ERROR
                "Retail-content matrix outcome child '${_pair}' requires a client-capable target that does not exist in ${DOC_PATH}")
        endif()
    endforeach()

    set(_required_outcomes_sorted ${_required_outcomes})
    list(SORT _required_outcomes_sorted)
    set(_declared_outcomes_sorted ${_outcome_pairs})
    list(SORT _declared_outcomes_sorted)
    if(NOT "${_required_outcomes_sorted}" STREQUAL "${_declared_outcomes_sorted}")
        message(FATAL_ERROR
            "Retail-content matrix applicable outcome children [${_required_outcomes_sorted}] do not match declared outcome children [${_declared_outcomes_sorted}] in ${DOC_PATH}")
    endif()

    # §6.1 full-key child records. The outcome triples above key children by
    # (case, mode, direction) only; the child-record schema keys every declared
    # axis: (target, mode, profile, case, direction). The `child` lines are the
    # canonical generated ledger of applicable commercial children — the
    # outcome set crossed with every target whose role supports the direction
    # and with both commercial profiles — and the guard derives the same set
    # from the declarations, so a record cannot be lost, duplicated, moved to
    # an inapplicable axis or promoted without failing validation. The
    # `child-count` line pins the derived record count so the ledger size is an
    # explicit, reviewable part of the contract.
    list(FIND _completeness_keys "child-ledger" _child_ledger_policy_index)
    if(_child_ledger_policy_index EQUAL -1)
        message(FATAL_ERROR
            "Retail-content matrix is missing the child-ledger completeness policy in ${DOC_PATH}")
    endif()
    list(GET _completeness_values ${_child_ledger_policy_index} _child_ledger_policy)
    if(NOT _child_ledger_policy STREQUAL "full-key-blocked-none")
        message(FATAL_ERROR
            "Retail-content matrix child-ledger completeness policy must be 'full-key-blocked-none', found '${_child_ledger_policy}' in ${DOC_PATH}")
    endif()

    list(LENGTH _child_records _child_record_count)
    if(_child_record_count EQUAL 0)
        message(FATAL_ERROR
            "Retail-content matrix declares no full-key child records in ${DOC_PATH}")
    endif()
    if(_child_count_declared STREQUAL "")
        message(FATAL_ERROR
            "Retail-content matrix is missing the child-count line pinning the full-key record count in ${DOC_PATH}")
    endif()
    if(NOT _child_count_declared EQUAL _child_record_count)
        message(FATAL_ERROR
            "Retail-content matrix child-count ${_child_count_declared} does not match the ${_child_record_count} declared full-key child records in ${DOC_PATH}")
    endif()
    set(_child_records_unique ${_child_records})
    list(REMOVE_DUPLICATES _child_records_unique)
    list(LENGTH _child_records_unique _child_record_unique_count)
    if(NOT _child_record_count EQUAL _child_record_unique_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate full-key child records in ${DOC_PATH}")
    endif()

    # Commercial profiles only: kisakcod-self is supplemental and never fills a
    # commercial child record.
    set(_child_commercial_profiles "")
    foreach(_profile IN LISTS _profiles)
        list(FIND _profiles "${_profile}" _child_profile_index)
        list(GET _profile_kinds ${_child_profile_index} _child_profile_kind)
        if(_child_profile_kind STREQUAL "commercial")
            list(APPEND _child_commercial_profiles "${_profile}")
        endif()
    endforeach()
    if(_child_commercial_profiles STREQUAL "")
        message(FATAL_ERROR
            "Retail-content matrix has no commercial profile to derive full-key children from in ${DOC_PATH}")
    endif()

    # Derive the required full-key child set from the declarations: every
    # target, every applicable case-mode pair, the directions the target's role
    # supports, both commercial profiles. This derivation — not the rendered
    # tables — is what §6.3 aggregate readiness reads.
    set(_required_child_keys "")
    foreach(_target IN LISTS _targets)
        list(FIND _target_role_targets "${_target}" _fk_role_index)
        list(GET _target_role_values ${_fk_role_index} _fk_role_string)
        string(REPLACE " " ";" _fk_caps "${_fk_role_string}")
        set(_fk_dirs "")
        list(FIND _fk_caps "server" _fk_server)
        if(NOT _fk_server EQUAL -1)
            list(APPEND _fk_dirs "kc-server-commercial-client")
        endif()
        list(FIND _fk_caps "client" _fk_client)
        if(NOT _fk_client EQUAL -1)
            list(APPEND _fk_dirs "kc-client-commercial-server")
        endif()
        foreach(_entry IN LISTS _required_cases)
            string(REPLACE " " ";" _fk_parts "${_entry}")
            list(GET _fk_parts 0 _fk_case)
            list(FIND _case_mode_cases "${_fk_case}" _fk_cm_index)
            list(GET _case_mode_values ${_fk_cm_index} _fk_modes_string)
            string(REPLACE " " ";" _fk_modes "${_fk_modes_string}")
            foreach(_fk_mode IN LISTS _fk_modes)
                foreach(_fk_dir IN LISTS _fk_dirs)
                    foreach(_fk_profile IN LISTS _child_commercial_profiles)
                        list(APPEND _required_child_keys
                            "${_target} ${_fk_mode} ${_fk_profile} ${_fk_case} ${_fk_dir}")
                    endforeach()
                endforeach()
            endforeach()
        endforeach()
    endforeach()

    # Per-record validation: known axes, applicable mode, direction within the
    # target's role scope, commercial profile, and the blocked/none state.
    math(EXPR _child_last_index "${_child_record_count} - 1")
    foreach(_ci RANGE ${_child_last_index})
        list(GET _child_records ${_ci} _ck)
        string(REPLACE " " ";" _ck_parts "${_ck}")
        list(GET _ck_parts 0 _ck_target)
        list(GET _ck_parts 1 _ck_mode)
        list(GET _ck_parts 2 _ck_profile)
        list(GET _ck_parts 3 _ck_case)
        list(GET _ck_parts 4 _ck_direction)
        list(FIND _targets "${_ck_target}" _ck_index)
        if(_ck_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' references unknown target '${_ck_target}' in ${DOC_PATH}")
        endif()
        list(FIND _modes "${_ck_mode}" _ck_index)
        if(_ck_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' references unknown mode '${_ck_mode}' in ${DOC_PATH}")
        endif()
        list(FIND _profiles "${_ck_profile}" _ck_index)
        if(_ck_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' references unknown profile '${_ck_profile}' in ${DOC_PATH}")
        endif()
        list(GET _profile_kinds ${_ck_index} _ck_profile_kind)
        if(NOT _ck_profile_kind STREQUAL "commercial")
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' must use a commercial profile, found '${_ck_profile}' in ${DOC_PATH}")
        endif()
        list(FIND _cases "${_ck_case}" _ck_index)
        if(_ck_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' references unknown case '${_ck_case}' in ${DOC_PATH}")
        endif()
        list(FIND _directions "${_ck_direction}" _ck_index)
        if(_ck_index EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' references unknown direction '${_ck_direction}' in ${DOC_PATH}")
        endif()
        list(FIND _case_mode_cases "${_ck_case}" _ck_index)
        list(GET _case_mode_values ${_ck_index} _ck_case_modes_string)
        string(REPLACE " " ";" _ck_case_modes "${_ck_case_modes_string}")
        list(FIND _ck_case_modes "${_ck_mode}" _ck_mode_applicable)
        if(_ck_mode_applicable EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' requires an inapplicable mode for case '${_ck_case}' in ${DOC_PATH}")
        endif()
        set(_ck_scope "${_target_scope_${_ck_target}}")
        list(FIND _ck_scope "${_ck_direction}" _ck_dir_applicable)
        if(_ck_dir_applicable EQUAL -1)
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' requires a direction the target's role does not support in ${DOC_PATH}")
        endif()
        list(GET _child_statuses ${_ci} _ck_status)
        list(GET _child_evidences ${_ci} _ck_evidence)
        if(NOT _ck_status STREQUAL "Blocked" OR NOT _ck_evidence STREQUAL "none")
            message(FATAL_ERROR
                "Retail-content matrix full-key child record '${_ck}' must stay 'Blocked / none' while the licensed reference manifests are unavailable, found '${_ck_status} / ${_ck_evidence}' in ${DOC_PATH}")
        endif()
    endforeach()

    # Exact coverage: the declared records must equal the derived applicable
    # set — a missing record is a coverage loss, an extra record is a wrong
    # axis (or a fabricated applicability), and either must fail.
    set(_required_child_keys_sorted ${_required_child_keys})
    list(SORT _required_child_keys_sorted)
    set(_child_records_sorted ${_child_records})
    list(SORT _child_records_sorted)
    if(NOT "${_required_child_keys_sorted}" STREQUAL "${_child_records_sorted}")
        list(LENGTH _required_child_keys_sorted _required_child_count)
        message(FATAL_ERROR
            "Retail-content matrix full-key child records (${_child_record_count}) do not exactly cover the applicable target×mode×profile×case×direction children (${_required_child_count}) in ${DOC_PATH}")
    endif()

    # §6.2 child-record ledger. Each case row carries one `status / evidence-ref`
    # cell per commercial direction; it is the per-case view of the §6.1
    # full-key child records above, which remain the canonical individually
    # keyed units of evidence. While the licensed reference manifests are
    # unavailable every commercial child cell must stay `Blocked / none`,
    # exactly like the full-key records, the §6.3 aggregates and the upstream
    # dispositions: without parsing these cells, a fabricated `Pass / <log>` in
    # one child cell escapes the guard while the aggregate roll-up still reads
    # all-Blocked.
    string(FIND "${DOC_TEXT}" "### 6.2" _child_begin)
    string(FIND "${DOC_TEXT}" "### 6.3" _child_end)
    if(_child_begin EQUAL -1 OR _child_end EQUAL -1 OR _child_end LESS_EQUAL _child_begin)
        message(FATAL_ERROR
            "Retail-content matrix is missing the §6.2 child ledger or §6.3 section in ${DOC_PATH}")
    endif()
    math(EXPR _child_length "${_child_end} - ${_child_begin}")
    string(SUBSTRING "${DOC_TEXT}" ${_child_begin} ${_child_length} _child_text)
    string(REPLACE "\r\n" "\n" _child_text "${_child_text}")
    string(REPLACE "\n" ";" _child_lines "${_child_text}")
    # Row shape: | `case` | modes | kc-server-commercial-client cell |
    # kc-client-commercial-server cell | stages | required-evidence |.
    # The Modes cell is validated too: the per-case view must display exactly
    # the applicability the index declares. The header and separator rows have
    # no backticked leading cell and never match; a data row reformatted to
    # dodge this regex simply drops its case from the ledger set and fails the
    # coverage check below, so the parser cannot be sidestepped by
    # restructuring a row.
    set(_ledger_cases "")
    foreach(_line IN LISTS _child_lines)
        if(_line MATCHES "^[ \t]*\\|[ \t]*`([^`]+)`[ \t]*\\|[ \t]*([^|]+)\\|([^|]*)\\|([^|]*)\\|")
            set(_ledger_case "${CMAKE_MATCH_1}")
            set(_ledger_modes_cell "${CMAKE_MATCH_2}")
            set(_cell_server "${CMAKE_MATCH_3}")
            set(_cell_client "${CMAKE_MATCH_4}")
            list(FIND _cases "${_ledger_case}" _ledger_known_index)
            if(_ledger_known_index EQUAL -1)
                message(FATAL_ERROR
                    "Retail-content matrix §6.2 child ledger references unknown case '${_ledger_case}' in ${DOC_PATH}")
            endif()
            list(APPEND _ledger_cases "${_ledger_case}")
            # Displayed Modes cell: hold it against the case's canonical
            # case-mode declaration. Narrowing the cell hides required modes
            # from the rendered per-case view, an entry no index line declares
            # invents applicability, and a duplicate entry would let one cell
            # claim a mode twice — so the parsed mode set must equal the
            # declared set exactly, fail closed on an unknown token, and never
            # pass on an empty or duplicated cell (review finding
            # r4039453978).
            string(REPLACE "`" "" _ledger_modes_text "${_ledger_modes_cell}")
            string(REPLACE "," ";" _ledger_modes_raw "${_ledger_modes_text}")
            set(_ledger_modes "")
            foreach(_ledger_mode_token IN LISTS _ledger_modes_raw)
                string(STRIP "${_ledger_mode_token}" _ledger_mode_token)
                if(_ledger_mode_token STREQUAL "")
                    message(FATAL_ERROR
                        "Retail-content matrix §6.2 child ledger case '${_ledger_case}' has an empty Modes entry in ${DOC_PATH}")
                endif()
                list(APPEND _ledger_modes "${_ledger_mode_token}")
            endforeach()
            list(LENGTH _ledger_modes _ledger_modes_count)
            set(_ledger_modes_unique ${_ledger_modes})
            list(REMOVE_DUPLICATES _ledger_modes_unique)
            list(LENGTH _ledger_modes_unique _ledger_modes_unique_count)
            if(NOT _ledger_modes_count EQUAL _ledger_modes_unique_count)
                message(FATAL_ERROR
                    "Retail-content matrix §6.2 child ledger case '${_ledger_case}' has duplicate Modes entries in ${DOC_PATH}")
            endif()
            foreach(_ledger_mode IN LISTS _ledger_modes)
                list(FIND _modes "${_ledger_mode}" _ledger_mode_known)
                if(_ledger_mode_known EQUAL -1)
                    message(FATAL_ERROR
                        "Retail-content matrix §6.2 child ledger case '${_ledger_case}' displays unknown mode '${_ledger_mode}' in ${DOC_PATH}")
                endif()
            endforeach()
            list(FIND _case_mode_cases "${_ledger_case}" _ledger_cm_index)
            if(_ledger_cm_index EQUAL -1)
                message(FATAL_ERROR
                    "Retail-content matrix §6.2 child ledger case '${_ledger_case}' has no case-mode applicability declaration in ${DOC_PATH}")
            endif()
            list(GET _case_mode_values ${_ledger_cm_index} _ledger_cm_declared_string)
            string(REPLACE " " ";" _ledger_cm_declared ${_ledger_cm_declared_string})
            set(_ledger_modes_sorted ${_ledger_modes})
            list(SORT _ledger_modes_sorted)
            set(_ledger_cm_declared_sorted ${_ledger_cm_declared})
            list(SORT _ledger_cm_declared_sorted)
            if(NOT "${_ledger_modes_sorted}" STREQUAL "${_ledger_cm_declared_sorted}")
                message(FATAL_ERROR
                    "Retail-content matrix §6.2 child ledger case '${_ledger_case}' displayed Modes [${_ledger_modes_sorted}] must equal its case-mode declaration [${_ledger_cm_declared_sorted}] in ${DOC_PATH}")
            endif()
            foreach(_ledger_cell IN ITEMS "${_cell_server}" "${_cell_client}")
                string(STRIP "${_ledger_cell}" _ledger_cell)
                string(REPLACE "/" ";" _ledger_fields "${_ledger_cell}")
                list(LENGTH _ledger_fields _ledger_field_count)
                if(NOT _ledger_field_count EQUAL 2)
                    message(FATAL_ERROR
                        "Retail-content matrix §6.2 child ledger case '${_ledger_case}' commercial direction cell must be 'status / evidence-ref', found '${_ledger_cell}' in ${DOC_PATH}")
                endif()
                list(GET _ledger_fields 0 _ledger_status)
                list(GET _ledger_fields 1 _ledger_evidence)
                string(STRIP "${_ledger_status}" _ledger_status)
                string(STRIP "${_ledger_evidence}" _ledger_evidence)
                if(NOT _ledger_status STREQUAL "Blocked" OR NOT _ledger_evidence STREQUAL "none")
                    message(FATAL_ERROR
                        "Retail-content matrix §6.2 child ledger case '${_ledger_case}' commercial direction cell must stay 'Blocked / none' while the licensed reference manifests are unavailable, found '${_ledger_status} / ${_ledger_evidence}' in ${DOC_PATH}")
                endif()
            endforeach()
        endif()
    endforeach()
    # Coverage and uniqueness: dropping a ledger row must not hide a case's
    # child cells from the blocked-status policy, and a duplicated row cannot
    # carry a conflicting claim.
    list(LENGTH _ledger_cases _ledger_count)
    if(_ledger_count EQUAL 0)
        message(FATAL_ERROR
            "Retail-content matrix §6.2 child ledger contains no case rows in ${DOC_PATH}")
    endif()
    list(REMOVE_DUPLICATES _ledger_cases)
    list(LENGTH _ledger_cases _ledger_unique_count)
    if(NOT _ledger_count EQUAL _ledger_unique_count)
        message(FATAL_ERROR
            "Retail-content matrix §6.2 child ledger contains duplicate case rows in ${DOC_PATH}")
    endif()
    set(_ledger_sorted ${_ledger_cases})
    list(SORT _ledger_sorted)
    set(_cases_sorted ${_cases})
    list(SORT _cases_sorted)
    if(NOT "${_ledger_sorted}" STREQUAL "${_cases_sorted}")
        message(FATAL_ERROR
            "Retail-content matrix §6.2 child ledger cases [${_ledger_sorted}] must cover exactly the index cases [${_cases_sorted}] in ${DOC_PATH}")
    endif()

    # §6.3 aggregate table applicability. Every applicable (target, mode) cell
    # must appear exactly once, with a 'Required directions' scope equal to the
    # target's derived role capabilities. This is the aggregate-level expression
    # of target-role applicability: the dual-role macos-arm64 target must require
    # both kc-client-commercial-server and kc-server-commercial-client, so its
    # server direction cannot be silently dropped.
    string(FIND "${DOC_TEXT}" "### 6.3" _agg_begin)
    string(FIND "${DOC_TEXT}" "## 7." _agg_end)
    if(_agg_begin EQUAL -1 OR _agg_end EQUAL -1 OR _agg_end LESS_EQUAL _agg_begin)
        message(FATAL_ERROR
            "Retail-content matrix is missing the §6.3 aggregate roll-up or §7 section in ${DOC_PATH}")
    endif()
    math(EXPR _agg_length "${_agg_end} - ${_agg_begin}")
    string(SUBSTRING "${DOC_TEXT}" ${_agg_begin} ${_agg_length} _agg_text)
    string(REPLACE "\r\n" "\n" _agg_text "${_agg_text}")
    string(REPLACE "\n" ";" _agg_lines "${_agg_text}")
    # §6.3 aggregate row tail: the three per-profile `status / evidence-ref`
    # cells (original-commercial-1.7, steam-commercial-1.8, kisakcod-self).
    set(_aggregate_profile_columns original-commercial-1.7 steam-commercial-1.8)
    set(_agg_cells "")
    foreach(_line IN LISTS _agg_lines)
        if(_line MATCHES "^[ \t]*\\|[ \t]*`([^`]+)`[^|]*\\|[ \t]*(listen|dedicated)[ \t]*\\|[ \t]*([^|]+)[ \t]*\\|(.*)$")
            set(_cell_target "${CMAKE_MATCH_1}")
            set(_cell_mode "${CMAKE_MATCH_2}")
            set(_cell_scope "${CMAKE_MATCH_3}")
            set(_cell_tail "${CMAKE_MATCH_4}")
            string(STRIP "${_cell_scope}" _cell_scope)
            string(REPLACE "`" "" _cell_scope "${_cell_scope}")
            if(_cell_scope STREQUAL "both")
                set(_cell_scope "kc-server-commercial-client kc-client-commercial-server")
            endif()
            string(REPLACE " " ";" _cell_scope_list "${_cell_scope}")
            foreach(_scope_dir IN LISTS _cell_scope_list)
                list(FIND _required_commercial_directions "${_scope_dir}" _scope_dir_index)
                if(_scope_dir_index EQUAL -1)
                    message(FATAL_ERROR
                        "Retail-content matrix §6.3 cell '${_cell_target} ${_cell_mode}' has unknown required direction '${_scope_dir}' in ${DOC_PATH}")
                endif()
            endforeach()
            list(FIND _targets "${_cell_target}" _cell_target_index)
            if(_cell_target_index EQUAL -1)
                message(FATAL_ERROR
                    "Retail-content matrix §6.3 cell references unknown target '${_cell_target}' in ${DOC_PATH}")
            endif()
            set(_expected_scope "${_target_scope_${_cell_target}}")
            set(_cell_scope_sorted ${_cell_scope_list})
            list(SORT _cell_scope_sorted)
            set(_expected_scope_sorted ${_expected_scope})
            list(SORT _expected_scope_sorted)
            if(NOT "${_cell_scope_sorted}" STREQUAL "${_expected_scope_sorted}")
                message(FATAL_ERROR
                    "Retail-content matrix §6.3 cell '${_cell_target} ${_cell_mode}' required directions [${_cell_scope_sorted}] must match target-role scope [${_expected_scope_sorted}] in ${DOC_PATH}")
            endif()
            # The per-profile result/evidence cells are part of the checked
            # contract, not renderer decoration: while the licensed reference
            # manifests are unavailable every commercial aggregate cell must
            # stay `Blocked / none`. Parse the target, mode, direction scope
            # AND both commercial status/evidence cells so promoting an
            # aggregate to `Pass`, or attaching evidence that no run produced,
            # cannot slip through as a one-word edit.
            string(REPLACE "|" ";" _cell_tail_parts "${_cell_tail}")
            list(LENGTH _cell_tail_parts _cell_tail_count)
            # Three profile columns plus the empty segment bounded by the row's
            # trailing pipe.
            if(NOT _cell_tail_count EQUAL 4)
                message(FATAL_ERROR
                    "Retail-content matrix §6.3 cell '${_cell_target} ${_cell_mode}' must carry original-commercial-1.7, steam-commercial-1.8 and kisakcod-self result/evidence cells in ${DOC_PATH}")
            endif()
            foreach(_agg_profile_index 0 1)
                list(GET _aggregate_profile_columns ${_agg_profile_index} _agg_profile)
                list(GET _cell_tail_parts ${_agg_profile_index} _agg_cell_raw)
                string(STRIP "${_agg_cell_raw}" _agg_cell)
                string(REPLACE "/" ";" _agg_cell_fields "${_agg_cell}")
                list(LENGTH _agg_cell_fields _agg_field_count)
                if(NOT _agg_field_count EQUAL 2)
                    message(FATAL_ERROR
                        "Retail-content matrix §6.3 cell '${_cell_target} ${_cell_mode}' ${_agg_profile} must be 'status / evidence-ref', found '${_agg_cell}' in ${DOC_PATH}")
                endif()
                list(GET _agg_cell_fields 0 _agg_status)
                list(GET _agg_cell_fields 1 _agg_evidence)
                string(STRIP "${_agg_status}" _agg_status)
                string(STRIP "${_agg_evidence}" _agg_evidence)
                if(NOT _agg_status STREQUAL "Blocked" OR NOT _agg_evidence STREQUAL "none")
                    message(FATAL_ERROR
                        "Retail-content matrix §6.3 commercial aggregate cell '${_cell_target} ${_cell_mode}' ${_agg_profile} must stay 'Blocked / none' while the licensed reference manifests are unavailable, found '${_agg_status} / ${_agg_evidence}' in ${DOC_PATH}")
                endif()
            endforeach()
            # The kisakcod-self column is a non-derived supplemental
            # annotation, not a readiness result: fork-only runs are never
            # enrolled in the commercial ledger (§6.1 child records are
            # commercial-profile only) and can never substitute for a
            # commercial profile, so the annotation is pinned at exactly
            # 'Supplemental / none'. Without this check the column is counted
            # by the tail-shape test above but never validated, and flipping a
            # cell to 'Pass / <invented>' validates.
            list(GET _cell_tail_parts 2 _sup_cell_raw)
            string(STRIP "${_sup_cell_raw}" _sup_cell)
            string(REPLACE "/" ";" _sup_cell_fields "${_sup_cell}")
            list(LENGTH _sup_cell_fields _sup_field_count)
            if(NOT _sup_field_count EQUAL 2)
                message(FATAL_ERROR
                    "Retail-content matrix §6.3 cell '${_cell_target} ${_cell_mode}' kisakcod-self must be 'status / evidence-ref', found '${_sup_cell}' in ${DOC_PATH}")
            endif()
            list(GET _sup_cell_fields 0 _sup_status)
            list(GET _sup_cell_fields 1 _sup_evidence)
            string(STRIP "${_sup_status}" _sup_status)
            string(STRIP "${_sup_evidence}" _sup_evidence)
            if(NOT _sup_status STREQUAL "Supplemental")
                message(FATAL_ERROR
                    "Retail-content matrix §6.3 cell '${_cell_target} ${_cell_mode}' kisakcod-self is a non-derived supplemental annotation and must stay 'Supplemental', found '${_sup_status}' in ${DOC_PATH}")
            endif()
            if(NOT _sup_evidence STREQUAL "none")
                message(FATAL_ERROR
                    "Retail-content matrix §6.3 cell '${_cell_target} ${_cell_mode}' kisakcod-self supplemental annotation carries no evidence claim, found '${_sup_evidence}' in ${DOC_PATH}")
            endif()
            list(APPEND _agg_cells "${_cell_target} ${_cell_mode}")
        endif()
    endforeach()
    # Expected cells: every target in every mode, exactly once.
    set(_expected_agg_cells "")
    foreach(_target IN LISTS _targets)
        foreach(_mode IN LISTS _modes)
            list(APPEND _expected_agg_cells "${_target} ${_mode}")
        endforeach()
    endforeach()
    set(_agg_cells_sorted ${_agg_cells})
    list(SORT _agg_cells_sorted)
    set(_expected_agg_cells_sorted ${_expected_agg_cells})
    list(SORT _expected_agg_cells_sorted)
    if(NOT "${_agg_cells_sorted}" STREQUAL "${_expected_agg_cells_sorted}")
        message(FATAL_ERROR
            "Retail-content matrix §6.3 aggregate cells [${_agg_cells_sorted}] must list every applicable target×mode cell [${_expected_agg_cells_sorted}] in ${DOC_PATH}")
    endif()
    list(LENGTH _agg_cells _agg_cell_count)
    set(_agg_cells_unique ${_agg_cells})
    list(REMOVE_DUPLICATES _agg_cells_unique)
    list(LENGTH _agg_cells_unique _agg_cell_unique_count)
    if(NOT _agg_cell_count EQUAL _agg_cell_unique_count)
        message(FATAL_ERROR
            "Retail-content matrix §6.3 contains duplicate aggregate cells in ${DOC_PATH}")
    endif()

    # Explicit aggregate completeness policy: an aggregate target/mode/profile
    # cell is Pass only when every applicable case×mode×direction child is Pass.
    # This keeps a missing applicable child from ever counting as a pass.
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

    # Disposition ids are keys, so they must be unique. Reject duplicates
    # BEFORE the status check: list(FIND) selects the first match, so a
    # conflicting `disposition upstream-89 pass` appended after the genuine
    # `blocked` entry would otherwise be silently ignored and the promotion
    # would pass validation.
    list(LENGTH _dispositions _disposition_count)
    set(_dispositions_unique ${_dispositions})
    list(REMOVE_DUPLICATES _dispositions_unique)
    list(LENGTH _dispositions_unique _disposition_unique_count)
    if(NOT _disposition_count EQUAL _disposition_unique_count)
        message(FATAL_ERROR
            "Retail-content matrix contains duplicate disposition ids [${_dispositions}] in ${DOC_PATH}")
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

    # Capture the index's upstream-family case set while the case/family lists
    # are still aligned (the catalog cross-check below sorts _cases): the
    # §4.5 visible-disposition check must hold exactly these cases.
    set(_index_upstream_cases "")
    set(_family_scan_index 0)
    foreach(_case_id IN LISTS _cases)
        list(GET _families ${_family_scan_index} _case_family)
        if(_case_family MATCHES "^upstream-")
            list(APPEND _index_upstream_cases "${_case_id}")
        endif()
        math(EXPR _family_scan_index "${_family_scan_index} + 1")
    endforeach()

    # Catalog/index membership cross-check: the §4 case catalog and the
    # machine-readable index must name exactly the same cases. Without this, a
    # case can be dropped from one side (e.g. the catalog row removed) while the
    # other side still lists it, so the contract shrinks unnoticed. The §4 side
    # is read from the first cell of actual catalog table rows — not from every
    # case-like token in the section: a prose cross-reference (the UP89-02 row's
    # 'depends on `UP89-01`' note) would otherwise supply a deleted row's id
    # and the missing-row document would validate.
    string(FIND "${DOC_TEXT}" "## 4." _catalog_begin)
    string(FIND "${DOC_TEXT}" "## 5." _catalog_end)
    if(_catalog_begin EQUAL -1 OR _catalog_end EQUAL -1 OR _catalog_end LESS_EQUAL _catalog_begin)
        message(FATAL_ERROR
            "Retail-content matrix is missing the §4 catalog or §5 section in ${DOC_PATH}")
    endif()
    math(EXPR _catalog_length "${_catalog_end} - ${_catalog_begin}")
    string(SUBSTRING "${DOC_TEXT}" ${_catalog_begin} ${_catalog_length} _catalog_text)
    string(REPLACE "\r\n" "\n" _catalog_text "${_catalog_text}")
    string(REPLACE "\n" ";" _catalog_lines "${_catalog_text}")
    # Row shape: | `case-id` | ... — the same first-cell convention as the §6.2
    # ledger parser. Header/separator rows have no backticked leading cell and
    # never match; a restructured row drops its case from the parsed set and
    # fails the membership check below, so the parser cannot be dodged by
    # reformatting.
    set(_catalog_cases "")
    foreach(_catalog_line IN LISTS _catalog_lines)
        if(_catalog_line MATCHES "^[ \t]*\\|[ \t]*`([^`]+)`[ \t]*\\|")
            list(APPEND _catalog_cases "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _catalog_cases)
    list(SORT _catalog_cases)
    list(SORT _cases)
    if(NOT "${_catalog_cases}" STREQUAL "${_cases}")
        message(FATAL_ERROR
            "Retail-content matrix catalog cases [${_catalog_cases}] do not match index cases [${_cases}] in ${DOC_PATH}")
    endif()

    # §4.5 visible upstream reproduction dispositions. The index `disposition`
    # lines are machine-readable and are already held at 'blocked', but they
    # are not what a reader sees: the §4.5 catalog rows carry the displayed
    # disposition labels. Promoting a visible '**Blocked**' cell to '**Pass**'
    # while the hidden index entry stays blocked claims an unavailable
    # reproduction in the rendered document, so the actual §4.5 table rows are
    # parsed and every upstream case's visible disposition label is held at
    # '**Blocked**' (review finding r4039453986).
    string(FIND "${DOC_TEXT}" "### 4.5" _upstream_begin)
    string(FIND "${DOC_TEXT}" "## 5." _upstream_end)
    if(_upstream_begin EQUAL -1 OR _upstream_end EQUAL -1 OR _upstream_end LESS_EQUAL _upstream_begin)
        message(FATAL_ERROR
            "Retail-content matrix is missing the §4.5 upstream reproductions section or §5 section in ${DOC_PATH}")
    endif()
    math(EXPR _upstream_length "${_upstream_end} - ${_upstream_begin}")
    string(SUBSTRING "${DOC_TEXT}" ${_upstream_begin} ${_upstream_length} _upstream_text)
    string(REPLACE "\r\n" "\n" _upstream_text "${_upstream_text}")
    # Shield semicolons before line-splitting: the upstream rows carry
    # semicolons inside their cells ('without crashing; failure captured',
    # 'fixtures; depends on `UP89-01`'), and CMake splits list values on
    # every unescaped ';' — an unshielded split would shatter such a row into
    # fragments whose trailing parts never match the row regex, letting the
    # disposition half dodge this check entirely (CMake's own '\;' escaping
    # does not survive the foreach boundary, hence a literal placeholder).
    string(REPLACE ";" "{@}" _upstream_text "${_upstream_text}")
    string(REPLACE "\n" ";" _upstream_lines "${_upstream_text}")
    # Row shape: | `case-id` | upstream | fixture | invariant | disposition |
    # — the same first-cell convention as the §6.2 ledger parser. The header
    # and separator rows have no backticked leading cell and never match; a
    # restructured row drops its case from the parsed set and fails the
    # coverage check below, so the parser cannot be dodged by reformatting.
    set(_upstream_row_cases "")
    foreach(_upstream_line IN LISTS _upstream_lines)
        string(REPLACE "{@}" ";" _upstream_line "${_upstream_line}")
        if(_upstream_line MATCHES "^[ \t]*\\|[ \t]*`([^`]+)`[ \t]*\\|(.*)$")
            set(_upstream_row_case "${CMAKE_MATCH_1}")
            list(FIND _cases "${_upstream_row_case}" _upstream_row_known)
            if(_upstream_row_known EQUAL -1)
                message(FATAL_ERROR
                    "Retail-content matrix §4.5 upstream reproductions table references unknown case '${_upstream_row_case}' in ${DOC_PATH}")
            endif()
            list(APPEND _upstream_row_cases "${_upstream_row_case}")
            # A five-column row carries exactly six pipes. Counting pipes
            # directly avoids the list-splitting trap: the cells themselves
            # contain plain semicolons, which would otherwise turn into
            # phantom list separators and shift every cell index.
            string(REGEX REPLACE "[^|]" "" _upstream_pipes "${_upstream_line}")
            string(LENGTH "${_upstream_pipes}" _upstream_pipe_count)
            if(NOT _upstream_pipe_count EQUAL 6)
                message(FATAL_ERROR
                    "Retail-content matrix §4.5 upstream reproduction row '${_upstream_row_case}' must keep the five-column catalog shape in ${DOC_PATH}")
            endif()
            # The visible disposition is the sixth cell. Shield the cells'
            # semicolons again so the pipe split yields exactly one element
            # per column.
            string(REPLACE ";" "{@}" _upstream_cells_input "${_upstream_line}")
            string(REPLACE "|" ";" _upstream_cells "${_upstream_cells_input}")
            list(GET _upstream_cells 5 _upstream_visible_disposition)
            string(REPLACE "{@}" ";" _upstream_visible_disposition "${_upstream_visible_disposition}")
            string(STRIP "${_upstream_visible_disposition}" _upstream_visible_disposition)
            if(NOT _upstream_visible_disposition MATCHES "^\\*\\*Blocked\\*\\*")
                message(FATAL_ERROR
                    "Retail-content matrix §4.5 upstream reproduction '${_upstream_row_case}' visible disposition must stay '**Blocked**' while the licensed reference is unavailable, found '${_upstream_visible_disposition}' in ${DOC_PATH}")
            endif()
        endif()
    endforeach()
    # Coverage and uniqueness: the §4.5 rows must carry exactly the index's
    # upstream-family cases — a dropped row would take its visible blocked
    # disposition out of the rendered document, a duplicated row cannot be
    # allowed to carry a second label, and an extra row names a case the index
    # never declared upstream.
    list(LENGTH _upstream_row_cases _upstream_row_count)
    set(_upstream_row_cases_unique ${_upstream_row_cases})
    list(REMOVE_DUPLICATES _upstream_row_cases_unique)
    list(LENGTH _upstream_row_cases_unique _upstream_row_unique_count)
    if(NOT _upstream_row_count EQUAL _upstream_row_unique_count)
        message(FATAL_ERROR
            "Retail-content matrix §4.5 upstream reproductions table contains duplicate case rows in ${DOC_PATH}")
    endif()
    set(_index_upstream_cases_sorted ${_index_upstream_cases})
    list(SORT _index_upstream_cases_sorted)
    set(_upstream_row_cases_sorted ${_upstream_row_cases_unique})
    list(SORT _upstream_row_cases_sorted)
    if(NOT "${_upstream_row_cases_sorted}" STREQUAL "${_index_upstream_cases_sorted}")
        message(FATAL_ERROR
            "Retail-content matrix §4.5 upstream reproduction rows [${_upstream_row_cases_sorted}] must cover exactly the index upstream-family cases [${_index_upstream_cases_sorted}] in ${DOC_PATH}")
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
        "pass-requires-all-case-directions"
        "case-mode"
        "target-role"
        "client-only"
        "applicable")
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

# The exact reproduced review bypass: delete the UP89-01 §4 catalog row while
# retaining its non-claim phrase in prose. The UP89-02 row's 'depends on
# `UP89-01`' cross-reference still supplies the id to a section-wide token
# scan, so a token-based membership check passes with the row gone (the literal
# deletion previously failed only because the row happened to carry the
# 'not available in this checkout' phrase). Membership must be read from
# actual catalog table rows, so the missing row is rejected while the
# non-claim prose stays.
string(REPLACE
    "| `UP89-01` | [#89](https://github.com/SwagSoftware/KisakCOD/issues/89) | Named listen-server mods launched on any map (per the upstream report) | Mod launches and loads a stock map without crashing; failure captured with the crashing stage | **Blocked** — named mod content and licensed retail fixtures are not available in this checkout; no reproduction is claimed. |\n"
    "The #89 named-mod launch reproduction is not recorded as a catalog row here; named mod content and licensed retail fixtures are not available in this checkout; no reproduction is claimed.\n"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-catalog-row-up89-01' did not apply")
endif()
expect_rejected("drop-catalog-row-up89-01" "${_mutated}")

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

# Append a CONFLICTING duplicate disposition id after the genuine blocked
# entry. The status check uses list(FIND), which selects the first match, so
# without an explicit duplicate-id rejection the appended 'pass' would be
# ignored and the promotion would validate.
string(REPLACE
    "disposition upstream-89 blocked unavailable named-mod and licensed retail fixtures, no reproduction claimed\n"
    "disposition upstream-89 blocked unavailable named-mod and licensed retail fixtures, no reproduction claimed\ndisposition upstream-89 pass competing duplicate\n"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'conflicting-duplicate-disposition' did not apply")
endif()
expect_rejected("conflicting-duplicate-disposition" "${_mutated}")

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

# Drop ONE case×mode×direction child record. The case, mode and direction still
# exist elsewhere, so only the applicable-child coverage check can catch the
# shrink.
string(REPLACE
    "outcome SM-03 listen kc-server-commercial-client\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-case-direction-child' did not apply")
endif()
expect_rejected("drop-case-direction-child" "${_mutated}")

# Drop a case-mode applicability line: the case then has no declared modes, so
# the applicability derivation and the mode invariant must reject it.
string(REPLACE "case-mode SM-01 listen\n" "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-case-mode' did not apply")
endif()
expect_rejected("drop-case-mode" "${_mutated}")

# Flip a case's applicable mode (listen -> dedicated). SM-01's declared listen
# outcome children then reference an inapplicable mode, and the mode invariant
# must reject it.
string(REPLACE
    "case-mode SM-01 listen\n"
    "case-mode SM-01 dedicated\n"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'flip-case-mode' did not apply")
endif()
expect_rejected("flip-case-mode" "${_mutated}")

# Require an impossible child: SM-02 is dedicated-only, so declaring a listen
# child for it must be rejected rather than silently required.
string(REPLACE
    "outcome SM-02 dedicated kc-server-commercial-client\n"
    "outcome SM-02 dedicated kc-server-commercial-client\noutcome SM-02 listen kc-server-commercial-client\n"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'add-unapplicable-mode-child' did not apply")
endif()
expect_rejected("add-unapplicable-mode-child" "${_mutated}")

# Drop an applicable child (SM-01 listen, client direction). It is required by
# the applicable-child derivation, so dropping it must be rejected.
string(REPLACE
    "outcome SM-01 listen kc-client-commercial-server\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-applicable-child' did not apply")
endif()
expect_rejected("drop-applicable-child" "${_mutated}")

# Demote the dual-role macos-arm64 target to client-only: this is exactly the
# commercial-interoperability coverage reduction the guard must reject, because
# it drops the original-client → native-headless-server direction.
string(REPLACE
    "target-role macos-arm64 client server\n"
    "target-role macos-arm64 client\n"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'demote-macos-server-role' did not apply")
endif()
expect_rejected("demote-macos-server-role" "${_mutated}")

# Demote a dual-role target to client-only: the hardcoded target-role invariant
# must reject the lost server capability.
string(REPLACE
    "target-role win-amd64 client server\n"
    "target-role win-amd64 client\n"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'demote-dual-role-target' did not apply")
endif()
expect_rejected("demote-dual-role-target" "${_mutated}")

# Drop a target-role applicability line entirely.
string(REPLACE
    "target-role linux-amd64 client server\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-target-role' did not apply")
endif()
expect_rejected("drop-target-role" "${_mutated}")

# Narrow the dual-role macOS aggregate cell to the client direction only: the
# §6.3 direction-scope check must reject dropping the required server direction.
string(REPLACE
    "| `macos-arm64` | listen | both |"
    "| `macos-arm64` | listen | `kc-client-commercial-server` |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'narrow-macos-aggregate-scope' did not apply")
endif()
expect_rejected("narrow-macos-aggregate-scope" "${_mutated}")

# Promote the first commercial §6.3 aggregate cell from Blocked to Pass while
# keeping the evidence-ref at 'none'. The aggregate cell is derived, and the
# licensed reference manifests are unavailable, so the status/evidence cells
# must be parsed and held at 'Blocked / none'.
string(REPLACE
    "| `win-amd64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |"
    "| `win-amd64` | listen | both | Pass / none | Blocked / none | Supplemental / none |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-aggregate-cell' did not apply")
endif()
expect_rejected("promote-aggregate-cell" "${_mutated}")

# Attach an evidence-ref to a commercial §6.3 aggregate cell while claiming no
# pass. The evidence-ref is validated too, so a fabricated reference id cannot
# be recorded without a real run.
string(REPLACE
    "| `win-amd64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |"
    "| `win-amd64` | listen | both | Blocked / manifest-1 | Blocked / none | Supplemental / none |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-aggregate-evidence' did not apply")
endif()
expect_rejected("promote-aggregate-evidence" "${_mutated}")

# The kisakcod-self §6.3 column is a non-derived supplemental annotation, not
# a commercial result: promoting its status half must be rejected exactly like
# a commercial promotion (review finding r4038931549 — this column was
# previously counted by the row-shape check but never validated).
string(REPLACE
    "| `win-amd64` | dedicated | both | Blocked / none | Blocked / none | Supplemental / none |"
    "| `win-amd64` | dedicated | both | Blocked / none | Blocked / none | Pass / none |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-supplemental-status' did not apply")
endif()
expect_rejected("promote-supplemental-status" "${_mutated}")

# Same pin on the evidence half: a supplemental annotation carries no evidence
# claim, so attaching an invented reference id must be rejected independently.
string(REPLACE
    "| `win-arm64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |"
    "| `win-arm64` | listen | both | Blocked / none | Blocked / none | Supplemental / invented |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-supplemental-evidence' did not apply")
endif()
expect_rejected("promote-supplemental-evidence" "${_mutated}")

# The exact reproduced review bypass: flip one Supplemental / none cell to
# 'Pass / invented'. Both halves of the annotation are pinned, so the
# combined edit must fail as well.
string(REPLACE
    "| `linux-amd64` | listen | both | Blocked / none | Blocked / none | Supplemental / none |"
    "| `linux-amd64` | listen | both | Blocked / none | Blocked / none | Pass / invented |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-supplemental-pass-invented' did not apply")
endif()
expect_rejected("promote-supplemental-pass-invented" "${_mutated}")

# Promote the SM-01 kc-server-commercial-client §6.2 child cell to Pass while
# keeping evidence at 'none'. The §11 outcome triples carry no result fields,
# so without direct child-ledger cell validation a per-case commercial pass
# claim could slip through while every aggregate stays Blocked.
string(REPLACE
    "| `SM-01` | listen | Blocked / none | Blocked / none | content load, client join, gameplay |"
    "| `SM-01` | listen | Pass / none | Blocked / none | content load, client join, gameplay |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-child-status-server' did not apply")
endif()
expect_rejected("promote-child-status-server" "${_mutated}")

# Same child status promotion in the kc-client-commercial-server direction
# (SM-03): both commercial directions of the ledger are validated.
string(REPLACE
    "| `SM-03` | listen, dedicated | Blocked / none | Blocked / none | map change, unload, reconnect |"
    "| `SM-03` | listen, dedicated | Blocked / none | Pass / none | map change, unload, reconnect |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-child-status-client' did not apply")
endif()
expect_rejected("promote-child-status-client" "${_mutated}")

# Attach fabricated evidence to the SM-02 kc-server-commercial-client child
# cell while keeping the Blocked status: an evidence-ref no run produced must
# be rejected exactly like a status promotion.
string(REPLACE
    "| `SM-02` | dedicated | Blocked / none | Blocked / none | content load, client join, gameplay |"
    "| `SM-02` | dedicated | Blocked / fabricated-log | Blocked / none | content load, client join, gameplay |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-child-evidence-server' did not apply")
endif()
expect_rejected("promote-child-evidence-server" "${_mutated}")

# Fabricated child evidence in the kc-client-commercial-server direction
# (PC-01): the evidence-ref half of the cell is validated in both directions.
string(REPLACE
    "| `PC-01` | listen, dedicated | Blocked / none | Blocked / none | client join |"
    "| `PC-01` | listen, dedicated | Blocked / none | Blocked / fabricated-log | client join |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-child-evidence-client' did not apply")
endif()
expect_rejected("promote-child-evidence-client" "${_mutated}")

# The exact reproduced review bypass: promote the SM-01
# kc-server-commercial-client child cell to `Pass / fabricated-log` with every
# aggregate cell unchanged. Both the status and evidence halves of the child
# cell must reject this.
string(REPLACE
    "| `SM-01` | listen | Blocked / none | Blocked / none | content load, client join, gameplay |"
    "| `SM-01` | listen | Pass / fabricated-log | Blocked / none | content load, client join, gameplay |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-child-fabricated-pass' did not apply")
endif()
expect_rejected("promote-child-fabricated-pass" "${_mutated}")

# Drop the SM-01 §6.2 ledger row entirely: the child cells must not be able to
# escape the blocked-status policy by removing their row, so the ledger must
# cover exactly the index case set.
string(REPLACE
    "| `SM-01` | listen | Blocked / none | Blocked / none | content load, client join, gameplay | Reference id + sanitized load/join log |\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-child-ledger-row' did not apply")
endif()
expect_rejected("drop-child-ledger-row" "${_mutated}")

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

# Drop ONE full-key §6.1 child record. The derived applicable set still
# requires it, so exact coverage must reject the loss — this is the review's
# core finding: a per-case view without full-key records can silently claim
# coverage for every other target/mode/profile combination.
string(REPLACE
    "child win-amd64 listen original-commercial-1.7 SM-01 kc-server-commercial-client Blocked none\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-full-key-child' did not apply")
endif()
expect_rejected("drop-full-key-child" "${_mutated}")

# Duplicate a full-key child record verbatim: the key must stay unique, so a
# repeated record cannot carry a second conflicting claim later.
string(REPLACE
    "child win-amd64 listen original-commercial-1.7 SM-01 kc-server-commercial-client Blocked none\n"
    "child win-amd64 listen original-commercial-1.7 SM-01 kc-server-commercial-client Blocked none\nchild win-amd64 listen original-commercial-1.7 SM-01 kc-server-commercial-client Blocked none\n"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'duplicate-full-key-child' did not apply")
endif()
expect_rejected("duplicate-full-key-child" "${_mutated}")

# Move a record to a wrong axis: SM-01 is listen-only, so its record on the
# dedicated mode is an inapplicable-axis record and the listen variant is
# missing; both the applicability check and exact coverage must reject.
string(REPLACE
    "child win-amd64 listen original-commercial-1.7 SM-01 kc-server-commercial-client Blocked none"
    "child win-amd64 dedicated original-commercial-1.7 SM-01 kc-server-commercial-client Blocked none"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'wrong-axis-full-key-child' did not apply")
endif()
expect_rejected("wrong-axis-full-key-child" "${_mutated}")

# Swap a record's commercial profile for the supplemental kisakcod-self: a
# supplemental profile never fills a commercial child record, and the original
# commercial record is missing.
string(REPLACE
    "child win-amd64 listen steam-commercial-1.8 SM-01 kc-server-commercial-client Blocked none"
    "child win-amd64 listen kisakcod-self SM-01 kc-server-commercial-client Blocked none"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'wrong-profile-full-key-child' did not apply")
endif()
expect_rejected("wrong-profile-full-key-child" "${_mutated}")

# Promote one full-key child record's status while the licensed references are
# unavailable: the per-record blocked policy must reject it exactly like a
# §6.2 cell promotion.
string(REPLACE
    "child win-arm64 listen original-commercial-1.7 MOD-02 kc-client-commercial-server Blocked none"
    "child win-arm64 listen original-commercial-1.7 MOD-02 kc-client-commercial-server Pass none"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-full-key-child' did not apply")
endif()
expect_rejected("promote-full-key-child" "${_mutated}")

# Fabricate evidence on a full-key child record: the evidence half is
# validated per record too, not only in the §6.2 per-case view.
string(REPLACE
    "child linux-amd64 dedicated steam-commercial-1.8 PC-03 kc-server-commercial-client Blocked none"
    "child linux-amd64 dedicated steam-commercial-1.8 PC-03 kc-server-commercial-client Blocked fabricated-log"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'fabricate-full-key-child-evidence' did not apply")
endif()
expect_rejected("fabricate-full-key-child-evidence" "${_mutated}")

# Drop the child-count pin: the exact generated record count must stay
# declared so coverage drift shows up as a reviewable one-line diff.
string(REPLACE
    "child-count 672\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-child-count' did not apply")
endif()
expect_rejected("drop-child-count" "${_mutated}")

# Drop the child-ledger completeness policy: the full-key records could then
# drift away from the blocked state without the policy being missed.
string(REPLACE
    "completeness child-ledger full-key-blocked-none\n"
    "" _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'drop-child-ledger-policy' did not apply")
endif()
expect_rejected("drop-child-ledger-policy" "${_mutated}")

# r4039453978 exact reproduced bypass: narrow the MOD-01 §6.2 Modes cell from
# 'listen, dedicated' to 'listen'. The parser captured the cell but never
# compared it with the canonical case-mode declaration, so the per-case view
# silently displayed weaker applicability than the index declares. The
# displayed Modes set must equal the declared set exactly.
string(REPLACE
    "| `MOD-01` | listen, dedicated |"
    "| `MOD-01` | listen |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'narrow-ledger-modes' did not apply")
endif()
expect_rejected("narrow-ledger-modes" "${_mutated}")

# The symmetric widening: an extra mode the case-mode line does not declare
# invents applicability and must be rejected by the same set equality.
string(REPLACE
    "| `SM-01` | listen |"
    "| `SM-01` | listen, dedicated |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'widen-ledger-modes' did not apply")
endif()
expect_rejected("widen-ledger-modes" "${_mutated}")

# An unknown mode token is not an applicability declaration the index knows,
# so it fails closed instead of being tolerated as prose.
string(REPLACE
    "| `PC-01` | listen, dedicated |"
    "| `PC-01` | listen, dedicated, lan |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'unknown-ledger-mode' did not apply")
endif()
expect_rejected("unknown-ledger-mode" "${_mutated}")

# A duplicated entry would let one cell claim a mode twice.
string(REPLACE
    "| `PC-02` | listen, dedicated |"
    "| `PC-02` | listen, listen |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'duplicate-ledger-mode' did not apply")
endif()
expect_rejected("duplicate-ledger-mode" "${_mutated}")

# r4039453986 exact reproduced bypass: promote the §4.5 UP89-01 visible
# disposition label from '**Blocked**' to '**Pass**' while the hidden §11
# index disposition stays blocked. The rendered document would then claim an
# unavailable reproduction, so the visible label is held at '**Blocked**'.
string(REPLACE
    "| **Blocked** — named mod content and licensed retail fixtures are not available in this checkout; no reproduction is claimed. |"
    "| **Pass** — named mod content and licensed retail fixtures are not available in this checkout; no reproduction is claimed. |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-up89-01-visible-disposition' did not apply")
endif()
expect_rejected("promote-up89-01-visible-disposition" "${_mutated}")

# The second upstream-89 case row: every upstream row is checked
# individually, not just one per family.
string(REPLACE
    "| **Blocked** — same unavailable fixtures; depends on `UP89-01`. |"
    "| **Pass** — same unavailable fixtures; depends on `UP89-01`. |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-up89-02-visible-disposition' did not apply")
endif()
expect_rejected("promote-up89-02-visible-disposition" "${_mutated}")

# The upstream-40 family: its visible '**Blocked**' label cannot be promoted
# any more than its index disposition can.
string(REPLACE
    "| **Blocked** — needs a pinned commercial reference; ties to A05/#127 and `ki-jgz`/`#116` scalar-determinism evidence, which is supplemental. |"
    "| **Pass** — needs a pinned commercial reference; ties to A05/#127 and `ki-jgz`/`#116` scalar-determinism evidence, which is supplemental. |"
    _mutated "${_matrix_text}")
if(_mutated STREQUAL _matrix_text)
    message(FATAL_ERROR "Negative self-test mutation 'promote-up40-01-visible-disposition' did not apply")
endif()
expect_rejected("promote-up40-01-visible-disposition" "${_mutated}")

file(REMOVE_RECURSE "${_scratch_dir}")

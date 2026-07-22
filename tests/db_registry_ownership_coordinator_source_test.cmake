cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_registry_ownership_coordinator.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_registry_ownership_coordinator.cpp")
set(_runtime_facade_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_facade.h")
set(_runtime_facade_source_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_facade.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_registry_ownership_coordinator_tests.cpp")
set(_integration_fixture_path
    "${SOURCE_ROOT}/tests/script_string_ownership_tests.cpp")
set(_stable_integration_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_stable_context_integration_tests.cpp")
set(_production_seal_path
    "${SOURCE_ROOT}/tests/db_registry_ownership_coordinator_production_seal_tests.cpp")
set(_security_path "${SOURCE_ROOT}/tests/security_regression_test.cmake")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_legacy_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_script_transaction_header_path
    "${SOURCE_ROOT}/src/script/scr_string_transaction.h")
set(_zone_script_ownership_header_path
    "${SOURCE_ROOT}/src/database/db_zone_script_string_ownership.h")
set(_script_string_source_path
    "${SOURCE_ROOT}/src/script/scr_stringlist.cpp")
set(_integer_suffix_token_paste_path
    "${SOURCE_ROOT}/src/universal/q_shared.h")
set(_server_token_literal_path
    "${SOURCE_ROOT}/src/server_mp/sv_client_mp.cpp")
set(_ui_component_token_literal_path
    "${SOURCE_ROOT}/src/ui/ui_component.cpp")
set(_ui_parser_token_literal_path
    "${SOURCE_ROOT}/src/ui/ui_shared_obj.cpp")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_runtime_facade_header_path}"
    "${_runtime_facade_source_path}"
    "${_fixture_path}"
    "${_integration_fixture_path}"
    "${_stable_integration_fixture_path}"
    "${_production_seal_path}"
    "${_security_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}"
    "${_legacy_registry_path}"
    "${_script_transaction_header_path}"
    "${_zone_script_ownership_header_path}"
    "${_script_string_source_path}"
    "${_integer_suffix_token_paste_path}"
    "${_server_token_literal_path}"
    "${_ui_component_token_literal_path}"
    "${_ui_parser_token_literal_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing registry ownership coordinator input: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_integration_fixture_path}" _integration_fixture)
file(READ "${_stable_integration_fixture_path}" _stable_integration_fixture)
file(READ "${_production_seal_path}" _production_seal)
file(READ "${_security_path}" _security)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)
file(READ "${_legacy_registry_path}" _legacy_registry)
file(READ "${_script_transaction_header_path}" _script_transaction_header)
file(READ "${_zone_script_ownership_header_path}"
    _zone_script_ownership_header)
file(READ "${_script_string_source_path}" _script_string_source)
file(READ "${_integer_suffix_token_paste_path}" _integer_suffix_source)
file(READ "${_server_token_literal_path}" _server_token_literal_source)
file(READ "${_ui_component_token_literal_path}" _ui_component_token_source)
file(READ "${_ui_parser_token_literal_path}" _ui_parser_token_source)

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing registry coordinator invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden registry coordinator regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_matches SOURCE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        message(FATAL_ERROR
            "Missing registry coordinator invariant (${DESCRIPTION}): "
            "'${PATTERN}'")
    endif()
endfunction()

function(require_not_matches SOURCE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SOURCE_VAR}}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden registry coordinator regression (${DESCRIPTION}): "
            "'${_match}'")
    endif()
endfunction()

function(require_literal_count SOURCE_VAR NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VAR}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    while(TRUE)
        string(FIND "${_remaining}" "${NEEDLE}" _position)
        if(_position EQUAL -1)
            break()
        endif()
        math(EXPR _count "${_count} + 1")
        math(EXPR _next "${_position} + ${_needle_length}")
        string(SUBSTRING "${_remaining}" ${_next} -1 _remaining)
    endwhile()
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected registry coordinator invariant count "
            "(${DESCRIPTION}): expected ${EXPECTED}, found ${_count} for "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_regex_count SOURCE_VAR PATTERN EXPECTED DESCRIPTION)
    string(REGEX MATCHALL "${PATTERN}" _matches "${${SOURCE_VAR}}")
    list(LENGTH _matches _count)
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Unexpected registry coordinator regex count (${DESCRIPTION}): "
            "expected ${EXPECTED}, found ${_count} for '${PATTERN}'")
    endif()
endfunction()

function(require_normalized_equals SOURCE_VAR EXPECTED_VAR DESCRIPTION)
    string(REGEX REPLACE "[ \t\r\n]+" " " _actual "${${SOURCE_VAR}}")
    string(STRIP "${_actual}" _actual)
    string(REGEX REPLACE "[ \t\r\n]+" " " _expected "${${EXPECTED_VAR}}")
    string(STRIP "${_expected}" _expected)
    if(NOT _actual STREQUAL _expected)
        message(FATAL_ERROR
            "Unexpected closed registry coordinator surface (${DESCRIPTION}). "
            "Expected '${_expected}', found '${_actual}'")
    endif()
endfunction()

function(extract_slice SOURCE_VAR START END OUT_VAR DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${START}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of ${DESCRIPTION}: '${START}'")
    endif()
    string(SUBSTRING "${${SOURCE_VAR}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR "Missing ordered end of ${DESCRIPTION}: '${END}'")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUT_VAR} "${_slice}" PARENT_SCOPE)
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered registry coordinator invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

# The raw paste policy has only four path-specific exemptions. Freeze every
# reviewed spelling/count before removal so duplicating an allowed literal or
# suffix macro cannot silently create a new token-construction channel.
require_literal_count(
    _integer_suffix_source "num ## LL" 1
    "reviewed q_shared long-long suffix paste")
require_literal_count(
    _integer_suffix_source "num ## i64" 2
    "reviewed q_shared Microsoft integer suffix pastes")
require_literal_count(
    _server_token_literal_source
    "\"###!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!###\\n\""
    1
    "reviewed server diagnostic hash literal")
require_literal_count(
    _server_token_literal_source
    "\"########################################\\n\""
    1
    "reviewed server separator hash literal")
require_literal_count(
    _ui_component_token_source "\"##\"" 1
    "reviewed UI component token literal")
require_literal_count(
    _ui_parser_token_source "\"##\"" 3
    "reviewed UI parser token literals")
require_literal_count(
    _ui_parser_token_source "\"define with misplaced ##\"" 1
    "reviewed UI parser diagnostic literal")

# Translation phase 2 removes escaped physical newlines before header names,
# namespace qualifiers, and identifiers are recognized. Comments are phase-3
# whitespace, including around qualification. Keep the enrollment detector on
# those semantics without attempting lossy comment or string-literal removal.
string(ASCII 92 _registry_coordinator_backslash)
string(ASCII 13 _registry_coordinator_carriage_return)
string(ASCII 10 _registry_coordinator_line_feed)
string(ASCII 12 _registry_coordinator_form_feed)
string(ASCII 11 _registry_coordinator_vertical_tab)
set(_registry_coordinator_block_comment "/\\*([^*]|\\*+[^*/])*\\*+/")
set(_registry_coordinator_comment_atom
    "([ \t\r\n${_registry_coordinator_form_feed}${_registry_coordinator_vertical_tab}]|${_registry_coordinator_block_comment}|//[^\r\n]*)")
set(_registry_coordinator_comment_gap
    "${_registry_coordinator_comment_atom}*")
set(_registry_coordinator_comment_separator
    "${_registry_coordinator_comment_atom}+")

function(normalize_registry_coordinator_phase2 SOURCE_VAR OUT_VAR)
    set(_spliced "${${SOURCE_VAR}}")
    string(REPLACE
        "${_registry_coordinator_backslash}${_registry_coordinator_carriage_return}${_registry_coordinator_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_registry_coordinator_backslash}${_registry_coordinator_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_registry_coordinator_backslash}${_registry_coordinator_carriage_return}"
        "" _spliced "${_spliced}")
    set(${OUT_VAR} "${_spliced}" PARENT_SCOPE)
endfunction()

function(source_has_registry_coordinator_identifier
    SOURCE_VAR IDENTIFIER OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])${IDENTIFIER}([^A-Za-z0-9_]|$)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

# Every distinctive public, private, and testing identifier is protected. A
# production caller cannot include the header, redeclare the namespace/API, or
# reproduce a private helper without tripping the same exact-token boundary.
set(_registry_coordinator_protected_tokens
    db_registry_ownership_coordinator
    registry_ownership
    RegistryOwnershipCoordinatorPhase
    RegistryOwnershipCoordinatorMode
    RegistryOwnershipStatus
    RegistryOwnershipName
    RegistryOwnershipBulkResult
    RegistryOwnershipCoordinatorAdmission
    RegistryOwnershipCoordinatorFacade
    RegistryOwnershipCoordinator
    RegistryOwnershipCoordinatorTestAccess
    TryBeginStandaloneRegistryOwnershipCoordinator
    TryBorrowRegistryOwnershipCoordinator
    TryBorrowActiveRuntimeCallback
    FinishRegistryOwnershipCoordinator
    TryRegistryAddDatabaseUser4
    TryRegistryAddDatabaseUsers4
    TryRegistryInternBoundedName
    TryRegistryReAddRetainedDefaultName
    TryRegistryReAddRetainedDefaultNames
    TryRegistryTransferDatabaseUsers4To8
    TryRegistryShutdownDatabaseUser8
    SetRegistryOwnershipCoordinatorBoundaryForTesting
    SetNextRegistryOwnershipCoordinatorSerialForTesting
    SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting
    KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)

function(find_registry_coordinator_protected_token SOURCE_VAR OUT_VAR)
    foreach(_token IN LISTS _registry_coordinator_protected_tokens)
        source_has_registry_coordinator_identifier(
            ${SOURCE_VAR} "${_token}" _token_found)
        if(_token_found)
            set(${OUT_VAR} "${_token}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${OUT_VAR} "" PARENT_SCOPE)
endfunction()

function(source_has_qualified_registry_coordinator SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])((db${_registry_coordinator_comment_gap}::${_registry_coordinator_comment_gap})?registry_ownership)${_registry_coordinator_comment_gap}::"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_registry_coordinator_namespace_declaration
    SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])namespace${_registry_coordinator_comment_separator}((db${_registry_coordinator_comment_gap}::${_registry_coordinator_comment_gap})?registry_ownership)(${_registry_coordinator_comment_gap}::|${_registry_coordinator_comment_gap}\\{)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_registry_coordinator_token_paste SOURCE_VAR OUT_VAR)
    # Token paste can assemble every protected identifier. After the one exact
    # reviewed legacy suffix macro is removed, reject both hash/digraph forms
    # and the trigraph spellings that can create them before phase 2.
    foreach(_operator IN ITEMS "##" "%:%:" "??/" "??=")
        string(FIND "${${SOURCE_VAR}}" "${_operator}" _position)
        if(NOT _position EQUAL -1)
            set(${OUT_VAR} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
endfunction()

function(remove_reviewed_registry_coordinator_token_text
    PATH SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    if(PATH STREQUAL "${_integer_suffix_token_paste_path}")
        # q_shared's three fixed integer-suffix definitions cannot form any
        # protected identifier. An altered/additional paste remains visible.
        string(REPLACE "num ## LL" "" _candidate "${_candidate}")
        string(REPLACE "num ## i64" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL "${_server_token_literal_path}")
        string(REPLACE
            "\"###!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!###\\n\""
            "" _candidate "${_candidate}")
        string(REPLACE
            "\"########################################\\n\""
            "" _candidate "${_candidate}")
    elseif(PATH STREQUAL "${_ui_component_token_literal_path}")
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL "${_ui_parser_token_literal_path}")
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
        string(REPLACE
            "\"define with misplaced ##\"" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL "${_script_transaction_header_path}")
        # The capability header has exactly one forward declaration, one
        # private friend, and one explanatory comment naming the coordinator.
        # Remove only those reviewed spellings; any new coordinator reference
        # in this large header survives and is rejected by the main detector.
        string(REGEX REPLACE
            "namespace[ \t\r\n]+db[ \t\r\n]*::[ \t\r\n]*registry_ownership[ \t\r\n]*\\{[ \t\r\n]*class[ \t\r\n]+RegistryOwnershipCoordinator[ \t\r\n]*;[ \t\r\n]*\\}"
            "" _candidate "${_candidate}")
        string(REGEX REPLACE
            "friend[ \t\r\n]+class[ \t\r\n]+db[ \t\r\n]*::[ \t\r\n]*registry_ownership[ \t\r\n]*::[ \t\r\n]*RegistryOwnershipCoordinator[ \t\r\n]*;"
            "" _candidate "${_candidate}")
        string(REPLACE
            "// RegistryOwnershipCoordinator. It carries numeric, mirrored identities so"
            "" _candidate "${_candidate}")
    elseif(PATH STREQUAL "${_zone_script_ownership_header_path}")
        # The ownership controller grants only its callback-specific snapshot
        # and reauthentication methods to the real coordinator. Remove the
        # exact reviewed forward/friend bridge; any additional enrollment in
        # this large header remains visible to the production-source scan.
        string(REGEX REPLACE
            "namespace[ \t\r\n]+db[ \t\r\n]*::[ \t\r\n]*registry_ownership[ \t\r\n]*\\{[ \t\r\n]*class[ \t\r\n]+RegistryOwnershipCoordinator[ \t\r\n]*;[ \t\r\n]*\\}"
            "" _candidate "${_candidate}")
        string(REGEX REPLACE
            "friend[ \t\r\n]+class[ \t\r\n]+db[ \t\r\n]*::[ \t\r\n]*registry_ownership[ \t\r\n]*::[ \t\r\n]*RegistryOwnershipCoordinator[ \t\r\n]*;"
            "" _candidate "${_candidate}")
    endif()
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(detect_registry_coordinator_enrollment SOURCE_VAR OUT_VAR)
    normalize_registry_coordinator_phase2(${SOURCE_VAR} _candidate)
    set(_found FALSE)
    string(FIND
        "${_candidate}" "db_registry_ownership_coordinator.h" _header_ref)
    if(NOT _header_ref EQUAL -1)
        set(_found TRUE)
    endif()
    find_registry_coordinator_protected_token(
        _candidate _protected_token)
    if(NOT _protected_token STREQUAL "")
        set(_found TRUE)
    endif()
    source_has_qualified_registry_coordinator(
        _candidate _qualified_found)
    source_has_registry_coordinator_namespace_declaration(
        _candidate _namespace_found)
    source_has_registry_coordinator_token_paste(
        _candidate _token_paste_found)
    if(_qualified_found OR _namespace_found OR _token_paste_found)
        set(_found TRUE)
    endif()
    set(${OUT_VAR} ${_found} PARENT_SCOPE)
endfunction()

# Production-neutrality is token-aware rather than spelling-aware: comments,
# escaped physical newlines, and transitively included standard headers cannot
# turn reports, allocation, or exceptions back on in the coordinator layer.
function(find_registry_coordinator_forbidden_production_token
    SOURCE_VAR OUT_VAR)
    normalize_registry_coordinator_phase2(${SOURCE_VAR} _candidate)
    foreach(_call IN ITEMS
        Com_Error
        Sys_Error
        MyAssertHandler
        malloc
        calloc
        realloc)
        string(REGEX MATCH
            "(^|[^A-Za-z0-9_])${_call}${_registry_coordinator_comment_gap}\\("
            _match "${_candidate}")
        if(NOT _match STREQUAL "")
            set(${OUT_VAR} "${_call}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])Com_Print[A-Za-z0-9_]*${_registry_coordinator_comment_gap}\\("
        _match "${_candidate}")
    if(NOT _match STREQUAL "")
        set(${OUT_VAR} "Com_Print*" PARENT_SCOPE)
        return()
    endif()
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])operator${_registry_coordinator_comment_separator}new([^A-Za-z0-9_]|$)"
        _match "${_candidate}")
    if(NOT _match STREQUAL "")
        set(${OUT_VAR} "operator new" PARENT_SCOPE)
        return()
    endif()
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])new(${_registry_coordinator_comment_separator}[A-Za-z_(]|${_registry_coordinator_comment_gap}\\()"
        _match "${_candidate}")
    if(NOT _match STREQUAL "")
        set(${OUT_VAR} "new expression" PARENT_SCOPE)
        return()
    endif()
    foreach(_keyword IN ITEMS catch throw)
        if(_keyword STREQUAL "catch")
            set(_tail "${_registry_coordinator_comment_gap}\\(")
        else()
            set(_tail "([^A-Za-z0-9_]|$)")
        endif()
        string(REGEX MATCH
            "(^|[^A-Za-z0-9_])${_keyword}${_tail}"
            _match "${_candidate}")
        if(NOT _match STREQUAL "")
            set(${OUT_VAR} "${_keyword}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])std${_registry_coordinator_comment_gap}::${_registry_coordinator_comment_gap}(vector|string|function)([^A-Za-z0-9_]|$)"
        _match "${_candidate}")
    if(NOT _match STREQUAL "")
        set(${OUT_VAR} "allocating standard-library surface" PARENT_SCOPE)
        return()
    endif()
    set(${OUT_VAR} "" PARENT_SCOPE)
endfunction()

function(require_registry_coordinator_forbidden_production_fixture
    SOURCE_VAR DESCRIPTION)
    find_registry_coordinator_forbidden_production_token(
        ${SOURCE_VAR} _forbidden)
    if(_forbidden STREQUAL "")
        message(FATAL_ERROR
            "Registry coordinator production-neutrality detector missed "
            "${DESCRIPTION}")
    endif()
endfunction()

function(require_registry_coordinator_detector_fixture
    SOURCE_VAR DESCRIPTION)
    detect_registry_coordinator_enrollment(${SOURCE_VAR} _detected)
    if(NOT _detected)
        message(FATAL_ERROR
            "Registry coordinator detector missed ${DESCRIPTION}")
    endif()
endfunction()

# Adversarial fixtures pin escaped-newline normalization, macro replacement,
# phase-3 comments, every preprocessing paste spelling, and all C++ whitespace
# characters. Several compile-valid equivalents also live in the macro-off C++
# production seal, preventing the scanner fixtures from becoming aspirational.
string(CONCAT _phase2_header_bypass
    "#include <database/db_registry_ownership_coordi"
    "${_registry_coordinator_backslash}${_registry_coordinator_line_feed}nator.h>")
require_registry_coordinator_detector_fixture(
    _phase2_header_bypass "a phase-2-spliced public header")

string(CONCAT _phase2_api_bypass
    "auto finish = &FinishRegistryOwnershipCoordi"
    "${_registry_coordinator_backslash}${_registry_coordinator_carriage_return}${_registry_coordinator_line_feed}nator;")
require_registry_coordinator_detector_fixture(
    _phase2_api_bypass "a CRLF-spliced API pointer")

string(CONCAT _macro_header_bypass
    "#define KISAK_REGISTRY_HEADER(name) <database/name.h>\n"
    "#include KISAK_REGISTRY_HEADER(db_registry_ownership_coordinator)")
require_registry_coordinator_detector_fixture(
    _macro_header_bypass "a macro-generated public include")

string(CONCAT _qualified_using_bypass
    "using db/**/::/**/registry_ownership/**/::/**/"
    "FinishRegistryOwnershipCoordinator;\n"
    "auto finish = &FinishRegistryOwnershipCoordinator;")
require_registry_coordinator_detector_fixture(
    _qualified_using_bypass "a comment-separated using declaration")

string(CONCAT _line_comment_qualified_bypass
    "auto begin = &db// phase-3 separator\n"
    "::/**/registry_ownership/**/::/**/"
    "TryBeginStandaloneRegistryOwnershipCoordinator;")
require_registry_coordinator_detector_fixture(
    _line_comment_qualified_bypass "a line-comment-separated qualifier")

set(_compact_namespace_bypass
    "namespace/**/db/**/::/**/registry_ownership { class Forged; }")
set(_nested_namespace_bypass
    "namespace/**/db { namespace/**/registry_ownership { class Forged; } }")
set(_macro_namespace_bypass
    "#define KISAK_REGISTRY_NAMESPACE registry_ownership")
string(CONCAT _forged_coordinator_friend_bypass
    "namespace db/**/::/**/registry_ownership { class "
    "RegistryOwnershipCoordi"
    "${_registry_coordinator_backslash}${_registry_coordinator_line_feed}nator"
    " { public: static void Mint(); }; }")
require_registry_coordinator_detector_fixture(
    _compact_namespace_bypass "a compact namespace declaration")
require_registry_coordinator_detector_fixture(
    _nested_namespace_bypass "a nested namespace declaration")
require_registry_coordinator_detector_fixture(
    _macro_namespace_bypass "a macro-substituted namespace")
require_registry_coordinator_detector_fixture(
    _forged_coordinator_friend_bypass
    "a forged definition of the real coordinator forward friend")

string(CONCAT _form_feed_namespace_bypass
    "namespace" "${_registry_coordinator_form_feed}"
    "registry_ownership { class Forged; }")
string(CONCAT _vertical_tab_namespace_bypass
    "namespace" "${_registry_coordinator_vertical_tab}"
    "registry_ownership { class Forged; }")
require_registry_coordinator_detector_fixture(
    _form_feed_namespace_bypass "a form-feed-separated namespace")
require_registry_coordinator_detector_fixture(
    _vertical_tab_namespace_bypass "a vertical-tab-separated namespace")

string(CONCAT _hash_token_paste_bypass
    "#define KISAK_REGISTRY_CAT_I(left, right) left/**/##/**/right\n"
    "#define KISAK_REGISTRY_CAT(left, right) "
    "KISAK_REGISTRY_CAT_I(left, right)\n"
    "auto finish = &KISAK_REGISTRY_CAT("
    "FinishRegistryOwnership, Coordinator);")
require_registry_coordinator_detector_fixture(
    _hash_token_paste_bypass "a hash token-paste API construction")

string(CONCAT _digraph_token_paste_bypass
    "%: define KISAK_REGISTRY_DIGRAPH(left, right) left %:%: right\n"
    "auto finish = &KISAK_REGISTRY_DIGRAPH("
    "FinishRegistryOwnership, Coordinator);")
require_registry_coordinator_detector_fixture(
    _digraph_token_paste_bypass "a digraph token-paste API construction")

set(_trigraph_token_paste_bypass
    "??=define KISAK_REGISTRY_TRIGRAPH(left, right) left ??=??= right")
require_registry_coordinator_detector_fixture(
    _trigraph_token_paste_bypass "a trigraph token-paste construction")

string(CONCAT _trigraph_splice_token_paste_bypass
    "#define KISAK_REGISTRY_TRIGRAPH_SPLICE(left, right) left #??/"
    "${_registry_coordinator_line_feed}# right")
require_registry_coordinator_detector_fixture(
    _trigraph_splice_token_paste_bypass
    "a trigraph and phase-2 splice paste construction")

string(CONCAT _comment_quote_token_paste_bypass
    "/* \"\n*/ %: define KISAK_REGISTRY_COMMENT_CAT(left, right) "
    "left %:%: right /* \" */")
require_registry_coordinator_detector_fixture(
    _comment_quote_token_paste_bypass
    "a multiline-comment/unmatched-quote paste construction")

string(CONCAT _phase2_report_bypass
    "Com_Er"
    "${_registry_coordinator_backslash}${_registry_coordinator_line_feed}ror/**/(nullptr);")
set(_comment_new_bypass "auto* value = new/**/ RegistryOwnershipName;")
set(_comment_operator_new_bypass "void* operator/**/new(unsigned long);")
set(_comment_malloc_bypass "auto* value = malloc /**/ (4);")
set(_comment_catch_bypass "try {} catch/**/(...) {}")
set(_throw_rethrow_bypass "try {} catch (...) { throw; }")
set(_transitive_vector_bypass "std/**/::/**/vector<int> values;")
foreach(_fixture IN ITEMS
    _phase2_report_bypass
    _comment_new_bypass
    _comment_operator_new_bypass
    _comment_malloc_bypass
    _comment_catch_bypass
    _throw_rethrow_bypass
    _transitive_vector_bypass)
    require_registry_coordinator_forbidden_production_fixture(
        ${_fixture} "comment/phase-2 production-neutrality bypass")
endforeach()

set(_server_payload_macro_bypass
    "#define KISAK_REGISTRY_HASH_RUN ########################################")
remove_reviewed_registry_coordinator_token_text(
    "${_server_token_literal_path}"
    _server_payload_macro_bypass
    _server_payload_macro_reviewed)
require_registry_coordinator_detector_fixture(
    _server_payload_macro_reviewed
    "a server diagnostic-payload macro construction")

set(_detector_negative
    "struct RegistryOwnershipCoordinatorLike { int unrelated; };")
detect_registry_coordinator_enrollment(_detector_negative _false_positive)
if(_false_positive)
    message(FATAL_ERROR
        "Registry coordinator detector lost exact identifier boundaries")
endif()

# All files—not merely known C++ extensions—must remain covered. The only
# approved enrollment sites are the coordinator bridge and the one private
# runtime facade that may invoke it. This still proves zero legacy loader
# callers before the atomic call-site cutover.
file(GLOB_RECURSE _production_sources
    LIST_DIRECTORIES FALSE "${SOURCE_ROOT}/src/*")
foreach(_non_extension_sentinel IN ITEMS
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.am"
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.in")
    list(FIND _production_sources
        "${_non_extension_sentinel}" _sentinel_index)
    if(_sentinel_index EQUAL -1)
        message(FATAL_ERROR
            "Registry coordinator production seal lost extension-independent "
            "traversal: ${_non_extension_sentinel}")
    endif()
endforeach()

set(_registry_coordinator_approved_sources
    "${_header_path}"
    "${_source_path}"
    "${_runtime_facade_header_path}"
    "${_runtime_facade_source_path}")
set(_registry_admission_approved_sources
    "${_header_path}"
    "${_source_path}"
    "${_script_transaction_header_path}"
    "${_script_string_source_path}")
foreach(_approved_path IN LISTS _registry_coordinator_approved_sources)
    list(FIND _production_sources "${_approved_path}" _approved_index)
    if(_approved_index EQUAL -1)
        message(FATAL_ERROR
            "Registry coordinator allowlist input escaped traversal: "
            "${_approved_path}")
    endif()
endforeach()

foreach(_production_path IN LISTS _production_sources)
    file(READ "${_production_path}" _production_raw)
    normalize_registry_coordinator_phase2(
        _production_raw _production_phase2)

    source_has_registry_coordinator_identifier(
        _production_phase2 "RegistryOwnershipAdmission"
        _registry_admission_found)
    list(FIND _registry_admission_approved_sources
        "${_production_path}" _registry_admission_approved_index)
    if(_registry_admission_found
        AND _registry_admission_approved_index EQUAL -1)
        message(FATAL_ERROR
            "Registry admission capability escaped its exact reviewed bridge "
            "in ${_production_path}")
    endif()

    list(FIND _registry_coordinator_approved_sources
        "${_production_path}" _approved_index)
    if(NOT _approved_index EQUAL -1)
        continue()
    endif()

    remove_reviewed_registry_coordinator_token_text(
        "${_production_path}" _production_phase2 _production_text)
    detect_registry_coordinator_enrollment(_production_text _enrolled)
    if(_enrolled)
        message(FATAL_ERROR
            "Premature registry ownership coordinator enrollment in "
            "${_production_path}")
    endif()
endforeach()

# The coordinator is a report-free, fixed-storage authority foundation. It may
# know its explicit outer transaction/controller and lock dependencies, but it
# must not reach production registry/load code or gain an allocation/callback
# escape hatch before the atomic caller cutover.
foreach(_var IN ITEMS _header _source)
    find_registry_coordinator_forbidden_production_token(
        ${_var} _forbidden_production_token)
    if(NOT _forbidden_production_token STREQUAL "")
        message(FATAL_ERROR
            "Forbidden token-aware registry coordinator production surface: "
            "${_forbidden_production_token}")
    endif()
    foreach(_forbidden IN ITEMS
        "database/database.h"
        "database/db_registry.h"
        "database/db_load.h"
        "qcommon/qcommon.h"
        "Com_Error("
        "Com_Print"
        "Sys_Error("
        "MyAssertHandler("
        "#include <vector>"
        "#include <string>"
        "#include <functional>"
        "std::function"
        "operator new"
        "malloc("
        "calloc("
        "realloc("
        "catch (")
        require_not_contains(
            ${_var} "${_forbidden}"
            "production-neutral report/allocation boundary")
    endforeach()
    require_not_matches(
        ${_var}
        "(^|[^A-Za-z0-9_])new[ \t\r\n]+[A-Za-z_(]"
        "dynamic allocation expression")
    require_not_matches(
        ${_var}
        "(^|[^A-Za-z0-9_])throw[ \t\r\n]+"
        "exception throw expression")
endforeach()

require_literal_count(
    _source "Sys_TryLockWrite(&db_hashCritSect)" 1
    "one nonblocking hash-write admission")
require_literal_count(
    _source "Sys_UnlockWrite(&db_hashCritSect)" 1
    "one authenticated reverse-order hash release")
require_not_matches(
    _source
    "(^|[^A-Za-z0-9_])Sys_LockWrite${_registry_coordinator_comment_gap}\\("
    "coordinator admission cannot block or self-deadlock on a held reader")
require_ordered(
    _source
    "Sys_TryLockWrite(&db_hashCritSect)"
    "coordinator->publishHashLockRetained(true);"
    "nonblocking hash acquisition precedes retained-receipt publication")
require_contains(
    _fixture "pre-held hash reader was not rejected"
    "runtime rejection of an existing read-held hash boundary")
foreach(_marker IN ITEMS
    "TestActiveRuntimeCallbackBorrow()"
    "ordinary borrow accepted callback-only authority"
    "callback borrow did not report hash contention"
    "foreign thread borrowed callback transaction"
    "active callback finish failed"
    "torn callback-purpose mirror authenticated"
    "borrow survived a different callback purpose"
    "TestCallbackWindowWitnessAuthentication()"
    "zero callback-window witness was recoverable"
    "torn global callback-window witness authenticated"
    "zero retained callback-window witness authenticated"
    "foreign callback-window witness authenticated"
    "operation survived a same-purpose callback-window rollover"
    "finish survived a same-purpose callback-window rollover"
    "TestCallbackReceiptFieldIsolation()"
    "high mode bits bled into callback receipt fields"
    "high callback-purpose bits bled into the witness field")
    require_contains(
        _fixture "${_marker}"
        "callback-origin coordinator runtime coverage")
endforeach()
foreach(_marker IN ITEMS
    "static_cast<std::uint16_t>(mode)\n               & kCoordinatorReceiptModeMask"
    "static_cast<std::uint16_t>(purpose)\n               << kCoordinatorReceiptPurposeShift)\n            & kCoordinatorReceiptPurposeMask"
    "static_cast<std::uint16_t>(windowWitness)\n               << kCoordinatorReceiptWitnessShift)\n            & kCoordinatorReceiptWitnessMask")
    require_contains(
        _source "${_marker}"
        "packed callback receipt field isolation")
endforeach()

extract_slice(
    _source
    "RegistryOwnershipStatus TryBorrowRegistryOwnershipCoordinator("
    "RegistryOwnershipStatus FinishRegistryOwnershipCoordinator("
    _ordinary_borrow_source
    "ordinary low-level borrowed-controller entry point")
require_contains(
    _ordinary_borrow_source
    "RegistryOwnershipCoordinatorMode::BorrowedZoneController"
    "ordinary low-level borrow selects only its quiescent mode")
require_not_contains(
    _ordinary_borrow_source "BorrowedActiveRuntimeCallback"
    "ordinary low-level borrow cannot select callback authority")
foreach(_marker IN ITEMS
    "RegistryOwnershipCoordinatorFacade::\n    TryBorrowActiveRuntimeCallback("
    "RegistryOwnershipCoordinator::tryBorrowController("
    "controller->trySnapshotRegistryCallbackTransaction("
    "controller->authenticatesRegistryCallbackTransaction("
    "RegistryOwnershipCoordinatorMode::BorrowedActiveRuntimeCallback"
    "s_borrowedCallbackPurposeMirror"
    "s_borrowedCallbackWindowWitnessMirror"
    "modeCallbackReceiptMirror_")
    require_contains(
        _source "${_marker}"
        "private mirrored callback-borrow authority")
endforeach()
foreach(_marker IN ITEMS
    "TestRegistryCoordinatorProductionStack()"
    "registry production-stack admitted a pre-held hash reader"
    "TryBeginStandaloneRegistryOwnershipCoordinator("
    "TryRegistryAddDatabaseUsers4("
    "TryRegistryInternBoundedName("
    "TryRegistryTransferDatabaseUsers4To8("
    "TryRegistryShutdownDatabaseUser8("
    "FinishRegistryOwnershipCoordinator("
    "Sys_IsWriteLocked(&db_hashCritSect)"
    "g_scriptStringTransactionLockDepth == 1")
    require_contains(
        _integration_fixture "${_marker}"
        "real production-stack coordinator composition coverage")
endforeach()

require_not_contains(
    _source "#define KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "production implementation cannot enable mutation access")
require_not_contains(
    _header "#define KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "public header cannot self-enable mutation access")
require_not_contains(
    _manifest "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "production engine manifest cannot enable coordinator test authority")

extract_slice(
    _header
    "class RegistryOwnershipCoordinatorFacade final"
    "// Production lock-order boundary"
    _coordinator_facade_declaration
    "complete private coordinator facade bridge")
set(_expected_coordinator_facade_declaration [=[
class RegistryOwnershipCoordinatorFacade final
{
public:
    RegistryOwnershipCoordinatorFacade() = delete;
    ~RegistryOwnershipCoordinatorFacade() noexcept = default;
    RegistryOwnershipCoordinatorFacade(
        const RegistryOwnershipCoordinatorFacade &) = delete;
    RegistryOwnershipCoordinatorFacade &operator=(
        const RegistryOwnershipCoordinatorFacade &) = delete;
    RegistryOwnershipCoordinatorFacade(
        RegistryOwnershipCoordinatorFacade &&) = delete;
    RegistryOwnershipCoordinatorFacade &operator=(
        RegistryOwnershipCoordinatorFacade &&) = delete;

private:
    friend class db::zone_runtime::ZoneRuntimeFacade;

    [[nodiscard]] static RegistryOwnershipStatus
    TryBeginStandalone() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus TryBorrow(
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController &controller,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryBorrowActiveRuntimeCallback(
        const zone_script_string_ownership::
            ZoneScriptStringOwnershipController &controller,
        const zone_load::ZoneLoadContextKey &expectedKey) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus Finish() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus ValidateInactive() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus ValidateActive() noexcept;
    [[nodiscard]] static bool WritableOutputIsSeparated(
        const void *output,
        std::size_t outputSize,
        std::size_t outputAlignment) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    authenticateConstructedStorage(
        RegistryOwnershipCoordinator *coordinator) noexcept;

    [[nodiscard]] static RegistryOwnershipStatus TryAddDatabaseUser4(
        std::uint32_t stringId) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus TryAddDatabaseUsers4(
        const std::uint32_t *stringIds,
        std::uint32_t count,
        RegistryOwnershipBulkResult *outResult) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus TryInternBoundedName(
        const char *bytes,
        std::uint32_t byteCount,
        RegistryOwnershipName *outName) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryReAddRetainedDefaultName(
        const char *retainedCanonicalName) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryReAddRetainedDefaultNames(
        const char *const *retainedCanonicalNames,
        std::uint32_t count,
        RegistryOwnershipBulkResult *outResult) noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryTransferDatabaseUsers4To8() noexcept;
    [[nodiscard]] static RegistryOwnershipStatus
    TryShutdownDatabaseUser8() noexcept;
};
]=])
require_normalized_equals(
    _coordinator_facade_declaration
    _expected_coordinator_facade_declaration
    "complete private facade declaration")
require_literal_count(
    _header "RegistryOwnershipCoordinatorFacade" 13
    "one complete private facade plus exact Admission/coordinator friendship")
require_regex_count(
    _coordinator_facade_declaration
    "(^|[^A-Za-z0-9_])static([^A-Za-z0-9_]|$)"
    15
    "exact private static coordinator facade authority")
require_not_contains(
    _coordinator_facade_declaration "RegistryOwnershipCoordinatorAdmission"
    "private facade mints but never exposes the admission type")

extract_slice(
    _header
    "class RegistryOwnershipCoordinatorAdmission final"
    "private:"
    _coordinator_admission_public_declaration
    "closed coordinator admission public surface")
set(_expected_coordinator_admission_public_declaration [=[
class RegistryOwnershipCoordinatorAdmission final
{
public:
    RegistryOwnershipCoordinatorAdmission() = delete;
    ~RegistryOwnershipCoordinatorAdmission() noexcept = default;
    RegistryOwnershipCoordinatorAdmission(
        const RegistryOwnershipCoordinatorAdmission &) = delete;
    RegistryOwnershipCoordinatorAdmission &operator=(
        const RegistryOwnershipCoordinatorAdmission &) = delete;
    RegistryOwnershipCoordinatorAdmission(
        RegistryOwnershipCoordinatorAdmission &&) = delete;
    RegistryOwnershipCoordinatorAdmission &operator=(
        RegistryOwnershipCoordinatorAdmission &&) = delete;

#if defined(KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)
    [[nodiscard]] static RegistryOwnershipCoordinatorAdmission
    ForTesting() noexcept;
#endif
]=])
require_normalized_equals(
    _coordinator_admission_public_declaration
    _expected_coordinator_admission_public_declaration
    "closed coordinator admission public surface")
extract_slice(
    _header
    "class RegistryOwnershipCoordinatorAdmission final"
    "RUNTIME_SIZE(RegistryOwnershipCoordinatorAdmission"
    _coordinator_admission_declaration
    "complete coordinator admission declaration")
require_regex_count(
    _coordinator_admission_declaration
    "(^|[^A-Za-z0-9_])public${_registry_coordinator_comment_gap}:"
    1
    "one admission public access section")
require_regex_count(
    _coordinator_admission_declaration
    "(^|[^A-Za-z0-9_])private${_registry_coordinator_comment_gap}:"
    1
    "one admission private access section")
require_regex_count(
    _coordinator_admission_declaration
    "(^|[^A-Za-z0-9_])protected${_registry_coordinator_comment_gap}:"
    0
    "no admission protected authority section")

extract_slice(
    _header
    "class alignas(8) RegistryOwnershipCoordinator final"
    "private:"
    _coordinator_public_declaration
    "closed coordinator public surface")
set(_expected_coordinator_public_declaration [=[
class alignas(8) RegistryOwnershipCoordinator final
{
public:
    RegistryOwnershipCoordinator() noexcept = default;
    ~RegistryOwnershipCoordinator() noexcept;

    RegistryOwnershipCoordinator(const RegistryOwnershipCoordinator &) =
        delete;
    RegistryOwnershipCoordinator &operator=(
        const RegistryOwnershipCoordinator &) = delete;
    RegistryOwnershipCoordinator(RegistryOwnershipCoordinator &&) = delete;
    RegistryOwnershipCoordinator &operator=(
        RegistryOwnershipCoordinator &&) = delete;

    [[nodiscard]] RegistryOwnershipCoordinatorPhase phase() const noexcept;
    [[nodiscard]] RegistryOwnershipCoordinatorMode mode() const noexcept;
    [[nodiscard]] std::uint64_t serial() const noexcept;
    [[nodiscard]] bool hashLockRetained() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;
    [[nodiscard]] bool isEmptyCanonical() const noexcept;
]=])
require_normalized_equals(
    _coordinator_public_declaration
    _expected_coordinator_public_declaration
    "closed coordinator public surface")
extract_slice(
    _header
    "class alignas(8) RegistryOwnershipCoordinator final"
    "RUNTIME_SIZE(RegistryOwnershipCoordinator,"
    _coordinator_declaration
    "complete coordinator declaration")
require_regex_count(
    _coordinator_declaration
    "(^|[^A-Za-z0-9_])public${_registry_coordinator_comment_gap}:"
    1
    "one coordinator public access section")
require_regex_count(
    _coordinator_declaration
    "(^|[^A-Za-z0-9_])private${_registry_coordinator_comment_gap}:"
    1
    "one coordinator private access section")
require_regex_count(
    _coordinator_declaration
    "(^|[^A-Za-z0-9_])protected${_registry_coordinator_comment_gap}:"
    0
    "no coordinator protected authority section")

# Trusted-build/ODR boundary: the script header forward-friends the real
# RegistryOwnershipCoordinator, whose complete non-equivalent definition is
# linked elsewhere. A mint-capable replacement would violate the C++ ODR; this
# project does not claim isolation from malicious same-process C++ or ODR-UB.
# The adversarial fixture and all-src scan above reject every in-repository
# attempt to create that second definition.
#
# The large script-string header is not allowlisted wholesale. Exactly one
# coordinator forward declaration and one private friend form the bridge; the
# remaining protected-name occurrence is its reviewed capability comment.
# Exact counts plus the removal-and-rescan above make every additional use a
# production-enrollment failure.
require_literal_count(
    _script_transaction_header "RegistryOwnershipCoordinator" 3
    "exact coordinator forward/friend/documentation bridge")
require_literal_count(
    _script_transaction_header "registry_ownership" 2
    "exact coordinator namespace forward/friend bridge")
foreach(_marker IN ITEMS
    "namespace db::registry_ownership"
    "class RegistryOwnershipCoordinator;"
    "class RegistryOwnershipAdmission final"
    "RegistryOwnershipAdmission() = delete;"
    "RegistryOwnershipAdmission(const RegistryOwnershipAdmission &) = delete;"
    "RegistryOwnershipAdmission(RegistryOwnershipAdmission &&) = delete;"
    "friend class db::registry_ownership::RegistryOwnershipCoordinator;"
    "RegistryOwnershipAdmission("
    "OwnershipBatch *tryAuthenticateBatchLocked() const noexcept;"
    "std::uintptr_t coordinatorAddress_ = 0;"
    "std::uintptr_t coordinatorAddressMirror_ = 0;"
    "std::uint64_t coordinatorSerial_ = 0;"
    "std::uint64_t coordinatorSerialMirror_ = 0;"
    "std::uintptr_t batchAddress_ = 0;"
    "std::uintptr_t batchAddressMirror_ = 0;"
    "std::uint64_t batchSerial_ = 0;"
    "std::uint64_t batchSerialMirror_ = 0;"
    "RUNTIME_SIZE(RegistryOwnershipAdmission, 0x30, 0x40);"
    "static_assert(!std::is_default_constructible_v<RegistryOwnershipAdmission>);"
    "static_assert(!std::is_copy_constructible_v<RegistryOwnershipAdmission>);")
    require_contains(
        _script_transaction_header "${_marker}"
        "nonforgeable exact-address registry admission")
endforeach()
require_literal_count(
    _script_transaction_header "RegistryOwnershipAdmission" 39
    "closed registry admission declaration/friend surface")

# Callback-origin borrowing is a separate, private controller capability. The
# large ownership header may contain exactly one forward declaration and one
# private friendship naming the coordinator; neither callback method is public.
require_literal_count(
    _zone_script_ownership_header "RegistryOwnershipCoordinator" 2
    "exact callback-authentication coordinator forward/friend bridge")
require_literal_count(
    _zone_script_ownership_header "registry_ownership" 2
    "exact callback-authentication namespace forward/friend bridge")
foreach(_marker IN ITEMS
    "namespace db::registry_ownership"
    "class RegistryOwnershipCoordinator;"
    "friend class db::registry_ownership::RegistryOwnershipCoordinator;"
    "enum class RegistryCallbackPurpose : std::uint8_t"
    "bool trySnapshotRegistryCallbackTransaction("
    "bool authenticatesRegistryCallbackTransaction("
    "std::uint8_t *outWindowWitness"
    "std::uint8_t expectedWindowWitness"
    "std::uint8_t callbackWindowWitness_ = 0;"
    "std::uint8_t callbackWindowWitnessMirror_ = 0;")
    require_contains(
        _zone_script_ownership_header "${_marker}"
        "private callback-specific controller authority")
endforeach()
extract_slice(
    _zone_script_ownership_header
    "class alignas(8) ZoneScriptStringOwnershipController final"
    "private:"
    _zone_controller_public_declaration
    "zone controller public declaration before callback authority")
require_not_contains(
    _zone_controller_public_declaration "RegistryCallbackPurpose"
    "callback purpose cannot become public controller vocabulary")
require_not_contains(
    _zone_controller_public_declaration
    "trySnapshotRegistryCallbackTransaction"
    "callback snapshot cannot become public controller authority")
require_not_contains(
    _zone_controller_public_declaration
    "authenticatesRegistryCallbackTransaction"
    "callback reauthentication cannot become public controller authority")

extract_slice(
    _script_transaction_header
    "// Registry operations use only the fixed legacy database masks."
    "#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)"
    _registry_admission_public_api
    "registry admission public operation declarations")
require_literal_count(
    _registry_admission_public_api
    "const RegistryOwnershipAdmission &admission"
    7
    "all scalar and bulk registry operations require the exact admission")
require_not_contains(
    _registry_admission_public_api "OwnershipBatch &"
    "an arbitrary public batch cannot authorize a registry operation")
require_not_contains(
    _registry_admission_public_api "OwnershipBatch *"
    "a public batch pointer cannot authorize a registry operation")
foreach(_registry_admission_operation IN ITEMS
    TryAddDatabaseUser4Reference
    TryAddDatabaseUser4References
    TryInternDatabaseUser4Name
    TryReAddRetainedDatabaseName
    TryReAddRetainedDatabaseNames
    TryTransferDatabaseUsers4To8
    TryShutdownDatabaseUser8)
    require_literal_count(
        _script_transaction_header
        "${_registry_admission_operation}("
        3
        "one admission friend, one batch friend, and one public declaration")
    require_contains(
        _registry_admission_public_api
        "${_registry_admission_operation}("
        "token-required registry operation declaration")
endforeach()

# Source implementations must be the same closed seven-function surface.
# This regex rejects the old public OwnershipBatch overloads without matching
# the separately named private *Internal helpers.
require_not_matches(
    _script_string_source
    "Try(AddDatabaseUser4Reference(s)?|InternDatabaseUser4Name|ReAddRetainedDatabaseName(s)?|TransferDatabaseUsers4To8|ShutdownDatabaseUser8)[ \t\r\n]*\\([^)]*OwnershipBatch[ \t\r\n]*[&*]"
    "fixed registry operations cannot accept arbitrary OwnershipBatch authority")
require_literal_count(
    _script_string_source "RegistryOwnershipAdmission" 13
    "one admission implementation/test factory and seven fixed entry points")
require_literal_count(
    _script_string_source
    "const RegistryOwnershipAdmission& registryAdmission"
    7
    "all fixed source entry points require the admission token")
require_literal_count(
    _script_string_source
    "registryAdmission.tryAuthenticateBatchLocked()"
    7
    "every fixed source entry point authenticates before batch access")

function(require_closed_registry_admission_entry START END DESCRIPTION)
    extract_slice(
        _script_string_source "${START}" "${END}"
        _entry "${DESCRIPTION}")
    string(REGEX REPLACE "[ \t\r\n]+" " " _entry "${_entry}")
    foreach(_required IN ITEMS
        "const RegistryOwnershipAdmission& registryAdmission"
        "OwnershipBatch* const batch ="
        "registryAdmission.tryAuthenticateBatchLocked()"
        "if (!batch)"
        "sl_ownershipBatchLifecycle = SL_OwnershipBatchLifecycle::Poisoned;"
        "sl_ownershipBatchLifecycleMirror = SL_OwnershipBatchLifecycle::Poisoned;"
        "OwnershipBatch::MakeMemoryTreeLeaseAdmission()")
        require_literal_count(
            _entry "${_required}" 1
            "one ${DESCRIPTION} authentication/poison step")
    endforeach()
    require_ordered(
        _entry
        "registryAdmission.tryAuthenticateBatchLocked()"
        "if (!batch)"
        "${DESCRIPTION} authenticates before token rejection")
    require_ordered(
        _entry
        "sl_ownershipBatchLifecycleMirror = SL_OwnershipBatchLifecycle::Poisoned;"
        "OwnershipBatch::MakeMemoryTreeLeaseAdmission()"
        "${DESCRIPTION} completes mirrored rejection before private batch use")
    require_ordered(
        _entry
        "registryAdmission.tryAuthenticateBatchLocked()"
        "batch->"
        "${DESCRIPTION} authenticates before any private batch access")
endfunction()

require_closed_registry_admission_entry(
    "DatabaseUserAddStatus TryAddDatabaseUser4Reference("
    "DatabaseUserAddBulkResult TryAddDatabaseUser4References("
    "scalar database-user add")
require_closed_registry_admission_entry(
    "DatabaseUserAddBulkResult TryAddDatabaseUser4References("
    "DatabaseNameResult TryInternDatabaseUser4Name("
    "bulk database-user add")
require_closed_registry_admission_entry(
    "DatabaseNameResult TryInternDatabaseUser4Name("
    "DatabaseNameStatus TryReAddRetainedDatabaseName("
    "bounded database-name intern")
require_closed_registry_admission_entry(
    "DatabaseNameStatus TryReAddRetainedDatabaseName("
    "DatabaseUserAddBulkResult TryReAddRetainedDatabaseNames("
    "scalar retained-name re-add")
require_closed_registry_admission_entry(
    "DatabaseUserAddBulkResult TryReAddRetainedDatabaseNames("
    "DatabaseSweepStatus TryTransferDatabaseUsers4To8("
    "bulk retained-name re-add")
require_closed_registry_admission_entry(
    "DatabaseSweepStatus TryTransferDatabaseUsers4To8("
    "DatabaseSweepStatus TryShutdownDatabaseUser8("
    "database-user transfer sweep")
require_closed_registry_admission_entry(
    "DatabaseSweepStatus TryShutdownDatabaseUser8("
    "} // namespace script_string"
    "database-user shutdown sweep")

extract_slice(
    _script_string_source
    "RegistryOwnershipAdmission::RegistryOwnershipAdmission("
    "#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)"
    _registry_admission_authentication
    "registry admission construction and authentication")
foreach(_marker IN ITEMS
    "coordinatorAddressMirror_(coordinatorAddress)"
    "coordinatorSerialMirror_(coordinatorSerial)"
    "batchAddressMirror_(batchAddress)"
    "batchSerialMirror_(batchSerial)"
    "coordinatorAddress_ != coordinatorAddressMirror_"
    "coordinatorSerial_ != coordinatorSerialMirror_"
    "batchAddress_ != batchAddressMirror_"
    "batchSerial_ != batchSerialMirror_"
    "SL_IsOwnershipBatchRegistryConsistentLocked()"
    "sl_activeOwnershipBatchAddress != batchAddress_"
    "sl_activeOwnershipBatchAddressMirror != batchAddress_"
    "sl_activeOwnershipBatchSerial != batchSerial_"
    "sl_activeOwnershipBatchSerialMirror != batchSerial_"
    "reinterpret_cast<OwnershipBatch*>(batchAddress_)"
    "batch->tryAuthenticateOperationLocked()")
    require_contains(
        _registry_admission_authentication "${_marker}"
        "mirrored exact-batch admission authentication")
endforeach()
require_ordered(
    _registry_admission_authentication
    "sl_activeOwnershipBatchSerialMirror != batchSerial_"
    "reinterpret_cast<OwnershipBatch*>(batchAddress_)"
    "global numeric authority must authenticate before pointer conversion")
require_ordered(
    _registry_admission_authentication
    "reinterpret_cast<OwnershipBatch*>(batchAddress_)"
    "batch->tryAuthenticateOperationLocked()"
    "the converted exact batch authenticates before private use")

extract_slice(
    _script_string_source
    "DatabaseUserAddStatus TryAddDatabaseUser4Reference("
    "} // namespace script_string"
    _registry_admission_source_api
    "seven token-required registry source entry points")
require_literal_count(
    _registry_admission_source_api
    "sl_ownershipBatchLifecycle = SL_OwnershipBatchLifecycle::Poisoned;"
    7
    "every rejected token poisons a still-active batch boundary")

foreach(_marker IN ITEMS
    "class RegistryOwnershipCoordinatorFacade final"
    "RegistryOwnershipCoordinatorFacade() = delete;"
    "RegistryOwnershipCoordinatorFacade("
    "RegistryOwnershipCoordinatorFacade &&) = delete;"
    "class RegistryOwnershipCoordinatorAdmission final"
    "RegistryOwnershipCoordinatorAdmission() = delete;"
    "RegistryOwnershipCoordinatorAdmission("
    "friend class RegistryOwnershipCoordinatorFacade;"
    "bool authenticates() const noexcept;"
    "std::uint64_t seal_ = 0;"
    "std::uint64_t sealMirror_ = 0;"
    "RUNTIME_SIZE(RegistryOwnershipCoordinatorAdmission, 0x10, 0x10);"
    "class alignas(8) RegistryOwnershipCoordinator final"
    "RegistryOwnershipCoordinator(const RegistryOwnershipCoordinator &) ="
    "RegistryOwnershipCoordinator(RegistryOwnershipCoordinator &&) = delete;"
    "RegistryOwnershipCoordinatorPhase phase() const noexcept;"
    "RegistryOwnershipCoordinatorMode mode() const noexcept;"
    "std::uint64_t serial() const noexcept;"
    "bool hashLockRetained() const noexcept;"
    "bool poisoned() const noexcept;"
    "bool isEmptyCanonical() const noexcept;"
    "FinishRegistryOwnershipCoordinator("
    "TryRegistryAddDatabaseUser4("
    "TryRegistryAddDatabaseUsers4("
    "TryRegistryInternBoundedName("
    "TryRegistryReAddRetainedDefaultName("
    "TryRegistryReAddRetainedDefaultNames("
    "TryRegistryTransferDatabaseUsers4To8("
    "TryRegistryShutdownDatabaseUser8("
    "RUNTIME_SIZE(RegistryOwnershipCoordinator,"
    "#ifdef KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "friend struct RegistryOwnershipCoordinatorTestAccess;")
    require_contains(_header "${_marker}" "sealed stable coordinator API")
endforeach()

extract_slice(
    _header
    "#if defined(KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING)\nstruct RegistryOwnershipCoordinatorTestAccess final"
    "#endif\n\n} // namespace db::registry_ownership"
    _guarded_coordinator_test_authority
    "guarded coordinator test-only declarations")
require_literal_count(
    _header "RegistryOwnershipCoordinatorTestAccess" 3
    "one guarded forward declaration, friendship, and test helper")
require_literal_count(
    _guarded_coordinator_test_authority
    "RegistryOwnershipCoordinatorTestAccess" 1
    "the complete test helper remains inside the exact bottom guard")
foreach(_test_only_name IN ITEMS
    SetCallbackReceipt
    SetRegistryOwnershipCoordinatorBoundaryForTesting
    SetNextRegistryOwnershipCoordinatorSerialForTesting
    SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting)
    require_literal_count(
        _header "${_test_only_name}" 1
        "one guarded ${_test_only_name} declaration")
    require_literal_count(
        _guarded_coordinator_test_authority "${_test_only_name}" 1
        "${_test_only_name} remains inside the exact test guard")
endforeach()
require_literal_count(
    _header "TryBorrowActiveRuntimeCallback" 2
    "one private facade callback borrow plus one guarded test bridge")
require_literal_count(
    _guarded_coordinator_test_authority
    "TryBorrowActiveRuntimeCallback" 1
    "callback-borrow test bridge remains inside the exact test guard")

foreach(_forbidden IN ITEMS
    "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "SetCallbackReceipt"
    "SetRegistryOwnershipCoordinatorBoundaryForTesting"
    "SetNextRegistryOwnershipCoordinatorSerialForTesting"
    "SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting")
    require_not_contains(
        _legacy_registry "${_forbidden}"
        "legacy registry cannot acquire test authority")
endforeach()

# The macro-off compilation seal must positively prove all private state and
# helper families inaccessible, and freeze every public API's noexcept pointer
# type—including Finish, whose omission would leave reverse-order closure open.
foreach(_marker IN ITEMS
    "SplicedFinishPointer"
    "CommentQualifiedFinishPointer"
    "CommentNamespaceProbe"
    "CanCallBoundarySetter"
    "CanCallNextSerialSetter"
    "CanCallGlobalMirrorsSetter"
    "DependentScalar"
    "CanReachBatch"
    "CanReachStandaloneToken"
    "CanCallMakeOperationAdmission"
    "CanMutateBorrowedControllerAddress"
    "CanMutateBorrowedControllerAddressMirror"
    "CanMutateKey"
    "CanMutateKeyMirror"
    "CanMutateSerial"
    "CanMutateSerialMirror"
    "CanMutateBorrowedSerial"
    "CanMutateBorrowedSerialMirror"
    "CanMutateStandaloneSerial"
    "CanMutateStandaloneSerialMirror"
    "CanMutatePhase"
    "CanMutatePhaseMirror"
    "CanMutateModeCallbackReceipt"
    "CanMutateModeCallbackReceiptMirror"
    "CanMutateHashReceipt"
    "CanMutateHashReceiptMirror"
    "CanCallCanonicalAfterStandaloneBegin"
    "CanCallRepresentationConsistent"
    "CanCallOwnsRegistryBoundary"
    "CanCallAuthenticatesOuterTransaction"
    "CanCallBeginRegistered"
    "CanCallTryBorrowController"
    "CanCallBeginOperation"
    "CanCallFinishOperation"
    "CanCallPublishPhase"
    "CanCallPublishHashLockRetained"
    "CanCallPoisonBoundary"
    "CanCallResetAfterFinish"
    "using FinishFunction ="
    "decltype(&FinishRegistryOwnershipCoordinator)"
    "decltype(&TryRegistryAddDatabaseUsers4)"
    "decltype(&TryRegistryReAddRetainedDefaultNames)"
    "RegistryOwnershipAdmissionTestAccess"
    "CanAuthenticateBatch"
    "RegistryAddBulkFunction"
    "RegistryReAddBulkFunction"
    "decltype(&TryAddDatabaseUser4References)"
    "decltype(&TryReAddRetainedDatabaseNames)"
    "RegistryOwnershipCoordinatorAdmissionTestAccess"
    "CanMutateSealMirror"
    "using CoordinatorAdmission = RegistryOwnershipCoordinatorAdmission;"
    "using CoordinatorFacade = RegistryOwnershipCoordinatorFacade;"
    "ZoneControllerCallbackProductionProbe"
    "CanTrySnapshotRegistryCallback"
    "CanAuthenticateRegistryCallback"
    "RegistryOwnershipCoordinatorFacadeProductionProbe"
    "CanTryBeginStandalone"
    "CanTryBorrow"
    "CanTryBorrowActiveRuntimeCallback"
    "CanFinish"
    "CanValidateInactive"
    "CanValidateActive"
    "CanWritableOutputIsSeparated"
    "CanAuthenticateConstructedStorage"
    "CanTryAddDatabaseUser4"
    "CanTryAddDatabaseUsers4"
    "CanTryInternBoundedName"
    "CanTryReAddRetainedDefaultName"
    "CanTryReAddRetainedDefaultNames"
    "CanTryTransferDatabaseUsers4To8"
    "CanTryShutdownDatabaseUser8"
    "std::is_empty_v<CoordinatorFacade>"
    "std::is_final_v<CoordinatorFacade>"
    "std::is_constructible_v<"
    "std::is_nothrow_destructible_v<RegistryOwnershipCoordinator>"
    "noexcept(TryBeginStandaloneRegistryOwnershipCoordinator("
    "noexcept(FinishRegistryOwnershipCoordinator(nullptr))")
    require_contains(
        _production_seal "${_marker}"
        "macro-off private/noexcept production seal")
endforeach()
require_not_contains(
    _production_seal "#define KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "production seal cannot enable test access")

# A literal macro-off fixture must exercise the complete production path from
# the facade's stable callback bank through this coordinator and the real
# script-string registry. Keep the contention retry, successful mutation and
# explicit finish in one process, while the deliberately forgotten finish is
# isolated in a second process that proves the boundary remains non-retirable.
extract_slice(
    _stable_integration_fixture
    "ZoneScriptStringUnpublishStatus EnsureGenerationUnreachable("
    "ZoneLoadCleanupCallbackStatus PerformExternalCleanup("
    _stable_registry_callback
    "stable production registry callback")
foreach(_marker IN ITEMS
    "ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback("
    "g_callbackProbe.firstBorrow"
    "RegistryOwnershipStatus::Busy"
    "ZoneScriptStringUnpublishStatus::Retry"
    "g_callbackProbe.retryBorrow"
    "RegistryOwnershipStatus::Success"
    "ZoneRuntimeFacade::TryAddDatabaseUser4("
    "ZoneRuntimeFacade::FinishRegistryOwnership();")
    require_contains(
        _stable_registry_callback "${_marker}"
        "stable callback coordinator flow")
endforeach()
require_ordered(
    _stable_registry_callback
    "g_callbackProbe.firstBorrow"
    "g_callbackProbe.retryBorrow"
    "busy callback borrow before the successful retry")
require_ordered(
    _stable_registry_callback
    "g_callbackProbe.retryBorrow"
    "ZoneRuntimeFacade::TryAddDatabaseUser4("
    "successful callback borrow before real registry mutation")
require_ordered(
    _stable_registry_callback
    "ZoneRuntimeFacade::TryAddDatabaseUser4("
    "ZoneRuntimeFacade::FinishRegistryOwnership();"
    "registry mutation before explicit coordinator finish")

extract_slice(
    _stable_integration_fixture
    "[[nodiscard]] bool TestStableCallbackRetryChain() noexcept"
    "[[nodiscard]] bool TestOmittedCallbackFinishFailsClosed() noexcept"
    _stable_registry_retry_run
    "stable registry contention/retry run")
foreach(_marker IN ITEMS
    "#include <script/scr_stringlist.cpp>"
    "FastCriticalSection db_hashCritSect{};"
    "script_string::TryAcquireOrdinaryStringOfSize("
    "Sys_LockWrite(&db_hashCritSect);"
    "ZoneRuntimeFacade::TryBeginGenerationAbandonment("
    "Sys_UnlockWrite(&db_hashCritSect);"
    "firstAbandonment == ZoneRuntimeTableStatus::Retry"
    "g_callbackProbe.firstBorrow"
    "RegistryOwnershipStatus::Busy"
    "ZoneRuntimeFacade::TryContinueGenerationAbandonment("
    "g_callbackProbe.retryBorrow"
    "g_callbackProbe.addStatus"
    "g_callbackProbe.finishStatus")
    require_contains(
        _stable_integration_fixture "${_marker}"
        "literal real-registry stable integration fixture")
endforeach()
require_ordered(
    _stable_registry_retry_run
    "Sys_LockWrite(&db_hashCritSect);"
    "ZoneRuntimeFacade::TryBeginGenerationAbandonment("
    "real hash lock held before the first callback attempt")
require_ordered(
    _stable_registry_retry_run
    "ZoneRuntimeFacade::TryBeginGenerationAbandonment("
    "Sys_UnlockWrite(&db_hashCritSect);"
    "first callback attempt completes before hash-lock release")
require_ordered(
    _stable_registry_retry_run
    "Sys_UnlockWrite(&db_hashCritSect);"
    "ZoneRuntimeFacade::TryContinueGenerationAbandonment("
    "hash-lock release before successful callback retry")

extract_slice(
    _stable_integration_fixture
    "[[nodiscard]] bool TestOmittedCallbackFinishFailsClosed() noexcept"
    "} // namespace"
    _stable_registry_omit_finish_run
    "forgotten coordinator finish run")
foreach(_marker IN ITEMS
    "g_callbackProbe.omitFinish = true;"
    "g_callbackProbe.omittedFinishBorrow"
    "RegistryOwnershipStatus::Success"
    "abandonment == ZoneRuntimeTableStatus::UnsafeFailure"
    "entry->executionMode()"
    "!= ZoneRuntimeExecutionMode::Terminal"
    "!entry->generationBindingPristine()"
    "ZoneRuntimeFacade::FinishAccess()"
    "== ZoneRuntimeFacadeStatus::UnsafeFailure"
    "ZoneRuntimeFacade::TryBeginAccess()"
    "facade boundary reopened")
    require_contains(
        _stable_registry_omit_finish_run "${_marker}"
        "forgotten finish fails closed")
endforeach()
require_contains(
    _stable_integration_fixture
    "std::strcmp(argv[1], \"--omit-finish\") == 0"
    "forgotten finish uses a process-isolated mode")

# The integration translation unit includes the real file-local string
# registry and links all other production components. It must never grow a
# fixture-local coordinator, admission, batch, registry implementation, or a
# testing-authority grant.
foreach(_forbidden IN ITEMS
    "class RegistryOwnershipCoordinator"
    "struct RegistryOwnershipCoordinator"
    "RegistryOwnershipCoordinator::RegistryOwnershipCoordinator("
    "class RegistryOwnershipAdmission"
    "struct RegistryOwnershipAdmission"
    "RegistryOwnershipAdmission::"
    "class OwnershipBatch"
    "struct OwnershipBatch"
    "OwnershipBatch::"
    "void SL_Init("
    "void SL_Shutdown("
    "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING"
    "KISAK_MEMORY_TREE_VALIDATION_TESTING"
    "KISAK_SCRIPT_STRING_PERF_TESTING")
    require_not_contains(
        _stable_integration_fixture "${_forbidden}"
        "macro-off fixture cannot replace production ownership code")
endforeach()

extract_slice(
    _tests
    "add_executable(kisakcod-db-zone-runtime-stable-context-integration-tests"
    "add_executable(kisakcod-fx-archive-disk32-tests"
    _stable_registry_integration_registration
    "stable production registry integration registration")
foreach(_marker IN ITEMS
    "db_zone_runtime_stable_context_integration_tests.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_facade.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_callback_context.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_table.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_storage.cpp"
    "\${SRC_DIR}/database/db_zone_stream_ownership.cpp"
    "\${SRC_DIR}/database/db_zone_pending_copy_ledger.cpp"
    "\${SRC_DIR}/database/db_zone_script_string_ownership.cpp"
    "\${SRC_DIR}/database/db_script_string_adapter.cpp"
    "\${SRC_DIR}/database/db_script_string_journal.cpp"
    "\${SRC_DIR}/database/db_script_string_transaction.cpp"
    "\${SRC_DIR}/database/db_zone_load_context.cpp"
    "\${SRC_DIR}/database/db_relocation.cpp"
    "\${SRC_DIR}/database/db_stream.cpp"
    "\${SRC_DIR}/database/db_registry_ownership_coordinator.cpp"
    "\${SRC_DIR}/EffectsCore/fx_zone_runtime_storage_bridge.cpp"
    "\${SRC_DIR}/universal/physicalmemory.cpp"
    "\${SRC_DIR}/universal/physicalmemory_checked.cpp"
    "\${SRC_DIR}/qcommon/sys_sync.cpp"
    "\${SRC_DIR}/script/scr_memorytree.cpp"
    "kisakcod-fx-fastfile-zone-adapter-disk32-subject"
    "kisakcod-fx-fastfile-native-arena-subject"
    "kisakcod-fx-fastfile-native-disk32-subject"
    "kisakcod-fx-fastfile-impact-native-disk32-subject"
    "PRIVATE cxx_std_20"
    "PRIVATE KISAK_MP"
    "PRIVATE Threads::Threads"
    "PRIVATE /wd4702)"
    "PRIVATE \"LINKER:/STACK:8388608\""
    "NAME database-zone-runtime-stable-context-integration"
    "NAME database-zone-runtime-stable-context-forgotten-finish"
    "--omit-finish"
    "PROPERTIES TIMEOUT 30")
    require_contains(
        _stable_registry_integration_registration "${_marker}"
        "complete macro-off production dependency/CTest enrollment")
endforeach()
foreach(_forbidden IN ITEMS
    "TESTING"
    "KISAK_PLATFORM_SERVICE_SOURCES"
    "winmm"
    "WILL_FAIL"
    "EXCLUDE_FROM_ALL")
    require_not_contains(
        _stable_registry_integration_registration "${_forbidden}"
        "stable production integration target remains macro-off and positive")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-stable-context-integration-tests"
    "database-zone-runtime-stable-context-(integration|forgotten-finish)")
    require_contains(
        _ci "${_marker}"
        "explicit Windows x86 stable integration enrollment")
endforeach()

# No production target, toolchain, profile, or workflow may inject the testing
# macro. Its only build-definition occurrences belong to the dedicated unit
# fixture and the real production-stack composition fixture, while the adjacent
# macro-off production seal remains independent.
extract_slice(
    _tests
    "add_executable(kisakcod-db-registry-ownership-coordinator-tests"
    "add_executable(\n    kisakcod-db-registry-ownership-coordinator-production-seal-tests"
    _coordinator_runtime_test_registration
    "coordinator runtime test CMake registration")
require_literal_count(
    _coordinator_runtime_test_registration
    "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING=1"
    1
    "the coordinator unit fixture receives test authority once")
extract_slice(
    _tests
    "add_executable(kisakcod-script-string-ownership-tests"
    "foreach(_profile mp sp)"
    _coordinator_integration_test_registration
    "coordinator production-stack integration CMake registration")
require_literal_count(
    _coordinator_integration_test_registration
    "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING=1"
    1
    "the production-stack fixture receives test authority once")
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_registry_ownership_coordinator.cpp"
    "\${SRC_DIR}/database/db_script_string_transaction.cpp"
    "\${SRC_DIR}/qcommon/sys_sync.cpp"
    "\${SRC_DIR}/script/scr_memorytree.cpp")
    require_contains(
        _coordinator_integration_test_registration "${_marker}"
        "real coordinator dependency composition")
endforeach()
require_literal_count(
    _tests
    "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    2
    "only the two reviewed runtime fixtures receive coordinator test authority")

file(GLOB_RECURSE _registry_coordinator_build_definition_paths
    LIST_DIRECTORIES FALSE
    "${SOURCE_ROOT}/scripts/CMakeLists.txt"
    "${SOURCE_ROOT}/scripts/*.cmake"
    "${SOURCE_ROOT}/.github/workflows/*.yml"
    "${SOURCE_ROOT}/.github/workflows/*.yaml")
list(APPEND _registry_coordinator_build_definition_paths
    "${SOURCE_ROOT}/CMakeLists.txt"
    "${_tests_path}")
list(REMOVE_DUPLICATES _registry_coordinator_build_definition_paths)
foreach(_build_sentinel IN ITEMS
    "${SOURCE_ROOT}/scripts/mp/CMakeLists.txt"
    "${SOURCE_ROOT}/scripts/dedi/CMakeLists.txt"
    "${SOURCE_ROOT}/scripts/sp/CMakeLists.txt"
    "${SOURCE_ROOT}/scripts/platform/linux/platform.cmake"
    "${_ci_path}")
    list(FIND _registry_coordinator_build_definition_paths
        "${_build_sentinel}" _build_sentinel_index)
    if(_build_sentinel_index EQUAL -1)
        message(FATAL_ERROR
            "Coordinator build-definition scan lost ${_build_sentinel}")
    endif()
endforeach()
foreach(_build_path IN LISTS _registry_coordinator_build_definition_paths)
    file(READ "${_build_path}" _build_text)
    normalize_registry_coordinator_phase2(_build_text _build_phase2)
    string(FIND
        "${_build_phase2}"
        "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
        _test_macro_position)
    if(NOT _test_macro_position EQUAL -1
        AND NOT _build_path STREQUAL "${_tests_path}")
        message(FATAL_ERROR
            "Coordinator testing authority escaped its exact runtime fixture: "
            "${_build_path}")
    endif()
endforeach()

extract_slice(
    _tests
    "add_executable(\n    kisakcod-db-registry-ownership-coordinator-production-seal-tests"
    "add_executable(kisakcod-db-zone-runtime-table-tests"
    _coordinator_production_seal_registration
    "coordinator production-seal CMake registration")
foreach(_marker IN ITEMS
    "db_registry_ownership_coordinator_production_seal_tests.cpp"
    "PRIVATE cxx_std_20"
    "PRIVATE KISAK_MP"
    "database-registry-ownership-production-test-access-sealed"
    "PROPERTIES TIMEOUT 20")
    require_contains(
        _coordinator_production_seal_registration "${_marker}"
        "positive macro-off production-seal target")
endforeach()
foreach(_forbidden IN ITEMS
    "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING"
    "WILL_FAIL"
    "EXCLUDE_FROM_ALL")
    require_not_contains(
        _coordinator_production_seal_registration "${_forbidden}"
        "production seal must compile normally without test authority")
endforeach()

# Keep the implementation, tests, source gate, production seal, and explicit
# Windows x86 CI selection inseparable from the foundation commit.
foreach(_marker IN ITEMS
    "database/db_registry_ownership_coordinator.cpp"
    "database/db_registry_ownership_coordinator.h")
    require_contains(_manifest "${_marker}" "shared engine source manifest")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-registry-ownership-coordinator-tests"
    "kisakcod-db-registry-ownership-coordinator-production-seal-tests"
    "database-registry-ownership-coordinator"
    "database-registry-ownership-production-test-access-sealed"
    "database-registry-ownership-source-invariants"
    "database-registry-ownership-coordinator PROPERTIES TIMEOUT 30"
    "database-registry-ownership-production-test-access-sealed"
    "PROPERTIES TIMEOUT 20"
    "db_registry_ownership_coordinator_source_test.cmake")
    require_contains(_tests "${_marker}" "portable CMake registration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-registry-ownership-coordinator-tests"
    "kisakcod-db-registry-ownership-coordinator-production-seal-tests"
    "database-registry-ownership-(coordinator|production-test-access-sealed|source-invariants)")
    require_contains(_ci "${_marker}" "explicit Windows x86 CI selection")
endforeach()
foreach(_marker IN ITEMS
    "db_registry_ownership_coordinator_source_test.cmake"
    "normalize_registry_coordinator_phase2"
    "find_registry_coordinator_forbidden_production_token"
    "require_closed_registry_admission_entry"
    "_expected_coordinator_facade_declaration"
    "SetRegistryOwnershipCoordinatorGlobalMirrorsForTesting"
    "_registry_coordinator_build_definition_paths"
    "CanCallGlobalMirrorsSetter"
    "registry coordinator admission must remain nonblocking"
    "_registry_coordinator_form_feed"
    "_registry_coordinator_vertical_tab"
    "LIST_DIRECTORIES FALSE")
    require_contains(_security "${_marker}" "security-regression meta-seal")
endforeach()

message(STATUS
    "Registry ownership coordinator source invariants verified")

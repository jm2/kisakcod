cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_pending_copy_ledger.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_pending_copy_ledger.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_pending_copy_ledger_tests.cpp")
set(_production_seal_path
    "${SOURCE_ROOT}/tests/db_zone_pending_copy_ledger_production_seal_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_legacy_header_path "${SOURCE_ROOT}/src/database/database.h")
set(_legacy_source_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
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
    "${_fixture_path}"
    "${_production_seal_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}"
    "${_legacy_header_path}"
    "${_legacy_source_path}"
    "${_integer_suffix_token_paste_path}"
    "${_server_token_literal_path}"
    "${_ui_component_token_literal_path}"
    "${_ui_parser_token_literal_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing pending-copy ledger input: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_production_seal_path}" _production_seal)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)
file(READ "${_legacy_header_path}" _legacy_header)
file(READ "${_legacy_source_path}" _legacy_source)

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing pending-copy invariant (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden pending-copy regression (${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_matches SOURCE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        message(FATAL_ERROR
            "Missing pending-copy invariant (${DESCRIPTION}): '${PATTERN}'")
    endif()
endfunction()

function(require_not_matches SOURCE_VAR PATTERN DESCRIPTION)
    string(REGEX MATCH "${PATTERN}" _match "${${SOURCE_VAR}}")
    if(NOT _match STREQUAL "")
        message(FATAL_ERROR
            "Forbidden pending-copy regression (${DESCRIPTION}): '${_match}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered pending-copy invariant (${DESCRIPTION})")
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

# Any public symbol is an enrollment oracle. Translation phase 2 joins escaped
# physical lines before header names and identifiers form. Phase-3 comments are
# accepted as complete token gaps around qualification and namespace grammar,
# avoiding a lossy comment-stripping pass that could alter string literals.
string(ASCII 92 _pending_copy_backslash)
string(ASCII 13 _pending_copy_carriage_return)
string(ASCII 10 _pending_copy_line_feed)
set(_pending_copy_block_comment "/\\*([^*]|\\*+[^*/])*\\*+/")
set(_pending_copy_comment_atom
    "([ \t\r\n]|${_pending_copy_block_comment}|//[^\r\n]*)")
set(_pending_copy_comment_gap "${_pending_copy_comment_atom}*")
set(_pending_copy_comment_separator "${_pending_copy_comment_atom}+")

set(_enrollment_tokens
    db_zone_pending_copy_ledger
    zone_pending_copy
    PendingCopyStatus
    PendingCopyAdmissionPhase
    PendingCopyDrainCallbackStatus
    PendingCopyRecord
    PendingCopyAdmissionCompletion
    PendingCopyDrainCallback
    PendingCopyAdmissionReceipt
    PendingCopyLedger
    PendingCopyLedgerTestAccess
    TryInitializePendingCopyLedger
    TryBeginPendingCopyAdmission
    TryAppendPendingCopyRecord
    TryReadPendingCopyRecord
    TryPreparePendingCopyAdmission
    FinalizePreparedPendingCopyAdmission
    TryDiscardPendingCopyAdmission
    TryBeginPendingCopyDrain
    TryDrainNextPendingCopy
    TryFinishPendingCopyDrain
    TryResetPendingCopyAdmissionReceipt
    kPendingCopyRecordCapacity
    kPendingCopyGenerationCapacity
    KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING)

function(normalize_pending_copy_phase2 SOURCE_VAR OUT_VAR)
    set(_spliced "${${SOURCE_VAR}}")
    string(REPLACE
        "${_pending_copy_backslash}${_pending_copy_carriage_return}${_pending_copy_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_pending_copy_backslash}${_pending_copy_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_pending_copy_backslash}${_pending_copy_carriage_return}"
        "" _spliced "${_spliced}")
    set(${OUT_VAR} "${_spliced}" PARENT_SCOPE)
endfunction()

function(source_has_pending_copy_identifier SOURCE_VAR IDENTIFIER OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])${IDENTIFIER}([^A-Za-z0-9_]|$)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_qualified_pending_copy SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])((db${_pending_copy_comment_gap}::${_pending_copy_comment_gap})?zone_pending_copy)${_pending_copy_comment_gap}::"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_pending_copy_namespace_declaration SOURCE_VAR OUT_VAR)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])namespace${_pending_copy_comment_separator}((db${_pending_copy_comment_gap}::${_pending_copy_comment_gap})?zone_pending_copy)(${_pending_copy_comment_gap}::|${_pending_copy_comment_gap}\\{)"
        _match "${${SOURCE_VAR}}")
    if(_match STREQUAL "")
        set(${OUT_VAR} FALSE PARENT_SCOPE)
    else()
        set(${OUT_VAR} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(source_has_preprocessor_token_paste SOURCE_VAR OUT_VAR)
    # After exact path-specific removal of reviewed fixed suffixes and
    # literals, reject every spelling that can become a paste operator. This
    # conservative raw policy cannot be confused by comments, raw strings, or
    # translation-phase ordering.
    foreach(_operator IN ITEMS "##" "%:%:" "??/" "??=")
        string(FIND "${${SOURCE_VAR}}" "${_operator}" _operator_position)
        if(NOT _operator_position EQUAL -1)
            set(${OUT_VAR} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${OUT_VAR} FALSE PARENT_SCOPE)
endfunction()

function(remove_reviewed_pending_copy_token_text PATH SOURCE_VAR OUT_VAR)
    set(_candidate "${${SOURCE_VAR}}")
    if(PATH STREQUAL _integer_suffix_token_paste_path)
        # q_shared's suffixes are fixed and no protected identifier ends in
        # LL or i64. Any altered or additional paste remains visible.
        string(REPLACE "num ## LL" "" _candidate "${_candidate}")
        string(REPLACE "num ## i64" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _server_token_literal_path)
        string(REPLACE
            "\"###!#!#!#!#!#!#!#!#!#!#!#!#!#!#!#!###\\n\""
            "" _candidate "${_candidate}")
        string(REPLACE
            "\"########################################\\n\""
            "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _ui_component_token_literal_path)
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
    elseif(PATH STREQUAL _ui_parser_token_literal_path)
        string(REPLACE "\"##\"" "" _candidate "${_candidate}")
        string(REPLACE
            "\"define with misplaced ##\"" "" _candidate "${_candidate}")
    endif()
    set(${OUT_VAR} "${_candidate}" PARENT_SCOPE)
endfunction()

function(detect_production_enrollment SOURCE_VAR OUT_VAR)
    normalize_pending_copy_phase2(${SOURCE_VAR} _candidate)
    set(_found FALSE)
    string(FIND "${_candidate}" "db_zone_pending_copy_ledger.h" _header_ref)
    if(NOT _header_ref EQUAL -1)
        set(_found TRUE)
    endif()
    foreach(_token IN LISTS _enrollment_tokens)
        source_has_pending_copy_identifier(
            _candidate "${_token}" _token_found)
        if(_token_found)
            set(_found TRUE)
        endif()
    endforeach()
    source_has_qualified_pending_copy(_candidate _qualified_found)
    source_has_pending_copy_namespace_declaration(
        _candidate _namespace_found)
    source_has_preprocessor_token_paste(_candidate _token_paste_found)
    if(_qualified_found OR _namespace_found OR _token_paste_found)
        set(_found TRUE)
    endif()
    set(${OUT_VAR} ${_found} PARENT_SCOPE)
endfunction()

function(require_detector_fixture SOURCE_VAR DESCRIPTION)
    detect_production_enrollment(${SOURCE_VAR} _detected)
    if(NOT _detected)
        message(FATAL_ERROR
            "Pending-copy enrollment detector missed ${DESCRIPTION}")
    endif()
endfunction()

# Compile-valid adversarial fixtures pin phase-2-spliced header/API tokens,
# phase-3 block/line-comment qualification, manual namespace declarations,
# using declarations, namespace aliases, unqualified function pointers, and
# both spellings of the preprocessing token-paste operator.
string(CONCAT _phase2_header_bypass
    "#include <database/db_zone_pending_copy_led"
    "${_pending_copy_backslash}${_pending_copy_line_feed}ger.h>")
require_detector_fixture(_phase2_header_bypass "a phase-2-spliced include")

string(CONCAT _macro_header_bypass
    "#define KISAK_PENDING_INCLUDE(name) <database/name.h>\n"
    "#include KISAK_PENDING_INCLUDE(db_zone_pending_copy_ledger)")
require_detector_fixture(
    _macro_header_bypass "a macro-generated pending-copy include")

string(CONCAT _qualified_using_bypass
    "using db/**/::/**/zone_pending_copy/**/::/**/"
    "TryBeginPendingCopyAdmission;\n"
    "auto begin = &TryBeginPendingCopyAdmission;")
require_detector_fixture(
    _qualified_using_bypass "a phase-3-commented using declaration")

string(CONCAT _phase2_pointer_bypass
    "auto drain = &TryDrainNextPendingCo"
    "${_pending_copy_backslash}${_pending_copy_line_feed}py;")
require_detector_fixture(
    _phase2_pointer_bypass "a phase-2-spliced function pointer")

set(_compact_namespace_bypass
    "namespace/**/db/**/::/**/zone_pending_copy { class Forged; }")
set(_nested_namespace_bypass
    "namespace/**/db { namespace/**/zone_pending_copy { class Forged; } }")
require_detector_fixture(
    _compact_namespace_bypass "a compact manual namespace declaration")
require_detector_fixture(
    _nested_namespace_bypass "a nested manual namespace declaration")

string(CONCAT _namespace_alias_bypass
    "namespace pending = db// phase-3 separator\n"
    "::/**/zone_pending_copy;")
require_detector_fixture(
    _namespace_alias_bypass "a phase-3-commented namespace alias")

string(CONCAT _manual_declaration_bypass
    "PendingCopyStatus TryResetPendingCopyAdmission"
    "${_pending_copy_backslash}${_pending_copy_line_feed}Receipt("
    "PendingCopyAdmissionReceipt *, const ZoneLoadContextKey &);")
require_detector_fixture(
    _manual_declaration_bypass "a phase-2-spliced manual declaration")

string(CONCAT _hash_token_paste_bypass
    "#define KISAK_PENDING_CAT_I(left, right) left/**/##/**/right\n"
    "#define KISAK_PENDING_CAT(left, right) "
    "KISAK_PENDING_CAT_I(left, right)\n"
    "auto reset = &KISAK_PENDING_CAT("
    "TryResetPendingCopyAdmission, Receipt);")
require_detector_fixture(
    _hash_token_paste_bypass "a hash token-paste API construction")

string(CONCAT _digraph_token_paste_bypass
    "%: define KISAK_PENDING_DIGRAPH(left, right) left %:%: right\n"
    "auto append = &KISAK_PENDING_DIGRAPH("
    "TryAppendPendingCopy, Record);")
require_detector_fixture(
    _digraph_token_paste_bypass "a digraph token-paste API construction")

set(_trigraph_token_paste_bypass
    "??=define KISAK_PENDING_TRIGRAPH(left, right) left ??=??= right")
require_detector_fixture(
    _trigraph_token_paste_bypass "a trigraph token-paste construction")

string(CONCAT _trigraph_splice_token_paste_bypass
    "#define KISAK_PENDING_TRIGRAPH_SPLICE(left, right) left #??/"
    "${_pending_copy_line_feed}# right")
require_detector_fixture(
    _trigraph_splice_token_paste_bypass
    "a phase-1 trigraph and phase-2 splice paste construction")

string(CONCAT _comment_quote_token_paste_bypass
    "/* \"\n*/ %: define KISAK_PENDING_COMMENT_CAT(left, right) "
    "left %:%: right /* \" */")
require_detector_fixture(
    _comment_quote_token_paste_bypass
    "a multiline-comment and unmatched-quote token-paste construction")

set(_server_payload_macro_bypass
    "#define KISAK_PENDING_HASH_RUN ########################################")
remove_reviewed_pending_copy_token_text(
    "${_server_token_literal_path}"
    _server_payload_macro_bypass
    _server_payload_macro_reviewed)
require_detector_fixture(
    _server_payload_macro_reviewed
    "a server diagnostic-payload macro construction")

set(_detector_negative
    "struct PendingCopySomethingElse { int unrelated; };")
detect_production_enrollment(_detector_negative _false_positive)
if(_false_positive)
    message(FATAL_ERROR
        "Pending-copy enrollment detector lost identifier boundaries")
endif()

file(GLOB_RECURSE _production_sources
    LIST_DIRECTORIES FALSE "${SOURCE_ROOT}/src/*")
foreach(_non_extension_sentinel IN ITEMS
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.am"
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.in")
    list(FIND _production_sources
        "${_non_extension_sentinel}" _sentinel_index)
    if(_sentinel_index EQUAL -1)
        message(FATAL_ERROR
            "Pending-copy production seal lost extension-independent traversal: "
            "${_non_extension_sentinel}")
    endif()
endforeach()
foreach(_production_path IN LISTS _production_sources)
    if(_production_path STREQUAL _header_path
        OR _production_path STREQUAL _source_path)
        continue()
    endif()
    file(READ "${_production_path}" _production_raw)
    normalize_pending_copy_phase2(_production_raw _production_phase2)
    remove_reviewed_pending_copy_token_text(
        "${_production_path}" _production_phase2 _production_text)
    detect_production_enrollment(_production_text _enrolled)
    if(_enrolled)
        message(FATAL_ERROR
            "Premature pending-copy ledger enrollment in ${_production_path}")
    endif()
endforeach()

# The foundation is fixed-capacity, allocation independent, externally
# serialized, report-free, and unaware of every production subsystem it will
# eventually coordinate. The later caller cutover must be one atomic batch.
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "database/database.h"
        "database/db_registry"
        "database/db_load"
        "qcommon/qcommon.h"
        "EffectsCore/"
        "g_copyInfo"
        "XAsset"
        "PMem_"
        "stream"
        "Com_Error"
        "Com_Print"
        "Sys_Error"
        "MyAssertHandler"
        "#include <vector>"
        "#include <string>"
        "#include <mutex>"
        "#include <atomic>"
        "std::function"
        "operator new"
        "malloc("
        "calloc("
        "realloc("
        "catch (")
        require_not_contains(
            ${_var} "${_forbidden}" "production-neutral no-allocation boundary")
    endforeach()
    require_not_matches(
        ${_var}
        "(^|[^A-Za-z0-9_])new[ \t\r\n]+[A-Za-z_(]"
        "dynamic allocation expression")
    require_not_matches(
        ${_var}
        "(^|[^A-Za-z0-9_])throw[ \t\r\n]+"
        "exception throw expression")
    require_not_matches(
        ${_var}
        "(^|[^A-Za-z0-9_])DB_[A-Za-z0-9_]+"
        "legacy database operation")
endforeach()

foreach(_marker IN ITEMS
    "kPendingCopyRecordCapacity = 2048"
    "kPendingCopyGenerationCapacity = 8"
    "kFirstAssetEntryIndex = 1"
    "kLastAssetEntryIndex = 0x7FFF"
    "RUNTIME_SIZE(PendingCopyRecord, 0x18, 0x18);"
    "std::array<PendingCopyRecord, kPendingCopyRecordCapacity> records_{};"
    "std::array<GenerationDescriptor, kPendingCopyGenerationCapacity>"
    "const PendingCopyLedger *self_ = nullptr;"
    "const PendingCopyAdmissionReceipt *self_ = nullptr;"
    "std::uint64_t generationSerial_ = 0;"
    "std::uint64_t nextGenerationSerial_ = 0;"
    "caller must externally serialize"
    "There is no internal cross-thread locking."
    "PendingCopyLedger(const PendingCopyLedger &) = delete;"
    "const PendingCopyAdmissionReceipt &) = delete;"
    "#ifdef KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING"
    "friend struct PendingCopyLedgerTestAccess;")
    require_contains(_header "${_marker}" "stable bounded authority")
endforeach()
require_not_contains(
    _source "#define KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING"
    "production source cannot enable mutation access")

# Receipt authentication, canonical unused spans, exact serial exhaustion,
# full preflight-before-publication, and terminal-first retry logic prevent ABA
# authority reuse and partial mutation on every status-bearing failure.
foreach(_marker IN ITEMS
    "self_ != this"
    "receipt->self_ = receipt;"
    "receipt->generationSerial_ = serial;"
    "descriptor.serial = serial;"
    "descriptor.receipt == this"
    "descriptor.serial == generationSerial_"
    "std::uint64_t previousSerial = 0;"
    "descriptor.serial <= previousSerial"
    "descriptor.serial >= nextGenerationSerial_"
    "previousSerial = descriptor.serial;"
    "if (ledger->nextGenerationSerial_ == UINT64_MAX)"
    "return PendingCopyStatus::GenerationExhausted;"
    "if (ledger->recordCount_ >= ledger->records_.size())"
    "return PendingCopyStatus::CapacityExceeded;"
    "if (ledger->generationCount_ >= ledger->generations_.size())"
    "return PendingCopyStatus::GenerationCapacityExceeded;"
    "!= PendingCopyLedger::GenerationPhase::Admitted)"
    "ledger, sizeof(*ledger), outRecord, sizeof(*outRecord)"
    "receipt, sizeof(*receipt), outRecord, sizeof(*outRecord)"
    "sizeof(*receipt->lifecycle_),"
    "if (IsTerminalReceiptPhase(receipt->phase_))"
    "return PendingCopyStatus::AlreadyComplete;"
    "ledger->records_[source - removed.recordCount]"
    "shifted.firstRecord -= removed.recordCount;"
    "shifted.receipt->generationIndex_ = source - 1;"
    "ledger->records_[index] = {};"
    "ledger->generations_[ledger->generationCount_] = {};")
    require_contains(_source "${_marker}" "failure-atomic stable ledger")
endforeach()
require_ordered(
    _source
    "if (ledger->nextGenerationSerial_ == UINT64_MAX)"
    "++ledger->nextGenerationSerial_;"
    "serial exhaustion precedes increment")
require_ordered(
    _source
    "descriptor.serial <= previousSerial"
    "previousSerial = descriptor.serial;"
    "strict serial comparison precedes monotonic publication")

extract_slice(
    _source
    "PendingCopyStatus TryBeginPendingCopyAdmission("
    "PendingCopyStatus TryAppendPendingCopyRecord("
    _begin_admission
    "checked pending-copy admission begin")
foreach(_marker IN ITEMS
    "ObjectsDisjoint(ledger, sizeof(*ledger), receipt, sizeof(*receipt))"
    "receipt, sizeof(*receipt), lifecycle, sizeof(*lifecycle)"
    "if (!receipt->isCanonical())"
    "if (IsTerminalReceiptPhase(receipt->phase_))")
    require_contains(
        _begin_admission "${_marker}" "pre-dereference object separation")
endforeach()
require_ordered(
    _begin_admission
    "ObjectsDisjoint(ledger, sizeof(*ledger), receipt, sizeof(*receipt))"
    "if (!receipt->isCanonical())"
    "object-span validation precedes pristine receipt authentication")
require_ordered(
    _source
    "if (IsTerminalReceiptPhase(receipt->phase_))"
    "PendingCopyLedger *const ledger = receipt->ledger_;"
    "terminal retry precedes ledger inspection")
require_ordered(
    _source
    "receipt->completionContext_ = completion.context;"
    "ledger->setPhase(PendingCopyLedger::Phase::AdmissionPrepared);"
    "prepare publishes the complete callback binding before its phase")

# Finalization has one replay guard and one exact callback. Admitting plus the
# callback-active witness makes every operation on retained, nonterminal
# authority Busy; a different terminal receipt remains ledger-independent.
# Completion identity clears before stable Admitted/Drained publication.
extract_slice(
    _source
    "void FinalizePreparedPendingCopyAdmission("
    "PendingCopyStatus TryDiscardPendingCopyAdmission("
    _finalizer
    "pending-copy admission finalizer")
foreach(_marker IN ITEMS
    "if (receipt.phase_ != PendingCopyAdmissionPhase::Prepared)"
    "receipt.setPhase(PendingCopyAdmissionPhase::Admitting);"
    "descriptor.phase = PendingCopyLedger::GenerationPhase::Admitted;"
    "ledger.callbackActive_ = 1;"
    "completion(completionContext);"
    "receipt.completionContext_ = nullptr;"
    "receipt.completion_ = nullptr;"
    "receipt.setPhase(PendingCopyAdmissionPhase::Admitted);"
    "ledger.callbackActive_ = 0;"
    "if (descriptor.recordCount == 0)"
    "receipt.setPhase(PendingCopyAdmissionPhase::Drained);")
    require_contains(_finalizer "${_marker}" "exactly-once finalization")
endforeach()
require_not_matches(
    _finalizer "(^|[^A-Za-z0-9_])(for|while)([^A-Za-z0-9_]|$)"
    "finalizer must remain constant-time")
require_ordered(
    _finalizer
    "receipt.setPhase(PendingCopyAdmissionPhase::Admitting);"
    "completion(completionContext);"
    "replay guard publishes before callback")
require_ordered(
    _finalizer
    "completion(completionContext);"
    "receipt.setPhase(PendingCopyAdmissionPhase::Admitted);"
    "callback completes before stable admission")
require_ordered(
    _finalizer
    "receipt.setPhase(PendingCopyAdmissionPhase::Admitted);"
    "if (descriptor.recordCount == 0)"
    "zero-record terminal follows admission")

extract_slice(
    _source
    "PendingCopyStatus TryResetPendingCopyAdmissionReceipt("
    "#ifdef KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING"
    _reset_receipt
    "terminal receipt reset")
foreach(_marker IN ITEMS
    "ValidateRequestedKey(receipt->key_, key);"
    "if (IsTerminalReceiptPhase(receipt->phase_))"
    "receipt->reset();"
    "if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)"
    "return PendingCopyStatus::Busy;"
    "PendingCopyLedger *const ledger = receipt->ledger_;"
    "if (!ledger || !ledger->isCanonical())"
    "if (ledger->callbackActive_ != 0)"
    "return PendingCopyStatus::InvalidPhase;")
    require_contains(
        _reset_receipt "${_marker}" "callback-safe terminal reset")
endforeach()
require_ordered(
    _reset_receipt
    "if (keyStatus != PendingCopyStatus::Success)"
    "if (IsTerminalReceiptPhase(receipt->phase_))"
    "stale-key validation precedes ledger-independent terminal reset")
require_ordered(
    _reset_receipt
    "receipt->reset();"
    "if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)"
    "terminal reset precedes nonterminal callback handling")
require_ordered(
    _reset_receipt
    "if (receipt->phase_ == PendingCopyAdmissionPhase::Admitting)"
    "PendingCopyLedger *const ledger = receipt->ledger_;"
    "Admitting completion reentry is ledger-independent")
require_ordered(
    _reset_receipt
    "if (!ledger || !ledger->isCanonical())"
    "if (ledger->callbackActive_ != 0)"
    "nonterminal authority authenticates before callback state")
require_ordered(
    _reset_receipt
    "if (ledger->callbackActive_ != 0)"
    "return PendingCopyStatus::InvalidPhase;"
    "drain callback reentry is Busy before phase rejection")

# Drain copies by value, preserves its cursor on Retry, advances only after a
# durable Success, rejects reentry, and permanently poisons unknown outcomes.
foreach(_marker IN ITEMS
    "const PendingCopyRecord record ="
    "callback.consume(callback.context, record);"
    "case PendingCopyDrainCallbackStatus::Success:"
    "++ledger->drainCursor_;"
    "case PendingCopyDrainCallbackStatus::Retry:"
    "return PendingCopyStatus::Retry;"
    "case PendingCopyDrainCallbackStatus::UnsafeFailure:"
    "default:"
    "ledger->poison();"
    "if (ledger->callbackActive_ != 0)"
    "return PendingCopyStatus::Busy;")
    require_contains(_source "${_marker}" "ordered callback drain")
endforeach()
require_contains(
    _header "PendingCopyRecord record) noexcept"
    "drain callback receives no pointer into ledger storage")

# Runtime fixtures exercise the exact capacity/order/compaction/reentry and
# poison boundaries, while the macro-off compile test proves the friend cannot
# be recreated by name in production.
foreach(_marker IN ITEMS
    "TestFinalizationExactlyOnceAndZeroRecordTerminal"
    "TestGenerationCapacityOrderedDrainAndRetry"
    "TestStableCompactionAndStaleTerminalAuthority"
    "TestUnknownDrainResultPoisonsLedger"
    "state.calls == pending::kPendingCopyRecordCapacity + 1"
    "state.resetStatus = pending::TryResetPendingCopyAdmissionReceipt("
    "CHECK(state.resetStatus == PendingCopyStatus::Busy);"
    "state.terminalResetStatus == PendingCopyStatus::Success"
    "state.receipt = &receipts[0];"
    "reinterpret_cast<PendingCopyAdmissionReceipt *>(&ledger)"
    "SetRecordCount("
    "SetGenerationCount("
    "SetDrainCursor("
    "SetLedgerPhaseWitness("
    "SetRecord("
    "SetDescriptorRecordCount("
    "SetDescriptorSerial("
    "SetReceiptGenerationIndex("
    "SetReceiptGenerationSerial("
    "SetReceiptPhaseWitness("
    "UINT64_MAX - 1"
    "PendingCopyStatus::GenerationCapacityExceeded"
    "PendingCopyStatus::StaleKey"
    "static_cast<PendingCopyDrainCallbackStatus>(UINT8_C(0xFF))")
    require_contains(_fixture "${_marker}" "runtime adversarial coverage")
endforeach()
foreach(_marker IN ITEMS
    "CanReachRecords"
    "CanReachGenerations"
    "CanMutateCallbackState"
    "CanReachLedger"
    "CanMutateGeneration"
    "CanReset"
    "!PendingCopyLedgerTestAccess::")
    require_contains(
        _production_seal "${_marker}" "macro-off private access seal")
endforeach()
require_not_contains(
    _production_seal "KISAK_DB_ZONE_PENDING_COPY_LEDGER_TESTING"
    "production seal must compile without test access")

# The primitive is shipped and tested everywhere, including the explicit
# Windows x86 engine build arm. The old raw global remains the sole production
# implementation until the later all-at-once caller cutover.
foreach(_marker IN ITEMS
    "db_zone_pending_copy_ledger.cpp"
    "db_zone_pending_copy_ledger.h")
    require_contains(_manifest "${_marker}" "engine source manifest")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-pending-copy-ledger-tests"
    "kisakcod-db-zone-pending-copy-ledger-production-seal-tests"
    "database-zone-pending-copy-ledger"
    "database-zone-pending-copy-production-test-access-sealed"
    "database-zone-pending-copy-source-invariants"
    "db_zone_pending_copy_ledger_source_test.cmake")
    require_contains(_tests "${_marker}" "portable CMake registration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-pending-copy-ledger-tests"
    "kisakcod-db-zone-pending-copy-ledger-production-seal-tests"
    "database-zone-pending-copy-(ledger|production-test-access-sealed|source-invariants)")
    require_contains(_ci "${_marker}" "explicit Windows x86 CI arm")
endforeach()
require_contains(
    _legacy_header "extern XAssetEntry *g_copyInfo[0x800];"
    "legacy declaration remains until atomic cutover")
require_contains(
    _legacy_header "extern uint32_t g_copyInfoCount;"
    "legacy count declaration remains until atomic cutover")
require_contains(
    _legacy_source "XAssetEntry *g_copyInfo[0x800];"
    "legacy storage remains until atomic cutover")
require_contains(
    _legacy_source "uint32_t g_copyInfoCount;"
    "legacy count remains until atomic cutover")

message(STATUS "Pending-copy ledger source invariants verified")

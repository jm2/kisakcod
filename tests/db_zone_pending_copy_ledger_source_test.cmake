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

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_production_seal_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}"
    "${_legacy_header_path}"
    "${_legacy_source_path}")
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

# Any public symbol is an enrollment oracle. Scanning tokens, rather than just
# includes or qualified calls, catches manual declarations, using-declarations,
# namespace aliases, function pointers, and whitespace-obfuscated references.
set(_enrollment_tokens
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

function(detect_production_enrollment TEXT OUT_VAR)
    set(_found FALSE)
    string(FIND "${TEXT}" "db_zone_pending_copy_ledger.h" _header_ref)
    if(NOT _header_ref EQUAL -1)
        set(_found TRUE)
    endif()
    foreach(_token IN LISTS _enrollment_tokens)
        string(REGEX MATCH
            "(^|[^A-Za-z0-9_])${_token}([^A-Za-z0-9_]|$)"
            _token_match "${TEXT}")
        if(NOT _token_match STREQUAL "")
            set(_found TRUE)
        endif()
    endforeach()
    set(${OUT_VAR} ${_found} PARENT_SCOPE)
endfunction()

function(require_detector_fixture TEXT DESCRIPTION)
    detect_production_enrollment("${TEXT}" _detected)
    if(NOT _detected)
        message(FATAL_ERROR
            "Pending-copy enrollment detector missed ${DESCRIPTION}")
    endif()
endfunction()

require_detector_fixture(
    "#include <database/db_zone_pending_copy_ledger.h>"
    "a direct include")
require_detector_fixture(
    "using db :: zone_pending_copy :: TryBeginPendingCopyAdmission"
    "a whitespace-obfuscated using-declaration")
require_detector_fixture(
    "auto callback = &TryDrainNextPendingCopy"
    "an unqualified function pointer")
require_detector_fixture(
    "namespace db { namespace zone_pending_copy { void Hidden() } }"
    "a manual namespace declaration")
require_detector_fixture(
    "namespace pending = db::zone_pending_copy"
    "a namespace alias")
detect_production_enrollment(
    "struct PendingCopySomethingElse { int unrelated; }" _false_positive)
if(_false_positive)
    message(FATAL_ERROR
        "Pending-copy enrollment detector lost identifier boundaries")
endif()

file(GLOB_RECURSE _production_sources
    "${SOURCE_ROOT}/src/*.c"
    "${SOURCE_ROOT}/src/*.cc"
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h"
    "${SOURCE_ROOT}/src/*.hpp")
foreach(_production_path IN LISTS _production_sources)
    if(_production_path STREQUAL _header_path
        OR _production_path STREQUAL _source_path)
        continue()
    endif()
    file(READ "${_production_path}" _production_text)
    detect_production_enrollment("${_production_text}" _enrolled)
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
    "if (IsTerminalReceiptPhase(receipt->phase_))"
    "PendingCopyLedger *const ledger = receipt->ledger_;"
    "terminal retry precedes ledger inspection")
require_ordered(
    _source
    "receipt->completionContext_ = completion.context;"
    "ledger->setPhase(PendingCopyLedger::Phase::AdmissionPrepared);"
    "prepare publishes the complete callback binding before its phase")

# Finalization has one replay guard and one exact callback. Admitting plus the
# callback-active witness makes every reentrant status-bearing operation Busy;
# completion identity is cleared before stable Admitted/Drained publication.
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

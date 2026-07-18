cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_script_string_ownership.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_script_string_ownership.cpp")
set(_adapter_header_path
    "${SOURCE_ROOT}/src/database/db_script_string_adapter.h")
set(_adapter_source_path
    "${SOURCE_ROOT}/src/database/db_script_string_adapter.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_script_string_ownership_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_stream_path "${SOURCE_ROOT}/src/database/db_stream_load.cpp")
set(_stringtable_path
    "${SOURCE_ROOT}/src/database/db_stringtable_load.cpp")
set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_file_load_path "${SOURCE_ROOT}/src/database/db_file_load.cpp")
set(_load_path "${SOURCE_ROOT}/src/database/db_load.cpp")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_adapter_header_path}"
    "${_adapter_source_path}"
    "${_fixture_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}"
    "${_stream_path}"
    "${_stringtable_path}"
    "${_registry_path}"
    "${_file_load_path}"
    "${_load_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR
            "Missing zone script-string ownership source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_adapter_header_path}" _adapter_header)
file(READ "${_adapter_source_path}" _adapter_source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)
file(READ "${_stream_path}" _stream)
file(READ "${_stringtable_path}" _stringtable)
file(READ "${_registry_path}" _registry)
file(READ "${_file_load_path}" _file_load)
file(READ "${_load_path}" _load)

foreach(_var IN ITEMS
    _header
    _source
    _adapter_header
    _adapter_source
    _fixture
    _manifest
    _tests
    _ci
    _stream
    _stringtable
    _registry
    _file_load
    _load)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing zone script-string ownership invariant "
            "(${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden zone script-string ownership regression "
            "(${DESCRIPTION}): '${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered zone script-string ownership invariant "
            "(${DESCRIPTION})")
    endif()
endfunction()

function(require_literal_count SOURCE_VAR NEEDLE EXPECTED DESCRIPTION)
    set(_remaining "${${SOURCE_VAR}}")
    set(_count 0)
    string(LENGTH "${NEEDLE}" _needle_length)
    if(_needle_length EQUAL 0)
        message(FATAL_ERROR "Empty literal for ${DESCRIPTION}")
    endif()
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
            "Wrong zone script-string ownership invariant count "
            "(${DESCRIPTION}): expected ${EXPECTED}, found ${_count} for "
            "'${NEEDLE}'")
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

# The controller is a constructed, report-free boundary. It owns exactly one
# lifecycle key, journal backing, and private global-serializer token. Future
# loader wiring must acquire the serializer before the registry lock.
foreach(_marker IN ITEMS
    "Constructed, production-neutral ownership boundary"
    "script-string transaction -> db_hashCritSect -> script string -> memory tree"
    "The controller never acquires db_hashCritSect"
    "Callbacks must not throw, longjmp, call Com_Error"
    "script_string_transaction::ScriptStringTransactionToken transaction_{};"
    "zone_load::ZoneLoadContextKey key_{};"
    "script_string_journal::ScriptStringJournal *journal_ = nullptr;"
    "script_string_journal::ScriptStringJournalEntry *storage_ = nullptr;"
    "validateAbandonedReceipt() const noexcept;"
    "RUNTIME_SIZE(ZoneScriptStringOwnershipController, 0x40, 0x58);"
    "UnpublishingCallback,"
    "Busy,")
    require_contains(_header "${_marker}" "bound fail-closed controller")
endforeach()
foreach(_forbidden IN ITEMS
    "Com_Error("
    "db_hashCritSect"
    "SL_"
    "PMem_"
    "DB_AddXAsset"
    "DB_SetXAssetName"
    "g_copyInfo")
    require_not_contains(_source "${_forbidden}" "production-neutral source")
endforeach()

extract_slice(
    _source
    "ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringOwnership("
    "ZoneScriptStringOwnershipStatus TryStageZoneScriptString("
    _begin
    "controller begin")
require_ordered(
    _begin
    "TryBeginScriptStringTransaction("
    "TryInitializeScriptStringJournal("
    "serializer before journal initialization")

extract_slice(
    _source
    "ZoneScriptStringOwnershipStatus TryCommitZoneScriptStringsAndAdmit("
    "ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringRollback("
    _commit
    "commit and admission")
require_ordered(
    _commit
    "TryCommitZoneLoadContext("
    "FinalizeScriptStringJournalCommit("
    "lifecycle publication before unconditional journal finalization")
require_ordered(
    _commit
    "FinalizeScriptStringJournalCommit("
    "admission.admitLive("
    "journal finalization before no-fail admission")
require_ordered(
    _commit
    "admission.admitLive("
    "FinishScriptStringTransaction("
    "admission before serializer release")

extract_slice(
    _source
    "ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringRollback("
    "ZoneScriptStringOwnershipStatus TryRollbackNextZoneScriptString("
    _rollback_begin
    "rollback begin")
require_ordered(
    _rollback_begin
    "controller->ensureUnreachable_("
    "TryBeginZoneLoadContextAbandonment("
    "unpublish before lifecycle abandonment")
require_ordered(
    _rollback_begin
    "TryBeginZoneLoadContextAbandonment("
    "TryBeginScriptStringRollback("
    "lifecycle abandonment before ownership rollback")
require_contains(
    _rollback_begin
    "ZoneScriptStringOwnershipPhase::UnpublishingCallback"
    "callback reentry phase")
require_contains(
    _rollback_begin
    "ZoneScriptStringOwnershipPhase::Unpublishing; return ZoneScriptStringOwnershipStatus::Retry;"
    "retry restores an externally callable phase")

extract_slice(
    _source
    "ZoneScriptStringOwnershipStatus TryFinishZoneScriptStringAbandonment("
    "ZoneScriptStringOwnershipStatus TryResetAbandonedZoneScriptStringOwnership("
    _rollback_finish
    "rollback cleanup")
require_ordered(
    _rollback_finish
    "TryFinishZoneLoadContextAbandonment("
    "FinishScriptStringTransaction("
    "full cleanup before serializer release")
require_contains(
    _source
    "controller->detachJournalBacking(); controller->phase_ = ZoneScriptStringOwnershipPhase::OwnershipRolledBack;"
    "journal backing detached before full cleanup")

extract_slice(
    _source
    "validateAbandonedReceipt() const noexcept"
    "void ZoneScriptStringOwnershipController::detachJournalBacking() noexcept"
    _abandoned_receipt
    "abandoned receipt validation")
foreach(_marker IN ITEMS
    "phase_ != ZoneScriptStringOwnershipPhase::Abandoned"
    "journal_ != nullptr || storage_ != nullptr"
    "rollbackContext_ != nullptr || ensureUnreachable_ != nullptr"
    "performCleanup_ != nullptr || storageCapacity_ != 0"
    "expectedCount_ != 0 || transactionSerial_ != 0"
    "!transaction_.canonicalInactive()"
    "resumePhase_ != ZoneScriptStringOwnershipPhase::Empty"
    "reserved_[0] != 0 || reserved_[1] != 0"
    "!lifecycle_ || !lifecycle_->initialized()"
    "lifecycle_->generation() != key_.generation"
    "ZoneScriptStringOwnershipStatus::StaleKey"
    "lifecycle::ZoneLoadContextPhase::Empty"
    "lifecycle::ZoneLoadTerminalKind::Abandoned")
    require_contains(
        _abandoned_receipt "${_marker}" "complete abandoned receipt")
endforeach()
require_literal_count(
    _source
    "controller->validateAbandonedReceipt();"
    2
    "terminal retry and reset share receipt authentication")

# Staging may expose an acquired ID only after the journal has appended the
# reference record successfully. All failed controller and adapter operations
# preserve the caller's output.
foreach(_marker IN ITEMS
    "caller's output is unchanged on every non-Success result."
    "std::uint32_t *outStringId = nullptr) noexcept;")
    require_contains(_adapter_header "${_marker}" "safe staged-ID publication")
endforeach()
require_contains(
    _adapter_source
    "std::uint32_t acquiredStringId = 0;"
    "private acquired-ID staging")
extract_slice(
    _adapter_source
    "TryStageScriptStringFromSource("
    "TryTransferNextScriptStringToDatabaseUser("
    _adapter_stage
    "adapter staging")
require_ordered(
    _adapter_stage
    "TryStageScriptString("
    "status == script_string_journal::ScriptStringJournalStatus::Success"
    "journal append before success check")
require_ordered(
    _adapter_stage
    "status == script_string_journal::ScriptStringJournalStatus::Success"
    "*outStringId = callbackContext.acquiredStringId;"
    "success check before output publication")

# Keep this batch production-neutral: these are the seven known legacy raw
# mutation sites. Their exact count is frozen until a later, separately
# reviewed enrollment replaces each one with the controller/adapter path.
require_literal_count(
    _stream "SL_GetStringOfSize(" 2 "two temporary-string claims")
require_literal_count(
    _stringtable "SL_AddUser(" 1 "one direct database-user claim")
require_literal_count(
    _registry "SL_GetString(" 2 "two dynamic default-name claims")
require_literal_count(
    _registry "SL_TransferSystem(" 1 "one global ownership transfer")
require_literal_count(
    _registry "SL_ShutdownSystem(" 1 "one global ownership sweep")
foreach(_var IN ITEMS _stream _stringtable _registry _file_load _load)
    require_not_contains(
        ${_var}
        "ZoneScriptStringOwnership"
        "legacy production site cannot be partially enrolled")
    require_not_contains(
        ${_var}
        "db_zone_script_string_ownership"
        "legacy production site cannot include the controller")
endforeach()

# The production manifest, portable test graph, x86 test selection, and the
# callback-reentry/foreign/stale/swapped/partial rollback fixture stay wired.
foreach(_marker IN ITEMS
    "database/db_zone_script_string_ownership.cpp"
    "database/db_zone_script_string_ownership.h")
    require_contains(_manifest "${_marker}" "production source manifest")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-script-string-ownership-tests"
    "KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1"
    "KISAK_PLATFORM_SERVICE_SOURCES"
    "database-zone-script-string-ownership-controller"
    "database-zone-script-string-ownership-source-invariants")
    require_contains(_tests "${_marker}" "portable CMake integration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-script-string-ownership-tests"
    "database-zone-script-string-ownership-(controller|source-invariants)")
    require_contains(_ci "${_marker}" "Windows x86 CI selection")
endforeach()
foreach(_marker IN ITEMS
    "TestCommittedAdmission();"
    "TestPartialRollbackAndCleanupRetry();"
    "TestBindingAndForeignThreadRejection();"
    "TestBeginFailureReleasesSerializer();"
    "TestAbandonedReceiptAuthentication();"
    "driver.beginReentryStatus == ZoneScriptStringOwnershipStatus::Busy"
    "foreignOutput == 0x12345678u"
    "nextKey.generation != originalKey.generation"
    "== ZoneScriptStringOwnershipStatus::StaleKey")
    require_contains(_fixture "${_marker}" "controller regression coverage")
endforeach()

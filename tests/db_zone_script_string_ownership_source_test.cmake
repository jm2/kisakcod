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
set(_stable_integration_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_stable_context_integration_tests.cpp")
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
    "${_stable_integration_fixture_path}"
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
file(READ "${_stable_integration_fixture_path}" _stable_integration_fixture)
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
    _stable_integration_fixture
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
    "const script_string_journal::ScriptStringJournal *placementJournal_ = nullptr;"
    "const script_string_journal::ScriptStringJournalEntry *placementStorage_ = nullptr;"
    "ZoneScriptStringStorageBindingPhase"
    "AuthenticateZoneScriptStringOwnershipStorage("
    "validateAbandonedReceipt() const noexcept;"
    "canonicalForBinding("
    "RUNTIME_SIZE(ZoneScriptStringOwnershipController, 0x50, 0x70);"
    "UnpublishingCallback,"
    "Unloading,"
    "UnloadingCallback,"
    "Unloaded,"
    "enum class RegistryCallbackPurpose : std::uint8_t"
    "friend class db::registry_ownership::RegistryOwnershipCoordinator;"
    "friend class db::zone_runtime::ZoneRuntimeTable;"
    "tryBeginRegistryCallbackWindow("
    "trySnapshotRegistryCallbackTransaction("
    "authenticatesRegistryCallbackTransaction("
    "std::uint8_t callbackWindowWitness_ = 0;"
    "std::uint8_t callbackWindowWitnessMirror_ = 0;"
    "TryUnloadLiveZoneScriptStringOwnership("
    "TryResetTerminalZoneScriptStringOwnership("
    "Busy,")
    require_contains(_header "${_marker}" "bound fail-closed controller")
endforeach()

# Ordinary registry borrows deliberately remain unavailable during callbacks.
# The private coordinator-only path snapshots the exact callback purpose,
# retained transaction serial, and mirrored nonzero callback-window witness
# only after authenticating the current key, complete binding, and owning
# thread. Reauthentication repeats those checks, so an admitted coordinator
# cannot cross either purpose transitions or later same-purpose invocations.
extract_slice(
    _source
    "bool ZoneScriptStringOwnershipController::trySnapshotRegistryTransaction("
    "bool ZoneScriptStringOwnershipController::trySnapshotRegistryCallbackTransaction("
    _ordinary_registry_borrow
    "ordinary retained registry transaction borrow")
require_contains(
    _ordinary_registry_borrow
    "validateOwned() != ZoneScriptStringOwnershipStatus::Success"
    "ordinary callback rejection remains routed through validateOwned")

extract_slice(
    _source
    "bool ZoneScriptStringOwnershipController::tryBeginRegistryCallbackWindow("
    "bool ZoneScriptStringOwnershipController::trySnapshotRegistryCallbackTransaction("
    _callback_window_begin
    "mirrored callback-window advance")
foreach(_marker IN ITEMS
    "!transaction::OwnsScriptStringTransaction(transaction_)"
    "!IsCallbackPhase(expectedCallbackPhase)"
    "phase_ != expectedCallbackPhase"
    "callbackWindowWitness_ != callbackWindowWitnessMirror_"
    "callbackWindowWitness_ == UINT8_MAX"
    "!bindingMatchesCurrentPhase()"
    "poison();"
    "callbackWindowWitness_ + 1"
    "if (nextWitness == 0)"
    "callbackWindowWitness_ = nextWitness;"
    "callbackWindowWitnessMirror_ = nextWitness;")
    require_contains(
        _callback_window_begin
        "${_marker}"
        "non-wrapping mirrored callback-window witness")
endforeach()
require_ordered(
    _callback_window_begin
    "!transaction::OwnsScriptStringTransaction(transaction_)"
    "phase_ != expectedCallbackPhase"
    "current-thread ownership before callback-state reads")
require_ordered(
    _callback_window_begin
    "callbackWindowWitness_ == UINT8_MAX"
    "callbackWindowWitness_ + 1"
    "exhaustion rejection before increment")
require_ordered(
    _callback_window_begin
    "callbackWindowWitness_ = nextWitness;"
    "callbackWindowWitnessMirror_ = nextWitness;"
    "mirrored callback-window publication")

extract_slice(
    _source
    "bool ZoneScriptStringOwnershipController::trySnapshotRegistryCallbackTransaction("
    "lifecycle::ZoneLoadCleanupCallbackStatus ZoneScriptStringOwnershipController::PerformBoundCleanup("
    _callback_registry_borrow
    "private callback registry transaction borrow")
foreach(_marker IN ITEMS
    "RegistryCallbackPurpose::None"
    "ZoneScriptStringOwnershipPhase::UnpublishingCallback"
    "RegistryCallbackPurpose::Unpublishing"
    "ZoneScriptStringOwnershipPhase::Cleaning"
    "RegistryCallbackPurpose::Cleaning"
    "ZoneScriptStringOwnershipPhase::Admitting"
    "RegistryCallbackPurpose::Admitting"
    "ZoneScriptStringOwnershipPhase::UnloadingCallback"
    "RegistryCallbackPurpose::Unloading"
    "default: return false;"
    "!bindingMatchesCurrentPhase()"
    "transactionSerial_ == 0"
    "transaction_.serial() != transactionSerial_"
    "!transaction::OwnsScriptStringTransaction(transaction_)"
    "!outWindowWitness"
    "callbackWindowWitness_ == 0"
    "callbackWindowWitness_ != callbackWindowWitnessMirror_"
    "*outSerial = serial;"
    "*outPurpose = purpose;"
    "*outWindowWitness = windowWitness;"
    "expectedPurpose == RegistryCallbackPurpose::None"
    "expectedWindowWitness == 0"
    "trySnapshotRegistryCallbackTransaction( expectedKey, &serial, &purpose, &windowWitness)"
    "serial == expectedSerial && purpose == expectedPurpose"
    "windowWitness == expectedWindowWitness")
    require_contains(
        _callback_registry_borrow
        "${_marker}"
        "exact callback purpose and transaction authentication")
endforeach()
require_ordered(
    _callback_registry_borrow
    "!transaction::OwnsScriptStringTransaction(transaction_)"
    "if (key_ != expectedKey)"
    "current-thread ownership before mutable binding reads")
require_ordered(
    _callback_registry_borrow
    "if (key_ != expectedKey)"
    "switch (phase_)"
    "exact key before callback-purpose inference")
require_ordered(
    _callback_registry_borrow
    "switch (phase_)"
    "!bindingMatchesCurrentPhase()"
    "purpose selection before canonical binding authentication")
require_ordered(
    _callback_registry_borrow
    "!transaction::OwnsScriptStringTransaction(transaction_)"
    "*outSerial = serial;"
    "current-thread ownership before serial publication")
require_ordered(
    _callback_registry_borrow
    "*outSerial = serial;"
    "*outPurpose = purpose;"
    "staged callback snapshot publication")
require_ordered(
    _callback_registry_borrow
    "*outPurpose = purpose;"
    "*outWindowWitness = windowWitness;"
    "window witness published last")
require_not_contains(
    _callback_registry_borrow
    "validateOwned()"
    "callback borrow cannot accidentally route through ordinary rejection")

extract_slice(
    _source
    "bool AuthenticateZoneScriptStringOwnershipStorage("
    "bool ZoneScriptStringOwnershipController::canonicalForBinding("
    _storage_auth
    "durable placement identity authentication")
foreach(_marker IN ITEMS
    "controller.canonicalForBinding(expectedLifecycle, expectedKey)"
    "controller.placementJournal_ != expectedJournal"
    "controller.placementStorage_ != expectedStorage"
    "controller.placementCapacity_ != expectedCapacity"
    "controller.placementExpectedCount_ != expectedCount"
    "ZoneScriptStringStorageBindingPhase::Attached"
    "ZoneScriptStringStorageBindingPhase::Detached")
    require_contains(
        _storage_auth "${_marker}" "exact placement identity tuple")
endforeach()

extract_slice(
    _source
    "void ZoneScriptStringOwnershipController::detachJournalBacking() noexcept"
    "void ZoneScriptStringOwnershipController::poison() noexcept"
    _detach
    "active journal detachment")
foreach(_forbidden IN ITEMS
    "placementJournal_"
    "placementStorage_"
    "placementCapacity_"
    "placementExpectedCount_")
    require_not_contains(
        _detach "${_forbidden}" "detachment retains immutable placement identity")
endforeach()

extract_slice(
    _source
    "void ZoneScriptStringOwnershipController::reset() noexcept"
    "ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringOwnership("
    _reset
    "placement identity reset")
foreach(_marker IN ITEMS
    "placementJournal_ = nullptr;"
    "placementStorage_ = nullptr;"
    "placementCapacity_ = 0;"
    "placementExpectedCount_ = 0;"
    "callbackWindowWitness_ = 0;"
    "callbackWindowWitnessMirror_ = 0;")
    require_contains(
        _reset "${_marker}" "terminal reset clears placement identity")
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
    "tryBeginRegistryCallbackWindow("
    "callback window begins after irreversible journal finalization")
require_ordered(
    _commit
    "tryBeginRegistryCallbackWindow("
    "const lifecycle::ZoneLoadContextKey callbackKey ="
    "fresh window before immutable callback identity snapshot")
require_ordered(
    _commit
    "const std::uint32_t callbackTransactionSerial ="
    "admission.admitLive("
    "immutable callback identity before admission")
require_ordered(
    _commit
    "admission.admitLive("
    "trySnapshotRegistryCallbackTransaction("
    "admission return before exact post-authentication")
require_ordered(
    _commit
    "trySnapshotRegistryCallbackTransaction("
    "callbackWindowWitness_ = 0;"
    "post-authentication before terminal witness reset")
foreach(_marker IN ITEMS
    "controller->trySnapshotRegistryCallbackTransaction( callbackKey,"
    "observedTransactionSerial != callbackTransactionSerial")
    require_contains(
        _commit
        "${_marker}"
        "admission compares immutable pre-callback identity")
endforeach()
require_ordered(
    _commit
    "callbackWindowWitnessMirror_ = 0;"
    "ZoneScriptStringOwnershipPhase::Live;"
    "mirrored witness reset before Live publication")
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
require_ordered(
    _rollback_begin
    "tryBeginRegistryCallbackWindow("
    "const lifecycle::ZoneLoadContextKey callbackKey ="
    "fresh unpublication window before identity snapshot")
require_ordered(
    _rollback_begin
    "const std::uint32_t callbackTransactionSerial ="
    "controller->ensureUnreachable_("
    "immutable identity before initial unpublication callback")
require_contains(
    _rollback_begin
    "authenticatesRegistryCallbackTransaction( callbackKey, callbackTransactionSerial,"
    "initial unpublication authenticates pre-callback identity")
require_ordered(
    _rollback_begin
    "controller->ensureUnreachable_("
    "authenticatesRegistryCallbackTransaction("
    "unpublication return before exact post-authentication")
extract_slice(
    _rollback_begin
    "const ZoneScriptStringUnpublishStatus callbackStatus ="
    "const lifecycle::ZoneLoadContextStatus lifecycleStatus ="
    _unpublish_callback_return
    "initial unpublication callback return")
require_ordered(
    _unpublish_callback_return
    "authenticatesRegistryCallbackTransaction("
    "ZoneScriptStringOwnershipPhase::Unpublishing;"
    "post-authentication before callback-phase exit")
require_contains(
    _rollback_begin
    "ZoneScriptStringOwnershipPhase::Unpublishing; return ZoneScriptStringOwnershipStatus::Retry;"
    "retry restores an externally callable phase")

extract_slice(
    _source
    "ZoneScriptStringOwnershipController::PerformBoundCleanup("
    "bool ZoneScriptStringOwnershipController::placementIdentityIsEmpty()"
    _bound_cleanup_callbacks
    "bound cleanup and unload callbacks")
require_literal_count(
    _bound_cleanup_callbacks
    "tryBeginRegistryCallbackWindow("
    3
    "one fresh window for each bound callback dispatch path")
require_literal_count(
    _bound_cleanup_callbacks
    "authenticatesRegistryCallbackTransaction("
    3
    "one exact post-authentication for each bound callback dispatch path")
require_literal_count(
    _bound_cleanup_callbacks
    "const lifecycle::ZoneLoadContextKey callbackKey ="
    3
    "one immutable key snapshot for each bound callback dispatch path")
require_literal_count(
    _bound_cleanup_callbacks
    "const std::uint32_t callbackTransactionSerial ="
    3
    "one immutable serial snapshot for each bound callback dispatch path")
require_literal_count(
    _bound_cleanup_callbacks
    "authenticatesRegistryCallbackTransaction( callbackKey, callbackTransactionSerial,"
    3
    "bound callbacks authenticate immutable pre-callback identity")
foreach(_marker IN ITEMS
    "RegistryCallbackPurpose::Cleaning"
    "RegistryCallbackPurpose::Unloading"
    "controller->poison();"
    "ZoneLoadCleanupCallbackStatus::UnsafeFailure")
    require_contains(
        _bound_cleanup_callbacks
        "${_marker}"
        "fail-closed bound callback postcondition")
endforeach()

require_literal_count(
    _source
    "tryBeginRegistryCallbackWindow("
    6
    "definition plus five controller-owned callback dispatch paths")

extract_slice(
    _source
    "ZoneScriptStringOwnershipStatus TryFinishZoneScriptStringAbandonment("
    "ZoneScriptStringOwnershipStatus TryUnloadLiveZoneScriptStringOwnership("
    _rollback_finish
    "rollback cleanup")
require_ordered(
    _rollback_finish
    "TryFinishZoneLoadContextAbandonment("
    "controller->callbackWindowWitness_ = 0;"
    "cleanup completion before callback-witness reset")
require_ordered(
    _rollback_finish
    "controller->callbackWindowWitnessMirror_ = 0;"
    "ZoneScriptStringOwnershipPhase::Abandoned;"
    "mirrored witness reset before abandoned receipt")
require_ordered(
    _rollback_finish
    "ZoneScriptStringOwnershipPhase::Abandoned;"
    "FinishScriptStringTransaction("
    "full cleanup before serializer release")
require_contains(
    _source
    "controller->detachJournalBacking(); controller->phase_ = ZoneScriptStringOwnershipPhase::OwnershipRolledBack;"
    "journal backing detached before full cleanup")

extract_slice(
    _source
    "validateLiveBinding("
    "void ZoneScriptStringOwnershipController::detachJournalBacking() noexcept"
    _terminal_receipts
    "live and terminal receipt validation")
foreach(_marker IN ITEMS
    "phase_ != ZoneScriptStringOwnershipPhase::Live"
    "expectedPhase = ZoneScriptStringOwnershipPhase::Abandoned;"
    "expectedPhase = ZoneScriptStringOwnershipPhase::Unloaded;"
    "journal_ != nullptr || storage_ != nullptr"
    "rollbackContext_ != nullptr || ensureUnreachable_ != nullptr"
    "performCleanup_ != nullptr || storageCapacity_ != 0"
    "expectedCount_ != 0 || transactionSerial_ != 0"
    "!transaction_.canonicalInactive()"
    "resumePhase_ != ZoneScriptStringOwnershipPhase::Empty"
    "callbackWindowWitness_ != 0"
    "callbackWindowWitnessMirror_ != 0"
    "lifecycle_ != expectedLifecycle"
    "!expectedLifecycle->canonical()"
    "expectedLifecycle->cleanupActive()"
    "expectedLifecycle->cleanupPoisoned()"
    "expectedLifecycle->generation() != expectedKey.generation"
    "ZoneScriptStringOwnershipStatus::StaleKey"
    "lifecycle::ZoneLoadContextPhase::Empty"
    "expectedLifecycle->terminalKind() != expectedTerminalKind")
    require_contains(
        _terminal_receipts "${_marker}" "complete terminal receipts")
endforeach()

extract_slice(
    _source
    "ZoneScriptStringOwnershipStatus TryUnloadLiveZoneScriptStringOwnership("
    "ZoneScriptStringOwnershipStatus TryResetTerminalZoneScriptStringOwnership("
    _live_unload
    "controller-owned live unload")
foreach(_marker IN ITEMS
    "validateLiveBinding(expectedLifecycle, expectedKey)"
    "TryBeginScriptStringTransaction("
    "controller->rollbackContext_ = callbacks.context;"
    "controller->performCleanup_ = callbacks.perform;"
    "ZoneScriptStringOwnershipPhase::UnloadingCallback"
    "TryUnloadZoneLoadContext("
    "ZoneScriptStringOwnershipPhase::Unloading;"
    "ZoneScriptStringOwnershipStatus::Retry"
    "controller->rollbackContext_ != callbacks.context"
    "controller->performCleanup_ != callbacks.perform"
    "controller->callbackWindowWitness_ = 0;"
    "controller->callbackWindowWitnessMirror_ = 0;"
    "ZoneScriptStringOwnershipPhase::Unloaded"
    "FinishScriptStringTransaction("
    "ZoneLoadTerminalKind::Unloaded")
    require_contains(
        _live_unload "${_marker}" "serialized retry-safe live unload")
endforeach()
require_ordered(
    _live_unload
    "TryBeginScriptStringTransaction("
    "TryUnloadZoneLoadContext("
    "outer serializer before lifecycle unload")
require_ordered(
    _live_unload
    "TryUnloadZoneLoadContext("
    "controller->callbackWindowWitness_ = 0;"
    "unload recipe before callback-witness reset")
require_ordered(
    _live_unload
    "controller->callbackWindowWitnessMirror_ = 0;"
    "ZoneScriptStringOwnershipPhase::Unloaded;"
    "mirrored witness reset before unloaded receipt")
require_ordered(
    _live_unload
    "ZoneScriptStringOwnershipPhase::Unloaded;"
    "FinishScriptStringTransaction("
    "unloaded receipt before serializer release")

extract_slice(
    _source
    "ZoneScriptStringOwnershipStatus TryResetTerminalZoneScriptStringOwnership("
    "} // namespace db::zone_script_string_ownership"
    _terminal_reset
    "exact terminal reset")
foreach(_marker IN ITEMS
    "expectedLifecycle->slotIndex() != expectedKey.slot"
    "expectedLifecycle->generation() != expectedKey.generation"
    "expectedLifecycle->phase() == lifecycle::ZoneLoadContextPhase::Empty"
    "expectedLifecycle->terminalKind() == expectedTerminalKind"
    "controller->validateTerminalReceipt("
    "controller->reset();"
    "controller->isEmptyCanonical()")
    require_contains(
        _terminal_reset "${_marker}" "exact idempotent terminal reset")
endforeach()

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
    require_not_contains(
        ${_var}
        "TryUnloadLiveZoneScriptStringOwnership("
        "legacy production site cannot invoke live unload")
    require_not_contains(
        ${_var}
        "TryResetTerminalZoneScriptStringOwnership("
        "legacy production site cannot reset terminal ownership")
endforeach()

# Exercise this controller through the literal macro-off facade/table callback
# chain, with the real coordinator and file-local script-string registry. The
# first callback borrow is blocked by the real DB hash lock, then the same
# stable context retries, mutates the registry, and explicitly retires the
# coordinator. A separate process mode deliberately omits that retirement and
# proves that the enclosing controller/table/facade chain fails closed.
extract_slice(
    _stable_integration_fixture
    "ZoneScriptStringUnpublishStatus EnsureGenerationUnreachable("
    "ZoneLoadCleanupCallbackStatus PerformExternalCleanup("
    _stable_ownership_callback
    "stable production ownership callback")
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
        _stable_ownership_callback "${_marker}"
        "stable controller/coordinator callback flow")
endforeach()
require_ordered(
    _stable_ownership_callback
    "g_callbackProbe.firstBorrow"
    "g_callbackProbe.retryBorrow"
    "busy ownership callback before the successful retry")
require_ordered(
    _stable_ownership_callback
    "g_callbackProbe.retryBorrow"
    "ZoneRuntimeFacade::TryAddDatabaseUser4("
    "authenticated callback retry before real registry mutation")
require_ordered(
    _stable_ownership_callback
    "ZoneRuntimeFacade::TryAddDatabaseUser4("
    "ZoneRuntimeFacade::FinishRegistryOwnership();"
    "registry mutation before coordinator retirement")

extract_slice(
    _stable_integration_fixture
    "[[nodiscard]] bool TestStableCallbackRetryChain() noexcept"
    "[[nodiscard]] bool TestOmittedCallbackFinishFailsClosed() noexcept"
    _stable_ownership_retry_run
    "stable controller contention/retry run")
foreach(_marker IN ITEMS
    "#include <script/scr_stringlist.cpp>"
    "FastCriticalSection db_hashCritSect{};"
    "ZoneRuntimeFacade::TryBeginScriptStringOwnership("
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
    "g_callbackProbe.finishStatus"
    "ZoneRuntimeFacade::TryResetTerminalReceipt("
    "terminal callback context remained live"
    "terminal callback context accepted a stale key")
    require_contains(
        _stable_integration_fixture "${_marker}"
        "literal stable controller/registry integration fixture")
endforeach()
require_ordered(
    _stable_ownership_retry_run
    "Sys_LockWrite(&db_hashCritSect);"
    "ZoneRuntimeFacade::TryBeginGenerationAbandonment("
    "real hash contention established before callback entry")
require_ordered(
    _stable_ownership_retry_run
    "ZoneRuntimeFacade::TryBeginGenerationAbandonment("
    "Sys_UnlockWrite(&db_hashCritSect);"
    "busy callback returns before hash-lock release")
require_ordered(
    _stable_ownership_retry_run
    "Sys_UnlockWrite(&db_hashCritSect);"
    "ZoneRuntimeFacade::TryContinueGenerationAbandonment("
    "uncontended ownership retry follows hash-lock release")

extract_slice(
    _stable_integration_fixture
    "[[nodiscard]] bool TestOmittedCallbackFinishFailsClosed() noexcept"
    "} // namespace"
    _stable_ownership_omit_finish_run
    "forgotten callback coordinator finish run")
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
        _stable_ownership_omit_finish_run "${_marker}"
        "forgotten finish leaves the ownership chain non-retirable")
endforeach()
require_contains(
    _stable_integration_fixture
    "std::strcmp(argv[1], \"--omit-finish\") == 0"
    "forgotten finish is isolated in its own process mode")

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
        "stable fixture cannot replace or test-enable ownership code")
endforeach()

extract_slice(
    _tests
    "add_executable(kisakcod-db-zone-runtime-stable-context-integration-tests"
    "add_executable(kisakcod-fx-archive-disk32-tests"
    _stable_ownership_integration_registration
    "stable production ownership integration registration")
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
    "PRIVATE \"LINKER:/STACK:8388608\""
    "NAME database-zone-runtime-stable-context-integration"
    "NAME database-zone-runtime-stable-context-forgotten-finish"
    "--omit-finish"
    "PROPERTIES TIMEOUT 30")
    require_contains(
        _stable_ownership_integration_registration "${_marker}"
        "complete macro-off ownership dependency/CTest enrollment")
endforeach()
foreach(_forbidden IN ITEMS
    "TESTING"
    "KISAK_PLATFORM_SERVICE_SOURCES"
    "winmm"
    "WILL_FAIL"
    "EXCLUDE_FROM_ALL")
    require_not_contains(
        _stable_ownership_integration_registration "${_forbidden}"
        "stable ownership integration target remains macro-off and positive")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-stable-context-integration-tests"
    "database-zone-runtime-stable-context-(integration|forgotten-finish)")
    require_contains(
        _ci "${_marker}"
        "explicit Windows x86 stable ownership enrollment")
endforeach()
foreach(_macro IN ITEMS
    "KISAK_DB_REGISTRY_OWNERSHIP_COORDINATOR_TESTING=1"
    "KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1"
    "KISAK_MEMORY_TREE_VALIDATION_TESTING=1"
    "KISAK_SCRIPT_STRING_PERF_TESTING=1")
    require_literal_count(
        _tests "${_macro}" 2
        "existing production-test authority grant count")
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
    "TestDurableStorageIdentityAuthentication();"
    "TestAbandonedReceiptAuthentication();"
    "TestLiveUnloadRetryBindingAndTerminalReset();"
    "TestAdmissionPostconditionFailsClosed();"
    "driver.beginReentryStatus == ZoneScriptStringOwnershipStatus::Busy"
    "driver.reentryStatus == ZoneScriptStringOwnershipStatus::Busy"
    "foreignOutput == 0x12345678u"
    "nextKey.generation != originalKey.generation"
    "driver.observedContextAfterFree"
    "ObserveRegistryCallbackAuthorization("
    "RegistryCallbackPurpose::Unpublishing)] == 2"
    "RegistryCallbackPurpose::Cleaning)] > 0"
    "RegistryCallbackPurpose::Admitting)] == 1"
    "RegistryCallbackPurpose::Unloading)] > 0"
    "rejectedSerial == UINT32_C(0x11223344)"
    "registryCallbackForeignSerial == UINT32_C(0x13572468)"
    "registryCallbackForeignWitness == UINT8_C(0xA5)"
    "registryCallbackLastWitness"
    "previousWindowWitness != windowWitness"
    "previousWindowWitness))"
    "rejectedWindowWitness == UINT8_C(0x5A)"
    "TryBeginRegistryCallbackWindow("
    "fixture->ownership.poisoned()"
    "UINT8_MAX, UINT8_MAX"
    "CorruptAdmissionWindow"
    "fixture.ownership.phase() == ZoneScriptStringOwnershipPhase::UnsafeFailure"
    "fixture.ownership.serializerRetained()"
    "SetTransactionSerial("
    "SetReserved("
    "== ZoneScriptStringOwnershipStatus::StaleKey")
    require_contains(_fixture "${_marker}" "controller regression coverage")
endforeach()

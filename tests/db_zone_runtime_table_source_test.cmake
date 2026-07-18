cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED SOURCE_ROOT OR SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "SOURCE_ROOT must identify the KisakCOD source tree")
endif()

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_table.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_table.cpp")
set(_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_table_tests.cpp")
set(_production_seal_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_table_production_seal_tests.cpp")
set(_manifest_path "${SOURCE_ROOT}/scripts/common_files.cmake")
set(_tests_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
set(_registry_path "${SOURCE_ROOT}/src/database/db_registry.cpp")
set(_file_load_path "${SOURCE_ROOT}/src/database/db_file_load.cpp")
set(_load_path "${SOURCE_ROOT}/src/database/db_load.cpp")
set(_stream_path "${SOURCE_ROOT}/src/database/db_stream_load.cpp")
set(_stringtable_path
    "${SOURCE_ROOT}/src/database/db_stringtable_load.cpp")
set(_xanim_path "${SOURCE_ROOT}/src/xanim/xanim.h")

foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_fixture_path}"
    "${_production_seal_path}"
    "${_manifest_path}"
    "${_tests_path}"
    "${_ci_path}"
    "${_registry_path}"
    "${_file_load_path}"
    "${_load_path}"
    "${_stream_path}"
    "${_stringtable_path}"
    "${_xanim_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing zone runtime table source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_fixture_path}" _fixture)
file(READ "${_production_seal_path}" _production_seal)
file(READ "${_manifest_path}" _manifest)
file(READ "${_tests_path}" _tests)
file(READ "${_ci_path}" _ci)
file(READ "${_registry_path}" _registry)
file(READ "${_file_load_path}" _file_load)
file(READ "${_load_path}" _load)
file(READ "${_stream_path}" _stream)
file(READ "${_stringtable_path}" _stringtable)
file(READ "${_xanim_path}" _xanim)

foreach(_var IN ITEMS
    _header
    _source
    _fixture
    _production_seal
    _manifest
    _tests
    _ci
    _registry
    _file_load
    _load
    _stream
    _stringtable
    _xanim)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${${_var}}")
    set(${_var} "${_normalized}")
endforeach()

function(require_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(_position EQUAL -1)
        message(FATAL_ERROR
            "Missing zone runtime table invariant (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_not_contains SOURCE_VAR NEEDLE DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${NEEDLE}" _position)
    if(NOT _position EQUAL -1)
        message(FATAL_ERROR
            "Forbidden zone runtime table regression (${DESCRIPTION}): "
            "'${NEEDLE}'")
    endif()
endfunction()

function(require_ordered SOURCE_VAR FIRST SECOND DESCRIPTION)
    string(FIND "${${SOURCE_VAR}}" "${FIRST}" _first)
    string(FIND "${${SOURCE_VAR}}" "${SECOND}" _second)
    if(_first EQUAL -1 OR _second EQUAL -1 OR _first GREATER_EQUAL _second)
        message(FATAL_ERROR
            "Missing or unordered zone runtime table invariant "
            "(${DESCRIPTION})")
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

# A macro-off translation unit recreates the helper's public name and probes
# every private mutable capability that the test-only implementation exposes.
# These are normal positive-build checks, so unrelated compiler failures cannot
# satisfy the production authority seal.
foreach(_marker IN ITEMS
    "struct ZoneRuntimeTableTestAccess"
    "CanReachEntries"
    "CanMutateReserved"
    "CanReachMutableLifecycle"
    "CanReachMutableOwnership"
    "CanMutateKey"
    "&table->entries_;"
    "table->reserved_ = 1u;"
    "&entry->lifecycle_;"
    "&entry->scriptStringOwnership_;"
    "entry->key_ = zone_load::ZoneLoadContextKey{};"
    "!ZoneRuntimeTableTestAccess::CanReachEntries<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanMutateReserved<ZoneRuntimeTable>"
    "!ZoneRuntimeTableTestAccess::CanReachMutableLifecycle<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanReachMutableOwnership<ZoneRuntimeEntry>"
    "!ZoneRuntimeTableTestAccess::CanMutateKey<ZoneRuntimeEntry>")
    require_contains(
        _production_seal "${_marker}" "independent production authority seal")
endforeach()
require_not_contains(
    _production_seal
    "#define KISAK_DB_ZONE_RUNTIME_TABLE_TESTING"
    "production authority seal cannot opt into test access")

extract_slice(
    _tests
    "# Compile production's runtime-table header without its test-access opt-in."
    "add_executable(kisakcod-fx-archive-disk32-tests"
    _production_seal_registration
    "production authority-seal registration")
foreach(_marker IN ITEMS
    "add_executable( kisakcod-db-zone-runtime-table-production-seal-tests"
    "db_zone_runtime_table_production_seal_tests.cpp"
    "NAME database-zone-runtime-table-production-test-access-sealed")
    require_contains(
        _production_seal_registration
        "${_marker}"
        "normal positive-build production authority seal")
endforeach()
foreach(_forbidden IN ITEMS
    "WILL_FAIL"
    "EXCLUDE_FROM_ALL"
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING")
    require_not_contains(
        _production_seal_registration
        "${_forbidden}"
        "production authority seal cannot accept a vacuous failure")
endforeach()

# Both owning classes independently gate their friendship, and the helper's
# forward declaration and implementation remain absent from production TUs.
foreach(_marker IN ITEMS
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING struct ZoneRuntimeTableTestAccess; #endif"
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING friend struct ZoneRuntimeTableTestAccess; #endif"
    "#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING // Tests opt in before including this header.")
    require_contains(_header "${_marker}" "macro-gated test authority")
endforeach()

# The fixed table owns only durable metadata.  It remains external to XZone and
# cannot allocate, report, touch PMem, publish assets, or mutate script strings.
foreach(_var IN ITEMS _header _source)
    foreach(_forbidden IN ITEMS
        "#include <xanim/"
        "#include <vector>"
        "#include <string>"
        "std::function"
        "Com_Error("
        "PMem_"
        "DB_LoadXFile"
        "DB_AddXAsset"
        "DB_LinkXAssetEntry"
        "g_zones"
        "g_zoneHandles"
        "SL_"
        "malloc("
        "calloc("
        "realloc("
        "operator new"
        "throw "
        "catch (")
        require_not_contains(
            ${_var} "${_forbidden}" "metadata-only report-free table")
    endforeach()
endforeach()
require_not_contains(
    _xanim "ZoneRuntimeTable" "runtime ownership cannot enter XZone")

# Freeze the external 33-entry schema, default-slot reservation, full-width
# generation key, stable control objects, and portable layouts.
foreach(_marker IN ITEMS
    "Physical slot zero remains the engine's reserved/default slot."
    "Only slots 1..32 are usable."
    "class alignas(8) ZoneRuntimeEntry final"
    "zone_load::ZoneLoadContextSlot lifecycle_{};"
    "ZoneScriptStringOwnershipController scriptStringOwnership_{};"
    "zone_load::ZoneLoadContextKey key_{};"
    "const ZoneRuntimeEntry *entry = nullptr;"
    "RUNTIME_SIZE(ZoneRuntimeEntry, 0x60, 0x78);"
    "std::array<ZoneRuntimeEntry, zone_slots::kPhysicalZoneSlotCount> entries_{};"
    "RUNTIME_SIZE(ZoneRuntimeTable, 0xC68, 0xF80);"
    "RUNTIME_SIZE(ZoneRuntimeGenerationView, 0x18, 0x18);"
    "ZoneRuntimeEntry(const ZoneRuntimeEntry &) = delete;"
    "ZoneRuntimeTable(const ZoneRuntimeTable &) = delete;"
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING"
    "friend class ZoneRuntimeTable;"
    "authenticateExactMutableEntry("
    "completeMutableOperation("
    "mutableScriptStringOwnership("
    "TryBeginZoneRuntimeScriptStringOwnership("
    "TryStageZoneRuntimeScriptString("
    "TrySealZoneRuntimeScriptStrings("
    "TryBeginZoneRuntimeScriptStringTransfer("
    "TryTransferNextZoneRuntimeScriptString("
    "TryPrepareZoneRuntimeScriptStringCommit("
    "TryCommitZoneRuntimeScriptStringsAndAdmit("
    "TryBeginZoneRuntimeScriptStringRollback("
    "TryRollbackNextZoneRuntimeScriptString("
    "TryFinishZoneRuntimeScriptStringAbandonment("
    "TryUnloadZoneRuntimeGeneration("
    "TryResetZoneRuntimeTerminalReceipt("
    "Rejected,"
    "CountMismatch,"
    "CapacityExceeded,"
    "authenticates table state, physical slot, durable key, lifecycle generation,"
    "unchanged on every non-Success result"
    "retains exact callback/controller ownership"
    "The lifecycle terminal kind,"
    "generation, and durable table key remain as a receipt"
    "external serialization.")
    require_contains(_header "${_marker}" "durable external table schema")
endforeach()
foreach(_marker IN ITEMS
    "zone_slots::kDefaultZoneSlot"
    "zone_slots::kFirstUsableZoneSlot"
    "zone_slots::kPhysicalZoneSlotCount"
    "ZoneRuntimeTable g_productionZoneRuntimeTable{};"
    "ZoneRuntimeTable &ProductionZoneRuntimeTable() noexcept"
    "TryInitializeZoneLoadContextSlot("
    "TryClaimZoneLoadContext("
    "ZoneLoadContextKeyMatches("
    "lifecycle.canonical()"
    "ownership.isEmptyCanonical()"
    "ValidateEntryBinding("
    "IsPristineLifecycle(lifecycle, true, physicalSlot)"
    "lifecycle.cleanupActive()"
    "lifecycle.cleanupPoisoned()"
    "ownership.poisoned()"
    "ownership.canonicalForBinding(&lifecycle, key)"
    "if (!ownership.serializerRetained())"
    "IsOwnershipCallbackPhase(ownershipPhase)"
    "case OwnershipPhase::UnpublishingCallback:"
    "case OwnershipPhase::Cleaning:"
    "case OwnershipPhase::Admitting:"
    "case OwnershipPhase::Unloading:"
    "case OwnershipPhase::UnloadingCallback:"
    "if (ownershipPhase == OwnershipPhase::Unloaded)"
    "ZoneRuntimeTableStatus::Retry"
    "ZoneRuntimeTableStatus::Rejected"
    "ZoneRuntimeTableStatus::CountMismatch"
    "ZoneRuntimeTableStatus::CapacityExceeded"
    "MapOwnershipStatus("
    "MapLiveUnloadOwnershipStatus("
    "ZoneRuntimeTable::authenticateExactMutableEntry("
    "ZoneRuntimeTable::completeMutableOperation("
    "ZoneRuntimeTable::mutableScriptStringOwnership("
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Loading"
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Abandoning"
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Live"
    "TableState::Poisoned")
    require_contains(_source "${_marker}" "checked table implementation")
endforeach()

extract_slice(
    _source
    "ZoneRuntimeTableStatus MapLiveUnloadOwnershipStatus("
    "ZoneRuntimeTableStatus AuthenticateExactEntry("
    _live_unload_status_map
    "Live-unload status allowlist")
foreach(_marker IN ITEMS
    "case OwnershipStatus::Success:"
    "case OwnershipStatus::Retry:"
    "case OwnershipStatus::Busy:"
    "case OwnershipStatus::InvalidArgument:"
    "case OwnershipStatus::InvalidState:"
    "case OwnershipStatus::InvalidKey:"
    "case OwnershipStatus::StaleKey:"
    "case OwnershipStatus::InvalidPhase:"
    "case OwnershipStatus::Rejected:"
    "case OwnershipStatus::CountMismatch:"
    "case OwnershipStatus::CapacityExceeded:"
    "case OwnershipStatus::UnsafeFailure:"
    "return ZoneRuntimeTableStatus::UnsafeFailure;")
    require_contains(
        _live_unload_status_map
        "${_marker}"
        "terminal Live-unload must fail closed on journal-only statuses")
endforeach()
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::InvalidPhase:"
    "return MapOwnershipStatus(status);"
    "allowed terminal result must use the shared status mapper")
require_ordered(
    _live_unload_status_map
    "return MapOwnershipStatus(status);"
    "case OwnershipStatus::Rejected:"
    "journal-only results must follow the terminal allowlist")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::Rejected:"
    "case OwnershipStatus::CountMismatch:"
    "Rejected must enter the journal-only fail-closed group")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::CountMismatch:"
    "case OwnershipStatus::CapacityExceeded:"
    "CountMismatch must enter the journal-only fail-closed group")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::CapacityExceeded:"
    "case OwnershipStatus::UnsafeFailure:"
    "CapacityExceeded must enter the terminal unsafe group")
require_ordered(
    _live_unload_status_map
    "case OwnershipStatus::UnsafeFailure:"
    "return ZoneRuntimeTableStatus::UnsafeFailure;"
    "terminal unsafe group must poison through UnsafeFailure")

# Initialization is idempotent only while every usable entry remains pristine.
# A rebound table is rejected without resetting its generation; malformed or
# partially initialized storage is poisoned.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable("
    "ZoneRuntimeTableStatus TryGetZoneRuntimeEntry("
    _initialize
    "table initialization")
require_ordered(
    _initialize
    "if (state == TableState::Initialized)"
    "return pristine ? ZoneRuntimeTableStatus::Success : ZoneRuntimeTableStatus::InvalidState;"
    "reinitialization accepts only pristine state")
require_ordered(
    _initialize
    "TryInitializeZoneLoadContextSlot("
    "table->state_ = static_cast<std::uint32_t>(TableState::Initialized);"
    "all lifecycle slots initialize before table publication")

# Claims and keyed lookup are failure-atomic.  The explicit physical-slot
# argument prevents a same-generation key from being replayed across slots.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration("
    "ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration("
    _claim
    "generation claim")
require_ordered(
    _claim
    "TryClaimZoneLoadContext("
    "entry.key_ = candidate;"
    "lifecycle claim before durable key publication")
require_ordered(
    _claim
    "entry.key_ = candidate;"
    "*inOutKey = candidate;"
    "durable key before caller output publication")
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration("
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration("
    _lookup
    "keyed generation lookup")
require_ordered(
    _lookup
    "if (key.slot != physicalSlot)"
    "ZoneLoadContextKeyMatches("
    "cross-slot rejection before active-generation authentication")
require_ordered(
    _lookup
    "ZoneLoadContextKeyMatches("
    "*outView = candidate;"
    "authentication before output publication")

# Every public loading mutation routes through the same inaccessible raw-entry
# shim, authenticates the exact durable binding on both sides of the controller
# call, and preserves recoverable controller statuses.  The one scalar output
# remains local until successful post-authentication.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership("
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration("
    _mutable_adapters
    "exact-key mutable loading adapters")
foreach(_marker IN ITEMS
    "TryBeginZoneRuntimeScriptStringOwnership("
    "TryStageZoneRuntimeScriptString("
    "TrySealZoneRuntimeScriptStrings("
    "TryBeginZoneRuntimeScriptStringTransfer("
    "TryTransferNextZoneRuntimeScriptString("
    "TryPrepareZoneRuntimeScriptStringCommit("
    "TryCommitZoneRuntimeScriptStringsAndAdmit("
    "TryBeginZoneRuntimeScriptStringRollback("
    "TryRollbackNextZoneRuntimeScriptString("
    "TryFinishZoneRuntimeScriptStringAbandonment("
    "authenticateExactMutableEntry("
    "completeMutableOperation("
    "TryBeginZoneScriptStringOwnership("
    "TryStageZoneScriptString("
    "TrySealZoneScriptStrings("
    "TryBeginZoneScriptStringTransfer("
    "TryTransferNextZoneScriptString("
    "TryPrepareZoneScriptStringCommit("
    "TryCommitZoneScriptStringsAndAdmit("
    "TryBeginZoneScriptStringRollback("
    "TryRollbackNextZoneScriptString("
    "TryFinishZoneScriptStringAbandonment(")
    require_contains(
        _mutable_adapters "${_marker}" "complete mutable adapter surface")
endforeach()

function(require_mutable_adapter START END MUTATION DESCRIPTION)
    extract_slice(
        _source "${START}" "${END}" _adapter
        "${DESCRIPTION} adapter")
    require_ordered(
        _adapter
        "authenticateExactMutableEntry("
        "${MUTATION}"
        "${DESCRIPTION} pre-authentication")
    require_ordered(
        _adapter
        "${MUTATION}"
        "completeMutableOperation("
        "${DESCRIPTION} post-authentication")
endfunction()

require_mutable_adapter(
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringOwnership("
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    "TryBeginZoneScriptStringOwnership("
    "ownership begin")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings("
    "TryStageZoneScriptString("
    "string stage")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringTransfer("
    "TrySealZoneScriptStrings("
    "journal seal")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringTransfer("
    "ZoneRuntimeTableStatus TryTransferNextZoneRuntimeScriptString("
    "TryBeginZoneScriptStringTransfer("
    "transfer begin")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryTransferNextZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TryPrepareZoneRuntimeScriptStringCommit("
    "TryTransferNextZoneScriptString("
    "one-entry transfer")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryPrepareZoneRuntimeScriptStringCommit("
    "ZoneRuntimeTableStatus TryCommitZoneRuntimeScriptStringsAndAdmit("
    "TryPrepareZoneScriptStringCommit("
    "commit prepare")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryCommitZoneRuntimeScriptStringsAndAdmit("
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback("
    "TryCommitZoneScriptStringsAndAdmit("
    "live commit")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryBeginZoneRuntimeScriptStringRollback("
    "ZoneRuntimeTableStatus TryRollbackNextZoneRuntimeScriptString("
    "TryBeginZoneScriptStringRollback("
    "rollback begin")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryRollbackNextZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TryFinishZoneRuntimeScriptStringAbandonment("
    "TryRollbackNextZoneScriptString("
    "one-entry rollback")
require_mutable_adapter(
    "ZoneRuntimeTableStatus TryFinishZoneRuntimeScriptStringAbandonment("
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration("
    "TryFinishZoneScriptStringAbandonment("
    "abandonment finish")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString("
    "ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings("
    _stage_adapter
    "failure-atomic keyed stage adapter")
require_ordered(
    _stage_adapter
    "std::uint32_t candidate = 0;"
    "TryStageZoneScriptString("
    "stage candidate precedes controller mutation")
require_ordered(
    _stage_adapter
    "TryStageZoneScriptString("
    "completeMutableOperation("
    "stage post-authentication follows mutation")
require_ordered(
    _stage_adapter
    "completeMutableOperation("
    "*outStringId = candidate;"
    "stage output publishes only after post-authentication")
require_not_contains(
    _header
    "ZoneRuntimeMutableAdapterAccess"
    "public header cannot expose raw mutable ownership")
require_not_contains(
    _source
    "ZoneRuntimeMutableAdapterAccess"
    "implementation cannot restore a forgeable friend shim")

extract_slice(
    _source
    "ZoneRuntimeTableStatus ZoneRuntimeTable::authenticateExactMutableEntry("
    "const zone_load::ZoneLoadContextKey &ZoneRuntimeEntry::key()"
    _mutable_access
    "private mutable table member boundary")
foreach(_marker IN ITEMS
    "validateInitializedHeader()"
    "if (!static_cast<bool>(key))"
    "ValidateUsableSlot(physicalSlot)"
    "if (key.slot != physicalSlot)"
    "AuthenticateExactEntry(entry, physicalSlot, key)"
    "MapOwnershipStatus(ownershipStatus)"
    "const ZoneRuntimeTableStatus postAuthentication ="
    "authenticateExactMutableEntry(physicalSlot, key, &postEntry)"
    "status == ZoneRuntimeTableStatus::UnsafeFailure"
    "postAuthentication != ZoneRuntimeTableStatus::Success"
    "poison();")
    require_contains(
        _mutable_access "${_marker}" "pre/post mutation authentication")
endforeach()
require_ordered(
    _mutable_access
    "MapOwnershipStatus(ownershipStatus)"
    "const ZoneRuntimeTableStatus postAuthentication ="
    "map result before mandatory post-authentication")
require_ordered(
    _mutable_access
    "const ZoneRuntimeTableStatus postAuthentication ="
    "return status;"
    "post-authentication before recoverable status publication")

# Live unload is owned by the script-string controller so the outer
# transaction and exact callback identity survive Retry. Reset consumes only
# the matching controller receipt; lifecycle/key evidence remains until Claim
# advances the generation and publishes a new key.
extract_slice(
    _source
    "ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration("
    "ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt("
    _unload
    "keyed live unload")
foreach(_marker IN ITEMS
    "AuthenticateExactEntry(entry, physicalSlot, key)"
    "TryUnloadLiveZoneScriptStringOwnership("
    "MapLiveUnloadOwnershipStatus(ownershipStatus)"
    "OwnershipPhase::Unloaded"
    "ZoneLoadTerminalKind::Unloaded"
    "table->poison();")
    require_contains(_unload "${_marker}" "controller-owned live unload")
endforeach()
require_ordered(
    _unload
    "AuthenticateExactEntry(entry, physicalSlot, key)"
    "TryUnloadLiveZoneScriptStringOwnership("
    "exact key authentication before unload mutation")
require_ordered(
    _unload
    "TryUnloadLiveZoneScriptStringOwnership("
    "const ZoneRuntimeTableStatus postAuthentication ="
    "post-callback authentication")

extract_slice(
    _source
    "ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt("
    "} // namespace db::zone_runtime"
    _terminal_reset
    "keyed terminal reset")
foreach(_marker IN ITEMS
    "AuthenticateExactEntry(entry, physicalSlot, key)"
    "entry.lifecycle_.phase() != zone_load::ZoneLoadContextPhase::Empty"
    "entry.lifecycle_.terminalKind()"
    "OwnershipPhase::Abandoned"
    "OwnershipPhase::Unloaded"
    "TryResetTerminalZoneScriptStringOwnership("
    "entry.scriptStringOwnership_.isEmptyCanonical()"
    "entry.lifecycle_.terminalKind() != terminalKind"
    "entry.key_ != key")
    require_contains(
        _terminal_reset "${_marker}" "exact receipt-preserving reset")
endforeach()
require_ordered(
    _terminal_reset
    "AuthenticateExactEntry(entry, physicalSlot, key)"
    "TryResetTerminalZoneScriptStringOwnership("
    "exact key and receipt authentication before reset")
require_not_contains(
    _terminal_reset
    "entry.key_ = {}"
    "terminal reset cannot erase ABA evidence")

# DB_Init initializes the table before mutating the legacy asset pools and uses
# the established fatal initialization boundary.  The load, stream, and asset
# publication paths remain completely unenrolled in this independent batch.
extract_slice(
    _registry
    "void DB_Init()"
    "void __cdecl DB_InitPoolHeader("
    _db_init
    "production DB initialization")
foreach(_marker IN ITEMS
    "#include \"db_zone_runtime_table.h\""
    "TryInitializeZoneRuntimeTable("
    "ProductionZoneRuntimeTable()"
    "ZoneRuntimeTableStatus::Success"
    "Com_Error( ERR_FATAL, \"DB_Init: zone runtime table initialization failed (%u)\""
    "return;")
    require_contains(_registry "${_marker}" "production initialization wiring")
endforeach()
require_ordered(
    _db_init
    "TryInitializeZoneRuntimeTable("
    "for (XAssetType type"
    "table initialization before asset-pool mutation")
foreach(_var IN ITEMS _registry _file_load _load _stream _stringtable)
    require_not_contains(
        ${_var}
        "TryClaimZoneRuntimeGeneration("
        "legacy loader cannot claim the table in this batch")
    require_not_contains(
        ${_var}
        "TryGetZoneRuntimeGeneration("
        "legacy loader cannot consume keyed capabilities in this batch")
    require_not_contains(
        ${_var}
        "TryUnloadZoneRuntimeGeneration("
        "legacy loader cannot unload through unwired callbacks")
    require_not_contains(
        ${_var}
        "TryResetZoneRuntimeTerminalReceipt("
        "legacy loader cannot reset runtime receipts")
    foreach(_mutable_api IN ITEMS
        "TryBeginZoneRuntimeScriptStringOwnership("
        "TryStageZoneRuntimeScriptString("
        "TrySealZoneRuntimeScriptStrings("
        "TryBeginZoneRuntimeScriptStringTransfer("
        "TryTransferNextZoneRuntimeScriptString("
        "TryPrepareZoneRuntimeScriptStringCommit("
        "TryCommitZoneRuntimeScriptStringsAndAdmit("
        "TryBeginZoneRuntimeScriptStringRollback("
        "TryRollbackNextZoneRuntimeScriptString("
        "TryFinishZoneRuntimeScriptStringAbandonment(")
        require_not_contains(
            ${_var} "${_mutable_api}" "legacy loader remains unenrolled")
    endforeach()
endforeach()

# Runtime coverage spans every slot, ABI/noexcept traits, stable addresses,
# bounds, output atomicity, generation/ABA rejection, and corruption handling.
foreach(_marker IN ITEMS
    "void TestLayoutNoexceptAndDefaultState()"
    "void TestAllPhysicalSlotsAndStableAddresses()"
    "void TestClaimAuthenticationAndAdjacentIsolation()"
    "void TestGenerationAdvanceRejectsAba()"
    "void TestPartialInitializationAndCorruptionFailClosed()"
    "void TestHiddenCorruptionAndCleanupReentryFailClosed()"
    "void TestControllerPhaseAndSerializerMatrix()"
    "void TestKeyedMutableCommitAndAuthentication()"
    "void TestKeyedMutableRecoverableAbandonment()"
    "void TestUnsafeMutableBoundary(const bool corruptPostcondition)"
    "void TestLiveUnloadRetryResetReuseAndAba()"
    "void TestAbandonedReceiptResetAndGenerationExhaustion()"
    "void TestTerminalAdapterPhaseSerializerAndCorruptionGates()"
    "void TestUnsafeLiveUnloadBoundary("
    "void ObserveAdmittingController(void *const context) noexcept"
    "current - previous == sizeof(ZoneRuntimeEntry)"
    "zone_slots::kUsableZoneSlotCount == 32"
    "zone_slots::kPhysicalZoneSlotCount == 33"
    "ZoneRuntimeTableStatus::InvalidSlot"
    "ZoneRuntimeTableStatus::StaleKey"
    "ZoneRuntimeTableStatus::UnsafeFailure"
    "ZoneRuntimeTableStatus::Busy"
    "ZoneRuntimeTableStatus::Retry"
    "ZoneRuntimeTableStatus::Rejected"
    "ZoneRuntimeTableStatus::CountMismatch"
    "ZoneRuntimeTableStatus::CapacityExceeded"
    "ZoneScriptStringOwnershipControllerTestAccess::SetStorage("
    "kInitializedFlag | kCleanupActiveFlag"
    "kInitializedFlag | kCleanupPoisonedFlag"
    "probe.status == ZoneRuntimeTableStatus::Busy"
    "ZoneScriptStringOwnershipPhase::UnpublishingCallback"
    "ZoneScriptStringOwnershipPhase::Live"
    "ZoneScriptStringOwnershipPhase::Unloading"
    "ZoneScriptStringOwnershipPhase::Unloaded"
    "driver.lookupReentry == ZoneRuntimeTableStatus::Busy"
    "driver.unloadReentry == ZoneRuntimeTableStatus::Busy"
    "driver.resetReentry == ZoneRuntimeTableStatus::Busy"
    "driver.claimReentry == ZoneRuntimeTableStatus::Busy"
    "driver.usedContextAfterFree"
    "maximumGeneration"
    "ZoneRuntimeTableStatus::GenerationExhausted"
    "ZoneScriptStringOwnershipControllerTestAccess::SetLifecycle("
    "ZoneScriptStringOwnershipControllerTestAccess::SetTransactionSerial("
    "ZoneScriptStringOwnershipControllerTestAccess::SetReserved("
    "hiddenGenerationLifecycle->generation() == 23"
    "newKey.generation == oldKey.generation + 1"
    "if (argc != 1)"
    "if (argc != 3"
    "std::string_view(argv[1]) == \"--unsafe-mutable\""
    "kind == \"backend\""
    "kind == \"postcondition\""
    "std::string_view(argv[1]) != \"--unsafe-live-unload\""
    "Zone runtime table tests passed")
    require_contains(_fixture "${_marker}" "focused runtime coverage")
endforeach()

# Compile the source in production targets, execute it on all portable hosts,
# and retain explicit measured Windows x86 Debug/Release coverage.
foreach(_marker IN ITEMS
    "\${SRC_DIR}/database/db_zone_runtime_table.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_table.h")
    require_contains(_manifest "${_marker}" "production source manifest")
endforeach()
foreach(_marker IN ITEMS
    "add_executable(kisakcod-db-zone-runtime-table-tests"
    "add_executable( kisakcod-db-zone-runtime-table-production-seal-tests"
    "db_zone_runtime_table_tests.cpp"
    "db_zone_runtime_table_production_seal_tests.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_table.cpp"
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING=1"
    "KISAK_DB_ZONE_LOAD_CONTEXT_TESTING=1"
    "KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1"
    "NAME database-zone-runtime-table-ownership"
    "NAME database-zone-runtime-table-production-test-access-sealed"
    "database-zone-runtime-table-unload-unsafe-${_zone_runtime_unsafe_boundary}"
    "--unsafe-live-unload ${_zone_runtime_unsafe_boundary}"
    "database-zone-runtime-table-unload-invalid-missing-value"
    "database-zone-runtime-table-unload-invalid-extra-value"
    "database-zone-runtime-table-unload-invalid-unknown-option"
    "database-zone-runtime-table-mutation-unsafe-${_zone_runtime_mutation_unsafe_kind}"
    "--unsafe-mutable ${_zone_runtime_mutation_unsafe_kind}"
    "database-zone-runtime-table-mutation-invalid-missing-value"
    "database-zone-runtime-table-mutation-invalid-extra-value"
    "database-zone-runtime-table-mutation-invalid-kind"
    "PROPERTIES TIMEOUT 30 WILL_FAIL TRUE"
    "NAME database-zone-runtime-table-source-invariants"
    "db_zone_runtime_table_source_test.cmake")
    require_contains(_tests "${_marker}" "portable CMake test integration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-table-tests"
    "kisakcod-db-zone-runtime-table-production-seal-tests"
    "production-test-access-sealed"
    "mutation-unsafe-(backend|postcondition)"
    "mutation-invalid-(missing-value|extra-value|kind)")
    require_contains(_ci "${_marker}" "measured Windows x86 CI integration")
endforeach()

message(STATUS "Zone runtime table source invariants passed")

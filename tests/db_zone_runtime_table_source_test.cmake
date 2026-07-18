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
    "TryUnloadZoneRuntimeGeneration("
    "TryResetZoneRuntimeTerminalReceipt("
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
    "MapOwnershipStatus("
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Loading"
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Abandoning"
    "lifecycle.phase() == zone_load::ZoneLoadContextPhase::Live"
    "TableState::Poisoned")
    require_contains(_source "${_marker}" "checked table implementation")
endforeach()

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
    "MapOwnershipStatus(ownershipStatus)"
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
    "newKey.generation == oldKey.generation + 1"
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
    "db_zone_runtime_table_tests.cpp"
    "\${SRC_DIR}/database/db_zone_runtime_table.cpp"
    "KISAK_DB_ZONE_RUNTIME_TABLE_TESTING=1"
    "KISAK_DB_ZONE_LOAD_CONTEXT_TESTING=1"
    "KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING=1"
    "NAME database-zone-runtime-table-ownership"
    "database-zone-runtime-table-unload-unsafe-${_zone_runtime_unsafe_boundary}"
    "--unsafe-live-unload ${_zone_runtime_unsafe_boundary}"
    "NAME database-zone-runtime-table-source-invariants"
    "db_zone_runtime_table_source_test.cmake")
    require_contains(_tests "${_marker}" "portable CMake test integration")
endforeach()
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-table-tests"
    "database-zone-runtime-table-(ownership|source-invariants|unload-unsafe-[0-6])")
    require_contains(_ci "${_marker}" "measured Windows x86 CI integration")
endforeach()

message(STATUS "Zone runtime table source invariants passed")

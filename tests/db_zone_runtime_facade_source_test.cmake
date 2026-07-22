if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
get_filename_component(SOURCE_ROOT "${SOURCE_ROOT}" ABSOLUTE)

set(_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_facade.h")
set(_source_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_facade.cpp")
set(_coordinator_header_path
    "${SOURCE_ROOT}/src/database/db_registry_ownership_coordinator.h")
set(_table_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_table.h")
set(_callback_context_header_path
    "${SOURCE_ROOT}/src/database/db_zone_runtime_callback_context.h")
set(_integration_fixture_path
    "${SOURCE_ROOT}/tests/db_zone_runtime_stable_context_integration_tests.cpp")
set(_tests_cmake_path "${SOURCE_ROOT}/tests/CMakeLists.txt")
set(_ci_path "${SOURCE_ROOT}/.github/workflows/ci.yml")
foreach(_path IN ITEMS
    "${_header_path}"
    "${_source_path}"
    "${_coordinator_header_path}"
    "${_table_header_path}"
    "${_callback_context_header_path}"
    "${_integration_fixture_path}"
    "${_tests_cmake_path}"
    "${_ci_path}")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Missing runtime facade source: ${_path}")
    endif()
endforeach()

file(READ "${_header_path}" _header)
file(READ "${_source_path}" _source)
file(READ "${_table_header_path}" _table_header)
file(READ "${_integration_fixture_path}" _integration_fixture)
file(READ "${_tests_cmake_path}" _tests_cmake)
file(READ "${_ci_path}" _ci)

function(normalize_space INPUT OUTPUT)
    string(REGEX REPLACE "[ \t\r\n]+" " " _normalized "${INPUT}")
    string(STRIP "${_normalized}" _normalized)
    set(${OUTPUT} "${_normalized}" PARENT_SCOPE)
endfunction()

function(require_contains VARIABLE MARKER LABEL)
    string(FIND "${${VARIABLE}}" "${MARKER}" _index)
    if(_index EQUAL -1)
        message(FATAL_ERROR "Missing runtime facade invariant (${LABEL}): ${MARKER}")
    endif()
endfunction()

function(require_not_contains VARIABLE MARKER LABEL)
    string(FIND "${${VARIABLE}}" "${MARKER}" _index)
    if(NOT _index EQUAL -1)
        message(FATAL_ERROR "Forbidden runtime facade construct (${LABEL}): ${MARKER}")
    endif()
endfunction()

function(substring_count VARIABLE MARKER OUTPUT)
    string(LENGTH "${${VARIABLE}}" _before_length)
    string(REPLACE "${MARKER}" "" _without "${${VARIABLE}}")
    string(LENGTH "${_without}" _after_length)
    string(LENGTH "${MARKER}" _marker_length)
    math(EXPR _removed "${_before_length} - ${_after_length}")
    math(EXPR _count "${_removed} / ${_marker_length}")
    set(${OUTPUT} ${_count} PARENT_SCOPE)
endfunction()

function(require_substring_count VARIABLE MARKER EXPECTED LABEL)
    substring_count(${VARIABLE} "${MARKER}" _count)
    if(NOT _count EQUAL EXPECTED)
        message(FATAL_ERROR
            "Runtime facade invariant count drifted (${LABEL}): "
            "expected ${EXPECTED} '${MARKER}', found ${_count}")
    endif()
endfunction()

function(require_normalized_equals VARIABLE EXPECTED_VARIABLE LABEL)
    normalize_space("${${VARIABLE}}" _actual)
    normalize_space("${${EXPECTED_VARIABLE}}" _expected)
    if(NOT _actual STREQUAL _expected)
        message(FATAL_ERROR
            "Runtime facade closed surface drifted (${LABEL}). "
            "Expected '${_expected}', found '${_actual}'")
    endif()
endfunction()

function(extract_slice VARIABLE START END OUTPUT LABEL)
    string(FIND "${${VARIABLE}}" "${START}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR "Missing start of runtime facade ${LABEL}: ${START}")
    endif()
    string(SUBSTRING "${${VARIABLE}}" ${_start} -1 _tail)
    string(FIND "${_tail}" "${END}" _end)
    if(_end LESS_EQUAL 0)
        message(FATAL_ERROR
            "Missing ordered end of runtime facade ${LABEL}: ${END}")
    endif()
    string(SUBSTRING "${_tail}" 0 ${_end} _slice)
    set(${OUTPUT} "${_slice}" PARENT_SCOPE)
endfunction()

normalize_space("${_header}" _header_normalized)
normalize_space("${_source}" _source_normalized)
normalize_space("${_table_header}" _table_header_normalized)

extract_slice(
    _table_header
    "struct ZoneRuntimePendingCopyView final"
    "RUNTIME_SIZE(ZoneRuntimePendingCopyView, 0x18, 0x18);"
    _pending_copy_view_declaration
    "pointer-free pending-copy view declaration")
set(_expected_pending_copy_view_declaration [=[
struct ZoneRuntimePendingCopyView final
{
    zone_load::ZoneLoadContextKey key{};
    std::uint32_t recordCount = 0;
    std::uint32_t reserved = 0;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return static_cast<bool>(key)
            && recordCount
                <= zone_pending_copy::kPendingCopyRecordCapacity
            && reserved == 0;
    }
};
]=])
require_normalized_equals(
    _pending_copy_view_declaration
    _expected_pending_copy_view_declaration
    "pointer-free 0x18 pending-copy view")
require_contains(
    _table_header_normalized
    "RUNTIME_SIZE(ZoneRuntimePendingCopyView, 0x18, 0x18);"
    "fixed-width pending-copy view ABI")

foreach(_contract_marker IN ITEMS
    "Writable outputs, retained descriptors, and non-empty stream payloads are rejected"
    "Caller buffers read after child mutation begins"
    "This boundary is not a sandbox for arbitrary engine globals"
    "callback contexts are identity-byte checked")
    require_contains(
        _header_normalized "${_contract_marker}"
        "scoped output and retained-input authority contract")
endforeach()

extract_slice(
    _header
    "enum class ZoneRuntimeFacadeStatus : std::uint8_t"
    "#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING"
    _facade_status_declaration
    "status declaration")
set(_expected_facade_status_declaration [=[
enum class ZoneRuntimeFacadeStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidState,
    UnsafeFailure,
};
]=])
require_normalized_equals(
    _facade_status_declaration
    _expected_facade_status_declaration
    "closed status declaration")

# Freeze the whole public declaration, not merely a list of method names. This
# catches overloads, changed argument authority, raw table/view exposure, and
# constructors that could create alternate facade instances.
extract_slice(
    _header
    "class ZoneRuntimeFacade final"
    "private:"
    _facade_public_declaration
    "public declaration")
set(_expected_facade_public_declaration [=[
class ZoneRuntimeFacade final
{
public:
    ZoneRuntimeFacade() = delete;
    ~ZoneRuntimeFacade() noexcept = default;
    ZoneRuntimeFacade(const ZoneRuntimeFacade &) = delete;
    ZoneRuntimeFacade &operator=(const ZoneRuntimeFacade &) = delete;
    ZoneRuntimeFacade(ZoneRuntimeFacade &&) = delete;
    ZoneRuntimeFacade &operator=(ZoneRuntimeFacade &&) = delete;

    [[nodiscard]] static ZoneRuntimeFacadeStatus TryBeginAccess() noexcept;
    [[nodiscard]] static ZoneRuntimeFacadeStatus FinishAccess() noexcept;

    [[nodiscard]] static ZoneRuntimeTableStatus
    TryInitializeRuntimeTable() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryClaimGeneration(
        std::uint32_t physicalSlot,
        zone_load::ZoneLoadContextKey *inOutKey) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryBindGenerationCallbacks(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const ZoneRuntimeGenerationCallbacks &callbacks) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryBeginPhysicalAllocation(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const char *name,
        std::uint32_t allocationType) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryAllocateMemory(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        std::uint32_t size,
        std::uint32_t alignment,
        std::uint32_t type,
        pmem_runtime::AllocationResult *outResult) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryBindStorage(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        void *slab,
        std::size_t slabCapacity,
        const zone_runtime_storage::ZoneRuntimeStoragePlan *plan) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryBeginStreamGeneration(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryBindStreams(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const XZoneMemory *zoneIdentity,
        const relocation::BlockView *blocks,
        std::size_t blockCount) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryBeginPendingCopies(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryAppendPendingCopy(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        std::uint32_t assetEntryIndex) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryGetPendingCopyView(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimePendingCopyView *outView) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryReadPendingCopy(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        std::uint32_t expectedRecordCount,
        std::uint32_t ordinal,
        zone_pending_copy::PendingCopyRecord *outRecord) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    TryBeginScriptStringOwnership(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        script_string_journal::ScriptStringJournal *journal,
        script_string_journal::ScriptStringJournalEntry *storage,
        std::uint32_t storageCapacity,
        std::uint32_t expectedCount) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryStageScriptString(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const script_string_adapter::ScriptStringSourceView &source,
        std::uint32_t *outStringId) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TrySealScriptStrings(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    TryBeginScriptStringTransfer(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryTransferNextScriptString(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    TryPrepareScriptStringCommit(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryPrepareAdmission(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryInvalidateStreams(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryEndPhysicalAllocation(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryCommitGeneration(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    TryBeginGenerationAbandonment(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    TryContinueGenerationAbandonment(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryUnloadGeneration(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryBeginPendingCopyDrain(
        const zone_pending_copy::PendingCopyDrainCallback &callback) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    TryDrainNextPendingCopy() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    TryFinishPendingCopyDrain() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus TryResetTerminalReceipt(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;

    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryBeginStandaloneRegistryOwnership() noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryBorrowRegistryOwnership(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    // This one-shot capability is valid only during the synchronous typed
    // callback that received both arguments. The callback must pass the exact
    // copied key, finish any successful registry borrow before returning, and
    // neither retain nor reuse the context pointer for a later callback or a
    // successor generation.
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryBorrowRegistryOwnershipFromCallback(
        const ZoneRuntimeCallbackContext *context,
        zone_load::ZoneLoadContextKey key) noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    FinishRegistryOwnership() noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryAddDatabaseUser4(std::uint32_t stringId) noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryAddDatabaseUsers4(
        const std::uint32_t *stringIds,
        std::uint32_t count,
        registry_ownership::RegistryOwnershipBulkResult *outResult) noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryInternBoundedName(
        const char *bytes,
        std::uint32_t byteCount,
        registry_ownership::RegistryOwnershipName *outName) noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryReAddRetainedDefaultName(const char *name) noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryReAddRetainedDefaultNames(
        const char *const *names,
        std::uint32_t count,
        registry_ownership::RegistryOwnershipBulkResult *outResult) noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryTransferDatabaseUsers4To8() noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    TryShutdownDatabaseUser8() noexcept;
]=])
require_normalized_equals(
    _facade_public_declaration
    _expected_facade_public_declaration
    "complete public declaration")

set(_facade_test_access_start [=[
#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING
struct ZoneRuntimeFacadeTestAccess final
]=])
extract_slice(
    _header
    "class ZoneRuntimeFacade final"
    "${_facade_test_access_start}"
    _facade_complete_declaration
    "complete class declaration")
require_substring_count(
    _facade_complete_declaration "public:" 1
    "one public facade section")
require_substring_count(
    _facade_complete_declaration "private:" 1
    "one private facade section")

extract_slice(
    _header
    "private:"
    "${_facade_test_access_start}"
    _facade_private_declaration
    "private declaration")
set(_expected_facade_private_declaration [=[
private:
#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING
    friend struct ZoneRuntimeFacadeTestAccess;
#endif

    [[nodiscard]] static ZoneRuntimeFacadeStatus
    authenticateAccess() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    authenticateTableOperationAccess() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    authenticateKeyedTableOperationAccess(
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    authenticateCompositeTableOperationAccess(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    authenticatePendingCopyInspectionOutput(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const void *output,
        std::size_t outputSize,
        std::size_t outputAlignment) noexcept;
    [[nodiscard]] static registry_ownership::RegistryOwnershipStatus
    authenticateRegistryOutputAccess() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus completeTableOperation(
        ZoneRuntimeTableStatus status) noexcept;
    [[nodiscard]] static bool authoritySpanIsSeparated(
        const void *storage,
        std::size_t storageSize,
        std::size_t storageAlignment) noexcept;
    static void poisonAccess() noexcept;
};
]=])
require_normalized_equals(
    _facade_private_declaration
    _expected_facade_private_declaration
    "complete private declaration")

foreach(_raw_authority IN ITEMS
    "ProductionZoneRuntimeTable"
    "ZoneRuntimeTable *"
    "ZoneRuntimeTable &"
    "ZoneRuntimeGenerationView"
    "ZoneRuntimeTableTestAccess"
    "RegistryOwnershipCoordinatorAdmission"
    "ZoneScriptStringAdmissionCallback"
    "PendingCopyLedger"
    "PendingCopyAdmissionReceipt"
    "ZoneLoadCleanupCallbacks"
    "TryGetEntry"
    "TryGetGeneration")
    require_not_contains(
        _facade_public_declaration "${_raw_authority}"
        "no raw table/view/admission authority exposure")
endforeach()

foreach(_marker IN ITEMS
    "enum class ZoneRuntimeFacadeStatus : std::uint8_t"
    "class ZoneRuntimeFacade final"
    "static ZoneRuntimeFacadeStatus TryBeginAccess() noexcept"
    "static ZoneRuntimeFacadeStatus FinishAccess() noexcept"
    "static ZoneRuntimeTableStatus TryInitializeRuntimeTable() noexcept"
    "static registry_ownership::RegistryOwnershipStatus TryBeginStandaloneRegistryOwnership() noexcept"
    "static registry_ownership::RegistryOwnershipStatus TryBorrowRegistryOwnership( std::uint32_t physicalSlot, const zone_load::ZoneLoadContextKey &key) noexcept"
    "static registry_ownership::RegistryOwnershipStatus TryBorrowRegistryOwnershipFromCallback( const ZoneRuntimeCallbackContext *context, zone_load::ZoneLoadContextKey key) noexcept"
    "static registry_ownership::RegistryOwnershipStatus FinishRegistryOwnership() noexcept")
    require_contains(_header_normalized "${_marker}" "closed public facade surface")
endforeach()

require_substring_count(
    _source "FastCriticalSection s_runtimeSerializer{};" 1
    "one private process-lifetime serializer")
require_substring_count(
    _source "Sys_TryLockWrite(&s_runtimeSerializer)" 2
    "nonblocking begin and idle-finish probe")
require_substring_count(
    _source "Sys_UnlockWrite(&s_runtimeSerializer)" 2
    "only idle probe and authenticated finish unlock")
require_substring_count(
    _source "thread_local" 7
    "scalar destructor-free TLS mirrors")
require_substring_count(
    _source "completeTableOperation(" 34
    "all table forwards use the shared post-authentication boundary")
require_substring_count(
    _source "authenticateTableOperationAccess()" 11
    "unkeyed and registry boundaries authenticate")
require_substring_count(
    _source "authenticateKeyedTableOperationAccess(" 20
    "all keyed table boundaries use the whole-bank key guard")
require_substring_count(
    _source "authenticateCompositeTableOperationAccess(" 7
    "all six dual-mode script adapters require composite authority")
require_substring_count(
    _source "RunRegistryOperation(" 12
    "all registry forwards use the shared status/authentication boundary")

# Canonicalize only whitespace that can be introduced by formatting a qualified
# call across lines. Adapter names and argument punctuation remain exact.
function(canonicalize_runtime_facade_forwarding VARIABLE OUTPUT)
    normalize_space("${${VARIABLE}}" _canonical)
    string(REPLACE ":: " "::" _canonical "${_canonical}")
    set(${OUTPUT} "${_canonical}" PARENT_SCOPE)
endfunction()

# Slice a definition at the next ZoneRuntimeFacade definition. This prevents a
# missing expected call from being satisfied by a later method body.
function(extract_runtime_facade_method_slice VARIABLE METHOD OUTPUT)
    set(_marker "ZoneRuntimeFacade::${METHOD}(")
    string(FIND "${${VARIABLE}}" "${_marker}" _start)
    if(_start EQUAL -1)
        message(FATAL_ERROR
            "Missing runtime facade forwarding method: ${METHOD}")
    endif()
    string(SUBSTRING "${${VARIABLE}}" ${_start} -1 _tail)
    string(LENGTH "${_marker}" _marker_length)
    string(SUBSTRING "${_tail}" ${_marker_length} -1 _after_marker)
    string(FIND "${_after_marker}" "ZoneRuntimeFacade::" _next_method)
    string(FIND
        "${_after_marker}"
        "#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING"
        _test_access)
    set(_end -1)
    if(NOT _next_method EQUAL -1)
        set(_end ${_next_method})
    endif()
    if(NOT _test_access EQUAL -1
        AND (_end EQUAL -1 OR _test_access LESS _end))
        set(_end ${_test_access})
    endif()
    if(_end EQUAL -1)
        message(FATAL_ERROR
            "Missing ordered end of runtime facade forwarding method: ${METHOD}")
    endif()
    math(EXPR _slice_length "${_marker_length} + ${_end}")
    string(SUBSTRING "${_tail}" 0 ${_slice_length} _slice)
    set(${OUTPUT} "${_slice}" PARENT_SCOPE)
endfunction()

# Return false unless a method contains its one expected adapter exactly once
# and every other adapter in the closed family zero times. Production checks
# and adversarial fixtures share this predicate.
function(runtime_facade_adapter_set_is_exact
    SLICE_VARIABLE EXPECTED_TOKEN ADAPTER_LIST_VARIABLE OUTPUT)
    canonicalize_runtime_facade_forwarding(
        ${SLICE_VARIABLE} _candidate)
    list(FIND ${ADAPTER_LIST_VARIABLE} "${EXPECTED_TOKEN}" _expected_index)
    if(_expected_index EQUAL -1)
        set(${OUTPUT} FALSE PARENT_SCOPE)
        return()
    endif()
    set(_valid TRUE)
    foreach(_adapter IN LISTS ${ADAPTER_LIST_VARIABLE})
        if(_adapter STREQUAL EXPECTED_TOKEN)
            set(_expected_count 1)
        else()
            set(_expected_count 0)
        endif()
        substring_count(_candidate "${_adapter}" _actual_count)
        if(NOT _actual_count EQUAL _expected_count)
            set(_valid FALSE)
        endif()
    endforeach()
    set(${OUTPUT} ${_valid} PARENT_SCOPE)
endfunction()

# Public method -> raw table adapter is a closed one-to-one map. Borrowing
# registry authority is intentionally special: its ordinary table lookup stays
# in this map, while a bespoke seal below pins both mutually exclusive
# coordinator borrows and the callback-only table authenticator.
set(_table_method_adapter_pairs
    "TryInitializeRuntimeTable|TryInitializeZoneRuntimeTable"
    "TryClaimGeneration|TryClaimZoneRuntimeGeneration"
    "TryBindGenerationCallbacks|TryBindZoneRuntimeGenerationCallbacks"
    "TryBeginPhysicalAllocation|TryBeginZoneRuntimePhysicalAllocation"
    "TryAllocateMemory|TryAllocateZoneRuntimeMemory"
    "TryBindStorage|TryBindZoneRuntimeStorage"
    "TryBeginStreamGeneration|TryBeginZoneRuntimeStreamGeneration"
    "TryBindStreams|TryBindZoneRuntimeStreams"
    "TryBeginPendingCopies|TryBeginZoneRuntimePendingCopies"
    "TryAppendPendingCopy|TryAppendZoneRuntimePendingCopy"
    "TryGetPendingCopyView|TryGetZoneRuntimePendingCopyView"
    "TryReadPendingCopy|TryReadZoneRuntimePendingCopy"
    "TryBeginScriptStringOwnership|TryBeginZoneRuntimeScriptStringOwnership"
    "TryStageScriptString|TryStageZoneRuntimeScriptString"
    "TrySealScriptStrings|TrySealZoneRuntimeScriptStrings"
    "TryBeginScriptStringTransfer|TryBeginZoneRuntimeScriptStringTransfer"
    "TryTransferNextScriptString|TryTransferNextZoneRuntimeScriptString"
    "TryPrepareScriptStringCommit|TryPrepareZoneRuntimeScriptStringCommit"
    "TryPrepareAdmission|TryPrepareZoneRuntimeAdmission"
    "TryInvalidateStreams|TryInvalidateZoneRuntimeStreams"
    "TryEndPhysicalAllocation|TryEndZoneRuntimePhysicalAllocation"
    "TryCommitGeneration|TryCommitZoneRuntimeGeneration"
    "TryBeginGenerationAbandonment|TryBeginZoneRuntimeGenerationAbandonment"
    "TryContinueGenerationAbandonment|TryContinueZoneRuntimeGenerationAbandonment"
    "TryUnloadGeneration|TryUnloadZoneRuntimeGeneration"
    "TryBeginPendingCopyDrain|TryBeginZoneRuntimePendingCopyDrain"
    "TryDrainNextPendingCopy|TryDrainNextZoneRuntimePendingCopy"
    "TryFinishPendingCopyDrain|TryFinishZoneRuntimePendingCopyDrain"
    "TryResetTerminalReceipt|TryResetZoneRuntimeTerminalReceipt"
    "TryBorrowRegistryOwnership|TryGetZoneRuntimeGeneration")
set(_registry_method_adapter_pairs
    "TryBeginStandaloneRegistryOwnership|TryBeginStandalone"
    "FinishRegistryOwnership|Finish"
    "TryAddDatabaseUser4|TryAddDatabaseUser4"
    "TryAddDatabaseUsers4|TryAddDatabaseUsers4"
    "TryInternBoundedName|TryInternBoundedName"
    "TryReAddRetainedDefaultName|TryReAddRetainedDefaultName"
    "TryReAddRetainedDefaultNames|TryReAddRetainedDefaultNames"
    "TryTransferDatabaseUsers4To8|TryTransferDatabaseUsers4To8"
    "TryShutdownDatabaseUser8|TryShutdownDatabaseUser8")

list(LENGTH _table_method_adapter_pairs _table_pair_count)
list(LENGTH _registry_method_adapter_pairs _registry_pair_count)
if(NOT _table_pair_count EQUAL 30 OR NOT _registry_pair_count EQUAL 9)
    message(FATAL_ERROR
        "Runtime facade method/adapter maps must remain closed at 30 table "
        "and 9 ordinary registry wrappers")
endif()

set(_table_adapter_tokens)
foreach(_pair IN LISTS _table_method_adapter_pairs)
    string(REPLACE "|" ";" _fields "${_pair}")
    list(GET _fields 1 _raw_method)
    list(APPEND _table_adapter_tokens "${_raw_method}(")
endforeach()
set(_registry_adapter_tokens)
foreach(_pair IN LISTS _registry_method_adapter_pairs)
    string(REPLACE "|" ";" _fields "${_pair}")
    list(GET _fields 1 _raw_method)
    list(APPEND _registry_adapter_tokens
        "RegistryOwnershipCoordinatorFacade::${_raw_method}(")
endforeach()
list(APPEND _registry_adapter_tokens
    "RegistryOwnershipCoordinatorFacade::TryBorrow("
    "RegistryOwnershipCoordinatorFacade::TryBorrowActiveRuntimeCallback(")
list(REMOVE_DUPLICATES _table_adapter_tokens)
list(REMOVE_DUPLICATES _registry_adapter_tokens)
list(LENGTH _table_adapter_tokens _table_adapter_count)
list(LENGTH _registry_adapter_tokens _registry_adapter_count)
if(NOT _table_adapter_count EQUAL 30 OR NOT _registry_adapter_count EQUAL 11)
    message(FATAL_ERROR "Runtime facade raw adapter map contains duplicates")
endif()

canonicalize_runtime_facade_forwarding(_source _source_forwarding_canonical)
foreach(_adapter IN LISTS _table_adapter_tokens _registry_adapter_tokens)
    require_substring_count(
        _source_forwarding_canonical "${_adapter}" 1
        "one global use of each reviewed raw adapter")
endforeach()

# The caller span must be authenticated against the active stream authority,
# not merely against the facade's coarse authority spans. Stream ownership and
# retained table internals remain encapsulated by the table's reviewed private
# composition helper; the facade can only classify its completed status.
extract_slice(
    _source
    "ZoneRuntimeFacade::authenticatePendingCopyInspectionOutput("
    "RegistryOwnershipStatus\nZoneRuntimeFacade::authenticateRegistryOutputAccess("
    _pending_copy_output_authentication
    "pending-copy output authentication")
canonicalize_runtime_facade_forwarding(
    _pending_copy_output_authentication
    _pending_copy_output_authentication_canonical)
foreach(_marker IN ITEMS
    "ZoneRuntimeTable &table = ProductionZoneRuntimeTable();"
    "return completeTableOperation( table.authenticateExactPendingCopyOutput( physicalSlot, key, output, outputSize, outputAlignment));")
    require_substring_count(
        _pending_copy_output_authentication_canonical "${_marker}" 1
        "table-mediated exact pending-copy caller stream authentication")
endforeach()
require_substring_count(
    _source "authenticateExactPendingCopyOutput(" 1
    "single table-mediated pending-copy output authority use")
foreach(_forbidden_component IN ITEMS
    "authenticateExactPendingCopyRead("
    "entries_["
    "activeZoneStreamBinding_"
    "AuthenticateZoneStreamOutputSpan("
    "zone_stream_ownership")
    require_not_contains(
        _pending_copy_output_authentication "${_forbidden_component}"
        "facade cannot bypass table-owned pending-copy output authentication")
endforeach()
foreach(_forbidden_stream_authority IN ITEMS
    "activeZoneStreamBinding_"
    "AuthenticateZoneStreamOutputSpan("
    "zone_stream_ownership")
    require_not_contains(
        _source "${_forbidden_stream_authority}"
        "facade cannot acquire protected zone-stream authority")
endforeach()

# Pending-copy inspection is a read-only, failure-atomic facade boundary. The
# caller output is rejected before forwarding when it aliases managed PMem or
# protected authority. The actual output is then authenticated against the
# active stream binding. The table writes only a local candidate; facade post-
# authentication and canonical-value checks precede the sole caller write.
require_substring_count(
    _source "pmem_runtime::StorageIsOutsideManagedMemory(" 2
    "both pending-copy caller outputs remain outside managed PMem")

extract_runtime_facade_method_slice(
    _source "TryGetPendingCopyView" _pending_view_slice)
canonicalize_runtime_facade_forwarding(
    _pending_view_slice _pending_view_slice_canonical)
foreach(_marker IN ITEMS
    "authenticateKeyedTableOperationAccess(key)"
    "pmem_runtime::StorageIsOutsideManagedMemory( outView, sizeof(*outView))"
    "authoritySpanIsSeparated( outView, sizeof(*outView), alignof(ZoneRuntimePendingCopyView))"
    "AddressRangesAreDisjoint( &key, sizeof(key), outView, sizeof(*outView))"
    "authenticatePendingCopyInspectionOutput( physicalSlot, key, outView, sizeof(*outView), alignof(ZoneRuntimePendingCopyView))"
    "if (outputAuthentication != ZoneRuntimeTableStatus::Success) return outputAuthentication;"
    "ZoneRuntimePendingCopyView candidate{};"
    "completeTableOperation( TryGetZoneRuntimePendingCopyView( &ProductionZoneRuntimeTable(), physicalSlot, key, &candidate))"
    "if (status != ZoneRuntimeTableStatus::Success)"
    "if (!candidate || candidate.key != key)"
    "poisonAccess();"
    "*outView = candidate;")
    require_substring_count(
        _pending_view_slice_canonical "${_marker}" 1
        "failure-atomic pending-copy view publication")
endforeach()
string(FIND "${_pending_view_slice_canonical}"
    "pmem_runtime::StorageIsOutsideManagedMemory(" _pending_view_pmem)
string(FIND "${_pending_view_slice_canonical}"
    "authoritySpanIsSeparated(" _pending_view_authority)
string(FIND "${_pending_view_slice_canonical}"
    "AddressRangesAreDisjoint( &key, sizeof(key), outView, sizeof(*outView))"
    _pending_view_key_separation)
string(FIND "${_pending_view_slice_canonical}"
    "authenticatePendingCopyInspectionOutput(" _pending_view_stream_auth)
string(FIND "${_pending_view_slice_canonical}"
    "ZoneRuntimePendingCopyView candidate{};" _pending_view_candidate)
string(FIND "${_pending_view_slice_canonical}"
    "TryGetZoneRuntimePendingCopyView(" _pending_view_forward)
string(FIND "${_pending_view_slice_canonical}"
    "if (status != ZoneRuntimeTableStatus::Success)" _pending_view_status)
string(FIND "${_pending_view_slice_canonical}"
    "if (!candidate || candidate.key != key)" _pending_view_validation)
string(FIND "${_pending_view_slice_canonical}"
    "*outView = candidate;" _pending_view_publication)
if(_pending_view_pmem EQUAL -1 OR _pending_view_authority EQUAL -1
    OR _pending_view_key_separation EQUAL -1
    OR _pending_view_stream_auth EQUAL -1
    OR _pending_view_candidate EQUAL -1 OR _pending_view_forward EQUAL -1
    OR _pending_view_status EQUAL -1 OR _pending_view_validation EQUAL -1
    OR _pending_view_publication EQUAL -1
    OR NOT _pending_view_pmem LESS _pending_view_authority
    OR NOT _pending_view_authority LESS _pending_view_key_separation
    OR NOT _pending_view_key_separation LESS _pending_view_stream_auth
    OR NOT _pending_view_stream_auth LESS _pending_view_candidate
    OR NOT _pending_view_candidate LESS _pending_view_forward
    OR NOT _pending_view_forward LESS _pending_view_status
    OR NOT _pending_view_status LESS _pending_view_validation
    OR NOT _pending_view_validation LESS _pending_view_publication)
    message(FATAL_ERROR
        "Runtime facade pending-copy view publication ordering drifted")
endif()

extract_runtime_facade_method_slice(
    _source "TryReadPendingCopy" _pending_read_slice)
canonicalize_runtime_facade_forwarding(
    _pending_read_slice _pending_read_slice_canonical)
foreach(_marker IN ITEMS
    "authenticateKeyedTableOperationAccess(key)"
    "pmem_runtime::StorageIsOutsideManagedMemory( outRecord, sizeof(*outRecord))"
    "authoritySpanIsSeparated( outRecord, sizeof(*outRecord), alignof(zone_pending_copy::PendingCopyRecord))"
    "AddressRangesAreDisjoint( &key, sizeof(key), outRecord, sizeof(*outRecord))"
    "authenticatePendingCopyInspectionOutput( physicalSlot, key, outRecord, sizeof(*outRecord), alignof(zone_pending_copy::PendingCopyRecord))"
    "if (outputAuthentication != ZoneRuntimeTableStatus::Success) return outputAuthentication;"
    "zone_pending_copy::PendingCopyRecord candidate{};"
    "completeTableOperation( TryReadZoneRuntimePendingCopy( &ProductionZoneRuntimeTable(), physicalSlot, key, expectedRecordCount, ordinal, &candidate))"
    "if (status != ZoneRuntimeTableStatus::Success)"
    "candidate.key != key"
    "candidate.assetEntryIndex < zone_pending_copy::kFirstAssetEntryIndex"
    "candidate.assetEntryIndex > zone_pending_copy::kLastAssetEntryIndex"
    "candidate.reserved != 0"
    "poisonAccess();"
    "*outRecord = candidate;")
    require_substring_count(
        _pending_read_slice_canonical "${_marker}" 1
        "failure-atomic pending-copy record publication")
endforeach()
string(FIND "${_pending_read_slice_canonical}"
    "pmem_runtime::StorageIsOutsideManagedMemory(" _pending_read_pmem)
string(FIND "${_pending_read_slice_canonical}"
    "authoritySpanIsSeparated(" _pending_read_authority)
string(FIND "${_pending_read_slice_canonical}"
    "AddressRangesAreDisjoint( &key, sizeof(key), outRecord, sizeof(*outRecord))"
    _pending_read_key_separation)
string(FIND "${_pending_read_slice_canonical}"
    "authenticatePendingCopyInspectionOutput(" _pending_read_stream_auth)
string(FIND "${_pending_read_slice_canonical}"
    "zone_pending_copy::PendingCopyRecord candidate{};" _pending_read_candidate)
string(FIND "${_pending_read_slice_canonical}"
    "TryReadZoneRuntimePendingCopy(" _pending_read_forward)
string(FIND "${_pending_read_slice_canonical}"
    "if (status != ZoneRuntimeTableStatus::Success)" _pending_read_status)
string(FIND "${_pending_read_slice_canonical}"
    "candidate.key != key" _pending_read_validation)
string(FIND "${_pending_read_slice_canonical}"
    "*outRecord = candidate;" _pending_read_publication)
if(_pending_read_pmem EQUAL -1 OR _pending_read_authority EQUAL -1
    OR _pending_read_key_separation EQUAL -1
    OR _pending_read_stream_auth EQUAL -1
    OR _pending_read_candidate EQUAL -1 OR _pending_read_forward EQUAL -1
    OR _pending_read_status EQUAL -1 OR _pending_read_validation EQUAL -1
    OR _pending_read_publication EQUAL -1
    OR NOT _pending_read_pmem LESS _pending_read_authority
    OR NOT _pending_read_authority LESS _pending_read_key_separation
    OR NOT _pending_read_key_separation LESS _pending_read_stream_auth
    OR NOT _pending_read_stream_auth LESS _pending_read_candidate
    OR NOT _pending_read_candidate LESS _pending_read_forward
    OR NOT _pending_read_forward LESS _pending_read_status
    OR NOT _pending_read_status LESS _pending_read_validation
    OR NOT _pending_read_validation LESS _pending_read_publication)
    message(FATAL_ERROR
        "Runtime facade pending-copy record publication ordering drifted")
endif()

# Keep the input-key alias and exact stream-output checks independently
# mutation-tested. A local table candidate cannot recover either caller
# relationship after the forward.
function(runtime_facade_pending_output_guard_is_exact
    SLICE_VARIABLE OUTPUT_NAME ALIGNMENT_MARKER FORWARD_MARKER OUTPUT)
    canonicalize_runtime_facade_forwarding(
        ${SLICE_VARIABLE} _candidate)
    set(_valid TRUE)
    set(_pmem
        "pmem_runtime::StorageIsOutsideManagedMemory( ${OUTPUT_NAME}, sizeof(*${OUTPUT_NAME}))")
    set(_authority
        "authoritySpanIsSeparated( ${OUTPUT_NAME}, sizeof(*${OUTPUT_NAME}), ${ALIGNMENT_MARKER})")
    set(_key
        "AddressRangesAreDisjoint( &key, sizeof(key), ${OUTPUT_NAME}, sizeof(*${OUTPUT_NAME}))")
    set(_stream_auth
        "authenticatePendingCopyInspectionOutput( physicalSlot, key, ${OUTPUT_NAME}, sizeof(*${OUTPUT_NAME}), ${ALIGNMENT_MARKER})")
    foreach(_marker IN ITEMS
        "${_pmem}" "${_authority}" "${_key}" "${_stream_auth}")
        substring_count(_candidate "${_marker}" _count)
        if(NOT _count EQUAL 1)
            set(_valid FALSE)
        endif()
    endforeach()
    string(FIND "${_candidate}" "${_pmem}" _pmem_position)
    string(FIND "${_candidate}" "${_authority}" _authority_position)
    string(FIND "${_candidate}" "${_key}" _key_position)
    string(FIND "${_candidate}" "${_stream_auth}" _stream_auth_position)
    string(FIND "${_candidate}" "${FORWARD_MARKER}" _forward_position)
    if(_pmem_position EQUAL -1 OR _authority_position EQUAL -1
        OR _key_position EQUAL -1 OR _stream_auth_position EQUAL -1
        OR _forward_position EQUAL -1
        OR NOT _pmem_position LESS _authority_position
        OR NOT _authority_position LESS _key_position
        OR NOT _key_position LESS _stream_auth_position
        OR NOT _stream_auth_position LESS _forward_position)
        set(_valid FALSE)
    endif()
    set(${OUTPUT} ${_valid} PARENT_SCOPE)
endfunction()

runtime_facade_pending_output_guard_is_exact(
    _pending_view_slice
    outView
    "alignof(ZoneRuntimePendingCopyView)"
    "TryGetZoneRuntimePendingCopyView("
    _pending_view_guard_is_exact)
if(NOT _pending_view_guard_is_exact)
    message(FATAL_ERROR
        "Runtime facade pending-copy view caller/key guard drifted")
endif()
runtime_facade_pending_output_guard_is_exact(
    _pending_read_slice
    outRecord
    "alignof(zone_pending_copy::PendingCopyRecord)"
    "TryReadZoneRuntimePendingCopy("
    _pending_read_guard_is_exact)
if(NOT _pending_read_guard_is_exact)
    message(FATAL_ERROR
        "Runtime facade pending-copy record caller/key guard drifted")
endif()

set(_pending_view_missing_key_guard_fixture [=[
pmem_runtime::StorageIsOutsideManagedMemory(outView, sizeof(*outView));
authoritySpanIsSeparated(
    outView, sizeof(*outView), alignof(ZoneRuntimePendingCopyView));
authenticatePendingCopyInspectionOutput(
    physicalSlot, key, outView, sizeof(*outView),
    alignof(ZoneRuntimePendingCopyView));
TryGetZoneRuntimePendingCopyView(table, slot, key, &candidate);
]=])
runtime_facade_pending_output_guard_is_exact(
    _pending_view_missing_key_guard_fixture
    outView
    "alignof(ZoneRuntimePendingCopyView)"
    "TryGetZoneRuntimePendingCopyView("
    _malformed_pending_guard_was_accepted)
if(_malformed_pending_guard_was_accepted)
    message(FATAL_ERROR
        "Runtime facade pending-copy seal accepted a missing key/output separation")
endif()

set(_pending_read_late_key_guard_fixture [=[
pmem_runtime::StorageIsOutsideManagedMemory(outRecord, sizeof(*outRecord));
authoritySpanIsSeparated(
    outRecord, sizeof(*outRecord),
    alignof(zone_pending_copy::PendingCopyRecord));
TryReadZoneRuntimePendingCopy(table, slot, key, count, ordinal, &candidate);
AddressRangesAreDisjoint(
    &key, sizeof(key), outRecord, sizeof(*outRecord));
authenticatePendingCopyInspectionOutput(
    physicalSlot, key, outRecord, sizeof(*outRecord),
    alignof(zone_pending_copy::PendingCopyRecord));
]=])
runtime_facade_pending_output_guard_is_exact(
    _pending_read_late_key_guard_fixture
    outRecord
    "alignof(zone_pending_copy::PendingCopyRecord)"
    "TryReadZoneRuntimePendingCopy("
    _malformed_pending_guard_was_accepted)
if(_malformed_pending_guard_was_accepted)
    message(FATAL_ERROR
        "Runtime facade pending-copy seal accepted a post-forward key guard")
endif()

set(_pending_view_missing_stream_auth_fixture [=[
pmem_runtime::StorageIsOutsideManagedMemory(outView, sizeof(*outView));
authoritySpanIsSeparated(
    outView, sizeof(*outView), alignof(ZoneRuntimePendingCopyView));
AddressRangesAreDisjoint(
    &key, sizeof(key), outView, sizeof(*outView));
TryGetZoneRuntimePendingCopyView(table, slot, key, &candidate);
]=])
runtime_facade_pending_output_guard_is_exact(
    _pending_view_missing_stream_auth_fixture
    outView
    "alignof(ZoneRuntimePendingCopyView)"
    "TryGetZoneRuntimePendingCopyView("
    _malformed_pending_guard_was_accepted)
if(_malformed_pending_guard_was_accepted)
    message(FATAL_ERROR
        "Runtime facade pending-copy seal accepted missing stream authentication")
endif()

set(_pending_read_late_stream_auth_fixture [=[
pmem_runtime::StorageIsOutsideManagedMemory(outRecord, sizeof(*outRecord));
authoritySpanIsSeparated(
    outRecord, sizeof(*outRecord),
    alignof(zone_pending_copy::PendingCopyRecord));
AddressRangesAreDisjoint(
    &key, sizeof(key), outRecord, sizeof(*outRecord));
TryReadZoneRuntimePendingCopy(table, slot, key, count, ordinal, &candidate);
authenticatePendingCopyInspectionOutput(
    physicalSlot, key, outRecord, sizeof(*outRecord),
    alignof(zone_pending_copy::PendingCopyRecord));
]=])
runtime_facade_pending_output_guard_is_exact(
    _pending_read_late_stream_auth_fixture
    outRecord
    "alignof(zone_pending_copy::PendingCopyRecord)"
    "TryReadZoneRuntimePendingCopy("
    _malformed_pending_guard_was_accepted)
if(_malformed_pending_guard_was_accepted)
    message(FATAL_ERROR
        "Runtime facade pending-copy seal accepted post-forward stream authentication")
endif()

foreach(_forbidden_pending_authority IN ITEMS
    "zone_pending_copy::TryReadPendingCopyRecord("
    "zone_pending_copy::AuthenticatePendingCopyAdmissionReceipt("
    "zone_pending_copy::AuthenticatePendingCopyLedgerDescriptors("
    "PendingCopyAdmissionReceipt"
    "PendingCopyLedger")
    require_not_contains(
        _source "${_forbidden_pending_authority}"
        "facade pending-copy inspection cannot acquire ledger or receipt authority")
endforeach()

# Ordinary registry borrowing is deliberately incapable of falling back to a
# callback capability on Busy. It accepts only a successful exact table view
# and then uses the ordinary coordinator adapter.
extract_runtime_facade_method_slice(
    _source "TryBorrowRegistryOwnership" _borrow_slice)
canonicalize_runtime_facade_forwarding(
    _borrow_slice _borrow_slice_canonical)
foreach(_adapter IN LISTS _registry_adapter_tokens)
    if(_adapter STREQUAL
        "RegistryOwnershipCoordinatorFacade::TryBorrow(")
        set(_expected_count 1)
    else()
        set(_expected_count 0)
    endif()
    require_substring_count(
        _borrow_slice_canonical "${_adapter}" ${_expected_count}
        "ordinary borrow's closed coordinator adapter set")
endforeach()
foreach(_marker IN ITEMS
    "authenticateTableOperationAccess()"
    "authoritySpanIsSeparated( &key, sizeof(key), alignof(zone_load::ZoneLoadContextKey))"
    "TryGetZoneRuntimeGeneration( &table, physicalSlot, key, &view)"
    "tableStatus != ZoneRuntimeTableStatus::Success"
    "zone_slots::IsUsableZoneSlot(physicalSlot)"
    "view.entry != &table.entries_[physicalSlot]"
    "RegistryOwnershipCoordinatorFacade::TryBorrow(view.entry->scriptStringOwnership(), key)")
    require_substring_count(
        _borrow_slice_canonical "${_marker}" 1
        "ordinary borrow exact-key and bounds gate")
endforeach()
foreach(_forbidden IN ITEMS
    "tableStatus == ZoneRuntimeTableStatus::Busy"
    "authenticateExactRegistryLifecycleCallback("
    "restoreExactRegistryLifecycleCallback("
    "TryBorrowActiveRuntimeCallback(")
    require_not_contains(
        _borrow_slice_canonical "${_forbidden}"
        "ordinary Busy cannot acquire callback authority")
endforeach()

# Callback borrowing is a separate one-shot surface. Full context
# authentication and exact marker consumption precede the narrower coordinator
# adapter. Only a recoverable coordinator Busy may reauthenticate the facade
# and structurally restore that same marker for the same synchronous callback.
extract_runtime_facade_method_slice(
    _source "TryBorrowRegistryOwnershipFromCallback" _callback_borrow_slice)
canonicalize_runtime_facade_forwarding(
    _callback_borrow_slice _callback_borrow_slice_canonical)
foreach(_adapter IN LISTS _registry_adapter_tokens)
    if(_adapter STREQUAL
        "RegistryOwnershipCoordinatorFacade::TryBorrowActiveRuntimeCallback(")
        set(_expected_count 1)
    else()
        set(_expected_count 0)
    endif()
    require_substring_count(
        _callback_borrow_slice_canonical "${_adapter}" ${_expected_count}
        "callback borrow's closed coordinator adapter set")
endforeach()
foreach(_marker IN ITEMS
    "ZoneRuntimeCallbackContextOwner::TryAuthenticate( context, key, ZoneRuntimeCallbackContextPhase::Bound)"
    "zone_slots::IsUsableZoneSlot(key.slot)"
    "table.authenticateExactRegistryLifecycleCallback( context, key.slot, key)"
    "RegistryOwnershipCoordinatorFacade::TryBorrowActiveRuntimeCallback( table.entries_[key.slot].scriptStringOwnership(), key)"
    "callbackBorrow != RegistryOwnershipStatus::Busy"
    "authenticateTableOperationAccess() != ZoneRuntimeTableStatus::Success"
    "table.restoreExactRegistryLifecycleCallback( context, key.slot, key, ZoneRuntimeTable::ValidationDepth::StructuralOnly)")
    require_substring_count(
        _callback_borrow_slice_canonical "${_marker}" 1
        "callback borrow exact capability and Busy restoration")
endforeach()
require_substring_count(
    _callback_borrow_slice_canonical "return callbackBorrow;" 2
    "callback result exits only before or after exact Busy restoration")
foreach(_forbidden IN ITEMS
    "TryGetZoneRuntimeGeneration("
    "RegistryOwnershipCoordinatorFacade::TryBorrow("
    "physicalSlot")
    require_not_contains(
        _callback_borrow_slice_canonical "${_forbidden}"
        "callback borrow cannot use ordinary lookup authority")
endforeach()

string(FIND "${_callback_borrow_slice_canonical}"
    "ZoneRuntimeCallbackContextOwner::TryAuthenticate(" _callback_context_auth)
string(FIND "${_callback_borrow_slice_canonical}"
    "zone_slots::IsUsableZoneSlot(key.slot)" _callback_slot_gate)
string(FIND "${_callback_borrow_slice_canonical}"
    "table.authenticateExactRegistryLifecycleCallback(" _callback_marker_consume)
string(FIND "${_callback_borrow_slice_canonical}"
    "RegistryOwnershipCoordinatorFacade::TryBorrowActiveRuntimeCallback("
    _callback_coordinator_borrow)
string(FIND "${_callback_borrow_slice_canonical}"
    "callbackBorrow != RegistryOwnershipStatus::Busy" _callback_busy_gate)
string(FIND "${_callback_borrow_slice_canonical}"
    "authenticateTableOperationAccess() != ZoneRuntimeTableStatus::Success"
    _callback_restore_auth)
string(FIND "${_callback_borrow_slice_canonical}"
    "table.restoreExactRegistryLifecycleCallback(" _callback_restore)
if(_callback_context_auth EQUAL -1 OR _callback_slot_gate EQUAL -1
    OR _callback_marker_consume EQUAL -1
    OR _callback_coordinator_borrow EQUAL -1
    OR _callback_busy_gate EQUAL -1 OR _callback_restore_auth EQUAL -1
    OR _callback_restore EQUAL -1
    OR NOT _callback_context_auth LESS _callback_slot_gate
    OR NOT _callback_slot_gate LESS _callback_marker_consume
    OR NOT _callback_marker_consume LESS _callback_coordinator_borrow
    OR NOT _callback_coordinator_borrow LESS _callback_busy_gate
    OR NOT _callback_busy_gate LESS _callback_restore_auth
    OR NOT _callback_restore_auth LESS _callback_restore)
    message(FATAL_ERROR
        "Runtime facade callback borrow ordering or exclusivity drifted")
endif()

foreach(_pair IN LISTS _table_method_adapter_pairs)
    string(REPLACE "|" ";" _fields "${_pair}")
    list(GET _fields 0 _public_method)
    list(GET _fields 1 _raw_method)
    extract_runtime_facade_method_slice(
        _source "${_public_method}" _method_slice)
    runtime_facade_adapter_set_is_exact(
        _method_slice "${_raw_method}(" _table_adapter_tokens _pair_is_exact)
    if(NOT _pair_is_exact)
        message(FATAL_ERROR
            "Runtime facade table wrapper ${_public_method} must call only "
            "${_raw_method}, exactly once")
    endif()
    if(_public_method STREQUAL "TryUnloadGeneration")
        canonicalize_runtime_facade_forwarding(
            _method_slice _unload_slice_canonical)
        require_substring_count(
            _unload_slice_canonical
            "TryUnloadZoneRuntimeGeneration( &ProductionZoneRuntimeTable(), physicalSlot, key)"
            1
            "unload remains the authenticated three-argument adapter")
    endif()
endforeach()

foreach(_pair IN LISTS _registry_method_adapter_pairs)
    string(REPLACE "|" ";" _fields "${_pair}")
    list(GET _fields 0 _public_method)
    list(GET _fields 1 _raw_method)
    extract_runtime_facade_method_slice(
        _source "${_public_method}" _method_slice)
    set(_expected_adapter
        "RegistryOwnershipCoordinatorFacade::${_raw_method}(")
    runtime_facade_adapter_set_is_exact(
        _method_slice "${_expected_adapter}"
        _registry_adapter_tokens _pair_is_exact)
    if(NOT _pair_is_exact)
        message(FATAL_ERROR
            "Runtime facade registry wrapper ${_public_method} must call only "
            "${_raw_method}, exactly once")
    endif()
endforeach()

# Adversarial fixtures keep the shared predicate honest: substituting a sibling
# adapter or appending a second adapter must fail for both forwarding families.
set(_table_swap_fixture
    "return TryBindZoneRuntimeGenerationCallbacks(table, slot, key, callbacks);")
runtime_facade_adapter_set_is_exact(
    _table_swap_fixture "TryClaimZoneRuntimeGeneration("
    _table_adapter_tokens _fixture_is_exact)
if(_fixture_is_exact)
    message(FATAL_ERROR "Runtime facade table-pair seal missed an adapter swap")
endif()
set(_table_extra_fixture
    "TryClaimZoneRuntimeGeneration(table, slot, key); TryGetZoneRuntimeGeneration(table, slot, key, view);")
runtime_facade_adapter_set_is_exact(
    _table_extra_fixture "TryClaimZoneRuntimeGeneration("
    _table_adapter_tokens _fixture_is_exact)
if(_fixture_is_exact)
    message(FATAL_ERROR "Runtime facade table-pair seal missed an extra forward")
endif()
set(_registry_swap_fixture
    "RegistryOwnershipCoordinatorFacade:: Finish();")
runtime_facade_adapter_set_is_exact(
    _registry_swap_fixture
    "RegistryOwnershipCoordinatorFacade::TryBeginStandalone("
    _registry_adapter_tokens _fixture_is_exact)
if(_fixture_is_exact)
    message(FATAL_ERROR "Runtime facade registry-pair seal missed an adapter swap")
endif()
set(_registry_extra_fixture
    "RegistryOwnershipCoordinatorFacade:: TryBeginStandalone(); RegistryOwnershipCoordinatorFacade:: Finish();")
runtime_facade_adapter_set_is_exact(
    _registry_extra_fixture
    "RegistryOwnershipCoordinatorFacade::TryBeginStandalone("
    _registry_adapter_tokens _fixture_is_exact)
if(_fixture_is_exact)
    message(FATAL_ERROR
        "Runtime facade registry-pair seal missed an extra forward")
endif()

foreach(_marker IN ITEMS
    "s_runtimeStateMirror"
    "s_activeThreadIdentityMirror"
    "s_activeSerialMirror"
    "s_nextSerialMirror"
    "s_retainedThreadStateMirror"
    "s_retainedThreadIdentityMirror"
    "s_retainedSerialMirror"
    "WritableOutputIsSeparateFromBoundary"
    "RegistryOwnershipCoordinatorFacade:: WritableOutputIsSeparated"
    "ZoneRuntimeCallbackContextOwner::SpanIsSeparated"
    "IsKnownTableStatus"
    "IsKnownRegistryStatus"
    "table.authenticateExactEntry(physicalSlot, key)"
    "table.authenticateExactRegistryLifecycleCallback( context, key.slot, key)"
    "table.restoreExactRegistryLifecycleCallback( context, key.slot, key, ZoneRuntimeTable::ValidationDepth::StructuralOnly)"
    "table.entries_[physicalSlot].generationBindingPristine()"
    "completeTableOperation"
    "RegistryOwnershipCoordinatorFacade::ValidateInactive()"
    "RegistryOwnershipCoordinatorFacade:: ValidateActive()"
    "table.validateReleaseSafety()")
    require_contains(
        _source_normalized "${_marker}" "mirrored fail-closed boundary")
endforeach()

foreach(_mirror_pair IN ITEMS
    "s_activeThreadIdentityMirror == s_retainedThreadIdentityMirror"
    "s_activeSerialMirror == s_retainedSerialMirror")
    require_substring_count(
        _source_normalized "${_mirror_pair}" 1
        "exact global-to-thread mirror pairing")
endforeach()
foreach(_cross_pair IN ITEMS
    "s_activeThreadIdentityMirror == s_retainedThreadIdentity &&"
    "s_activeSerialMirror == s_retainedSerial &&")
    require_not_contains(
        _source_normalized "${_cross_pair}"
        "no global-mirror to primary-thread comparison")
endforeach()

foreach(_composite_forward IN ITEMS
    "TryBeginScriptStringOwnership|TryBeginZoneRuntimeScriptStringOwnership"
    "TryStageScriptString|TryStageZoneRuntimeScriptString"
    "TrySealScriptStrings|TrySealZoneRuntimeScriptStrings"
    "TryBeginScriptStringTransfer|TryBeginZoneRuntimeScriptStringTransfer"
    "TryTransferNextScriptString|TryTransferNextZoneRuntimeScriptString"
    "TryPrepareScriptStringCommit|TryPrepareZoneRuntimeScriptStringCommit")
    string(REPLACE "|" ";" _composite_fields "${_composite_forward}")
    list(GET _composite_fields 0 _facade_method)
    list(GET _composite_fields 1 _raw_method)
    extract_slice(
        _source
        "ZoneRuntimeFacade::${_facade_method}("
        "${_raw_method}("
        _composite_slice
        "composite-only script adapter ${_facade_method}")
    normalize_space("${_composite_slice}" _composite_slice_normalized)
    require_contains(
        _composite_slice_normalized
        "authenticateCompositeTableOperationAccess(physicalSlot, key)"
        "dual-mode script adapter cannot enter legacy compatibility")
endforeach()

# The exact nine stream payload ranges become retained writable targets after
# binding. Keep their protected-authority preflight after the descriptor-array
# guard and before the raw table adapter. Invalid null/count/layout inputs still
# forward so the lower adapter retains its established status precedence.
extract_runtime_facade_method_slice(
    _source "TryBindStreams" _stream_bind_slice)
normalize_space("${_stream_bind_slice}" _stream_bind_slice_normalized)
set(_stream_payload_gate
    "zoneIdentity && blocks && blockCount == relocation::kBlockCount")
set(_stream_payload_span
    "authoritySpanIsSeparated( reinterpret_cast<const void *>(blocks[index].base), blocks[index].size, 1)")
require_substring_count(
    _stream_bind_slice_normalized "${_stream_payload_gate}" 1
    "exact readable stream-payload gate")
require_substring_count(
    _stream_bind_slice_normalized "${_stream_payload_span}" 1
    "each retained stream payload is authority-separated")
require_substring_count(
    _stream_bind_slice_normalized "authoritySpanIsSeparated(" 3
    "zone, descriptor-array, and payload authority guards")
string(FIND
    "${_stream_bind_slice_normalized}"
    "authoritySpanIsSeparated( blocks, blockCount * sizeof(*blocks), alignof(relocation::BlockView))"
    _stream_descriptor_guard)
string(FIND
    "${_stream_bind_slice_normalized}"
    "${_stream_payload_gate}" _stream_payload_gate_position)
string(FIND
    "${_stream_bind_slice_normalized}"
    "${_stream_payload_span}" _stream_payload_span_position)
string(FIND
    "${_stream_bind_slice_normalized}"
    "TryBindZoneRuntimeStreams(" _stream_forward)
if(_stream_descriptor_guard EQUAL -1
    OR _stream_payload_gate_position EQUAL -1
    OR _stream_payload_span_position EQUAL -1
    OR _stream_forward EQUAL -1
    OR NOT _stream_descriptor_guard LESS _stream_payload_gate_position
    OR NOT _stream_payload_gate_position LESS _stream_payload_span_position
    OR NOT _stream_payload_span_position LESS _stream_forward)
    message(FATAL_ERROR
        "Runtime facade stream-payload authority ordering drifted")
endif()

foreach(_registry_output_method IN ITEMS
    TryAddDatabaseUsers4
    TryInternBoundedName
    TryReAddRetainedDefaultNames)
    extract_slice(
        _source
        "ZoneRuntimeFacade::${_registry_output_method}("
        "return RunRegistryOperation("
        _registry_output_slice
        "authenticated registry output ${_registry_output_method}")
    normalize_space(
        "${_registry_output_slice}" _registry_output_slice_normalized)
    require_contains(
        _registry_output_slice_normalized
        "authenticateRegistryOutputAccess()"
        "active registry authority precedes output-alias validation")
endforeach()

# Aggregate registry inputs are read only after the coordinator has published
# its Operating phase and begun a private OwnershipBatch. Freeze the exact
# output-first, readable-count-only preflight so those reads cannot alias and
# derive facade/table/coordinator/hash authority. The bounded predicates also
# make both pointer-array multiplications representable on i386 without
# changing the lower adapter's null/zero/over-cap status precedence.
function(runtime_facade_registry_input_guard_is_exact
    SLICE_VARIABLE OUTPUT_MARKER GATE_MARKER SPAN_MARKER INPUT_PREFIX OUTPUT)
    normalize_space("${${SLICE_VARIABLE}}" _candidate)
    set(_valid TRUE)
    foreach(_expected IN ITEMS
        "authenticateRegistryOutputAccess()"
        "${OUTPUT_MARKER}"
        "${GATE_MARKER}"
        "${SPAN_MARKER}"
        "return RunRegistryOperation(")
        substring_count(_candidate "${_expected}" _count)
        if(NOT _count EQUAL 1)
            set(_valid FALSE)
        endif()
    endforeach()
    substring_count(_candidate "${INPUT_PREFIX}" _input_count)
    if(NOT _input_count EQUAL 1)
        set(_valid FALSE)
    endif()
    string(FIND
        "${_candidate}" "authenticateRegistryOutputAccess()" _authentication)
    string(FIND "${_candidate}" "${OUTPUT_MARKER}" _output_guard)
    string(FIND "${_candidate}" "${GATE_MARKER}" _read_gate)
    string(FIND "${_candidate}" "${SPAN_MARKER}" _input_span)
    string(FIND "${_candidate}" "return RunRegistryOperation(" _forward)
    if(_authentication EQUAL -1 OR _output_guard EQUAL -1
        OR _read_gate EQUAL -1 OR _input_span EQUAL -1 OR _forward EQUAL -1
        OR NOT _authentication LESS _output_guard
        OR NOT _output_guard LESS _read_gate
        OR NOT _read_gate LESS _input_span
        OR NOT _input_span LESS _forward)
        set(_valid FALSE)
    endif()
    set(${OUTPUT} ${_valid} PARENT_SCOPE)
endfunction()

extract_runtime_facade_method_slice(
    _source "TryAddDatabaseUsers4" _registry_bulk_add_slice)
set(_bulk_output_marker
    "authoritySpanIsSeparated( outResult, sizeof(*outResult), alignof(registry_ownership::RegistryOwnershipBulkResult))")
set(_bulk_add_gate_marker
    "outResult && stringIds && count != 0 && count <= script_string::kRegistryOwnershipBulkCapacity")
set(_bulk_add_span_marker
    "authoritySpanIsSeparated( stringIds, static_cast<std::size_t>(count) * sizeof(*stringIds), alignof(std::uint32_t))")
runtime_facade_registry_input_guard_is_exact(
    _registry_bulk_add_slice
    "${_bulk_output_marker}"
    "${_bulk_add_gate_marker}"
    "${_bulk_add_span_marker}"
    "authoritySpanIsSeparated( stringIds,"
    _bulk_add_guard_is_exact)
if(NOT _bulk_add_guard_is_exact)
    message(FATAL_ERROR
        "Runtime facade bulk-add input authority guard drifted")
endif()

extract_runtime_facade_method_slice(
    _source "TryInternBoundedName" _registry_intern_slice)
set(_intern_output_marker
    "authoritySpanIsSeparated( outName, sizeof(*outName), alignof(registry_ownership::RegistryOwnershipName))")
set(_intern_gate_marker
    "outName && bytes && byteCount != 0 && byteCount <= UINT32_C(65531)")
set(_intern_span_marker
    "authoritySpanIsSeparated( bytes, static_cast<std::size_t>(byteCount), 1)")
runtime_facade_registry_input_guard_is_exact(
    _registry_intern_slice
    "${_intern_output_marker}"
    "${_intern_gate_marker}"
    "${_intern_span_marker}"
    "authoritySpanIsSeparated( bytes,"
    _intern_guard_is_exact)
if(NOT _intern_guard_is_exact)
    message(FATAL_ERROR
        "Runtime facade bounded-name input authority guard drifted")
endif()

extract_runtime_facade_method_slice(
    _source "TryReAddRetainedDefaultNames" _registry_bulk_readd_slice)
set(_bulk_readd_gate_marker
    "outResult && names && count != 0 && count <= script_string::kRegistryOwnershipBulkCapacity")
set(_bulk_readd_span_marker
    "authoritySpanIsSeparated( names, static_cast<std::size_t>(count) * sizeof(*names), alignof(const char *))")
runtime_facade_registry_input_guard_is_exact(
    _registry_bulk_readd_slice
    "${_bulk_output_marker}"
    "${_bulk_readd_gate_marker}"
    "${_bulk_readd_span_marker}"
    "authoritySpanIsSeparated( names,"
    _bulk_readd_guard_is_exact)
if(NOT _bulk_readd_guard_is_exact)
    message(FATAL_ERROR
        "Runtime facade retained-name array authority guard drifted")
endif()

foreach(_bulk_registry_slice IN ITEMS
    _registry_bulk_add_slice
    _registry_bulk_readd_slice)
    require_not_contains(
        ${_bulk_registry_slice} "numeric_limits<std::size_t>"
        "bounded registry count must precede representable multiplication")
    require_not_contains(
        ${_bulk_registry_slice} "SIZE_MAX"
        "bounded registry count must preserve lower capacity status")
endforeach()
require_not_contains(
    _registry_bulk_readd_slice "names["
    "canonical retained pointees remain lower-adapter identity authority")
extract_runtime_facade_method_slice(
    _source "TryReAddRetainedDefaultName" _registry_scalar_readd_slice)
normalize_space(
    "${_registry_scalar_readd_slice}"
    _registry_scalar_readd_slice_normalized)
foreach(_marker IN ITEMS
    "authenticateRegistryOutputAccess()"
    "if (name && !authoritySpanIsSeparated(name, 1, 1))"
    "RegistryOwnershipCoordinatorFacade:: TryReAddRetainedDefaultName(name)")
    require_substring_count(
        _registry_scalar_readd_slice_normalized "${_marker}" 1
        "scalar retained identity separates its readable byte")
endforeach()
string(FIND "${_registry_scalar_readd_slice_normalized}"
    "authenticateRegistryOutputAccess()" _scalar_registry_auth)
string(FIND "${_registry_scalar_readd_slice_normalized}"
    "authoritySpanIsSeparated(name, 1, 1)" _scalar_registry_span)
string(FIND "${_registry_scalar_readd_slice_normalized}"
    "TryReAddRetainedDefaultName(name)" _scalar_registry_forward)
if(_scalar_registry_auth EQUAL -1 OR _scalar_registry_span EQUAL -1
    OR _scalar_registry_forward EQUAL -1
    OR NOT _scalar_registry_auth LESS _scalar_registry_span
    OR NOT _scalar_registry_span LESS _scalar_registry_forward)
    message(FATAL_ERROR
        "Runtime facade scalar retained-name authority ordering drifted")
endif()

# These malformed synthetic bodies must not satisfy the shared predicate. One
# drops the capacity gate (which would permit i386 multiplication wrap and mask
# CapacityExceeded); the other checks only pointer width rather than the exact
# caller span.
set(_missing_capacity_guard_fixture [=[
authenticateRegistryOutputAccess();
authoritySpanIsSeparated(
    outResult, sizeof(*outResult),
    alignof(registry_ownership::RegistryOwnershipBulkResult));
if (outResult && stringIds && count != 0
    && !authoritySpanIsSeparated(
        stringIds, static_cast<std::size_t>(count) * sizeof(*stringIds),
        alignof(std::uint32_t))) {}
return RunRegistryOperation([]() noexcept {});
]=])
runtime_facade_registry_input_guard_is_exact(
    _missing_capacity_guard_fixture
    "${_bulk_output_marker}"
    "${_bulk_add_gate_marker}"
    "${_bulk_add_span_marker}"
    "authoritySpanIsSeparated( stringIds,"
    _malformed_guard_was_accepted)
if(_malformed_guard_was_accepted)
    message(FATAL_ERROR
        "Runtime facade input seal accepted an unbounded multiplication")
endif()
set(_pointer_width_guard_fixture [=[
authenticateRegistryOutputAccess();
authoritySpanIsSeparated(
    outResult, sizeof(*outResult),
    alignof(registry_ownership::RegistryOwnershipBulkResult));
if (outResult && names && count != 0
    && count <= script_string::kRegistryOwnershipBulkCapacity
    && !authoritySpanIsSeparated(
        names, sizeof(names), alignof(const char *))) {}
return RunRegistryOperation([]() noexcept {});
]=])
runtime_facade_registry_input_guard_is_exact(
    _pointer_width_guard_fixture
    "${_bulk_output_marker}"
    "${_bulk_readd_gate_marker}"
    "${_bulk_readd_span_marker}"
    "authoritySpanIsSeparated( names,"
    _malformed_guard_was_accepted)
if(_malformed_guard_was_accepted)
    message(FATAL_ERROR
        "Runtime facade input seal accepted a pointer-width-only span")
endif()
require_contains(
    _source_normalized
    "view.entry != &table.entries_[physicalSlot]"
    "borrowed registry view must name the exact stable table entry")

string(FIND "${_source}" "RegistryOwnershipCoordinatorFacade::ValidateInactive()" _registry_check)
string(FIND "${_source}" "table.validateReleaseSafety()" _table_check)
string(FIND "${_source}" "Sys_UnlockWrite(&s_runtimeSerializer);" _final_unlock REVERSE)
if(_registry_check EQUAL -1 OR _table_check EQUAL -1 OR _final_unlock EQUAL -1
    OR NOT _registry_check LESS _table_check
    OR NOT _table_check LESS _final_unlock)
    message(FATAL_ERROR
        "Runtime facade release ordering must authenticate registry, table, then unlock")
endif()

foreach(_marker IN ITEMS
    "Sys_LockWrite("
    "Sys_Sleep("
    "std::construct_at("
    "operator new"
    "malloc("
    "calloc("
    "realloc("
    "Com_Error("
    "Com_Print"
    "std::abort("
    "std::exit("
    "longjmp("
    "throw ")
    require_not_contains(_source "${_marker}" "allocation/report/block/nonlocal-exit free")
endforeach()
require_not_contains(_source "CRITSECT_" "no fixed critical-section enum enrollment")

# Freeze the literal facade-to-registry integration target as a macro-off
# composition of the real production translation units. This prevents a test
# double, omitted boundary, or private TestAccess grant from satisfying the
# callback-borrow integration gate.
normalize_space("${_tests_cmake}" _tests_cmake_normalized)
normalize_space("${_integration_fixture}" _integration_fixture_normalized)
normalize_space("${_ci}" _ci_normalized)
extract_slice(
    _tests_cmake_normalized
    "add_executable(kisakcod-db-zone-runtime-stable-context-integration-tests"
    "add_executable(kisakcod-fx-archive-disk32-tests"
    _stable_integration_registration
    "stable-context integration registration")
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
    "$<TARGET_OBJECTS:kisakcod-fx-fastfile-zone-adapter-disk32-subject>"
    "$<TARGET_OBJECTS:kisakcod-fx-fastfile-native-arena-subject>"
    "$<TARGET_OBJECTS:kisakcod-fx-fastfile-native-disk32-subject>"
    "$<TARGET_OBJECTS:kisakcod-fx-fastfile-impact-native-disk32-subject>"
    "SYSTEM PRIVATE \${SRC_DIR}"
    "PRIVATE cxx_std_20"
    "PRIVATE KISAK_MP)"
    "PRIVATE Threads::Threads)"
    "PRIVATE /wd4702)"
    "PRIVATE \"LINKER:/STACK:8388608\""
    "NAME database-zone-runtime-stable-context-integration"
    "NAME database-zone-runtime-stable-context-forgotten-finish"
    "--omit-finish"
    "database-zone-runtime-stable-context-integration database-zone-runtime-stable-context-forgotten-finish PROPERTIES TIMEOUT 30")
    require_contains(
        _stable_integration_registration "${_marker}"
        "macro-off stable facade source closure")
endforeach()
require_substring_count(
    _stable_integration_registration "\${SRC_DIR}/" 19
    "exact stable integration production source closure")
require_substring_count(
    _stable_integration_registration "$<TARGET_OBJECTS:" 4
    "exact stable integration object-source closure")
require_substring_count(
    _stable_integration_registration ".cpp" 20
    "one fixture plus nineteen exact production translation units")
require_substring_count(
    _stable_integration_registration "target_compile_definitions(" 1
    "one stable integration compile-definition block")
require_not_contains(
    _stable_integration_registration "_TESTING"
    "stable integration cannot enable a private TestAccess capability")
require_not_contains(
    _stable_integration_registration "winmm"
    "stable integration owns deterministic platform seams")

foreach(_marker IN ITEMS
    "ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback( context, key)"
    "RegistryOwnershipStatus::Busy ? ZoneScriptStringUnpublishStatus::Retry"
    "ZoneRuntimeFacade::FinishRegistryOwnership()"
    "ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback( g_callbackProbe.context, fixture.key)"
    "ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback( g_callbackProbe.context, staleKey)"
    "--omit-finish")
    require_contains(
        _integration_fixture_normalized "${_marker}"
        "real stable facade callback-borrow coverage")
endforeach()
require_substring_count(
    _integration_fixture_normalized
    "ZoneRuntimeFacade::TryBorrowRegistryOwnershipFromCallback" 5
    "closed callback-only facade borrow coverage")
foreach(_marker IN ITEMS
    "kisakcod-db-zone-runtime-stable-context-integration-tests"
    "database-zone-runtime-stable-context-(integration|forgotten-finish)")
    require_contains(
        _ci_normalized "${_marker}"
        "Windows x86 stable facade integration gate")
endforeach()

# The prerequisite remains production-neutral. Traverse every file below src,
# including generated inputs and files without a C/C++ extension. Only the
# facade implementation and the two exact private friend declarations may name
# the facade before the atomic loader cutover.
file(GLOB_RECURSE _production_sources
    LIST_DIRECTORIES FALSE "${SOURCE_ROOT}/src/*")
foreach(_non_extension_sentinel IN ITEMS
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.am"
    "${SOURCE_ROOT}/src/groupvoice/speex/Makefile.in")
    list(FIND _production_sources "${_non_extension_sentinel}" _sentinel_index)
    if(_sentinel_index EQUAL -1)
        message(FATAL_ERROR
            "Runtime facade seal lost extension-independent traversal: "
            "${_non_extension_sentinel}")
    endif()
endforeach()

set(_runtime_facade_whole_file_sources
    "${_header_path}"
    "${_source_path}")
foreach(_approved_path IN ITEMS
    ${_runtime_facade_whole_file_sources}
    "${_coordinator_header_path}"
    "${_table_header_path}")
    list(FIND _production_sources "${_approved_path}" _approved_index)
    if(_approved_index EQUAL -1)
        message(FATAL_ERROR
            "Runtime facade allowlist input escaped traversal: ${_approved_path}")
    endif()
endforeach()

string(ASCII 92 _runtime_facade_backslash)
string(ASCII 13 _runtime_facade_carriage_return)
string(ASCII 10 _runtime_facade_line_feed)
function(normalize_runtime_facade_phase2 VARIABLE OUTPUT)
    set(_spliced "${${VARIABLE}}")
    string(REPLACE
        "${_runtime_facade_backslash}${_runtime_facade_carriage_return}${_runtime_facade_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_runtime_facade_backslash}${_runtime_facade_line_feed}"
        "" _spliced "${_spliced}")
    string(REPLACE
        "${_runtime_facade_backslash}${_runtime_facade_carriage_return}"
        "" _spliced "${_spliced}")
    set(${OUTPUT} "${_spliced}" PARENT_SCOPE)
endfunction()

# The test-access macro is a build-system capability, so scan every supported
# build-control format rather than one production source manifest. The only
# permitted grant is the exact private definition on the facade unit-test
# executable; portable macro-off and production targets must remain sealed.
set(_expected_runtime_facade_test_definition
    "target_compile_definitions( kisakcod-db-zone-runtime-facade-tests PRIVATE KISAK_MP KISAK_DB_ZONE_RUNTIME_FACADE_TESTING=1)")
function(runtime_facade_build_control_is_sealed PATH VARIABLE OUTPUT)
    normalize_runtime_facade_phase2(${VARIABLE} _candidate_phase2)
    normalize_space("${_candidate_phase2}" _candidate_normalized)
    substring_count(
        _candidate_normalized
        "KISAK_DB_ZONE_RUNTIME_FACADE_TESTING"
        _macro_count)
    if("${PATH}" STREQUAL "${_tests_cmake_path}")
        substring_count(
            _candidate_normalized
            "${_expected_runtime_facade_test_definition}"
            _grant_count)
        if(NOT _grant_count EQUAL 1 OR NOT _macro_count EQUAL 1)
            set(${OUTPUT} FALSE PARENT_SCOPE)
            return()
        endif()
        string(REPLACE
            "${_expected_runtime_facade_test_definition}" ""
            _candidate_normalized "${_candidate_normalized}")
        string(FIND
            "${_candidate_normalized}"
            "KISAK_DB_ZONE_RUNTIME_FACADE_TESTING"
            _remaining_macro)
        if(NOT _remaining_macro EQUAL -1)
            set(${OUTPUT} FALSE PARENT_SCOPE)
            return()
        endif()
    elseif(NOT _macro_count EQUAL 0)
        set(${OUTPUT} FALSE PARENT_SCOPE)
        return()
    endif()
    set(${OUTPUT} TRUE PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE _runtime_facade_build_controls
    LIST_DIRECTORIES FALSE
    "${SOURCE_ROOT}/*CMakeLists.txt"
    "${SOURCE_ROOT}/*.cmake"
    "${SOURCE_ROOT}/*.yml"
    "${SOURCE_ROOT}/*.yaml"
    "${SOURCE_ROOT}/*.bat"
    "${SOURCE_ROOT}/*.ps1"
    "${SOURCE_ROOT}/*CMakePresets.json"
    "${SOURCE_ROOT}/.github/workflows/*.yml"
    "${SOURCE_ROOT}/.github/workflows/*.yaml")
list(REMOVE_DUPLICATES _runtime_facade_build_controls)
set(_runtime_facade_filtered_build_controls)
foreach(_path IN LISTS _runtime_facade_build_controls)
    file(RELATIVE_PATH _relative "${SOURCE_ROOT}" "${_path}")
    if(NOT _relative MATCHES "(^|/)\\.(codex-worktrees|git)/")
        list(APPEND _runtime_facade_filtered_build_controls "${_path}")
    endif()
endforeach()
set(_runtime_facade_build_controls
    ${_runtime_facade_filtered_build_controls})
get_filename_component(
    _runtime_facade_source_seal_path "${CMAKE_CURRENT_LIST_FILE}" ABSOLUTE)
foreach(_sentinel IN ITEMS
    "${SOURCE_ROOT}/CMakeLists.txt"
    "${_tests_cmake_path}"
    "${SOURCE_ROOT}/scripts/common_files.cmake"
    "${SOURCE_ROOT}/build-win.bat"
    "${SOURCE_ROOT}/build-win.ps1"
    "${SOURCE_ROOT}/.github/workflows/ci.yml"
    "${SOURCE_ROOT}/.github/workflows/release.yml")
    list(FIND _runtime_facade_build_controls "${_sentinel}" _sentinel_index)
    if(_sentinel_index EQUAL -1)
        message(FATAL_ERROR
            "Runtime facade macro seal lost build-control traversal: ${_sentinel}")
    endif()
endforeach()

foreach(_path IN LISTS _runtime_facade_build_controls)
    if(_path STREQUAL _runtime_facade_source_seal_path)
        # This seal necessarily names the token in its detector and fixtures.
        continue()
    endif()
    file(READ "${_path}" _build_control)
    runtime_facade_build_control_is_sealed(
        "${_path}" _build_control _build_control_is_sealed)
    if(NOT _build_control_is_sealed)
        file(RELATIVE_PATH _relative "${SOURCE_ROOT}" "${_path}")
        message(FATAL_ERROR
            "Runtime facade test-access macro leaked into build control: "
            "${_relative}")
    endif()
endforeach()

set(_macro_good_fixture "${_expected_runtime_facade_test_definition}")
runtime_facade_build_control_is_sealed(
    "${_tests_cmake_path}" _macro_good_fixture _fixture_is_sealed)
if(NOT _fixture_is_sealed)
    message(FATAL_ERROR
        "Runtime facade macro seal rejected its exact private test grant")
endif()
set(_macro_extra_fixture
    "${_expected_runtime_facade_test_definition} add_compile_definitions(KISAK_DB_ZONE_RUNTIME_FACADE_TESTING=1)")
runtime_facade_build_control_is_sealed(
    "${_tests_cmake_path}" _macro_extra_fixture _fixture_is_sealed)
if(_fixture_is_sealed)
    message(FATAL_ERROR
        "Runtime facade macro seal missed a second test-access grant")
endif()
set(_macro_wrong_target_fixture
    "target_compile_definitions(production PRIVATE KISAK_DB_ZONE_RUNTIME_FACADE_TESTING=1)")
runtime_facade_build_control_is_sealed(
    "${_tests_cmake_path}" _macro_wrong_target_fixture _fixture_is_sealed)
if(_fixture_is_sealed)
    message(FATAL_ERROR
        "Runtime facade macro seal accepted the wrong target")
endif()
set(_macro_production_fixture
    "set(CMAKE_CXX_FLAGS KISAK_DB_ZONE_RUNTIME_FACADE_TESTING=1)")
runtime_facade_build_control_is_sealed(
    "${SOURCE_ROOT}/CMakeLists.txt"
    _macro_production_fixture _fixture_is_sealed)
if(_fixture_is_sealed)
    message(FATAL_ERROR
        "Runtime facade macro seal missed a production build-control leak")
endif()

function(remove_reviewed_runtime_facade_friend_tokens PATH VARIABLE OUTPUT)
    set(_candidate "${${VARIABLE}}")
    string(REGEX REPLACE "[ \t\r\n]+" " " _candidate "${_candidate}")
    if(PATH STREQUAL _coordinator_header_path)
        set(_forward "class ZoneRuntimeFacade;")
        set(_friend "friend class db::zone_runtime::ZoneRuntimeFacade;")
    elseif(PATH STREQUAL _table_header_path)
        set(_forward "class ZoneRuntimeFacade;")
        set(_friend "friend class ZoneRuntimeFacade;")
    elseif(PATH STREQUAL _callback_context_header_path)
        set(_forward "class ZoneRuntimeFacade;")
        set(_friend "friend class ZoneRuntimeFacade;")
    else()
        message(FATAL_ERROR
            "Runtime facade friend scrub used for unexpected path: ${PATH}")
    endif()
    require_substring_count(
        _candidate "${_friend}" 1
        "one reviewed private facade friendship in ${PATH}")
    string(REPLACE "${_friend}" "" _candidate "${_candidate}")
    # The unqualified table friendship contains the forward declaration text
    # as a suffix, so remove the exact friendship before counting the standalone
    # declaration.
    require_substring_count(
        _candidate "${_forward}" 1
        "one reviewed facade forward declaration in ${PATH}")
    string(REPLACE "${_forward}" "" _candidate "${_candidate}")
    set(${OUTPUT} "${_candidate}" PARENT_SCOPE)
endfunction()

function(runtime_facade_candidate_has_reference VARIABLE OUTPUT)
    string(FIND
        "${${VARIABLE}}" "db_zone_runtime_facade" _header_reference)
    string(REGEX MATCH
        "(^|[^A-Za-z0-9_])(ZoneRuntimeFacade[A-Za-z0-9_]*|KISAK_DB_ZONE_RUNTIME_FACADE_TESTING)([^A-Za-z0-9_]|$)"
        _facade_reference "${${VARIABLE}}")
    if(_header_reference EQUAL -1 AND _facade_reference STREQUAL "")
        set(${OUTPUT} FALSE PARENT_SCOPE)
    else()
        set(${OUTPUT} TRUE PARENT_SCOPE)
    endif()
endfunction()

set(_runtime_facade_table_friend_fixture
    "class ZoneRuntimeFacade; friend class ZoneRuntimeFacade;")
remove_reviewed_runtime_facade_friend_tokens(
    "${_table_header_path}"
    _runtime_facade_table_friend_fixture
    _runtime_facade_table_friend_fixture)
runtime_facade_candidate_has_reference(
    _runtime_facade_table_friend_fixture _fixture_reference)
if(_fixture_reference)
    message(FATAL_ERROR
        "Runtime facade seal rejected reviewed table friendship")
endif()
set(_runtime_facade_friend_bypass
    "class ZoneRuntimeFacade; friend class ZoneRuntimeFacade; "
    "inline auto Bypass() { return ZoneRuntimeFacade::TryBeginAccess(); }")
remove_reviewed_runtime_facade_friend_tokens(
    "${_table_header_path}"
    _runtime_facade_friend_bypass
    _runtime_facade_friend_bypass)
runtime_facade_candidate_has_reference(
    _runtime_facade_friend_bypass _fixture_reference)
if(NOT _fixture_reference)
    message(FATAL_ERROR
        "Runtime facade friend scrub missed an inline production caller")
endif()
set(_runtime_facade_include_bypass
    "#include <database/db_zone_runtime_facade.h>")
runtime_facade_candidate_has_reference(
    _runtime_facade_include_bypass _fixture_reference)
if(NOT _fixture_reference)
    message(FATAL_ERROR
        "Runtime facade detector missed a direct header enrollment")
endif()

foreach(_path IN LISTS _production_sources)
    list(FIND _runtime_facade_whole_file_sources "${_path}" _approved_index)
    if(NOT _approved_index EQUAL -1)
        continue()
    endif()
    file(READ "${_path}" _candidate)
    normalize_runtime_facade_phase2(_candidate _candidate_phase2)
    if(_path STREQUAL _coordinator_header_path
        OR _path STREQUAL _table_header_path
        OR _path STREQUAL _callback_context_header_path)
        remove_reviewed_runtime_facade_friend_tokens(
            "${_path}" _candidate_phase2 _candidate_phase2)
    endif()
    runtime_facade_candidate_has_reference(
        _candidate_phase2 _candidate_has_reference)
    if(_candidate_has_reference)
        file(RELATIVE_PATH _relative "${SOURCE_ROOT}" "${_path}")
        message(FATAL_ERROR
            "Runtime facade gained a production caller before atomic cutover: "
            "${_relative}")
    endif()
endforeach()

#pragma once

#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_table.h>

#include <cstddef>
#include <cstdint>

namespace db::zone_runtime
{
// Process-wide, nonblocking serialization for the durable production runtime
// table and its child registry/string ownership scopes. The facade owns no
// reclaimable zone storage and exposes neither the table nor child authority.
// Writable outputs and retained descriptors are rejected when they overlap
// facade session state, the whole production table, the private coordinator,
// or the retained hash lock. Caller buffers read after child mutation begins
// receive the same protected-authority separation. Lower adapters own their
// managed/object-span checks; this boundary is not a sandbox for arbitrary engine globals.
// Opaque callback contexts are identity-byte checked and must not derive or mutate
// any of those protected authority ranges.
enum class ZoneRuntimeFacadeStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidState,
    UnsafeFailure,
};

#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING
struct ZoneRuntimeFacadeTestAccess;
#endif

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

private:
#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING
    friend struct ZoneRuntimeFacadeTestAccess;
#endif

    [[nodiscard]] static ZoneRuntimeFacadeStatus
    authenticateAccess() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    authenticateTableOperationAccess() noexcept;
    [[nodiscard]] static ZoneRuntimeTableStatus
    authenticateCompositeTableOperationAccess(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
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

#ifdef KISAK_DB_ZONE_RUNTIME_FACADE_TESTING
struct ZoneRuntimeFacadeTestAccess final
{
    static void ResetForTesting() noexcept;
    static void SetGlobalStateForTesting(
        std::uint8_t state,
        std::uint8_t stateMirror,
        std::uintptr_t threadIdentity,
        std::uintptr_t threadIdentityMirror,
        std::uint64_t activeSerial,
        std::uint64_t activeSerialMirror,
        std::uint64_t nextSerial,
        std::uint64_t nextSerialMirror) noexcept;
    static void SetThreadStateForTesting(
        std::uint8_t state,
        std::uint8_t stateMirror,
        std::uintptr_t threadIdentity,
        std::uintptr_t threadIdentityMirror,
        std::uint64_t serial,
        std::uint64_t serialMirror) noexcept;
    [[nodiscard]] static std::uintptr_t CurrentThreadIdentity() noexcept;
};
#endif
} // namespace db::zone_runtime

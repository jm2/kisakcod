#pragma once

#include <database/db_zone_pending_copy_ledger.h>
#include <database/db_zone_load_context.h>
#include <database/db_zone_runtime_storage.h>
#include <database/db_zone_script_string_ownership.h>
#include <database/db_zone_slots.h>
#include <database/db_zone_stream_ownership.h>
#include <universal/kisak_abi.h>
#include <universal/physicalmemory_checked.h>

#include <array>
#include <cstdint>

namespace db::zone_runtime
{
// This table is the durable, allocation-independent owner for the metadata of
// every native zone-registry slot.  It is deliberately separate from XZone:
// the legacy loader clears XZone with memset and frees its PMem allocation
// before lifecycle cleanup can publish an empty slot.  Per-generation backing
// slabs, journal entries, FX workspaces, native arenas, and zone memory do not
// belong in this table.  Only their durable, allocation-independent receipts
// and placement handle live here; later wiring binds those objects by exact
// generation key.
//
// Physical slot zero remains the engine's reserved/default slot.  The table
// has stable storage for all 33 physical slots.  Only slots 1..32 are usable.

enum class ZoneRuntimeTableStatus : std::uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidSlot,
    InvalidState,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    GenerationExhausted,
    UnsafeFailure,
    // Appended to preserve the established numeric values above.  A cleanup
    // Retry retains exact callback/controller ownership; Busy is reentry or a
    // temporarily unavailable serializer.
    Retry,
    // Appended so all established values above remain stable.  Mutable table
    // adapters preserve these recoverable journal outcomes verbatim.
    Rejected,
    CountMismatch,
    CapacityExceeded,
};

class ZoneRuntimeEntry;
class ZoneRuntimeTable;
class ZoneRuntimeReceiptCapsule;

namespace detail
{
[[nodiscard]] bool IsPristineRuntimeReceipt(
    const ZoneRuntimeReceiptCapsule &capsule) noexcept;
[[nodiscard]] bool EntryReceiptsArePristine(
    const ZoneRuntimeEntry &entry) noexcept;
} // namespace detail

// An authenticated, read-only observation of one active generation.  The key
// is copied by value so a retained old view cannot silently become an authority
// for a later generation at the same stable table address.  Raw mutable
// lifecycle and ownership state stays private; the adapters below
// reauthenticate this key immediately around every mutation.  Callers must
// retain the same external per-slot serializer across lookup and use.
struct ZoneRuntimeGenerationView final
{
    zone_load::ZoneLoadContextKey key{};
    const ZoneRuntimeEntry *entry = nullptr;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return static_cast<bool>(key) && entry;
    }
};

RUNTIME_SIZE(ZoneRuntimeGenerationView, 0x18, 0x18);

#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
struct ZoneRuntimeTableTestAccess;
#endif

// Stable-address, allocation-independent storage for the four authorities
// owned by one physical slot. The type is visible only so ZoneRuntimeEntry's
// by-value layout is complete; construction, destruction, state inspection,
// and every contained object remain private. No production operation can
// reach a mutable receipt through this composition layer.
class alignas(8) ZoneRuntimeReceiptCapsule final
{
private:
    friend class ZoneRuntimeEntry;
    friend bool detail::IsPristineRuntimeReceipt(
        const ZoneRuntimeReceiptCapsule &capsule) noexcept;
#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    friend struct ZoneRuntimeTableTestAccess;
#endif

    ZoneRuntimeReceiptCapsule() noexcept = default;
    ~ZoneRuntimeReceiptCapsule() noexcept = default;

    ZoneRuntimeReceiptCapsule(const ZoneRuntimeReceiptCapsule &) = delete;
    ZoneRuntimeReceiptCapsule &operator=(
        const ZoneRuntimeReceiptCapsule &) = delete;
    ZoneRuntimeReceiptCapsule(ZoneRuntimeReceiptCapsule &&) = delete;
    ZoneRuntimeReceiptCapsule &operator=(
        ZoneRuntimeReceiptCapsule &&) = delete;

    [[nodiscard]] bool isPristine() const noexcept;

    physical_memory::AllocationReceipt allocationReceipt_{};
    zone_stream_ownership::ZoneStreamGenerationReceipt
        streamGenerationReceipt_{};
    zone_pending_copy::PendingCopyAdmissionReceipt
        pendingCopyAdmissionReceipt_{};
    zone_runtime_storage::ZoneRuntimeStorageBinding storageBinding_{};
};

RUNTIME_SIZE(ZoneRuntimeReceiptCapsule, 0xD0, 0x120);

class alignas(8) ZoneRuntimeEntry final
{
public:
    ZoneRuntimeEntry() noexcept = default;
    ~ZoneRuntimeEntry() noexcept = default;

    ZoneRuntimeEntry(const ZoneRuntimeEntry &) = delete;
    ZoneRuntimeEntry &operator=(const ZoneRuntimeEntry &) = delete;
    ZoneRuntimeEntry(ZoneRuntimeEntry &&) = delete;
    ZoneRuntimeEntry &operator=(ZoneRuntimeEntry &&) = delete;

    [[nodiscard]] const zone_load::ZoneLoadContextKey &key() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextSlot &lifecycle()
        const noexcept;
    [[nodiscard]] const zone_script_string_ownership::
        ZoneScriptStringOwnershipController &scriptStringOwnership()
        const noexcept;

private:
    friend class ZoneRuntimeTable;
    friend bool detail::EntryReceiptsArePristine(
        const ZoneRuntimeEntry &entry) noexcept;
    friend ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(
        ZoneRuntimeTable *table) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const ZoneRuntimeEntry **outEntry) noexcept;
    friend ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        zone_load::ZoneLoadContextKey *inOutKey) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeGenerationView *outView) noexcept;
    friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept;
    friend ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    friend struct ZoneRuntimeTableTestAccess;
#endif

    zone_load::ZoneLoadContextSlot lifecycle_{};
    zone_script_string_ownership::ZoneScriptStringOwnershipController
        scriptStringOwnership_{};
    zone_load::ZoneLoadContextKey key_{};
    ZoneRuntimeReceiptCapsule receiptCapsule_{};
};

RUNTIME_SIZE(ZoneRuntimeEntry, 0x130, 0x198);

class alignas(8) ZoneRuntimeTable final
{
public:
    ZoneRuntimeTable() noexcept = default;
    ~ZoneRuntimeTable() noexcept = default;

    ZoneRuntimeTable(const ZoneRuntimeTable &) = delete;
    ZoneRuntimeTable &operator=(const ZoneRuntimeTable &) = delete;
    ZoneRuntimeTable(ZoneRuntimeTable &&) = delete;
    ZoneRuntimeTable &operator=(ZoneRuntimeTable &&) = delete;

    // This is a diagnostic hint, not a synchronization primitive.  Every
    // initialization, lookup, claim, accessor, and capability use requires
    // external serialization.  DB_Init initializes the production object
    // before the database thread can observe it.
    [[nodiscard]] bool initialized() const noexcept;

private:
    friend ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(
        ZoneRuntimeTable *table) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const ZoneRuntimeEntry **outEntry) noexcept;
    friend ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        zone_load::ZoneLoadContextKey *inOutKey) noexcept;
    friend ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeGenerationView *outView) noexcept;
    friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept;
    friend ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ZoneRuntimeTableStatus
    TryBeginZoneRuntimeScriptStringOwnership(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        script_string_journal::ScriptStringJournal *journal,
        script_string_journal::ScriptStringJournalEntry *storage,
        std::uint32_t storageCapacity,
        std::uint32_t expectedCount) noexcept;
    friend ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const script_string_adapter::ScriptStringSourceView &source,
        std::uint32_t *outStringId) noexcept;
    friend ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ZoneRuntimeTableStatus
    TryBeginZoneRuntimeScriptStringTransfer(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ZoneRuntimeTableStatus
    TryTransferNextZoneRuntimeScriptString(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ZoneRuntimeTableStatus
    TryPrepareZoneRuntimeScriptStringCommit(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ZoneRuntimeTableStatus
    TryCommitZoneRuntimeScriptStringsAndAdmit(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const zone_script_string_ownership::
            ZoneScriptStringAdmissionCallback &admission) noexcept;
    friend ZoneRuntimeTableStatus
    TryBeginZoneRuntimeScriptStringRollback(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        const zone_script_string_ownership::
            ZoneScriptStringRollbackCallbacks &callbacks) noexcept;
    friend ZoneRuntimeTableStatus
    TryRollbackNextZoneRuntimeScriptString(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    friend ZoneRuntimeTableStatus
    TryFinishZoneRuntimeScriptStringAbandonment(
        ZoneRuntimeTable *table,
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    friend struct ZoneRuntimeTableTestAccess;
#endif

    [[nodiscard]] ZoneRuntimeTableStatus
    validateInitializedHeader() noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus authenticateExactMutableEntry(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeEntry **outEntry) noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus completeMutableOperation(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        zone_script_string_ownership::
            ZoneScriptStringOwnershipStatus ownershipStatus) noexcept;
    [[nodiscard]] static zone_load::ZoneLoadContextSlot *mutableLifecycle(
        ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static zone_script_string_ownership::
        ZoneScriptStringOwnershipController *mutableScriptStringOwnership(
        ZoneRuntimeEntry *entry) noexcept;
    void poison() noexcept;

    std::array<ZoneRuntimeEntry, zone_slots::kPhysicalZoneSlotCount>
        entries_{};
    // These process-wide authorities are unique by construction. They remain
    // pristine until later exact-key adapters enroll them atomically.
    zone_stream_ownership::ActiveZoneStreamBinding
        activeZoneStreamBinding_{};
    zone_pending_copy::PendingCopyLedger pendingCopyLedger_{};
    std::uint32_t state_ = 0;
    std::uint32_t reserved_ = 0;
};

RUNTIME_SIZE(ZoneRuntimeTable, 0xE900, 0xF700);

#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
// Tests opt in before including this header.  Production callers cannot reach
// unkeyed mutable entry state.
struct ZoneRuntimeTableTestAccess final
{
    static zone_load::ZoneLoadContextSlot *Lifecycle(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot].lifecycle_;
    }

    static zone_script_string_ownership::
        ZoneScriptStringOwnershipController *Ownership(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot].scriptStringOwnership_;
    }

    static physical_memory::AllocationReceipt *AllocationReceipt(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot]
                    .receiptCapsule_.allocationReceipt_;
    }

    static zone_stream_ownership::ZoneStreamGenerationReceipt *
    StreamGenerationReceipt(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot]
                    .receiptCapsule_.streamGenerationReceipt_;
    }

    static zone_pending_copy::PendingCopyAdmissionReceipt *
    PendingCopyAdmissionReceipt(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot]
                    .receiptCapsule_.pendingCopyAdmissionReceipt_;
    }

    static zone_runtime_storage::ZoneRuntimeStorageBinding *StorageBinding(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot]
                    .receiptCapsule_.storageBinding_;
    }

    static zone_stream_ownership::ActiveZoneStreamBinding *
    ActiveStreamBinding(ZoneRuntimeTable *const table) noexcept
    {
        return table ? &table->activeZoneStreamBinding_ : nullptr;
    }

    static zone_pending_copy::PendingCopyLedger *PendingCopyLedger(
        ZoneRuntimeTable *const table) noexcept
    {
        return table ? &table->pendingCopyLedger_ : nullptr;
    }

    [[nodiscard]] static bool ReceiptCapsulePristine(
        const ZoneRuntimeTable *table,
        std::uint32_t physicalSlot) noexcept;

    [[nodiscard]] static bool SharedResourcesPristine(
        const ZoneRuntimeTable *table) noexcept;

    static void SetKey(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept
    {
        if (table && physicalSlot < table->entries_.size())
            table->entries_[physicalSlot].key_ = key;
    }

    static void SetReserved(
        ZoneRuntimeTable *const table,
        const std::uint32_t reserved) noexcept
    {
        if (table)
            table->reserved_ = reserved;
    }
};
#endif

// Returns the process-lifetime production table.  The object owns only durable
// control metadata and therefore outlives every per-generation PMem region.
[[nodiscard]] ZoneRuntimeTable &ProductionZoneRuntimeTable() noexcept;

// Initializes all usable lifecycle slots exactly once.  Repeating this call
// succeeds only for the exact pristine initialized representation.  A table
// that is partially initialized or corrupt is poisoned; a valid table already
// rebound to a generation is rejected without resetting any ownership.
[[nodiscard]] ZoneRuntimeTableStatus TryInitializeZoneRuntimeTable(
    ZoneRuntimeTable *table) noexcept;

// Checked, read-only physical lookup.  Slot zero and out-of-range slots are
// rejected.  The caller's output is unchanged on every non-Success result.
[[nodiscard]] ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const ZoneRuntimeEntry **outEntry) noexcept;

// Claims one usable slot through its embedded generation lifecycle.  This
// function is implemented for the future loader adapter, but the legacy loader
// does not call it yet.  The exact terminal-receipt reset/unload adapters below
// and exact-key mutable ownership adapters are implemented but remain
// unenrolled.  Production wiring must still bind durable resources and enroll
// the complete path; a claimed slot cannot be rebound until its exact terminal
// ownership receipt is reset.  The lifecycle terminal receipt and durable old
// key remain intact until a subsequent claim atomically advances the generation;
// the new durable entry key and caller output publish only after that lifecycle
// claim succeeds.
[[nodiscard]] ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    zone_load::ZoneLoadContextKey *inOutKey) noexcept;

// Authenticates both physical slot and generation and returns a read-only
// observation only for an active Loading/Live/Abandoning slot.  Mutable
// controller operations use the exact-key adapters below; callers cannot retain
// raw mutable authority across generation reuse.  The caller's output is
// unchanged on every non-Success result.
[[nodiscard]] ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimeGenerationView *outView) noexcept;

// The following production-neutral adapters are the only public route from a
// durable runtime-table key to mutable loading ownership.  Every call
// authenticates table state, physical slot, durable key, lifecycle generation,
// and controller binding both before and after the underlying mutation.  A
// recoverable controller result is preserved only after post-authentication;
// an unsafe result or impossible postcondition mismatch poisons the table.
// External per-slot serialization remains mandatory.  These adapters do not
// allocate resources or enroll any legacy loader caller.

[[nodiscard]] ZoneRuntimeTableStatus
TryBeginZoneRuntimeScriptStringOwnership(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    script_string_journal::ScriptStringJournal *journal,
    script_string_journal::ScriptStringJournalEntry *storage,
    std::uint32_t storageCapacity,
    std::uint32_t expectedCount) noexcept;

// outStringId is unchanged on every non-Success result, including an unsafe
// postcondition mismatch after the controller staged a local candidate.
[[nodiscard]] ZoneRuntimeTableStatus TryStageZoneRuntimeScriptString(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const script_string_adapter::ScriptStringSourceView &source,
    std::uint32_t *outStringId) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TrySealZoneRuntimeScriptStrings(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryBeginZoneRuntimeScriptStringTransfer(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Processes at most one journal entry.  Exact retries after Transferred are
// harmless and still pass through both authentication boundaries.
[[nodiscard]] ZoneRuntimeTableStatus
TryTransferNextZoneRuntimeScriptString(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryPrepareZoneRuntimeScriptStringCommit(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryCommitZoneRuntimeScriptStringsAndAdmit(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const zone_script_string_ownership::
        ZoneScriptStringAdmissionCallback &admission) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryBeginZoneRuntimeScriptStringRollback(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const zone_script_string_ownership::
        ZoneScriptStringRollbackCallbacks &callbacks) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryRollbackNextZoneRuntimeScriptString(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryFinishZoneRuntimeScriptStringAbandonment(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Drives only a committed Live generation through the controller-owned
// live-unload recipe.  The controller binds the callback identity and retains
// the outer script-string transaction across Retry.  The same externally
// serialized thread must resume it.  Success leaves an exact Unloaded receipt;
// an exact final retry succeeds without replaying callbacks.
[[nodiscard]] ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept;

// Authenticates an exact Abandoned or Unloaded terminal receipt and resets only
// its ownership controller to canonical Empty.  The lifecycle terminal kind,
// generation, and durable table key remain as a receipt until the next claim
// advances the generation.  Repeating the exact reset is a no-op success.
[[nodiscard]] ZoneRuntimeTableStatus TryResetZoneRuntimeTerminalReceipt(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

} // namespace db::zone_runtime

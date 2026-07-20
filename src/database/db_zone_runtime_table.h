#pragma once

#include <database/db_zone_pending_copy_ledger.h>
#include <database/db_zone_load_context.h>
#include <database/db_zone_runtime_storage.h>
#include <database/db_zone_script_string_ownership.h>
#include <database/db_zone_slots.h>
#include <database/db_zone_stream_ownership.h>
#include <universal/kisak_abi.h>
#include <universal/physicalmemory_checked.h>
#include <universal/physicalmemory_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace db::zone_runtime
{
// This table is the durable, allocation-independent owner for the metadata of
// every native zone-registry slot.  It is deliberately separate from XZone:
// the legacy loader clears XZone with memset and frees its PMem allocation
// before lifecycle cleanup can publish an empty slot.  Per-generation backing
// slabs, journal entries, FX workspaces, native arenas, and zone memory do not
// belong in this table.  Only their durable, allocation-independent receipts
// and placement handle live here. The composite controller below binds those
// objects by exact generation key; the legacy-loader cutover remains separate.
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
class ZoneRuntimeFacade;
class ZoneRuntimeReceiptCapsule;
class ZoneRuntimeGenerationBinding;

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
// reauthenticate this key immediately around every mutation. Callers must
// retain one table-wide external serializer across lookup, mutation,
// callbacks, post-authentication, and use: validation scans all entries and
// the stream/pending authorities are process-wide and intentionally lockless.
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

// The composite adapter follows one strict, monotonic construction order.
// The stage is a private ownership witness, not permission to skip the exact
// component authenticators.  Cleanup retains the highest completed stage so
// an early abandonment can distinguish resources that never existed from
// resources that must have reached their terminal receipt.
enum class ZoneRuntimeSetupStage : std::uint8_t
{
    Passive,
    CallbacksBound,
    AllocationBegun,
    StorageBound,
    StreamGenerationBegun,
    StreamsBound,
    PendingCopyBegun,
    ScriptStringsBegun,
    AdmissionPrepared,
    StreamsInvalidated,
    AllocationEnded,
};

enum class ZoneRuntimeExecutionMode : std::uint8_t
{
    Passive,
    Loading,
    Admitting,
    Live,
    Abandoning,
    Unloading,
    Terminal,
};

// Durable callback metadata is copied into the stable runtime entry before
// any reclaimable resource is acquired.  The callback context and every
// function identity must remain valid until exact terminal reset.  Owned
// cleanup operations are intercepted by the table; performExternalCleanup is
// invoked only for the remaining engine-specific recipe steps.
struct ZoneRuntimeGenerationCallbacks final
{
    void *context = nullptr;
    zone_script_string_ownership::ZoneScriptStringUnpublishStatus
        (*ensureUnreachable)(void *context) noexcept = nullptr;
    zone_load::ZoneLoadCleanupCallbackStatus (*performExternalCleanup)(
        void *context,
        zone_load::ZoneLoadCleanupOperation operation) noexcept = nullptr;
    void (*completePendingAdmission)(void *context) noexcept = nullptr;
    void (*admitLive)(void *context) noexcept = nullptr;
};

RUNTIME_SIZE(ZoneRuntimeGenerationCallbacks, 0x14, 0x28);

// Exact-key binding for the composite adapter.  It lives beside, not inside,
// the reclaimable PMem allocation and placement slab.  No production accessor
// exposes its callback pointers or mutable state.
class alignas(8) ZoneRuntimeGenerationBinding final
{
public:
    ZoneRuntimeGenerationBinding() noexcept = default;
    ~ZoneRuntimeGenerationBinding() noexcept = default;

    ZoneRuntimeGenerationBinding(
        const ZoneRuntimeGenerationBinding &) = delete;
    ZoneRuntimeGenerationBinding &operator=(
        const ZoneRuntimeGenerationBinding &) = delete;
    ZoneRuntimeGenerationBinding(ZoneRuntimeGenerationBinding &&) = delete;
    ZoneRuntimeGenerationBinding &operator=(
        ZoneRuntimeGenerationBinding &&) = delete;

    [[nodiscard]] ZoneRuntimeSetupStage setupStage() const noexcept;
    [[nodiscard]] ZoneRuntimeExecutionMode executionMode() const noexcept;
    [[nodiscard]] bool isPristine() const noexcept;

private:
    friend class ZoneRuntimeTable;
#ifdef KISAK_DB_ZONE_RUNTIME_TABLE_TESTING
    friend struct ZoneRuntimeTableTestAccess;
#endif

    [[nodiscard]] bool canonicalFor(
        const ZoneRuntimeTable *table,
        const zone_load::ZoneLoadContextSlot *lifecycle,
        const zone_load::ZoneLoadContextKey &key) const noexcept;
    [[nodiscard]] bool callbacksMatch(
        const ZoneRuntimeGenerationCallbacks &callbacks) const noexcept;
    void bind(
        ZoneRuntimeTable *table,
        zone_load::ZoneLoadContextSlot *lifecycle,
        const zone_load::ZoneLoadContextKey &key,
        const ZoneRuntimeGenerationCallbacks &callbacks) noexcept;
    void setSetupStage(ZoneRuntimeSetupStage stage) noexcept;
    void setExecutionMode(ZoneRuntimeExecutionMode mode) noexcept;
    void reset() noexcept;

    // One ABI-neutral byte distinguishes the exact external lifecycle
    // callback windows that may borrow registry ownership from all internal
    // callback work.  Any non-Idle marker keeps ordinary table operations
    // callback-busy; only ZoneRuntimeTable's exact-key authenticator may
    // interpret ActiveRegistryBorrow as authority, and successful admission
    // consumes it to ActiveNoRegistry before returning. A recoverable
    // coordinator-lock collision restores that exact consumed marker before
    // returning Busy so the same synchronous callback may retry; every other
    // result remains one-shot. This rule does not contain arbitrary nonlocal
    // exits. Enrolled callbacks must not longjmp, and production enrollment
    // still requires checked no-report adapters.
    enum class CallbackMarker : std::uint8_t
    {
        Idle,
        ActiveNoRegistry,
        ActiveRegistryBorrow,
    };

    zone_load::ZoneLoadContextKey key_{};
    ZoneRuntimeTable *table_ = nullptr;
    zone_load::ZoneLoadContextSlot *lifecycle_ = nullptr;
    const ZoneRuntimeGenerationBinding *self_ = nullptr;
    ZoneRuntimeGenerationCallbacks callbacks_{};
    const script_string_journal::ScriptStringJournal *placementJournal_ =
        nullptr;
    const script_string_journal::ScriptStringJournalEntry *placementEntries_ =
        nullptr;
    std::uint32_t placementCapacity_ = 0;
    std::uint32_t placementExpectedCount_ = 0;
    std::uint32_t allocationType_ = 0;
    ZoneRuntimeSetupStage setupStage_ = ZoneRuntimeSetupStage::Passive;
    ZoneRuntimeExecutionMode executionMode_ =
        ZoneRuntimeExecutionMode::Passive;
    CallbackMarker callbackMarker_ = CallbackMarker::Idle;
    std::uint8_t witness_ = 0;
    std::uint32_t reserved_ = 0;
};

RUNTIME_SIZE(ZoneRuntimeGenerationBinding, 0x50, 0x78);

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
    friend class ZoneRuntimeTable;
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
    [[nodiscard]] ZoneRuntimeSetupStage setupStage() const noexcept;
    [[nodiscard]] ZoneRuntimeExecutionMode executionMode() const noexcept;
    [[nodiscard]] bool generationBindingPristine() const noexcept;

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
    friend ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        const ZoneRuntimeGenerationCallbacks &) noexcept;
    friend ZoneRuntimeTableStatus TryBeginZoneRuntimePhysicalAllocation(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        const char *,
        std::uint32_t) noexcept;
    friend ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        pmem_runtime::AllocationResult *) noexcept;
    friend ZoneRuntimeTableStatus TryBindZoneRuntimeStorage(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        void *,
        std::size_t,
        const zone_runtime_storage::ZoneRuntimeStoragePlan *) noexcept;
    friend ZoneRuntimeTableStatus TryBeginZoneRuntimeStreamGeneration(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryBindZoneRuntimeStreams(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        const XZoneMemory *,
        const relocation::BlockView *,
        std::size_t) noexcept;
    friend ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryAppendZoneRuntimePendingCopy(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t) noexcept;
    friend ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryInvalidateZoneRuntimeStreams(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryEndZoneRuntimePhysicalAllocation(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryCommitZoneRuntimeGeneration(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus
    TryBeginZoneRuntimeGenerationAbandonment(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus
    TryContinueZoneRuntimeGenerationAbandonment(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
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
    ZoneRuntimeGenerationBinding generationBinding_{};
};

RUNTIME_SIZE(ZoneRuntimeEntry, 0x190, 0x228);

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
    // the same table-wide external serialization. DB_Init initializes the
    // production object
    // before the database thread can observe it.
    [[nodiscard]] bool initialized() const noexcept;

private:
    friend class ZoneRuntimeFacade;
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
    friend ZoneRuntimeTableStatus TryBindZoneRuntimeGenerationCallbacks(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        const ZoneRuntimeGenerationCallbacks &) noexcept;
    friend ZoneRuntimeTableStatus TryBeginZoneRuntimePhysicalAllocation(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        const char *,
        std::uint32_t) noexcept;
    friend ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        pmem_runtime::AllocationResult *) noexcept;
    friend ZoneRuntimeTableStatus TryBindZoneRuntimeStorage(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        void *,
        std::size_t,
        const zone_runtime_storage::ZoneRuntimeStoragePlan *) noexcept;
    friend ZoneRuntimeTableStatus TryBeginZoneRuntimeStreamGeneration(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryBindZoneRuntimeStreams(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        const XZoneMemory *,
        const relocation::BlockView *,
        std::size_t) noexcept;
    friend ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryAppendZoneRuntimePendingCopy(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &,
        std::uint32_t) noexcept;
    friend ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryInvalidateZoneRuntimeStreams(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryEndZoneRuntimePhysicalAllocation(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryCommitZoneRuntimeGeneration(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus
    TryBeginZoneRuntimeGenerationAbandonment(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus
    TryContinueZoneRuntimeGenerationAbandonment(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
        ZoneRuntimeTable *,
        std::uint32_t,
        const zone_load::ZoneLoadContextKey &) noexcept;
    friend ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopyDrain(
        ZoneRuntimeTable *,
        const zone_pending_copy::PendingCopyDrainCallback &) noexcept;
    friend ZoneRuntimeTableStatus TryDrainNextZoneRuntimePendingCopy(
        ZoneRuntimeTable *) noexcept;
    friend ZoneRuntimeTableStatus TryFinishZoneRuntimePendingCopyDrain(
        ZoneRuntimeTable *) noexcept;
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
    [[nodiscard]] ZoneRuntimeTableStatus validateReleaseSafety() noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus validateEntryBinding(
        std::uint32_t physicalSlot) noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus
    validateSharedComposition() noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus authenticateExactEntry(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus authenticateExactMutableEntry(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeEntry **outEntry) noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus
    authenticateExactRegistryLifecycleCallback(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus
    restoreExactRegistryLifecycleCallback(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus
    transitionExactRegistryLifecycleCallback(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeGenerationBinding::CallbackMarker expectedMarker,
        ZoneRuntimeGenerationBinding::CallbackMarker replacementMarker)
        noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus completeMutableOperation(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        zone_script_string_ownership::
            ZoneScriptStringOwnershipStatus ownershipStatus) noexcept;
    [[nodiscard]] ZoneRuntimeTableStatus completeCompositeOperation(
        std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key,
        ZoneRuntimeTableStatus operationStatus) noexcept;
    [[nodiscard]] static zone_load::ZoneLoadContextSlot *mutableLifecycle(
        ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static zone_script_string_ownership::
        ZoneScriptStringOwnershipController *mutableScriptStringOwnership(
        ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static ZoneRuntimeReceiptCapsule *mutableReceiptCapsule(
        ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static ZoneRuntimeGenerationBinding *
    mutableGenerationBinding(ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static physical_memory::AllocationReceipt *
    mutableAllocationReceipt(ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static zone_stream_ownership::
        ZoneStreamGenerationReceipt *mutableStreamGenerationReceipt(
            ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static zone_pending_copy::
        PendingCopyAdmissionReceipt *mutablePendingCopyAdmissionReceipt(
            ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static zone_runtime_storage::ZoneRuntimeStorageBinding *
    mutableStorageBinding(ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static std::uint32_t generationAllocationType(
        const ZoneRuntimeEntry *entry) noexcept;
    [[nodiscard]] static bool generationCallbacksMatch(
        const ZoneRuntimeEntry *entry,
        const ZoneRuntimeGenerationCallbacks &callbacks) noexcept;
    [[nodiscard]] static bool generationPlacementMatches(
        const ZoneRuntimeEntry *entry,
        const script_string_journal::ScriptStringJournal *journal,
        const script_string_journal::ScriptStringJournalEntry *storage,
        std::uint32_t capacity,
        std::uint32_t expectedCount) noexcept;
    [[nodiscard]] static bool
    exactRegistryLifecycleCallbackPhaseMatches(
        const ZoneRuntimeEntry &entry) noexcept;
    static void bindGeneration(
        ZoneRuntimeTable *table,
        ZoneRuntimeEntry *entry,
        const zone_load::ZoneLoadContextKey &key,
        const ZoneRuntimeGenerationCallbacks &callbacks) noexcept;
    static void setGenerationAllocation(
        ZoneRuntimeEntry *entry,
        std::uint32_t allocationType) noexcept;
    static void setGenerationSetupStage(
        ZoneRuntimeEntry *entry,
        ZoneRuntimeSetupStage stage) noexcept;
    static void setGenerationExecutionMode(
        ZoneRuntimeEntry *entry,
        ZoneRuntimeExecutionMode mode) noexcept;
    static void retainGenerationPlacement(
        ZoneRuntimeEntry *entry,
        const script_string_journal::ScriptStringJournal *journal,
        const script_string_journal::ScriptStringJournalEntry *storage,
        std::uint32_t capacity,
        std::uint32_t expectedCount) noexcept;
    static void resetCompositeReceiptsAndBinding(
        ZoneRuntimeEntry *entry) noexcept;
    static zone_script_string_ownership::ZoneScriptStringUnpublishStatus
    EnsureBoundGenerationUnreachable(void *context) noexcept;
    static zone_load::ZoneLoadCleanupCallbackStatus
    PerformBoundGenerationCleanup(
        void *context,
        zone_load::ZoneLoadCleanupOperation operation) noexcept;
    static void CompleteBoundPendingAdmission(void *context) noexcept;
    static void AdmitBoundGeneration(void *context) noexcept;
    void poison() noexcept;

    std::array<ZoneRuntimeEntry, zone_slots::kPhysicalZoneSlotCount>
        entries_{};
    // These process-wide authorities are unique by construction. The current
    // exact-key adapters enroll and authenticate them as table-owned shared
    // state across generation setup, admission, cleanup, and pending drain.
    zone_stream_ownership::ActiveZoneStreamBinding
        activeZoneStreamBinding_{};
    zone_pending_copy::PendingCopyLedger pendingCopyLedger_{};
    // Retains one exact callback identity for the complete drain, including
    // Retry. Callers cannot replace consumption authority between records.
    zone_pending_copy::PendingCopyDrainCallback pendingDrainCallback_{};
    std::uint32_t state_ = 0;
    std::uint32_t sharedState_ = 0;
};

RUNTIME_SIZE(ZoneRuntimeTable, 0xF568, 0x109A0);

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

    static ZoneRuntimeGenerationBinding *GenerationBinding(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return nullptr;
        return &table->entries_[physicalSlot].generationBinding_;
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

    [[nodiscard]] static ZoneRuntimeTableStatus ReleaseSafety(
        ZoneRuntimeTable *const table) noexcept
    {
        return table
            ? table->validateReleaseSafety()
            : ZoneRuntimeTableStatus::InvalidArgument;
    }

    [[nodiscard]] static ZoneRuntimeTableStatus
    AuthenticateExactRegistryLifecycleCallback(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept
    {
        return table
            ? table->authenticateExactRegistryLifecycleCallback(
                physicalSlot, key)
            : ZoneRuntimeTableStatus::InvalidArgument;
    }

    [[nodiscard]] static ZoneRuntimeTableStatus
    RestoreExactRegistryLifecycleCallback(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot,
        const zone_load::ZoneLoadContextKey &key) noexcept
    {
        return table
            ? table->restoreExactRegistryLifecycleCallback(
                physicalSlot, key)
            : ZoneRuntimeTableStatus::InvalidArgument;
    }

    static void SetCallbackMarker(
        ZoneRuntimeTable *const table,
        const std::uint32_t physicalSlot,
        const std::uint8_t marker) noexcept
    {
        if (!table || physicalSlot >= table->entries_.size())
            return;
        table->entries_[physicalSlot]
            .generationBinding_.callbackMarker_ =
            static_cast<ZoneRuntimeGenerationBinding::CallbackMarker>(
                marker);
    }

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
            table->sharedState_ = reserved;
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

// Checked, read-only physical lookup. Slot zero, out-of-range slots, and an
// output that is misaligned or overlaps the table are rejected. The caller's
// output is unchanged on every non-Success result.
[[nodiscard]] ZoneRuntimeTableStatus TryGetZoneRuntimeEntry(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const ZoneRuntimeEntry **outEntry) noexcept;

// Claims one usable slot through its embedded generation lifecycle. The
// composite adapters below can enroll the resulting exact key, but the legacy
// loader does not call this controller yet. A claimed slot cannot be rebound
// until its exact terminal ownership receipt is reset. The lifecycle terminal
// receipt and durable old key remain intact until a subsequent claim atomically
// advances the generation; the new durable entry key and caller output publish
// only after that lifecycle claim and whole-table postcondition both succeed.
// A misaligned key/output or one overlapping the table is rejected before the
// generation can advance.
[[nodiscard]] ZoneRuntimeTableStatus TryClaimZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    zone_load::ZoneLoadContextKey *inOutKey) noexcept;

// Authenticates both physical slot and generation and returns a read-only
// observation only for an active Loading/Live/Abandoning slot.  Mutable
// controller operations use the exact-key adapters below; callers cannot retain
// raw mutable authority across generation reuse. A misaligned output or one
// overlapping the table or input key is rejected, and the caller's output is
// unchanged on every non-Success result.
[[nodiscard]] ZoneRuntimeTableStatus TryGetZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    ZoneRuntimeGenerationView *outView) noexcept;

// Enrolls an exact claimed Loading generation in the production-neutral
// composite path.  The pending ledger is initialized once per table and the
// callback identity is copied before any reclaimable resource exists.  An
// exact retry accepts only the identical callback tuple.
[[nodiscard]] ZoneRuntimeTableStatus
TryBindZoneRuntimeGenerationCallbacks(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const ZoneRuntimeGenerationCallbacks &callbacks) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryBeginZoneRuntimePhysicalAllocation(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const char *name,
    std::uint32_t allocationType) noexcept;

// On Success and CapacityExceeded, outResult receives the exact report-free
// PMem result. It must be aligned and outside the managed PMem, table, and key
// ranges; it is unchanged on authentication or composition failure.
[[nodiscard]] ZoneRuntimeTableStatus TryAllocateZoneRuntimeMemory(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    std::uint32_t size,
    std::uint32_t alignment,
    std::uint32_t type,
    pmem_runtime::AllocationResult *outResult) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryBindZoneRuntimeStorage(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    void *slab,
    std::size_t slabCapacity,
    const zone_runtime_storage::ZoneRuntimeStoragePlan *plan) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryBeginZoneRuntimeStreamGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryBindZoneRuntimeStreams(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    const XZoneMemory *zoneIdentity,
    const relocation::BlockView *blocks,
    std::size_t blockCount) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopies(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryAppendZoneRuntimePendingCopy(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key,
    std::uint32_t assetEntryIndex) noexcept;

// Requires script strings to be CommitReady. It freezes the pending-copy
// completion identity, then the next two calls invalidate stream authority
// and End the exact PMem receipt before the no-fail Live publication.
[[nodiscard]] ZoneRuntimeTableStatus TryPrepareZoneRuntimeAdmission(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryInvalidateZoneRuntimeStreams(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryEndZoneRuntimePhysicalAllocation(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryCommitZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus
TryBeginZoneRuntimeGenerationAbandonment(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Advances at most one script-string rollback record, or drives the remaining
// ordered lifecycle cleanup until it reaches Retry or the terminal receipt.
[[nodiscard]] ZoneRuntimeTableStatus
TryContinueZoneRuntimeGenerationAbandonment(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Composite Live unload uses only the callback identity already retained in
// the stable entry. The four-argument compatibility overload below is rejected
// for enrolled generations so it cannot bypass the composite controller.
[[nodiscard]] ZoneRuntimeTableStatus TryUnloadZoneRuntimeGeneration(
    ZoneRuntimeTable *table,
    std::uint32_t physicalSlot,
    const zone_load::ZoneLoadContextKey &key) noexcept;

// Drains all retained Live pending-copy records through one callback identity.
// Begin copies the callback into stable table storage; Retry preserves both
// the current record and that exact identity until Finish returns the shared
// ledger to Ready.
[[nodiscard]] ZoneRuntimeTableStatus TryBeginZoneRuntimePendingCopyDrain(
    ZoneRuntimeTable *table,
    const zone_pending_copy::PendingCopyDrainCallback &callback) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryDrainNextZoneRuntimePendingCopy(
    ZoneRuntimeTable *table) noexcept;

[[nodiscard]] ZoneRuntimeTableStatus TryFinishZoneRuntimePendingCopyDrain(
    ZoneRuntimeTable *table) noexcept;

// The following production-neutral adapters are the only public route from a
// durable runtime-table key to mutable loading ownership.  Every call
// authenticates table state, physical slot, durable key, lifecycle generation,
// and controller binding both before and after the underlying mutation.  A
// recoverable controller result is preserved only after post-authentication;
// an unsafe result or impossible postcondition mismatch poisons the table.
// One external table-wide serializer remains mandatory. These adapters do not
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

// outStringId must be aligned and disjoint from the table, key, and source
// descriptor (and, for composite generations, managed PMem). It is unchanged
// on every non-Success result, including an unsafe postcondition mismatch after
// the controller staged a local candidate.
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

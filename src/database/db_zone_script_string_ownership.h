#pragma once

#include <database/db_script_string_adapter.h>
#include <database/db_script_string_journal.h>
#include <database/db_script_string_transaction.h>
#include <database/db_zone_load_context.h>
#include <universal/kisak_abi.h>

#include <cstdint>

namespace db::zone_script_string_ownership
{
// Constructed, production-neutral ownership boundary for exactly one zone-load
// generation. It deliberately does not enroll the legacy loader yet.
//
// Begin acquires the dedicated recursive serializer before journal
// initialization and retains it through either terminal Live admission or
// completed abandonment. Every operation authenticates the same lifecycle
// key, journal object, backing span, token serial, and owning thread. The
// token is private so a caller cannot use one controller's admission to mutate
// another journal through the lower-level adapter.
//
// The controller supplies global transaction serialization, not independent
// lifecycle synchronization. Begin's claim precondition, terminal accessors/
// reset, and destruction still require the lifecycle slot's external
// serializer. The controller, lifecycle slot, callback context, and cleanup
// metadata must live outside every per-zone allocation that cleanup can free.
// The journal control object and entry backing may be per-zone because both
// are detached before full lifecycle cleanup begins.
//
// Required lock order for future production wiring is:
//   script-string transaction -> db_hashCritSect -> script string -> memory tree
// The controller never acquires db_hashCritSect and must therefore be entered
// before any registry lock. Callbacks must not throw, longjmp, call Com_Error,
// or otherwise leave nonlocally. A nonlocal exit intentionally strands the
// retained serializer in a callback phase and fails closed.

enum class ZoneScriptStringOwnershipPhase : std::uint8_t
{
    Empty,
    Staging,
    Sealed,
    Transferring,
    Transferred,
    CommitReady,
    Unpublishing,
    UnpublishingCallback,
    RollingBack,
    OwnershipRolledBack,
    Cleaning,
    Admitting,
    Live,
    Abandoned,
    UnsafeFailure,
    // Appended so the established phase values above remain stable.
    Unloading,
    UnloadingCallback,
    Unloaded,
};

enum class ZoneScriptStringOwnershipStatus : std::uint8_t
{
    Success,
    Retry,
    Rejected,
    Busy,
    InvalidArgument,
    InvalidState,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    CountMismatch,
    CapacityExceeded,
    UnsafeFailure,
};

enum class ZoneScriptStringUnpublishStatus : std::uint8_t
{
    Success,
    Retry,
    UnsafeFailure,
};

struct ZoneScriptStringRollbackCallbacks final
{
    void *context = nullptr;

    // Ensures that no registry entry, alias, completed-pointer record,
    // g_copyInfo entry, or other externally reachable object can expose a
    // staged string ID. It must be convergent because rollback invokes it
    // before releasing ownership and the lifecycle cleanup recipe invokes it
    // again at MakePartialAssetsAndStagedReferencesUnreachable.
    ZoneScriptStringUnpublishStatus (*ensureUnreachable)(
        void *context) noexcept = nullptr;

    // Handles every remaining ZoneLoadCleanupOperation. The controller owns
    // MakePartialAssetsAndStagedReferencesUnreachable and routes that operation
    // back through ensureUnreachable, so this callback is not invoked for it.
    // Neither callback may mutate the bound controller, lifecycle slot,
    // journal, or attached entry span through another saved pointer.
    zone_load::ZoneLoadCleanupCallbackStatus (*performCleanup)(
        void *context,
        zone_load::ZoneLoadCleanupOperation operation) noexcept = nullptr;
};

struct ZoneScriptStringAdmissionCallback final
{
    void *context = nullptr;

    // Runs only after lifecycle Live publication and unconditional journal
    // finalization, while the global serializer is still retained. It must be
    // an infallible ensure-postcondition operation which releases the loading/
    // queue/recovery admission gate without reporting or nonlocal exit.
    // It may not mutate the bound controller or lifecycle slot.
    void (*admitLive)(void *context) noexcept = nullptr;
};

#ifdef KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING
struct ZoneScriptStringOwnershipControllerTestAccess;
#endif

class alignas(8) ZoneScriptStringOwnershipController final
{
public:
    ZoneScriptStringOwnershipController() noexcept = default;
    ~ZoneScriptStringOwnershipController() noexcept = default;

    ZoneScriptStringOwnershipController(
        const ZoneScriptStringOwnershipController &) = delete;
    ZoneScriptStringOwnershipController &operator=(
        const ZoneScriptStringOwnershipController &) = delete;
    ZoneScriptStringOwnershipController(
        ZoneScriptStringOwnershipController &&) = delete;
    ZoneScriptStringOwnershipController &operator=(
        ZoneScriptStringOwnershipController &&) = delete;

    [[nodiscard]] ZoneScriptStringOwnershipPhase phase() const noexcept;
    [[nodiscard]] const zone_load::ZoneLoadContextKey &key() const noexcept;
    [[nodiscard]] bool serializerRetained() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;
    // A read-only whole-representation predicate used by durable containing
    // tables before they initialize or reuse an entry.
    [[nodiscard]] bool isEmptyCanonical() const noexcept;
    // Read-only whole-representation authentication for a durable containing
    // table.  The expected lifecycle address is part of the binding: matching
    // only slot/generation is insufficient when stable entries are reused.
    [[nodiscard]] bool canonicalForBinding(
        const zone_load::ZoneLoadContextSlot *expectedLifecycle,
        const zone_load::ZoneLoadContextKey &expectedKey) const noexcept;

private:
    friend ZoneScriptStringOwnershipStatus
    TryBeginZoneScriptStringOwnership(
        ZoneScriptStringOwnershipController *,
        zone_load::ZoneLoadContextSlot *,
        const zone_load::ZoneLoadContextKey &,
        script_string_journal::ScriptStringJournal *,
        script_string_journal::ScriptStringJournalEntry *,
        std::uint32_t,
        std::uint32_t) noexcept;
    friend ZoneScriptStringOwnershipStatus TryStageZoneScriptString(
        ZoneScriptStringOwnershipController *,
        const script_string_adapter::ScriptStringSourceView &,
        std::uint32_t *) noexcept;
    friend ZoneScriptStringOwnershipStatus TrySealZoneScriptStrings(
        ZoneScriptStringOwnershipController *) noexcept;
    friend ZoneScriptStringOwnershipStatus
    TryBeginZoneScriptStringTransfer(
        ZoneScriptStringOwnershipController *) noexcept;
    friend ZoneScriptStringOwnershipStatus
    TryTransferNextZoneScriptString(
        ZoneScriptStringOwnershipController *) noexcept;
    friend ZoneScriptStringOwnershipStatus
    TryPrepareZoneScriptStringCommit(
        ZoneScriptStringOwnershipController *) noexcept;
    friend ZoneScriptStringOwnershipStatus TryCommitZoneScriptStringsAndAdmit(
        ZoneScriptStringOwnershipController *,
        const ZoneScriptStringAdmissionCallback &) noexcept;
    friend ZoneScriptStringOwnershipStatus TryBeginZoneScriptStringRollback(
        ZoneScriptStringOwnershipController *,
        const ZoneScriptStringRollbackCallbacks &) noexcept;
    friend ZoneScriptStringOwnershipStatus
    TryRollbackNextZoneScriptString(
        ZoneScriptStringOwnershipController *) noexcept;
    friend ZoneScriptStringOwnershipStatus
    TryFinishZoneScriptStringAbandonment(
        ZoneScriptStringOwnershipController *) noexcept;
    friend ZoneScriptStringOwnershipStatus
    TryUnloadLiveZoneScriptStringOwnership(
        ZoneScriptStringOwnershipController *,
        zone_load::ZoneLoadContextSlot *,
        const zone_load::ZoneLoadContextKey &,
        const zone_load::ZoneLoadCleanupCallbacks &) noexcept;
    friend ZoneScriptStringOwnershipStatus
    TryResetTerminalZoneScriptStringOwnership(
        ZoneScriptStringOwnershipController *,
        zone_load::ZoneLoadContextSlot *,
        const zone_load::ZoneLoadContextKey &,
        zone_load::ZoneLoadTerminalKind) noexcept;
#ifdef KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING
    friend struct ZoneScriptStringOwnershipControllerTestAccess;
#endif

    [[nodiscard]] ZoneScriptStringOwnershipStatus validateOwned() const noexcept;
    [[nodiscard]] ZoneScriptStringOwnershipStatus
    validateAbandonedReceipt() const noexcept;
    [[nodiscard]] ZoneScriptStringOwnershipStatus validateLiveBinding(
        const zone_load::ZoneLoadContextSlot *expectedLifecycle,
        const zone_load::ZoneLoadContextKey &expectedKey) const noexcept;
    [[nodiscard]] ZoneScriptStringOwnershipStatus validateTerminalReceipt(
        const zone_load::ZoneLoadContextSlot *expectedLifecycle,
        const zone_load::ZoneLoadContextKey &expectedKey,
        zone_load::ZoneLoadTerminalKind expectedTerminalKind) const noexcept;
    [[nodiscard]] bool bindingMatchesCurrentPhase() const noexcept;
    static zone_load::ZoneLoadCleanupCallbackStatus PerformBoundCleanup(
        void *context,
        zone_load::ZoneLoadCleanupOperation operation) noexcept;
    static zone_load::ZoneLoadCleanupCallbackStatus PerformBoundLiveUnload(
        void *context,
        zone_load::ZoneLoadCleanupOperation operation) noexcept;
    void detachJournalBacking() noexcept;
    void poison() noexcept;
    void reset() noexcept;

    zone_load::ZoneLoadContextKey key_{};
    zone_load::ZoneLoadContextSlot *lifecycle_ = nullptr;
    script_string_journal::ScriptStringJournal *journal_ = nullptr;
    script_string_journal::ScriptStringJournalEntry *storage_ = nullptr;
    void *rollbackContext_ = nullptr;
    ZoneScriptStringUnpublishStatus (*ensureUnreachable_)(
        void *) noexcept = nullptr;
    zone_load::ZoneLoadCleanupCallbackStatus (*performCleanup_)(
        void *, zone_load::ZoneLoadCleanupOperation) noexcept = nullptr;
    script_string_transaction::ScriptStringTransactionToken transaction_{};
    std::uint32_t storageCapacity_ = 0;
    std::uint32_t expectedCount_ = 0;
    std::uint32_t transactionSerial_ = 0;
    ZoneScriptStringOwnershipPhase phase_ =
        ZoneScriptStringOwnershipPhase::Empty;
    ZoneScriptStringOwnershipPhase resumePhase_ =
        ZoneScriptStringOwnershipPhase::Empty;
    std::uint8_t reserved_[2]{};
};

RUNTIME_SIZE(ZoneScriptStringOwnershipController, 0x40, 0x58);

#ifdef KISAK_DB_ZONE_SCRIPT_STRING_OWNERSHIP_TESTING
struct ZoneScriptStringOwnershipControllerTestAccess final
{
    static void SetJournal(
        ZoneScriptStringOwnershipController *const controller,
        script_string_journal::ScriptStringJournal *const journal) noexcept
    {
        if (controller)
            controller->journal_ = journal;
    }

    static void SetStorage(
        ZoneScriptStringOwnershipController *const controller,
        script_string_journal::ScriptStringJournalEntry *const storage) noexcept
    {
        if (controller)
            controller->storage_ = storage;
    }

    static void SetKey(
        ZoneScriptStringOwnershipController *const controller,
        const zone_load::ZoneLoadContextKey &key) noexcept
    {
        if (controller)
            controller->key_ = key;
    }

    static void SetPhase(
        ZoneScriptStringOwnershipController *const controller,
        const ZoneScriptStringOwnershipPhase phase) noexcept
    {
        if (controller)
            controller->phase_ = phase;
    }

    static void SetLifecycle(
        ZoneScriptStringOwnershipController *const controller,
        zone_load::ZoneLoadContextSlot *const lifecycle) noexcept
    {
        if (controller)
            controller->lifecycle_ = lifecycle;
    }

    static void SetCleanupBinding(
        ZoneScriptStringOwnershipController *const controller,
        void *const context,
        zone_load::ZoneLoadCleanupCallbackStatus (*const perform)(
            void *, zone_load::ZoneLoadCleanupOperation) noexcept) noexcept
    {
        if (!controller)
            return;
        controller->rollbackContext_ = context;
        controller->performCleanup_ = perform;
    }

    static void SetTransactionSerial(
        ZoneScriptStringOwnershipController *const controller,
        const std::uint32_t serial) noexcept
    {
        if (controller)
            controller->transactionSerial_ = serial;
    }

    static void SetResumePhase(
        ZoneScriptStringOwnershipController *const controller,
        const ZoneScriptStringOwnershipPhase phase) noexcept
    {
        if (controller)
            controller->resumePhase_ = phase;
    }

    static void SetReserved(
        ZoneScriptStringOwnershipController *const controller,
        const std::uint8_t first,
        const std::uint8_t second) noexcept
    {
        if (!controller)
            return;
        controller->reserved_[0] = first;
        controller->reserved_[1] = second;
    }
};
#endif

// lifecycle must already be an externally serialized, claimed Loading slot.
// Begin validates its exact nonzero key, acquires the global serializer, then
// initializes and binds the fixed-capacity journal. expectedCount must include
// every staged acquisition; dynamic claims need a separately bounded journal
// and are intentionally outside this controller. On every recoverable failure
// Begin releases a serializer acquisition it made and leaves controller output
// state unchanged. An impossible invalid transaction serial poisons the bound
// controller and deliberately retains the serializer.
[[nodiscard]] ZoneScriptStringOwnershipStatus
TryBeginZoneScriptStringOwnership(
    ZoneScriptStringOwnershipController *controller,
    zone_load::ZoneLoadContextSlot *lifecycle,
    const zone_load::ZoneLoadContextKey &key,
    script_string_journal::ScriptStringJournal *journal,
    script_string_journal::ScriptStringJournalEntry *storage,
    std::uint32_t storageCapacity,
    std::uint32_t expectedCount) noexcept;

// outStringId is required and unchanged on failure. Success means the journal
// owns the returned ordinary reference and the caller may safely write the ID
// into still-unpublished zone materialization.
[[nodiscard]] ZoneScriptStringOwnershipStatus TryStageZoneScriptString(
    ZoneScriptStringOwnershipController *controller,
    const script_string_adapter::ScriptStringSourceView &source,
    std::uint32_t *outStringId) noexcept;

[[nodiscard]] ZoneScriptStringOwnershipStatus TrySealZoneScriptStrings(
    ZoneScriptStringOwnershipController *controller) noexcept;

[[nodiscard]] ZoneScriptStringOwnershipStatus
TryBeginZoneScriptStringTransfer(
    ZoneScriptStringOwnershipController *controller) noexcept;

// Processes at most one entry. Exact retries after Transferred are harmless.
[[nodiscard]] ZoneScriptStringOwnershipStatus
TryTransferNextZoneScriptString(
    ZoneScriptStringOwnershipController *controller) noexcept;

[[nodiscard]] ZoneScriptStringOwnershipStatus
TryPrepareZoneScriptStringCommit(
    ZoneScriptStringOwnershipController *controller) noexcept;

// The caller must complete all fallible materialization/registration and make
// the final assets reachable under the retained serializer before this call.
// This operation publishes lifecycle Live, unconditionally finalizes the
// prepared journal, invokes the no-fail admission callback, then releases the
// serializer. There is no rollback boundary after lifecycle publication.
[[nodiscard]] ZoneScriptStringOwnershipStatus
TryCommitZoneScriptStringsAndAdmit(
    ZoneScriptStringOwnershipController *controller,
    const ZoneScriptStringAdmissionCallback &admission) noexcept;

// First drives the bound convergent unpublication callback. Only after it
// reports Success does the controller publish lifecycle Abandoning and enter
// journal rollback. Retry retains every reference and the serializer. The
// exact callback identities become part of the controller binding.
[[nodiscard]] ZoneScriptStringOwnershipStatus
TryBeginZoneScriptStringRollback(
    ZoneScriptStringOwnershipController *controller,
    const ZoneScriptStringRollbackCallbacks &callbacks) noexcept;

[[nodiscard]] ZoneScriptStringOwnershipStatus
TryRollbackNextZoneScriptString(
    ZoneScriptStringOwnershipController *controller) noexcept;

// Valid only after the journal has detached its backing. Drives the complete
// zone abandonment recipe through the callbacks bound at rollback begin,
// releases the serializer only after the lifecycle slot publishes Empty, and
// retains an Abandoned receipt until explicit reset.
[[nodiscard]] ZoneScriptStringOwnershipStatus
TryFinishZoneScriptStringAbandonment(
    ZoneScriptStringOwnershipController *controller) noexcept;

// Drives a committed Live generation through the lifecycle's live-only
// cleanup recipe.  The controller acquires the outer script-string
// transaction, binds the exact callback identity, and retains both across
// Retry.  The same externally serialized thread must resume it.  Success
// retains an exact Unloaded receipt; no callback is replayed by an exact final
// retry.
[[nodiscard]] ZoneScriptStringOwnershipStatus
TryUnloadLiveZoneScriptStringOwnership(
    ZoneScriptStringOwnershipController *controller,
    zone_load::ZoneLoadContextSlot *expectedLifecycle,
    const zone_load::ZoneLoadContextKey &expectedKey,
    const zone_load::ZoneLoadCleanupCallbacks &callbacks) noexcept;

// Canonicalizes only the ownership controller after authenticating the exact
// lifecycle terminal receipt and its kind.  The lifecycle generation/receipt
// remains durable until the next claim atomically advances the generation.
// Repeating the exact reset while the controller is already canonical Empty is
// a no-op success.  This primitive remains production-neutral; durable tables
// must reauthenticate their own physical slot and key before calling it.
[[nodiscard]] ZoneScriptStringOwnershipStatus
TryResetTerminalZoneScriptStringOwnership(
    ZoneScriptStringOwnershipController *controller,
    zone_load::ZoneLoadContextSlot *expectedLifecycle,
    const zone_load::ZoneLoadContextKey &expectedKey,
    zone_load::ZoneLoadTerminalKind expectedTerminalKind) noexcept;

} // namespace db::zone_script_string_ownership

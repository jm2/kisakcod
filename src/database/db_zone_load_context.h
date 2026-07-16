#pragma once

#include <universal/kisak_abi.h>

#include <cstdint>

namespace db::zone_load
{
inline constexpr std::uint32_t kInvalidZoneLoadSlot = UINT32_MAX;

// A key names one use of one external zone-registry slot. Generation zero is
// never issued. The reserved word makes the object representation canonical
// and keeps the key exactly 16 bytes on every supported target.
struct alignas(8) ZoneLoadContextKey final
{
    std::uint64_t generation = 0;
    std::uint32_t slot = kInvalidZoneLoadSlot;
    std::uint32_t reserved = 0;

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return generation != 0 && slot != kInvalidZoneLoadSlot
            && reserved == 0;
    }

    [[nodiscard]] constexpr bool operator==(
        const ZoneLoadContextKey &) const noexcept = default;
};

RUNTIME_SIZE(ZoneLoadContextKey, 0x10, 0x10);

enum class ZoneLoadContextPhase : std::uint8_t
{
    Empty,
    Loading,
    Live,
    Abandoning,
};

enum class ZoneLoadTerminalKind : std::uint8_t
{
    None,
    Abandoned,
    Unloaded,
};

// Cleanup callbacks cover only external engine work. ReleaseSlot is an
// internal controller transition and therefore can never be skipped or
// reported successful before the preceding operations have completed.
enum class ZoneLoadCleanupOperation : std::uint8_t
{
    CancelLoadInputAndInflate,
    AbortNativeAdapterTransactions,
    MakePartialAssetsAndStagedReferencesUnreachable,
    RemoveLiveAssetsAndReferences,
    InvalidateAliasDirectStreamAndDelayState,
    ReleaseGeometry,
    TearDownNativeArenaWorkspaceAndSidecars,
    EndPhysicalMemoryAllocation,
    FreePhysicalMemory,
    ClearRegistryLoadingQueueGateAndSignal,
    RemoveLiveRegistryAndHandles,
    ReleaseSlot,
};

enum class ZoneLoadCleanupCallbackStatus : std::uint8_t
{
    Success,
    Retry,
    UnsafeFailure,
};

enum class ZoneLoadContextStatus : std::uint8_t
{
    Success,
    Retry,
    Busy,
    InvalidArgument,
    InvalidState,
    InvalidKey,
    StaleKey,
    InvalidPhase,
    GenerationExhausted,
    UnsafeFailure,
};

struct ZoneLoadCleanupCallbacks final
{
    void *context = nullptr;
    // Every callback is a convergent ensure-postcondition operation. If the
    // named postcondition already holds, including because normal-path loading
    // completed it, return Success without replaying one-shot side effects.
    // Success means the cleanup is durably complete and safe to skip on every
    // later retry. Retry means it is not durably complete, retains
    // all required ownership and metadata, and is safe to invoke again even if
    // it made visible partial progress. UnsafeFailure means completion is
    // indeterminate and permanently poisons the slot. The callback context
    // and all metadata needed to invoke remaining operations must outlive the
    // complete Abandoning interval. A released resource must remain valid until
    // its corresponding callback returns Success. The final callback must
    // retain external serialization until the controller
    // publishes Empty and TryFinish/TryUnload returns.
    //
    // The callback must not throw, longjmp, call Com_Error, or otherwise leave
    // nonlocally; a nonlocal exit leaves cleanupActive set so the slot remains
    // fail-closed instead of being released out of order.
    ZoneLoadCleanupCallbackStatus (*perform)(
        void *context,
        ZoneLoadCleanupOperation operation) noexcept = nullptr;
};

// One caller-owned lifecycle object lives outside the native XZone array for
// each registry slot. It is explicitly initialized and never allocates. Its
// destructor deliberately performs no cleanup: every transition, including
// error abandonment, must be driven through the functions below.
//
// The slot storage must live outside and outlast every per-generation PMem
// allocation that a cleanup recipe can free: the controller must remain valid
// after FreePhysicalMemory so it can publish Empty and return. The object is
// non-copyable so duplicating a live slot cannot duplicate ownership. Read-only
// accessors expose its fixed-layout state for portable diagnostics. Production
// callers have no mutation escape hatch; every public transition validates the
// complete representation and fails closed.
//
// This object has no internal synchronization. Callers must externally
// serialize initialization and every transition. They must also serialize
// every accessor and KeyMatches, callback execution, and destruction for each
// slot. Busy detects callback reentry while cleanup is active; it is not
// cross-thread synchronization.
#ifdef KISAK_DB_ZONE_LOAD_CONTEXT_TESTING
struct ZoneLoadContextSlotTestAccess;
#endif

class alignas(8) ZoneLoadContextSlot final
{
public:
    ZoneLoadContextSlot() noexcept = default;
    ~ZoneLoadContextSlot() noexcept = default;

    ZoneLoadContextSlot(const ZoneLoadContextSlot &) = delete;
    ZoneLoadContextSlot &operator=(const ZoneLoadContextSlot &) = delete;
    ZoneLoadContextSlot(ZoneLoadContextSlot &&) = delete;
    ZoneLoadContextSlot &operator=(ZoneLoadContextSlot &&) = delete;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::uint32_t slotIndex() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] ZoneLoadContextPhase phase() const noexcept;
    [[nodiscard]] ZoneLoadTerminalKind terminalKind() const noexcept;
    [[nodiscard]] ZoneLoadCleanupOperation
    nextCleanupOperation() const noexcept;
    [[nodiscard]] bool cleanupActive() const noexcept;
    [[nodiscard]] bool cleanupPoisoned() const noexcept;

private:
    friend ZoneLoadContextStatus TryInitializeZoneLoadContextSlot(
        ZoneLoadContextSlot *slot,
        std::uint32_t slotIndex,
        std::uint64_t initialGeneration) noexcept;
    friend ZoneLoadContextStatus TryClaimZoneLoadContext(
        ZoneLoadContextSlot *slot,
        ZoneLoadContextKey *inOutKey) noexcept;
    friend ZoneLoadContextStatus TryCommitZoneLoadContext(
        ZoneLoadContextSlot *slot,
        const ZoneLoadContextKey &key) noexcept;
    friend ZoneLoadContextStatus TryBeginZoneLoadContextAbandonment(
        ZoneLoadContextSlot *slot,
        const ZoneLoadContextKey &key) noexcept;
    friend ZoneLoadContextStatus TryFinishZoneLoadContextAbandonment(
        ZoneLoadContextSlot *slot,
        const ZoneLoadContextKey &key,
        const ZoneLoadCleanupCallbacks &callbacks) noexcept;
    friend ZoneLoadContextStatus TryUnloadZoneLoadContext(
        ZoneLoadContextSlot *slot,
        const ZoneLoadContextKey &key,
        const ZoneLoadCleanupCallbacks &callbacks) noexcept;
    friend bool ZoneLoadContextKeyMatches(
        const ZoneLoadContextSlot *slot,
        const ZoneLoadContextKey &key) noexcept;
#ifdef KISAK_DB_ZONE_LOAD_CONTEXT_TESTING
    friend struct ZoneLoadContextSlotTestAccess;
#endif

    [[nodiscard]] bool isCanonical() const noexcept;
    [[nodiscard]] ZoneLoadContextStatus validate() const noexcept;
    [[nodiscard]] ZoneLoadContextStatus validateKey(
        const ZoneLoadContextKey &key) const noexcept;
    void poisonCleanup() noexcept;
    [[nodiscard]] ZoneLoadContextStatus runCleanup(
        const ZoneLoadCleanupCallbacks &callbacks) noexcept;

    std::uint64_t generation_ = 0;
    std::uint32_t slotIndex_ = kInvalidZoneLoadSlot;
    ZoneLoadContextPhase phase_ = ZoneLoadContextPhase::Empty;
    ZoneLoadCleanupOperation nextCleanupOperation_ =
        ZoneLoadCleanupOperation::CancelLoadInputAndInflate;
    ZoneLoadTerminalKind terminalKind_ = ZoneLoadTerminalKind::None;
    std::uint8_t flags_ = 0;
};

RUNTIME_SIZE(ZoneLoadContextSlot, 0x10, 0x10);

#ifdef KISAK_DB_ZONE_LOAD_CONTEXT_TESTING
// Tests opt in before including this header. Production code has no mutation
// escape hatch around the checked lifecycle API.
struct ZoneLoadContextSlotTestAccess final
{
    static void SetPhase(
        ZoneLoadContextSlot *const slot,
        const ZoneLoadContextPhase phase) noexcept
    {
        if (slot)
            slot->phase_ = phase;
    }

    static void SetCleanupOperation(
        ZoneLoadContextSlot *const slot,
        const ZoneLoadCleanupOperation operation) noexcept
    {
        if (slot)
            slot->nextCleanupOperation_ = operation;
    }

    static void SetTerminalKind(
        ZoneLoadContextSlot *const slot,
        const ZoneLoadTerminalKind terminalKind) noexcept
    {
        if (slot)
            slot->terminalKind_ = terminalKind;
    }

    static void SetFlags(
        ZoneLoadContextSlot *const slot,
        const std::uint8_t flags) noexcept
    {
        if (slot)
            slot->flags_ = flags;
    }
};
#endif

// Initializes one external slot. initialGeneration is the last generation
// already consumed by a reconstructed table; zero starts a fresh slot. A slot
// initialized with UINT64_MAX is valid but permanently refuses a new claim.
[[nodiscard]] ZoneLoadContextStatus TryInitializeZoneLoadContextSlot(
    ZoneLoadContextSlot *slot,
    std::uint32_t slotIndex,
    std::uint64_t initialGeneration = 0) noexcept;

// A default-constructed inOutKey requests a new claim (an actually all-zero
// object is invalid because its slot is not kInvalidZoneLoadSlot). Success
// publishes a nonzero generation and enters Loading. Repeating the call with
// that exact key while Loading succeeds without advancing the generation. A
// key for another slot/generation is stale; the current key in another phase
// is invalid for claim. Output is unchanged on failure.
[[nodiscard]] ZoneLoadContextStatus TryClaimZoneLoadContext(
    ZoneLoadContextSlot *slot,
    ZoneLoadContextKey *inOutKey) noexcept;

// Loading -> Live. Under one external serializer, production integration must
// first complete every fallible conversion/registration/publication step and
// all load-only closure work, including input/inflate completion or
// cancellation, native-adapter transaction closure, and PMem EndAlloc. Keep
// loading/queue/recovery admission closed while final live state is assembled.
// TryCommit then publishes Live. Only after Success may the caller
// perform the infallible, no-drop gate/signal release that admits the zone, with
// no fallible or nonlocal operation in between. Drop the same external
// serializer last. Repeating the exact commit while Live is a no-op success.
// The subsequent admission release must also ensure its postcondition without
// replaying one-shot side effects.
[[nodiscard]] ZoneLoadContextStatus TryCommitZoneLoadContext(
    ZoneLoadContextSlot *slot,
    const ZoneLoadContextKey &key) noexcept;

// Loading -> Abandoning. A committed Live slot must use TryUnload instead so
// load-only cleanup cannot be repeated. Repeating with the exact key, including
// after a completed abandonment but before slot reuse, is a no-op success. A
// poisoned cleanup remains UnsafeFailure and can never release the slot.
[[nodiscard]] ZoneLoadContextStatus TryBeginZoneLoadContextAbandonment(
    ZoneLoadContextSlot *slot,
    const ZoneLoadContextKey &key) noexcept;

// Drives the remaining mandatory cleanup operations in order: cancel load
// input/inflate; abort nested native-adapter transactions; make partial assets,
// staged references, and current-zone copy records unreachable; invalidate
// alias/direct/stream/delay state; release geometry; destroy native arena,
// workspace, and sidecars; end the physical-memory allocation; free physical
// memory; clear registry/loading/queue/recovery-gate/signal state; then release
// the slot internally. Normal-path loading may already have satisfied an early
// stage; convergent callbacks report Success without replaying its side effects.
// The exact next operation is retained across Retry. UnsafeFailure (or an
// unknown callback value) poisons the slot and fails closed. The Abandoned
// terminal receipt permits an exact final retry.
[[nodiscard]] ZoneLoadContextStatus TryFinishZoneLoadContextAbandonment(
    ZoneLoadContextSlot *slot,
    const ZoneLoadContextKey &key,
    const ZoneLoadCleanupCallbacks &callbacks) noexcept;

// Live -> Abandoning(Unloaded), then runs only live-owned teardown: remove live
// assets/references; invalidate alias/direct/stream/delay state; release
// geometry; destroy the native arena/workspace/sidecars; free physical memory;
// remove live registry/handle state; then release the slot internally. It must
// never replay load-only cancel/abort, PMem EndAlloc, or loading-gate/signal
// work. Retry resumes at the first incomplete operation. Repeating after
// release is a no-op success until the next claim advances the generation.
[[nodiscard]] ZoneLoadContextStatus TryUnloadZoneLoadContext(
    ZoneLoadContextSlot *slot,
    const ZoneLoadContextKey &key,
    const ZoneLoadCleanupCallbacks &callbacks) noexcept;

// Active-ownership predicate only. Terminal receipts intentionally return
// false after slot release even though the final idempotent operation may
// still recognize the exact key until the next generation is claimed.
[[nodiscard]] bool ZoneLoadContextKeyMatches(
    const ZoneLoadContextSlot *slot,
    const ZoneLoadContextKey &key) noexcept;
} // namespace db::zone_load

#pragma once

#include <cstdint>

namespace fx::archive
{
// The archive gate has two closed states. Pending stops new iterator
// admission while allowing already-admitted allocator work to drain;
// Exclusive also blocks allocator admission after the iterator count reaches
// exclusive ownership.
enum class ArchiveGateValue : std::int32_t
{
    Open = 0,
    Pending = 1,
    Exclusive = 2,
};

[[nodiscard]] constexpr bool ArchiveGateBlocksIteratorAdmission(
    const ArchiveGateValue value) noexcept
{
    // Unknown encoded values are corruption and must not admit work.
    return value != ArchiveGateValue::Open;
}

[[nodiscard]] constexpr bool ArchiveGateBlocksAllocatorAdmission(
    const ArchiveGateValue value) noexcept
{
    // Pending is the only closed value that deliberately permits allocator
    // drain. Unknown encoded values fail closed.
    return value != ArchiveGateValue::Open
        && value != ArchiveGateValue::Pending;
}

// A writer that observed Open before claiming iterator exclusivity may race
// with a normal archive claim changing the gate to Pending. Both states are a
// valid completion snapshot; Exclusive and unknown encodings fail closed.
[[nodiscard]] constexpr bool ArchiveGateAllowsPrecheckedExclusiveCompletion(
    const ArchiveGateValue value) noexcept
{
    return value == ArchiveGateValue::Open
        || value == ArchiveGateValue::Pending;
}

enum class ArchiveGateOwnerPhase : std::uint8_t
{
    Idle,
    Pending,
    PendingExclusive,
    Acquired,
    ExclusiveGateOnly,
};

// Every non-idle phase records the exact resources still owned. The state is
// intentionally caller-owned so thread-local engine adapters can preserve it
// across a failed cleanup and retry without allocating.
struct ArchiveGateOwnerState
{
    const void *identity = nullptr;
    std::uint32_t generation = 0;
    ArchiveGateOwnerPhase phase = ArchiveGateOwnerPhase::Idle;
};

enum class ArchiveGateControlOperation : std::uint8_t
{
    ClaimPending,
    TryAcquireIterator,
    WaitForIteratorProgress,
    PromoteExclusive,
    ValidateAdmission,
    ValidateExclusive,
    ReleaseIterator,
    ClearArchivingForError,
    ReopenPending,
    ReopenExclusive,
};

// A callback reports Success only after completing the operation named by the
// controller. Retry means the operation made no ownership change and may be
// attempted again by the caller/controller. Rejected and Cancelled are safe
// terminal refusals. UnsafeFailure includes indeterminate or invalid results.
enum class ArchiveGateControlStatus : std::uint8_t
{
    Success,
    Retry,
    Rejected,
    Cancelled,
    UnsafeFailure,
};

struct ArchiveGateControlCallbacks
{
    void *context = nullptr;
    ArchiveGateControlStatus (*invoke)(
        void *context,
        ArchiveGateControlOperation operation) noexcept = nullptr;
};

// Claims the pending gate, deterministically retries iterator acquisition via
// WaitForIteratorProgress, promotes the gate, and validates the admission
// snapshot. A failed promotion or validation is rolled back. If rollback is
// incomplete, UnsafeFailure is returned and state retains the exact owned
// phase for AbandonArchiveGateForError to retry.
[[nodiscard]] ArchiveGateControlStatus AcquireArchiveGate(
    ArchiveGateOwnerState *state,
    const void *identity,
    std::uint32_t generation,
    const ArchiveGateControlCallbacks &callbacks) noexcept;

// Releases a fully acquired archive in iterator-before-gate order. Identity
// and generation must still match. Partial cleanup is retained in state.
[[nodiscard]] ArchiveGateControlStatus ReleaseArchiveGate(
    ArchiveGateOwnerState *state,
    const void *identity,
    std::uint32_t currentGeneration,
    const ArchiveGateControlCallbacks &callbacks) noexcept;

// Error cleanup is phase-aware and retryable. It clears the engine's
// isArchiving marker after any iterator release and before reopening the gate.
[[nodiscard]] ArchiveGateControlStatus AbandonArchiveGateForError(
    ArchiveGateOwnerState *state,
    std::uint32_t currentGeneration,
    const ArchiveGateControlCallbacks &callbacks) noexcept;

[[nodiscard]] bool ArchiveGateOwnerMatches(
    const ArchiveGateOwnerState *state,
    const void *identity,
    std::uint32_t currentGeneration) noexcept;

// A lifecycle reset performed while the archive remains fully acquired may
// advance its generation. No pending or partially released phase can be
// refreshed, because doing so would legitimize stale ownership.
[[nodiscard]] bool RefreshArchiveGateOwnerGeneration(
    ArchiveGateOwnerState *state,
    const void *identity,
    std::uint32_t newGeneration) noexcept;
} // namespace fx::archive

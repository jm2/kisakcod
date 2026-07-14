#include "fx_archive_gate_control.h"

namespace fx::archive
{
namespace
{
[[nodiscard]] constexpr bool IsKnownPhase(
    const ArchiveGateOwnerPhase phase) noexcept
{
    switch (phase)
    {
    case ArchiveGateOwnerPhase::Idle:
    case ArchiveGateOwnerPhase::Pending:
    case ArchiveGateOwnerPhase::PendingExclusive:
    case ArchiveGateOwnerPhase::Acquired:
    case ArchiveGateOwnerPhase::ExclusiveGateOnly:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsKnownStatus(
    const ArchiveGateControlStatus status) noexcept
{
    switch (status)
    {
    case ArchiveGateControlStatus::Success:
    case ArchiveGateControlStatus::Retry:
    case ArchiveGateControlStatus::Rejected:
    case ArchiveGateControlStatus::Cancelled:
    case ArchiveGateControlStatus::UnsafeFailure:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr bool IsCanonicalState(
    const ArchiveGateOwnerState &state) noexcept
{
    if (!IsKnownPhase(state.phase))
        return false;
    if (state.phase == ArchiveGateOwnerPhase::Idle)
        return !state.identity && state.generation == 0;
    return state.identity != nullptr;
}

void ClearOwner(ArchiveGateOwnerState *const state) noexcept
{
    *state = {};
}

[[nodiscard]] ArchiveGateControlStatus Invoke(
    const ArchiveGateControlCallbacks &callbacks,
    const ArchiveGateControlOperation operation) noexcept
{
    const ArchiveGateControlStatus status =
        callbacks.invoke(callbacks.context, operation);
    return IsKnownStatus(status)
        ? status
        : ArchiveGateControlStatus::UnsafeFailure;
}

[[nodiscard]] bool HasCallbacks(
    const ArchiveGateControlCallbacks &callbacks) noexcept
{
    return callbacks.context && callbacks.invoke;
}

[[nodiscard]] ArchiveGateControlStatus ReopenOwnedGate(
    ArchiveGateOwnerState *const state,
    const ArchiveGateControlCallbacks &callbacks) noexcept
{
    ArchiveGateControlOperation operation{};
    switch (state->phase)
    {
    case ArchiveGateOwnerPhase::Pending:
        operation = ArchiveGateControlOperation::ReopenPending;
        break;
    case ArchiveGateOwnerPhase::ExclusiveGateOnly:
        operation = ArchiveGateControlOperation::ReopenExclusive;
        break;
    default:
        return ArchiveGateControlStatus::UnsafeFailure;
    }

    if (Invoke(callbacks, operation) != ArchiveGateControlStatus::Success)
        return ArchiveGateControlStatus::UnsafeFailure;
    ClearOwner(state);
    return ArchiveGateControlStatus::Success;
}

[[nodiscard]] ArchiveGateControlStatus ReleaseIteratorForCleanup(
    ArchiveGateOwnerState *const state,
    const ArchiveGateControlCallbacks &callbacks) noexcept
{
    const ArchiveGateOwnerPhase sourcePhase = state->phase;
    if (sourcePhase != ArchiveGateOwnerPhase::PendingExclusive
        && sourcePhase != ArchiveGateOwnerPhase::Acquired)
    {
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    if (Invoke(callbacks, ArchiveGateControlOperation::ReleaseIterator)
        != ArchiveGateControlStatus::Success)
    {
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    state->phase = sourcePhase == ArchiveGateOwnerPhase::PendingExclusive
        ? ArchiveGateOwnerPhase::Pending
        : ArchiveGateOwnerPhase::ExclusiveGateOnly;
    return ArchiveGateControlStatus::Success;
}

[[nodiscard]] ArchiveGateControlStatus RollBackAcquire(
    ArchiveGateOwnerState *const state,
    const ArchiveGateControlCallbacks &callbacks,
    const ArchiveGateControlStatus originalStatus) noexcept
{
    if (state->phase == ArchiveGateOwnerPhase::PendingExclusive
        || state->phase == ArchiveGateOwnerPhase::Acquired)
    {
        if (ReleaseIteratorForCleanup(state, callbacks)
            != ArchiveGateControlStatus::Success)
        {
            return ArchiveGateControlStatus::UnsafeFailure;
        }
    }
    if (ReopenOwnedGate(state, callbacks)
        != ArchiveGateControlStatus::Success)
    {
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    return originalStatus;
}
} // namespace

ArchiveGateControlStatus AcquireArchiveGate(
    ArchiveGateOwnerState *const state,
    const void *const identity,
    const std::uint32_t generation,
    const ArchiveGateControlCallbacks &callbacks) noexcept
{
    if (!state || !identity || !HasCallbacks(callbacks))
        return ArchiveGateControlStatus::UnsafeFailure;
    if (!IsCanonicalState(*state))
        return ArchiveGateControlStatus::UnsafeFailure;
    if (state->phase != ArchiveGateOwnerPhase::Idle)
        return ArchiveGateControlStatus::Rejected;

    const ArchiveGateControlStatus claimStatus =
        Invoke(callbacks, ArchiveGateControlOperation::ClaimPending);
    if (claimStatus != ArchiveGateControlStatus::Success)
        return claimStatus;

    state->identity = identity;
    state->generation = generation;
    state->phase = ArchiveGateOwnerPhase::Pending;

    for (;;)
    {
        const ArchiveGateControlStatus acquireStatus = Invoke(
            callbacks,
            ArchiveGateControlOperation::TryAcquireIterator);
        if (acquireStatus == ArchiveGateControlStatus::Success)
        {
            state->phase = ArchiveGateOwnerPhase::PendingExclusive;
            break;
        }
        if (acquireStatus != ArchiveGateControlStatus::Retry)
            return RollBackAcquire(state, callbacks, acquireStatus);

        const ArchiveGateControlStatus waitStatus = Invoke(
            callbacks,
            ArchiveGateControlOperation::WaitForIteratorProgress);
        if (waitStatus != ArchiveGateControlStatus::Success
            && waitStatus != ArchiveGateControlStatus::Retry)
        {
            return RollBackAcquire(state, callbacks, waitStatus);
        }
    }

    const ArchiveGateControlStatus promoteStatus =
        Invoke(callbacks, ArchiveGateControlOperation::PromoteExclusive);
    if (promoteStatus != ArchiveGateControlStatus::Success)
        return RollBackAcquire(state, callbacks, promoteStatus);
    state->phase = ArchiveGateOwnerPhase::Acquired;

    const ArchiveGateControlStatus validationStatus =
        Invoke(callbacks, ArchiveGateControlOperation::ValidateAdmission);
    if (validationStatus != ArchiveGateControlStatus::Success)
        return RollBackAcquire(state, callbacks, validationStatus);
    return ArchiveGateControlStatus::Success;
}

ArchiveGateControlStatus ReleaseArchiveGate(
    ArchiveGateOwnerState *const state,
    const void *const identity,
    const std::uint32_t currentGeneration,
    const ArchiveGateControlCallbacks &callbacks) noexcept
{
    if (!state || !identity || !HasCallbacks(callbacks))
        return ArchiveGateControlStatus::UnsafeFailure;
    if (!IsCanonicalState(*state))
        return ArchiveGateControlStatus::UnsafeFailure;
    if (!ArchiveGateOwnerMatches(state, identity, currentGeneration)
        || (state->phase != ArchiveGateOwnerPhase::Acquired
            && state->phase
                != ArchiveGateOwnerPhase::ExclusiveGateOnly))
    {
        return ArchiveGateControlStatus::Rejected;
    }

    // A previous release may already have dropped iterator ownership before
    // the exclusive gate failed to reopen. Resume at the durable gate-only
    // phase instead of trying to validate or release the iterator twice.
    if (state->phase == ArchiveGateOwnerPhase::ExclusiveGateOnly)
        return ReopenOwnedGate(state, callbacks);

    const ArchiveGateControlStatus validationStatus =
        Invoke(callbacks, ArchiveGateControlOperation::ValidateExclusive);
    if (validationStatus != ArchiveGateControlStatus::Success)
        return validationStatus;
    if (ReleaseIteratorForCleanup(state, callbacks)
        != ArchiveGateControlStatus::Success)
    {
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    return ReopenOwnedGate(state, callbacks);
}

ArchiveGateControlStatus AbandonArchiveGateForError(
    ArchiveGateOwnerState *const state,
    const std::uint32_t currentGeneration,
    const ArchiveGateControlCallbacks &callbacks) noexcept
{
    if (!state || !HasCallbacks(callbacks))
        return ArchiveGateControlStatus::UnsafeFailure;
    if (!IsCanonicalState(*state))
        return ArchiveGateControlStatus::UnsafeFailure;
    if (state->phase == ArchiveGateOwnerPhase::Idle)
        return ArchiveGateControlStatus::Success;
    if (state->generation != currentGeneration)
        return ArchiveGateControlStatus::Rejected;

    if (state->phase == ArchiveGateOwnerPhase::PendingExclusive
        || state->phase == ArchiveGateOwnerPhase::Acquired)
    {
        if (ReleaseIteratorForCleanup(state, callbacks)
            != ArchiveGateControlStatus::Success)
        {
            return ArchiveGateControlStatus::UnsafeFailure;
        }
    }

    if (state->phase != ArchiveGateOwnerPhase::Pending
        && state->phase != ArchiveGateOwnerPhase::ExclusiveGateOnly)
    {
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    if (Invoke(callbacks,
            ArchiveGateControlOperation::ClearArchivingForError)
        != ArchiveGateControlStatus::Success)
    {
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    return ReopenOwnedGate(state, callbacks);
}

bool ArchiveGateOwnerMatches(
    const ArchiveGateOwnerState *const state,
    const void *const identity,
    const std::uint32_t currentGeneration) noexcept
{
    return state && identity && IsCanonicalState(*state)
        && state->phase != ArchiveGateOwnerPhase::Idle
        && state->identity == identity
        && state->generation == currentGeneration;
}

bool RefreshArchiveGateOwnerGeneration(
    ArchiveGateOwnerState *const state,
    const void *const identity,
    const std::uint32_t newGeneration) noexcept
{
    if (!state || !identity || !IsCanonicalState(*state)
        || state->phase != ArchiveGateOwnerPhase::Acquired
        || state->identity != identity)
    {
        return false;
    }
    state->generation = newGeneration;
    return true;
}
} // namespace fx::archive

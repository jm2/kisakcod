#include "EffectsCore/fx_archive_gate_control.h"

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <type_traits>
#include <utility>

namespace
{
using fx::archive::AbandonArchiveGateForError;
using fx::archive::AcquireArchiveGate;
using fx::archive::ArchiveGateControlCallbacks;
using fx::archive::ArchiveGateControlOperation;
using fx::archive::ArchiveGateControlStatus;
using fx::archive::ArchiveGateValue;
using fx::archive::ArchiveGateOwnerMatches;
using fx::archive::ArchiveGateOwnerPhase;
using fx::archive::ArchiveGateOwnerState;
using fx::archive::RefreshArchiveGateOwnerGeneration;
using fx::archive::ReleaseArchiveGate;

struct ScriptStep
{
    ArchiveGateControlOperation operation;
    ArchiveGateControlStatus status;
};

struct Script
{
    const ScriptStep *steps = nullptr;
    std::size_t stepCount = 0;
    std::size_t nextStep = 0;
    bool mismatch = false;
};

ArchiveGateControlStatus InvokeScript(
    void *const context,
    const ArchiveGateControlOperation operation) noexcept
{
    Script *const script = static_cast<Script *>(context);
    if (!script || script->nextStep >= script->stepCount)
    {
        if (script)
            script->mismatch = true;
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    const ScriptStep &step = script->steps[script->nextStep++];
    if (step.operation != operation)
    {
        script->mismatch = true;
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    return step.status;
}

ArchiveGateControlCallbacks MakeCallbacks(Script *const script) noexcept
{
    return {script, InvokeScript};
}

bool ScriptComplete(const Script &script) noexcept
{
    return !script.mismatch && script.nextStep == script.stepCount;
}

bool IsIdle(const ArchiveGateOwnerState &state) noexcept
{
    return !state.identity && state.generation == 0
        && state.phase == ArchiveGateOwnerPhase::Idle;
}

struct FakeGateBackend
{
    ArchiveGateValue gate = ArchiveGateValue::Open;
    std::int32_t iteratorCount = 0;
    bool initialized = true;
    bool isArchiving = false;
    bool changeGenerationDuringWait = false;
    std::uint32_t generation = 1;
    std::uint32_t expectedGeneration = 1;
    ArchiveGateControlOperation operations[32]{};
    std::size_t operationCount = 0;
};

ArchiveGateControlStatus InvokeFakeBackend(
    void *const context,
    const ArchiveGateControlOperation operation) noexcept
{
    FakeGateBackend *const backend =
        static_cast<FakeGateBackend *>(context);
    if (!backend
        || backend->operationCount >= std::size(backend->operations))
    {
        return ArchiveGateControlStatus::UnsafeFailure;
    }
    backend->operations[backend->operationCount++] = operation;

    switch (operation)
    {
    case ArchiveGateControlOperation::ClaimPending:
        if (backend->gate != ArchiveGateValue::Open)
            return ArchiveGateControlStatus::Rejected;
        backend->gate = ArchiveGateValue::Pending;
        return ArchiveGateControlStatus::Success;
    case ArchiveGateControlOperation::TryAcquireIterator:
        if (backend->iteratorCount != 0)
            return ArchiveGateControlStatus::Retry;
        backend->iteratorCount = -1;
        return ArchiveGateControlStatus::Success;
    case ArchiveGateControlOperation::WaitForIteratorProgress:
        if (!backend->initialized || backend->isArchiving
            || backend->generation != backend->expectedGeneration)
        {
            return ArchiveGateControlStatus::Cancelled;
        }
        if (backend->changeGenerationDuringWait)
        {
            backend->changeGenerationDuringWait = false;
            ++backend->generation;
            return ArchiveGateControlStatus::Cancelled;
        }
        if (backend->iteratorCount > 0)
            --backend->iteratorCount;
        return ArchiveGateControlStatus::Retry;
    case ArchiveGateControlOperation::PromoteExclusive:
        if (backend->gate != ArchiveGateValue::Pending)
            return ArchiveGateControlStatus::UnsafeFailure;
        backend->gate = ArchiveGateValue::Exclusive;
        return ArchiveGateControlStatus::Success;
    case ArchiveGateControlOperation::ValidateAdmission:
        if (backend->gate != ArchiveGateValue::Exclusive
            || backend->iteratorCount != -1)
        {
            return ArchiveGateControlStatus::UnsafeFailure;
        }
        return backend->initialized && !backend->isArchiving
                && backend->generation == backend->expectedGeneration
            ? ArchiveGateControlStatus::Success
            : ArchiveGateControlStatus::Cancelled;
    case ArchiveGateControlOperation::ValidateExclusive:
        return backend->gate == ArchiveGateValue::Exclusive
                && backend->iteratorCount == -1
            ? ArchiveGateControlStatus::Success
            : ArchiveGateControlStatus::UnsafeFailure;
    case ArchiveGateControlOperation::ReleaseIterator:
        if (backend->iteratorCount != -1)
            return ArchiveGateControlStatus::UnsafeFailure;
        backend->iteratorCount = 0;
        return ArchiveGateControlStatus::Success;
    case ArchiveGateControlOperation::ClearArchivingForError:
        backend->isArchiving = false;
        return ArchiveGateControlStatus::Success;
    case ArchiveGateControlOperation::ReopenPending:
        if (backend->gate != ArchiveGateValue::Pending)
            return ArchiveGateControlStatus::UnsafeFailure;
        backend->gate = ArchiveGateValue::Open;
        return ArchiveGateControlStatus::Success;
    case ArchiveGateControlOperation::ReopenExclusive:
        if (backend->gate != ArchiveGateValue::Exclusive)
            return ArchiveGateControlStatus::UnsafeFailure;
        backend->gate = ArchiveGateValue::Open;
        return ArchiveGateControlStatus::Success;
    default:
        return ArchiveGateControlStatus::UnsafeFailure;
    }
}

ArchiveGateControlCallbacks MakeFakeCallbacks(
    FakeGateBackend *const backend) noexcept
{
    return {backend, InvokeFakeBackend};
}

bool FakeOperationsEqual(
    const FakeGateBackend &backend,
    const ArchiveGateControlOperation *const expected,
    const std::size_t expectedCount) noexcept
{
    if (backend.operationCount != expectedCount)
        return false;
    for (std::size_t index = 0; index < expectedCount; ++index)
    {
        if (backend.operations[index] != expected[index])
            return false;
    }
    return true;
}

bool TestStatefulImmediateAcquireReleaseAndOrder()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    FakeGateBackend backend{};
    backend.generation = 73u;
    backend.expectedGeneration = 73u;
    const ArchiveGateControlCallbacks callbacks =
        MakeFakeCallbacks(&backend);

    if (AcquireArchiveGate(
            &state, &identity, 73u, callbacks)
            != ArchiveGateControlStatus::Success
        || state.phase != ArchiveGateOwnerPhase::Acquired
        || backend.gate != ArchiveGateValue::Exclusive
        || backend.iteratorCount != -1)
    {
        return false;
    }
    constexpr ArchiveGateControlOperation acquireOrder[] = {
        ArchiveGateControlOperation::ClaimPending,
        ArchiveGateControlOperation::TryAcquireIterator,
        ArchiveGateControlOperation::PromoteExclusive,
        ArchiveGateControlOperation::ValidateAdmission,
    };
    if (!FakeOperationsEqual(
            backend, acquireOrder, std::size(acquireOrder)))
    {
        return false;
    }

    const std::size_t releaseStart = backend.operationCount;
    if (ReleaseArchiveGate(
            &state, &identity, 73u, callbacks)
            != ArchiveGateControlStatus::Success
        || !IsIdle(state) || backend.gate != ArchiveGateValue::Open
        || backend.iteratorCount != 0)
    {
        return false;
    }
    constexpr ArchiveGateControlOperation releaseOrder[] = {
        ArchiveGateControlOperation::ValidateExclusive,
        ArchiveGateControlOperation::ReleaseIterator,
        ArchiveGateControlOperation::ReopenExclusive,
    };
    if (backend.operationCount - releaseStart != std::size(releaseOrder))
        return false;
    for (std::size_t index = 0; index < std::size(releaseOrder); ++index)
    {
        if (backend.operations[releaseStart + index] != releaseOrder[index])
            return false;
    }
    return true;
}

bool TestStatefulBusyAttemptsAndGenerationCancellation()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    FakeGateBackend backend{};
    backend.iteratorCount = 3;
    backend.generation = 80u;
    backend.expectedGeneration = 80u;
    if (AcquireArchiveGate(
            &state,
            &identity,
            80u,
            MakeFakeCallbacks(&backend))
            != ArchiveGateControlStatus::Success
        || backend.gate != ArchiveGateValue::Exclusive
        || backend.iteratorCount != -1
        || backend.operationCount != 10u)
    {
        return false;
    }
    std::size_t tryCount = 0;
    std::size_t waitCount = 0;
    for (std::size_t index = 0; index < backend.operationCount; ++index)
    {
        if (backend.operations[index]
            == ArchiveGateControlOperation::TryAcquireIterator)
        {
            ++tryCount;
        }
        if (backend.operations[index]
            == ArchiveGateControlOperation::WaitForIteratorProgress)
        {
            ++waitCount;
        }
    }
    if (tryCount != 4u || waitCount != 3u)
        return false;

    state = {};
    backend = {};
    backend.iteratorCount = 1;
    backend.generation = 91u;
    backend.expectedGeneration = 91u;
    backend.changeGenerationDuringWait = true;
    if (AcquireArchiveGate(
            &state,
            &identity,
            91u,
            MakeFakeCallbacks(&backend))
            != ArchiveGateControlStatus::Cancelled
        || !IsIdle(state) || backend.gate != ArchiveGateValue::Open
        || backend.iteratorCount != 1 || backend.generation != 92u)
    {
        return false;
    }
    constexpr ArchiveGateControlOperation cancellationOrder[] = {
        ArchiveGateControlOperation::ClaimPending,
        ArchiveGateControlOperation::TryAcquireIterator,
        ArchiveGateControlOperation::WaitForIteratorProgress,
        ArchiveGateControlOperation::ReopenPending,
    };
    return FakeOperationsEqual(
        backend,
        cancellationOrder,
        std::size(cancellationOrder));
}

bool TestInvalidAndDoubleTransitions()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    FakeGateBackend backend{};
    backend.generation = 6u;
    backend.expectedGeneration = 6u;
    const ArchiveGateControlCallbacks callbacks =
        MakeFakeCallbacks(&backend);
    if (AcquireArchiveGate(&state, &identity, 6u, callbacks)
        != ArchiveGateControlStatus::Success)
    {
        return false;
    }

    const std::size_t acquiredOperations = backend.operationCount;
    if (AcquireArchiveGate(&state, &identity, 6u, callbacks)
            != ArchiveGateControlStatus::Rejected
        || backend.operationCount != acquiredOperations)
    {
        return false;
    }
    if (ReleaseArchiveGate(&state, &identity, 6u, callbacks)
            != ArchiveGateControlStatus::Success
        || backend.gate != ArchiveGateValue::Open
        || backend.iteratorCount != 0)
    {
        return false;
    }

    const std::size_t releasedOperations = backend.operationCount;
    if (ReleaseArchiveGate(&state, &identity, 6u, callbacks)
            != ArchiveGateControlStatus::Rejected
        || backend.operationCount != releasedOperations)
    {
        return false;
    }

    state = {&identity, 6u, ArchiveGateOwnerPhase::Pending};
    if (ReleaseArchiveGate(&state, &identity, 6u, callbacks)
            != ArchiveGateControlStatus::Rejected
        || backend.operationCount != releasedOperations)
    {
        return false;
    }

    state = {&identity, 6u, ArchiveGateOwnerPhase::Pending};
    backend.gate = ArchiveGateValue::Pending;
    if (AbandonArchiveGateForError(&state, 6u, callbacks)
            != ArchiveGateControlStatus::Success
        || !IsIdle(state) || backend.gate != ArchiveGateValue::Open)
    {
        return false;
    }
    const std::size_t abandonedOperations = backend.operationCount;
    return AbandonArchiveGateForError(&state, 6u, callbacks)
            == ArchiveGateControlStatus::Success
        && backend.operationCount == abandonedOperations && IsIdle(state);
}

bool TestLayoutNoexceptAndAdmissionSemantics()
{
    using fx::archive::ArchiveGateAllowsPrecheckedExclusiveCompletion;
    using fx::archive::ArchiveGateBlocksAllocatorAdmission;
    using fx::archive::ArchiveGateBlocksIteratorAdmission;
    using fx::archive::ArchiveGateValue;

    static_assert(std::is_standard_layout_v<ArchiveGateOwnerState>);
    static_assert(std::is_trivially_copyable_v<ArchiveGateOwnerState>);
    static_assert(sizeof(ArchiveGateOwnerState) == sizeof(void *) + 8u);
    static_assert(alignof(ArchiveGateOwnerState) == alignof(void *));
    static_assert(offsetof(ArchiveGateOwnerState, identity) == 0u);
    static_assert(offsetof(ArchiveGateOwnerState, generation)
        == sizeof(void *));
    static_assert(offsetof(ArchiveGateOwnerState, phase)
        == sizeof(void *) + sizeof(std::uint32_t));
    static_assert(sizeof(ArchiveGateOwnerPhase) == 1u);
    static_assert(sizeof(ArchiveGateControlOperation) == 1u);
    static_assert(sizeof(ArchiveGateControlStatus) == 1u);
    static_assert(static_cast<std::int32_t>(ArchiveGateValue::Open) == 0);
    static_assert(static_cast<std::int32_t>(ArchiveGateValue::Pending) == 1);
    static_assert(static_cast<std::int32_t>(ArchiveGateValue::Exclusive) == 2);
    static_assert(!ArchiveGateBlocksIteratorAdmission(ArchiveGateValue::Open));
    static_assert(ArchiveGateBlocksIteratorAdmission(
        ArchiveGateValue::Pending));
    static_assert(ArchiveGateBlocksIteratorAdmission(
        ArchiveGateValue::Exclusive));
    static_assert(!ArchiveGateBlocksAllocatorAdmission(ArchiveGateValue::Open));
    static_assert(!ArchiveGateBlocksAllocatorAdmission(
        ArchiveGateValue::Pending));
    static_assert(ArchiveGateBlocksAllocatorAdmission(
        ArchiveGateValue::Exclusive));
    static_assert(ArchiveGateAllowsPrecheckedExclusiveCompletion(
        ArchiveGateValue::Open));
    static_assert(ArchiveGateAllowsPrecheckedExclusiveCompletion(
        ArchiveGateValue::Pending));
    static_assert(!ArchiveGateAllowsPrecheckedExclusiveCompletion(
        ArchiveGateValue::Exclusive));
    static_assert(noexcept(AcquireArchiveGate(
        nullptr, nullptr, 0, std::declval<const ArchiveGateControlCallbacks &>())));
    static_assert(noexcept(ReleaseArchiveGate(
        nullptr, nullptr, 0, std::declval<const ArchiveGateControlCallbacks &>())));
    static_assert(noexcept(AbandonArchiveGateForError(
        nullptr, 0, std::declval<const ArchiveGateControlCallbacks &>())));
    static_assert(noexcept(ArchiveGateOwnerMatches(nullptr, nullptr, 0)));
    static_assert(noexcept(
        RefreshArchiveGateOwnerGeneration(nullptr, nullptr, 0)));

    const auto invalid = static_cast<ArchiveGateValue>(3);
    return ArchiveGateBlocksIteratorAdmission(invalid)
        && ArchiveGateBlocksAllocatorAdmission(invalid)
        && !ArchiveGateAllowsPrecheckedExclusiveCompletion(invalid);
}

bool TestImmediateAcquireAndRelease()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    constexpr ScriptStep acquireSteps[] = {
        {ArchiveGateControlOperation::ClaimPending,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::PromoteExclusive,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ValidateAdmission,
            ArchiveGateControlStatus::Success},
    };
    Script acquireScript{acquireSteps, std::size(acquireSteps)};
    if (AcquireArchiveGate(
            &state, &identity, 17u, MakeCallbacks(&acquireScript))
            != ArchiveGateControlStatus::Success
        || !ScriptComplete(acquireScript)
        || state.phase != ArchiveGateOwnerPhase::Acquired
        || !ArchiveGateOwnerMatches(&state, &identity, 17u))
    {
        return false;
    }

    constexpr ScriptStep releaseSteps[] = {
        {ArchiveGateControlOperation::ValidateExclusive,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenExclusive,
            ArchiveGateControlStatus::Success},
    };
    Script releaseScript{releaseSteps, std::size(releaseSteps)};
    return ReleaseArchiveGate(
               &state, &identity, 17u, MakeCallbacks(&releaseScript))
            == ArchiveGateControlStatus::Success
        && ScriptComplete(releaseScript) && IsIdle(state);
}

bool TestClaimBusyCancelAndInvalidStatus()
{
    int identity = 0;
    constexpr ArchiveGateControlStatus outcomes[] = {
        ArchiveGateControlStatus::Retry,
        ArchiveGateControlStatus::Rejected,
        ArchiveGateControlStatus::Cancelled,
        ArchiveGateControlStatus::UnsafeFailure,
        static_cast<ArchiveGateControlStatus>(255),
    };
    for (const ArchiveGateControlStatus callbackStatus : outcomes)
    {
        ArchiveGateOwnerState state{};
        const ScriptStep steps[] = {
            {ArchiveGateControlOperation::ClaimPending, callbackStatus},
        };
        Script script{steps, std::size(steps)};
        const ArchiveGateControlStatus expected =
            callbackStatus == static_cast<ArchiveGateControlStatus>(255)
            ? ArchiveGateControlStatus::UnsafeFailure
            : callbackStatus;
        if (AcquireArchiveGate(
                &state, &identity, 2u, MakeCallbacks(&script)) != expected
            || !ScriptComplete(script) || !IsIdle(state))
        {
            return false;
        }
    }
    return true;
}

bool TestIteratorContentionAndCancellation()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    constexpr ScriptStep successSteps[] = {
        {ArchiveGateControlOperation::ClaimPending,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Retry},
        {ArchiveGateControlOperation::WaitForIteratorProgress,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Retry},
        {ArchiveGateControlOperation::WaitForIteratorProgress,
            ArchiveGateControlStatus::Retry},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::PromoteExclusive,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ValidateAdmission,
            ArchiveGateControlStatus::Success},
    };
    Script successScript{successSteps, std::size(successSteps)};
    if (AcquireArchiveGate(
            &state, &identity, 9u, MakeCallbacks(&successScript))
            != ArchiveGateControlStatus::Success
        || !ScriptComplete(successScript)
        || state.phase != ArchiveGateOwnerPhase::Acquired)
    {
        return false;
    }

    state = {};
    constexpr ScriptStep cancelSteps[] = {
        {ArchiveGateControlOperation::ClaimPending,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Retry},
        {ArchiveGateControlOperation::WaitForIteratorProgress,
            ArchiveGateControlStatus::Cancelled},
        {ArchiveGateControlOperation::ReopenPending,
            ArchiveGateControlStatus::Success},
    };
    Script cancelScript{cancelSteps, std::size(cancelSteps)};
    return AcquireArchiveGate(
               &state, &identity, 9u, MakeCallbacks(&cancelScript))
            == ArchiveGateControlStatus::Cancelled
        && ScriptComplete(cancelScript) && IsIdle(state);
}

bool TestPromotionRollbackAndPartialCleanup()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    constexpr ScriptStep rollbackSteps[] = {
        {ArchiveGateControlOperation::ClaimPending,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::PromoteExclusive,
            ArchiveGateControlStatus::Rejected},
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenPending,
            ArchiveGateControlStatus::Success},
    };
    Script rollbackScript{rollbackSteps, std::size(rollbackSteps)};
    if (AcquireArchiveGate(
            &state, &identity, 31u, MakeCallbacks(&rollbackScript))
            != ArchiveGateControlStatus::Rejected
        || !ScriptComplete(rollbackScript) || !IsIdle(state))
    {
        return false;
    }

    constexpr ScriptStep partialSteps[] = {
        {ArchiveGateControlOperation::ClaimPending,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::PromoteExclusive,
            ArchiveGateControlStatus::Rejected},
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::UnsafeFailure},
    };
    Script partialScript{partialSteps, std::size(partialSteps)};
    if (AcquireArchiveGate(
            &state, &identity, 31u, MakeCallbacks(&partialScript))
            != ArchiveGateControlStatus::UnsafeFailure
        || !ScriptComplete(partialScript)
        || state.phase != ArchiveGateOwnerPhase::PendingExclusive)
    {
        return false;
    }

    constexpr ScriptStep abandonSteps[] = {
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ClearArchivingForError,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenPending,
            ArchiveGateControlStatus::Success},
    };
    Script abandonScript{abandonSteps, std::size(abandonSteps)};
    return AbandonArchiveGateForError(
               &state, 31u, MakeCallbacks(&abandonScript))
            == ArchiveGateControlStatus::Success
        && ScriptComplete(abandonScript) && IsIdle(state);
}

bool TestAdmissionValidationRollback()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    constexpr ScriptStep steps[] = {
        {ArchiveGateControlOperation::ClaimPending,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::PromoteExclusive,
            ArchiveGateControlStatus::Success},
        // This models initialization, archiving, identity, or lifecycle
        // generation changing after the admission snapshot.
        {ArchiveGateControlOperation::ValidateAdmission,
            ArchiveGateControlStatus::Rejected},
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenExclusive,
            ArchiveGateControlStatus::Success},
    };
    Script script{steps, std::size(steps)};
    return AcquireArchiveGate(
               &state, &identity, 44u, MakeCallbacks(&script))
            == ArchiveGateControlStatus::Rejected
        && ScriptComplete(script) && IsIdle(state);
}

bool TestReleasePartialCleanupRetry()
{
    int identity = 0;
    ArchiveGateOwnerState state{
        &identity, 5u, ArchiveGateOwnerPhase::Acquired};
    constexpr ScriptStep firstSteps[] = {
        {ArchiveGateControlOperation::ValidateExclusive,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenExclusive,
            ArchiveGateControlStatus::Retry},
    };
    Script firstScript{firstSteps, std::size(firstSteps)};
    if (ReleaseArchiveGate(
            &state, &identity, 5u, MakeCallbacks(&firstScript))
            != ArchiveGateControlStatus::UnsafeFailure
        || !ScriptComplete(firstScript)
        || state.phase != ArchiveGateOwnerPhase::ExclusiveGateOnly)
    {
        return false;
    }

    constexpr ScriptStep retrySteps[] = {
        {ArchiveGateControlOperation::ReopenExclusive,
            ArchiveGateControlStatus::Success},
    };
    Script retryScript{retrySteps, std::size(retrySteps)};
    if (ReleaseArchiveGate(
            &state, &identity, 5u, MakeCallbacks(&retryScript))
            != ArchiveGateControlStatus::Success
        || !ScriptComplete(retryScript) || !IsIdle(state))
    {
        return false;
    }

    state = {&identity, 5u, ArchiveGateOwnerPhase::Acquired};
    constexpr ScriptStep iteratorFailureSteps[] = {
        {ArchiveGateControlOperation::ValidateExclusive,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Rejected},
    };
    Script iteratorFailure{
        iteratorFailureSteps, std::size(iteratorFailureSteps)};
    if (ReleaseArchiveGate(
            &state, &identity, 5u, MakeCallbacks(&iteratorFailure))
            != ArchiveGateControlStatus::UnsafeFailure
        || !ScriptComplete(iteratorFailure)
        || state.phase != ArchiveGateOwnerPhase::Acquired)
    {
        return false;
    }

    constexpr ScriptStep finishSteps[] = {
        {ArchiveGateControlOperation::ValidateExclusive,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenExclusive,
            ArchiveGateControlStatus::Success},
    };
    Script finishScript{finishSteps, std::size(finishSteps)};
    return ReleaseArchiveGate(
               &state, &identity, 5u, MakeCallbacks(&finishScript))
            == ArchiveGateControlStatus::Success
        && ScriptComplete(finishScript) && IsIdle(state);
}

bool TestReleaseIdentityGenerationAndValidationFailures()
{
    int identity = 0;
    int otherIdentity = 0;
    ArchiveGateOwnerState state{
        &identity, 12u, ArchiveGateOwnerPhase::Acquired};
    Script noCalls{};
    const ArchiveGateControlCallbacks callbacks = MakeCallbacks(&noCalls);
    if (ReleaseArchiveGate(&state, &otherIdentity, 12u, callbacks)
            != ArchiveGateControlStatus::Rejected
        || ReleaseArchiveGate(&state, &identity, 13u, callbacks)
            != ArchiveGateControlStatus::Rejected
        || !ScriptComplete(noCalls)
        || state.phase != ArchiveGateOwnerPhase::Acquired)
    {
        return false;
    }

    constexpr ScriptStep validationSteps[] = {
        {ArchiveGateControlOperation::ValidateExclusive,
            ArchiveGateControlStatus::Rejected},
    };
    Script validationScript{validationSteps, std::size(validationSteps)};
    return ReleaseArchiveGate(
               &state, &identity, 12u, MakeCallbacks(&validationScript))
            == ArchiveGateControlStatus::Rejected
        && ScriptComplete(validationScript)
        && state.phase == ArchiveGateOwnerPhase::Acquired;
}

bool TestAbandonEveryPhase()
{
    int identity = 0;

    ArchiveGateOwnerState idle{};
    Script idleScript{};
    if (AbandonArchiveGateForError(
            &idle, 0u, MakeCallbacks(&idleScript))
            != ArchiveGateControlStatus::Success
        || !ScriptComplete(idleScript) || !IsIdle(idle))
    {
        return false;
    }

    constexpr ArchiveGateOwnerPhase phases[] = {
        ArchiveGateOwnerPhase::Pending,
        ArchiveGateOwnerPhase::PendingExclusive,
        ArchiveGateOwnerPhase::Acquired,
        ArchiveGateOwnerPhase::ExclusiveGateOnly,
    };
    for (const ArchiveGateOwnerPhase phase : phases)
    {
        const bool ownsIterator =
            phase == ArchiveGateOwnerPhase::PendingExclusive
            || phase == ArchiveGateOwnerPhase::Acquired;
        const bool pendingGate = phase == ArchiveGateOwnerPhase::Pending
            || phase == ArchiveGateOwnerPhase::PendingExclusive;
        ScriptStep steps[3]{};
        std::size_t count = 0;
        if (ownsIterator)
        {
            steps[count++] = {ArchiveGateControlOperation::ReleaseIterator,
                ArchiveGateControlStatus::Success};
        }
        steps[count++] = {
            ArchiveGateControlOperation::ClearArchivingForError,
            ArchiveGateControlStatus::Success};
        steps[count++] = {
            pendingGate
                ? ArchiveGateControlOperation::ReopenPending
                : ArchiveGateControlOperation::ReopenExclusive,
            ArchiveGateControlStatus::Success};

        ArchiveGateOwnerState state{&identity, 22u, phase};
        Script script{steps, count};
        if (AbandonArchiveGateForError(
                &state, 22u, MakeCallbacks(&script))
                != ArchiveGateControlStatus::Success
            || !ScriptComplete(script) || !IsIdle(state))
        {
            return false;
        }
    }
    return true;
}

bool TestAbandonPartialCleanupRetryAndStaleGeneration()
{
    int identity = 0;
    ArchiveGateOwnerState state{
        &identity, 28u, ArchiveGateOwnerPhase::Acquired};
    constexpr ScriptStep firstSteps[] = {
        {ArchiveGateControlOperation::ReleaseIterator,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ClearArchivingForError,
            ArchiveGateControlStatus::UnsafeFailure},
    };
    Script firstScript{firstSteps, std::size(firstSteps)};
    if (AbandonArchiveGateForError(
            &state, 28u, MakeCallbacks(&firstScript))
            != ArchiveGateControlStatus::UnsafeFailure
        || !ScriptComplete(firstScript)
        || state.phase != ArchiveGateOwnerPhase::ExclusiveGateOnly)
    {
        return false;
    }

    Script staleScript{};
    if (AbandonArchiveGateForError(
            &state, 29u, MakeCallbacks(&staleScript))
            != ArchiveGateControlStatus::Rejected
        || !ScriptComplete(staleScript)
        || state.phase != ArchiveGateOwnerPhase::ExclusiveGateOnly)
    {
        return false;
    }

    constexpr ScriptStep retrySteps[] = {
        {ArchiveGateControlOperation::ClearArchivingForError,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenExclusive,
            ArchiveGateControlStatus::Success},
    };
    Script retryScript{retrySteps, std::size(retrySteps)};
    if (AbandonArchiveGateForError(
            &state, 28u, MakeCallbacks(&retryScript))
            != ArchiveGateControlStatus::Success
        || !ScriptComplete(retryScript) || !IsIdle(state))
    {
        return false;
    }

    state = {&identity, 28u, ArchiveGateOwnerPhase::Pending};
    constexpr ScriptStep reopenFailureSteps[] = {
        {ArchiveGateControlOperation::ClearArchivingForError,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::ReopenPending,
            ArchiveGateControlStatus::Rejected},
    };
    Script reopenFailure{
        reopenFailureSteps, std::size(reopenFailureSteps)};
    return AbandonArchiveGateForError(
               &state, 28u, MakeCallbacks(&reopenFailure))
            == ArchiveGateControlStatus::UnsafeFailure
        && ScriptComplete(reopenFailure)
        && state.phase == ArchiveGateOwnerPhase::Pending;
}

bool TestOwnerMatchAndCheckedGenerationRefresh()
{
    int identity = 0;
    int otherIdentity = 0;
    ArchiveGateOwnerState state{
        &identity, 3u, ArchiveGateOwnerPhase::Acquired};
    if (!ArchiveGateOwnerMatches(&state, &identity, 3u)
        || ArchiveGateOwnerMatches(&state, &otherIdentity, 3u)
        || ArchiveGateOwnerMatches(&state, &identity, 4u)
        || !RefreshArchiveGateOwnerGeneration(&state, &identity, 4u)
        || !ArchiveGateOwnerMatches(&state, &identity, 4u)
        || RefreshArchiveGateOwnerGeneration(&state, &otherIdentity, 5u))
    {
        return false;
    }

    state.phase = ArchiveGateOwnerPhase::Pending;
    if (RefreshArchiveGateOwnerGeneration(&state, &identity, 5u))
        return false;
    state.phase = ArchiveGateOwnerPhase::ExclusiveGateOnly;
    if (RefreshArchiveGateOwnerGeneration(&state, &identity, 5u))
        return false;
    state.phase = ArchiveGateOwnerPhase::Idle;
    state.identity = nullptr;
    state.generation = 0;
    return !ArchiveGateOwnerMatches(&state, &identity, 0u)
        && !RefreshArchiveGateOwnerGeneration(&state, &identity, 1u);
}

bool TestCorruptStateAndNullCallbacksFailClosed()
{
    int identity = 0;
    Script noCalls{};
    const ArchiveGateControlCallbacks callbacks = MakeCallbacks(&noCalls);

    ArchiveGateOwnerState corruptIdle{
        &identity, 0u, ArchiveGateOwnerPhase::Idle};
    if (AcquireArchiveGate(&corruptIdle, &identity, 1u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure
        || ReleaseArchiveGate(
            &corruptIdle, &identity, 0u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure
        || AbandonArchiveGateForError(&corruptIdle, 0u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure
        || ArchiveGateOwnerMatches(&corruptIdle, &identity, 0u)
        || RefreshArchiveGateOwnerGeneration(
            &corruptIdle, &identity, 1u))
    {
        return false;
    }

    ArchiveGateOwnerState corruptActive{
        nullptr, 1u, ArchiveGateOwnerPhase::Acquired};
    if (ReleaseArchiveGate(&corruptActive, &identity, 1u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure
        || AbandonArchiveGateForError(&corruptActive, 1u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure)
    {
        return false;
    }

    ArchiveGateOwnerState unknown{
        &identity,
        1u,
        static_cast<ArchiveGateOwnerPhase>(255)};
    if (ReleaseArchiveGate(&unknown, &identity, 1u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure
        || AbandonArchiveGateForError(&unknown, 1u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure
        || ArchiveGateOwnerMatches(&unknown, &identity, 1u)
        || !ScriptComplete(noCalls))
    {
        return false;
    }

    ArchiveGateOwnerState state{};
    ArchiveGateControlCallbacks nullInvoke{&noCalls, nullptr};
    ArchiveGateControlCallbacks nullContext{nullptr, InvokeScript};
    if (AcquireArchiveGate(&state, &identity, 1u, nullInvoke)
            != ArchiveGateControlStatus::UnsafeFailure
        || AcquireArchiveGate(&state, &identity, 1u, nullContext)
            != ArchiveGateControlStatus::UnsafeFailure
        || AcquireArchiveGate(nullptr, &identity, 1u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure
        || AcquireArchiveGate(&state, nullptr, 1u, callbacks)
            != ArchiveGateControlStatus::UnsafeFailure)
    {
        return false;
    }

    state = {&identity, 1u, ArchiveGateOwnerPhase::Acquired};
    return ReleaseArchiveGate(&state, &identity, 1u, nullInvoke)
            == ArchiveGateControlStatus::UnsafeFailure
        && AbandonArchiveGateForError(&state, 1u, nullContext)
            == ArchiveGateControlStatus::UnsafeFailure
        && !ArchiveGateOwnerMatches(nullptr, &identity, 1u)
        && !RefreshArchiveGateOwnerGeneration(nullptr, &identity, 1u);
}

bool TestInvalidMidSequenceStatusRollsBack()
{
    int identity = 0;
    ArchiveGateOwnerState state{};
    constexpr ScriptStep steps[] = {
        {ArchiveGateControlOperation::ClaimPending,
            ArchiveGateControlStatus::Success},
        {ArchiveGateControlOperation::TryAcquireIterator,
            static_cast<ArchiveGateControlStatus>(254)},
        {ArchiveGateControlOperation::ReopenPending,
            ArchiveGateControlStatus::Success},
    };
    Script script{steps, std::size(steps)};
    return AcquireArchiveGate(
               &state, &identity, 1u, MakeCallbacks(&script))
            == ArchiveGateControlStatus::UnsafeFailure
        && ScriptComplete(script) && IsIdle(state);
}
} // namespace

int main()
{
    struct TestCase
    {
        const char *name;
        bool (*run)();
    };
    constexpr TestCase tests[] = {
        {"layout/noexcept/admission semantics",
            TestLayoutNoexceptAndAdmissionSemantics},
        {"stateful immediate/release ordering",
            TestStatefulImmediateAcquireReleaseAndOrder},
        {"stateful busy/generation cancellation",
            TestStatefulBusyAttemptsAndGenerationCancellation},
        {"invalid/double transitions", TestInvalidAndDoubleTransitions},
        {"immediate acquire/release", TestImmediateAcquireAndRelease},
        {"claim busy/cancel/invalid", TestClaimBusyCancelAndInvalidStatus},
        {"iterator contention/cancel", TestIteratorContentionAndCancellation},
        {"promotion rollback/partial", TestPromotionRollbackAndPartialCleanup},
        {"admission validation rollback", TestAdmissionValidationRollback},
        {"release partial cleanup retry", TestReleasePartialCleanupRetry},
        {"release identity/generation/validation",
            TestReleaseIdentityGenerationAndValidationFailures},
        {"abandon every phase", TestAbandonEveryPhase},
        {"abandon partial/stale generation",
            TestAbandonPartialCleanupRetryAndStaleGeneration},
        {"owner match/generation refresh",
            TestOwnerMatchAndCheckedGenerationRefresh},
        {"corrupt/null fail closed", TestCorruptStateAndNullCallbacksFailClosed},
        {"invalid mid-sequence status", TestInvalidMidSequenceStatusRollsBack},
    };

    for (const TestCase &test : tests)
    {
        if (!test.run())
        {
            std::fprintf(stderr, "FAIL: %s\n", test.name);
            return 1;
        }
    }
    std::printf("fx archive gate control tests passed (%zu cases)\n",
        std::size(tests));
    return 0;
}

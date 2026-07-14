#include "fx_archive_restore_control.h"

#include <cstddef>

namespace fx::archive
{
namespace
{
enum class SequenceResult : std::uint8_t
{
    Success,
    RecoverableFailure,
    UnsafeFailure,
};

template <std::size_t OperationCount>
[[nodiscard]] SequenceResult RunSequence(
    const RestoreControlCallbacks &callbacks,
    const RestoreControlOperation (&operations)[OperationCount]) noexcept
{
    for (const RestoreControlOperation operation : operations)
    {
        switch (callbacks.perform(callbacks.context, operation))
        {
        case RestoreControlOperationStatus::Success:
            break;
        case RestoreControlOperationStatus::RecoverableFailure:
            return SequenceResult::RecoverableFailure;
        case RestoreControlOperationStatus::UnsafeFailure:
            return SequenceResult::UnsafeFailure;
        default:
            return SequenceResult::UnsafeFailure;
        }
    }
    return SequenceResult::Success;
}

[[nodiscard]] RestoreControlOutcome PublishSafeEmpty(
    const RestoreControlCallbacks &callbacks) noexcept
{
    constexpr RestoreControlOperation operations[] = {
        RestoreControlOperation::PublishSafeEmpty,
    };
    return RunSequence(callbacks, operations)
            == SequenceResult::Success
        ? RestoreControlOutcome::SafeEmptyPublished
        : RestoreControlOutcome::UnsafeFailure;
}

[[nodiscard]] RestoreControlOutcome FinishRecovery(
    const RestoreControlCallbacks &callbacks,
    const SequenceResult recoveryResult) noexcept
{
    switch (recoveryResult)
    {
    case SequenceResult::Success:
        return RestoreControlOutcome::OriginalRestored;
    case SequenceResult::RecoverableFailure:
        return PublishSafeEmpty(callbacks);
    case SequenceResult::UnsafeFailure:
    default:
        return RestoreControlOutcome::UnsafeFailure;
    }
}

constexpr RestoreControlOperation PrepareOperations[] = {
    RestoreControlOperation::CaptureOriginal,
    RestoreControlOperation::PlanRetirement,
    RestoreControlOperation::RetireOriginal,
    RestoreControlOperation::PreparePhysicsReplacement,
    RestoreControlOperation::CreateDesiredPhysics,
    RestoreControlOperation::ValidateDesiredPhysics,
    RestoreControlOperation::PublishPhysicsReplacement,
};

constexpr RestoreControlOperation DesiredPublicationOperations[] = {
    RestoreControlOperation::PublishDesiredGraph,
    RestoreControlOperation::ValidateDesiredState,
};

constexpr RestoreControlOperation CommitOperations[] = {
    RestoreControlOperation::ValidateDiscardedOriginalPhysics,
    RestoreControlOperation::DrainNonLivePhysics,
};

constexpr RestoreControlOperation LiveGraphRecoveryOperations[] = {
    RestoreControlOperation::DrainNonLivePhysics,
    RestoreControlOperation::ValidateOriginalTokensInLiveGraph,
    RestoreControlOperation::ReconstructRetiredOriginalPhysics,
    RestoreControlOperation::PatchOriginalTokensInLiveGraph,
    RestoreControlOperation::ValidateOriginalGraph,
    RestoreControlOperation::ValidateOriginalPhysics,
};

constexpr RestoreControlOperation SnapshotRecoveryOperations[] = {
    RestoreControlOperation::RollbackPhysicsReplacement,
    RestoreControlOperation::DrainNonLivePhysics,
    RestoreControlOperation::ValidateOriginalTokensInSnapshot,
    RestoreControlOperation::ReconstructRetiredOriginalPhysics,
    RestoreControlOperation::PatchOriginalTokensInSnapshot,
    RestoreControlOperation::ValidateOriginalPhysics,
    RestoreControlOperation::PublishOriginalGraph,
    RestoreControlOperation::ValidateOriginalGraph,
};
} // namespace

RestoreControlOutcome RunRestoreControl(
    const RestoreControlCallbacks &callbacks) noexcept
{
    if (!callbacks.context || !callbacks.perform)
        return RestoreControlOutcome::UnsafeFailure;

    const SequenceResult prepareResult =
        RunSequence(callbacks, PrepareOperations);
    if (prepareResult == SequenceResult::UnsafeFailure)
        return RestoreControlOutcome::UnsafeFailure;
    if (prepareResult == SequenceResult::RecoverableFailure)
    {
        return FinishRecovery(
            callbacks,
            RunSequence(callbacks, LiveGraphRecoveryOperations));
    }

    const SequenceResult desiredPublicationResult =
        RunSequence(callbacks, DesiredPublicationOperations);
    if (desiredPublicationResult == SequenceResult::UnsafeFailure)
        return RestoreControlOutcome::UnsafeFailure;
    if (desiredPublicationResult == SequenceResult::RecoverableFailure)
    {
        return FinishRecovery(
            callbacks,
            RunSequence(callbacks, SnapshotRecoveryOperations));
    }

    const SequenceResult commitResult =
        RunSequence(callbacks, CommitOperations);
    if (commitResult == SequenceResult::Success)
        return RestoreControlOutcome::DesiredPublished;
    if (commitResult == SequenceResult::RecoverableFailure)
        return PublishSafeEmpty(callbacks);
    return RestoreControlOutcome::UnsafeFailure;
}
} // namespace fx::archive

#include "fx_archive_physics_batch_control.h"

#include <limits>

namespace fx::archive
{
namespace
{
[[nodiscard]] bool OutputOverlapsPlan(
    const std::size_t *const planIndices,
    const std::size_t selectedCount,
    const std::size_t *const outCompletedCount) noexcept
{
    if (!planIndices || selectedCount == 0 || !outCompletedCount)
        return false;
    if (selectedCount
        > (std::numeric_limits<std::uintptr_t>::max)()
            / sizeof(*planIndices))
    {
        return true;
    }

    const std::uintptr_t planBegin =
        reinterpret_cast<std::uintptr_t>(planIndices);
    const std::uintptr_t planBytes = selectedCount * sizeof(*planIndices);
    if (planBegin
        > (std::numeric_limits<std::uintptr_t>::max)() - planBytes)
    {
        return true;
    }
    const std::uintptr_t outputAddress =
        reinterpret_cast<std::uintptr_t>(outCompletedCount);
    return outputAddress >= planBegin
        && outputAddress < planBegin + planBytes;
}

[[nodiscard]] bool SelectionIsValid(
    const std::size_t *const planIndices,
    const std::size_t selectedCount,
    const std::size_t entryCount) noexcept
{
    if (selectedCount > entryCount
        || (selectedCount != 0 && !planIndices))
    {
        return false;
    }

    for (std::size_t index = 0; index < selectedCount; ++index)
    {
        if (planIndices[index] >= entryCount)
            return false;
        for (std::size_t prior = 0; prior < index; ++prior)
        {
            if (planIndices[prior] == planIndices[index])
                return false;
        }
    }
    return true;
}

[[nodiscard]] RestoreControlOperationStatus NormalizeStatus(
    const RestoreControlOperationStatus status) noexcept
{
    switch (status)
    {
    case RestoreControlOperationStatus::Success:
    case RestoreControlOperationStatus::RecoverableFailure:
    case RestoreControlOperationStatus::UnsafeFailure:
        return status;
    default:
        return RestoreControlOperationStatus::UnsafeFailure;
    }
}

[[nodiscard]] RestoreControlOperationStatus RunArchivePhysicsBatch(
    const ArchivePhysicsBatchCallbacks &callbacks,
    const ArchivePhysicsBatchOperation preflightOperation,
    const ArchivePhysicsBatchOperation commitOperation,
    const std::size_t *const planIndices,
    const std::size_t selectedCount,
    const std::size_t entryCount,
    std::size_t *const outCompletedCount) noexcept
{
    if (!outCompletedCount)
        return RestoreControlOperationStatus::UnsafeFailure;
    if (OutputOverlapsPlan(
            planIndices, selectedCount, outCompletedCount))
    {
        return RestoreControlOperationStatus::UnsafeFailure;
    }

    void *const callbackContext = callbacks.context;
    const ArchivePhysicsBatchPerformCallback perform = callbacks.perform;
    std::size_t completedCount = 0;
    *outCompletedCount = completedCount;

    if (!callbackContext || !perform
        || !SelectionIsValid(planIndices, selectedCount, entryCount))
    {
        *outCompletedCount = completedCount;
        return RestoreControlOperationStatus::UnsafeFailure;
    }

    for (std::size_t index = 0; index < selectedCount; ++index)
    {
        const RestoreControlOperationStatus callbackStatus =
            perform(
                callbackContext,
                preflightOperation,
                planIndices[index]);
        *outCompletedCount = completedCount;
        const RestoreControlOperationStatus status =
            NormalizeStatus(callbackStatus);
        if (status != RestoreControlOperationStatus::Success)
            return status;
    }

    for (std::size_t index = 0; index < selectedCount; ++index)
    {
        const RestoreControlOperationStatus callbackStatus =
            perform(
                callbackContext,
                commitOperation,
                planIndices[index]);
        *outCompletedCount = completedCount;
        const RestoreControlOperationStatus status =
            NormalizeStatus(callbackStatus);
        if (status != RestoreControlOperationStatus::Success)
            return status;

        // A representable, non-overlapping plan extent bounds selectedCount
        // below SIZE_MAX. Keep this guard so the successful-prefix counter
        // also fails closed if that precondition ever changes.
        if (completedCount >= selectedCount
            || completedCount
                == (std::numeric_limits<std::size_t>::max)())
        {
            *outCompletedCount = completedCount;
            return RestoreControlOperationStatus::UnsafeFailure;
        }
        ++completedCount;
        *outCompletedCount = completedCount;
    }
    *outCompletedCount = completedCount;
    return RestoreControlOperationStatus::Success;
}
} // namespace

RestoreControlOperationStatus RunArchivePhysicsRetirementBatch(
    const ArchivePhysicsBatchCallbacks &callbacks,
    const std::size_t *const planIndices,
    const std::size_t selectedCount,
    const std::size_t entryCount,
    std::size_t *const outCompletedCount) noexcept
{
    return RunArchivePhysicsBatch(
        callbacks,
        ArchivePhysicsBatchOperation::ValidateRetirement,
        ArchivePhysicsBatchOperation::Retire,
        planIndices,
        selectedCount,
        entryCount,
        outCompletedCount);
}

RestoreControlOperationStatus RunArchivePhysicsReconstructionBatch(
    const ArchivePhysicsBatchCallbacks &callbacks,
    const std::size_t *const planIndices,
    const std::size_t selectedCount,
    const std::size_t entryCount,
    std::size_t *const outCompletedCount) noexcept
{
    return RunArchivePhysicsBatch(
        callbacks,
        ArchivePhysicsBatchOperation::ValidateReconstruction,
        ArchivePhysicsBatchOperation::Reconstruct,
        planIndices,
        selectedCount,
        entryCount,
        outCompletedCount);
}
} // namespace fx::archive

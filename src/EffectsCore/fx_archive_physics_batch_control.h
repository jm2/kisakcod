#pragma once

#include "fx_archive_restore_control.h"

#include <cstddef>
#include <cstdint>

namespace fx::archive
{
// Engine-independent phases used to retire or reconstruct the selected FX
// physics entries. The controller owns selection validation and sequencing;
// the callback retains all engine resources and performs each entry operation.
enum class ArchivePhysicsBatchOperation : std::uint8_t
{
    ValidateRetirement,
    Retire,
    ValidateReconstruction,
    Reconstruct,
};

using ArchivePhysicsBatchPerformCallback =
    RestoreControlOperationStatus (*)(
        void *context,
        ArchivePhysicsBatchOperation operation,
        std::size_t entryIndex) noexcept;

struct ArchivePhysicsBatchCallbacks
{
    void *context = nullptr;
    ArchivePhysicsBatchPerformCallback perform = nullptr;
};

// Runs an all-preflight-then-commit retirement batch in plan order. Selection
// indices must be unique and less than entryCount. A null plan is valid only
// for an empty selection. The plan must remain stable for the complete
// synchronous call and cannot overlap outCompletedCount. The output is cleared
// before any callback and counts only commit callbacks that returned Success.
// A RecoverableFailure from a commit callback must leave that current entry
// unmodified; an UnsafeFailure denotes ownership that cannot be recovered by
// this controller. Invalid arguments or callback results fail closed as
// UnsafeFailure. An overlapping output is rejected without modifying the plan.
[[nodiscard]] RestoreControlOperationStatus RunArchivePhysicsRetirementBatch(
    const ArchivePhysicsBatchCallbacks &callbacks,
    const std::size_t *planIndices,
    std::size_t selectedCount,
    std::size_t entryCount,
    std::size_t *outCompletedCount) noexcept;

// Applies the same validated two-pass and stable non-overlapping plan contract
// to reconstruction. The controller allocates no storage, performs no
// reporting, and invokes no callback after the first non-success result.
[[nodiscard]] RestoreControlOperationStatus
RunArchivePhysicsReconstructionBatch(
    const ArchivePhysicsBatchCallbacks &callbacks,
    const std::size_t *planIndices,
    std::size_t selectedCount,
    std::size_t entryCount,
    std::size_t *outCompletedCount) noexcept;
} // namespace fx::archive

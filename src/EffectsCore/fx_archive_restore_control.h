#pragma once

#include <cstdint>

namespace fx::archive
{
// Operations are deliberately expressed without engine types so the restore
// branch tree can be exercised on every host and architecture. The callback
// retains ownership of all engine state and must keep the archive and physics
// exclusion intervals held until RunRestoreControl returns a terminal outcome.
enum class RestoreControlOperation : std::uint8_t
{
    CaptureOriginal,
    PlanRetirement,
    RetireOriginal,
    PreparePhysicsReplacement,
    CreateDesiredPhysics,
    ValidateDesiredPhysics,
    PublishPhysicsReplacement,
    PublishDesiredGraph,
    ValidateDesiredState,
    ValidateDiscardedOriginalPhysics,
    DrainNonLivePhysics,
    RollbackPhysicsReplacement,
    ValidateOriginalTokensInSnapshot,
    ValidateOriginalTokensInLiveGraph,
    ReconstructRetiredOriginalPhysics,
    PatchOriginalTokensInSnapshot,
    PatchOriginalTokensInLiveGraph,
    ValidateOriginalPhysics,
    PublishOriginalGraph,
    ValidateOriginalGraph,
    PublishSafeEmpty,
};

enum class RestoreControlOperationStatus : std::uint8_t
{
    Success,
    RecoverableFailure,
    UnsafeFailure,
};

enum class RestoreControlOutcome : std::uint8_t
{
    DesiredPublished,
    OriginalRestored,
    SafeEmptyPublished,
    UnsafeFailure,
};

struct RestoreControlCallbacks
{
    void *context = nullptr;
    RestoreControlOperationStatus (*perform)(
        void *context,
        RestoreControlOperation operation) noexcept = nullptr;
};

// Drives the transaction to exactly one terminal ownership state. A
// RecoverableFailure enters the recovery path appropriate to the last
// publication boundary; an UnsafeFailure (including an invalid callback
// result) stops immediately. The controller allocates no storage and invokes
// callbacks synchronously.
[[nodiscard]] RestoreControlOutcome RunRestoreControl(
    const RestoreControlCallbacks &callbacks) noexcept;
} // namespace fx::archive

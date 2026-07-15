#include <EffectsCore/fx_archive_restore_candidate_disk32.h>

#include <EffectsCore/fx_archive_semantics.h>
#include <EffectsCore/fx_snapshot_publication.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace fx::archive
{
namespace
{
[[nodiscard]] FxArchiveDisk32ReaderLeaseIdentity SnapshotLease(
    const EffectTableRestoreLease &lease) noexcept
{
    return {
        lease.identity,
        lease.ownerCookie,
        lease.lifecycleGeneration,
        static_cast<std::uint32_t>(lease.serial),
        static_cast<std::uint32_t>(lease.serial >> 32u)};
}

[[nodiscard]] bool LeaseMatches(
    const FxArchiveDisk32ReaderLeaseIdentity &stored,
    const EffectTableRestoreLease &lease) noexcept
{
    const FxArchiveDisk32ReaderLeaseIdentity candidate =
        SnapshotLease(lease);
    return stored.identity == candidate.identity
        && stored.ownerCookie == candidate.ownerCookie
        && stored.lifecycleGeneration == candidate.lifecycleGeneration
        && stored.serialLow == candidate.serialLow
        && stored.serialHigh == candidate.serialHigh;
}

[[nodiscard]] bool LinksAndSelectorsMatch(
    const FxSystem &system,
    const FxSystemBuffers &buffers,
    const FxSystemDisk32Metadata &metadata) noexcept
{
    if (metadata.readVisibilitySelector > 1
        || metadata.writeVisibilitySelector > 1
        || metadata.readVisibilitySelector
            == metadata.writeVisibilitySelector
        || system.effects != buffers.effects
        || system.elems != buffers.elems
        || system.trails != buffers.trails
        || system.trailElems != buffers.trailElems
        || system.deferredElems != buffers.deferredElems
        || system.visState != buffers.visState)
    {
        return false;
    }

    std::uint8_t readSelector = 0;
    std::uint8_t writeSelector = 0;
    return FX_TryDeriveVisibilitySelectors(
               &buffers.visState[0],
               &buffers.visState[1],
               system.visStateBufferRead,
               system.visStateBufferWrite,
               &readSelector,
               &writeSelector)
        && readSelector == metadata.readVisibilitySelector
        && writeSelector == metadata.writeVisibilitySelector;
}

void LinkCandidateBuffers(
    FxSystem *const system,
    FxSystemBuffers *const buffers,
    const FxSystemDisk32Metadata &metadata) noexcept
{
    system->effects = buffers->effects;
    system->elems = buffers->elems;
    system->trails = buffers->trails;
    system->trailElems = buffers->trailElems;
    system->deferredElems = buffers->deferredElems;
    system->visState = buffers->visState;
    system->visStateBufferRead =
        &buffers->visState[metadata.readVisibilitySelector];
    system->visStateBufferWrite =
        &buffers->visState[metadata.writeVisibilitySelector];
}

[[nodiscard]] bool BodyStateIsCanonical(
    const BodyState &source,
    const std::int32_t archiveTime) noexcept
{
    BodyStateDisk32 disk{};
    std::memcpy(disk.position, source.position, sizeof(disk.position));
    std::memcpy(disk.rotation, source.rotation, sizeof(disk.rotation));
    std::memcpy(disk.velocity, source.velocity, sizeof(disk.velocity));
    std::memcpy(
        disk.angVelocity,
        source.angVelocity,
        sizeof(disk.angVelocity));
    std::memcpy(
        disk.centerOfMassOffset,
        source.centerOfMassOffset,
        sizeof(disk.centerOfMassOffset));
    disk.mass = source.mass;
    disk.friction = source.friction;
    disk.bounce = source.bounce;
    disk.state = source.state;
    disk.timeLastAsleep = source.timeLastAsleep;
    disk.type = source.type;
    disk.underwater = static_cast<std::uint32_t>(source.underwater);

    BodyState canonical{};
    return TryUnpackBodyStateDisk32(disk, archiveTime, &canonical)
        && std::memcmp(&canonical, &source, sizeof(source)) == 0;
}

struct PhysicsValidationContext
{
    const FxArchiveDisk32ReaderPhysicsBody *sourceBodies = nullptr;
    const FxArchiveRestoreCandidateDisk32PhysicsBody *candidateBodies =
        nullptr;
    const FxSystemBuffers *buffers = nullptr;
    const FxSystemBuffersDisk32PoolStates *poolStates = nullptr;
    std::int32_t archiveTime = -1;
    std::size_t expectedCount = 0;
    std::size_t acceptedCount = 0;
};

[[nodiscard]] bool AcceptSourcePhysics(
    void *const opaqueContext,
    const FxArchiveSemanticPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const context =
        static_cast<PhysicsValidationContext *>(opaqueContext);
    if (!context || !context->sourceBodies || !context->buffers
        || !context->poolStates
        || physicsIndex != context->acceptedCount
        || physicsIndex >= context->expectedCount
        || descriptor.ownerIndex >= MAX_ELEMS
        || !FxPoolAllocationStateIsAllocated(
            context->poolStates->elems,
            descriptor.ownerIndex))
    {
        return false;
    }

    const FxArchiveDisk32ReaderPhysicsBody &body =
        context->sourceBodies[physicsIndex];
    const FxElem *const exactElem = std::addressof(
        context->buffers->elems[descriptor.ownerIndex].item);
    if (descriptor.elem != exactElem
        || body.descriptor.elem != exactElem
        || body.descriptor.model != descriptor.model
        || body.descriptor.ownerIndex != descriptor.ownerIndex
        || body.descriptor.token != descriptor.token
        || !descriptor.model
        || descriptor.token == FX_ARCHIVE_INVALID_PHYSICS_TOKEN
        || !BodyStateIsCanonical(body.state, context->archiveTime))
    {
        return false;
    }

    ++context->acceptedCount;
    return true;
}

[[nodiscard]] bool AcceptCandidatePhysics(
    void *const opaqueContext,
    const FxArchiveSemanticPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    auto *const context =
        static_cast<PhysicsValidationContext *>(opaqueContext);
    if (!context || !context->candidateBodies || !context->buffers
        || !context->poolStates
        || physicsIndex != context->acceptedCount
        || physicsIndex >= context->expectedCount
        || descriptor.ownerIndex >= MAX_ELEMS
        || !FxPoolAllocationStateIsAllocated(
            context->poolStates->elems,
            descriptor.ownerIndex))
    {
        return false;
    }

    const FxArchiveRestoreCandidateDisk32PhysicsBody &body =
        context->candidateBodies[physicsIndex];
    FxElem *const exactElem = std::addressof(
        const_cast<FxSystemBuffers *>(context->buffers)
            ->elems[descriptor.ownerIndex]
            .item);
    if (descriptor.elem != exactElem || body.elem != exactElem
        || body.model != descriptor.model
        || body.ownerIndex != descriptor.ownerIndex
        || body.token != descriptor.token
        || !descriptor.model
        || descriptor.token == FX_ARCHIVE_INVALID_PHYSICS_TOKEN
        || !BodyStateIsCanonical(body.state, context->archiveTime))
    {
        return false;
    }

    ++context->acceptedCount;
    return true;
}

[[nodiscard]] bool ShapeIsValid(
    const FxArchiveDisk32ReaderReadyView &source) noexcept
{
    const FxArchiveDisk32ReadyView &graph = source.graph;
    return graph.system && graph.buffers && graph.poolStates
        && graph.metadata && source.archivedSystemAddress.value != 0
        && source.physicsBodyCount <= FX_ARCHIVE_PHYSICS_BODY_LIMIT
        && graph.physicsBodyCount == source.physicsBodyCount
        && ((source.physicsBodies == nullptr)
            == (source.physicsBodyCount == 0))
        && LinksAndSelectorsMatch(
            *graph.system, *graph.buffers, *graph.metadata);
}

[[nodiscard]] bool EffectMetadataMatchesValidatedGraph(
    const FxSystemDisk32Metadata &metadata,
    const FxPoolAllocationGraphScratch &scratch) noexcept
{
    for (std::size_t index = 0; index < MAX_EFFECTS; ++index)
    {
        const std::uint8_t active = metadata.activeEffectSlots[index];
        if (active > 1
            || (active != 0) != scratch.allocatedEffectSlots[index])
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateSemantics(
    FxSystem *const system,
    PhysicsValidationContext *const context,
    const FxArchiveSemanticPhysicsSinkCallback sink,
    const std::int16_t expectedSpotLightBoltDobj) noexcept
{
    FxArchiveSemanticResult result{};
    const FxArchiveSemanticCallbacks callbacks{context, nullptr, sink};
    return TryValidateFxArchiveSemanticsNoReport(
               system, callbacks, &result)
        && context->acceptedCount == context->expectedCount
        && result.physicsBodyCount == context->expectedCount
        && result.spotLightBoltDobj == expectedSpotLightBoltDobj;
}
} // namespace

FxArchiveRestoreCandidateDisk32Status
TryBuildFxArchiveRestoreCandidateDisk32(
    FxArchiveDisk32ReaderWorkspace *const sourceWorkspace,
    const EffectTableRestoreLease &lease,
    FxArchiveRestoreCandidateDisk32Workspace *const candidateWorkspace)
    noexcept
{
    if (!candidateWorkspace)
        return FxArchiveRestoreCandidateDisk32Status::InvalidArgument;
    if (candidateWorkspace->operating_)
        return FxArchiveRestoreCandidateDisk32Status::Busy;

    candidateWorkspace->phase_ =
        FxArchiveRestoreCandidateDisk32Phase::Empty;
    candidateWorkspace->physicsBodyCount_ = 0;
    candidateWorkspace->archivedSystemAddress_ = ArchiveAddress32{};
    candidateWorkspace->lease_ = FxArchiveDisk32ReaderLeaseIdentity{};
    if (!sourceWorkspace)
        return FxArchiveRestoreCandidateDisk32Status::InvalidArgument;
    if (sourceWorkspace->operating_)
        return FxArchiveRestoreCandidateDisk32Status::Busy;

    candidateWorkspace->operating_ = true;
    sourceWorkspace->operating_ = true;
    const auto finish = [sourceWorkspace, candidateWorkspace](
                            const FxArchiveRestoreCandidateDisk32Status status)
        noexcept {
            sourceWorkspace->operating_ = false;
            candidateWorkspace->operating_ = false;
            return status;
        };

    if (sourceWorkspace->phase_
        != FxArchiveDisk32ReaderPhase::Ready)
    {
        return finish(
            FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    }
    if (!LeaseMatches(sourceWorkspace->lease_, lease)
        || ValidateEffectTableRestoreLease(lease)
        != EffectTableRestoreStatus::Success)
    {
        return finish(
            FxArchiveRestoreCandidateDisk32Status::InvalidLease);
    }

    FxArchiveDisk32ReadyView sourceGraph{};
    if (!TryGetFxArchiveDisk32ReadyView(
            &sourceWorkspace->nativeWorkspace_, &sourceGraph))
    {
        return finish(FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    }
    const FxArchiveDisk32ReaderReadyView source{
        sourceGraph,
        sourceWorkspace->archivedSystemAddress_,
        sourceWorkspace->physicsBodyCount_ != 0
            ? &sourceWorkspace->physicsBodies_[0]
            : nullptr,
        sourceWorkspace->physicsBodyCount_};
    if (!ShapeIsValid(source))
        return finish(FxArchiveRestoreCandidateDisk32Status::InvalidGraph);

    const FxArchiveDisk32ReadyView &graph = source.graph;
    if (!FxValidatePoolAllocationGraphWithScratch(
            graph.system,
            graph.poolStates->elems,
            graph.poolStates->trails,
            graph.poolStates->trailElems,
            &candidateWorkspace->graphScratch_)
        || !EffectMetadataMatchesValidatedGraph(
            *graph.metadata, candidateWorkspace->graphScratch_))
    {
        return finish(FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    }

    PhysicsValidationContext sourceContext{
        source.physicsBodies,
        nullptr,
        graph.buffers,
        graph.poolStates,
        graph.system->msecNow,
        source.physicsBodyCount,
        0};
    if (!ValidateSemantics(
            const_cast<FxSystem *>(graph.system),
            &sourceContext,
            AcceptSourcePhysics,
            graph.system->activeSpotLightBoltDobj))
    {
        if (ValidateEffectTableRestoreLease(lease)
            != EffectTableRestoreStatus::Success)
        {
            return finish(
                FxArchiveRestoreCandidateDisk32Status::InvalidLease);
        }
        return finish(
            FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    }
    if (ValidateEffectTableRestoreLease(lease)
        != EffectTableRestoreStatus::Success)
    {
        return finish(
            FxArchiveRestoreCandidateDisk32Status::InvalidLease);
    }

    std::memcpy(
        &candidateWorkspace->system_,
        graph.system,
        sizeof(candidateWorkspace->system_));
    std::memcpy(
        &candidateWorkspace->buffers_,
        graph.buffers,
        sizeof(candidateWorkspace->buffers_));
    candidateWorkspace->poolStates_ = *graph.poolStates;
    candidateWorkspace->metadata_ = *graph.metadata;
    candidateWorkspace->archivedSystemAddress_ =
        source.archivedSystemAddress;
    LinkCandidateBuffers(
        &candidateWorkspace->system_,
        &candidateWorkspace->buffers_,
        candidateWorkspace->metadata_);

    for (std::size_t index = 0;
         index < source.physicsBodyCount;
         ++index)
    {
        const FxArchiveDisk32ReaderPhysicsBody &sourceBody =
            source.physicsBodies[index];
        FxArchiveRestoreCandidateDisk32PhysicsBody &candidateBody =
            candidateWorkspace->physicsBodies_[index];
        candidateBody.elem = std::addressof(
            candidateWorkspace->buffers_.elems[
                sourceBody.descriptor.ownerIndex]
                .item);
        candidateBody.model = sourceBody.descriptor.model;
        candidateBody.state = sourceBody.state;
        candidateBody.ownerIndex = sourceBody.descriptor.ownerIndex;
        candidateBody.token = sourceBody.descriptor.token;
    }

    if (!LinksAndSelectorsMatch(
            candidateWorkspace->system_,
            candidateWorkspace->buffers_,
            candidateWorkspace->metadata_)
        || !FxValidatePoolAllocationGraphWithScratch(
            &candidateWorkspace->system_,
            candidateWorkspace->poolStates_.elems,
            candidateWorkspace->poolStates_.trails,
            candidateWorkspace->poolStates_.trailElems,
            &candidateWorkspace->graphScratch_)
        || !EffectMetadataMatchesValidatedGraph(
            candidateWorkspace->metadata_,
            candidateWorkspace->graphScratch_))
    {
        return finish(FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    }

    PhysicsValidationContext candidateContext{
        nullptr,
        candidateWorkspace->physicsBodies_,
        &candidateWorkspace->buffers_,
        &candidateWorkspace->poolStates_,
        candidateWorkspace->system_.msecNow,
        source.physicsBodyCount,
        0};
    if (!ValidateSemantics(
            &candidateWorkspace->system_,
            &candidateContext,
            AcceptCandidatePhysics,
            candidateWorkspace->system_.activeSpotLightBoltDobj))
    {
        if (ValidateEffectTableRestoreLease(lease)
            != EffectTableRestoreStatus::Success)
        {
            return finish(
                FxArchiveRestoreCandidateDisk32Status::InvalidLease);
        }
        return finish(
            FxArchiveRestoreCandidateDisk32Status::InvalidPhysics);
    }
    if (ValidateEffectTableRestoreLease(lease)
        != EffectTableRestoreStatus::Success)
    {
        return finish(
            FxArchiveRestoreCandidateDisk32Status::InvalidLease);
    }

    FxArchiveDisk32ReadyView finalSourceGraph{};
    if (sourceWorkspace->phase_
            != FxArchiveDisk32ReaderPhase::Ready
        || !LeaseMatches(sourceWorkspace->lease_, lease)
        || sourceWorkspace->archivedSystemAddress_.value
            != source.archivedSystemAddress.value
        || sourceWorkspace->physicsBodyCount_
            != source.physicsBodyCount
        || !TryGetFxArchiveDisk32ReadyView(
            &sourceWorkspace->nativeWorkspace_, &finalSourceGraph)
        || finalSourceGraph.system != source.graph.system
        || finalSourceGraph.buffers != source.graph.buffers
        || finalSourceGraph.poolStates != source.graph.poolStates
        || finalSourceGraph.metadata != source.graph.metadata
        || finalSourceGraph.physicsBodyCount
            != source.graph.physicsBodyCount)
    {
        return finish(FxArchiveRestoreCandidateDisk32Status::InvalidGraph);
    }

    candidateWorkspace->lease_ = SnapshotLease(lease);
    candidateWorkspace->physicsBodyCount_ = source.physicsBodyCount;
    sourceWorkspace->operating_ = false;
    candidateWorkspace->operating_ = false;
    candidateWorkspace->phase_ =
        FxArchiveRestoreCandidateDisk32Phase::Ready;
    return FxArchiveRestoreCandidateDisk32Status::Success;
}

bool TryGetFxArchiveRestoreCandidateDisk32ReadyView(
    FxArchiveRestoreCandidateDisk32Workspace *const workspace,
    const EffectTableRestoreLease &lease,
    FxArchiveRestoreCandidateDisk32ReadyView *const outView) noexcept
{
    if (!workspace || !outView || workspace->operating_
        || workspace->phase_
            != FxArchiveRestoreCandidateDisk32Phase::Ready
        || !LeaseMatches(workspace->lease_, lease)
        || workspace->physicsBodyCount_
            > FX_ARCHIVE_PHYSICS_BODY_LIMIT
        || workspace->archivedSystemAddress_.value == 0)
    {
        return false;
    }

    workspace->operating_ = true;
    const auto finish = [workspace](const bool result) noexcept {
        workspace->operating_ = false;
        return result;
    };
    if (ValidateEffectTableRestoreLease(lease)
            != EffectTableRestoreStatus::Success
        || workspace->phase_
            != FxArchiveRestoreCandidateDisk32Phase::Ready
        || !LeaseMatches(workspace->lease_, lease))
    {
        return finish(false);
    }

    const FxArchiveRestoreCandidateDisk32ReadyView view{
        &workspace->system_,
        &workspace->buffers_,
        workspace->archivedSystemAddress_,
        workspace->physicsBodyCount_ != 0
            ? &workspace->physicsBodies_[0]
            : nullptr,
        workspace->physicsBodyCount_};
    workspace->operating_ = false;
    *outView = view;
    return true;
}
} // namespace fx::archive

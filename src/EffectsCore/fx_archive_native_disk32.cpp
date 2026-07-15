#include <EffectsCore/fx_archive_native_disk32.h>

#include <EffectsCore/fx_snapshot_publication.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace fx::archive
{
namespace
{
template <typename ITEM_TYPE, typename DISK_SLOT_TYPE>
inline constexpr bool PoolSlotRepresentationIsCompatible =
    sizeof(FxPool<ITEM_TYPE>) == sizeof(DISK_SLOT_TYPE)
    && alignof(FxPool<ITEM_TYPE>) == alignof(DISK_SLOT_TYPE)
    && std::is_trivially_copyable_v<FxPool<ITEM_TYPE>>
    && std::is_trivially_destructible_v<FxPool<ITEM_TYPE>>
    && std::is_trivially_copyable_v<DISK_SLOT_TYPE>;

static_assert(PoolSlotRepresentationIsCompatible<
              FxElem,
              FxElemPoolSlotDisk32>);
static_assert(PoolSlotRepresentationIsCompatible<
              FxTrail,
              FxTrailPoolSlotDisk32>);
static_assert(PoolSlotRepresentationIsCompatible<
              FxTrailElem,
              FxTrailElemPoolSlotDisk32>);

template <std::size_t BYTE_COUNT>
void CopyRepresentationBytes(
    const std::uint8_t (&source)[BYTE_COUNT],
    void *const destination) noexcept
{
    auto *const destinationBytes =
        static_cast<std::uint8_t *>(destination);
    for (std::size_t index = 0; index < BYTE_COUNT; ++index)
        destinationBytes[index] = source[index];
}

void UnpackElemRecord(
    const FxElemDisk32 &source,
    FxElem *const destination) noexcept
{
    destination->defIndex = source.defIndex;
    destination->sequence = source.sequence;
    destination->atRestFraction = source.atRestFraction;
    destination->emitResidual = source.emitResidual;
    destination->nextElemHandleInEffect =
        source.nextElemHandleInEffect;
    destination->prevElemHandleInEffect =
        source.prevElemHandleInEffect;
    destination->msecBegin = source.msecBegin;
    for (std::size_t component = 0; component < 3; ++component)
        destination->baseVel[component] = source.baseVel[component];

    // Definition semantics decide between physObjId/origin and between
    // trailTexCoord/lightingHandle.  The structural phase deliberately starts
    // the conservative origin and lightingHandle members, then preserves each
    // complete representation through byte access. A later Ready transition
    // must explicitly reactivate the definition-selected members.
    destination->origin[0] = 0.0f;
    CopyRepresentationBytes(
        source.payload, static_cast<void *>(destination->origin));
    destination->u.lightingHandle = 0;
    CopyRepresentationBytes(
        source.value,
        static_cast<void *>(std::addressof(destination->u)));
}

void UnpackTrailRecord(
    const FxTrailDisk32 &source,
    FxTrail *const destination) noexcept
{
    destination->nextTrailHandle = source.nextTrailHandle;
    destination->firstElemHandle = source.firstElemHandle;
    destination->lastElemHandle = source.lastElemHandle;
    destination->defIndex = source.defIndex;
    destination->sequence = source.sequence;
}

void UnpackTrailElemRecord(
    const FxTrailElemDisk32 &source,
    FxTrailElem *const destination) noexcept
{
    for (std::size_t component = 0; component < 3; ++component)
        destination->origin[component] = source.origin[component];
    destination->spawnDist = source.spawnDist;
    destination->msecBegin = source.msecBegin;
    destination->nextTrailElemHandle = source.nextTrailElemHandle;
    destination->baseVelZ = source.baseVelZ;
    for (std::size_t row = 0; row < 2; ++row)
    {
        for (std::size_t component = 0; component < 3; ++component)
        {
            destination->basis[row][component] =
                source.basis[row][component];
        }
    }
    destination->sequence = source.sequence;
    destination->unused = source.unused;
}

bool TryBuildElemPool(
    const FxElemPoolSlotDisk32 (&source)[MAX_ELEMS],
    const FxPoolAllocationState<MAX_ELEMS> &allocationState,
    FxPool<FxElem> (&destination)[MAX_ELEMS]) noexcept
{
    for (std::size_t index = 0; index < MAX_ELEMS; ++index)
    {
        if (FxPoolAllocationStateIsAllocated(allocationState, index))
        {
            std::construct_at(std::addressof(destination[index].item));
            const FxElemDisk32 record =
                std::bit_cast<FxElemDisk32>(source[index]);
            UnpackElemRecord(record, &destination[index].item);
            continue;
        }

        std::int32_t nextFree = -1;
        if (!TryDecodeFxPoolSlotFreeLinkDisk32(
                source[index], &nextFree))
        {
            return false;
        }
        std::construct_at(
            std::addressof(destination[index].nextFree), nextFree);
        // Only nextFree is interpreted, but legacy x86 restore retained the
        // dead bytes that follow it. Preserve the complete representation and
        // then reassert the independently validated active member value.
        CopyRepresentationBytes(
            source[index].bytes,
            static_cast<void *>(std::addressof(destination[index])));
        destination[index].nextFree = nextFree;
    }
    return true;
}

bool TryBuildTrailPool(
    const FxTrailPoolSlotDisk32 (&source)[MAX_TRAILS],
    const FxPoolAllocationState<MAX_TRAILS> &allocationState,
    FxPool<FxTrail> (&destination)[MAX_TRAILS]) noexcept
{
    for (std::size_t index = 0; index < MAX_TRAILS; ++index)
    {
        if (FxPoolAllocationStateIsAllocated(allocationState, index))
        {
            std::construct_at(std::addressof(destination[index].item));
            const FxTrailDisk32 record =
                std::bit_cast<FxTrailDisk32>(source[index]);
            UnpackTrailRecord(record, &destination[index].item);
            continue;
        }

        std::int32_t nextFree = -1;
        if (!TryDecodeFxPoolSlotFreeLinkDisk32(
                source[index], &nextFree))
        {
            return false;
        }
        std::construct_at(
            std::addressof(destination[index].nextFree), nextFree);
        CopyRepresentationBytes(
            source[index].bytes,
            static_cast<void *>(std::addressof(destination[index])));
        destination[index].nextFree = nextFree;
    }
    return true;
}

bool TryBuildTrailElemPool(
    const FxTrailElemPoolSlotDisk32 (&source)[MAX_TRAIL_ELEMS],
    const FxPoolAllocationState<MAX_TRAIL_ELEMS> &allocationState,
    FxPool<FxTrailElem> (&destination)[MAX_TRAIL_ELEMS]) noexcept
{
    for (std::size_t index = 0; index < MAX_TRAIL_ELEMS; ++index)
    {
        if (FxPoolAllocationStateIsAllocated(allocationState, index))
        {
            std::construct_at(std::addressof(destination[index].item));
            const FxTrailElemDisk32 record =
                std::bit_cast<FxTrailElemDisk32>(source[index]);
            UnpackTrailElemRecord(record, &destination[index].item);
            continue;
        }

        std::int32_t nextFree = -1;
        if (!TryDecodeFxPoolSlotFreeLinkDisk32(
                source[index], &nextFree))
        {
            return false;
        }
        std::construct_at(
            std::addressof(destination[index].nextFree), nextFree);
        CopyRepresentationBytes(
            source[index].bytes,
            static_cast<void *>(std::addressof(destination[index])));
        destination[index].nextFree = nextFree;
    }
    return true;
}

void CopyVisibilityAndDeferredBuffers(
    const FxSystemBuffersDisk32 &source,
    FxSystemBuffers *const destination) noexcept
{
    for (std::size_t stateIndex = 0; stateIndex < 2; ++stateIndex)
    {
        const FxVisStateDisk32 &sourceState =
            source.visState[stateIndex];
        FxVisState &destinationState =
            destination->visState[stateIndex];
        for (std::size_t blockerIndex = 0;
             blockerIndex < 256;
             ++blockerIndex)
        {
            const FxVisBlockerDisk32 &sourceBlocker =
                sourceState.blocker[blockerIndex];
            FxVisBlocker &destinationBlocker =
                destinationState.blocker[blockerIndex];
            for (std::size_t component = 0; component < 3; ++component)
            {
                destinationBlocker.origin[component] =
                    sourceBlocker.origin[component];
            }
            destinationBlocker.radius = sourceBlocker.radius;
            destinationBlocker.visibility = sourceBlocker.visibility;
        }
        destinationState.blockerCount = sourceState.blockerCount;
        for (std::size_t index = 0; index < 3; ++index)
            destinationState.pad[index] = sourceState.pad[index];
    }

    for (std::size_t index = 0; index < MAX_ELEMS; ++index)
        destination->deferredElems[index] = source.deferredElems[index];
    for (std::size_t index = 0;
         index < sizeof(destination->padBuffer);
         ++index)
    {
        destination->padBuffer[index] = source.padBuffer[index];
    }
}

void LinkWorkspaceBuffers(
    FxSystemBuffers *const buffers,
    const FxSystemDisk32Metadata &metadata,
    FxSystem *const system) noexcept
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

bool WorkspaceLinksAndSelectorsRoundTrip(
    const FxSystem &system,
    const FxSystemBuffers &buffers,
    const FxSystemDisk32Metadata &metadata) noexcept
{
    if (system.effects != buffers.effects
        || system.elems != buffers.elems
        || system.trails != buffers.trails
        || system.trailElems != buffers.trailElems
        || system.deferredElems != buffers.deferredElems
        || system.visState != buffers.visState
        || metadata.readVisibilitySelector > 1
        || metadata.writeVisibilitySelector > 1
        || metadata.readVisibilitySelector
            == metadata.writeVisibilitySelector)
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
} // namespace

FxArchiveDisk32StructuralStatus
TryBuildFxArchiveDisk32StructuralImage(
    const FxSystemDisk32 &sourceSystem,
    const FxSystemBuffersDisk32 &sourceBuffers,
    const FxArchiveDisk32Resolver &resolver,
    FxArchiveDisk32NativeWorkspace *const workspace) noexcept
{
    if (!workspace)
        return FxArchiveDisk32StructuralStatus::InvalidArgument;

    // A retry invalidates every earlier view before any staged byte changes.
    workspace->phase_ = FxArchiveDisk32WorkspacePhase::Empty;
    if (workspace->building_ || !resolver.resolve)
        return FxArchiveDisk32StructuralStatus::InvalidArgument;
    workspace->building_ = true;
    const auto finish = [workspace](
                            const FxArchiveDisk32StructuralStatus status)
        noexcept {
            workspace->building_ = false;
            return status;
        };

    if (!TryUnpackFxSystemDisk32(
            sourceSystem,
            &workspace->system_,
            &workspace->metadata_))
    {
        return finish(FxArchiveDisk32StructuralStatus::InvalidSystem);
    }

    if (!TryRebuildFxSystemBuffersDisk32PoolStates(
            sourceBuffers,
            sourceSystem,
            &workspace->poolStates_))
    {
        return finish(
            FxArchiveDisk32StructuralStatus::InvalidFreeLists);
    }

    for (std::size_t index = 0; index < MAX_EFFECTS; ++index)
    {
        const bool isActive =
            workspace->metadata_.activeEffectSlots[index] != 0;
        const FxEffectDef *resolvedDefinition = nullptr;
        if (isActive)
        {
            const EffectDefinitionKey32 key =
                sourceBuffers.effects[index].definitionKey;
            if (!EffectDefinitionKeyIsValid(key))
            {
                return finish(
                    FxArchiveDisk32StructuralStatus::InvalidEffect);
            }
            resolvedDefinition = resolver.resolve(resolver.context, key);
            if (!resolvedDefinition)
            {
                return finish(
                    FxArchiveDisk32StructuralStatus::DefinitionNotFound);
            }
        }

        if (!TryUnpackFxEffectDisk32(
                sourceBuffers.effects[index],
                isActive,
                resolvedDefinition,
                &workspace->buffers_.effects[index]))
        {
            return finish(
                FxArchiveDisk32StructuralStatus::InvalidEffect);
        }
    }

    if (!TryBuildElemPool(
            sourceBuffers.elems,
            workspace->poolStates_.elems,
            workspace->buffers_.elems)
        || !TryBuildTrailPool(
            sourceBuffers.trails,
            workspace->poolStates_.trails,
            workspace->buffers_.trails)
        || !TryBuildTrailElemPool(
            sourceBuffers.trailElems,
            workspace->poolStates_.trailElems,
            workspace->buffers_.trailElems))
    {
        return finish(
            FxArchiveDisk32StructuralStatus::InvalidPoolRecord);
    }

    CopyVisibilityAndDeferredBuffers(
        sourceBuffers, &workspace->buffers_);
    LinkWorkspaceBuffers(
        &workspace->buffers_,
        workspace->metadata_,
        &workspace->system_);

    if (!WorkspaceLinksAndSelectorsRoundTrip(
            workspace->system_,
            workspace->buffers_,
            workspace->metadata_)
        || !FxValidatePoolAllocationGraphWithScratch(
            &workspace->system_,
            workspace->poolStates_.elems,
            workspace->poolStates_.trails,
            workspace->poolStates_.trailElems,
            &workspace->graphScratch_))
    {
        return finish(FxArchiveDisk32StructuralStatus::InvalidGraph);
    }

    workspace->building_ = false;
    workspace->phase_ =
        FxArchiveDisk32WorkspacePhase::StructurallyValid;
    return FxArchiveDisk32StructuralStatus::Success;
}

bool TryGetFxArchiveDisk32StructuralView(
    const FxArchiveDisk32NativeWorkspace *const workspace,
    FxArchiveDisk32StructuralView *const outView) noexcept
{
    if (!workspace || !outView
        || (workspace->phase_
                != FxArchiveDisk32WorkspacePhase::StructurallyValid
            && workspace->phase_
                != FxArchiveDisk32WorkspacePhase::Ready))
    {
        return false;
    }

    const FxArchiveDisk32StructuralView view{
        &workspace->system_,
        &workspace->buffers_,
        &workspace->poolStates_,
        &workspace->metadata_};
    *outView = view;
    return true;
}
} // namespace fx::archive

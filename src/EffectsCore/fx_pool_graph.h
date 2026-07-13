#pragma once

#include "fx_pool.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace kisak_fx_pool_graph_detail
{
template <std::size_t LIMIT>
bool AllocationStateIsConsistent(
    const FxPoolAllocationState<LIMIT> &state) noexcept
{
    if (!state.initialized || state.allocatedCount > LIMIT)
        return false;

    std::size_t allocatedCount = 0;
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        if (FxPoolAllocationStateIsAllocated(state, index))
            ++allocatedCount;
    }
    if (allocatedCount != state.allocatedCount)
        return false;

    constexpr std::size_t bitCapacity =
        FxPoolAllocationState<LIMIT>::WORD_COUNT
        * FxPoolAllocationState<LIMIT>::WORD_BITS;
    for (std::size_t index = LIMIT; index < bitCapacity; ++index)
    {
        const std::uint64_t bit =
            std::uint64_t{1}
            << (index % FxPoolAllocationState<LIMIT>::WORD_BITS);
        if ((state.allocatedWords[
                 index / FxPoolAllocationState<LIMIT>::WORD_BITS]
             & bit)
            != 0)
        {
            return false;
        }
    }
    return true;
}

template <typename ITEM_TYPE, std::size_t LIMIT>
const FxPool<ITEM_TYPE> *DecodeAllocatedPoolHandle(
    const FxPool<ITEM_TYPE> *pool,
    const FxPoolAllocationState<LIMIT> &allocationState,
    const std::uint16_t handle,
    std::size_t *const outIndex) noexcept
{
    if (!outIndex)
        return nullptr;

    const FxPool<ITEM_TYPE> *const item =
        FxDecodeHandle<
            FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
            pool, handle);
    if (!item)
        return nullptr;

    const std::size_t index = static_cast<std::size_t>(item - pool);
    if (!FxPoolAllocationStateIsAllocated(allocationState, index))
        return nullptr;

    *outIndex = index;
    return item;
}

template <std::size_t LIMIT>
bool EveryAllocatedSlotWasVisited(
    const FxPoolAllocationState<LIMIT> &allocationState,
    const std::array<bool, LIMIT> &visited,
    const std::size_t visitedCount) noexcept
{
    if (visitedCount != allocationState.allocatedCount)
        return false;

    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        if (visited[index]
            != FxPoolAllocationStateIsAllocated(allocationState, index))
        {
            return false;
        }
    }
    return true;
}
} // namespace kisak_fx_pool_graph_detail

// Validates the complete ownership graph reconstructed from an archived FX
// system. Callers must provide a stable system/pool snapshot (normally while
// holding the FX allocator lock and archive/iterator exclusion). The function
// is bounded, allocation-free, non-mutating, and never reports errors itself.
inline bool FxValidatePoolAllocationGraph(
    const FxSystem *const system,
    const FxPoolAllocationState<MAX_ELEMS> &elemAllocationState,
    const FxPoolAllocationState<MAX_TRAILS> &trailAllocationState,
    const FxPoolAllocationState<MAX_TRAIL_ELEMS>
        &trailElemAllocationState) noexcept
{
    using namespace kisak_fx_pool_graph_detail;

    if (!system || !system->effects || !system->elems || !system->trails
        || !system->trailElems
        || !AllocationStateIsConsistent(elemAllocationState)
        || !AllocationStateIsConsistent(trailAllocationState)
        || !AllocationStateIsConsistent(trailElemAllocationState))
    {
        return false;
    }

    if (Sys_AtomicLoad(&system->activeElemCount)
            != static_cast<std::int32_t>(
                elemAllocationState.allocatedCount)
        || Sys_AtomicLoad(&system->activeTrailCount)
            != static_cast<std::int32_t>(
                trailAllocationState.allocatedCount)
        || Sys_AtomicLoad(&system->activeTrailElemCount)
            != static_cast<std::int32_t>(
                trailElemAllocationState.allocatedCount))
    {
        return false;
    }

    const std::int32_t firstActiveEffect =
        Sys_AtomicLoad(&system->firstActiveEffect);
    const std::int32_t firstNewEffect =
        Sys_AtomicLoad(&system->firstNewEffect);
    const std::int32_t firstFreeEffect =
        Sys_AtomicLoad(&system->firstFreeEffect);
    if (firstActiveEffect < 0
        || firstActiveEffect > firstNewEffect
        || firstNewEffect != firstFreeEffect
        || static_cast<std::int64_t>(firstFreeEffect)
                - firstActiveEffect
            > static_cast<std::int64_t>(MAX_EFFECTS))
    {
        return false;
    }

    // allEffectHandles is both the active-ring storage and the free-slot
    // inventory. A malformed handle outside the active interval is still
    // security-relevant because it will eventually be consumed by spawn.
    std::array<bool, MAX_EFFECTS> effectSlotsInPermutation{};
    for (std::size_t handleIndex = 0;
         handleIndex < MAX_EFFECTS;
         ++handleIndex)
    {
        const FxEffect *const effect =
            FxDecodeHandle<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                static_cast<const FxEffect *>(system->effects),
                system->allEffectHandles[handleIndex]);
        if (!effect)
            return false;

        const std::size_t effectIndex =
            static_cast<std::size_t>(effect - system->effects);
        if (effectSlotsInPermutation[effectIndex])
            return false;
        effectSlotsInPermutation[effectIndex] = true;
    }

    constexpr std::uint32_t refCountMask = 0x0000FFFFu;
    constexpr std::uint32_t pendingLoopElemStatusBit = 0x00010000u;
    constexpr std::uint32_t ownedEffectCountMask = 0x07FE0000u;
    constexpr std::uint32_t ownedEffectCountShift = 17;
    constexpr std::uint32_t deferUpdateStatusBit = 0x08000000u;
    constexpr std::uint32_t selfOwnedStatusBit = 0x10000000u;
    constexpr std::uint32_t persistedStatusMask =
        refCountMask | pendingLoopElemStatusBit | ownedEffectCountMask
        | deferUpdateStatusBit | selfOwnedStatusBit;
    static_assert(
        (ownedEffectCountMask >> ownedEffectCountShift)
            == MAX_EFFECTS - 1,
        "FX owned-effect status field must represent every possible child");

    std::array<bool, MAX_EFFECTS> allocatedEffectSlots{};
    std::array<std::size_t, MAX_EFFECTS> linkedEffectReferenceCounts{};
    std::array<bool, MAX_ELEMS> visitedElems{};
    std::array<bool, MAX_TRAILS> visitedTrails{};
    std::array<bool, MAX_TRAIL_ELEMS> visitedTrailElems{};
    std::size_t visitedElemCount = 0;
    std::size_t visitedTrailCount = 0;
    std::size_t visitedTrailElemCount = 0;

    for (std::int64_t activeIndex = firstActiveEffect;
         activeIndex < firstFreeEffect;
         ++activeIndex)
    {
        const std::uint16_t effectHandle =
            system->allEffectHandles[
                static_cast<std::size_t>(activeIndex)
                & (MAX_EFFECTS - 1)];
        const FxEffect *const effect =
            FxDecodeHandle<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                static_cast<const FxEffect *>(system->effects),
                effectHandle);
        if (!effect)
            return false;

        const std::size_t effectIndex =
            static_cast<std::size_t>(effect - system->effects);
        if (allocatedEffectSlots[effectIndex])
            return false;
        allocatedEffectSlots[effectIndex] = true;

        const std::uint32_t status = static_cast<std::uint32_t>(
            Sys_AtomicLoad(&effect->status));
        // Archive exclusion must drain every effect locker. Persisting a lock
        // or sign/reserved bit could make the signed lock-acquisition loop
        // treat a malformed status as ownership held by the current thread.
        if ((status & ~(persistedStatusMask)) != 0)
            return false;

        // Ordinary elements retain an effect reference. A zero-reference
        // effect may await trail garbage collection, but any ordinary element
        // chain would be orphaned when garbage collection clears the effect.
        if ((status & refCountMask) == 0
            && (effect->firstElemHandle[0] != FX_INVALID_HANDLE
                || effect->firstElemHandle[1] != FX_INVALID_HANDLE
                || effect->firstElemHandle[2] != FX_INVALID_HANDLE))
        {
            return false;
        }

        bool foundFirstSortedElem =
            effect->firstSortedElemHandle == FX_INVALID_HANDLE;
        for (std::size_t elemClass = 0; elemClass < 3; ++elemClass)
        {
            std::uint16_t expectedPreviousHandle = FX_INVALID_HANDLE;
            std::uint16_t elemHandle =
                effect->firstElemHandle[elemClass];
            std::size_t chainLength = 0;
            while (elemHandle != FX_INVALID_HANDLE)
            {
                if (chainLength == MAX_ELEMS)
                    return false;
                ++chainLength;

                std::size_t elemIndex = 0;
                const FxPool<FxElem> *const elem =
                    DecodeAllocatedPoolHandle<FxElem, MAX_ELEMS>(
                        system->elems,
                        elemAllocationState,
                        elemHandle,
                        &elemIndex);
                if (!elem || visitedElems[elemIndex]
                    || elem->item.prevElemHandleInEffect
                        != expectedPreviousHandle)
                {
                    return false;
                }
                visitedElems[elemIndex] = true;
                ++visitedElemCount;
                ++linkedEffectReferenceCounts[effectIndex];

                if (elemClass == 0
                    && elemHandle == effect->firstSortedElemHandle)
                {
                    foundFirstSortedElem = true;
                }

                expectedPreviousHandle = elemHandle;
                elemHandle = elem->item.nextElemHandleInEffect;
            }
        }
        if (!foundFirstSortedElem)
            return false;

        std::uint16_t trailHandle = effect->firstTrailHandle;
        std::size_t effectTrailCount = 0;
        while (trailHandle != FX_INVALID_HANDLE)
        {
            if (effectTrailCount == MAX_TRAILS)
                return false;
            ++effectTrailCount;

            std::size_t trailIndex = 0;
            const FxPool<FxTrail> *const trail =
                DecodeAllocatedPoolHandle<FxTrail, MAX_TRAILS>(
                    system->trails,
                    trailAllocationState,
                    trailHandle,
                    &trailIndex);
            if (!trail || visitedTrails[trailIndex])
                return false;
            visitedTrails[trailIndex] = true;
            ++visitedTrailCount;

            const bool hasFirstTrailElem =
                trail->item.firstElemHandle != FX_INVALID_HANDLE;
            const bool hasLastTrailElem =
                trail->item.lastElemHandle != FX_INVALID_HANDLE;
            if (hasFirstTrailElem != hasLastTrailElem)
                return false;

            std::uint16_t trailElemHandle =
                trail->item.firstElemHandle;
            std::uint16_t terminalTrailElemHandle = FX_INVALID_HANDLE;
            std::size_t trailElemChainLength = 0;
            while (trailElemHandle != FX_INVALID_HANDLE)
            {
                if (trailElemChainLength == MAX_TRAIL_ELEMS)
                    return false;
                ++trailElemChainLength;

                std::size_t trailElemIndex = 0;
                const FxPool<FxTrailElem> *const trailElem =
                    DecodeAllocatedPoolHandle<
                        FxTrailElem, MAX_TRAIL_ELEMS>(
                        system->trailElems,
                        trailElemAllocationState,
                        trailElemHandle,
                        &trailElemIndex);
                if (!trailElem || visitedTrailElems[trailElemIndex])
                    return false;
                visitedTrailElems[trailElemIndex] = true;
                ++visitedTrailElemCount;
                ++linkedEffectReferenceCounts[effectIndex];

                terminalTrailElemHandle = trailElemHandle;
                trailElemHandle =
                    trailElem->item.nextTrailElemHandle;
            }
            if (terminalTrailElemHandle != trail->item.lastElemHandle)
                return false;

            trailHandle = trail->item.nextTrailHandle;
        }
    }

    // Every live effect has an owner. Root effects are explicitly self-owned;
    // runner/emitted effects point at another live effect. A non-self-owned
    // self-reference would recurse through reference release, so the flag and
    // handle relationship must agree in both directions.
    std::array<std::size_t, MAX_EFFECTS> inboundLiveOwnedEffectCounts{};
    for (std::int64_t activeIndex = firstActiveEffect;
         activeIndex < firstFreeEffect;
         ++activeIndex)
    {
        const std::uint16_t effectHandle =
            system->allEffectHandles[
                static_cast<std::size_t>(activeIndex)
                & (MAX_EFFECTS - 1)];
        const FxEffect *const effect =
            FxDecodeHandle<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                static_cast<const FxEffect *>(system->effects),
                effectHandle);
        if (!effect)
            return false;

        const FxEffect *const owner =
            FxDecodeHandle<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                static_cast<const FxEffect *>(system->effects),
                effect->owner);
        if (!owner)
            return false;

        const std::size_t ownerIndex =
            static_cast<std::size_t>(owner - system->effects);
        if (!allocatedEffectSlots[ownerIndex])
            return false;

        const std::uint32_t status = static_cast<std::uint32_t>(
            Sys_AtomicLoad(&effect->status));
        const bool isSelfOwned =
            (status & selfOwnedStatusBit) != 0;
        if (isSelfOwned != (effect->owner == effectHandle))
            return false;

        // A non-self-owned effect retains exactly one owner reference until
        // its own final reference is released. Retired effects remain in the
        // ring until garbage collection, but no longer contribute to their
        // former owner's encoded child count.
        if (!isSelfOwned && (status & refCountMask) != 0)
        {
            if (inboundLiveOwnedEffectCounts[ownerIndex]
                == MAX_EFFECTS - 1)
            {
                return false;
            }
            ++inboundLiveOwnedEffectCounts[ownerIndex];
        }
    }

    for (std::int64_t activeIndex = firstActiveEffect;
         activeIndex < firstFreeEffect;
         ++activeIndex)
    {
        const std::uint16_t effectHandle =
            system->allEffectHandles[
                static_cast<std::size_t>(activeIndex)
                & (MAX_EFFECTS - 1)];
        const FxEffect *const effect =
            FxDecodeHandle<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                static_cast<const FxEffect *>(system->effects),
                effectHandle);
        if (!effect)
            return false;

        const std::size_t effectIndex =
            static_cast<std::size_t>(effect - system->effects);
        const std::uint32_t status = static_cast<std::uint32_t>(
            Sys_AtomicLoad(&effect->status));
        const std::size_t encodedOwnedEffectCount =
            (status & ownedEffectCountMask) >> ownedEffectCountShift;
        if (encodedOwnedEffectCount
            != inboundLiveOwnedEffectCounts[effectIndex])
        {
            return false;
        }

        // Owner relationships form a forest rooted at self-owned effects.
        // Bounding the walk makes cycle rejection deterministic without
        // recursion or allocation.
        const FxEffect *ownerChainEffect = effect;
        bool reachedSelfOwnedRoot = false;
        for (std::size_t depth = 0; depth < MAX_EFFECTS; ++depth)
        {
            const std::uint32_t ownerChainStatus =
                static_cast<std::uint32_t>(
                    Sys_AtomicLoad(&ownerChainEffect->status));
            if ((ownerChainStatus & selfOwnedStatusBit) != 0)
            {
                reachedSelfOwnedRoot = true;
                break;
            }

            ownerChainEffect =
                FxDecodeHandle<
                    FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                    static_cast<const FxEffect *>(system->effects),
                    ownerChainEffect->owner);
            if (!ownerChainEffect)
                return false;

            const std::size_t ownerChainIndex =
                static_cast<std::size_t>(
                    ownerChainEffect - system->effects);
            if (!allocatedEffectSlots[ownerChainIndex])
                return false;
        }
        if (!reachedSelfOwnedRoot)
            return false;
    }

    const std::int32_t activeSpotLightEffectCount =
        Sys_AtomicLoad(&system->activeSpotLightEffectCount);
    const std::int32_t activeSpotLightElemCount =
        Sys_AtomicLoad(&system->activeSpotLightElemCount);
    if (activeSpotLightEffectCount < 0
        || activeSpotLightEffectCount > 1
        || activeSpotLightElemCount < 0
        || activeSpotLightElemCount > 1
        || (activeSpotLightEffectCount == 0
            && activeSpotLightElemCount != 0))
    {
        return false;
    }

    const FxEffect *spotLightEffect = nullptr;
    std::size_t spotLightEffectIndex = MAX_EFFECTS;
    if (activeSpotLightEffectCount == 1)
    {
        spotLightEffect =
            FxDecodeHandle<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                static_cast<const FxEffect *>(system->effects),
                system->activeSpotLightEffectHandle);
        if (!spotLightEffect)
            return false;
        spotLightEffectIndex = static_cast<std::size_t>(
            spotLightEffect - system->effects);
        if (!allocatedEffectSlots[spotLightEffectIndex])
            return false;
    }

    if (activeSpotLightElemCount == 1)
    {
        if (!spotLightEffect
            || (static_cast<std::uint32_t>(
                    Sys_AtomicLoad(&spotLightEffect->status))
                & refCountMask)
                == 0)
        {
            return false;
        }

        std::size_t elemIndex = 0;
        const FxPool<FxElem> *const spotLightElem =
            DecodeAllocatedPoolHandle<FxElem, MAX_ELEMS>(
                system->elems,
                elemAllocationState,
                system->activeSpotLightElemHandle,
                &elemIndex);
        if (!spotLightElem || visitedElems[elemIndex])
            return false;
        visitedElems[elemIndex] = true;
        ++visitedElemCount;
        ++linkedEffectReferenceCounts[spotLightEffectIndex];
    }

    for (std::int64_t activeIndex = firstActiveEffect;
         activeIndex < firstFreeEffect;
         ++activeIndex)
    {
        const FxEffect *const effect =
            FxDecodeHandle<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                static_cast<const FxEffect *>(system->effects),
                system->allEffectHandles[
                    static_cast<std::size_t>(activeIndex)
                    & (MAX_EFFECTS - 1)]);
        if (!effect)
            return false;

        const std::size_t effectIndex =
            static_cast<std::size_t>(effect - system->effects);
        const std::uint32_t status = static_cast<std::uint32_t>(
            Sys_AtomicLoad(&effect->status));
        const std::size_t referenceCount = status & refCountMask;
        const std::size_t requiredReferenceCount =
            linkedEffectReferenceCounts[effectIndex]
            + inboundLiveOwnedEffectCounts[effectIndex]
            + ((status & pendingLoopElemStatusBit) != 0 ? 1u : 0u);
        if (requiredReferenceCount > referenceCount
            || referenceCount == refCountMask)
        {
            return false;
        }
    }

    return EveryAllocatedSlotWasVisited(
               elemAllocationState, visitedElems, visitedElemCount)
        && EveryAllocatedSlotWasVisited(
            trailAllocationState, visitedTrails, visitedTrailCount)
        && EveryAllocatedSlotWasVisited(
            trailElemAllocationState,
            visitedTrailElems,
            visitedTrailElemCount);
}

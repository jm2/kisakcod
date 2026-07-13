#include <EffectsCore/fx_pool_graph.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_same_v<decltype(FxSystem::activeElemCount),
                             volatile std::int32_t>);
static_assert(std::is_same_v<decltype(FxSystem::activeTrailElemCount),
                             volatile std::int32_t>);
static_assert(std::is_same_v<decltype(FxSystem::activeTrailCount),
                             volatile std::int32_t>);
static_assert(sizeof(FxPool<FxElem>) == sizeof(FxElem));
static_assert(sizeof(FxPool<FxTrail>) == sizeof(FxTrail));
static_assert(sizeof(FxPool<FxTrailElem>) == sizeof(FxTrailElem));
static_assert(std::is_same_v<decltype(FxTrail::defIndex), std::uint8_t>);
static_assert(std::is_same_v<decltype(FxTrail::sequence), std::uint8_t>);
static_assert(std::is_same_v<
              std::remove_all_extents_t<decltype(FxTrailElem::basis)>,
              std::int8_t>);
static_assert(std::is_trivially_copyable_v<
              FxPoolAllocationState<MAX_ELEMS>>);
static_assert(noexcept(FxValidatePoolAllocationGraph(
    std::declval<const FxSystem *>(),
    std::declval<const FxPoolAllocationState<MAX_ELEMS> &>(),
    std::declval<const FxPoolAllocationState<MAX_TRAILS> &>(),
    std::declval<const FxPoolAllocationState<MAX_TRAIL_ELEMS> &>())));

namespace
{
template <typename ITEM_TYPE, std::size_t LIMIT>
void InitializeFreeList(FxPool<ITEM_TYPE> (&pool)[LIMIT])
{
    std::memset(pool, 0, sizeof(pool));
    for (std::size_t index = 0; index + 1 < LIMIT; ++index)
        pool[index].nextFree = static_cast<std::int32_t>(index + 1);
    pool[LIMIT - 1].nextFree = -1;
}

template <typename ITEM_TYPE, std::size_t LIMIT>
std::uint16_t PoolHandle(
    const FxPool<ITEM_TYPE> *pool,
    const std::size_t index)
{
    return FxEncodeHandle<
        FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
        pool, &pool[index].item);
}

struct PoolGraphFixture
{
    std::unique_ptr<FxSystemBuffers> buffers =
        std::make_unique<FxSystemBuffers>();
    FxSystem system{};
    FxPoolAllocationState<MAX_ELEMS> elemState{};
    FxPoolAllocationState<MAX_TRAILS> trailState{};
    FxPoolAllocationState<MAX_TRAIL_ELEMS> trailElemState{};

    PoolGraphFixture()
    {
        std::memset(buffers.get(), 0, sizeof(*buffers));
        system.effects = buffers->effects;
        system.elems = buffers->elems;
        system.trails = buffers->trails;
        system.trailElems = buffers->trailElems;
        system.deferredElems = buffers->deferredElems;
        system.firstActiveEffect = 0;
        system.firstNewEffect = 2;
        system.firstFreeEffect = 2;

        for (std::size_t effectIndex = 0;
             effectIndex < MAX_EFFECTS;
             ++effectIndex)
        {
            system.allEffectHandles[effectIndex] =
                FxEncodeHandle<
                    FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                    buffers->effects, &buffers->effects[effectIndex]);
        }

        FxPoolResetAllocationState(&elemState);
        FxPoolResetAllocationState(&trailState);
        FxPoolResetAllocationState(&trailElemState);

        for (std::size_t effectIndex = 0; effectIndex < 2; ++effectIndex)
        {
            FxEffect &effect = buffers->effects[effectIndex];
            effect.firstElemHandle[0] = FX_INVALID_HANDLE;
            effect.firstElemHandle[1] = FX_INVALID_HANDLE;
            effect.firstElemHandle[2] = FX_INVALID_HANDLE;
            effect.firstSortedElemHandle = FX_INVALID_HANDLE;
            effect.firstTrailHandle = FX_INVALID_HANDLE;
        }
        constexpr std::uint32_t selfOwnedStatusBit = 0x10000000u;
        constexpr std::uint32_t oneOwnedEffectAndSixReferences =
            0x00020006u;
        buffers->effects[0].status =
            selfOwnedStatusBit | oneOwnedEffectAndSixReferences;
        buffers->effects[0].owner = system.allEffectHandles[0];
        buffers->effects[1].status = 2;
        buffers->effects[1].owner = system.allEffectHandles[0];

        for (std::size_t elemIndex = 0; elemIndex < 5; ++elemIndex)
        {
            FxPoolSetAllocationStateBit(&elemState, elemIndex, true);
            ++elemState.allocatedCount;
            buffers->elems[elemIndex].item.nextElemHandleInEffect =
                FX_INVALID_HANDLE;
            buffers->elems[elemIndex].item.prevElemHandleInEffect =
                FX_INVALID_HANDLE;
        }
        Sys_AtomicStore(
            &system.activeElemCount,
            static_cast<std::int32_t>(elemState.allocatedCount));

        const std::uint16_t elem0 =
            PoolHandle<FxElem, MAX_ELEMS>(buffers->elems, 0);
        const std::uint16_t elem1 =
            PoolHandle<FxElem, MAX_ELEMS>(buffers->elems, 1);
        const std::uint16_t elem2 =
            PoolHandle<FxElem, MAX_ELEMS>(buffers->elems, 2);
        const std::uint16_t elem3 =
            PoolHandle<FxElem, MAX_ELEMS>(buffers->elems, 3);
        const std::uint16_t elem4 =
            PoolHandle<FxElem, MAX_ELEMS>(buffers->elems, 4);
        buffers->elems[0].item.nextElemHandleInEffect = elem1;
        buffers->elems[1].item.prevElemHandleInEffect = elem0;
        buffers->effects[0].firstElemHandle[0] = elem0;
        buffers->effects[0].firstSortedElemHandle = elem1;
        buffers->effects[0].firstElemHandle[1] = elem2;
        buffers->effects[1].firstElemHandle[2] = elem3;

        for (std::size_t trailIndex = 0; trailIndex < 2; ++trailIndex)
        {
            FxPoolSetAllocationStateBit(&trailState, trailIndex, true);
            ++trailState.allocatedCount;
            buffers->trails[trailIndex].item.nextTrailHandle =
                FX_INVALID_HANDLE;
            buffers->trails[trailIndex].item.firstElemHandle =
                FX_INVALID_HANDLE;
            buffers->trails[trailIndex].item.lastElemHandle =
                FX_INVALID_HANDLE;
        }
        Sys_AtomicStore(
            &system.activeTrailCount,
            static_cast<std::int32_t>(trailState.allocatedCount));

        const std::uint16_t trail0 =
            PoolHandle<FxTrail, MAX_TRAILS>(buffers->trails, 0);
        const std::uint16_t trail1 =
            PoolHandle<FxTrail, MAX_TRAILS>(buffers->trails, 1);
        buffers->effects[0].firstTrailHandle = trail0;
        buffers->trails[0].item.nextTrailHandle = trail1;

        for (std::size_t trailElemIndex = 0;
             trailElemIndex < 2;
             ++trailElemIndex)
        {
            FxPoolSetAllocationStateBit(
                &trailElemState, trailElemIndex, true);
            ++trailElemState.allocatedCount;
            buffers->trailElems[trailElemIndex]
                .item.nextTrailElemHandle = FX_INVALID_HANDLE;
        }
        Sys_AtomicStore(
            &system.activeTrailElemCount,
            static_cast<std::int32_t>(trailElemState.allocatedCount));

        const std::uint16_t trailElem0 =
            PoolHandle<FxTrailElem, MAX_TRAIL_ELEMS>(
                buffers->trailElems, 0);
        const std::uint16_t trailElem1 =
            PoolHandle<FxTrailElem, MAX_TRAIL_ELEMS>(
                buffers->trailElems, 1);
        buffers->trails[0].item.firstElemHandle = trailElem0;
        buffers->trails[0].item.lastElemHandle = trailElem1;
        buffers->trailElems[0].item.nextTrailElemHandle = trailElem1;

        system.activeSpotLightEffectCount = 1;
        system.activeSpotLightElemCount = 1;
        system.activeSpotLightEffectHandle = system.allEffectHandles[1];
        system.activeSpotLightElemHandle = elem4;
    }

    bool IsValid() const noexcept
    {
        return FxValidatePoolAllocationGraph(
            &system, elemState, trailState, trailElemState);
    }
};

template <typename ITEM_TYPE, std::size_t LIMIT>
bool ExhaustAndRefillPool()
{
    auto pool = std::make_unique<FxPool<ITEM_TYPE>[]>(LIMIT);
    std::memset(pool.get(), 0, LIMIT * sizeof(FxPool<ITEM_TYPE>));
    for (std::size_t index = 0; index + 1 < LIMIT; ++index)
        pool[index].nextFree = static_cast<std::int32_t>(index + 1);
    pool[LIMIT - 1].nextFree = -1;

    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<LIMIT> allocationState{};
    FxPoolResetAllocationState(&allocationState);
    std::vector<FxPool<ITEM_TYPE> *> allocated;
    allocated.reserve(LIMIT);
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        FxPoolMutationStatus status;
        FxPool<ITEM_TYPE> *const item =
            FxPoolAllocateLocked<ITEM_TYPE, LIMIT>(
                &firstFree,
                pool.get(),
                &activeCount,
                &allocationState,
                &status);
        if (item != &pool[index]
            || status != FxPoolMutationStatus::Success)
        {
            return false;
        }
        allocated.push_back(item);
    }
    FxPoolMutationStatus status;
    if (firstFree != -1
        || Sys_AtomicLoad(&activeCount) != static_cast<std::int32_t>(LIMIT)
        || FxPoolAllocateLocked<ITEM_TYPE, LIMIT>(
            &firstFree,
            pool.get(),
            &activeCount,
            &allocationState,
            &status)
        || status != FxPoolMutationStatus::Empty)
    {
        return false;
    }
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        if (!FxPoolAllocationStateIsAllocated(allocationState, index))
            return false;
    }

    for (FxPool<ITEM_TYPE> *const item : allocated)
    {
        if (FxPoolFreeLocked<ITEM_TYPE, LIMIT>(
                &item->item,
                &firstFree,
                pool.get(),
                &activeCount,
                &allocationState)
            != FxPoolMutationStatus::Success)
        {
            return false;
        }
    }
    if (Sys_AtomicLoad(&activeCount) != 0
        || allocationState.allocatedCount != 0)
        return false;
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        if (FxPoolAllocationStateIsAllocated(allocationState, index))
            return false;
    }

    std::vector<bool> seen(LIMIT, false);
    for (std::size_t allocation = 0; allocation < LIMIT; ++allocation)
    {
        FxPool<ITEM_TYPE> *const item =
            FxPoolAllocateLocked<ITEM_TYPE, LIMIT>(
                &firstFree,
                pool.get(),
                &activeCount,
                &allocationState,
                &status);
        if (!item || status != FxPoolMutationStatus::Success)
            return false;
        const std::size_t index = static_cast<std::size_t>(item - pool.get());
        if (index >= LIMIT || seen[index])
            return false;
        seen[index] = true;
    }
    return firstFree == -1
        && Sys_AtomicLoad(&activeCount) == static_cast<std::int32_t>(LIMIT)
        && allocationState.allocatedCount == LIMIT;
}

template <typename T>
std::array<std::byte, sizeof(T)> Snapshot(const T &value)
{
    std::array<std::byte, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(value));
    return bytes;
}

template <typename T>
bool MatchesSnapshot(const T &value, const std::array<std::byte, sizeof(T)> &bytes)
{
    return std::memcmp(&value, bytes.data(), sizeof(value)) == 0;
}

template <std::size_t LIMIT>
bool MatchesAllocationState(
    const FxPoolAllocationState<LIMIT> &state,
    const FxPoolAllocationState<LIMIT> &expected)
{
    return state.allocatedWords == expected.allocatedWords
        && state.allocatedCount == expected.allocatedCount
        && state.initialized == expected.initialized;
}

bool SignedFxByteFieldsPreserveNegativeValues()
{
    constexpr std::int8_t expected[2][3] = {
        {-128, -97, -1},
        {-2, -64, -127},
    };
    FxTrailElem source{};
    std::memcpy(source.basis, expected, sizeof(expected));

    const auto serialized = Snapshot(source);
    FxTrailElem restored{};
    std::memcpy(&restored, serialized.data(), serialized.size());
    for (std::size_t row = 0; row < 2; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (static_cast<int>(restored.basis[row][column])
                != static_cast<int>(expected[row][column]))
            {
                return false;
            }
        }
    }
    return true;
}

bool AllocationAndFreeTransitions()
{
    FxPool<FxTrail> pool[3];
    InitializeFreeList(pool);
    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<3> allocationState{};
    FxPoolResetAllocationState(&allocationState);
    FxPoolMutationStatus status = FxPoolMutationStatus::InvalidArgument;

    FxPool<FxTrail> *first = FxPoolAllocateLocked<FxTrail, 3>(
        &firstFree, pool, &activeCount, &allocationState, &status);
    if (first != &pool[0] || status != FxPoolMutationStatus::Success
        || firstFree != 1 || Sys_AtomicLoad(&activeCount) != 1)
    {
        return false;
    }

    FxPool<FxTrail> *second = FxPoolAllocateLocked<FxTrail, 3>(
        &firstFree, pool, &activeCount, &allocationState, &status);
    FxPool<FxTrail> *third = FxPoolAllocateLocked<FxTrail, 3>(
        &firstFree, pool, &activeCount, &allocationState, &status);
    if (second != &pool[1] || third != &pool[2] || firstFree != -1
        || Sys_AtomicLoad(&activeCount) != 3)
    {
        return false;
    }

    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::Empty || firstFree != -1
        || Sys_AtomicLoad(&activeCount) != 3)
    {
        return false;
    }

    pool[1].item = FxTrail{FX_INVALID_HANDLE, 7, 9, 3, 4};
    bool callbackCalled = false;
    bool callbackSawUnpublishedItem = false;
    const FxPoolMutationStatus freeStatus = FxPoolFreeLocked<FxTrail, 3>(
            &pool[1].item,
            &firstFree,
            pool,
            &activeCount,
            &allocationState,
            [&]() noexcept {
                callbackCalled = true;
                callbackSawUnpublishedItem = firstFree == -1
                    && Sys_AtomicLoad(&activeCount) == 3
                    && allocationState.allocatedCount == 3
                    && FxPoolAllocationStateIsAllocated(allocationState, 1)
                    && pool[1].item.lastElemHandle == 9
                    && pool[1].item.defIndex == 3;
            });
    const auto releasedSlot = Snapshot(pool[1]);
    bool payloadCleared = true;
    for (std::size_t byteIndex = sizeof(std::int32_t);
         byteIndex < releasedSlot.size();
         ++byteIndex)
    {
        payloadCleared = payloadCleared
            && releasedSlot[byteIndex] == std::byte{};
    }
    if (freeStatus != FxPoolMutationStatus::Success
        || !callbackCalled || !callbackSawUnpublishedItem
        || firstFree != 1 || pool[1].nextFree != -1
        || !payloadCleared
        || allocationState.allocatedCount != 2
        || FxPoolAllocationStateIsAllocated(allocationState, 1)
        || Sys_AtomicLoad(&activeCount) != 2)
    {
        return false;
    }

    return FxPoolFreeLocked<FxTrail, 3>(
               &pool[1].item,
               &firstFree,
               pool,
               &activeCount,
               &allocationState)
            == FxPoolMutationStatus::DuplicateFreeHead
        && firstFree == 1 && Sys_AtomicLoad(&activeCount) == 2;
}

bool AllocationFailuresAreTransactional()
{
    FxPool<FxTrail> pool[3];
    InitializeFreeList(pool);
    alignas(4) volatile std::int32_t firstFree = 3;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<3> allocationState{};
    FxPoolMutationStatus status = FxPoolMutationStatus::Success;

    auto before = Snapshot(pool);
    auto stateBefore = allocationState;
    firstFree = 0;
    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::UninitializedAllocationState
        || firstFree != 0 || Sys_AtomicLoad(&activeCount) != 0
        || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    FxPoolResetAllocationState(&allocationState);
    stateBefore = allocationState;
    firstFree = -1;
    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::CorruptFreeList || firstFree != -1
        || Sys_AtomicLoad(&activeCount) != 0 || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    firstFree = 3;
    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::CorruptFreeList || firstFree != 3
        || Sys_AtomicLoad(&activeCount) != 0 || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    firstFree = 0;
    pool[0].nextFree = 0;
    before = Snapshot(pool);
    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::CorruptFreeList || firstFree != 0
        || Sys_AtomicLoad(&activeCount) != 0 || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    pool[0].nextFree = 3;
    before = Snapshot(pool);
    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::CorruptFreeList || firstFree != 0
        || Sys_AtomicLoad(&activeCount) != 0 || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    pool[0].nextFree = 1;
    Sys_AtomicStore(&activeCount, 1);
    before = Snapshot(pool);
    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::AllocationStateMismatch
        || firstFree != 0 || Sys_AtomicLoad(&activeCount) != 1
        || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    FxPoolSetAllocationStateBit(&allocationState, 0, true);
    allocationState.allocatedCount = 1;
    stateBefore = allocationState;
    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree, pool, &activeCount, &allocationState, &status)
        || status != FxPoolMutationStatus::CorruptFreeList
        || firstFree != 0 || Sys_AtomicLoad(&activeCount) != 1
        || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    FxPoolResetAllocationState(&allocationState);
    stateBefore = allocationState;
    Sys_AtomicStore(&activeCount, 4);
    before = Snapshot(pool);
    return !FxPoolAllocateLocked<FxTrail, 3>(
               &firstFree,
               pool,
               &activeCount,
               &allocationState,
               &status)
        && status == FxPoolMutationStatus::InvalidActiveCount
        && firstFree == 0 && Sys_AtomicLoad(&activeCount) == 4
        && MatchesSnapshot(pool, before)
        && MatchesAllocationState(allocationState, stateBefore);
}

bool FreeFailuresAreTransactional()
{
    FxPool<FxTrail> pool[3];
    InitializeFreeList(pool);
    alignas(4) volatile std::int32_t firstFree = 1;
    alignas(4) volatile std::int32_t activeCount = 1;
    FxPoolAllocationState<3> allocationState{};
    FxPoolResetAllocationState(&allocationState);
    FxPoolSetAllocationStateBit(&allocationState, 0, true);
    allocationState.allocatedCount = 1;
    FxTrail foreign{};

    auto before = Snapshot(pool);
    auto stateBefore = allocationState;
    if (FxPoolFreeLocked<FxTrail, 3>(
            &foreign, &firstFree, pool, &activeCount, &allocationState)
            != FxPoolMutationStatus::InvalidArgument
        || firstFree != 1 || Sys_AtomicLoad(&activeCount) != 1
        || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    FxTrail *const misaligned = reinterpret_cast<FxTrail *>(
        reinterpret_cast<std::byte *>(pool) + 1);
    if (FxPoolFreeLocked<FxTrail, 3>(
            misaligned,
            &firstFree,
            pool,
            &activeCount,
            &allocationState)
            != FxPoolMutationStatus::InvalidArgument
        || firstFree != 1 || Sys_AtomicLoad(&activeCount) != 1
        || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    firstFree = 3;
    if (FxPoolFreeLocked<FxTrail, 3>(
            &pool[0].item,
            &firstFree,
            pool,
            &activeCount,
            &allocationState)
            != FxPoolMutationStatus::CorruptFreeList
        || firstFree != 3 || Sys_AtomicLoad(&activeCount) != 1
        || !MatchesSnapshot(pool, before)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    firstFree = 1;
    Sys_AtomicStore(&activeCount, 0);
    return FxPoolFreeLocked<FxTrail, 3>(
               &pool[0].item,
               &firstFree,
               pool,
               &activeCount,
               &allocationState)
            == FxPoolMutationStatus::InvalidActiveCount
        && firstFree == 1 && Sys_AtomicLoad(&activeCount) == 0
        && MatchesSnapshot(pool, before)
        && MatchesAllocationState(allocationState, stateBefore);
}

bool AllocationCycleIsDetectedTransactionally()
{
    FxPool<FxTrail> pool[3];
    InitializeFreeList(pool);
    pool[1].nextFree = 0;
    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<3> allocationState{};
    FxPoolResetAllocationState(&allocationState);
    FxPoolMutationStatus status = FxPoolMutationStatus::InvalidArgument;

    if (FxPoolAllocateLocked<FxTrail, 3>(
            &firstFree,
            pool,
            &activeCount,
            &allocationState,
            &status)
            != &pool[0]
        || status != FxPoolMutationStatus::Success || firstFree != 1
        || Sys_AtomicLoad(&activeCount) != 1)
    {
        return false;
    }

    const auto poolBefore = Snapshot(pool);
    const auto stateBefore = allocationState;
    return !FxPoolAllocateLocked<FxTrail, 3>(
               &firstFree,
               pool,
               &activeCount,
               &allocationState,
               &status)
        && status == FxPoolMutationStatus::CorruptFreeList
        && firstFree == 1 && Sys_AtomicLoad(&activeCount) == 1
        && MatchesSnapshot(pool, poolBefore)
        && MatchesAllocationState(allocationState, stateBefore);
}

bool NonHeadDoubleFreeIsRejectedTransactionally()
{
    FxPool<FxTrail> pool[3];
    InitializeFreeList(pool);
    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<3> allocationState{};
    FxPoolResetAllocationState(&allocationState);
    FxPoolMutationStatus status = FxPoolMutationStatus::InvalidArgument;

    for (std::size_t index = 0; index < 3; ++index)
    {
        if (FxPoolAllocateLocked<FxTrail, 3>(
                &firstFree,
                pool,
                &activeCount,
                &allocationState,
                &status)
                != &pool[index]
            || status != FxPoolMutationStatus::Success)
        {
            return false;
        }
    }
    if (FxPoolFreeLocked<FxTrail, 3>(
            &pool[0].item,
            &firstFree,
            pool,
            &activeCount,
            &allocationState)
            != FxPoolMutationStatus::Success
        || FxPoolFreeLocked<FxTrail, 3>(
               &pool[1].item,
               &firstFree,
               pool,
               &activeCount,
               &allocationState)
            != FxPoolMutationStatus::Success)
    {
        return false;
    }

    const auto poolBefore = Snapshot(pool);
    const auto stateBefore = allocationState;
    return FxPoolFreeLocked<FxTrail, 3>(
               &pool[0].item,
               &firstFree,
               pool,
               &activeCount,
               &allocationState)
            == FxPoolMutationStatus::AlreadyFree
        && firstFree == 1 && Sys_AtomicLoad(&activeCount) == 1
        && MatchesSnapshot(pool, poolBefore)
        && MatchesAllocationState(allocationState, stateBefore);
}

bool AllocatedFreeHeadCorruptionIsRejectedTransactionally()
{
    FxPool<FxTrail> pool[3];
    InitializeFreeList(pool);
    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<3> allocationState{};
    FxPoolResetAllocationState(&allocationState);
    FxPoolMutationStatus status = FxPoolMutationStatus::InvalidArgument;

    for (std::size_t index = 0; index < 2; ++index)
    {
        if (FxPoolAllocateLocked<FxTrail, 3>(
                &firstFree,
                pool,
                &activeCount,
                &allocationState,
                &status)
                != &pool[index]
            || status != FxPoolMutationStatus::Success)
        {
            return false;
        }
    }

    firstFree = 0;
    auto poolBefore = Snapshot(pool);
    auto stateBefore = allocationState;
    if (FxPoolFreeLocked<FxTrail, 3>(
            &pool[0].item,
            &firstFree,
            pool,
            &activeCount,
            &allocationState)
            != FxPoolMutationStatus::DuplicateFreeHead
        || firstFree != 0 || Sys_AtomicLoad(&activeCount) != 2
        || !MatchesSnapshot(pool, poolBefore)
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    firstFree = 1;
    poolBefore = Snapshot(pool);
    stateBefore = allocationState;
    return FxPoolFreeLocked<FxTrail, 3>(
               &pool[0].item,
               &firstFree,
               pool,
               &activeCount,
               &allocationState)
            == FxPoolMutationStatus::CorruptFreeList
        && firstFree == 1 && Sys_AtomicLoad(&activeCount) == 2
        && MatchesSnapshot(pool, poolBefore)
        && MatchesAllocationState(allocationState, stateBefore);
}

bool AllocationStateRebuildIsBoundedAndTransactional()
{
    FxPool<FxTrail> pool[4];
    InitializeFreeList(pool);
    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 37;
    FxPoolAllocationState<4> allocationState{};

    if (FxPoolAllocationStateIsInitialized(&allocationState)
        || FxPoolRebuildAllocationStateLocked<FxTrail, 4>(
               &firstFree, pool, &activeCount, &allocationState)
            != FxPoolMutationStatus::Success
        || !FxPoolAllocationStateIsInitialized(&allocationState)
        || allocationState.allocatedCount != 0
        || Sys_AtomicLoad(&activeCount) != 0)
    {
        return false;
    }

    FxPoolMutationStatus status = FxPoolMutationStatus::InvalidArgument;
    for (std::size_t index = 0; index < 2; ++index)
    {
        if (FxPoolAllocateLocked<FxTrail, 4>(
                &firstFree,
                pool,
                &activeCount,
                &allocationState,
                &status)
                != &pool[index]
            || status != FxPoolMutationStatus::Success)
        {
            return false;
        }
    }

    FxPoolResetAllocationState(&allocationState);
    Sys_AtomicStore(&activeCount, -7);
    if (FxPoolRebuildAllocationStateLocked<FxTrail, 4>(
            &firstFree, pool, &activeCount, &allocationState)
            != FxPoolMutationStatus::Success
        || allocationState.allocatedCount != 2
        || Sys_AtomicLoad(&activeCount) != 2
        || !FxPoolAllocationStateIsAllocated(allocationState, 0)
        || !FxPoolAllocationStateIsAllocated(allocationState, 1)
        || FxPoolAllocationStateIsAllocated(allocationState, 2)
        || FxPoolAllocationStateIsAllocated(allocationState, 3))
    {
        return false;
    }

    const auto stateBefore = allocationState;
    pool[3].nextFree = 4;
    if (FxPoolRebuildAllocationStateLocked<FxTrail, 4>(
            &firstFree, pool, &activeCount, &allocationState)
            != FxPoolMutationStatus::CorruptFreeList
        || Sys_AtomicLoad(&activeCount) != 2
        || !MatchesAllocationState(allocationState, stateBefore))
    {
        return false;
    }

    pool[3].nextFree = 2;
    return FxPoolRebuildAllocationStateLocked<FxTrail, 4>(
               &firstFree, pool, &activeCount, &allocationState)
            == FxPoolMutationStatus::CorruptFreeList
        && Sys_AtomicLoad(&activeCount) == 2
        && MatchesAllocationState(allocationState, stateBefore);
}

bool ConcurrentOwnershipStress()
{
    constexpr std::size_t LIMIT = 64;
    constexpr int THREAD_COUNT = 8;
    constexpr int ITERATIONS = 4000;

    FxPool<FxTrail> pool[LIMIT];
    InitializeFreeList(pool);
    alignas(4) volatile std::int32_t firstFree = 0;
    alignas(4) volatile std::int32_t activeCount = 0;
    FxPoolAllocationState<LIMIT> allocationState{};
    FxPoolResetAllocationState(&allocationState);
    std::array<std::atomic<bool>, LIMIT> owned{};
    std::atomic<bool> failed{false};
    std::atomic<bool> stopObserver{false};
    std::mutex poolMutex;
    std::vector<std::thread> threads;
    std::thread observer([&] {
        while (!stopObserver.load(std::memory_order_relaxed))
        {
            const std::int32_t active = Sys_AtomicLoad(&activeCount);
            if (active < 0 || active > static_cast<std::int32_t>(LIMIT))
                failed.store(true, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    for (int threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex)
    {
        threads.emplace_back([&] {
            for (int iteration = 0; iteration < ITERATIONS; ++iteration)
            {
                FxPool<FxTrail> *item = nullptr;
                std::size_t itemIndex = LIMIT;
                {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    FxPoolMutationStatus status;
                    item = FxPoolAllocateLocked<FxTrail, LIMIT>(
                        &firstFree,
                        pool,
                        &activeCount,
                        &allocationState,
                        &status);
                    if (!item || status != FxPoolMutationStatus::Success)
                    {
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                    itemIndex = static_cast<std::size_t>(item - pool);
                    if (owned[itemIndex].exchange(
                            true, std::memory_order_relaxed))
                    {
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                    item->item = FxTrail{};
                }

                std::this_thread::yield();
                {
                    std::lock_guard<std::mutex> lock(poolMutex);
                    const FxPoolMutationStatus status =
                        FxPoolFreeLocked<FxTrail, LIMIT>(
                            &item->item,
                            &firstFree,
                            pool,
                            &activeCount,
                            &allocationState,
                            [&]() noexcept {
                                if (!owned[itemIndex].exchange(
                                        false, std::memory_order_relaxed))
                                {
                                    failed.store(true, std::memory_order_relaxed);
                                }
                            });
                    if (status != FxPoolMutationStatus::Success)
                    {
                        failed.store(true, std::memory_order_relaxed);
                        return;
                    }
                }
            }
        });
    }

    for (std::thread &thread : threads)
        thread.join();
    stopObserver.store(true, std::memory_order_relaxed);
    observer.join();
    if (failed.load(std::memory_order_relaxed)
        || Sys_AtomicLoad(&activeCount) != 0
        || allocationState.allocatedCount != 0)
    {
        return false;
    }

    std::array<bool, LIMIT> seen{};
    std::int32_t freeIndex = firstFree;
    std::size_t freeCount = 0;
    while (freeIndex != -1)
    {
        if (!FxPoolFreeIndexIsValid<LIMIT>(freeIndex)
            || seen[static_cast<std::size_t>(freeIndex)]
            || freeCount == LIMIT)
        {
            return false;
        }
        seen[static_cast<std::size_t>(freeIndex)] = true;
        ++freeCount;
        freeIndex = pool[freeIndex].nextFree;
    }
    if (freeCount != LIMIT)
        return false;
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        if (FxPoolAllocationStateIsAllocated(allocationState, index))
            return false;
    }
    return true;
}

template <typename ITEM_TYPE, std::size_t LIMIT>
bool PoolHandleRoundTrips()
{
    auto pool = std::make_unique<FxPool<ITEM_TYPE>[]>(LIMIT);
    constexpr std::size_t stride =
        sizeof(FxPool<ITEM_TYPE>) / ITEM_TYPE::HANDLE_SCALE;
    for (std::size_t index = 0; index < LIMIT; ++index)
    {
        const std::uint16_t handle =
            FxEncodeHandle<
                FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
                pool.get(), &pool[index].item);
        if (handle != index * stride
            || FxDecodeHandle<
                   FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
                   pool.get(), handle)
                != &pool[index])
        {
            return false;
        }
    }

    ITEM_TYPE *const misaligned = reinterpret_cast<ITEM_TYPE *>(
        reinterpret_cast<std::byte *>(pool.get()) + 1);
    constexpr std::uint32_t handleLimit = LIMIT * stride;
    return FxEncodeHandle<
               FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
               pool.get(), nullptr)
            == FX_INVALID_HANDLE
        && FxEncodeHandle<
               FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
               pool.get(), misaligned)
            == FX_INVALID_HANDLE
        && !FxDecodeHandle<
            FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
            pool.get(), FX_INVALID_HANDLE)
        && !FxDecodeHandle<
            FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
            pool.get(), handleLimit)
        && (stride == 1
            || !FxDecodeHandle<
                FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
                pool.get(), 1));
}

bool EffectHandlesUseNativeEntrySize()
{
    auto effects = std::make_unique<FxEffect[]>(MAX_EFFECTS);
    constexpr std::size_t stride =
        FxHandleStride<FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>();
    constexpr std::size_t lastIndex = MAX_EFFECTS - 1;
    const std::uint16_t lastHandle =
        FxEncodeHandle<FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
            effects.get(), &effects[lastIndex]);

    if (lastHandle != lastIndex * stride
        || FxDecodeHandle<FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
               effects.get(), lastHandle)
            != &effects[lastIndex])
    {
        return false;
    }

    if constexpr (sizeof(void *) == 8)
    {
        if (sizeof(FxEffect) != 0x88 || lastHandle <= 0x7fff)
            return false;
    }
    else if (sizeof(FxEffect) != 0x80)
    {
        return false;
    }

    constexpr std::uint32_t handleLimit = MAX_EFFECTS * stride;
    return !FxDecodeHandle<FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
               effects.get(), handleLimit)
        && !FxDecodeHandle<FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
            effects.get(), 1)
        && !FxDecodeHandle<FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
            effects.get(), FX_INVALID_HANDLE);
}

bool ValidPoolAllocationGraphIsAccepted()
{
    PoolGraphFixture fixture;
    if (!fixture.IsValid())
        return false;

    PoolGraphFixture wrappedFixture;
    const std::uint16_t effect0 =
        wrappedFixture.system.allEffectHandles[0];
    const std::uint16_t effect1 =
        wrappedFixture.system.allEffectHandles[1];
    const std::uint16_t finalEffect =
        wrappedFixture.system.allEffectHandles[MAX_EFFECTS - 1];
    wrappedFixture.system.allEffectHandles[MAX_EFFECTS - 1] = effect0;
    wrappedFixture.system.allEffectHandles[0] = effect1;
    wrappedFixture.system.allEffectHandles[1] = finalEffect;
    wrappedFixture.system.firstActiveEffect =
        static_cast<std::int32_t>(MAX_EFFECTS - 1);
    wrappedFixture.system.firstNewEffect =
        static_cast<std::int32_t>(MAX_EFFECTS + 1);
    wrappedFixture.system.firstFreeEffect =
        static_cast<std::int32_t>(MAX_EFFECTS + 1);
    if (!wrappedFixture.IsValid())
        return false;

    PoolGraphFixture deferredFixture;
    deferredFixture.buffers->effects[1].status |= 0x08000000u;
    if (!deferredFixture.IsValid())
        return false;

    PoolGraphFixture pendingFixture;
    pendingFixture.buffers->effects[1].status = 0x00010003u;
    if (!pendingFixture.IsValid())
        return false;

    PoolGraphFixture rootedChainFixture;
    FxEffect &rootedGrandchild = rootedChainFixture.buffers->effects[2];
    rootedGrandchild.firstElemHandle[0] = FX_INVALID_HANDLE;
    rootedGrandchild.firstElemHandle[1] = FX_INVALID_HANDLE;
    rootedGrandchild.firstElemHandle[2] = FX_INVALID_HANDLE;
    rootedGrandchild.firstSortedElemHandle = FX_INVALID_HANDLE;
    rootedGrandchild.firstTrailHandle = FX_INVALID_HANDLE;
    rootedGrandchild.status = 1;
    rootedGrandchild.owner =
        rootedChainFixture.system.allEffectHandles[1];
    rootedChainFixture.buffers->effects[1].status = 0x00020003u;
    rootedChainFixture.system.firstNewEffect = 3;
    rootedChainFixture.system.firstFreeEffect = 3;
    if (!rootedChainFixture.IsValid())
        return false;

    PoolGraphFixture retiredChildFixture;
    FxEffect &retiredChild = retiredChildFixture.buffers->effects[2];
    retiredChild.firstElemHandle[0] = FX_INVALID_HANDLE;
    retiredChild.firstElemHandle[1] = FX_INVALID_HANDLE;
    retiredChild.firstElemHandle[2] = FX_INVALID_HANDLE;
    retiredChild.firstSortedElemHandle = FX_INVALID_HANDLE;
    const std::uint16_t retiredTrail =
        PoolHandle<FxTrail, MAX_TRAILS>(
            retiredChildFixture.buffers->trails, 1);
    retiredChildFixture.buffers->trails[0].item.nextTrailHandle =
        FX_INVALID_HANDLE;
    retiredChild.firstTrailHandle = retiredTrail;
    retiredChild.status = 0;
    retiredChild.owner = retiredChildFixture.system.allEffectHandles[0];
    retiredChildFixture.system.firstNewEffect = 3;
    retiredChildFixture.system.firstFreeEffect = 3;
    return retiredChildFixture.IsValid();
}

bool NonzeroSelfOwnedTombstoneIsGarbageCollectable()
{
    PoolGraphFixture fixture;
    FxEffect &root = fixture.buffers->effects[0];
    FxEffect &tombstone = fixture.buffers->effects[1];
    constexpr std::uint32_t selfOwnedStatusBit = 0x10000000u;

    // Model a failed spotlight reservation in a nonzero effect slot. The
    // unpublished owner/spotlight relations have been rolled back, while an
    // already allocated empty trail remains attached for normal GC cleanup.
    root.status = selfOwnedStatusBit | 5u;
    tombstone.status = selfOwnedStatusBit;
    tombstone.owner = fixture.system.allEffectHandles[1];
    tombstone.firstElemHandle[0] = FX_INVALID_HANDLE;
    tombstone.firstElemHandle[1] = FX_INVALID_HANDLE;
    tombstone.firstElemHandle[2] = FX_INVALID_HANDLE;
    tombstone.firstSortedElemHandle = FX_INVALID_HANDLE;

    const std::uint16_t tombstoneTrail =
        fixture.buffers->trails[0].item.nextTrailHandle;
    fixture.buffers->trails[0].item.nextTrailHandle = FX_INVALID_HANDLE;
    tombstone.firstTrailHandle = tombstoneTrail;

    FxPoolSetAllocationStateBit(&fixture.elemState, 3, false);
    FxPoolSetAllocationStateBit(&fixture.elemState, 4, false);
    fixture.elemState.allocatedCount -= 2;
    Sys_AtomicStore(
        &fixture.system.activeElemCount,
        static_cast<std::int32_t>(fixture.elemState.allocatedCount));
    fixture.system.activeSpotLightEffectCount = 0;
    fixture.system.activeSpotLightElemCount = 0;
    fixture.system.activeSpotLightEffectHandle = FX_INVALID_HANDLE;
    fixture.system.activeSpotLightElemHandle = FX_INVALID_HANDLE;
    return fixture.IsValid();
}

bool CorruptPoolAllocationGraphsAreRejected()
{
    {
        PoolGraphFixture fixture;
        fixture.system.firstNewEffect = 1;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.system.allEffectHandles[1] =
            fixture.system.allEffectHandles[0];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.system.allEffectHandles[2] =
            fixture.system.allEffectHandles[3];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.system.allEffectHandles[2] = 1;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        constexpr std::size_t effectHandleLimit =
            MAX_EFFECTS
            * FxHandleStride<
                FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>();
        static_assert(effectHandleLimit < FX_INVALID_HANDLE);
        fixture.system.allEffectHandles[2] =
            static_cast<std::uint16_t>(effectHandleLimit);
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].owner = FX_INVALID_HANDLE;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].owner =
            fixture.system.allEffectHandles[2];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].owner =
            fixture.system.allEffectHandles[1];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].owner =
            fixture.system.allEffectHandles[1];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x00020001u;
        fixture.buffers->effects[0].owner =
            fixture.system.allEffectHandles[1];
        fixture.buffers->effects[1].status = 0x00020001u;
        fixture.buffers->effects[1].owner =
            fixture.system.allEffectHandles[0];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x10000002u;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x10040002u;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x10020000u;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].status = 0x20000001u;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].status |= 0x80000000u;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x10020005u;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].status = 1;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x1002FFFFu;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x10000001u;
        fixture.buffers->effects[1].status = 0;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].status = 0x10000001u;
        fixture.buffers->effects[1].status = 0;
        fixture.buffers->effects[0].firstElemHandle[2] =
            fixture.buffers->effects[1].firstElemHandle[2];
        fixture.buffers->effects[1].firstElemHandle[2] =
            FX_INVALID_HANDLE;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].firstElemHandle[1] =
            PoolHandle<FxElem, MAX_ELEMS>(fixture.buffers->elems, 5);
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->elems[1].item.prevElemHandleInEffect =
            FX_INVALID_HANDLE;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->elems[1].item.nextElemHandleInEffect =
            PoolHandle<FxElem, MAX_ELEMS>(fixture.buffers->elems, 0);
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].firstElemHandle[1] =
            fixture.buffers->effects[0].firstElemHandle[1];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[0].firstSortedElemHandle =
            fixture.buffers->effects[0].firstElemHandle[1];
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        // This reproduces the stale owner shape that could otherwise expose a
        // freed FxTrail's overlaid nextFree bytes as endpoint handles.
        fixture.buffers->trails[2].nextFree = 3;
        fixture.buffers->effects[1].firstTrailHandle =
            PoolHandle<FxTrail, MAX_TRAILS>(fixture.buffers->trails, 2);
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->trails[1].item.nextTrailHandle =
            fixture.buffers->effects[0].firstTrailHandle;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->effects[1].firstTrailHandle =
            fixture.buffers->trails[0].item.nextTrailHandle;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->trails[0].item.lastElemHandle =
            FX_INVALID_HANDLE;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->trails[0].item.lastElemHandle =
            fixture.buffers->trails[0].item.firstElemHandle;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.buffers->trails[0].item.firstElemHandle =
            PoolHandle<FxTrailElem, MAX_TRAIL_ELEMS>(
                fixture.buffers->trailElems, 2);
        fixture.buffers->trails[0].item.lastElemHandle =
            fixture.buffers->trails[0].item.firstElemHandle;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.system.activeSpotLightElemHandle =
            PoolHandle<FxElem, MAX_ELEMS>(fixture.buffers->elems, 5);
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        fixture.system.activeSpotLightEffectCount = 0;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        FxPoolSetAllocationStateBit(&fixture.elemState, 6, true);
        ++fixture.elemState.allocatedCount;
        Sys_AtomicStore(
            &fixture.system.activeElemCount,
            static_cast<std::int32_t>(fixture.elemState.allocatedCount));
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        --fixture.elemState.allocatedCount;
        if (fixture.IsValid())
            return false;
    }
    {
        PoolGraphFixture fixture;
        Sys_AtomicStore(&fixture.system.activeTrailCount, 1);
        if (fixture.IsValid())
            return false;
    }

    return true;
}

bool Run(const char *name, bool (*test)())
{
    if (test())
        return true;
    std::cerr << "failed: " << name << '\n';
    return false;
}
} // namespace

int main()
{
    bool ok = true;
    ok = Run("signed FX byte preservation",
             SignedFxByteFieldsPreserveNegativeValues) && ok;
    ok = Run("allocation/free transitions", AllocationAndFreeTransitions) && ok;
    ok = Run("transactional allocation failures", AllocationFailuresAreTransactional) && ok;
    ok = Run("transactional free failures", FreeFailuresAreTransactional) && ok;
    ok = Run("A-B-A allocation cycle detection",
             AllocationCycleIsDetectedTransactionally) && ok;
    ok = Run("non-head double-free detection",
             NonHeadDoubleFreeIsRejectedTransactionally) && ok;
    ok = Run("allocated free-head corruption",
             AllocatedFreeHeadCorruptionIsRejectedTransactionally) && ok;
    ok = Run("bounded allocation-state rebuild",
             AllocationStateRebuildIsBoundedAndTransactional) && ok;
    ok = Run("concurrent exact ownership", ConcurrentOwnershipStress) && ok;
    ok = Run("exhaust/refill FxElem pool",
             ExhaustAndRefillPool<FxElem, MAX_ELEMS>) && ok;
    ok = Run("exhaust/refill FxTrail pool",
             ExhaustAndRefillPool<FxTrail, MAX_TRAILS>) && ok;
    ok = Run("exhaust/refill FxTrailElem pool",
             ExhaustAndRefillPool<FxTrailElem, MAX_TRAIL_ELEMS>) && ok;
    ok = Run("FxElem handles", PoolHandleRoundTrips<FxElem, MAX_ELEMS>) && ok;
    ok = Run("FxTrail handles", PoolHandleRoundTrips<FxTrail, MAX_TRAILS>) && ok;
    ok = Run("FxTrailElem handles", PoolHandleRoundTrips<FxTrailElem, MAX_TRAIL_ELEMS>) && ok;
    ok = Run("native-size effect handles", EffectHandlesUseNativeEntrySize) && ok;
    ok = Run("valid FX allocation graph",
             ValidPoolAllocationGraphIsAccepted) && ok;
    ok = Run("nonzero self-owned reservation tombstone",
             NonzeroSelfOwnedTombstoneIsGarbageCollectable) && ok;
    ok = Run("corrupt FX allocation graphs",
             CorruptPoolAllocationGraphsAreRejected) && ok;
    return ok ? 0 : 1;
}

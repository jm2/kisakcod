#pragma once

#include "fx_runtime.h"

#include <universal/sys_atomic.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

constexpr std::uint16_t FX_INVALID_HANDLE =
    (std::numeric_limits<std::uint16_t>::max)();

enum class FxPoolMutationStatus : std::uint8_t
{
    Success,
    Empty,
    InvalidArgument,
    CorruptFreeList,
    InvalidActiveCount,
    DuplicateFreeHead,
    AlreadyFree,
    UninitializedAllocationState,
    AllocationStateMismatch,
};

constexpr std::size_t FX_POOL_ALLOCATION_WORD_BITS = 64;

template <std::size_t LIMIT>
struct FxPoolAllocationState
{
    static_assert(LIMIT > 0, "FX allocation state cannot be empty");
    static_assert(
        LIMIT <= static_cast<std::size_t>(
                     (std::numeric_limits<std::int32_t>::max)()),
        "FX pool active counts must fit in int32_t");

    static constexpr std::size_t WORD_BITS = FX_POOL_ALLOCATION_WORD_BITS;
    static constexpr std::size_t WORD_COUNT =
        (LIMIT + WORD_BITS - 1) / WORD_BITS;

    std::array<std::uint64_t, WORD_COUNT> allocatedWords{};
    std::size_t allocatedCount = 0;
    bool initialized = false;
};

template <std::size_t LIMIT>
constexpr bool FxPoolAllocationStateIsInitialized(
    const FxPoolAllocationState<LIMIT> *state) noexcept
{
    return state && state->initialized;
}

template <std::size_t LIMIT>
constexpr bool FxPoolAllocationStateIsAllocated(
    const FxPoolAllocationState<LIMIT> &state,
    const std::size_t index) noexcept
{
    return index < LIMIT
        && (state.allocatedWords[
                index / FxPoolAllocationState<LIMIT>::WORD_BITS]
            & (std::uint64_t{1}
               << (index % FxPoolAllocationState<LIMIT>::WORD_BITS)))
            != 0;
}

template <std::size_t LIMIT>
void FxPoolResetAllocationState(
    FxPoolAllocationState<LIMIT> *state) noexcept
{
    if (!state)
        return;

    state->allocatedWords.fill(0);
    state->allocatedCount = 0;
    state->initialized = true;
}

inline void FxPoolSetAllocationWordBit(
    std::uint64_t *words,
    const std::size_t index,
    const bool allocated) noexcept
{
    const std::uint64_t bit =
        std::uint64_t{1}
        << (index % FX_POOL_ALLOCATION_WORD_BITS);
    std::uint64_t &word =
        words[index / FX_POOL_ALLOCATION_WORD_BITS];
    if (allocated)
        word |= bit;
    else
        word &= ~bit;
}

template <std::size_t LIMIT>
void FxPoolSetAllocationStateBit(
    FxPoolAllocationState<LIMIT> *state,
    const std::size_t index,
    const bool allocated) noexcept
{
    if (!state || index >= LIMIT)
        return;
    FxPoolSetAllocationWordBit(
        state->allocatedWords.data(), index, allocated);
}

// Production handle consumers cannot safely recover from a corrupt shared FX
// graph. The low-level codec remains nullable for validation/tests; the engine
// wrapper reports the malformed handle through this fail-closed hook.
[[noreturn]] void KISAK_CDECL FX_InvalidPoolHandle(
    const void *pool,
    std::uint32_t handle);

template <typename ITEM_TYPE>
constexpr bool FxPoolSlotLayoutIsCompatible() noexcept
{
    // The int free-list member can raise the slot alignment (FxTrail is only
    // 2-byte aligned), but it must never reduce item alignment or add stride.
    return sizeof(FxPool<ITEM_TYPE>) == sizeof(ITEM_TYPE)
        && alignof(FxPool<ITEM_TYPE>) >= alignof(ITEM_TYPE);
}

template <typename ENTRY_TYPE, std::size_t LIMIT, std::size_t HANDLE_SCALE>
constexpr bool FxHandleEntrySizeIsValid() noexcept
{
    return HANDLE_SCALE > 0 && sizeof(ENTRY_TYPE) % HANDLE_SCALE == 0;
}

template <typename ENTRY_TYPE, std::size_t LIMIT, std::size_t HANDLE_SCALE>
constexpr std::size_t FxHandleStride() noexcept
{
    static_assert(LIMIT > 0, "FX handle arrays cannot be empty");
    static_assert(HANDLE_SCALE > 0, "FX handle scale must be nonzero");
    static_assert(FxHandleEntrySizeIsValid<
                      ENTRY_TYPE, LIMIT, HANDLE_SCALE>(),
                  "FX entries must have an integral handle stride");
    return sizeof(ENTRY_TYPE) / HANDLE_SCALE;
}

template <typename ENTRY_TYPE, std::size_t LIMIT, std::size_t HANDLE_SCALE>
constexpr void FxValidateHandleCapacity() noexcept
{
    constexpr std::size_t stride =
        FxHandleStride<ENTRY_TYPE, LIMIT, HANDLE_SCALE>();
    static_assert((LIMIT - 1) <=
                      static_cast<std::size_t>(FX_INVALID_HANDLE - 1) / stride,
                  "FX handle array exceeds the 16-bit handle space");
}

template <typename ENTRY_TYPE, std::size_t LIMIT, std::size_t HANDLE_SCALE>
std::uint16_t FxEncodeHandle(
    const ENTRY_TYPE *entries,
    const void *entryAddress) noexcept
{
    FxValidateHandleCapacity<ENTRY_TYPE, LIMIT, HANDLE_SCALE>();
    if (!entries || !entryAddress)
        return FX_INVALID_HANDLE;

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(entries);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(entryAddress);
    if (address < base)
        return FX_INVALID_HANDLE;

    constexpr std::size_t span = LIMIT * sizeof(ENTRY_TYPE);
    const std::uintptr_t offset = address - base;
    if (offset >= span || offset % sizeof(ENTRY_TYPE) != 0)
        return FX_INVALID_HANDLE;

    const std::uintptr_t handle = offset / HANDLE_SCALE;
    if (handle >= FX_INVALID_HANDLE)
        return FX_INVALID_HANDLE;
    return static_cast<std::uint16_t>(handle);
}

template <typename ENTRY_TYPE, std::size_t LIMIT, std::size_t HANDLE_SCALE>
ENTRY_TYPE *FxDecodeHandle(ENTRY_TYPE *entries, std::uint32_t handle) noexcept
{
    FxValidateHandleCapacity<ENTRY_TYPE, LIMIT, HANDLE_SCALE>();
    constexpr std::size_t stride =
        FxHandleStride<ENTRY_TYPE, LIMIT, HANDLE_SCALE>();
    constexpr std::size_t handleLimit = LIMIT * stride;

    if (!entries || handle >= handleLimit || handle % stride != 0)
        return nullptr;
    return &entries[handle / stride];
}

template <typename ENTRY_TYPE, std::size_t LIMIT, std::size_t HANDLE_SCALE>
const ENTRY_TYPE *FxDecodeHandle(
    const ENTRY_TYPE *entries,
    std::uint32_t handle) noexcept
{
    return FxDecodeHandle<ENTRY_TYPE, LIMIT, HANDLE_SCALE>(
        const_cast<ENTRY_TYPE *>(entries), handle);
}

template <typename ITEM_TYPE, std::size_t LIMIT>
bool FxPoolItemIndex(
    const FxPool<ITEM_TYPE> *pool,
    const ITEM_TYPE *item,
    std::int32_t *outIndex) noexcept
{
    static_assert(FxPoolSlotLayoutIsCompatible<ITEM_TYPE>(),
                  "FX pool slots must preserve the legacy item stride");
    if (!pool || !item || !outIndex)
        return false;

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(pool);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(item);
    if (address < base)
        return false;

    constexpr std::size_t span = LIMIT * sizeof(FxPool<ITEM_TYPE>);
    const std::uintptr_t offset = address - base;
    if (offset >= span || offset % sizeof(FxPool<ITEM_TYPE>) != 0)
        return false;

    *outIndex = static_cast<std::int32_t>(
        offset / sizeof(FxPool<ITEM_TYPE>));
    return true;
}

template <std::size_t LIMIT>
constexpr bool FxPoolFreeIndexIsValid(std::int32_t index) noexcept
{
    return index == -1
        || static_cast<std::uint32_t>(index) < static_cast<std::uint32_t>(LIMIT);
}

template <typename ITEM_TYPE, std::size_t LIMIT>
FxPoolMutationStatus FxPoolRebuildAllocationStateLocked(
    const volatile std::int32_t *firstFreeIndex,
    const FxPool<ITEM_TYPE> *pool,
    volatile std::int32_t *activeCount,
    FxPoolAllocationState<LIMIT> *allocationState) noexcept
{
    static_assert(FxPoolSlotLayoutIsCompatible<ITEM_TYPE>(),
                  "FX pool slots must preserve the legacy item stride");
    if (!firstFreeIndex || !pool || !activeCount || !allocationState)
        return FxPoolMutationStatus::InvalidArgument;

    FxPoolAllocationState<LIMIT> rebuilt{};
    rebuilt.allocatedWords.fill((std::numeric_limits<std::uint64_t>::max)());
    if constexpr (LIMIT % FxPoolAllocationState<LIMIT>::WORD_BITS != 0)
    {
        constexpr std::size_t usedBits =
            LIMIT % FxPoolAllocationState<LIMIT>::WORD_BITS;
        rebuilt.allocatedWords.back() =
            (std::uint64_t{1} << usedBits) - 1;
    }
    rebuilt.allocatedCount = LIMIT;
    rebuilt.initialized = true;

    std::int32_t freeIndex = *firstFreeIndex;
    std::size_t freeCount = 0;
    while (freeIndex != -1)
    {
        if (!FxPoolFreeIndexIsValid<LIMIT>(freeIndex)
            || freeCount == LIMIT)
        {
            return FxPoolMutationStatus::CorruptFreeList;
        }

        const std::size_t index = static_cast<std::size_t>(freeIndex);
        if (!FxPoolAllocationStateIsAllocated(rebuilt, index))
            return FxPoolMutationStatus::CorruptFreeList;

        FxPoolSetAllocationStateBit(&rebuilt, index, false);
        --rebuilt.allocatedCount;
        ++freeCount;
        freeIndex = pool[index].nextFree;
    }

    *allocationState = rebuilt;
    Sys_AtomicStore(
        activeCount,
        static_cast<std::int32_t>(rebuilt.allocatedCount));
    return FxPoolMutationStatus::Success;
}

// These two operations are called while CRITSECT_FX_ALLOC is held. Every
// possible failure is checked before the free-list head, pool item, or active
// count is modified.
template <typename ITEM_TYPE, std::size_t LIMIT>
FxPool<ITEM_TYPE> *FxPoolAllocateLocked(
    volatile std::int32_t *firstFreeIndex,
    FxPool<ITEM_TYPE> *pool,
    volatile std::int32_t *activeCount,
    FxPoolAllocationState<LIMIT> *allocationState,
    FxPoolMutationStatus *outStatus = nullptr) noexcept
{
    static_assert(FxPoolSlotLayoutIsCompatible<ITEM_TYPE>(),
                  "FX pool slots must preserve the legacy item stride");
    auto setStatus = [outStatus](FxPoolMutationStatus status) {
        if (outStatus)
            *outStatus = status;
    };

    if (!firstFreeIndex || !pool || !activeCount || !allocationState)
    {
        setStatus(FxPoolMutationStatus::InvalidArgument);
        return nullptr;
    }
    if (!allocationState->initialized)
    {
        setStatus(FxPoolMutationStatus::UninitializedAllocationState);
        return nullptr;
    }

    const std::int32_t active = Sys_AtomicLoad(activeCount);
    if (active < 0 || static_cast<std::size_t>(active) > LIMIT)
    {
        setStatus(FxPoolMutationStatus::InvalidActiveCount);
        return nullptr;
    }
    if (allocationState->allocatedCount > LIMIT
        || static_cast<std::size_t>(active)
            != allocationState->allocatedCount)
    {
        setStatus(FxPoolMutationStatus::AllocationStateMismatch);
        return nullptr;
    }

    const std::int32_t itemIndex = *firstFreeIndex;
    if (itemIndex == -1)
    {
        setStatus(static_cast<std::size_t>(active) == LIMIT
                      ? FxPoolMutationStatus::Empty
                      : FxPoolMutationStatus::CorruptFreeList);
        return nullptr;
    }
    if (!FxPoolFreeIndexIsValid<LIMIT>(itemIndex))
    {
        setStatus(FxPoolMutationStatus::CorruptFreeList);
        return nullptr;
    }
    if (FxPoolAllocationStateIsAllocated(
            *allocationState, static_cast<std::size_t>(itemIndex)))
    {
        setStatus(FxPoolMutationStatus::CorruptFreeList);
        return nullptr;
    }

    FxPool<ITEM_TYPE> *const item = &pool[itemIndex];
    const std::int32_t nextFree = item->nextFree;
    if (!FxPoolFreeIndexIsValid<LIMIT>(nextFree) || nextFree == itemIndex)
    {
        setStatus(FxPoolMutationStatus::CorruptFreeList);
        return nullptr;
    }
    if (nextFree != -1
        && FxPoolAllocationStateIsAllocated(
            *allocationState, static_cast<std::size_t>(nextFree)))
    {
        setStatus(FxPoolMutationStatus::CorruptFreeList);
        return nullptr;
    }

    if (static_cast<std::size_t>(active) >= LIMIT)
    {
        setStatus(FxPoolMutationStatus::InvalidActiveCount);
        return nullptr;
    }

    FxPoolSetAllocationStateBit(
        allocationState, static_cast<std::size_t>(itemIndex), true);
    ++allocationState->allocatedCount;
    *firstFreeIndex = nextFree;
    Sys_AtomicIncrement(activeCount);
    setStatus(FxPoolMutationStatus::Success);
    return item;
}

template <typename ITEM_TYPE, std::size_t LIMIT>
FxPoolMutationStatus FxPoolCanFreeLocked(
    ITEM_TYPE *item,
    volatile std::int32_t *firstFreeIndex,
    FxPool<ITEM_TYPE> *pool,
    volatile std::int32_t *activeCount,
    FxPoolAllocationState<LIMIT> *allocationState,
    std::int32_t *outFreedIndex) noexcept
{
    static_assert(std::is_trivially_copyable_v<ITEM_TYPE>,
                  "FX pool items must be trivially copyable");
    if (!item || !firstFreeIndex || !pool || !activeCount
        || !allocationState || !outFreedIndex)
    {
        return FxPoolMutationStatus::InvalidArgument;
    }
    if (!allocationState->initialized)
        return FxPoolMutationStatus::UninitializedAllocationState;

    std::int32_t freedIndex = -1;
    if (!FxPoolItemIndex<ITEM_TYPE, LIMIT>(pool, item, &freedIndex))
        return FxPoolMutationStatus::InvalidArgument;

    const std::int32_t firstFree = *firstFreeIndex;
    if (!FxPoolFreeIndexIsValid<LIMIT>(firstFree))
        return FxPoolMutationStatus::CorruptFreeList;

    const std::int32_t active = Sys_AtomicLoad(activeCount);
    if (active <= 0 || static_cast<std::size_t>(active) > LIMIT)
        return FxPoolMutationStatus::InvalidActiveCount;
    if (allocationState->allocatedCount > LIMIT
        || static_cast<std::size_t>(active)
            != allocationState->allocatedCount)
    {
        return FxPoolMutationStatus::AllocationStateMismatch;
    }
    if (firstFree == freedIndex)
        return FxPoolMutationStatus::DuplicateFreeHead;
    if (firstFree != -1
        && FxPoolAllocationStateIsAllocated(
            *allocationState, static_cast<std::size_t>(firstFree)))
    {
        return FxPoolMutationStatus::CorruptFreeList;
    }
    if (!FxPoolAllocationStateIsAllocated(
            *allocationState, static_cast<std::size_t>(freedIndex)))
    {
        return FxPoolMutationStatus::AlreadyFree;
    }

    *outFreedIndex = freedIndex;
    return FxPoolMutationStatus::Success;
}

template <typename ITEM_TYPE, std::size_t LIMIT, typename BEFORE_PUBLISH>
FxPoolMutationStatus FxPoolFreeLocked(
    ITEM_TYPE *item,
    volatile std::int32_t *firstFreeIndex,
    FxPool<ITEM_TYPE> *pool,
    volatile std::int32_t *activeCount,
    FxPoolAllocationState<LIMIT> *allocationState,
    BEFORE_PUBLISH &&beforePublish) noexcept
{
    static_assert(std::is_nothrow_invocable_v<BEFORE_PUBLISH>,
                  "FX pool pre-publish unlink must not throw");
    std::int32_t freedIndex = -1;
    const FxPoolMutationStatus status =
        FxPoolCanFreeLocked<ITEM_TYPE, LIMIT>(
            item,
            firstFreeIndex,
            pool,
            activeCount,
            allocationState,
            &freedIndex);
    if (status != FxPoolMutationStatus::Success)
        return status;

    const std::int32_t firstFree = *firstFreeIndex;
    std::forward<BEFORE_PUBLISH>(beforePublish)();
    pool[freedIndex].item = ITEM_TYPE{};
    pool[freedIndex].nextFree = firstFree;
    FxPoolSetAllocationStateBit(
        allocationState, static_cast<std::size_t>(freedIndex), false);
    --allocationState->allocatedCount;
    *firstFreeIndex = freedIndex;
    Sys_AtomicDecrement(activeCount);
    return FxPoolMutationStatus::Success;
}

template <typename ITEM_TYPE, std::size_t LIMIT>
FxPoolMutationStatus FxPoolFreeLocked(
    ITEM_TYPE *item,
    volatile std::int32_t *firstFreeIndex,
    FxPool<ITEM_TYPE> *pool,
    volatile std::int32_t *activeCount,
    FxPoolAllocationState<LIMIT> *allocationState) noexcept
{
    return FxPoolFreeLocked<ITEM_TYPE, LIMIT>(
        item,
        firstFreeIndex,
        pool,
        activeCount,
        allocationState,
        []() noexcept {});
}

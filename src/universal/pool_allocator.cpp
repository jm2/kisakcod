#include "pool_allocator.h"

#include "assertive.h"

#include <cstring>
#include <limits>

namespace
{
constexpr std::uint32_t POOL_CONTROL_INIT_MAGIC = 0x504F4F4Cu;

struct PoolRange
{
    std::uintptr_t begin;
    std::uintptr_t end;
};

struct PoolDescriptorRanges
{
    PoolRange items;
    PoolRange control;
    PoolRange slotState;
};

struct PoolFastState
{
    std::size_t activeCount;
    poolslotstate_t headIndex;
    poolslotstate_t nextIndex;
    void *head;
    void *next;
};

bool Pool_Reject(
    const int line,
    const char *const expression,
    const bool reportFailure = true) noexcept
{
    if (reportFailure)
        MyAssertHandler(__FILE__, line, 0, "%s", expression);
    return false;
}

#define POOL_REJECT(expression) Pool_Reject(__LINE__, expression)

bool Pool_TryMakeRange(
    const void *const address,
    const std::size_t byteCount,
    const char *const overflowExpression,
    PoolRange *const outRange,
    const bool reportFailure = true) noexcept
{
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    if (begin
        > (std::numeric_limits<std::uintptr_t>::max)() - byteCount)
    {
        return Pool_Reject(__LINE__, overflowExpression, reportFailure);
    }

    outRange->begin = begin;
    outRange->end = begin + byteCount;
    return true;
}

bool Pool_RangesAreSeparate(
    const PoolRange first,
    const PoolRange second,
    const char *const expression,
    const bool reportFailure = true) noexcept
{
    if (first.begin < second.end && second.begin < first.end)
        return Pool_Reject(__LINE__, expression, reportFailure);
    return true;
}

bool Pool_StorageIsValid(
    const poolstorage_t storage,
    PoolDescriptorRanges *const outRanges = nullptr,
    const bool reportFailure = true) noexcept
{
    if (!storage.base)
        return Pool_Reject(__LINE__, "storage.base", reportFailure);
    if (storage.itemSize < sizeof(void *))
        return Pool_Reject(
            __LINE__, "storage.itemSize >= sizeof(void *)", reportFailure);
    if (storage.itemCount < 2)
        return Pool_Reject(__LINE__, "storage.itemCount >= 2", reportFailure);

    constexpr std::size_t maxActiveCount =
        static_cast<std::size_t>((std::numeric_limits<int>::max)());
    if (storage.itemCount > maxActiveCount)
        return Pool_Reject(
            __LINE__, "storage.itemCount <= INT_MAX", reportFailure);
    if (storage.itemSize
        > (std::numeric_limits<std::size_t>::max)() / storage.itemCount)
    {
        return Pool_Reject(
            __LINE__,
            "storage.itemSize * storage.itemCount does not overflow",
            reportFailure);
    }

    if (!storage.control)
        return Pool_Reject(__LINE__, "storage.control", reportFailure);
    if (reinterpret_cast<std::uintptr_t>(storage.control)
        % alignof(poolcontrol_t) != 0)
    {
        return Pool_Reject(
            __LINE__, "storage.control is aligned", reportFailure);
    }

    PoolDescriptorRanges ranges{};
    if (!Pool_TryMakeRange(
            storage.control,
            sizeof(*storage.control),
            "control address range does not wrap",
            &ranges.control,
            reportFailure))
    {
        return false;
    }

    if (!storage.control->slotState)
        return Pool_Reject(
            __LINE__, "storage.control->slotState", reportFailure);
    if (storage.control->slotStateCount != storage.itemCount)
    {
        return Pool_Reject(
            __LINE__,
            "storage.control->slotStateCount == storage.itemCount",
            reportFailure);
    }
    if (storage.control->slotStateCount
        > (std::numeric_limits<std::size_t>::max)()
            / sizeof(*storage.control->slotState))
    {
        return Pool_Reject(
            __LINE__,
            "slot-state byte count does not overflow",
            reportFailure);
    }
    if (reinterpret_cast<std::uintptr_t>(storage.control->slotState)
        % alignof(poolslotstate_t) != 0)
    {
        return Pool_Reject(
            __LINE__,
            "storage.control->slotState is aligned",
            reportFailure);
    }

    if (!Pool_TryMakeRange(
            storage.base,
            storage.itemSize * storage.itemCount,
            "storage address range does not wrap",
            &ranges.items,
            reportFailure)
        || !Pool_TryMakeRange(
            storage.control->slotState,
            sizeof(*storage.control->slotState)
                * storage.control->slotStateCount,
            "slot-state address range does not wrap",
            &ranges.slotState,
            reportFailure))
    {
        return false;
    }

    if (!Pool_RangesAreSeparate(
            ranges.items,
            ranges.control,
            "pool control does not overlap pool storage",
            reportFailure)
        || !Pool_RangesAreSeparate(
            ranges.items,
            ranges.slotState,
            "slot-state table does not overlap pool storage",
            reportFailure)
        || !Pool_RangesAreSeparate(
            ranges.control,
            ranges.slotState,
            "slot-state table does not overlap pool control",
            reportFailure))
    {
        return false;
    }

    if (outRanges)
        *outRanges = ranges;
    return true;
}

bool Pool_MetadataIsSeparate(
    const pooldata_t *const pooldata,
    const PoolDescriptorRanges &ranges,
    const bool reportFailure = true) noexcept
{
    if (!pooldata)
        return Pool_Reject(__LINE__, "pooldata", reportFailure);
    if (reinterpret_cast<std::uintptr_t>(pooldata)
        % alignof(pooldata_t) != 0)
    {
        return Pool_Reject(__LINE__, "pooldata is aligned", reportFailure);
    }

    PoolRange metadata{};
    if (!Pool_TryMakeRange(
            pooldata,
            sizeof(*pooldata),
            "pooldata address range does not wrap",
            &metadata,
            reportFailure))
    {
        return false;
    }

    return Pool_RangesAreSeparate(
               metadata,
               ranges.items,
               "pooldata does not overlap pool storage",
               reportFailure)
        && Pool_RangesAreSeparate(
               metadata,
               ranges.control,
               "pooldata does not overlap pool control",
               reportFailure)
        && Pool_RangesAreSeparate(
               metadata,
               ranges.slotState,
               "pooldata does not overlap slot-state storage",
               reportFailure);
}

bool Pool_TryGetSlotIndex(
    const poolstorage_t storage,
    const void *const item,
    std::size_t *const outIndex) noexcept
{
    if (!item || !outIndex)
        return false;

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage.base);
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(item);
    const std::size_t byteCount = storage.itemSize * storage.itemCount;
    const std::uintptr_t end = base + byteCount;
    if (address < base || address >= end)
        return false;

    const std::uintptr_t offset = address - base;
    if (offset % storage.itemSize != 0)
        return false;

    const std::size_t index = static_cast<std::size_t>(offset / storage.itemSize);
    if (index >= storage.itemCount)
        return false;

    *outIndex = index;
    return true;
}

void *Pool_SlotAt(const poolstorage_t storage, const std::size_t index) noexcept
{
    return static_cast<void *>(
        static_cast<unsigned char *>(storage.base) + storage.itemSize * index);
}

void Pool_WriteNext(void *const item, void *const next) noexcept
{
    std::memcpy(item, &next, sizeof(next));
}

bool Pool_LinkMatches(
    const void *const item,
    void *const expectedNext) noexcept
{
    // Never materialize corruption-controlled bytes as a pointer. The shadow
    // table supplies the trusted canonical pointer; compare representations.
    return std::memcmp(item, &expectedNext, sizeof(expectedNext)) == 0;
}

bool Pool_ControlBindingIsValid(
    const poolstorage_t storage,
    const pooldata_t *const pooldata,
    const bool reportFailure = true) noexcept
{
    const poolcontrol_t &control = *storage.control;
    if (control.initMagic != POOL_CONTROL_INIT_MAGIC)
        return Pool_Reject(
            __LINE__, "pool control is initialized", reportFailure);
    if (control.boundBase != storage.base
        || control.boundData != pooldata
        || control.boundStride != storage.itemSize
        || control.boundCount != storage.itemCount)
    {
        return Pool_Reject(
            __LINE__,
            "pool control binding matches its descriptor",
            reportFailure);
    }
    return true;
}

bool Pool_TryDecodeNext(
    const poolstorage_t storage,
    const poolslotstate_t nextIndex,
    void **const outNext,
    const bool reportFailure = true) noexcept
{
    if (nextIndex == POOL_SLOT_END)
    {
        *outNext = nullptr;
        return true;
    }
    if (nextIndex >= storage.itemCount)
        return Pool_Reject(
            __LINE__, "slot-state next index is in range", reportFailure);

    *outNext = Pool_SlotAt(storage, nextIndex);
    return true;
}

bool Pool_FastStateIsValid(
    const poolstorage_t storage,
    const pooldata_t *const pooldata,
    PoolFastState *const outState,
    const bool reportFailure = true) noexcept
{
    PoolDescriptorRanges ranges{};
    if (!Pool_StorageIsValid(storage, &ranges, reportFailure)
        || !Pool_MetadataIsSeparate(pooldata, ranges, reportFailure)
        || !Pool_ControlBindingIsValid(storage, pooldata, reportFailure))
    {
        return false;
    }

    const poolcontrol_t &control = *storage.control;
    if (pooldata->activeCount < 0)
        return Pool_Reject(
            __LINE__, "pooldata->activeCount >= 0", reportFailure);
    if (control.activeCount != pooldata->activeCount)
        return Pool_Reject(
            __LINE__,
            "control.activeCount == pooldata->activeCount",
            reportFailure);

    const std::size_t activeCount =
        static_cast<std::size_t>(pooldata->activeCount);
    if (activeCount > storage.itemCount)
        return Pool_Reject(
            __LINE__, "activeCount <= storage.itemCount", reportFailure);

    PoolFastState state{
        activeCount,
        control.headIndex,
        POOL_SLOT_END,
        pooldata->firstFree,
        nullptr,
    };
    if (activeCount == storage.itemCount)
    {
        if (pooldata->firstFree)
            return Pool_Reject(
                __LINE__,
                "a full pool requires a null free-list head",
                reportFailure);
        if (control.headIndex != POOL_SLOT_END)
            return Pool_Reject(
                __LINE__,
                "a full pool requires an end control head",
                reportFailure);
        if (outState)
            *outState = state;
        return true;
    }

    std::size_t headIndex = 0;
    if (!Pool_TryGetSlotIndex(storage, pooldata->firstFree, &headIndex))
        return Pool_Reject(
            __LINE__,
            "free-list head is an exact pool slot",
            reportFailure);
    if (headIndex != control.headIndex)
        return Pool_Reject(
            __LINE__,
            "free-list head matches the control head",
            reportFailure);

    const poolslotstate_t nextIndex = control.slotState[headIndex];
    if (nextIndex == POOL_SLOT_ALLOCATED)
        return Pool_Reject(
            __LINE__,
            "free-list head is not marked allocated",
            reportFailure);

    void *expectedNext = nullptr;
    if (!Pool_TryDecodeNext(
            storage, nextIndex, &expectedNext, reportFailure))
        return false;
    if (!Pool_LinkMatches(pooldata->firstFree, expectedNext))
        return Pool_Reject(
            __LINE__,
            "free-list head link matches slot-state control",
            reportFailure);

    const bool allocationWouldFill = activeCount + 1 == storage.itemCount;
    if (allocationWouldFill != (nextIndex == POOL_SLOT_END))
        return Pool_Reject(
            __LINE__,
            "free-list end matches inactive slot count",
            reportFailure);
    if (nextIndex != POOL_SLOT_END)
    {
        if (nextIndex == headIndex)
            return Pool_Reject(
                __LINE__,
                "free-list head does not link to itself",
                reportFailure);
        if (control.slotState[nextIndex] == POOL_SLOT_ALLOCATED)
            return Pool_Reject(
                __LINE__,
                "free-list next slot is not marked allocated",
                reportFailure);
    }

    state.headIndex = static_cast<poolslotstate_t>(headIndex);
    state.nextIndex = nextIndex;
    state.head = pooldata->firstFree;
    state.next = expectedNext;
    if (outState)
        *outState = state;
    return true;
}
} // namespace

bool __cdecl Pool_Init(
    const poolstorage_t storage,
    pooldata_t *const pooldata) noexcept
{
    if (!pooldata)
        return POOL_REJECT("pooldata");

    PoolDescriptorRanges ranges{};
    if (!Pool_StorageIsValid(storage, &ranges))
        return false;
    if (!Pool_MetadataIsSeparate(pooldata, ranges))
        return false;

    poolcontrol_t &control = *storage.control;
    if (control.initMagic == POOL_CONTROL_INIT_MAGIC
        && (control.boundBase != storage.base
            || control.boundData != pooldata
            || control.boundStride != storage.itemSize
            || control.boundCount != storage.itemCount))
    {
        return POOL_REJECT("initialized control is not rebound to another pool");
    }

    control.initMagic = 0;
    control.boundBase = nullptr;
    control.boundData = nullptr;
    control.boundStride = 0;
    control.boundCount = 0;
    control.headIndex = POOL_SLOT_END;
    control.activeCount = 0;
    pooldata->firstFree = nullptr;
    pooldata->activeCount = 0;

    for (std::size_t index = 0; index < storage.itemCount; ++index)
    {
        const poolslotstate_t nextIndex = index + 1 < storage.itemCount
            ? static_cast<poolslotstate_t>(index + 1)
            : POOL_SLOT_END;
        void *const item = Pool_SlotAt(storage, index);
        void *next = nullptr;
        if (nextIndex != POOL_SLOT_END)
            next = Pool_SlotAt(storage, nextIndex);
        Pool_WriteNext(item, next);
        control.slotState[index] = nextIndex;
    }

    control.boundBase = storage.base;
    control.boundData = pooldata;
    control.boundStride = storage.itemSize;
    control.boundCount = storage.itemCount;
    control.headIndex = 0;
    control.activeCount = 0;
    pooldata->firstFree = storage.base;
    pooldata->activeCount = 0;
    control.initMagic = POOL_CONTROL_INIT_MAGIC;
    return true;
}

bool __cdecl Pool_Invalidate(
    const poolstorage_t storage,
    pooldata_t *const pooldata) noexcept
{
    if (!pooldata)
        return POOL_REJECT("pooldata");

    PoolDescriptorRanges ranges{};
    if (!Pool_StorageIsValid(storage, &ranges)
        || !Pool_MetadataIsSeparate(pooldata, ranges))
    {
        return false;
    }

    poolcontrol_t &control = *storage.control;
    if (control.initMagic == POOL_CONTROL_INIT_MAGIC
        && (control.boundBase != storage.base
            || control.boundData != pooldata
            || control.boundStride != storage.itemSize
            || control.boundCount != storage.itemCount))
    {
        return POOL_REJECT("only the bound pool can invalidate its control");
    }

    control.initMagic = 0;
    pooldata->firstFree = nullptr;
    pooldata->activeCount = 0;
    control.headIndex = POOL_SLOT_END;
    control.activeCount = 0;
    control.boundBase = nullptr;
    control.boundData = nullptr;
    control.boundStride = 0;
    control.boundCount = 0;
    return true;
}

void *__cdecl Pool_Alloc(
    const poolstorage_t storage,
    pooldata_t *const pooldata) noexcept
{
    PoolFastState state{};
    if (!Pool_FastStateIsValid(storage, pooldata, &state))
        return nullptr;
    if (state.activeCount == storage.itemCount)
        return nullptr;

    poolcontrol_t &control = *storage.control;
    control.slotState[state.headIndex] = POOL_SLOT_ALLOCATED;
    control.headIndex = state.nextIndex;
    ++control.activeCount;
    pooldata->firstFree = state.next;
    ++pooldata->activeCount;
    return state.head;
}

bool __cdecl Pool_Free(
    const poolstorage_t storage,
    pooldata_t *const pooldata,
    void *const item) noexcept
{
    if (!item)
        return POOL_REJECT("item");

    PoolFastState state{};
    if (!Pool_FastStateIsValid(storage, pooldata, &state))
        return false;
    if (state.activeCount == 0)
        return POOL_REJECT("pooldata->activeCount > 0");

    std::size_t itemIndex = 0;
    if (!Pool_TryGetSlotIndex(storage, item, &itemIndex))
        return POOL_REJECT("item is an exact pool slot");

    poolcontrol_t &control = *storage.control;
    if (control.slotState[itemIndex] != POOL_SLOT_ALLOCATED)
        return POOL_REJECT("item is currently allocated");

    Pool_WriteNext(item, state.head);
    control.slotState[itemIndex] = state.headIndex;
    control.headIndex = static_cast<poolslotstate_t>(itemIndex);
    --control.activeCount;
    pooldata->firstFree = item;
    --pooldata->activeCount;
    return true;
}

poolallocresult_t __cdecl Pool_TryAllocNoReport(
    const poolstorage_t storage,
    pooldata_t *const pooldata) noexcept
{
    PoolFastState state{};
    if (!Pool_FastStateIsValid(storage, pooldata, &state, false))
        return {poolmutationstatus_t::InvalidState, nullptr};
    if (state.activeCount == storage.itemCount)
        return {poolmutationstatus_t::Unavailable, nullptr};

    poolcontrol_t &control = *storage.control;
    control.slotState[state.headIndex] = POOL_SLOT_ALLOCATED;
    control.headIndex = state.nextIndex;
    ++control.activeCount;
    pooldata->firstFree = state.next;
    ++pooldata->activeCount;
    return {poolmutationstatus_t::Success, state.head};
}

poolmutationstatus_t __cdecl Pool_TryFreeNoReport(
    const poolstorage_t storage,
    pooldata_t *const pooldata,
    void *const item) noexcept
{
    if (!item)
        return poolmutationstatus_t::InvalidState;

    PoolFastState state{};
    if (!Pool_FastStateIsValid(storage, pooldata, &state, false)
        || state.activeCount == 0)
    {
        return poolmutationstatus_t::InvalidState;
    }

    std::size_t itemIndex = 0;
    if (!Pool_TryGetSlotIndex(storage, item, &itemIndex))
        return poolmutationstatus_t::InvalidState;

    poolcontrol_t &control = *storage.control;
    if (control.slotState[itemIndex] != POOL_SLOT_ALLOCATED)
        return poolmutationstatus_t::InvalidState;

    Pool_WriteNext(item, state.head);
    control.slotState[itemIndex] = state.headIndex;
    control.headIndex = static_cast<poolslotstate_t>(itemIndex);
    --control.activeCount;
    pooldata->firstFree = item;
    --pooldata->activeCount;
    return poolmutationstatus_t::Success;
}

poolmutationstatus_t __cdecl Pool_TryValidateAllocatedNoReport(
    const poolstorage_t storage,
    const pooldata_t *const pooldata,
    const void *const item) noexcept
{
    if (!item)
        return poolmutationstatus_t::InvalidState;

    PoolFastState state{};
    if (!Pool_FastStateIsValid(storage, pooldata, &state, false)
        || state.activeCount == 0)
    {
        return poolmutationstatus_t::InvalidState;
    }

    std::size_t itemIndex = 0;
    if (!Pool_TryGetSlotIndex(storage, item, &itemIndex)
        || storage.control->slotState[itemIndex] != POOL_SLOT_ALLOCATED)
    {
        return poolmutationstatus_t::InvalidState;
    }
    return poolmutationstatus_t::Success;
}

static bool Pool_ValidateFullImpl(
    const poolstorage_t storage,
    const pooldata_t *const pooldata,
    const bool reportFailure) noexcept
{
    PoolFastState state{};
    if (!Pool_FastStateIsValid(
            storage, pooldata, &state, reportFailure))
        return false;

    const poolcontrol_t &control = *storage.control;
    std::size_t allocatedCount = 0;
    for (std::size_t index = 0; index < storage.itemCount; ++index)
    {
        const poolslotstate_t slotState = control.slotState[index];
        if (slotState == POOL_SLOT_ALLOCATED)
        {
            ++allocatedCount;
            continue;
        }
        if (slotState != POOL_SLOT_END && slotState >= storage.itemCount)
            return Pool_Reject(
                __LINE__, "every slot-state value is valid", reportFailure);
    }
    if (allocatedCount != state.activeCount)
        return Pool_Reject(
            __LINE__,
            "allocated slot-state count matches activeCount",
            reportFailure);

    const std::size_t expectedFreeCount = storage.itemCount - state.activeCount;
    poolslotstate_t index = control.headIndex;
    for (std::size_t visited = 0; visited < expectedFreeCount; ++visited)
    {
        if (index == POOL_SLOT_END || index >= storage.itemCount)
            return Pool_Reject(
                __LINE__,
                "free-list contains every inactive slot",
                reportFailure);

        const poolslotstate_t nextIndex = control.slotState[index];
        if (nextIndex == POOL_SLOT_ALLOCATED)
            return Pool_Reject(
                __LINE__,
                "free-list node is not marked allocated",
                reportFailure);

        void *expectedNext = nullptr;
        if (!Pool_TryDecodeNext(
                storage, nextIndex, &expectedNext, reportFailure))
            return false;
        if (!Pool_LinkMatches(Pool_SlotAt(storage, index), expectedNext))
        {
            return Pool_Reject(
                __LINE__,
                "every free-list link matches slot-state control",
                reportFailure);
        }
        index = nextIndex;
    }

    if (index != POOL_SLOT_END)
        return Pool_Reject(
            __LINE__,
            "free-list length matches inactive slot count",
            reportFailure);
    return true;
}

bool __cdecl Pool_ValidateFull(
    const poolstorage_t storage,
    const pooldata_t *const pooldata) noexcept
{
    return Pool_ValidateFullImpl(storage, pooldata, true);
}

poolmutationstatus_t __cdecl Pool_TryValidateFullNoReport(
    const poolstorage_t storage,
    const pooldata_t *const pooldata) noexcept
{
    return Pool_ValidateFullImpl(storage, pooldata, false)
        ? poolmutationstatus_t::Success
        : poolmutationstatus_t::InvalidState;
}

poolcountresult_t __cdecl Pool_GetFreeCount(
    const poolstorage_t storage,
    const pooldata_t *const pooldata) noexcept
{
    PoolFastState state{};
    if (!Pool_FastStateIsValid(storage, pooldata, &state))
        return {false, 0};

    return {true, storage.itemCount - state.activeCount};
}

poolnextresult_t __cdecl Pool_NextFree(
    const poolstorage_t storage,
    const void *const item) noexcept
{
    PoolDescriptorRanges ranges{};
    if (!Pool_StorageIsValid(storage, &ranges))
        return {false, nullptr};

    const pooldata_t *const pooldata = storage.control->boundData;
    PoolFastState state{};
    if (!Pool_FastStateIsValid(storage, pooldata, &state))
        return {false, nullptr};

    std::size_t itemIndex = 0;
    if (!Pool_TryGetSlotIndex(storage, item, &itemIndex))
    {
        POOL_REJECT("item is an exact pool slot");
        return {false, nullptr};
    }

    const poolslotstate_t nextIndex = storage.control->slotState[itemIndex];
    if (nextIndex == POOL_SLOT_ALLOCATED)
    {
        POOL_REJECT("item is not marked allocated");
        return {false, nullptr};
    }
    if (nextIndex == itemIndex)
    {
        POOL_REJECT("free-list item does not link to itself");
        return {false, nullptr};
    }

    void *expectedNext = nullptr;
    if (!Pool_TryDecodeNext(storage, nextIndex, &expectedNext))
        return {false, nullptr};
    if (!Pool_LinkMatches(item, expectedNext))
    {
        POOL_REJECT("free-list link matches slot-state control");
        return {false, nullptr};
    }
    if (nextIndex != POOL_SLOT_END
        && storage.control->slotState[nextIndex] == POOL_SLOT_ALLOCATED)
    {
        POOL_REJECT("free-list next slot is not marked allocated");
        return {false, nullptr};
    }

    return {true, expectedNext};
}

poolindexresult_t __cdecl Pool_GetSlotIndex(
    const poolstorage_t storage,
    const void *const item) noexcept
{
    if (!Pool_StorageIsValid(storage))
        return {false, 0};

    std::size_t index = 0;
    if (!Pool_TryGetSlotIndex(storage, item, &index))
    {
        POOL_REJECT("item is an exact pool slot");
        return {false, 0};
    }
    return {true, index};
}

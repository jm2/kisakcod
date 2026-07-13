#include "pool_allocator.h"

#include "assertive.h"

#include <cstring>
#include <limits>

namespace
{
bool Pool_Reject(const int line, const char *const expression) noexcept
{
    MyAssertHandler(__FILE__, line, 0, "%s", expression);
    return false;
}

#define POOL_REJECT(expression) Pool_Reject(__LINE__, expression)

bool Pool_StorageIsValid(const poolstorage_t storage) noexcept
{
    if (!storage.base)
        return POOL_REJECT("storage.base");
    if (storage.itemSize < sizeof(void *))
        return POOL_REJECT("storage.itemSize >= sizeof(void *)");
    if (storage.itemCount < 2)
        return POOL_REJECT("storage.itemCount >= 2");

    constexpr std::size_t maxActiveCount =
        static_cast<std::size_t>((std::numeric_limits<int>::max)());
    if (storage.itemCount > maxActiveCount)
        return POOL_REJECT("storage.itemCount <= INT_MAX");
    if (storage.itemSize
        > (std::numeric_limits<std::size_t>::max)() / storage.itemCount)
    {
        return POOL_REJECT("storage.itemSize * storage.itemCount does not overflow");
    }

    const std::size_t byteCount = storage.itemSize * storage.itemCount;
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(storage.base);
    if (base > (std::numeric_limits<std::uintptr_t>::max)() - byteCount)
        return POOL_REJECT("storage address range does not wrap");

    return true;
}

bool Pool_MetadataIsSeparate(
    const poolstorage_t storage,
    const pooldata_t *const pooldata) noexcept
{
    const std::uintptr_t storageBegin =
        reinterpret_cast<std::uintptr_t>(storage.base);
    const std::uintptr_t storageEnd =
        storageBegin + storage.itemSize * storage.itemCount;
    const std::uintptr_t metadataBegin =
        reinterpret_cast<std::uintptr_t>(pooldata);
    if (metadataBegin
        > (std::numeric_limits<std::uintptr_t>::max)() - sizeof(*pooldata))
    {
        return POOL_REJECT("pooldata address range does not wrap");
    }

    const std::uintptr_t metadataEnd = metadataBegin + sizeof(*pooldata);
    if (metadataBegin < storageEnd && storageBegin < metadataEnd)
        return POOL_REJECT("pooldata does not overlap pool storage");

    return true;
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

void *Pool_ReadNext(const void *const item) noexcept
{
    void *next = nullptr;
    std::memcpy(&next, item, sizeof(next));
    return next;
}

bool Pool_StateIsValid(
    const poolstorage_t storage,
    const pooldata_t *const pooldata,
    const void *const itemBeingFreed = nullptr) noexcept
{
    if (!pooldata)
        return POOL_REJECT("pooldata");
    if (!Pool_StorageIsValid(storage))
        return false;
    if (!Pool_MetadataIsSeparate(storage, pooldata))
        return false;
    if (itemBeingFreed)
    {
        std::size_t itemIndex = 0;
        if (!Pool_TryGetSlotIndex(storage, itemBeingFreed, &itemIndex))
            return POOL_REJECT("item is an exact pool slot");
    }
    if (pooldata->activeCount < 0)
        return POOL_REJECT("pooldata->activeCount >= 0");

    const std::size_t activeCount =
        static_cast<std::size_t>(pooldata->activeCount);
    if (activeCount > storage.itemCount)
        return POOL_REJECT("activeCount <= storage.itemCount");

    const std::size_t expectedFreeCount = storage.itemCount - activeCount;
    void *item = pooldata->firstFree;
    for (std::size_t visited = 0; visited < expectedFreeCount; ++visited)
    {
        std::size_t index = 0;
        if (!Pool_TryGetSlotIndex(storage, item, &index))
            return POOL_REJECT("free-list node is an exact pool slot");
        if (item == itemBeingFreed)
            return POOL_REJECT("item is not already free");

        item = Pool_ReadNext(item);
    }

    if (item)
        return POOL_REJECT("free-list length matches inactive slot count");

    return true;
}
} // namespace

bool __cdecl Pool_Init(
    const poolstorage_t storage,
    pooldata_t *const pooldata) noexcept
{
    if (!pooldata)
        return POOL_REJECT("pooldata");

    if (!Pool_StorageIsValid(storage))
    {
        // A usable extent cannot be established, so fail closed by retiring
        // stale metadata without walking or publishing the described storage.
        pooldata->firstFree = nullptr;
        pooldata->activeCount = 0;
        return false;
    }
    if (!Pool_MetadataIsSeparate(storage, pooldata))
        return false;

    pooldata->firstFree = nullptr;
    pooldata->activeCount = 0;

    for (std::size_t index = 0; index < storage.itemCount; ++index)
    {
        void *const item = Pool_SlotAt(storage, index);
        void *const next = index + 1 < storage.itemCount
            ? Pool_SlotAt(storage, index + 1)
            : nullptr;
        Pool_WriteNext(item, next);
    }

    pooldata->firstFree = storage.base;
    return true;
}

void *__cdecl Pool_Alloc(
    const poolstorage_t storage,
    pooldata_t *const pooldata) noexcept
{
    if (!Pool_StateIsValid(storage, pooldata))
        return nullptr;
    if (!pooldata->firstFree)
        return nullptr;
    if (pooldata->activeCount == (std::numeric_limits<int>::max)())
    {
        POOL_REJECT("pooldata->activeCount < INT_MAX");
        return nullptr;
    }

    void *const item = pooldata->firstFree;
    void *const next = Pool_ReadNext(item);
    pooldata->firstFree = next;
    ++pooldata->activeCount;
    return item;
}

bool __cdecl Pool_Free(
    const poolstorage_t storage,
    pooldata_t *const pooldata,
    void *const item) noexcept
{
    if (!item)
        return POOL_REJECT("item");
    if (!pooldata)
        return POOL_REJECT("pooldata");
    if (pooldata->activeCount <= 0)
        return POOL_REJECT("pooldata->activeCount > 0");
    if (!Pool_StateIsValid(storage, pooldata, item))
        return false;

    Pool_WriteNext(item, pooldata->firstFree);
    pooldata->firstFree = item;
    --pooldata->activeCount;
    return true;
}

poolcountresult_t __cdecl Pool_GetFreeCount(
    const poolstorage_t storage,
    const pooldata_t *const pooldata) noexcept
{
    if (!Pool_StateIsValid(storage, pooldata))
        return {false, 0};

    return {
        true,
        storage.itemCount - static_cast<std::size_t>(pooldata->activeCount),
    };
}

poolnextresult_t __cdecl Pool_NextFree(
    const poolstorage_t storage,
    const void *const item) noexcept
{
    if (!Pool_StorageIsValid(storage))
        return {false, nullptr};

    std::size_t itemIndex = 0;
    if (!Pool_TryGetSlotIndex(storage, item, &itemIndex))
    {
        POOL_REJECT("item is an exact pool slot");
        return {false, nullptr};
    }

    void *const next = Pool_ReadNext(item);
    if (next)
    {
        std::size_t nextIndex = 0;
        if (!Pool_TryGetSlotIndex(storage, next, &nextIndex))
        {
            POOL_REJECT("next is an exact pool slot");
            return {false, nullptr};
        }
    }

    return {true, next};
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

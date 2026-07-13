#pragma once

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

struct pooldata_t
{
    void *firstFree;
    int activeCount;
};

RUNTIME_SIZE(pooldata_t, 0x8, 0x10);
RUNTIME_OFFSET(pooldata_t, activeCount, 0x4, 0x8);

// Kept as an opaque compatibility type for callers that name the retail pool
// node. Pool links are raw bytes in arbitrary object storage; the allocator
// never forms or dereferences a freenode object.
struct freenode
{
    freenode *next;
};

RUNTIME_SIZE(freenode, 0x4, 0x8);

// The retail metadata intentionally remains two fields so its x86 ABI is
// unchanged. Supplying the backing extent to each operation lets the portable
// implementation validate exact slot membership and bound every free-list walk
// without a hidden global registry.
struct poolstorage_t
{
    void *base;
    std::size_t itemSize;
    std::size_t itemCount;
};

RUNTIME_SIZE(poolstorage_t, 0xC, 0x18);
RUNTIME_OFFSET(poolstorage_t, itemSize, 0x4, 0x8);
RUNTIME_OFFSET(poolstorage_t, itemCount, 0x8, 0x10);

struct poolcountresult_t
{
    bool valid;
    std::size_t count;
};

struct poolnextresult_t
{
    bool valid;
    void *next;
};

struct poolindexresult_t
{
    bool valid;
    std::size_t index;
};

inline poolstorage_t Pool_StorageFor(
    void *const base,
    const std::size_t itemSize,
    const std::size_t itemCount) noexcept
{
    return {base, itemSize, itemCount};
}

template <typename T, std::size_t N>
inline poolstorage_t Pool_StorageFor(T (&items)[N]) noexcept
{
    return {static_cast<void *>(&items), sizeof(T), N};
}

bool __cdecl Pool_Init(poolstorage_t storage, pooldata_t *pooldata) noexcept;
void *__cdecl Pool_Alloc(poolstorage_t storage, pooldata_t *pooldata) noexcept;
bool __cdecl Pool_Free(
    poolstorage_t storage,
    pooldata_t *pooldata,
    void *item) noexcept;
[[nodiscard]] poolcountresult_t __cdecl Pool_GetFreeCount(
    poolstorage_t storage,
    const pooldata_t *pooldata) noexcept;
[[nodiscard]] poolnextresult_t __cdecl Pool_NextFree(
    poolstorage_t storage,
    const void *item) noexcept;
[[nodiscard]] poolindexresult_t __cdecl Pool_GetSlotIndex(
    poolstorage_t storage,
    const void *item) noexcept;

#define INIT_STATIC_POOL(array, pooldata) \
    Pool_Init(Pool_StorageFor(array), pooldata)

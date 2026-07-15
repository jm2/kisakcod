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

using poolslotstate_t = std::uint32_t;

// Slot-state values are external to the retail objects. Every other value is
// the index of the next free slot. This makes ownership and the canonical next
// edge available in O(1) without consuming payload bytes from allocated slots.
constexpr poolslotstate_t POOL_SLOT_END = UINT32_MAX - 1u;
constexpr poolslotstate_t POOL_SLOT_ALLOCATED = UINT32_MAX;

// Mutable control storage is deliberately kept beside, rather than inside,
// the ABI-frozen retail globals. The binding fields prevent a descriptor from
// accidentally pairing one pool's control table with another pool.
struct poolcontrol_t
{
    poolslotstate_t *slotState;
    std::size_t slotStateCount;
    const void *boundBase;
    const pooldata_t *boundData;
    std::size_t boundStride;
    std::size_t boundCount;
    poolslotstate_t headIndex;
    int activeCount;
    std::uint32_t initMagic;
};

RUNTIME_SIZE(poolcontrol_t, 0x24, 0x40);

// The retail metadata intentionally remains two fields so its x86 ABI is
// unchanged. Each transient descriptor supplies the backing extent and its
// authoritative external control table; there is no hidden global registry.
struct poolstorage_t
{
    void *base;
    std::size_t itemSize;
    std::size_t itemCount;
    poolcontrol_t *control;
};

RUNTIME_SIZE(poolstorage_t, 0x10, 0x20);
RUNTIME_OFFSET(poolstorage_t, itemSize, 0x4, 0x8);
RUNTIME_OFFSET(poolstorage_t, itemCount, 0x8, 0x10);
RUNTIME_OFFSET(poolstorage_t, control, 0xC, 0x18);

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

enum class poolmutationstatus_t : std::uint8_t
{
    Success,
    Unavailable,
    InvalidState,
};

struct poolallocresult_t
{
    poolmutationstatus_t status;
    void *item;
};

inline poolstorage_t Pool_StorageFor(
    void *const base,
    const std::size_t itemSize,
    const std::size_t itemCount,
    poolcontrol_t &control) noexcept
{
    return {base, itemSize, itemCount, &control};
}

template <std::size_t N>
constexpr poolcontrol_t Pool_ControlFor(poolslotstate_t (&slotState)[N]) noexcept
{
    return {
        slotState,
        N,
        nullptr,
        nullptr,
        0,
        0,
        POOL_SLOT_END,
        0,
        0,
    };
}

template <typename T, std::size_t N>
inline poolstorage_t Pool_StorageFor(
    T (&items)[N],
    poolcontrol_t &control) noexcept
{
    return {static_cast<void *>(&items), sizeof(T), N, &control};
}

bool __cdecl Pool_Init(poolstorage_t storage, pooldata_t *pooldata) noexcept;
bool __cdecl Pool_Invalidate(
    poolstorage_t storage,
    pooldata_t *pooldata) noexcept;
void *__cdecl Pool_Alloc(poolstorage_t storage, pooldata_t *pooldata) noexcept;
bool __cdecl Pool_Free(
    poolstorage_t storage,
    pooldata_t *pooldata,
    void *item) noexcept;
// Physics transaction code invokes these while holding CRITSECT_PHYSICS.
// They preserve the O(1) mutation rules of Pool_Alloc/Pool_Free, but never
// report through MyAssertHandler. InvalidState is returned without mutation;
// Unavailable is the valid full-pool result for allocation.
[[nodiscard]] poolallocresult_t __cdecl Pool_TryAllocNoReport(
    poolstorage_t storage,
    pooldata_t *pooldata) noexcept;
[[nodiscard]] poolmutationstatus_t __cdecl Pool_TryFreeNoReport(
    poolstorage_t storage,
    pooldata_t *pooldata,
    void *item) noexcept;
[[nodiscard]] poolmutationstatus_t __cdecl
Pool_TryValidateAllocatedNoReport(
    poolstorage_t storage,
    const pooldata_t *pooldata,
    const void *item) noexcept;
[[nodiscard]] poolmutationstatus_t __cdecl Pool_TryValidateFullNoReport(
    poolstorage_t storage,
    const pooldata_t *pooldata) noexcept;
[[nodiscard]] bool __cdecl Pool_ValidateFull(
    poolstorage_t storage,
    const pooldata_t *pooldata) noexcept;
[[nodiscard]] poolcountresult_t __cdecl Pool_GetFreeCount(
    poolstorage_t storage,
    const pooldata_t *pooldata) noexcept;
[[nodiscard]] poolnextresult_t __cdecl Pool_NextFree(
    poolstorage_t storage,
    const void *item) noexcept;
[[nodiscard]] poolindexresult_t __cdecl Pool_GetSlotIndex(
    poolstorage_t storage,
    const void *item) noexcept;

#define INIT_STATIC_POOL(array, pooldata, control) \
    Pool_Init(Pool_StorageFor(array, control), pooldata)

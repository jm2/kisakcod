#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <script/scr_memorytree.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <vector>

namespace
{
std::recursive_mutex g_memoryTreeMutex;
thread_local std::uint32_t g_memoryTreeLockDepth = 0;
std::uint32_t g_comErrorCount = 0;
std::uint32_t g_assertCount = 0;

using TreeImage = std::vector<std::byte>;

struct Allocation final
{
    std::uint16_t id = 0;
    int bytes = 0;
    std::uint32_t buckets = 0;
    std::uint8_t type = 0;
};

[[nodiscard]] TreeImage CaptureTree()
{
    TreeImage image(sizeof(scrMemTreeGlob_t));
    std::memcpy(image.data(), &scrMemTreeGlob, image.size());
    return image;
}

[[nodiscard]] bool TreeMatches(const TreeImage &image) noexcept
{
    return std::memcmp(image.data(), &scrMemTreeGlob, image.size()) == 0;
}

[[nodiscard]] bool Check(
    const bool condition,
    const char *const message) noexcept
{
    if (!condition)
        std::fprintf(stderr, "script memory-tree test failed: %s\n", message);
    return condition;
}

[[nodiscard]] int RequestBytesForSize(const std::uint32_t size) noexcept
{
    if (size == 0)
        return 1;
    return static_cast<int>(
        (UINT64_C(1) << (size - 1u))
            * static_cast<std::uint64_t>(sizeof(MemoryNode))
        + UINT64_C(1));
}

[[nodiscard]] bool QueryAllocation(
    const std::uint16_t id,
    const std::uint8_t expectedType,
    Allocation *const allocation) noexcept
{
    MT_AllocationInfo info{0xFF, 0xFF, 0xFFFF, UINT32_MAX};
    if (!Check(
            MT_TryGetAllocationInfo(id, &info)
                == MT_AllocationInfoStatus::Success,
            "live allocation query failed")
        || !Check(info.type == expectedType, "allocation type mismatch")
        || !Check(info.reserved == 0, "allocation query reserved bytes changed")
        || !Check(
            info.capacityBytes % sizeof(MemoryNode) == 0,
            "allocation capacity is not bucket aligned"))
    {
        return false;
    }

    const std::uint32_t buckets =
        info.capacityBytes / static_cast<std::uint32_t>(sizeof(MemoryNode));
    if (!Check(buckets != 0 && (buckets & (buckets - 1u)) == 0,
            "allocation capacity is not a power of two")
        || !Check(
            buckets == (UINT32_C(1) << info.size),
            "allocation size/capacity mismatch")
        || !Check((id & (buckets - 1u)) == 0,
            "allocation ID is not capacity aligned")
        || !Check(id > 0 && id <= MEMORY_NODE_COUNT - buckets,
            "allocation interval is outside the memory tree"))
    {
        return false;
    }

    allocation->id = id;
    allocation->buckets = buckets;
    allocation->type = expectedType;
    return true;
}

[[nodiscard]] bool AddInterval(
    const Allocation &allocation,
    std::array<std::uint8_t, MEMORY_NODE_COUNT> *const occupied) noexcept
{
    for (std::uint32_t node = allocation.id;
         node < allocation.id + allocation.buckets;
         ++node)
    {
        if (!Check((*occupied)[node] == 0, "allocator returned an overlap"))
            return false;
        (*occupied)[node] = 1;
    }
    return true;
}

[[nodiscard]] bool RemoveInterval(
    const Allocation &allocation,
    std::array<std::uint8_t, MEMORY_NODE_COUNT> *const occupied) noexcept
{
    for (std::uint32_t node = allocation.id;
         node < allocation.id + allocation.buckets;
         ++node)
    {
        if (!Check((*occupied)[node] == 1, "free interval was not allocated"))
            return false;
        (*occupied)[node] = 0;
    }
    return true;
}

[[nodiscard]] bool TestInvalidAndNoChange() noexcept
{
    MT_Init();
    const TreeImage initial = CaptureTree();

    std::uint16_t outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndex(0, 1, &outId)
                == MT_AllocIndexStatus::InvalidArgumentNoChange,
            "zero-byte allocation was accepted")
        || !Check(outId == 0xBEEF, "failed allocation changed output")
        || !Check(TreeMatches(initial), "failed allocation changed tree")
        || !Check(
            MT_TryAllocIndex(MEMORY_NODE_COUNT, 1, &outId)
                == MT_AllocIndexStatus::InvalidArgumentNoChange,
            "oversized allocation was accepted")
        || !Check(outId == 0xBEEF, "oversized allocation changed output")
        || !Check(TreeMatches(initial), "oversized allocation changed tree")
        || !Check(
            MT_TryAllocIndex(1, 0, &outId)
                == MT_AllocIndexStatus::InvalidArgumentNoChange,
            "zero allocation type was accepted")
        || !Check(
            MT_TryAllocIndex(1, 22, &outId)
                == MT_AllocIndexStatus::InvalidArgumentNoChange,
            "out-of-range allocation type was accepted")
        || !Check(
            MT_TryAllocIndex(1, 1, nullptr)
                == MT_AllocIndexStatus::InvalidArgumentNoChange,
            "null allocation output was accepted")
        || !Check(TreeMatches(initial), "invalid allocation mutated tree"))
    {
        return false;
    }

    MT_AllocationInfo info{0xA5, 0x5A, 0xA55A, UINT32_C(0xA55AA55A)};
    const MT_AllocationInfo unchangedInfo = info;
    if (!Check(
            MT_TryGetAllocationInfo(0, &info)
                == MT_AllocationInfoStatus::InvalidArgumentNoChange,
            "zero allocation query was accepted")
        || !Check(
            std::memcmp(&info, &unchangedInfo, sizeof(info)) == 0,
            "failed allocation query changed output")
        || !Check(
            MT_TryGetAllocationInfo(MEMORY_NODE_COUNT, &info)
                == MT_AllocationInfoStatus::InvalidArgumentNoChange,
            "oversized allocation query was accepted")
        || !Check(
            MT_TryGetAllocationInfo(1, nullptr)
                == MT_AllocationInfoStatus::InvalidArgumentNoChange,
            "null allocation query output was accepted")
        || !Check(TreeMatches(initial), "invalid query mutated tree"))
    {
        return false;
    }

    if (!Check(
            MT_TryFreeIndex(0, 1)
                == MT_FreeIndexStatus::InvalidArgumentNoChange,
            "zero free ID was accepted")
        || !Check(
            MT_TryFreeIndex(MEMORY_NODE_COUNT, 1)
                == MT_FreeIndexStatus::InvalidArgumentNoChange,
            "oversized free ID was accepted")
        || !Check(
            MT_TryFreeIndex(1, 0)
                == MT_FreeIndexStatus::InvalidArgumentNoChange,
            "zero-byte free was accepted")
        || !Check(
            MT_TryFreeIndex(1, MEMORY_NODE_COUNT)
                == MT_FreeIndexStatus::InvalidArgumentNoChange,
            "oversized free was accepted")
        || !Check(TreeMatches(initial), "invalid free mutated tree"))
    {
        return false;
    }

    return Check(g_memoryTreeLockDepth == 0, "invalid paths leaked the lock");
}

[[nodiscard]] bool TestQueryAndFreeContracts() noexcept
{
    MT_Init();
    std::uint16_t id = 0xBEEF;
    if (!Check(
            MT_TryAllocIndex(25, 15, &id) == MT_AllocIndexStatus::Success,
            "representative allocation failed"))
    {
        return false;
    }

    Allocation allocation{id, 25, 0, 15};
    if (!QueryAllocation(id, 15, &allocation)
        || !Check(allocation.buckets == 4, "wrong rounded allocation capacity"))
    {
        return false;
    }

    const TreeImage beforeWrongFree = CaptureTree();
    if (!Check(
            MT_TryFreeIndex(id, 13)
                == MT_FreeIndexStatus::OwnershipMismatchNoChange,
            "wrong-size free was accepted")
        || !Check(TreeMatches(beforeWrongFree), "wrong-size free changed tree")
        || !QueryAllocation(id, 15, &allocation)
        || !Check(
            MT_TryFreeIndex(id, allocation.bytes)
                == MT_FreeIndexStatus::Success,
            "valid free failed"))
    {
        return false;
    }

    MT_AllocationInfo info{0xA5, 0x5A, 0xA55A, UINT32_C(0xA55AA55A)};
    const MT_AllocationInfo unchangedInfo = info;
    if (!Check(
            MT_TryGetAllocationInfo(id, &info)
                == MT_AllocationInfoStatus::NotAllocatedNoChange,
            "freed allocation still queried as live")
        || !Check(
            std::memcmp(&info, &unchangedInfo, sizeof(info)) == 0,
            "not-allocated query changed output"))
    {
        return false;
    }

    const TreeImage beforeDoubleFree = CaptureTree();
    return Check(
            MT_TryFreeIndex(id, allocation.bytes)
                == MT_FreeIndexStatus::OwnershipMismatchNoChange,
            "double free was accepted")
        && Check(TreeMatches(beforeDoubleFree), "double free changed tree")
        && Check(g_memoryTreeLockDepth == 0, "free/query paths leaked the lock");
}

[[nodiscard]] bool TestFullExhaustionAndRecovery() noexcept
{
    MT_Init();
    std::array<std::uint8_t, MEMORY_NODE_COUNT> occupied{};
    occupied[0] = 1;
    std::vector<Allocation> allocations;
    allocations.reserve(20);

    const auto allocateSize = [&](const std::uint32_t size) -> bool
    {
        const int bytes = RequestBytesForSize(size);
        std::uint16_t id = 0xBEEF;
        if (!Check(
                MT_TryAllocIndex(bytes, 1, &id)
                    == MT_AllocIndexStatus::Success,
                "full-capacity allocation failed"))
        {
            return false;
        }
        Allocation allocation{id, bytes, 0, 1};
        if (!QueryAllocation(id, 1, &allocation)
            || !Check(
                allocation.buckets == (UINT32_C(1) << size),
                "full-capacity allocation rounded incorrectly")
            || !AddInterval(allocation, &occupied))
        {
            return false;
        }
        allocations.push_back(allocation);
        return true;
    };

    for (std::uint32_t index = 0; index < 7; ++index)
    {
        if (!allocateSize(13))
            return false;
    }
    for (int size = 12; size >= 0; --size)
    {
        if (!allocateSize(static_cast<std::uint32_t>(size)))
            return false;
    }

    for (std::uint32_t node = 0; node < MEMORY_NODE_COUNT; ++node)
    {
        if (!Check(occupied[node] == 1, "full allocation left a gap"))
            return false;
    }
    if (!Check(scrMemTreeGlob.totalAllocBuckets == MEMORY_NODE_COUNT - 1,
            "full allocation accounting mismatch"))
    {
        return false;
    }

    const TreeImage exhausted = CaptureTree();
    std::uint16_t rejectedId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndex(1, 1, &rejectedId)
                == MT_AllocIndexStatus::InsufficientCapacityNoChange,
            "exhausted allocation did not report capacity")
        || !Check(rejectedId == 0xBEEF, "capacity failure changed output")
        || !Check(TreeMatches(exhausted), "capacity failure changed tree"))
    {
        return false;
    }

    std::mt19937 random(UINT32_C(0x4D545245));
    std::shuffle(allocations.begin(), allocations.end(), random);
    for (const Allocation &allocation : allocations)
    {
        if (!Check(
                MT_TryFreeIndex(allocation.id, allocation.bytes)
                    == MT_FreeIndexStatus::Success,
                "full-capacity recovery free failed")
            || !RemoveInterval(allocation, &occupied))
        {
            return false;
        }
    }
    if (!Check(scrMemTreeGlob.totalAlloc == 0, "recovery leaked allocations")
        || !Check(scrMemTreeGlob.totalAllocBuckets == 0,
            "recovery leaked allocation buckets"))
    {
        return false;
    }

    std::uint16_t recoveredId = 0;
    return Check(
            MT_TryAllocIndex(1, 2, &recoveredId)
                == MT_AllocIndexStatus::Success,
            "allocator did not recover after exhaustion")
        && Check(
            MT_TryFreeIndex(recoveredId, 1) == MT_FreeIndexStatus::Success,
            "recovered allocation could not be freed")
        && Check(g_memoryTreeLockDepth == 0, "exhaustion paths leaked the lock");
}

[[nodiscard]] bool TestRandomizedIntervals() noexcept
{
    MT_Init();
    std::array<std::uint8_t, MEMORY_NODE_COUNT> occupied{};
    occupied[0] = 1;
    std::vector<Allocation> allocations;
    allocations.reserve(128);
    std::mt19937 random(UINT32_C(0xC0D4BEEF));

    for (std::uint32_t iteration = 0; iteration < 2048; ++iteration)
    {
        const bool allocate = allocations.empty()
            || (allocations.size() < 128 && random() % 100u < 62u);
        if (allocate)
        {
            const std::uint32_t size = random() % 14u;
            const int minimumBytes = RequestBytesForSize(size);
            const std::uint32_t maximumBytes = std::min(
                UINT32_C(65535),
                (UINT32_C(1) << size)
                    * static_cast<std::uint32_t>(sizeof(MemoryNode)));
            const int bytes = minimumBytes + static_cast<int>(
                random() % (maximumBytes -
                    static_cast<std::uint32_t>(minimumBytes) + 1u));
            const std::uint8_t type =
                static_cast<std::uint8_t>(1u + random() % 21u);
            std::uint16_t id = 0xBEEF;
            const MT_AllocIndexStatus status =
                MT_TryAllocIndex(bytes, type, &id);
            if (status == MT_AllocIndexStatus::InsufficientCapacityNoChange)
            {
                if (!Check(id == 0xBEEF, "random capacity failure changed output"))
                    return false;
                continue;
            }
            if (!Check(status == MT_AllocIndexStatus::Success,
                    "random allocation reported unsafe failure"))
            {
                return false;
            }

            Allocation allocation{id, bytes, 0, type};
            if (!QueryAllocation(id, type, &allocation)
                || !AddInterval(allocation, &occupied))
            {
                return false;
            }
            allocations.push_back(allocation);
        }
        else
        {
            const std::size_t index = random() % allocations.size();
            const Allocation allocation = allocations[index];
            if (!Check(
                    MT_TryFreeIndex(allocation.id, allocation.bytes)
                        == MT_FreeIndexStatus::Success,
                    "random free failed")
                || !RemoveInterval(allocation, &occupied))
            {
                return false;
            }
            allocations[index] = allocations.back();
            allocations.pop_back();
        }

        if (!allocations.empty() && iteration % 31u == 0)
        {
            const Allocation &allocation =
                allocations[random() % allocations.size()];
            Allocation queried{
                allocation.id,
                allocation.bytes,
                0,
                allocation.type};
            if (!QueryAllocation(
                    allocation.id,
                    allocation.type,
                    &queried))
            {
                return false;
            }
        }
    }

    for (const Allocation &allocation : allocations)
    {
        if (!Check(
                MT_TryFreeIndex(allocation.id, allocation.bytes)
                    == MT_FreeIndexStatus::Success,
                "random cleanup free failed")
            || !RemoveInterval(allocation, &occupied))
        {
            return false;
        }
    }
    return Check(scrMemTreeGlob.totalAlloc == 0, "random test leaked allocations")
        && Check(g_memoryTreeLockDepth == 0, "random paths leaked the lock");
}

[[nodiscard]] bool TestCorruptionRejection() noexcept
{
    MT_Init();
    const std::uint16_t root = scrMemTreeGlob.head[0];
    if (!Check(root != 0, "cycle probe has no free root"))
        return false;
    scrMemTreeGlob.nodes[root].prev = root;
    const TreeImage cycle = CaptureTree();
    std::uint16_t outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndex(1, 1, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "free-tree cycle was not rejected")
        || !Check(outId == 0xBEEF, "cycle failure changed output")
        || !Check(TreeMatches(cycle), "cycle failure changed tree"))
    {
        return false;
    }

    g_comErrorCount = 0;
    g_assertCount = 0;
    if (!Check(MT_AllocIndex(1, 1) == 0,
            "legacy allocation did not fail closed on corruption")
        || !Check(g_comErrorCount == 1,
            "legacy allocation did not use one terminal diagnostic")
        || !Check(g_assertCount == 0,
            "unsafe legacy allocation entered an assert path")
        || !Check(TreeMatches(cycle),
            "legacy unsafe allocation changed the corrupt tree"))
    {
        return false;
    }
    MT_FreeIndex(root, 1);
    if (!Check(g_comErrorCount == 2,
            "legacy free did not use one terminal diagnostic")
        || !Check(g_assertCount == 0,
            "unsafe legacy free entered an assert path")
        || !Check(TreeMatches(cycle),
            "legacy unsafe free changed the corrupt tree"))
    {
        return false;
    }

    MT_Init();
    std::uint16_t owner = 0;
    if (!Check(
            MT_TryAllocIndex(48, 1, &owner) == MT_AllocIndexStatus::Success,
            "interior-overlap owner allocation failed"))
    {
        return false;
    }
    Allocation ownerAllocation{owner, 48, 0, 1};
    if (!QueryAllocation(owner, 1, &ownerAllocation)
        || !Check(ownerAllocation.buckets == 4,
            "interior-overlap owner has wrong size"))
    {
        return false;
    }

    const std::uint16_t interior = static_cast<std::uint16_t>(owner + 2u);
    scrMemTreeGlob.head[1] = interior;
    scrMemTreeGlob.nodes[interior].prev = 0;
    scrMemTreeGlob.nodes[interior].next = 0;
    const TreeImage overlap = CaptureTree();
    outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndex(13, 2, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "interior free interval was not rejected")
        || !Check(outId == 0xBEEF, "overlap failure changed output")
        || !Check(TreeMatches(overlap), "overlap failure changed tree"))
    {
        return false;
    }

    MT_AllocationInfo info{0xA5, 0x5A, 0xA55A, UINT32_C(0xA55AA55A)};
    const MT_AllocationInfo unchangedInfo = info;
    return Check(
            MT_TryGetAllocationInfo(owner, &info)
                == MT_AllocationInfoStatus::UnsafeFailure,
            "query trusted an overlapping partition")
        && Check(
            std::memcmp(&info, &unchangedInfo, sizeof(info)) == 0,
            "unsafe query changed output")
        && Check(TreeMatches(overlap), "unsafe query changed tree")
        && Check(g_memoryTreeLockDepth == 0, "corruption paths leaked the lock");
}
} // namespace

void KISAK_CDECL Sys_EnterCriticalSection(const int critSect)
{
    if (critSect != CRITSECT_MEMORY_TREE)
        std::abort();
    g_memoryTreeMutex.lock();
    ++g_memoryTreeLockDepth;
}

void KISAK_CDECL Sys_LeaveCriticalSection(const int critSect)
{
    if (critSect != CRITSECT_MEMORY_TREE || g_memoryTreeLockDepth == 0)
        std::abort();
    --g_memoryTreeLockDepth;
    g_memoryTreeMutex.unlock();
}

void QDECL Com_Printf(int, const char *, ...)
{
}

void QDECL Com_Error(errorParm_t, const char *, ...)
{
    ++g_comErrorCount;
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_assertCount;
}

char *QDECL va(const char *, ...)
{
    static thread_local char empty[1]{};
    return empty;
}

const char *SL_DebugConvertToString(std::uint32_t)
{
    return "";
}

int main()
{
    if (!TestInvalidAndNoChange()
        || !TestQueryAndFreeContracts()
        || !TestFullExhaustionAndRecovery()
        || !TestRandomizedIntervals()
        || !TestCorruptionRejection())
    {
        return 1;
    }

    std::puts("script memory-tree try contracts passed");
    return 0;
}

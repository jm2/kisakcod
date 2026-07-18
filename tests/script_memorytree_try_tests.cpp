#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <script/scr_memorytree.cpp>

#include <algorithm>
#include <array>
#include <chrono>
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
    TreeImage image;
    image.reserve(
        sizeof(scrMemTreePub)
        + sizeof(scrMemTreeGlob)
        + sizeof(scrMemTreeDebugGlob)
        + sizeof(mt_allocationMetadataShadow)
        + sizeof(mt_freeNodeSizeShadow)
        + sizeof(mt_freeTreeHeadShadow)
        + sizeof(mt_freeNodeLinkShadow)
        + sizeof(mt_freeNodeCountShadow)
        + sizeof(mt_freeNodeCountMirror)
        + sizeof(mt_totalAllocShadow)
        + sizeof(mt_totalAllocBucketsShadow));
    const auto append = [&image](const void *const source,
                            const std::size_t size) {
        const std::size_t offset = image.size();
        image.resize(offset + size);
        std::memcpy(image.data() + offset, source, size);
    };
    append(&scrMemTreePub, sizeof(scrMemTreePub));
    append(&scrMemTreeGlob, sizeof(scrMemTreeGlob));
    append(&scrMemTreeDebugGlob, sizeof(scrMemTreeDebugGlob));
    append(mt_allocationMetadataShadow,
        sizeof(mt_allocationMetadataShadow));
    append(mt_freeNodeSizeShadow, sizeof(mt_freeNodeSizeShadow));
    append(mt_freeTreeHeadShadow, sizeof(mt_freeTreeHeadShadow));
    append(mt_freeNodeLinkShadow, sizeof(mt_freeNodeLinkShadow));
    append(&mt_freeNodeCountShadow, sizeof(mt_freeNodeCountShadow));
    append(&mt_freeNodeCountMirror, sizeof(mt_freeNodeCountMirror));
    append(&mt_totalAllocShadow, sizeof(mt_totalAllocShadow));
    append(&mt_totalAllocBucketsShadow, sizeof(mt_totalAllocBucketsShadow));
    return image;
}

[[nodiscard]] bool TreeMatches(const TreeImage &image) noexcept
{
    return CaptureTree() == image;
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

[[nodiscard]] bool TestLegacyLocalValidationScope() noexcept
{
    MT_Init();
    MT_ResetCompleteValidationCountForTesting();

    std::uint16_t legacyId = 0xBEEF;
    MT_AllocationInfo legacyInfo{};
    if (!Check(
            MT_TryAllocIndexLegacy(25, 15, &legacyId)
                == MT_AllocIndexStatus::Success,
            "legacy-local allocation failed")
        || !Check(legacyId != 0 && legacyId != 0xBEEF,
            "legacy-local allocation did not publish an ID")
        || !Check(
            MT_TryGetAllocationInfoLegacy(legacyId, &legacyInfo)
                == MT_AllocationInfoStatus::Success,
            "legacy-local query failed")
        || !Check(legacyInfo.type == 15 && legacyInfo.capacityBytes == 48,
            "legacy-local query returned the wrong allocation")
        || !Check(
            MT_TryFreeIndexLegacy(legacyId, 25)
                == MT_FreeIndexStatus::Success,
            "legacy-local free failed")
        || !Check(MT_CompleteValidationCountForTesting() == 0,
            "legacy-local operations performed a complete partition scan")
        || !Check(MT_CompleteForestValidationCountForTesting() == 0,
            "legacy-local operations performed a complete forest walk"))
    {
        return false;
    }

    std::uint16_t typedId = 0;
    if (!Check(
            MT_TryAllocIndex(25, 15, &typedId)
                == MT_AllocIndexStatus::Success,
            "typed allocation failed after legacy-local operations")
        || !Check(MT_CompleteValidationCountForTesting() != 0,
            "typed allocation skipped complete partition validation")
        || !Check(
            MT_TryFreeIndex(typedId, 25)
                == MT_FreeIndexStatus::Success,
            "typed cleanup failed after legacy-local operations")
        || !Check(g_memoryTreeLockDepth == 0,
            "legacy-local validation paths leaked the lock"))
    {
        return false;
    }

    MT_Init();
    MT_ResetCompleteValidationCountForTesting();
    const std::uint16_t root = scrMemTreeGlob.head[0];
    scrMemTreeGlob.nodes[root].prev = root;
    const TreeImage corruptPath = CaptureTree();
    legacyId = 0xBEEF;
    return Check(
            MT_TryAllocIndexLegacy(1, 1, &legacyId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy-local allocation trusted a traversed cycle")
        && Check(legacyId == 0xBEEF,
            "legacy-local cycle rejection changed output")
        && Check(TreeMatches(corruptPath),
            "legacy-local cycle rejection changed allocator state")
        && Check(MT_CompleteValidationCountForTesting() == 0,
            "legacy-local corruption rejection used a complete scan")
        && Check(g_memoryTreeLockDepth == 0,
            "legacy-local corruption rejection leaked the lock");
}

[[nodiscard]] bool TestRawFreeTreeMutatorsSynchronizeShadows() noexcept
{
    MT_Init();
    const TreeImage initial = CaptureTree();
    const std::uint16_t root = scrMemTreeGlob.head[0];
    const std::uint32_t initialFreeCount = mt_freeNodeCountShadow;
    if (!Check(root != 0, "raw mutator fixture has no size-zero root"))
        return false;

    MT_RemoveHeadMemoryNode(0);
    if (!Check(scrMemTreeGlob.head[0] == 0
            && mt_freeTreeHeadShadow[0] == 0,
            "raw head removal did not synchronize the head mirror")
        || !Check(mt_freeNodeSizeShadow[root] == 0
                && mt_freeNodeCountShadow + 1 == initialFreeCount
                && mt_freeNodeCountMirror == mt_freeNodeCountShadow,
            "raw head removal did not synchronize membership accounting"))
    {
        return false;
    }
    MT_AddMemoryNode(root, 0);
    if (!Check(TreeMatches(initial),
            "raw head remove/add did not restore all authenticated state"))
    {
        return false;
    }

    if (!Check(MT_RemoveMemoryNode(root, 0),
            "raw targeted removal did not find the size-zero root")
        || !Check(scrMemTreeGlob.head[0] == 0
                && mt_freeTreeHeadShadow[0] == 0,
            "raw targeted removal did not synchronize the head mirror")
        || !Check(mt_freeNodeSizeShadow[root] == 0
                && mt_freeNodeCountShadow + 1 == initialFreeCount
                && mt_freeNodeCountMirror == mt_freeNodeCountShadow,
            "raw targeted removal did not synchronize membership accounting"))
    {
        return false;
    }
    MT_AddMemoryNode(root, 0);
    return Check(TreeMatches(initial),
            "raw targeted remove/add did not restore authenticated state")
        && Check(MT_TryValidateState(),
            "raw synchronized mutators left an invalid complete tree")
        && Check(g_memoryTreeLockDepth == 0,
            "raw synchronized mutators leaked the lock");
}

[[nodiscard]] bool TestTopologyShadowCorruptionFailsClosed() noexcept
{
    MT_Init();
    const std::uint16_t root = scrMemTreeGlob.head[0];
    if (!Check(root != 0, "topology-shadow fixture has no size-zero root"))
        return false;

    mt_freeNodeLinkShadow[root].next = 3;
    const TreeImage corruptLinkShadow = CaptureTree();
    std::uint16_t outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(1, 15, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted a forged free-link shadow")
        || !Check(outId == 0xBEEF,
            "forged free-link shadow rejection published an ID")
        || !Check(TreeMatches(corruptLinkShadow),
            "forged free-link shadow rejection changed state"))
    {
        return false;
    }

    MT_Init();
    mt_freeTreeHeadShadow[0] = 0;
    const TreeImage corruptHeadShadow = CaptureTree();
    outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(13, 15, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted a forged free-head shadow")
        || !Check(outId == 0xBEEF,
            "forged free-head shadow rejection published an ID")
        || !Check(TreeMatches(corruptHeadShadow),
            "forged free-head shadow rejection changed state"))
    {
        return false;
    }

    MT_Init();
    mt_freeNodeLinkShadow[3].next = 1;
    const TreeImage corruptOrphanShadow = CaptureTree();
    if (!Check(!MT_TryValidateState(),
            "complete validation trusted an orphan topology shadow")
        || !Check(TreeMatches(corruptOrphanShadow),
            "orphan topology-shadow rejection changed state"))
    {
        return false;
    }

    MT_Init();
    ++mt_freeNodeCountShadow;
    const TreeImage corruptFreeCount = CaptureTree();
    outId = 0xBEEF;
    return Check(
            MT_TryAllocIndexLegacy(13, 15, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted a one-sided free-node count")
        && Check(outId == 0xBEEF,
            "free-node count rejection published an ID")
        && Check(TreeMatches(corruptFreeCount),
            "free-node count rejection changed state")
        && Check(g_memoryTreeLockDepth == 0,
            "topology-shadow rejection leaked the lock");
}

[[nodiscard]] bool TestLegacyTouchedIntervalCorruption() noexcept
{
    MT_Init();
    MT_ResetCompleteValidationCountForTesting();
    const std::uint16_t sizeOneRoot = scrMemTreeGlob.head[1];
    MT_CorruptAllocationMetadataForTesting(
        static_cast<std::uint32_t>(sizeOneRoot) + 1u, 1, 0);
    const TreeImage corruptCandidate = CaptureTree();
    std::uint16_t outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(13, 15, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted hidden interval metadata")
        || !Check(outId == 0xBEEF,
            "hidden interval metadata rejection published an ID")
        || !Check(TreeMatches(corruptCandidate),
            "hidden interval metadata rejection changed tree topology")
        || !Check(MT_CompleteValidationCountForTesting() == 0,
            "hidden interval metadata rejection used a complete scan"))
    {
        return false;
    }
    MT_CorruptAllocationMetadataForTesting(
        static_cast<std::uint32_t>(sizeOneRoot) + 1u, 0, 0);

    MT_Init();
    std::uint16_t allocationId = 0;
    if (!Check(
            MT_TryAllocIndexLegacy(25, 15, &allocationId)
                == MT_AllocIndexStatus::Success,
            "legacy interval-corruption allocation setup failed"))
    {
        return false;
    }
    MT_AllocationInfo unchanged{0xA5, 0x5A, 0xA55A,
        UINT32_C(0xA55AA55A)};
    MT_CorruptAllocationMetadataForTesting(allocationId, 0, 0);
    const TreeImage clearedPrimary = CaptureTree();
    if (!Check(
            MT_TryGetAllocationInfoLegacy(allocationId, &unchanged)
                == MT_AllocationInfoStatus::UnsafeFailure,
            "legacy query trusted cleared primary metadata")
        || !Check(unchanged.type == 0xA5 && unchanged.size == 0x5A
                && unchanged.reserved == 0xA55A
                && unchanged.capacityBytes == UINT32_C(0xA55AA55A),
            "cleared primary metadata rejection changed output")
        || !Check(
            MT_TryFreeIndexLegacy(allocationId, 25)
                == MT_FreeIndexStatus::UnsafeFailure,
            "legacy free trusted cleared primary metadata")
        || !Check(TreeMatches(clearedPrimary),
            "cleared primary metadata rejection changed allocator state"))
    {
        return false;
    }
    MT_CorruptAllocationMetadataForTesting(allocationId, 15, 2);

    MT_CorruptAllocationMetadataForTesting(allocationId, 15, 0);
    if (!Check(
            MT_TryGetAllocationInfoLegacy(allocationId, &unchanged)
                == MT_AllocationInfoStatus::UnsafeFailure,
            "legacy query trusted forged root metadata")
        || !Check(unchanged.type == 0xA5 && unchanged.size == 0x5A
                && unchanged.reserved == 0xA55A
                && unchanged.capacityBytes == UINT32_C(0xA55AA55A),
            "forged root metadata rejection changed output"))
    {
        return false;
    }
    MT_CorruptAllocationMetadataForTesting(allocationId, 15, 2);

    ++scrMemTreeGlob.totalAlloc;
    const TreeImage corruptAllocationCount = CaptureTree();
    if (!Check(
            MT_TryGetAllocationInfoLegacy(allocationId, &unchanged)
                == MT_AllocationInfoStatus::UnsafeFailure,
            "legacy query trusted corrupted allocation count")
        || !Check(
            MT_TryFreeIndexLegacy(allocationId, 25)
                == MT_FreeIndexStatus::UnsafeFailure,
            "legacy free trusted corrupted allocation count")
        || !Check(TreeMatches(corruptAllocationCount),
            "allocation-count rejection changed allocator state"))
    {
        return false;
    }
    --scrMemTreeGlob.totalAlloc;

    ++scrMemTreeGlob.totalAllocBuckets;
    const TreeImage corruptBucketCount = CaptureTree();
    if (!Check(
            MT_TryGetAllocationInfoLegacy(allocationId, &unchanged)
                == MT_AllocationInfoStatus::UnsafeFailure,
            "legacy query trusted corrupted bucket count")
        || !Check(
            MT_TryFreeIndexLegacy(allocationId, 25)
                == MT_FreeIndexStatus::UnsafeFailure,
            "legacy free trusted corrupted bucket count")
        || !Check(TreeMatches(corruptBucketCount),
            "bucket-count rejection changed allocator state"))
    {
        return false;
    }
    --scrMemTreeGlob.totalAllocBuckets;

    MT_CorruptAllocationMetadataForTesting(
        static_cast<std::uint32_t>(allocationId) + 1u, 1, 0);
    const TreeImage corruptAllocation = CaptureTree();
    if (!Check(
            MT_TryGetAllocationInfoLegacy(allocationId, &unchanged)
                == MT_AllocationInfoStatus::UnsafeFailure,
            "legacy query trusted hidden interior allocation metadata")
        || !Check(
            MT_TryFreeIndexLegacy(allocationId, 25)
                == MT_FreeIndexStatus::UnsafeFailure,
            "legacy free trusted hidden interior allocation metadata")
        || !Check(TreeMatches(corruptAllocation),
            "hidden allocation metadata rejection changed tree topology"))
    {
        return false;
    }
    MT_CorruptAllocationMetadataForTesting(
        static_cast<std::uint32_t>(allocationId) + 1u, 0, 0);
    if (!Check(
            MT_TryFreeIndexLegacy(allocationId, 25)
                == MT_FreeIndexStatus::Success,
            "legacy hidden-metadata cleanup failed"))
    {
        return false;
    }

    MT_Init();
    const std::uint16_t orphanedHead = scrMemTreeGlob.head[0];
    scrMemTreeGlob.head[0] = 0;
    const TreeImage clearedHead = CaptureTree();
    outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(1, 15, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted an orphaned free head")
        || !Check(outId == 0xBEEF,
            "orphaned free-head rejection published an ID")
        || !Check(TreeMatches(clearedHead),
            "orphaned free-head rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.head[0] = orphanedHead;

    MT_Init();
    const std::uint16_t wrongBranchRoot = scrMemTreeGlob.head[0];
    const MemoryNode savedWrongBranchRoot =
        scrMemTreeGlob.nodes[wrongBranchRoot];
    const MemoryNode savedWrongBranchAlias = scrMemTreeGlob.nodes[3];
    scrMemTreeGlob.nodes[wrongBranchRoot].prev = 3;
    scrMemTreeGlob.nodes[3] = {};
    const TreeImage wrongBranchAlias = CaptureTree();
    if (!Check(!MT_TryValidateState(),
            "complete validation trusted a wrong-branch free alias")
        || !Check(TreeMatches(wrongBranchAlias),
            "wrong-branch alias rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.nodes[wrongBranchRoot] = savedWrongBranchRoot;
    scrMemTreeGlob.nodes[3] = savedWrongBranchAlias;
    MT_ResetCompleteValidationCountForTesting();

    MT_Init();
    std::array<std::uint16_t, 5> branchAllocations{};
    for (std::uint16_t &branchAllocation : branchAllocations)
    {
        if (!Check(
                MT_TryAllocIndexLegacy(1, 13, &branchAllocation)
                    == MT_AllocIndexStatus::Success,
                "off-path alias allocation setup failed"))
        {
            return false;
        }
    }
    if (!Check(
            MT_TryFreeIndexLegacy(branchAllocations[1], 1)
                == MT_FreeIndexStatus::Success,
            "off-path alias first free setup failed")
        || !Check(
            MT_TryFreeIndexLegacy(branchAllocations[3], 1)
                == MT_FreeIndexStatus::Success,
            "off-path alias second free setup failed"))
    {
        return false;
    }
    const std::uint16_t branchRoot = scrMemTreeGlob.head[0];
    const std::uint16_t offPathParent =
        scrMemTreeGlob.nodes[branchRoot].prev != 0
            ? scrMemTreeGlob.nodes[branchRoot].prev
            : scrMemTreeGlob.nodes[branchRoot].next;
    if (!Check(offPathParent != 0,
            "off-path alias setup did not create a nontrivial tree"))
    {
        return false;
    }
    const MemoryNode savedBranchRoot = scrMemTreeGlob.nodes[branchRoot];
    const MemoryNode savedPriorityChild =
        scrMemTreeGlob.nodes[offPathParent];
    scrMemTreeGlob.head[0] = offPathParent;
    scrMemTreeGlob.nodes[offPathParent].prev = 0;
    scrMemTreeGlob.nodes[offPathParent].next = branchRoot;
    scrMemTreeGlob.nodes[branchRoot] = {};
    const TreeImage invertedPriority = CaptureTree();
    outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(1, 13, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted inverted free-tree priority")
        || !Check(outId == 0xBEEF,
            "inverted-priority rejection published an ID")
        || !Check(TreeMatches(invertedPriority),
            "inverted-priority rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.head[0] = branchRoot;
    scrMemTreeGlob.nodes[branchRoot] = savedBranchRoot;
    scrMemTreeGlob.nodes[offPathParent] = savedPriorityChild;

    if (scrMemTreeGlob.nodes[branchRoot].prev == offPathParent)
        scrMemTreeGlob.nodes[branchRoot].prev = 0;
    else
        scrMemTreeGlob.nodes[branchRoot].next = 0;
    const TreeImage clearedChild = CaptureTree();
    if (!Check(!MT_TryValidateState(),
            "complete validation trusted an orphaned off-path free child")
        || !Check(TreeMatches(clearedChild),
            "orphaned free-child rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.nodes[branchRoot] = savedBranchRoot;
    MT_ResetCompleteValidationCountForTesting();

    scrMemTreeGlob.nodes[branchRoot].prev = offPathParent;
    scrMemTreeGlob.nodes[branchRoot].next = 0;
    const TreeImage misplacedFreeNode = CaptureTree();
    outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(1, 13, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted swapped free-tree branches")
        || !Check(outId == 0xBEEF,
            "swapped-branch rejection published an ID")
        || !Check(
            MT_TryFreeIndexLegacy(offPathParent, 1)
                == MT_FreeIndexStatus::UnsafeFailure,
            "legacy double-free trusted a misplaced free member")
        || !Check(TreeMatches(misplacedFreeNode),
            "misplaced free-member rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.nodes[branchRoot] = savedBranchRoot;

    if (scrMemTreeGlob.nodes[offPathParent].prev == 0)
        scrMemTreeGlob.nodes[offPathParent].prev = 7;
    else
        scrMemTreeGlob.nodes[offPathParent].next = 7;
    scrMemTreeGlob.nodes[7] = {};
    const TreeImage offPathAlias = CaptureTree();
    if (!Check(!MT_TryValidateState(),
            "complete validation trusted an off-path free alias")
        || !Check(TreeMatches(offPathAlias),
            "off-path alias rejection changed allocator state"))
    {
        return false;
    }
    MT_ResetCompleteValidationCountForTesting();

    MT_Init();
    const std::uint16_t savedSizeZeroHead = scrMemTreeGlob.head[0];
    const MemoryNode savedNestedNode = scrMemTreeGlob.nodes[3];
    scrMemTreeGlob.head[0] = 3;
    scrMemTreeGlob.nodes[3] = {};
    const TreeImage corruptFreeAlias = CaptureTree();
    outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(13, 15, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted a nested free-tree alias")
        || !Check(outId == 0xBEEF,
            "nested free-tree alias rejection published an ID")
        || !Check(TreeMatches(corruptFreeAlias),
            "nested free-tree alias rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.head[0] = savedSizeZeroHead;
    scrMemTreeGlob.nodes[3] = savedNestedNode;

    MT_Init();
    const std::uint16_t invalidMembershipRoot = scrMemTreeGlob.head[1];
    MT_CorruptFreeNodeMembershipForTesting(
        static_cast<std::uint32_t>(invalidMembershipRoot) + 1u, 0xFF);
    const TreeImage invalidMembership = CaptureTree();
    outId = 0xBEEF;
    if (!Check(
            MT_TryAllocIndexLegacy(13, 15, &outId)
                == MT_AllocIndexStatus::UnsafeFailure,
            "legacy allocation trusted invalid free membership")
        || !Check(outId == 0xBEEF,
            "invalid free-membership rejection published an ID")
        || !Check(TreeMatches(invalidMembership),
            "invalid free-membership rejection changed allocator state"))
    {
        return false;
    }
    MT_CorruptFreeNodeMembershipForTesting(
        static_cast<std::uint32_t>(invalidMembershipRoot) + 1u, 0);

    MT_Init();
    std::array<std::uint16_t, 3> ancestorAllocations{};
    for (std::uint16_t &ancestorAllocation : ancestorAllocations)
    {
        if (!Check(
                MT_TryAllocIndexLegacy(25, 15, &ancestorAllocation)
                    == MT_AllocIndexStatus::Success,
                "larger-ancestor alias allocation setup failed"))
        {
            return false;
        }
    }
    if (!Check(
            MT_TryFreeIndexLegacy(ancestorAllocations[1], 25)
                == MT_FreeIndexStatus::Success,
            "larger-ancestor alias free setup failed"))
    {
        return false;
    }
    const std::uint16_t savedSizeThreeHead = scrMemTreeGlob.head[3];
    scrMemTreeGlob.head[3] = ancestorAllocations[1];
    const TreeImage largerAncestorAlias = CaptureTree();
    if (!Check(
            MT_TryFreeIndexLegacy(ancestorAllocations[2], 25)
                == MT_FreeIndexStatus::UnsafeFailure,
            "legacy free trusted a larger-ancestor free alias")
        || !Check(TreeMatches(largerAncestorAlias),
            "larger-ancestor alias rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.head[3] = savedSizeThreeHead;
    if (!Check(
            MT_TryFreeIndexLegacy(ancestorAllocations[2], 25)
                == MT_FreeIndexStatus::Success,
            "larger-ancestor alias cleanup failed")
        || !Check(
            MT_TryFreeIndexLegacy(ancestorAllocations[0], 25)
                == MT_FreeIndexStatus::Success,
            "larger-ancestor alias final cleanup failed"))
    {
        return false;
    }

    MT_Init();
    std::uint16_t firstId = 0;
    std::uint16_t mergeId = 0;
    if (!Check(
            MT_TryAllocIndexLegacy(13, 15, &firstId)
                == MT_AllocIndexStatus::Success,
            "legacy merge-alias first allocation failed")
        || !Check(
            MT_TryAllocIndexLegacy(13, 15, &mergeId)
                == MT_AllocIndexStatus::Success,
            "legacy merge-alias second allocation failed"))
    {
        return false;
    }
    const std::uint16_t mergeSizeZeroHead = scrMemTreeGlob.head[0];
    const MemoryNode savedMergeAlias = scrMemTreeGlob.nodes[7];
    scrMemTreeGlob.head[0] = 7;
    scrMemTreeGlob.nodes[7] = {};
    const TreeImage corruptMergeAlias = CaptureTree();
    if (!Check(
            MT_TryFreeIndexLegacy(mergeId, 13)
                == MT_FreeIndexStatus::UnsafeFailure,
            "legacy free trusted an alias inside a consumed buddy")
        || !Check(TreeMatches(corruptMergeAlias),
            "consumed-buddy alias rejection changed allocator state"))
    {
        return false;
    }
    scrMemTreeGlob.head[0] = mergeSizeZeroHead;
    scrMemTreeGlob.nodes[7] = savedMergeAlias;

    return Check(
            MT_TryFreeIndexLegacy(mergeId, 13)
                == MT_FreeIndexStatus::Success,
            "legacy consumed-buddy alias cleanup failed")
        && Check(
            MT_TryFreeIndexLegacy(firstId, 13)
                == MT_FreeIndexStatus::Success,
            "legacy alias test final cleanup failed")
        && Check(MT_CompleteValidationCountForTesting() == 0,
            "legacy interval/alias checks used a complete scan")
        && Check(g_memoryTreeLockDepth == 0,
            "legacy interval/alias rejection leaked the lock");
}

[[nodiscard]] bool TestLegacyFragmentedForestCost() noexcept
{
    MT_Init();
    MT_ResetCompleteValidationCountForTesting();
    constexpr std::uint32_t fragmentCount = 4096;
    std::vector<std::uint16_t> allocations(fragmentCount * 2u);
    for (std::uint16_t &allocation : allocations)
    {
        if (!Check(
                MT_TryAllocIndexLegacy(1, 13, &allocation)
                    == MT_AllocIndexStatus::Success,
                "fragmented-forest allocation setup failed"))
        {
            return false;
        }
    }
    for (std::uint32_t index = 0; index < allocations.size(); index += 2)
    {
        if (!Check(
                MT_TryFreeIndexLegacy(allocations[index], 1)
                    == MT_FreeIndexStatus::Success,
                "fragmented-forest free setup failed"))
        {
            return false;
        }
    }
    if (!Check(MT_CompleteValidationCountForTesting() == 0,
            "fragmented legacy setup used a complete partition scan")
        || !Check(MT_CompleteForestValidationCountForTesting() == 0,
            "fragmented legacy setup used a complete forest walk"))
    {
        return false;
    }

    std::uint16_t probe = 0;
    const auto start = std::chrono::steady_clock::now();
    const MT_AllocIndexStatus probeStatus =
        MT_TryAllocIndexLegacy(1, 13, &probe);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(elapsed).count();
    std::printf(
        "legacy fragmented-forest benchmark: %u size-0 extents, %.3f ms alloc\n",
        fragmentCount,
        elapsedMs);
    if (!Check(probeStatus == MT_AllocIndexStatus::Success,
            "fragmented-forest probe allocation failed")
        || !Check(
            MT_TryFreeIndexLegacy(probe, 1) == MT_FreeIndexStatus::Success,
            "fragmented-forest probe cleanup failed"))
    {
        return false;
    }

    for (std::uint32_t index = 1; index < allocations.size(); index += 2)
    {
        if (!Check(
                MT_TryFreeIndexLegacy(allocations[index], 1)
                    == MT_FreeIndexStatus::Success,
                "fragmented-forest final cleanup failed"))
        {
            return false;
        }
    }
    return Check(MT_CompleteValidationCountForTesting() == 0,
            "fragmented legacy mutations used a complete partition scan")
        && Check(MT_CompleteForestValidationCountForTesting() == 0,
            "fragmented legacy cleanup used a complete forest walk")
        && Check(scrMemTreeGlob.totalAlloc == 0,
            "fragmented-forest cleanup leaked allocations")
        && Check(g_memoryTreeLockDepth == 0,
            "fragmented-forest benchmark leaked the lock");
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
        || !TestLegacyLocalValidationScope()
        || !TestRawFreeTreeMutatorsSynchronizeShadows()
        || !TestTopologyShadowCorruptionFailsClosed()
        || !TestLegacyTouchedIntervalCorruption()
        || !TestLegacyFragmentedForestCost()
        || !TestCorruptionRejection())
    {
        return 1;
    }

    std::puts("script memory-tree try contracts passed");
    return 0;
}

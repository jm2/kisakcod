#include "scr_memorytree.h"
#include "scr_stringlist.h" // scrMemTreePub_t ownership remains declared here.

#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <atomic>
#include <cstdint>
#include <universal/profile.h>

scrMemTreePub_t scrMemTreePub;
int marker_scr_memorytree;

scrMemTreeGlob_t scrMemTreeGlob;

struct scrMemTreeDebugGlob_t // sizeof=0x20000
{                                       // ...
    uint8_t mt_usage[MEMORY_NODE_COUNT];    // ...
    uint8_t mt_usage_size[MEMORY_NODE_COUNT]; // ...
};
scrMemTreeDebugGlob_t scrMemTreeDebugGlob{};

struct MT_ValidationLeaseAccess final
{
    static bool IsCanonicalClear(
        const MT_ValidationLease &lease) noexcept
    {
        return lease.serial_ == 0 && lease.mutationCount_ == 0
            && !lease.active_ && !lease.poisoned_
            && lease.reserved_[0] == 0 && lease.reserved_[1] == 0;
    }

    static bool Active(const MT_ValidationLease &lease) noexcept
    {
        return lease.active_;
    }

    static bool Poisoned(const MT_ValidationLease &lease) noexcept
    {
        return lease.poisoned_;
    }

    static uint64_t Serial(const MT_ValidationLease &lease) noexcept
    {
        return lease.serial_;
    }

    static uint32_t MutationCount(
        const MT_ValidationLease &lease) noexcept
    {
        return lease.mutationCount_;
    }

    static bool ReservedClear(
        const MT_ValidationLease &lease) noexcept
    {
        return lease.reserved_[0] == 0 && lease.reserved_[1] == 0;
    }

    static void Activate(
        MT_ValidationLease &lease,
        const uint64_t serial) noexcept
    {
        lease.serial_ = serial;
        lease.mutationCount_ = 0;
        lease.active_ = true;
        lease.poisoned_ = false;
        lease.reserved_[0] = 0;
        lease.reserved_[1] = 0;
    }

    static void Poison(MT_ValidationLease &lease) noexcept
    {
        lease.poisoned_ = true;
    }

    static bool CanCountMutation(
        const MT_ValidationLease &lease) noexcept
    {
        return lease.mutationCount_ != UINT32_MAX;
    }

    static void CountMutation(MT_ValidationLease &lease) noexcept
    {
        ++lease.mutationCount_;
    }

    static void Clear(MT_ValidationLease &lease) noexcept
    {
        lease.serial_ = 0;
        lease.mutationCount_ = 0;
        lease.active_ = false;
        lease.poisoned_ = false;
        lease.reserved_[0] = 0;
        lease.reserved_[1] = 0;
    }
};

namespace
{
enum class MT_ValidationPolicy : uint8_t
{
    Complete,
    LegacyLocal,
    Leased,
};

enum class MT_PolicyEntryStatus : uint8_t
{
    Success,
    InvalidLease,
    UnsafeFailure,
};

enum class MT_ValidationLeaseLifecycle : uint8_t
{
    Idle,
    Active,
    Poisoned,
    Frozen,
};

constexpr const char *mt_type_names[22] =
{
    "empty",
    "thread",
    "vector",
    "notetrack",
    "anim tree",
    "small anim tree",
    "external",
    "temp",
    "surface",
    "anim part",
    "model part",
    "model part map",
    "duplicate parts",
    "model list",
    "script parse",
    "script string",
    "class",
    "tag info",
    "animscripted",
    "config string",
    "debugger string",
    "generic",
};

uint64_t mt_nextValidationLeaseSerial = 0;
uintptr_t mt_activeValidationLeaseAddress = 0;
uint64_t mt_activeValidationLeaseSerial = 0;
uintptr_t mt_activeValidationLeaseAddressMirror = 0;
uint64_t mt_activeValidationLeaseSerialMirror = 0;
MT_ValidationLeaseLifecycle mt_validationLeaseLifecycle =
    MT_ValidationLeaseLifecycle::Idle;
MT_ValidationLeaseLifecycle mt_validationLeaseLifecycleMirror =
    MT_ValidationLeaseLifecycle::Idle;
thread_local uintptr_t mt_retainedValidationLeaseAddress = 0;
thread_local uintptr_t mt_retainedValidationLeaseAddressMirror = 0;
thread_local uint64_t mt_retainedValidationLeaseSerial = 0;
thread_local uint64_t mt_retainedValidationLeaseSerialMirror = 0;

constexpr uint64_t kAbandonedValidationLeasePoison =
    UINT64_C(0x4D545F4142414E44);
constexpr uint64_t kAbandonedValidationLeasePoisonMirror =
    UINT64_C(0xB2ABA0BEBDBEB1BB);
uint64_t mt_abandonedValidationLeasePoison = 0;
uint64_t mt_abandonedValidationLeasePoisonMirror = 0;

bool MT_IsValidationLeaseBoundaryFrozenLocked() noexcept;
bool MT_HasRetainedValidationLeaseAuthenticationLocked() noexcept;
bool MT_HasValidationLeaseRegistryActivityLocked() noexcept;
bool MT_IsValidationLeaseRegistryConsistentLocked() noexcept;
#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
bool MT_RegistryNamesValidationLeaseStorageLocked(
    const MT_ValidationLease *lease) noexcept;
#endif
bool MT_CanReadValidationLeaseSnapshotLocked(
    const MT_ValidationLease *lease) noexcept;
bool MT_OwnsValidationLeaseLocked(
    const MT_ValidationLease *lease) noexcept;
bool MT_IsValidationLeasePoisonedLocked(
    const MT_ValidationLease *lease) noexcept;
void MT_ClearValidationLeaseRegistryLocked() noexcept;
void MT_ClearRetainedValidationLeaseAuthenticationLocked() noexcept;
void MT_FreezeValidationLeaseBoundaryLocked() noexcept;
void MT_PoisonValidationLeaseLocked(
    MT_ValidationLease *lease) noexcept;
void MT_PoisonActiveValidationLeaseLocked() noexcept;
bool MT_RejectUnleasedAccessForActiveLeaseLocked() noexcept;
}

// Bounded legacy operations cannot afford complete 64K-bucket partition or
// fragmented-forest scans on every string operation. These lock-protected
// allocation, membership, topology, and accounting mirrors authenticate every
// piece of ownership state a bounded operation touches. Legitimate
// init/alloc/free mutations update primary and mirror values together, so
// cleared or forged metadata fails closed with O(1) authentication per entry
// inspected along the touched interval or tree path.
static uint16_t mt_allocationMetadataShadow[MEMORY_NODE_COUNT];
static uint8_t mt_freeNodeSizeShadow[MEMORY_NODE_COUNT];

struct MT_FreeNodeLinkShadow final
{
    uint16_t prev;
    uint16_t next;
};
RUNTIME_SIZE(MT_FreeNodeLinkShadow, 0x4, 0x4);

// The free-node treap is the only allocator ownership structure whose links
// live in storage later handed to callers. Keep a compact authenticated copy
// of every head and link so bounded legacy operations need only compare the
// paths they will consume or change. Complete typed/global validation still
// walks the entire primary forest and partition.
static uint16_t mt_freeTreeHeadShadow[MEMORY_NODE_BITS + 1];
static MT_FreeNodeLinkShadow mt_freeNodeLinkShadow[MEMORY_NODE_COUNT];
static uint32_t mt_freeNodeCountShadow;
static uint32_t mt_freeNodeCountMirror;
static int mt_totalAllocShadow;
static int mt_totalAllocBucketsShadow;

constexpr uint16_t MT_PackAllocationMetadata(
    const uint8_t type,
    const uint8_t size) noexcept
{
    return static_cast<uint16_t>(type) |
        static_cast<uint16_t>(static_cast<uint16_t>(size) << 8);
}

static void MT_InitBits(void)
{
    uint8_t bits; // [esp+0h] [ebp-Ch]
    int temp; // [esp+4h] [ebp-8h]

    for (int i = 0; i < NUM_BUCKETS; ++i)
    {
        bits = 0;
        for (temp = i; temp; temp >>= 1)
        {
            if ((temp & 1) != 0)
                ++bits;
        }
        scrMemTreeGlob.numBits[i] = bits;

        for (bits = 8; (i & ((1 << bits) - 1)) != 0; --bits);

        scrMemTreeGlob.leftBits[i] = bits;
        bits = 0;
        for (temp = i; temp; temp >>= 1)
        {
            ++bits;
        }
        scrMemTreeGlob.logBits[i] = bits;
    }
}

void MT_Init()
{
	Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
	// A same-thread recursive reset would invalidate every allocation certified
	// by the retained lease. Pointer OR serial activity is enough to reject so a
	// torn registry cannot turn a reset into an unauthenticated mutation.
	if (MT_RejectUnleasedAccessForActiveLeaseLocked())
	{
		Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
		return;
	}

    scrMemTreePub.mt_buffer = (char*)&scrMemTreeGlob.nodes;
    MT_InitBits();

    for (int i = 0; i <= MEMORY_NODE_BITS; ++i)
        scrMemTreeGlob.head[i] = 0;

    scrMemTreeGlob.nodes[0].prev = 0;
    scrMemTreeGlob.nodes[0].next = 0;

    memset(scrMemTreeDebugGlob.mt_usage, 0, sizeof(scrMemTreeDebugGlob.mt_usage));
    memset(scrMemTreeDebugGlob.mt_usage_size, 0, sizeof(scrMemTreeDebugGlob.mt_usage_size));
    memset(mt_allocationMetadataShadow, 0, sizeof(mt_allocationMetadataShadow));
    memset(mt_freeNodeSizeShadow, 0, sizeof(mt_freeNodeSizeShadow));
    memset(mt_freeTreeHeadShadow, 0, sizeof(mt_freeTreeHeadShadow));
    memset(mt_freeNodeLinkShadow, 0, sizeof(mt_freeNodeLinkShadow));

    scrMemTreeGlob.totalAlloc = 0;
    scrMemTreeGlob.totalAllocBuckets = 0;
    mt_freeNodeCountShadow = 0;
    mt_freeNodeCountMirror = 0;
    mt_totalAllocShadow = 0;
    mt_totalAllocBucketsShadow = 0;
    for (int i = 0; i < MEMORY_NODE_BITS; ++i)
    {
        MT_AddMemoryNode(1 << i, i);
    }

	Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void* MT_Alloc(int numBytes, int type)
{
    const uint16_t nodeNum = MT_AllocIndex(numBytes, type);
    return nodeNum ? &scrMemTreeGlob.nodes[nodeNum] : nullptr;
}

namespace
{
constexpr int kMemoryTreeTypeCount =
    static_cast<int>(sizeof(mt_type_names) / sizeof(mt_type_names[0]));

struct MT_DumpSnapshot final
{
    uint8_t types[MEMORY_NODE_COUNT];
    uint8_t sizes[MEMORY_NODE_COUNT];
    int typeUsage[kMemoryTreeTypeCount];
    int subTreeSizes[MEMORY_NODE_BITS + 1];
    int totalAlloc;
    int totalAllocBuckets;
    int totalBuckets;
};

enum class MT_DumpStatus : uint8_t
{
    Success,
    BoundaryUnavailable,
    ScratchUnavailable,
    UnsafeState,
};

// Dump emission cannot retain CRITSECT_MEMORY_TREE across Com_Printf or an
// assertion. Capture one authenticated image into fixed BSS instead. A
// concurrent or nonlocally-abandoned reporter leaves this scratch unavailable
// rather than exposing a partially overwritten image.
MT_DumpSnapshot mt_dumpSnapshot{};
std::atomic_flag mt_dumpSnapshotInUse = ATOMIC_FLAG_INIT;

MT_DumpStatus MT_DumpTreeInternal() noexcept;

int MT_GetScoreFromDeltaNoReport(const int value) noexcept
{
    const uint32_t representation = static_cast<uint32_t>(value);
    const uint8_t lowByte = static_cast<uint8_t>(representation);
    const uint8_t highByte = static_cast<uint8_t>(representation >> 8);
    uint8_t bits = scrMemTreeGlob.leftBits[lowByte];
    if (lowByte == 0)
        bits = static_cast<uint8_t>(
            bits + scrMemTreeGlob.leftBits[highByte]);
    return value
        - (scrMemTreeGlob.numBits[highByte]
            + scrMemTreeGlob.numBits[lowByte])
        + (1 << bits);
}

int MT_GetScoreNoReport(const uint16_t nodeNum) noexcept
{
    return MT_GetScoreFromDeltaNoReport(
        MEMORY_NODE_COUNT - static_cast<int>(nodeNum));
}

int MT_GetSubTreeSizeNoReport(const int nodeNum) noexcept
{
    if (!nodeNum)
        return 0;

    return MT_GetSubTreeSizeNoReport(scrMemTreeGlob.nodes[nodeNum].prev)
        + MT_GetSubTreeSizeNoReport(scrMemTreeGlob.nodes[nodeNum].next)
        + 1;
}

bool MT_TryGetSizeNoReport(int numBytes, int *outSize) noexcept
{
    if (!outSize || numBytes <= 0 || numBytes >= MEMORY_NODE_COUNT)
    {
        return false;
    }

    const uint32_t bucketCount =
        (static_cast<uint32_t>(numBytes) + sizeof(MemoryNode) - 1u) / sizeof(MemoryNode);
    uint32_t bucketCapacity = 1;
    int size = 0;
    while (bucketCapacity < bucketCount)
    {
        bucketCapacity <<= 1;
        ++size;
    }

    if (size > MEMORY_NODE_BITS)
    {
        return false;
    }

    *outSize = size;
    return true;
}

bool MT_AreBitTablesValidNoReport() noexcept
{
    for (int value = 0; value < NUM_BUCKETS; ++value)
    {
        uint8_t numBits = 0;
        for (int remaining = value; remaining; remaining >>= 1)
        {
            numBits += static_cast<uint8_t>(remaining & 1);
        }

        uint8_t leftBits = 8;
        if (value != 0)
        {
            leftBits = 0;
            for (int remaining = value; (remaining & 1) == 0; remaining >>= 1)
            {
                ++leftBits;
            }
        }

        uint8_t logBits = 0;
        for (int remaining = value; remaining; remaining >>= 1)
        {
            ++logBits;
        }

        if (scrMemTreeGlob.numBits[value] != numBits ||
            scrMemTreeGlob.leftBits[value] != leftBits ||
            scrMemTreeGlob.logBits[value] != logBits)
        {
            return false;
        }
    }

    return true;
}

bool MT_AreFreeTreeHeadsAuthenticatedNoReport() noexcept;

bool MT_IsBasicAccountingStateValidNoReport() noexcept
{
    return scrMemTreePub.mt_buffer
            == reinterpret_cast<char *>(&scrMemTreeGlob.nodes) &&
        scrMemTreeGlob.nodes[0].prev == 0 &&
        scrMemTreeGlob.nodes[0].next == 0 &&
        scrMemTreeDebugGlob.mt_usage[0] == 0 &&
        scrMemTreeDebugGlob.mt_usage_size[0] == 0 &&
        mt_allocationMetadataShadow[0] == 0 &&
        mt_freeNodeSizeShadow[0] == 0 &&
        mt_freeNodeLinkShadow[0].prev == 0 &&
        mt_freeNodeLinkShadow[0].next == 0 &&
        MT_AreFreeTreeHeadsAuthenticatedNoReport() &&
        mt_freeNodeCountShadow == mt_freeNodeCountMirror &&
        mt_freeNodeCountShadow <= MEMORY_NODE_COUNT - 1 &&
        scrMemTreeGlob.totalAlloc == mt_totalAllocShadow &&
        scrMemTreeGlob.totalAllocBuckets == mt_totalAllocBucketsShadow &&
        scrMemTreeGlob.totalAlloc >= 0 &&
        scrMemTreeGlob.totalAlloc < MEMORY_NODE_COUNT &&
        scrMemTreeGlob.totalAllocBuckets >= 0 &&
        scrMemTreeGlob.totalAllocBuckets <= MEMORY_NODE_COUNT - 1 &&
        scrMemTreeGlob.totalAlloc <= scrMemTreeGlob.totalAllocBuckets &&
        ((scrMemTreeGlob.totalAlloc == 0) ==
         (scrMemTreeGlob.totalAllocBuckets == 0));
}

bool MT_IsBasicCoreStateValidNoReport() noexcept
{
    return MT_IsBasicAccountingStateValidNoReport()
        && MT_AreBitTablesValidNoReport();
}

bool MT_IsFreeTreeForestValidNoReport() noexcept;

bool MT_IsValidNodeRangeNoReport(uint32_t nodeNum, int size) noexcept
{
    if (size < 0 || size > MEMORY_NODE_BITS || nodeNum == 0 ||
        nodeNum >= MEMORY_NODE_COUNT)
    {
        return false;
    }

    const uint32_t blockSize = 1u << size;
    if ((nodeNum & (blockSize - 1u)) != 0 || nodeNum > MEMORY_NODE_COUNT - blockSize)
    {
        return false;
    }

    return true;
}

bool MT_IsFreeNodeMetadataClearNoReport(
    const uint32_t nodeNum,
    const int size) noexcept
{
    if (!MT_IsValidNodeRangeNoReport(nodeNum, size))
    {
        return false;
    }

    return scrMemTreeDebugGlob.mt_usage[nodeNum] == 0 &&
        scrMemTreeDebugGlob.mt_usage_size[nodeNum] == 0 &&
        mt_allocationMetadataShadow[nodeNum] == 0;
}

bool MT_IsValidFreeNodeNoReport(uint32_t nodeNum, int size) noexcept
{
    return MT_IsFreeNodeMetadataClearNoReport(nodeNum, size) &&
        mt_freeNodeSizeShadow[nodeNum] ==
            static_cast<uint8_t>(size + 1);
}

bool MT_IsValidUnlinkedFreeIntervalNoReport(
    const uint32_t nodeNum,
    const int size) noexcept
{
    return MT_IsFreeNodeMetadataClearNoReport(nodeNum, size) &&
        mt_freeNodeSizeShadow[nodeNum] == 0;
}

bool MT_AreFreeTreeHeadsAuthenticatedNoReport() noexcept
{
    uint32_t nonEmptyHeadCount = 0;
    for (int size = 0; size <= MEMORY_NODE_BITS; ++size)
    {
        const uint16_t root = scrMemTreeGlob.head[size];
        if (root != mt_freeTreeHeadShadow[size] ||
            (root != 0 && !MT_IsValidFreeNodeNoReport(root, size)))
        {
            return false;
        }
        nonEmptyHeadCount += root != 0 ? 1u : 0u;
    }
    return nonEmptyHeadCount <= mt_freeNodeCountShadow;
}

bool MT_AreFreeNodeLinksAuthenticatedNoReport(
    const uint32_t nodeNum,
    const int size) noexcept
{
    if (!MT_IsValidFreeNodeNoReport(nodeNum, size))
        return false;

    const MemoryNode &node = scrMemTreeGlob.nodes[nodeNum];
    const MT_FreeNodeLinkShadow &shadow =
        mt_freeNodeLinkShadow[nodeNum];
    if (node.prev != shadow.prev || node.next != shadow.next)
        return false;
    if (node.prev == nodeNum || node.next == nodeNum ||
        (node.prev != 0 && node.prev == node.next))
    {
        return false;
    }

    return (node.prev == 0 || MT_IsValidFreeNodeNoReport(node.prev, size)) &&
        (node.next == 0 || MT_IsValidFreeNodeNoReport(node.next, size));
}

bool MT_HasAllocationAncestorNoReport(
    const uint32_t nodeNum,
    const int size) noexcept
{
    for (int ancestorSize = size + 1;
         ancestorSize <= MEMORY_NODE_BITS;
         ++ancestorSize)
    {
        const uint32_t ancestorBuckets = 1u << ancestorSize;
        const uint32_t ancestorNode =
            nodeNum & ~(ancestorBuckets - 1u);
        if (ancestorNode == 0 || ancestorNode == nodeNum)
            continue;

        const uint8_t ancestorType =
            scrMemTreeDebugGlob.mt_usage[ancestorNode];
        const uint8_t allocationSize =
            scrMemTreeDebugGlob.mt_usage_size[ancestorNode];
        const uint16_t shadowMetadata =
            mt_allocationMetadataShadow[ancestorNode];
        if (shadowMetadata !=
            MT_PackAllocationMetadata(ancestorType, allocationSize))
        {
            return true;
        }
        if (shadowMetadata == 0)
            continue;
        if (ancestorType == 0 || ancestorType >= kMemoryTreeTypeCount ||
            allocationSize > MEMORY_NODE_BITS ||
            !MT_IsValidNodeRangeNoReport(ancestorNode, allocationSize) ||
            ancestorNode + (1u << allocationSize) > nodeNum)
        {
            return true;
        }
    }
    return false;
}

bool MT_IsAllocationIntervalClearNoReport(
    const uint32_t nodeNum,
    const int size) noexcept
{
    if (!MT_IsValidNodeRangeNoReport(nodeNum, size) ||
        MT_HasAllocationAncestorNoReport(nodeNum, size))
    {
        return false;
    }

    const uint32_t rangeEnd = nodeNum + (1u << size);
    for (uint32_t bucket = nodeNum; bucket < rangeEnd; ++bucket)
    {
        if (scrMemTreeDebugGlob.mt_usage[bucket] != 0 ||
            scrMemTreeDebugGlob.mt_usage_size[bucket] != 0 ||
            mt_allocationMetadataShadow[bucket] != 0)
        {
            return false;
        }
    }
    return true;
}

bool MT_IsAllocationIntervalExactNoReport(
    const uint32_t nodeNum,
    const uint8_t type,
    const uint8_t size) noexcept
{
    if (type == 0 || type >= kMemoryTreeTypeCount ||
        !MT_IsValidNodeRangeNoReport(nodeNum, size) ||
        scrMemTreeDebugGlob.mt_usage[nodeNum] != type ||
        scrMemTreeDebugGlob.mt_usage_size[nodeNum] != size ||
        mt_allocationMetadataShadow[nodeNum] !=
            MT_PackAllocationMetadata(type, size) ||
        MT_HasAllocationAncestorNoReport(nodeNum, size))
    {
        return false;
    }

    const uint32_t rangeEnd = nodeNum + (1u << size);
    for (uint32_t bucket = nodeNum + 1; bucket < rangeEnd; ++bucket)
    {
        if (scrMemTreeDebugGlob.mt_usage[bucket] != 0 ||
            scrMemTreeDebugGlob.mt_usage_size[bucket] != 0 ||
            mt_allocationMetadataShadow[bucket] != 0)
        {
            return false;
        }
    }
    return true;
}

MT_AllocationInfoStatus MT_GetAllocationInfoLockedNoReport(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo) noexcept
{
    const uint8_t type = scrMemTreeDebugGlob.mt_usage[nodeNum];
    const uint8_t size = scrMemTreeDebugGlob.mt_usage_size[nodeNum];
    if (mt_allocationMetadataShadow[nodeNum] !=
        MT_PackAllocationMetadata(type, size))
    {
        return MT_AllocationInfoStatus::UnsafeFailure;
    }
    if (type == 0)
    {
        return size == 0
            ? MT_AllocationInfoStatus::NotAllocatedNoChange
            : MT_AllocationInfoStatus::UnsafeFailure;
    }
    if (type >= kMemoryTreeTypeCount || size > MEMORY_NODE_BITS ||
        !MT_IsValidNodeRangeNoReport(nodeNum, size))
    {
        return MT_AllocationInfoStatus::UnsafeFailure;
    }

    const int allocationBuckets = 1 << size;
    if (scrMemTreeGlob.totalAlloc == 0 ||
        scrMemTreeGlob.totalAllocBuckets < allocationBuckets)
    {
        return MT_AllocationInfoStatus::UnsafeFailure;
    }

    *outInfo = {
        type,
        size,
        0,
        static_cast<uint32_t>(allocationBuckets) *
            static_cast<uint32_t>(sizeof(MemoryNode)),
    };
    return MT_AllocationInfoStatus::Success;
}

constexpr uint32_t kPartitionWordBits = 64;
constexpr uint32_t kPartitionWordCount =
    MEMORY_NODE_COUNT / kPartitionWordBits;

// The allocator stores ownership only at allocation starts, while free-tree
// links occupy the first node of every free interval. Validate both views into
// one complete bucket partition before trusting either. All workspace is
// protected by CRITSECT_MEMORY_TREE and is deliberately outside allocator
// ownership state so a rejected operation remains failure-atomic.
uint64_t mt_partitionBuckets[kPartitionWordCount];
uint64_t mt_partitionFreeNodes[kPartitionWordCount];
uint16_t mt_partitionStack[MEMORY_NODE_COUNT - 1];
#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
uint32_t mt_completeValidationCount = 0;
uint32_t mt_completeForestValidationCount = 0;
#endif

bool MT_TryMarkPartitionRangeNoReport(
    const uint32_t nodeNum,
    const int size) noexcept
{
    if (!MT_IsValidNodeRangeNoReport(nodeNum, size))
    {
        return false;
    }

    const uint32_t rangeBegin = nodeNum;
    const uint32_t rangeEnd = nodeNum + (1u << size);
    const uint32_t firstWord = rangeBegin / kPartitionWordBits;
    const uint32_t lastWord = (rangeEnd - 1u) / kPartitionWordBits;
    for (uint32_t wordIndex = firstWord;
         wordIndex <= lastWord;
         ++wordIndex)
    {
        const uint32_t lowBit = wordIndex == firstWord
            ? rangeBegin % kPartitionWordBits
            : 0;
        const uint32_t highBit = wordIndex == lastWord
            ? ((rangeEnd - 1u) % kPartitionWordBits) + 1u
            : kPartitionWordBits;
        const uint64_t lowMask = UINT64_MAX << lowBit;
        const uint64_t highMask = highBit == kPartitionWordBits
            ? UINT64_MAX
            : (UINT64_C(1) << highBit) - UINT64_C(1);
        const uint64_t rangeMask = lowMask & highMask;
        if ((mt_partitionBuckets[wordIndex] & rangeMask) != 0)
        {
            return false;
        }
        mt_partitionBuckets[wordIndex] |= rangeMask;
    }
    return true;
}

bool MT_TryRecordPartitionFreeNodeNoReport(
    const uint16_t nodeNum) noexcept
{
    if (nodeNum == 0)
    {
        return true;
    }

    const uint32_t wordIndex = nodeNum / kPartitionWordBits;
    const uint64_t nodeMask =
        UINT64_C(1) << (nodeNum % kPartitionWordBits);
    if ((mt_partitionFreeNodes[wordIndex] & nodeMask) != 0)
    {
        return false;
    }
    mt_partitionFreeNodes[wordIndex] |= nodeMask;
    return true;
}

bool MT_IsGlobalPartitionValidNoReport() noexcept
{
#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
    ++mt_completeValidationCount;
#endif
    memset(mt_partitionBuckets, 0, sizeof(mt_partitionBuckets));
    memset(mt_partitionFreeNodes, 0, sizeof(mt_partitionFreeNodes));

    // Bucket zero is the permanent sentinel and must be the only interval not
    // represented by allocation metadata or a free-tree node.
    mt_partitionBuckets[0] = UINT64_C(1);

    uint32_t allocationCount = 0;
    uint32_t allocationBuckets = 0;
    for (uint32_t nodeNum = 1; nodeNum < MEMORY_NODE_COUNT; ++nodeNum)
    {
        const uint8_t type = scrMemTreeDebugGlob.mt_usage[nodeNum];
        const uint8_t size = scrMemTreeDebugGlob.mt_usage_size[nodeNum];
        if (mt_allocationMetadataShadow[nodeNum] !=
            MT_PackAllocationMetadata(type, size))
        {
            return false;
        }
        if (type == 0)
        {
            if (size != 0)
            {
                return false;
            }
            continue;
        }
        if (type >= kMemoryTreeTypeCount || size > MEMORY_NODE_BITS ||
            !MT_TryMarkPartitionRangeNoReport(nodeNum, size))
        {
            return false;
        }

        ++allocationCount;
        allocationBuckets += 1u << size;
        if (allocationCount >= MEMORY_NODE_COUNT ||
            allocationBuckets >= MEMORY_NODE_COUNT)
        {
            return false;
        }
    }
    if (allocationCount !=
            static_cast<uint32_t>(scrMemTreeGlob.totalAlloc) ||
        allocationBuckets !=
            static_cast<uint32_t>(scrMemTreeGlob.totalAllocBuckets))
    {
        return false;
    }

    uint32_t stackSize = 0;
    uint32_t freeNodeCount = 0;
    for (int size = 0; size <= MEMORY_NODE_BITS; ++size)
    {
        const uint16_t root = scrMemTreeGlob.head[size];
        if (root == 0)
        {
            continue;
        }
        if (!MT_TryRecordPartitionFreeNodeNoReport(root) ||
            stackSize >= MEMORY_NODE_COUNT - 1)
        {
            return false;
        }
        mt_partitionStack[stackSize++] = root;

        while (stackSize != 0)
        {
            const uint16_t nodeNum = mt_partitionStack[--stackSize];
            if (!MT_AreFreeNodeLinksAuthenticatedNoReport(nodeNum, size) ||
                !MT_TryMarkPartitionRangeNoReport(nodeNum, size))
            {
                return false;
            }
            ++freeNodeCount;

            const MemoryNode &node = scrMemTreeGlob.nodes[nodeNum];
            const uint16_t children[2] = {node.prev, node.next};
            for (const uint16_t child : children)
            {
                if (child == 0)
                {
                    continue;
                }
                if (!MT_TryRecordPartitionFreeNodeNoReport(child) ||
                    stackSize >= MEMORY_NODE_COUNT - 1)
                {
                    return false;
                }
                mt_partitionStack[stackSize++] = child;
            }
        }
    }

    if (freeNodeCount != mt_freeNodeCountShadow)
        return false;

    for (uint32_t nodeNum = 1; nodeNum < MEMORY_NODE_COUNT; ++nodeNum)
    {
        const bool recorded =
            (mt_partitionFreeNodes[nodeNum / kPartitionWordBits] &
             (UINT64_C(1) << (nodeNum % kPartitionWordBits))) != 0;
        const uint8_t membership = mt_freeNodeSizeShadow[nodeNum];
        if (recorded != (membership != 0) ||
            membership > MEMORY_NODE_BITS + 1 ||
            (!recorded &&
             (mt_freeNodeLinkShadow[nodeNum].prev != 0 ||
              mt_freeNodeLinkShadow[nodeNum].next != 0)))
        {
            return false;
        }
    }

    for (const uint64_t partitionWord : mt_partitionBuckets)
    {
        if (partitionWord != UINT64_MAX)
        {
            return false;
        }
    }
    return true;
}

bool MT_IsCoreStateValidNoReport() noexcept
{
    return MT_IsBasicCoreStateValidNoReport() &&
        MT_IsFreeTreeForestValidNoReport() &&
        MT_IsGlobalPartitionValidNoReport();
}

bool MT_TryCaptureDumpSnapshotLocked() noexcept
{
    if (!MT_IsCoreStateValidNoReport())
        return false;

    memset(mt_dumpSnapshot.typeUsage, 0,
        sizeof(mt_dumpSnapshot.typeUsage));
    int capturedAlloc = 0;
    int capturedAllocBuckets = 0;
    for (uint32_t nodeNum = 0;
         nodeNum < MEMORY_NODE_COUNT;
         ++nodeNum)
    {
        const uint8_t type = scrMemTreeDebugGlob.mt_usage[nodeNum];
        const uint8_t size = scrMemTreeDebugGlob.mt_usage_size[nodeNum];
        mt_dumpSnapshot.types[nodeNum] = type;
        mt_dumpSnapshot.sizes[nodeNum] = size;
        if (type == 0)
            continue;

        const int buckets = 1 << size;
        ++capturedAlloc;
        capturedAllocBuckets += buckets;
        mt_dumpSnapshot.typeUsage[type] += buckets;
    }

    int capturedTotalBuckets = capturedAllocBuckets;
    for (int size = 0; size <= MEMORY_NODE_BITS; ++size)
    {
        const int subTreeSize =
            MT_GetSubTreeSizeNoReport(scrMemTreeGlob.head[size]);
        mt_dumpSnapshot.subTreeSizes[size] = subTreeSize;
        capturedTotalBuckets += subTreeSize * (1 << size);
    }

    if (capturedAlloc != scrMemTreeGlob.totalAlloc
        || capturedAllocBuckets != scrMemTreeGlob.totalAllocBuckets
        || capturedTotalBuckets != MEMORY_NODE_COUNT - 1)
    {
        return false;
    }

    mt_dumpSnapshot.totalAlloc = capturedAlloc;
    mt_dumpSnapshot.totalAllocBuckets = capturedAllocBuckets;
    mt_dumpSnapshot.totalBuckets = capturedTotalBuckets;
    return true;
}

bool MT_HasValidationLeaseRegistryActivityLocked() noexcept
{
    return mt_activeValidationLeaseAddress != 0
        || mt_activeValidationLeaseSerial != 0
        || mt_activeValidationLeaseAddressMirror != 0
        || mt_activeValidationLeaseSerialMirror != 0
        || mt_validationLeaseLifecycle
            != MT_ValidationLeaseLifecycle::Idle
        || mt_validationLeaseLifecycleMirror
            != MT_ValidationLeaseLifecycle::Idle
        || MT_HasRetainedValidationLeaseAuthenticationLocked()
        || MT_IsValidationLeaseBoundaryFrozenLocked();
}

bool MT_IsValidationLeaseBoundaryFrozenLocked() noexcept
{
    // Either word is terminal activity. A torn poison publication therefore
    // fails closed instead of reopening allocator state.
    return mt_abandonedValidationLeasePoison != 0
        || mt_abandonedValidationLeasePoisonMirror != 0
        || mt_validationLeaseLifecycle
            == MT_ValidationLeaseLifecycle::Frozen
        || mt_validationLeaseLifecycleMirror
            == MT_ValidationLeaseLifecycle::Frozen;
}

bool MT_HasRetainedValidationLeaseAuthenticationLocked() noexcept
{
    return mt_retainedValidationLeaseAddress != 0
        || mt_retainedValidationLeaseAddressMirror != 0
        || mt_retainedValidationLeaseSerial != 0
        || mt_retainedValidationLeaseSerialMirror != 0;
}

bool MT_IsValidationLeaseRegistryConsistentLocked() noexcept
{
    if (MT_IsValidationLeaseBoundaryFrozenLocked())
        return false;

    if (!MT_HasValidationLeaseRegistryActivityLocked())
        return true;

    if (mt_activeValidationLeaseAddress == 0
        || mt_activeValidationLeaseSerial == 0
        || mt_activeValidationLeaseAddressMirror == 0
        || mt_activeValidationLeaseSerialMirror == 0
        || mt_activeValidationLeaseAddress
            != mt_activeValidationLeaseAddressMirror
        || mt_activeValidationLeaseSerial
            != mt_activeValidationLeaseSerialMirror
        || mt_validationLeaseLifecycle
            != mt_validationLeaseLifecycleMirror
        || (mt_validationLeaseLifecycle
                != MT_ValidationLeaseLifecycle::Active
            && mt_validationLeaseLifecycle
                != MT_ValidationLeaseLifecycle::Poisoned)
        || mt_retainedValidationLeaseAddress
            != mt_activeValidationLeaseAddress
        || mt_retainedValidationLeaseAddressMirror
            != mt_activeValidationLeaseAddressMirror
        || mt_retainedValidationLeaseSerial
            != mt_activeValidationLeaseSerial
        || mt_retainedValidationLeaseSerialMirror
            != mt_activeValidationLeaseSerialMirror)
    {
        return false;
    }

    // Registry consistency is deliberately by value. Stored addresses are
    // compared only with an explicit live lease argument in MT_Owns...; generic
    // unleased and reset paths never dereference stack-owned registry storage.
    return true;
}

bool MT_CanReadValidationLeaseSnapshotLocked(
    const MT_ValidationLease *const lease) noexcept
{
    // The address/registry checks inside MT_Owns... precede every token member
    // read. A waiter that wakes after abandonment therefore rejects dead
    // storage, while a live token with torn local authentication fails closed.
    return MT_OwnsValidationLeaseLocked(lease);
}

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
bool MT_RegistryNamesValidationLeaseStorageLocked(
    const MT_ValidationLease *const lease) noexcept
{
    if (!lease || MT_IsValidationLeaseBoundaryFrozenLocked())
        return false;

    const uintptr_t leaseAddress = reinterpret_cast<uintptr_t>(lease);
    return mt_activeValidationLeaseAddress == leaseAddress
        && mt_activeValidationLeaseAddressMirror == leaseAddress
        && MT_IsValidationLeaseRegistryConsistentLocked();
}
#endif

bool MT_OwnsValidationLeaseLocked(
    const MT_ValidationLease *const lease) noexcept
{
    if (!lease)
        return false;

    const uintptr_t leaseAddress = reinterpret_cast<uintptr_t>(lease);
    if (mt_activeValidationLeaseAddress != leaseAddress
        || mt_activeValidationLeaseAddressMirror != leaseAddress
        || !MT_IsValidationLeaseRegistryConsistentLocked())
    {
        return false;
    }

    return MT_ValidationLeaseAccess::Active(*lease)
        && MT_ValidationLeaseAccess::Serial(*lease) != 0
        && MT_ValidationLeaseAccess::Serial(*lease)
            == mt_activeValidationLeaseSerial
        && MT_ValidationLeaseAccess::ReservedClear(*lease);
}

bool MT_IsValidationLeasePoisonedLocked(
    const MT_ValidationLease *const lease) noexcept
{
    if (!MT_OwnsValidationLeaseLocked(lease))
        return true;

    return MT_ValidationLeaseAccess::Poisoned(*lease)
        || mt_validationLeaseLifecycle
            == MT_ValidationLeaseLifecycle::Poisoned;
}

void MT_ClearValidationLeaseRegistryLocked() noexcept
{
    mt_activeValidationLeaseAddress = 0;
    mt_activeValidationLeaseSerial = 0;
    mt_activeValidationLeaseAddressMirror = 0;
    mt_activeValidationLeaseSerialMirror = 0;
    mt_validationLeaseLifecycle = MT_ValidationLeaseLifecycle::Idle;
    mt_validationLeaseLifecycleMirror = MT_ValidationLeaseLifecycle::Idle;
}

void MT_ClearRetainedValidationLeaseAuthenticationLocked() noexcept
{
    mt_retainedValidationLeaseAddress = 0;
    mt_retainedValidationLeaseAddressMirror = 0;
    mt_retainedValidationLeaseSerial = 0;
    mt_retainedValidationLeaseSerialMirror = 0;
}

void MT_FreezeValidationLeaseBoundaryLocked() noexcept
{
    mt_abandonedValidationLeasePoison =
        kAbandonedValidationLeasePoison;
    mt_abandonedValidationLeasePoisonMirror =
        kAbandonedValidationLeasePoisonMirror;
    mt_activeValidationLeaseAddress = 0;
    mt_activeValidationLeaseSerial = 0;
    mt_activeValidationLeaseAddressMirror = 0;
    mt_activeValidationLeaseSerialMirror = 0;
    mt_validationLeaseLifecycle = MT_ValidationLeaseLifecycle::Frozen;
    mt_validationLeaseLifecycleMirror =
        MT_ValidationLeaseLifecycle::Frozen;
}

void MT_PoisonValidationLeaseLocked(
    MT_ValidationLease *const lease) noexcept
{
    if (MT_OwnsValidationLeaseLocked(lease))
    {
        MT_ValidationLeaseAccess::Poison(*lease);
        mt_validationLeaseLifecycle =
            MT_ValidationLeaseLifecycle::Poisoned;
        mt_validationLeaseLifecycleMirror =
            MT_ValidationLeaseLifecycle::Poisoned;
    }
}

void MT_PoisonActiveValidationLeaseLocked() noexcept
{
    // Generic callers never dereference the stored address. They poison the
    // by-value registry so a longjmp, abandoned stack frame, or torn token
    // cannot turn rejection into a write through dead storage.
    if (MT_HasValidationLeaseRegistryActivityLocked()
        && MT_IsValidationLeaseRegistryConsistentLocked())
    {
        mt_validationLeaseLifecycle =
            MT_ValidationLeaseLifecycle::Poisoned;
        mt_validationLeaseLifecycleMirror =
            MT_ValidationLeaseLifecycle::Poisoned;
    }
}

bool MT_RejectUnleasedAccessForActiveLeaseLocked() noexcept
{
    const bool active = MT_HasValidationLeaseRegistryActivityLocked();
    if (active)
        MT_PoisonActiveValidationLeaseLocked();
    return active;
}

MT_PolicyEntryStatus MT_ValidatePolicyEntryLocked(
    const MT_ValidationPolicy policy,
    MT_ValidationLease *const lease,
    const bool query) noexcept
{
    if (policy == MT_ValidationPolicy::Leased)
    {
        if (!MT_OwnsValidationLeaseLocked(lease))
            return MT_PolicyEntryStatus::InvalidLease;
        if (MT_IsValidationLeasePoisonedLocked(lease))
            return MT_PolicyEntryStatus::UnsafeFailure;

        const bool locallyValid = query
            ? MT_IsBasicAccountingStateValidNoReport()
            : MT_IsBasicCoreStateValidNoReport();
        if (!locallyValid)
        {
            MT_ValidationLeaseAccess::Poison(*lease);
            return MT_PolicyEntryStatus::UnsafeFailure;
        }
        return MT_PolicyEntryStatus::Success;
    }

    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
        return MT_PolicyEntryStatus::UnsafeFailure;

    const bool valid = policy == MT_ValidationPolicy::Complete
        ? MT_IsCoreStateValidNoReport()
        : (query
            ? MT_IsBasicAccountingStateValidNoReport()
            : MT_IsBasicCoreStateValidNoReport());
    return valid
        ? MT_PolicyEntryStatus::Success
        : MT_PolicyEntryStatus::UnsafeFailure;
}

bool MT_CanCommitPolicyMutationLocked(
    const MT_ValidationPolicy policy,
    MT_ValidationLease *const lease) noexcept
{
    if (policy != MT_ValidationPolicy::Leased)
        return true;
    if (!MT_OwnsValidationLeaseLocked(lease)
        || MT_IsValidationLeasePoisonedLocked(lease)
        || !MT_ValidationLeaseAccess::CanCountMutation(*lease))
    {
        MT_PoisonValidationLeaseLocked(lease);
        return false;
    }
    return true;
}

void MT_RecordPolicyMutationLocked(
    const MT_ValidationPolicy policy,
    MT_ValidationLease *const lease) noexcept
{
    if (policy == MT_ValidationPolicy::Leased && lease)
        MT_ValidationLeaseAccess::CountMutation(*lease);
}

void MT_PoisonPolicyLeaseLocked(
    const MT_ValidationPolicy policy,
    MT_ValidationLease *const lease) noexcept
{
    if (policy == MT_ValidationPolicy::Leased)
        MT_PoisonValidationLeaseLocked(lease);
}

void MT_UnsafeErrorNoDump(
    const char *const funcName,
    const int numBytes) noexcept
{
    // MT_DumpTree recursively follows untrusted links. UnsafeFailure means the
    // partition or a mutation path has already failed validation, so reporting
    // must not walk that state again.
    Com_Printf(
        23,
        "%s: unsafe memory-tree state while processing %d bytes\n",
        funcName,
        numBytes);
    Com_Error(ERR_FATAL, "MT_Error: unsafe memory-tree state (KISAK)\n");
}

// The public try entries hold CRITSECT_MEMORY_TREE while using this scratch.
// It is not allocator ownership state. The first map detects cycles within one
// path; the second rejects aliases between paths that will mutate in sequence.
uint8_t mt_preflightVisited[MEMORY_NODE_COUNT / 8];
uint8_t mt_preflightTransactionVisited[MEMORY_NODE_COUNT / 8];
uint16_t mt_preflightVisitedNodes[MEMORY_NODE_COUNT];
uint16_t mt_preflightTransactionNodes[MEMORY_NODE_COUNT];
uint32_t mt_preflightVisitedCount = 0;
uint32_t mt_preflightTransactionCount = 0;

void MT_ClearRecordedNodes(
    uint8_t *const visited,
    const uint16_t *const nodes,
    const uint32_t count) noexcept
{
    for (uint32_t index = 0; index < count; ++index)
    {
        const uint16_t nodeNum = nodes[index];
        visited[nodeNum >> 3] &= static_cast<uint8_t>(
            ~(1u << (nodeNum & 7u)));
    }
}

void MT_ResetPreflightNodes() noexcept
{
    MT_ClearRecordedNodes(
        mt_preflightVisited,
        mt_preflightVisitedNodes,
        mt_preflightVisitedCount);
    mt_preflightVisitedCount = 0;
}

void MT_ResetPreflightTransaction() noexcept
{
    MT_ClearRecordedNodes(
        mt_preflightTransactionVisited,
        mt_preflightTransactionNodes,
        mt_preflightTransactionCount);
    mt_preflightTransactionCount = 0;
}

bool MT_RecordScanNode(uint16_t nodeNum) noexcept
{
    const uint32_t byteIndex = nodeNum >> 3;
    const uint8_t bitMask = static_cast<uint8_t>(1u << (nodeNum & 7u));
    if ((mt_preflightVisited[byteIndex] & bitMask) != 0)
    {
        return false;
    }

    if (mt_preflightVisitedCount >= MEMORY_NODE_COUNT)
        return false;

    mt_preflightVisited[byteIndex] |= bitMask;
    mt_preflightVisitedNodes[mt_preflightVisitedCount++] = nodeNum;
    return true;
}

bool MT_RecordPreflightNode(uint16_t nodeNum) noexcept
{
    if (!MT_RecordScanNode(nodeNum))
    {
        return false;
    }

    const uint32_t byteIndex = nodeNum >> 3;
    const uint8_t bitMask = static_cast<uint8_t>(1u << (nodeNum & 7u));
    if ((mt_preflightTransactionVisited[byteIndex] & bitMask) != 0)
    {
        return false;
    }

    if (mt_preflightTransactionCount >= MEMORY_NODE_COUNT)
        return false;

    mt_preflightTransactionVisited[byteIndex] |= bitMask;
    mt_preflightTransactionNodes[mt_preflightTransactionCount++] = nodeNum;
    return true;
}

bool MT_ScanIsDisjointFromPreflightTransaction() noexcept
{
    for (uint32_t index = 0; index < mt_preflightVisitedCount; ++index)
    {
        const uint16_t nodeNum = mt_preflightVisitedNodes[index];
        const uint8_t bitMask =
            static_cast<uint8_t>(1u << (nodeNum & 7u));
        if ((mt_preflightTransactionVisited[nodeNum >> 3] & bitMask) != 0)
        {
            return false;
        }
    }
    return true;
}

bool MT_CommitScanToPreflightTransaction() noexcept
{
    if (!MT_ScanIsDisjointFromPreflightTransaction())
    {
        return false;
    }
    if (mt_preflightVisitedCount
        > MEMORY_NODE_COUNT - mt_preflightTransactionCount)
    {
        return false;
    }
    for (uint32_t index = 0; index < mt_preflightVisitedCount; ++index)
    {
        const uint16_t nodeNum = mt_preflightVisitedNodes[index];
        mt_preflightTransactionVisited[nodeNum >> 3] |=
            static_cast<uint8_t>(1u << (nodeNum & 7u));
        mt_preflightTransactionNodes[mt_preflightTransactionCount++] =
            nodeNum;
    }
    return true;
}

bool MT_IsFreeTreeForestValidNoReport() noexcept
{
#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
    ++mt_completeForestValidationCount;
#endif
    struct Frame final
    {
        uint16_t node;
        int32_t position;
        uint32_t level;
        uint32_t low;
        uint32_t high;
    };
    constexpr uint32_t kStackCapacity = MEMORY_NODE_BITS + 2;
    Frame stack[kStackCapacity]{};

    MT_ResetPreflightNodes();
    uint32_t reachableCount = 0;
    for (int size = 0; size <= MEMORY_NODE_BITS; ++size)
    {
        uint32_t stackSize = 0;
        const uint16_t root = scrMemTreeGlob.head[size];
        if (root != 0)
        {
            stack[stackSize++] = {
                root,
                0,
                MEMORY_NODE_COUNT,
                1,
                MEMORY_NODE_COUNT - 1,
            };
        }

        while (stackSize != 0)
        {
            const Frame frame = stack[--stackSize];
            const uint16_t nodeNum = frame.node;
            if (frame.low > frame.high || nodeNum < frame.low ||
                nodeNum > frame.high || frame.level == 0)
            {
                return false;
            }
            if (!MT_AreFreeNodeLinksAuthenticatedNoReport(nodeNum, size) ||
                !MT_RecordScanNode(nodeNum))
            {
                return false;
            }
            ++reachableCount;

            const MemoryNode &node = scrMemTreeGlob.nodes[nodeNum];
            const int parentScore = MT_GetScoreNoReport(nodeNum);
            const uint32_t nextLevel = frame.level >> 1;
            if (node.prev != 0)
            {
                if (nextLevel == 0 || frame.position <= 0 ||
                    frame.low > static_cast<uint32_t>(frame.position - 1) ||
                    parentScore <= MT_GetScoreNoReport(node.prev) ||
                    stackSize >= kStackCapacity)
                {
                    return false;
                }
                const uint32_t previousHigh =
                    frame.high < static_cast<uint32_t>(frame.position - 1)
                    ? frame.high
                    : static_cast<uint32_t>(frame.position - 1);
                stack[stackSize++] = {
                    node.prev,
                    frame.position - static_cast<int32_t>(nextLevel),
                    nextLevel,
                    frame.low,
                    previousHigh,
                };
            }
            if (node.next != 0)
            {
                if (nextLevel == 0 ||
                    frame.position >= MEMORY_NODE_COUNT - 1 ||
                    static_cast<uint32_t>(frame.position + 1) > frame.high ||
                    parentScore <= MT_GetScoreNoReport(node.next) ||
                    stackSize >= kStackCapacity)
                {
                    return false;
                }
                const uint32_t nextLow =
                    frame.low > static_cast<uint32_t>(frame.position + 1)
                    ? frame.low
                    : static_cast<uint32_t>(frame.position + 1);
                stack[stackSize++] = {
                    node.next,
                    frame.position + static_cast<int32_t>(nextLevel),
                    nextLevel,
                    nextLow,
                    frame.high,
                };
            }
        }
    }
    return reachableCount == mt_freeNodeCountShadow;
}

bool MT_CanRemoveHeadMemoryNodeNoReport(int size) noexcept
{
    MT_ResetPreflightNodes();
    uint16_t nodeNum = scrMemTreeGlob.head[size];
    bool foundNode = false;

    while (nodeNum != 0)
    {
        if (!MT_AreFreeNodeLinksAuthenticatedNoReport(nodeNum, size) ||
            !MT_RecordPreflightNode(nodeNum))
        {
            return false;
        }
        foundNode = true;

        const MemoryNode &node = scrMemTreeGlob.nodes[nodeNum];
        uint16_t nextNode = 0;
        if (node.prev != 0 && node.next != 0)
        {
            if (!MT_IsValidFreeNodeNoReport(node.prev, size) ||
                !MT_IsValidFreeNodeNoReport(node.next, size))
            {
                return false;
            }

            const int prevScore = MT_GetScoreNoReport(node.prev);
            const int nextScore = MT_GetScoreNoReport(node.next);
            if (prevScore == nextScore)
            {
                return false;
            }
            nextNode = prevScore >= nextScore ? node.prev : node.next;
        }
        else
        {
            nextNode = node.prev != 0 ? node.prev : node.next;
        }
        nodeNum = nextNode;
    }

    return foundNode;
}

bool MT_CanAddMemoryNodeNoReport(
    uint16_t newNode,
    int size,
    uint16_t prospectiveAllocatedNode) noexcept
{
    const uint8_t transactionMask =
        static_cast<uint8_t>(1u << (newNode & 7u));
    const bool scheduledForRemoval =
        (mt_preflightTransactionVisited[newNode >> 3] & transactionMask) != 0;
    if (!MT_IsValidNodeRangeNoReport(newNode, size) ||
        (newNode != prospectiveAllocatedNode &&
         !MT_IsValidUnlinkedFreeIntervalNoReport(newNode, size) &&
         !(scheduledForRemoval &&
           MT_IsFreeNodeMetadataClearNoReport(newNode, size) &&
           mt_freeNodeSizeShadow[newNode] != 0)) ||
        (newNode == prospectiveAllocatedNode &&
         mt_freeNodeSizeShadow[newNode] != 0))
    {
        return false;
    }

    MT_ResetPreflightNodes();
    if (!MT_RecordScanNode(newNode))
    {
        return false;
    }

    uint16_t node = scrMemTreeGlob.head[size];
    if (node == 0)
    {
        return true;
    }

    const int newScore = MT_GetScoreNoReport(newNode);
    int nodeNum = 0;
    int level = MEMORY_NODE_COUNT;
    while (node != 0)
    {
        if (node == newNode ||
            !MT_AreFreeNodeLinksAuthenticatedNoReport(node, size) ||
            !MT_RecordPreflightNode(node))
        {
            return false;
        }

        const int score = MT_GetScoreNoReport(node);
        if (score == newScore)
        {
            return false;
        }

        if (score < newScore)
        {
            while (node != 0)
            {
                level >>= 1;
                if (node == nodeNum)
                {
                    return false;
                }

                const MemoryNode &displacedNode = scrMemTreeGlob.nodes[node];
                if (node >= nodeNum)
                {
                    nodeNum += level;
                    node = displacedNode.next;
                }
                else
                {
                    nodeNum -= level;
                    node = displacedNode.prev;
                }

                if (node != 0 &&
                    (!MT_AreFreeNodeLinksAuthenticatedNoReport(node, size) ||
                     !MT_RecordPreflightNode(node)))
                {
                    return false;
                }
            }
            return true;
        }

        level >>= 1;
        if (newNode == nodeNum)
        {
            return false;
        }

        if (newNode >= nodeNum)
        {
            nodeNum += level;
            node = scrMemTreeGlob.nodes[node].next;
        }
        else
        {
            nodeNum -= level;
            node = scrMemTreeGlob.nodes[node].prev;
        }
    }

    return true;
}

bool MT_TryPreflightRemoveMemoryNodeNoReport(
    uint16_t oldNode,
    int size,
    bool *outFound) noexcept
{
    if (!outFound || size < 0 || size > MEMORY_NODE_BITS)
    {
        return false;
    }
    if (oldNode == 0)
    {
        *outFound = false;
        return true;
    }
    if (!MT_IsValidNodeRangeNoReport(oldNode, size))
    {
        return false;
    }

    MT_ResetPreflightNodes();
    int nodeNum = 0;
    int level = MEMORY_NODE_COUNT;
    uint16_t node = scrMemTreeGlob.head[size];
    while (node != 0)
    {
        if (!MT_AreFreeNodeLinksAuthenticatedNoReport(node, size) ||
            !MT_RecordScanNode(node))
        {
            return false;
        }

        if (oldNode == node)
        {
            while (node != 0)
            {
                const MemoryNode &removalNode = scrMemTreeGlob.nodes[node];
                uint16_t nextNode = 0;
                if (removalNode.prev != 0 && removalNode.next != 0)
                {
                    if (!MT_IsValidFreeNodeNoReport(removalNode.prev, size) ||
                        !MT_IsValidFreeNodeNoReport(removalNode.next, size))
                    {
                        return false;
                    }

                    const int prevScore =
                        MT_GetScoreNoReport(removalNode.prev);
                    const int nextScore =
                        MT_GetScoreNoReport(removalNode.next);
                    if (prevScore == nextScore)
                    {
                        return false;
                    }
                    nextNode = prevScore >= nextScore
                        ? removalNode.prev
                        : removalNode.next;
                }
                else
                {
                    nextNode = removalNode.prev != 0
                        ? removalNode.prev
                        : removalNode.next;
                }

                if (nextNode == 0)
                {
                    if (!MT_CommitScanToPreflightTransaction())
                    {
                        return false;
                    }
                    *outFound = true;
                    return true;
                }
                if (!MT_AreFreeNodeLinksAuthenticatedNoReport(nextNode, size) ||
                    !MT_RecordScanNode(nextNode))
                {
                    return false;
                }
                node = nextNode;
            }
        }

        if (oldNode == nodeNum)
        {
            if (!MT_ScanIsDisjointFromPreflightTransaction())
            {
                return false;
            }
            *outFound = false;
            return true;
        }

        level >>= 1;
        if (oldNode >= nodeNum)
        {
            nodeNum += level;
            node = scrMemTreeGlob.nodes[node].next;
        }
        else
        {
            nodeNum -= level;
            node = scrMemTreeGlob.nodes[node].prev;
        }
    }

    if (!MT_ScanIsDisjointFromPreflightTransaction())
    {
        return false;
    }
    *outFound = false;
    return true;
}

bool MT_IsFreeTreeIntervalValidNoReport(
    const uint32_t nodeNum,
    const int size,
    const uint16_t allowedNode,
    const int allowedSize) noexcept
{
    if (!MT_IsValidNodeRangeNoReport(nodeNum, size) ||
        (allowedNode != 0 &&
         (!MT_IsValidNodeRangeNoReport(allowedNode, allowedSize) ||
          allowedNode < nodeNum ||
          allowedNode + (1u << allowedSize) >
              nodeNum + (1u << size))))
    {
        return false;
    }

    bool allowedFound = false;
    const uint32_t rangeEnd = nodeNum + (1u << size);
    for (uint32_t candidate = nodeNum; candidate < rangeEnd; ++candidate)
    {
        const uint8_t membership = mt_freeNodeSizeShadow[candidate];
        if (membership == 0)
            continue;
        if (membership > MEMORY_NODE_BITS + 1)
            return false;

        const int treeSize = static_cast<int>(membership) - 1;
        if (!MT_IsValidNodeRangeNoReport(candidate, treeSize) ||
            candidate + (1u << treeSize) > rangeEnd ||
            candidate != allowedNode || treeSize != allowedSize ||
            allowedFound)
        {
            return false;
        }
        allowedFound = true;
    }

    for (int ancestorSize = size + 1;
         ancestorSize <= MEMORY_NODE_BITS;
         ++ancestorSize)
    {
        const uint32_t ancestorBuckets = 1u << ancestorSize;
        const uint32_t ancestorNode =
            nodeNum & ~(ancestorBuckets - 1u);
        if (ancestorNode == 0 || ancestorNode == nodeNum)
            continue;

        const uint8_t membership = mt_freeNodeSizeShadow[ancestorNode];
        if (membership == 0)
            continue;
        if (membership > MEMORY_NODE_BITS + 1)
            return false;

        const int memberSize = static_cast<int>(membership) - 1;
        if (!MT_IsValidNodeRangeNoReport(ancestorNode, memberSize))
            return false;
        const uint32_t memberEnd = ancestorNode + (1u << memberSize);
        if (memberEnd > nodeNum)
        {
            return false;
        }
    }

    return allowedNode == 0 || allowedFound;
}

// Mutation helpers used only after accounting, metadata, and every affected
// tree path have been preflighted for the selected validation scope. They
// update primary topology and its shadow together and contain no assertion or
// reporting calls, so a try surface cannot escape between partial mutations.
void MT_RemoveHeadMemoryNodeCommitNoReport(const int size) noexcept
{
    uint16_t *parentNode = &scrMemTreeGlob.head[size];
    uint16_t *parentShadow = &mt_freeTreeHeadShadow[size];
    const uint16_t removedNode = *parentNode;
    mt_freeNodeSizeShadow[removedNode] = 0;
    mt_freeNodeLinkShadow[removedNode] = {};
    --mt_freeNodeCountShadow;
    --mt_freeNodeCountMirror;
    MemoryNode oldNodeValue = scrMemTreeGlob.nodes[*parentNode];
    while (true)
    {
        uint16_t oldNode = 0;
        if (oldNodeValue.prev == 0)
        {
            oldNode = oldNodeValue.next;
            *parentNode = oldNode;
            *parentShadow = oldNode;
            if (oldNode == 0)
                return;
            parentNode = &scrMemTreeGlob.nodes[oldNode].next;
            parentShadow = &mt_freeNodeLinkShadow[oldNode].next;
        }
        else if (oldNodeValue.next != 0)
        {
            const int prevScore =
                MT_GetScoreNoReport(oldNodeValue.prev);
            const int nextScore =
                MT_GetScoreNoReport(oldNodeValue.next);
            oldNode = prevScore >= nextScore
                ? oldNodeValue.prev
                : oldNodeValue.next;
            *parentNode = oldNode;
            *parentShadow = oldNode;
            parentNode = prevScore >= nextScore
                ? &scrMemTreeGlob.nodes[oldNode].prev
                : &scrMemTreeGlob.nodes[oldNode].next;
            parentShadow = prevScore >= nextScore
                ? &mt_freeNodeLinkShadow[oldNode].prev
                : &mt_freeNodeLinkShadow[oldNode].next;
        }
        else
        {
            oldNode = oldNodeValue.prev;
            *parentNode = oldNode;
            *parentShadow = oldNode;
            parentNode = &scrMemTreeGlob.nodes[oldNode].prev;
            parentShadow = &mt_freeNodeLinkShadow[oldNode].prev;
        }

        const MemoryNode displacedValue = oldNodeValue;
        oldNodeValue = scrMemTreeGlob.nodes[oldNode];
        scrMemTreeGlob.nodes[oldNode] = displacedValue;
        mt_freeNodeLinkShadow[oldNode] = {
            displacedValue.prev,
            displacedValue.next,
        };
    }
}

bool MT_RemoveMemoryNodeCommitNoReport(
    int oldNode,
    const int size) noexcept
{
    int nodeNum = 0;
    int level = MEMORY_NODE_COUNT;
    uint16_t *parentNode = &scrMemTreeGlob.head[size];
    uint16_t *parentShadow = &mt_freeTreeHeadShadow[size];
    for (int node = *parentNode; node != 0; node = *parentNode)
    {
        if (oldNode == node)
        {
            mt_freeNodeSizeShadow[static_cast<uint16_t>(oldNode)] = 0;
            mt_freeNodeLinkShadow[static_cast<uint16_t>(oldNode)] = {};
            --mt_freeNodeCountShadow;
            --mt_freeNodeCountMirror;
            MemoryNode oldNodeValue = scrMemTreeGlob.nodes[oldNode];
            while (true)
            {
                if (oldNodeValue.prev != 0)
                {
                    if (oldNodeValue.next != 0)
                    {
                        const int prevScore =
                            MT_GetScoreNoReport(oldNodeValue.prev);
                        const int nextScore =
                            MT_GetScoreNoReport(oldNodeValue.next);
                        if (prevScore >= nextScore)
                        {
                            oldNode = oldNodeValue.prev;
                            *parentNode = oldNodeValue.prev;
                            *parentShadow = oldNodeValue.prev;
                            parentNode =
                                &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                            parentShadow =
                                &mt_freeNodeLinkShadow[oldNodeValue.prev].prev;
                        }
                        else
                        {
                            oldNode = oldNodeValue.next;
                            *parentNode = oldNodeValue.next;
                            *parentShadow = oldNodeValue.next;
                            parentNode =
                                &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                            parentShadow =
                                &mt_freeNodeLinkShadow[oldNodeValue.next].next;
                        }
                    }
                    else
                    {
                        oldNode = oldNodeValue.prev;
                        *parentNode = oldNodeValue.prev;
                        *parentShadow = oldNodeValue.prev;
                        parentNode =
                            &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                        parentShadow =
                            &mt_freeNodeLinkShadow[oldNodeValue.prev].prev;
                    }
                }
                else
                {
                    oldNode = oldNodeValue.next;
                    *parentNode = oldNodeValue.next;
                    *parentShadow = oldNodeValue.next;
                    if (oldNodeValue.next == 0)
                        return true;
                    parentNode =
                        &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                    parentShadow =
                        &mt_freeNodeLinkShadow[oldNodeValue.next].next;
                }

                const MemoryNode displacedValue = oldNodeValue;
                oldNodeValue = scrMemTreeGlob.nodes[oldNode];
                scrMemTreeGlob.nodes[oldNode] = displacedValue;
                mt_freeNodeLinkShadow[oldNode] = {
                    displacedValue.prev,
                    displacedValue.next,
                };
            }
        }

        if (oldNode == nodeNum)
            return false;
        level >>= 1;
        if (oldNode >= nodeNum)
        {
            parentNode = &scrMemTreeGlob.nodes[node].next;
            parentShadow = &mt_freeNodeLinkShadow[node].next;
            nodeNum += level;
        }
        else
        {
            parentNode = &scrMemTreeGlob.nodes[node].prev;
            parentShadow = &mt_freeNodeLinkShadow[node].prev;
            nodeNum -= level;
        }
    }
    return false;
}

void MT_AddMemoryNodeCommitNoReport(
    int newNode,
    const int size) noexcept
{
    mt_freeNodeSizeShadow[static_cast<uint16_t>(newNode)] =
        static_cast<uint8_t>(size + 1);
    ++mt_freeNodeCountShadow;
    ++mt_freeNodeCountMirror;
    uint16_t *parentNode = &scrMemTreeGlob.head[size];
    uint16_t *parentShadow = &mt_freeTreeHeadShadow[size];
    int node = *parentNode;
    if (node != 0)
    {
        const int newScore =
            MT_GetScoreNoReport(static_cast<uint16_t>(newNode));
        int nodeNum = 0;
        int level = MEMORY_NODE_COUNT;
        do
        {
            const int score =
                MT_GetScoreNoReport(static_cast<uint16_t>(node));
            if (score < newScore)
            {
                while (true)
                {
                    *parentNode = static_cast<uint16_t>(newNode);
                    *parentShadow = static_cast<uint16_t>(newNode);
                    scrMemTreeGlob.nodes[newNode] =
                        scrMemTreeGlob.nodes[node];
                    mt_freeNodeLinkShadow[newNode] = {
                        scrMemTreeGlob.nodes[newNode].prev,
                        scrMemTreeGlob.nodes[newNode].next,
                    };
                    if (node == 0)
                        return;
                    level >>= 1;
                    if (node >= nodeNum)
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].next;
                        parentShadow = &mt_freeNodeLinkShadow[newNode].next;
                        nodeNum += level;
                    }
                    else
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].prev;
                        parentShadow = &mt_freeNodeLinkShadow[newNode].prev;
                        nodeNum -= level;
                    }
                    newNode = node;
                    node = *parentNode;
                }
            }
            level >>= 1;
            if (newNode >= nodeNum)
            {
                parentNode = &scrMemTreeGlob.nodes[node].next;
                parentShadow = &mt_freeNodeLinkShadow[node].next;
                nodeNum += level;
            }
            else
            {
                parentNode = &scrMemTreeGlob.nodes[node].prev;
                parentShadow = &mt_freeNodeLinkShadow[node].prev;
                nodeNum -= level;
            }
            node = *parentNode;
        } while (node != 0);
    }

    *parentNode = static_cast<uint16_t>(newNode);
    *parentShadow = static_cast<uint16_t>(newNode);
    scrMemTreeGlob.nodes[newNode].prev = 0;
    scrMemTreeGlob.nodes[newNode].next = 0;
    mt_freeNodeLinkShadow[newNode] = {};
}
} // namespace

MT_ValidationLease::~MT_ValidationLease() noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const uintptr_t address = reinterpret_cast<uintptr_t>(this);
    const bool boundaryMentionsThis =
        mt_activeValidationLeaseAddress == address
        || mt_activeValidationLeaseAddressMirror == address
        || mt_retainedValidationLeaseAddress == address
        || mt_retainedValidationLeaseAddressMirror == address;
    const bool canonical =
        MT_ValidationLeaseAccess::IsCanonicalClear(*this);

    // A never-admitted object, a successfully finished object, or an
    // unrelated canonical object nested inside another owner has no lifetime
    // authority to revoke.
    if (canonical && !boundaryMentionsThis)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }

    // MT_Owns... compares both stored addresses with this live object before
    // reading its authentication fields. No generic stored address is ever
    // dereferenced here or elsewhere.
    const bool ownsRetainedAcquisition =
        MT_OwnsValidationLeaseLocked(this);

    // Publish terminal process-lifetime poison before removing the only
    // address that may name this stack object. This remains frozen even when
    // allocator state itself is currently valid.
    MT_FreezeValidationLeaseBoundaryLocked();
    MT_ValidationLeaseAccess::Poison(*this);
    if (ownsRetainedAcquisition)
        MT_ClearRetainedValidationLeaseAuthenticationLocked();

    // Always drop the acquisition made by this destructor. Only exact
    // address/serial/mirror authentication proves that Begin's retained
    // acquisition belongs to this object and may also be released.
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    if (ownsRetainedAcquisition)
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

bool MT_ValidationLease::AbandonFromOwnershipBatch(
    MT_ValidationLease &lease) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);

    // The caller supplies live nested storage. MT_Owns... compares that
    // address against every by-value registry/TLS mirror before reading any
    // member. Only that exact identity proves Begin's retained acquisition.
    const bool ownsRetainedAcquisition =
        MT_OwnsValidationLeaseLocked(&lease);

    // Terminal state must be visible before the stack address disappears or
    // either acquisition can be released. A torn nested identity keeps the
    // unproven retained acquisition held; its automatic destructor repeats the
    // same fail-closed publication without converting a stored address back to
    // a pointer.
    MT_FreezeValidationLeaseBoundaryLocked();
    if (ownsRetainedAcquisition)
    {
        MT_ValidationLeaseAccess::Clear(lease);
        MT_ClearRetainedValidationLeaseAuthenticationLocked();
    }
    else
    {
        MT_ValidationLeaseAccess::Poison(lease);
    }

    // Drop this helper's recursive acquisition. Drop Begin's retained
    // acquisition only when the live address, serial, lifecycle, and every
    // global/TLS mirror proved exact above.
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    if (ownsRetainedAcquisition)
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return ownsRetainedAcquisition;
}

bool MT_ValidationLease::active() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const bool readable =
        MT_CanReadValidationLeaseSnapshotLocked(this);
    const bool value = readable && active_;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return value;
}

bool MT_ValidationLease::poisoned() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const bool readable =
        MT_CanReadValidationLeaseSnapshotLocked(this);
    const bool value = readable
        ? (poisoned_
            || mt_validationLeaseLifecycle
                == MT_ValidationLeaseLifecycle::Poisoned)
        : MT_HasValidationLeaseRegistryActivityLocked();
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return value;
}

uint64_t MT_ValidationLease::serial() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const bool readable =
        MT_CanReadValidationLeaseSnapshotLocked(this);
    const uint64_t value = readable ? serial_ : 0;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return value;
}

uint32_t MT_ValidationLease::mutationCount() const noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const bool readable =
        MT_CanReadValidationLeaseSnapshotLocked(this);
    const uint32_t value = readable ? mutationCount_ : 0;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return value;
}

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
void MT_ValidationLease::SetAuthenticationFieldsForTesting(
    const uint64_t serial,
    const uint8_t reserved0,
    const uint8_t reserved1) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_HasValidationLeaseRegistryActivityLocked()
        && !MT_RegistryNamesValidationLeaseStorageLocked(this))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }
    serial_ = serial;
    reserved_[0] = reserved0;
    reserved_[1] = reserved1;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void MT_ValidationLease::SetMutationCountForTesting(
    const uint32_t mutationCount) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_HasValidationLeaseRegistryActivityLocked()
        && !MT_RegistryNamesValidationLeaseStorageLocked(this))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }
    mutationCount_ = mutationCount;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}
#endif

MT_ValidationLeaseStatus MT_TryBeginValidationLease(
    MT_ValidationLease *const lease,
    const MT_ValidationLeaseAdmission &admission) noexcept
{
    if (!MT_ValidationLeaseAdmission::Authenticates(admission))
        return MT_ValidationLeaseStatus::InvalidArgument;
    if (!lease)
        return MT_ValidationLeaseStatus::InvalidArgument;

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_HasValidationLeaseRegistryActivityLocked())
    {
        // A foreign owner cannot reach this check until its retained lock is
        // released. A consistent live owner here is therefore same-thread
        // recursion; any torn pointer/serial/mirror combination fails closed.
        const bool consistent =
            MT_IsValidationLeaseRegistryConsistentLocked();
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return consistent
            ? MT_ValidationLeaseStatus::Busy
            : MT_ValidationLeaseStatus::UnsafeFailure;
    }

    if (!MT_ValidationLeaseAccess::IsCanonicalClear(*lease))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_ValidationLeaseStatus::InvalidToken;
    }

    if (!MT_IsCoreStateValidNoReport()
        || mt_nextValidationLeaseSerial == UINT64_MAX)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_ValidationLeaseStatus::UnsafeFailure;
    }

    const uint64_t serial = mt_nextValidationLeaseSerial + UINT64_C(1);
    mt_nextValidationLeaseSerial = serial;
    MT_ValidationLeaseAccess::Activate(*lease, serial);
    mt_activeValidationLeaseAddress =
        reinterpret_cast<uintptr_t>(lease);
    mt_activeValidationLeaseSerial = serial;
    mt_activeValidationLeaseAddressMirror =
        reinterpret_cast<uintptr_t>(lease);
    mt_activeValidationLeaseSerialMirror = serial;
    mt_validationLeaseLifecycle = MT_ValidationLeaseLifecycle::Active;
    mt_validationLeaseLifecycleMirror =
        MT_ValidationLeaseLifecycle::Active;
    mt_retainedValidationLeaseAddress =
        reinterpret_cast<uintptr_t>(lease);
    mt_retainedValidationLeaseAddressMirror =
        reinterpret_cast<uintptr_t>(lease);
    mt_retainedValidationLeaseSerial = serial;
    mt_retainedValidationLeaseSerialMirror = serial;
    // Retain the acquisition made above until Finish.
    return MT_ValidationLeaseStatus::Success;
}

MT_ValidationLeaseStatus MT_FinishValidationLease(
    MT_ValidationLease *const lease,
    const MT_ValidationLeaseAdmission &admission) noexcept
{
    if (!MT_ValidationLeaseAdmission::Authenticates(admission))
        return MT_ValidationLeaseStatus::InvalidArgument;
    if (!lease)
        return MT_ValidationLeaseStatus::InvalidArgument;

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!MT_OwnsValidationLeaseLocked(lease))
    {
        // Drop only this authentication acquisition. The retained acquisition
        // cannot be identified safely from an invalid token.
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_ValidationLeaseStatus::InvalidToken;
    }

    const bool poisoned = MT_IsValidationLeasePoisonedLocked(lease);
    const bool valid = MT_IsCoreStateValidNoReport();
    MT_ClearValidationLeaseRegistryLocked();
    MT_ClearRetainedValidationLeaseAuthenticationLocked();
    MT_ValidationLeaseAccess::Clear(*lease);

    // Drop this authentication acquisition and then Begin's retained one.
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return valid && !poisoned
        ? MT_ValidationLeaseStatus::Success
        : MT_ValidationLeaseStatus::UnsafeFailure;
}

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
void MT_SetNextValidationLeaseSerialForTesting(
    const uint64_t serial) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!MT_HasValidationLeaseRegistryActivityLocked())
        mt_nextValidationLeaseSerial = serial;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void MT_SetValidationLeaseRegistryForTesting(
    MT_ValidationLease *const lease,
    const uint64_t serial) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    mt_activeValidationLeaseAddress =
        reinterpret_cast<uintptr_t>(lease);
    mt_activeValidationLeaseSerial = serial;
    mt_activeValidationLeaseAddressMirror =
        reinterpret_cast<uintptr_t>(lease);
    mt_activeValidationLeaseSerialMirror = serial;
    const MT_ValidationLeaseLifecycle lifecycle =
        lease && serial != 0
        ? MT_ValidationLeaseLifecycle::Active
        : MT_ValidationLeaseLifecycle::Idle;
    mt_validationLeaseLifecycle = lifecycle;
    mt_validationLeaseLifecycleMirror = lifecycle;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void MT_SetValidationLeaseRegistryMirrorsForTesting(
    const uintptr_t address,
    const uint64_t serial,
    const uintptr_t addressMirror,
    const uint64_t serialMirror) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    mt_activeValidationLeaseAddress = address;
    mt_activeValidationLeaseSerial = serial;
    mt_activeValidationLeaseAddressMirror = addressMirror;
    mt_activeValidationLeaseSerialMirror = serialMirror;
    const MT_ValidationLeaseLifecycle lifecycle =
        address != 0 || serial != 0 || addressMirror != 0
                || serialMirror != 0
        ? MT_ValidationLeaseLifecycle::Active
        : MT_ValidationLeaseLifecycle::Idle;
    mt_validationLeaseLifecycle = lifecycle;
    mt_validationLeaseLifecycleMirror = lifecycle;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void MT_SetValidationLeaseLifecycleForTesting(
    const uint8_t lifecycle,
    const uint8_t lifecycleMirror) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    mt_validationLeaseLifecycle =
        static_cast<MT_ValidationLeaseLifecycle>(lifecycle);
    mt_validationLeaseLifecycleMirror =
        static_cast<MT_ValidationLeaseLifecycle>(lifecycleMirror);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void MT_SetRetainedValidationLeaseAuthenticationForTesting(
    const uintptr_t address,
    const uint64_t serial,
    const uintptr_t addressMirror,
    const uint64_t serialMirror) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    mt_retainedValidationLeaseAddress = address;
    mt_retainedValidationLeaseSerial = serial;
    mt_retainedValidationLeaseAddressMirror = addressMirror;
    mt_retainedValidationLeaseSerialMirror = serialMirror;
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}

void MT_ResetAbandonedValidationLeaseForTesting(
    const bool releaseRetainedAcquisition) noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const bool retainedActivity =
        MT_HasRetainedValidationLeaseAuthenticationLocked();
    const bool retainedAuthenticated =
        mt_retainedValidationLeaseAddress != 0
        && mt_retainedValidationLeaseAddress
            == mt_retainedValidationLeaseAddressMirror
        && mt_retainedValidationLeaseSerial != 0
        && mt_retainedValidationLeaseSerial
            == mt_retainedValidationLeaseSerialMirror;
    if ((releaseRetainedAcquisition && !retainedAuthenticated)
        || (!releaseRetainedAcquisition && retainedActivity))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }

    mt_abandonedValidationLeasePoison = 0;
    mt_abandonedValidationLeasePoisonMirror = 0;
    MT_ClearValidationLeaseRegistryLocked();
    MT_ClearRetainedValidationLeaseAuthenticationLocked();
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    if (releaseRetainedAcquisition)
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
}
#endif

namespace
{
MT_AllocIndexStatus MT_TryAllocIndexImpl(
    int numBytes,
    int type,
    uint16_t *outIndex,
    const MT_ValidationPolicy policy,
    MT_ValidationLease *const lease) noexcept
{
    int size = 0;
    if (!outIndex || type <= 0 || type >= kMemoryTreeTypeCount ||
        !MT_TryGetSizeNoReport(numBytes, &size))
    {
        return MT_AllocIndexStatus::InvalidArgumentNoChange;
    }

    PROF_SCOPED("scriptMemory");

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const MT_PolicyEntryStatus entryStatus =
        MT_ValidatePolicyEntryLocked(policy, lease, false);
    if (entryStatus != MT_PolicyEntryStatus::Success)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return entryStatus == MT_PolicyEntryStatus::InvalidLease
            ? MT_AllocIndexStatus::InvalidArgumentNoChange
            : MT_AllocIndexStatus::UnsafeFailure;
    }
    int newSize = size;
    while (newSize <= MEMORY_NODE_BITS && scrMemTreeGlob.head[newSize] == 0)
    {
        ++newSize;
    }
    if (newSize > MEMORY_NODE_BITS)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_AllocIndexStatus::InsufficientCapacityNoChange;
    }

    const uint16_t nodeNum = scrMemTreeGlob.head[newSize];
    const int allocationBuckets = 1 << size;
    const uint32_t splitCount = static_cast<uint32_t>(newSize - size);
    MT_ResetPreflightTransaction();
    if (mt_freeNodeCountShadow == 0 ||
        splitCount > (MEMORY_NODE_COUNT - 1) -
            (mt_freeNodeCountShadow - 1) ||
        scrMemTreeGlob.totalAllocBuckets >
            (MEMORY_NODE_COUNT - 1) - allocationBuckets ||
        !MT_IsAllocationIntervalClearNoReport(nodeNum, newSize) ||
        !MT_IsFreeTreeIntervalValidNoReport(
            nodeNum, newSize, nodeNum, newSize) ||
        !MT_CanRemoveHeadMemoryNodeNoReport(newSize))
    {
        MT_PoisonPolicyLeaseLocked(policy, lease);
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_AllocIndexStatus::UnsafeFailure;
    }

    for (int splitSize = newSize - 1; splitSize >= size; --splitSize)
    {
        const uint32_t splitNode =
            static_cast<uint32_t>(nodeNum) + (1u << splitSize);
        if (splitNode >= MEMORY_NODE_COUNT ||
            !MT_CanAddMemoryNodeNoReport(
                static_cast<uint16_t>(splitNode), splitSize, 0))
        {
            MT_PoisonPolicyLeaseLocked(policy, lease);
            Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
            return MT_AllocIndexStatus::UnsafeFailure;
        }
    }

    if (!MT_CanCommitPolicyMutationLocked(policy, lease))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_AllocIndexStatus::UnsafeFailure;
    }

    MT_RemoveHeadMemoryNodeCommitNoReport(newSize);
    while (newSize != size)
    {
        --newSize;
        MT_AddMemoryNodeCommitNoReport(
            nodeNum + (1 << newSize), newSize);
    }
    ++scrMemTreeGlob.totalAlloc;
    scrMemTreeGlob.totalAllocBuckets += allocationBuckets;
    ++mt_totalAllocShadow;
    mt_totalAllocBucketsShadow += allocationBuckets;

    scrMemTreeDebugGlob.mt_usage[nodeNum] = static_cast<uint8_t>(type);
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = static_cast<uint8_t>(size);
    mt_allocationMetadataShadow[nodeNum] = MT_PackAllocationMetadata(
        static_cast<uint8_t>(type), static_cast<uint8_t>(size));
    MT_RecordPolicyMutationLocked(policy, lease);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    *outIndex = nodeNum;
    return MT_AllocIndexStatus::Success;
}

MT_AllocationInfoStatus MT_TryGetAllocationInfoImpl(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo,
    const MT_ValidationPolicy policy,
    MT_ValidationLease *const lease) noexcept
{
    if (!outInfo || nodeNum == 0 || nodeNum >= MEMORY_NODE_COUNT)
    {
        return MT_AllocationInfoStatus::InvalidArgumentNoChange;
    }

    MT_AllocationInfo allocationInfo{};
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const MT_PolicyEntryStatus entryStatus =
        MT_ValidatePolicyEntryLocked(policy, lease, true);
    if (entryStatus != MT_PolicyEntryStatus::Success)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return entryStatus == MT_PolicyEntryStatus::InvalidLease
            ? MT_AllocationInfoStatus::InvalidArgumentNoChange
            : MT_AllocationInfoStatus::UnsafeFailure;
    }

    MT_AllocationInfoStatus status =
        MT_GetAllocationInfoLockedNoReport(nodeNum, &allocationInfo);
    if (status == MT_AllocationInfoStatus::Success &&
        (!MT_IsAllocationIntervalExactNoReport(
             nodeNum, allocationInfo.type, allocationInfo.size) ||
         !MT_IsFreeTreeIntervalValidNoReport(
             nodeNum, allocationInfo.size, 0, -1)))
    {
        status = MT_AllocationInfoStatus::UnsafeFailure;
    }
    if (status == MT_AllocationInfoStatus::UnsafeFailure)
        MT_PoisonPolicyLeaseLocked(policy, lease);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    if (status == MT_AllocationInfoStatus::Success)
    {
        *outInfo = allocationInfo;
    }
    return status;
}
} // namespace

MT_AllocIndexStatus MT_TryAllocIndex(
    int numBytes,
    int type,
    uint16_t *outIndex) noexcept
{
    return MT_TryAllocIndexImpl(
        numBytes, type, outIndex, MT_ValidationPolicy::Complete, nullptr);
}

MT_AllocIndexStatus MT_TryAllocIndexLegacy(
    int numBytes,
    int type,
    uint16_t *outIndex) noexcept
{
    return MT_TryAllocIndexImpl(
        numBytes, type, outIndex, MT_ValidationPolicy::LegacyLocal, nullptr);
}

MT_AllocIndexStatus MT_TryAllocIndexLeased(
    MT_ValidationLease &lease,
    int numBytes,
    int type,
    uint16_t *outIndex,
    const MT_ValidationLeaseAdmission &admission) noexcept
{
    if (!MT_ValidationLeaseAdmission::Authenticates(admission))
        return MT_AllocIndexStatus::InvalidArgumentNoChange;
    return MT_TryAllocIndexImpl(
        numBytes, type, outIndex, MT_ValidationPolicy::Leased, &lease);
}

unsigned short MT_AllocIndex(int numBytes, int type)
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return 0;
    }

    uint16_t nodeNum = 0;
    const MT_AllocIndexStatus status =
        MT_TryAllocIndexLegacy(numBytes, type, &nodeNum);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    if (status != MT_AllocIndexStatus::Success)
    {
        if (status == MT_AllocIndexStatus::UnsafeFailure)
            MT_UnsafeErrorNoDump("MT_AllocIndex", numBytes);
        else
            MT_Error("MT_AllocIndex", numBytes);
        return 0;
    }

    return nodeNum;
}

MT_AllocationInfoStatus MT_TryGetAllocationInfo(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo) noexcept
{
    return MT_TryGetAllocationInfoImpl(
        nodeNum, outInfo, MT_ValidationPolicy::Complete, nullptr);
}

MT_AllocationInfoStatus MT_TryGetAllocationInfoLegacy(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo) noexcept
{
    return MT_TryGetAllocationInfoImpl(
        nodeNum, outInfo, MT_ValidationPolicy::LegacyLocal, nullptr);
}

MT_AllocationInfoStatus MT_TryGetAllocationInfoLeased(
    MT_ValidationLease &lease,
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo,
    const MT_ValidationLeaseAdmission &admission) noexcept
{
    if (!MT_ValidationLeaseAdmission::Authenticates(admission))
        return MT_AllocationInfoStatus::InvalidArgumentNoChange;
    return MT_TryGetAllocationInfoImpl(
        nodeNum, outInfo, MT_ValidationPolicy::Leased, &lease);
}

bool MT_TryValidateState() noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const bool valid =
        !MT_RejectUnleasedAccessForActiveLeaseLocked()
        && MT_IsCoreStateValidNoReport();
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return valid;
}

#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
void MT_ResetCompleteValidationCountForTesting() noexcept
{
    mt_completeValidationCount = 0;
    mt_completeForestValidationCount = 0;
}

uint32_t MT_CompleteValidationCountForTesting() noexcept
{
    return mt_completeValidationCount;
}

uint32_t MT_CompleteForestValidationCountForTesting() noexcept
{
    return mt_completeForestValidationCount;
}

void MT_CorruptAllocationMetadataForTesting(
    const uint32_t nodeNum,
    const uint8_t type,
    const uint8_t size) noexcept
{
    if (nodeNum >= MEMORY_NODE_COUNT)
        return;
    scrMemTreeDebugGlob.mt_usage[nodeNum] = type;
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = size;
}

void MT_CorruptFreeNodeMembershipForTesting(
    const uint32_t nodeNum,
    const uint8_t membership) noexcept
{
    if (nodeNum >= MEMORY_NODE_COUNT)
        return;
    mt_freeNodeSizeShadow[nodeNum] = membership;
}
#endif

bool MT_Realloc(int oldNumBytes, int newNumbytes)
{
    int oldSize = 0;
    int newSize = 0;
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return false;
    }
    const bool oldValid = MT_TryGetSizeNoReport(oldNumBytes, &oldSize);
    const bool newValid = MT_TryGetSizeNoReport(newNumbytes, &newSize);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    // Preserve legacy diagnostics, but reacquire through the guarded public
    // entry instead of reporting while the allocator lock is held.
    if (!oldValid)
    {
        (void)MT_GetSize(oldNumBytes);
        return false;
    }
    if (!newValid)
    {
        (void)MT_GetSize(newNumbytes);
        return false;
    }
    return oldSize >= newSize;
}

void MT_RemoveHeadMemoryNode(int size)
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }

    if (size < 0 || size > MEMORY_NODE_BITS)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        iassert(size >= 0 && size <= MEMORY_NODE_BITS);
        return;
    }

    MT_ResetPreflightTransaction();
    const bool valid = MT_IsBasicCoreStateValidNoReport()
        && mt_freeNodeCountShadow != 0
        && scrMemTreeGlob.head[size] != 0
        && MT_CanRemoveHeadMemoryNodeNoReport(size);
    if (valid)
        MT_RemoveHeadMemoryNodeCommitNoReport(size);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    iassert(valid);
}

namespace
{
MT_FreeIndexStatus MT_TryFreeIndexImpl(
    uint32_t nodeNum,
    int numBytes,
    const MT_ValidationPolicy policy,
    MT_ValidationLease *const lease) noexcept
{
    int size = 0;
    if (nodeNum == 0 || nodeNum >= MEMORY_NODE_COUNT ||
        !MT_TryGetSizeNoReport(numBytes, &size))
    {
        return MT_FreeIndexStatus::InvalidArgumentNoChange;
    }

    PROF_SCOPED("scriptMemory");

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const MT_PolicyEntryStatus entryStatus =
        MT_ValidatePolicyEntryLocked(policy, lease, false);
    if (entryStatus != MT_PolicyEntryStatus::Success)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return entryStatus == MT_PolicyEntryStatus::InvalidLease
            ? MT_FreeIndexStatus::InvalidArgumentNoChange
            : MT_FreeIndexStatus::UnsafeFailure;
    }
    MT_AllocationInfo allocationInfo{};
    const MT_AllocationInfoStatus allocationStatus =
        MT_GetAllocationInfoLockedNoReport(nodeNum, &allocationInfo);
    if (allocationStatus == MT_AllocationInfoStatus::NotAllocatedNoChange)
    {
        bool foundFreeNode = false;
        MT_ResetPreflightTransaction();
        const bool freePathValid =
            !MT_IsValidNodeRangeNoReport(nodeNum, size)
            || MT_TryPreflightRemoveMemoryNodeNoReport(
                static_cast<uint16_t>(nodeNum), size, &foundFreeNode);
        const bool shadowFound =
            mt_freeNodeSizeShadow[nodeNum] == static_cast<uint8_t>(size + 1);
        const bool ownershipMismatch =
            freePathValid && foundFreeNode == shadowFound;
        if (!ownershipMismatch)
            MT_PoisonPolicyLeaseLocked(policy, lease);
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return ownershipMismatch
            ? MT_FreeIndexStatus::OwnershipMismatchNoChange
            : MT_FreeIndexStatus::UnsafeFailure;
    }
    if (allocationStatus != MT_AllocationInfoStatus::Success)
    {
        MT_PoisonPolicyLeaseLocked(policy, lease);
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
    }
    if (!MT_IsAllocationIntervalExactNoReport(
            nodeNum, allocationInfo.type, allocationInfo.size) ||
        !MT_IsFreeTreeIntervalValidNoReport(
            nodeNum, allocationInfo.size, 0, -1))
    {
        MT_PoisonPolicyLeaseLocked(policy, lease);
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
    }
    if (allocationInfo.size != size)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::OwnershipMismatchNoChange;
    }

    uint32_t mergedNode = nodeNum;
    int mergedSize = size;
    uint32_t buddyCount = 0;
    MT_ResetPreflightTransaction();
    while (mergedSize < MEMORY_NODE_BITS)
    {
        const uint32_t lowBit = 1u << mergedSize;
        const uint32_t buddyNode = lowBit ^ mergedNode;
        bool buddyFound = false;
        if (!MT_TryPreflightRemoveMemoryNodeNoReport(
                static_cast<uint16_t>(buddyNode), mergedSize, &buddyFound))
        {
            MT_PoisonPolicyLeaseLocked(policy, lease);
            Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
            return MT_FreeIndexStatus::UnsafeFailure;
        }
        const bool shadowBuddyFound =
            mt_freeNodeSizeShadow[buddyNode] ==
                static_cast<uint8_t>(mergedSize + 1);
        if (buddyFound != shadowBuddyFound)
        {
            MT_PoisonPolicyLeaseLocked(policy, lease);
            Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
            return MT_FreeIndexStatus::UnsafeFailure;
        }
        if (!buddyFound)
        {
            break;
        }
        if (!MT_IsAllocationIntervalClearNoReport(
                buddyNode, mergedSize) ||
            !MT_IsFreeTreeIntervalValidNoReport(
                buddyNode,
                mergedSize,
                static_cast<uint16_t>(buddyNode),
                mergedSize))
        {
            MT_PoisonPolicyLeaseLocked(policy, lease);
            Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
            return MT_FreeIndexStatus::UnsafeFailure;
        }

        mergedNode &= ~lowBit;
        ++mergedSize;
        ++buddyCount;
    }

    if (buddyCount > mt_freeNodeCountShadow ||
        mt_freeNodeCountShadow - buddyCount > MEMORY_NODE_COUNT - 2 ||
        !MT_CanAddMemoryNodeNoReport(
            static_cast<uint16_t>(mergedNode),
            mergedSize,
            static_cast<uint16_t>(nodeNum)))
    {
        MT_PoisonPolicyLeaseLocked(policy, lease);
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
    }

    if (!MT_CanCommitPolicyMutationLocked(policy, lease))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
    }

    --scrMemTreeGlob.totalAlloc;
    scrMemTreeGlob.totalAllocBuckets -= 1 << size;
    --mt_totalAllocShadow;
    mt_totalAllocBucketsShadow -= 1 << size;
    scrMemTreeDebugGlob.mt_usage[nodeNum] = 0;
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = 0;
    mt_allocationMetadataShadow[nodeNum] = 0;

    mergedNode = nodeNum;
    mergedSize = size;
    while (1)
    {
        const uint32_t lowBit = 1u << mergedSize;
        if (mergedSize == MEMORY_NODE_BITS ||
            !MT_RemoveMemoryNodeCommitNoReport(
                static_cast<int>(lowBit ^ mergedNode), mergedSize))
        {
            break;
        }

        mergedNode &= ~lowBit;
        ++mergedSize;
    }
    MT_AddMemoryNodeCommitNoReport(
        static_cast<int>(mergedNode), mergedSize);
    MT_RecordPolicyMutationLocked(policy, lease);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return MT_FreeIndexStatus::Success;
}
} // namespace

MT_FreeIndexStatus MT_TryFreeIndex(
    uint32_t nodeNum,
    int numBytes) noexcept
{
    return MT_TryFreeIndexImpl(
        nodeNum, numBytes, MT_ValidationPolicy::Complete, nullptr);
}

MT_FreeIndexStatus MT_TryFreeIndexLegacy(
    uint32_t nodeNum,
    int numBytes) noexcept
{
    return MT_TryFreeIndexImpl(
        nodeNum, numBytes, MT_ValidationPolicy::LegacyLocal, nullptr);
}

MT_FreeIndexStatus MT_TryFreeIndexLeased(
    MT_ValidationLease &lease,
    uint32_t nodeNum,
    int numBytes,
    const MT_ValidationLeaseAdmission &admission) noexcept
{
    if (!MT_ValidationLeaseAdmission::Authenticates(admission))
        return MT_FreeIndexStatus::InvalidArgumentNoChange;
    return MT_TryFreeIndexImpl(
        nodeNum, numBytes, MT_ValidationPolicy::Leased, &lease);
}

namespace
{
struct MT_LegacyFreeAttempt final
{
    MT_FreeIndexStatus status =
        MT_FreeIndexStatus::InvalidArgumentNoChange;
    bool validSize = false;
    bool validNode = false;
};

MT_LegacyFreeAttempt MT_TryFreeIndexLegacyLockedNoReport(
    const uint32_t nodeNum,
    const int numBytes) noexcept
{
    MT_LegacyFreeAttempt attempt{};
    int size = 0;
    attempt.validSize = MT_TryGetSizeNoReport(numBytes, &size);
    attempt.validNode = nodeNum > 0 && nodeNum < MEMORY_NODE_COUNT;
    if (attempt.validSize && attempt.validNode)
        attempt.status = MT_TryFreeIndexLegacy(nodeNum, numBytes);
    return attempt;
}

void MT_ReportLegacyFreeAttempt(
    const MT_LegacyFreeAttempt &attempt,
    const int numBytes)
{
    if (!attempt.validSize || !attempt.validNode)
    {
        iassert(attempt.validSize);
        iassert(attempt.validNode);
        return;
    }
    if (attempt.status == MT_FreeIndexStatus::Success)
        return;

    if (attempt.status == MT_FreeIndexStatus::UnsafeFailure)
    {
        MT_UnsafeErrorNoDump("MT_FreeIndex", numBytes);
        return;
    }

    iassert(attempt.status == MT_FreeIndexStatus::Success);
    MT_Error("MT_FreeIndex", numBytes);
}
} // namespace

void MT_FreeIndex(uint32_t nodeNum, int numBytes)
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }

    const MT_LegacyFreeAttempt attempt =
        MT_TryFreeIndexLegacyLockedNoReport(nodeNum, numBytes);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    MT_ReportLegacyFreeAttempt(attempt, numBytes);
}

bool __cdecl MT_RemoveMemoryNode(int oldNode, uint32_t size)
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return false;
    }

    if (size > MEMORY_NODE_BITS || oldNode <= 0
        || oldNode >= MEMORY_NODE_COUNT)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        iassert(size <= MEMORY_NODE_BITS);
        return false;
    }

    MT_ResetPreflightTransaction();
    bool found = false;
    const bool valid = MT_IsBasicCoreStateValidNoReport()
        && mt_freeNodeCountShadow != 0
        && MT_TryPreflightRemoveMemoryNodeNoReport(
            static_cast<uint16_t>(oldNode),
            static_cast<int>(size),
            &found);
    const bool removed = valid && found
        && MT_RemoveMemoryNodeCommitNoReport(
            oldNode, static_cast<int>(size));
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    iassert(valid);
    return removed;
}

void MT_Free(byte* p, int numBytes)
{
    uint32_t nodeNum = 0;
    MT_LegacyFreeAttempt attempt{};
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }

    const uintptr_t treeBegin =
        reinterpret_cast<uintptr_t>(&scrMemTreeGlob.nodes[0]);
    const uintptr_t candidate = reinterpret_cast<uintptr_t>(p);
    const bool inRange = candidate >= treeBegin
        && candidate - treeBegin < sizeof(scrMemTreeGlob.nodes);
    const bool aligned = inRange
        && (candidate - treeBegin) % sizeof(MemoryNode) == 0;
    if (aligned)
    {
        nodeNum = static_cast<uint32_t>(
            (candidate - treeBegin) / sizeof(MemoryNode));
        attempt =
            MT_TryFreeIndexLegacyLockedNoReport(nodeNum, numBytes);
    }
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    iassert(aligned);
    if (!aligned)
        return;
    MT_ReportLegacyFreeAttempt(attempt, numBytes);
}

int MT_GetSize(int numBytes)
{
    int size = 0;
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return 0;
    }
    const bool valid = MT_TryGetSizeNoReport(numBytes, &size);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    if (!valid)
    {
        iassert(numBytes > 0);
        if (numBytes >= MEMORY_NODE_COUNT)
            MT_Error("MT_GetSize: max allocation exceeded", numBytes);
        return 0;
    }
    return size;
}

int MT_GetScore(int num)
{
    int score = 0;
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return 0;
    }

    const bool validNode = num > 0 && num < MEMORY_NODE_COUNT;
    const bool validState = validNode && MT_AreBitTablesValidNoReport();
    if (validState)
        score = MT_GetScoreNoReport(static_cast<uint16_t>(num));
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    iassert(validNode);
    return validState ? score : 0;
}

int MT_GetSubTreeSize(int nodeNum)
{
    int subTreeSize = 0;
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return 0;
    }

    const bool validNode = nodeNum >= 0 && nodeNum < MEMORY_NODE_COUNT;
    const bool validRoot = validNode
        && (nodeNum == 0 || mt_freeNodeSizeShadow[nodeNum] != 0);
    const bool validTree = validRoot && MT_IsCoreStateValidNoReport();
    if (validTree)
        subTreeSize = MT_GetSubTreeSizeNoReport(nodeNum);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    iassert(validRoot);
    return validTree ? subTreeSize : 0;
}

void MT_AddMemoryNode(int newNode, int size)
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return;
    }

    if (size < 0 || size > MEMORY_NODE_BITS || newNode <= 0
        || newNode >= MEMORY_NODE_COUNT)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        iassert(size >= 0 && size <= MEMORY_NODE_BITS);
        return;
    }

    MT_ResetPreflightTransaction();
    const bool valid = MT_IsBasicCoreStateValidNoReport()
        && mt_freeNodeCountShadow < MEMORY_NODE_COUNT - 1
        && MT_CanAddMemoryNodeNoReport(
            static_cast<uint16_t>(newNode), size, 0);
    if (valid)
        MT_AddMemoryNodeCommitNoReport(newNode, size);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    iassert(valid);
}

void MT_Error(const char* funcName, int numBytes)
{
    // Capture admission and every allocator-derived dump field in one locked
    // interval. Once admitted, terminal reporting consumes only the snapshot.
    if (MT_DumpTreeInternal() == MT_DumpStatus::BoundaryUnavailable)
        return;

    Com_Printf(23, "%s: failed memory allocation of %d bytes for script usage\n", funcName, numBytes);
    Com_Error(ERR_FATAL, "MT_Error (KISAK)\n");
    //Scr_TerminalError("failed memory allocation for script usage");
}

namespace
{
MT_DumpStatus MT_DumpTreeInternal() noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_DumpStatus::BoundaryUnavailable;
    }
    if (mt_dumpSnapshotInUse.test_and_set(std::memory_order_acquire))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_DumpStatus::ScratchUnavailable;
    }
    const bool captured = MT_TryCaptureDumpSnapshotLocked();
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    if (!captured)
    {
        Com_Printf(23, "memory-tree dump unavailable: unsafe state\n");
        mt_dumpSnapshotInUse.clear(std::memory_order_release);
        return MT_DumpStatus::UnsafeState;
    }

    Com_Printf(23, "********************************\n");
    for (uint32_t nodeNum = 0;
         nodeNum < MEMORY_NODE_COUNT;
         ++nodeNum)
    {
        const uint8_t type = mt_dumpSnapshot.types[nodeNum];
        if (type)
        {
            Com_Printf(
                23,
                "%s: '#%u' (%u)\n",
                mt_type_names[type],
                static_cast<unsigned>(nodeNum),
                static_cast<unsigned>(mt_dumpSnapshot.sizes[nodeNum]));
        }
    }

    Com_Printf(23, "********************************\n");
    for (int size = 0; size <= MEMORY_NODE_BITS; ++size)
    {
        const int subTreeSize = mt_dumpSnapshot.subTreeSizes[size];
        Com_Printf(
            23,
            "%d subtree has %d * %d = %d free buckets\n",
            size,
            subTreeSize,
            1 << size,
            subTreeSize * (1 << size));
    }

    Com_Printf(23, "********************************\n");
    for (int type = 1; type < kMemoryTreeTypeCount; ++type)
    {
        Com_Printf(
            23,
            "'%s' allocated: %d\n",
            mt_type_names[type],
            mt_dumpSnapshot.typeUsage[type]);
    }
    Com_Printf(23, "********************************\n");
    Com_Printf(
        23,
        "total memory alloc buckets: %d (%d instances)\n",
        mt_dumpSnapshot.totalAllocBuckets,
        mt_dumpSnapshot.totalAlloc);
    Com_Printf(
        23,
        "total memory free buckets: %d\n",
        (MEMORY_NODE_COUNT - 1) - mt_dumpSnapshot.totalAllocBuckets);
    Com_Printf(23, "********************************\n");

    iassert(mt_dumpSnapshot.totalBuckets == MEMORY_NODE_COUNT - 1);
    mt_dumpSnapshotInUse.clear(std::memory_order_release);
    return MT_DumpStatus::Success;
}
} // namespace

void MT_DumpTree()
{
    (void)MT_DumpTreeInternal();
}

char const* MT_NodeInfoString(uint32_t nodeNum)
{
    MT_AllocationInfo info{};
    MT_AllocationInfoStatus status =
        MT_AllocationInfoStatus::InvalidArgumentNoChange;
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (MT_RejectUnleasedAccessForActiveLeaseLocked())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return "<UNAVAILABLE>";
    }
    if (nodeNum < MEMORY_NODE_COUNT && MT_IsCoreStateValidNoReport())
    {
        status = MT_GetAllocationInfoLockedNoReport(nodeNum, &info);
        if (status == MT_AllocationInfoStatus::Success
            && !MT_IsAllocationIntervalExactNoReport(
                nodeNum, info.type, info.size))
        {
            status = MT_AllocationInfoStatus::UnsafeFailure;
        }
    }
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    if (status == MT_AllocationInfoStatus::NotAllocatedNoChange)
        return "<FREE>";
    if (status != MT_AllocationInfoStatus::Success)
        return "<UNAVAILABLE>";
    return va(
        "%s: '#%u' (%u)",
        mt_type_names[info.type],
        static_cast<unsigned>(nodeNum),
        static_cast<unsigned>(info.size));
}

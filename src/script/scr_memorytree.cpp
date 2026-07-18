#include "scr_memorytree.h"
#include "scr_stringlist.h"

#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
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
static_assert(sizeof(MT_FreeNodeLinkShadow) == 4);

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
    return &scrMemTreeGlob.nodes[MT_AllocIndex(numBytes, type)];
}

namespace
{
constexpr int kMemoryTreeTypeCount =
    static_cast<int>(sizeof(mt_type_names) / sizeof(mt_type_names[0]));

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

namespace
{
MT_AllocIndexStatus MT_TryAllocIndexImpl(
    int numBytes,
    int type,
    uint16_t *outIndex,
    const bool completeValidation) noexcept
{
    int size = 0;
    if (!outIndex || type <= 0 || type >= kMemoryTreeTypeCount ||
        !MT_TryGetSizeNoReport(numBytes, &size))
    {
        return MT_AllocIndexStatus::InvalidArgumentNoChange;
    }

    PROF_SCOPED("scriptMemory");

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!(completeValidation
            ? MT_IsCoreStateValidNoReport()
            : MT_IsBasicCoreStateValidNoReport()))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_AllocIndexStatus::UnsafeFailure;
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
            Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
            return MT_AllocIndexStatus::UnsafeFailure;
        }
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
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    *outIndex = nodeNum;
    return MT_AllocIndexStatus::Success;
}

MT_AllocationInfoStatus MT_TryGetAllocationInfoImpl(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo,
    const bool completeValidation) noexcept
{
    if (!outInfo || nodeNum == 0 || nodeNum >= MEMORY_NODE_COUNT)
    {
        return MT_AllocationInfoStatus::InvalidArgumentNoChange;
    }

    MT_AllocationInfo allocationInfo{};
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!(completeValidation
            ? MT_IsCoreStateValidNoReport()
            : MT_IsBasicAccountingStateValidNoReport()))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_AllocationInfoStatus::UnsafeFailure;
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
    return MT_TryAllocIndexImpl(numBytes, type, outIndex, true);
}

MT_AllocIndexStatus MT_TryAllocIndexLegacy(
    int numBytes,
    int type,
    uint16_t *outIndex) noexcept
{
    return MT_TryAllocIndexImpl(numBytes, type, outIndex, false);
}

unsigned short MT_AllocIndex(int numBytes, int type)
{
    uint16_t nodeNum = 0;
    const MT_AllocIndexStatus status =
        MT_TryAllocIndexLegacy(numBytes, type, &nodeNum);
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
    return MT_TryGetAllocationInfoImpl(nodeNum, outInfo, true);
}

MT_AllocationInfoStatus MT_TryGetAllocationInfoLegacy(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo) noexcept
{
    return MT_TryGetAllocationInfoImpl(nodeNum, outInfo, false);
}

bool MT_TryValidateState() noexcept
{
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    const bool valid = MT_IsCoreStateValidNoReport();
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
    return MT_GetSize(oldNumBytes) >= MT_GetSize(newNumbytes);
}

void MT_RemoveHeadMemoryNode(int size)
{
    iassert(size >= 0 && size <= MEMORY_NODE_BITS);
    if (size < 0 || size > MEMORY_NODE_BITS)
        return;

    MT_ResetPreflightTransaction();
    const bool valid = MT_IsBasicCoreStateValidNoReport()
        && mt_freeNodeCountShadow != 0
        && scrMemTreeGlob.head[size] != 0
        && MT_CanRemoveHeadMemoryNodeNoReport(size);
    iassert(valid);
    if (valid)
        MT_RemoveHeadMemoryNodeCommitNoReport(size);
}

namespace
{
MT_FreeIndexStatus MT_TryFreeIndexImpl(
    uint32_t nodeNum,
    int numBytes,
    const bool completeValidation) noexcept
{
    int size = 0;
    if (nodeNum == 0 || nodeNum >= MEMORY_NODE_COUNT ||
        !MT_TryGetSizeNoReport(numBytes, &size))
    {
        return MT_FreeIndexStatus::InvalidArgumentNoChange;
    }

    PROF_SCOPED("scriptMemory");

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!(completeValidation
            ? MT_IsCoreStateValidNoReport()
            : MT_IsBasicCoreStateValidNoReport()))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
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
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return freePathValid && foundFreeNode == shadowFound
            ? MT_FreeIndexStatus::OwnershipMismatchNoChange
            : MT_FreeIndexStatus::UnsafeFailure;
    }
    if (allocationStatus != MT_AllocationInfoStatus::Success)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
    }
    if (!MT_IsAllocationIntervalExactNoReport(
            nodeNum, allocationInfo.type, allocationInfo.size) ||
        !MT_IsFreeTreeIntervalValidNoReport(
            nodeNum, allocationInfo.size, 0, -1))
    {
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
            Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
            return MT_FreeIndexStatus::UnsafeFailure;
        }
        const bool shadowBuddyFound =
            mt_freeNodeSizeShadow[buddyNode] ==
                static_cast<uint8_t>(mergedSize + 1);
        if (buddyFound != shadowBuddyFound)
        {
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
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    return MT_FreeIndexStatus::Success;
}
} // namespace

MT_FreeIndexStatus MT_TryFreeIndex(
    uint32_t nodeNum,
    int numBytes) noexcept
{
    return MT_TryFreeIndexImpl(nodeNum, numBytes, true);
}

MT_FreeIndexStatus MT_TryFreeIndexLegacy(
    uint32_t nodeNum,
    int numBytes) noexcept
{
    return MT_TryFreeIndexImpl(nodeNum, numBytes, false);
}

void MT_FreeIndex(uint32_t nodeNum, int numBytes)
{
    const int size = MT_GetSize(numBytes);
    iassert(size >= 0 && size <= MEMORY_NODE_BITS);
    iassert(nodeNum > 0 && nodeNum < MEMORY_NODE_COUNT);
    (void)size;

    const MT_FreeIndexStatus status =
        MT_TryFreeIndexLegacy(nodeNum, numBytes);
    if (status != MT_FreeIndexStatus::Success)
    {
        if (status == MT_FreeIndexStatus::UnsafeFailure)
        {
            MT_UnsafeErrorNoDump("MT_FreeIndex", numBytes);
        }
        else
        {
            iassert(status == MT_FreeIndexStatus::Success);
            MT_Error("MT_FreeIndex", numBytes);
        }
    }
}

bool __cdecl MT_RemoveMemoryNode(int oldNode, uint32_t size)
{
    iassert(size <= MEMORY_NODE_BITS);
    if (size > MEMORY_NODE_BITS || oldNode <= 0
        || oldNode >= MEMORY_NODE_COUNT)
    {
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
    iassert(valid);
    if (!valid || !found)
        return false;
    return MT_RemoveMemoryNodeCommitNoReport(
        oldNode, static_cast<int>(size));
}

void MT_Free(byte* p, int numBytes)
{
	iassert(((MemoryNode*)p - scrMemTreeGlob.nodes >= 0 && (MemoryNode*)p - scrMemTreeGlob.nodes < MEMORY_NODE_COUNT));

    MT_FreeIndex((MemoryNode *)p - scrMemTreeGlob.nodes, numBytes);
}

int MT_GetSize(int numBytes)
{
    int numBuckets; // [esp+4h] [ebp-4h]

    iassert(numBytes > 0);

    if (numBytes >= MEMORY_NODE_COUNT)
    {
        MT_Error("MT_GetSize: max allocation exceeded", numBytes);
        return 0;
    }
    else
    {
        numBuckets = (numBytes + 11) / 12 - 1;
        if (numBuckets > 255)
            return scrMemTreeGlob.logBits[numBuckets >> 8] + 8;
        else
            return scrMemTreeGlob.logBits[numBuckets];
    }
}

int MT_GetScore(int num)
{
    iassert(num != 0);

    const int value = MEMORY_NODE_COUNT - num;
    iassert(value != 0);
    return MT_GetScoreFromDeltaNoReport(value);
}

int MT_GetSubTreeSize(int nodeNum)
{
    if (!nodeNum)
        return 0;

    return MT_GetSubTreeSize(scrMemTreeGlob.nodes[nodeNum].prev) + MT_GetSubTreeSize(scrMemTreeGlob.nodes[nodeNum].next) + 1;
}

void MT_AddMemoryNode(int newNode, int size)
{
    iassert(size >= 0 && size <= MEMORY_NODE_BITS);
    if (size < 0 || size > MEMORY_NODE_BITS || newNode <= 0
        || newNode >= MEMORY_NODE_COUNT)
    {
        return;
    }

    MT_ResetPreflightTransaction();
    const bool valid = MT_IsBasicCoreStateValidNoReport()
        && mt_freeNodeCountShadow < MEMORY_NODE_COUNT - 1
        && MT_CanAddMemoryNodeNoReport(
            static_cast<uint16_t>(newNode), size, 0);
    iassert(valid);
    if (valid)
        MT_AddMemoryNodeCommitNoReport(newNode, size);
}

void MT_Error(const char* funcName, int numBytes)
{
    MT_DumpTree();
    Com_Printf(23, "%s: failed memory allocation of %d bytes for script usage\n", funcName, numBytes);
    Com_Error(ERR_FATAL, "MT_Error (KISAK)\n");
    //Scr_TerminalError("failed memory allocation for script usage");
}

void MT_DumpTree()
{
    int mt_type_usage[22];

    memset(mt_type_usage, 0, sizeof(mt_type_usage));

    Com_Printf(23, "********************************\n");

    int totalAlloc = 0;
    int totalAllocBuckets = 0;
    int totalBuckets = 0;

    for (int nodeNum = 0; nodeNum < MEMORY_NODE_COUNT; nodeNum++)
    {
        int type = scrMemTreeDebugGlob.mt_usage[nodeNum];
        if (type)
        {
            Com_Printf(23, "%s\n", MT_NodeInfoString(nodeNum));
            ++totalAlloc;
            totalAllocBuckets += 1 << scrMemTreeDebugGlob.mt_usage_size[nodeNum];
            mt_type_usage[type] += 1 << scrMemTreeDebugGlob.mt_usage_size[nodeNum];
        }
    }

    iassert(scrMemTreeGlob.totalAlloc == totalAlloc);
    iassert(scrMemTreeGlob.totalAllocBuckets == totalAllocBuckets);
    (void)totalAlloc;
    (void)totalAllocBuckets;

    Com_Printf(23, "********************************\n");

    totalBuckets = scrMemTreeGlob.totalAllocBuckets;

    for (int size = 0; size <= MEMORY_NODE_BITS; ++size)
    {
        int subTreeSize = MT_GetSubTreeSize(scrMemTreeGlob.head[size]);
        totalBuckets += subTreeSize * (1 << size);
        Com_Printf(
            23,
            "%d subtree has %d * %d = %d free buckets\n",
            size,
            subTreeSize,
            1 << size,
            subTreeSize * (1 << size));
    }

    Com_Printf(23, "********************************\n");
    for (int type = 1; type < 22; ++type)
        Com_Printf(23, "'%s' allocated: %d\n", mt_type_names[type], mt_type_usage[type]);
    Com_Printf(23, "********************************\n");
    Com_Printf(
        23,
        "total memory alloc buckets: %d (%d instances)\n",
        scrMemTreeGlob.totalAllocBuckets,
        scrMemTreeGlob.totalAlloc);
    Com_Printf(23, "total memory free buckets: %d\n", 0xFFFF - scrMemTreeGlob.totalAllocBuckets);
    Com_Printf(23, "********************************\n");

    iassert(totalBuckets == (1 << MEMORY_NODE_BITS) - 1);
    (void)totalBuckets;
}

char const* MT_NodeInfoString(uint32_t nodeNum)
{
    int type = scrMemTreeDebugGlob.mt_usage[nodeNum];

    if (!scrMemTreeDebugGlob.mt_usage[nodeNum])
        return "<FREE>";

    int v3 = scrMemTreeDebugGlob.mt_usage_size[nodeNum];
    const char* v1 = SL_DebugConvertToString(nodeNum);
    return va("%s: '%s' (%d)", mt_type_names[type], v1, v3);
}

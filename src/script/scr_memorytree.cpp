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

    for (int i = 0; i < MEMORY_NODE_BITS; ++i)
        MT_AddMemoryNode(1 << i, i);

    scrMemTreeGlob.totalAlloc = 0;
    scrMemTreeGlob.totalAllocBuckets = 0;
    memset(scrMemTreeDebugGlob.mt_usage, 0, sizeof(scrMemTreeDebugGlob.mt_usage));
    memset(scrMemTreeDebugGlob.mt_usage_size, 0, sizeof(scrMemTreeDebugGlob.mt_usage_size));

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

int MT_GetScoreNoReport(const uint16_t nodeNum) noexcept
{
    union MTnum_t
    {
        int i;
        uint8_t b[4];
    };

    MTnum_t value{};
    value.i = MEMORY_NODE_COUNT - nodeNum;
    uint8_t bits = scrMemTreeGlob.leftBits[value.b[0]];
    if (value.b[0] == 0)
        bits = static_cast<uint8_t>(
            bits + scrMemTreeGlob.leftBits[value.b[1]]);
    return value.i
        - (scrMemTreeGlob.numBits[value.b[1]]
            + scrMemTreeGlob.numBits[value.b[0]])
        + (1 << bits);
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

bool MT_IsBasicCoreStateValidNoReport() noexcept
{
    return scrMemTreePub.mt_buffer
            == reinterpret_cast<char *>(&scrMemTreeGlob.nodes) &&
        MT_AreBitTablesValidNoReport() &&
        scrMemTreeGlob.nodes[0].prev == 0 &&
        scrMemTreeGlob.nodes[0].next == 0 &&
        scrMemTreeDebugGlob.mt_usage[0] == 0 &&
        scrMemTreeDebugGlob.mt_usage_size[0] == 0 &&
        scrMemTreeGlob.totalAlloc >= 0 &&
        scrMemTreeGlob.totalAlloc < MEMORY_NODE_COUNT &&
        scrMemTreeGlob.totalAllocBuckets >= 0 &&
        scrMemTreeGlob.totalAllocBuckets <= MEMORY_NODE_COUNT - 1 &&
        scrMemTreeGlob.totalAlloc <= scrMemTreeGlob.totalAllocBuckets &&
        ((scrMemTreeGlob.totalAlloc == 0) ==
         (scrMemTreeGlob.totalAllocBuckets == 0));
}

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

bool MT_IsValidFreeNodeNoReport(uint32_t nodeNum, int size) noexcept
{
    if (!MT_IsValidNodeRangeNoReport(nodeNum, size))
    {
        return false;
    }

    return scrMemTreeDebugGlob.mt_usage[nodeNum] == 0 &&
        scrMemTreeDebugGlob.mt_usage_size[nodeNum] == 0;
}

MT_AllocationInfoStatus MT_GetAllocationInfoLockedNoReport(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo) noexcept
{
    const uint8_t type = scrMemTreeDebugGlob.mt_usage[nodeNum];
    const uint8_t size = scrMemTreeDebugGlob.mt_usage_size[nodeNum];
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
            if (!MT_IsValidFreeNodeNoReport(nodeNum, size) ||
                !MT_TryMarkPartitionRangeNoReport(nodeNum, size))
            {
                return false;
            }

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

void MT_ResetPreflightNodes() noexcept
{
    memset(mt_preflightVisited, 0, sizeof(mt_preflightVisited));
}

void MT_ResetPreflightTransaction() noexcept
{
    memset(
        mt_preflightTransactionVisited,
        0,
        sizeof(mt_preflightTransactionVisited));
}

bool MT_RecordScanNode(uint16_t nodeNum) noexcept
{
    const uint32_t byteIndex = nodeNum >> 3;
    const uint8_t bitMask = static_cast<uint8_t>(1u << (nodeNum & 7u));
    if ((mt_preflightVisited[byteIndex] & bitMask) != 0)
    {
        return false;
    }

    mt_preflightVisited[byteIndex] |= bitMask;
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

    mt_preflightTransactionVisited[byteIndex] |= bitMask;
    return true;
}

bool MT_ScanIsDisjointFromPreflightTransaction() noexcept
{
    for (uint32_t byteIndex = 0;
         byteIndex < sizeof(mt_preflightVisited);
         ++byteIndex)
    {
        if ((mt_preflightVisited[byteIndex] &
             mt_preflightTransactionVisited[byteIndex]) != 0)
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
    for (uint32_t byteIndex = 0;
         byteIndex < sizeof(mt_preflightVisited);
         ++byteIndex)
    {
        mt_preflightTransactionVisited[byteIndex] |=
            mt_preflightVisited[byteIndex];
    }
    return true;
}

bool MT_CanRemoveHeadMemoryNodeNoReport(int size) noexcept
{
    MT_ResetPreflightNodes();
    uint16_t nodeNum = scrMemTreeGlob.head[size];
    bool foundNode = false;

    while (nodeNum != 0)
    {
        if (!MT_IsValidFreeNodeNoReport(nodeNum, size) ||
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
    if (!MT_IsValidNodeRangeNoReport(newNode, size) ||
        (newNode != prospectiveAllocatedNode &&
         !MT_IsValidFreeNodeNoReport(newNode, size)))
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
        if (node == newNode || !MT_IsValidFreeNodeNoReport(node, size) ||
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
                    (!MT_IsValidFreeNodeNoReport(node, size) ||
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
        if (!MT_IsValidFreeNodeNoReport(node, size) ||
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
                if (!MT_IsValidFreeNodeNoReport(nextNode, size) ||
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

// Mutation helpers used only after the complete partition and every affected
// tree path have been preflighted. They deliberately contain no assertion or
// reporting calls, so the typed try surface cannot escape through a legacy
// diagnostic between partial mutations.
void MT_RemoveHeadMemoryNodeCommitNoReport(const int size) noexcept
{
    uint16_t *parentNode = &scrMemTreeGlob.head[size];
    MemoryNode oldNodeValue = scrMemTreeGlob.nodes[*parentNode];
    while (true)
    {
        uint16_t oldNode = 0;
        if (oldNodeValue.prev == 0)
        {
            oldNode = oldNodeValue.next;
            *parentNode = oldNode;
            if (oldNode == 0)
                return;
            parentNode = &scrMemTreeGlob.nodes[oldNode].next;
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
            parentNode = prevScore >= nextScore
                ? &scrMemTreeGlob.nodes[oldNode].prev
                : &scrMemTreeGlob.nodes[oldNode].next;
        }
        else
        {
            oldNode = oldNodeValue.prev;
            *parentNode = oldNode;
            parentNode = &scrMemTreeGlob.nodes[oldNode].prev;
        }

        const MemoryNode displacedValue = oldNodeValue;
        oldNodeValue = scrMemTreeGlob.nodes[oldNode];
        scrMemTreeGlob.nodes[oldNode] = displacedValue;
    }
}

bool MT_RemoveMemoryNodeCommitNoReport(
    int oldNode,
    const int size) noexcept
{
    int nodeNum = 0;
    int level = MEMORY_NODE_COUNT;
    uint16_t *parentNode = &scrMemTreeGlob.head[size];
    for (int node = *parentNode; node != 0; node = *parentNode)
    {
        if (oldNode == node)
        {
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
                            parentNode =
                                &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                        }
                        else
                        {
                            oldNode = oldNodeValue.next;
                            *parentNode = oldNodeValue.next;
                            parentNode =
                                &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                        }
                    }
                    else
                    {
                        oldNode = oldNodeValue.prev;
                        *parentNode = oldNodeValue.prev;
                        parentNode =
                            &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                    }
                }
                else
                {
                    oldNode = oldNodeValue.next;
                    *parentNode = oldNodeValue.next;
                    if (oldNodeValue.next == 0)
                        return true;
                    parentNode =
                        &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                }

                const MemoryNode displacedValue = oldNodeValue;
                oldNodeValue = scrMemTreeGlob.nodes[oldNode];
                scrMemTreeGlob.nodes[oldNode] = displacedValue;
            }
        }

        if (oldNode == nodeNum)
            return false;
        level >>= 1;
        if (oldNode >= nodeNum)
        {
            parentNode = &scrMemTreeGlob.nodes[node].next;
            nodeNum += level;
        }
        else
        {
            parentNode = &scrMemTreeGlob.nodes[node].prev;
            nodeNum -= level;
        }
    }
    return false;
}

void MT_AddMemoryNodeCommitNoReport(
    int newNode,
    const int size) noexcept
{
    uint16_t *parentNode = &scrMemTreeGlob.head[size];
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
                    scrMemTreeGlob.nodes[newNode] =
                        scrMemTreeGlob.nodes[node];
                    if (node == 0)
                        return;
                    level >>= 1;
                    if (node >= nodeNum)
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].next;
                        nodeNum += level;
                    }
                    else
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].prev;
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
                nodeNum += level;
            }
            else
            {
                parentNode = &scrMemTreeGlob.nodes[node].prev;
                nodeNum -= level;
            }
            node = *parentNode;
        } while (node != 0);
    }

    *parentNode = static_cast<uint16_t>(newNode);
    scrMemTreeGlob.nodes[newNode].prev = 0;
    scrMemTreeGlob.nodes[newNode].next = 0;
}
} // namespace

MT_AllocIndexStatus MT_TryAllocIndex(
    int numBytes,
    int type,
    uint16_t *outIndex) noexcept
{
    int size = 0;
    if (!outIndex || type <= 0 || type >= kMemoryTreeTypeCount ||
        !MT_TryGetSizeNoReport(numBytes, &size))
    {
        return MT_AllocIndexStatus::InvalidArgumentNoChange;
    }

    PROF_SCOPED("scriptMemory");

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!MT_IsCoreStateValidNoReport())
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
    MT_ResetPreflightTransaction();
    if (scrMemTreeGlob.totalAllocBuckets >
            (MEMORY_NODE_COUNT - 1) - allocationBuckets ||
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

    scrMemTreeDebugGlob.mt_usage[nodeNum] = static_cast<uint8_t>(type);
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = static_cast<uint8_t>(size);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);

    *outIndex = nodeNum;
    return MT_AllocIndexStatus::Success;
}

unsigned short MT_AllocIndex(int numBytes, int type)
{
    uint16_t nodeNum = 0;
    const MT_AllocIndexStatus status =
        MT_TryAllocIndex(numBytes, type, &nodeNum);
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
    if (!outInfo || nodeNum == 0 || nodeNum >= MEMORY_NODE_COUNT)
    {
        return MT_AllocationInfoStatus::InvalidArgumentNoChange;
    }

    MT_AllocationInfo allocationInfo{};
    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!MT_IsCoreStateValidNoReport())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_AllocationInfoStatus::UnsafeFailure;
    }

    const MT_AllocationInfoStatus status =
        MT_GetAllocationInfoLockedNoReport(nodeNum, &allocationInfo);
    Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
    if (status == MT_AllocationInfoStatus::Success)
    {
        *outInfo = allocationInfo;
    }
    return status;
}

bool MT_Realloc(int oldNumBytes, int newNumbytes)
{
    return MT_GetSize(oldNumBytes) >= MT_GetSize(newNumbytes);
}

void MT_RemoveHeadMemoryNode(int size)
{
    MemoryNode tempNodeValue;
    int oldNode;
    MemoryNode oldNodeValue;
    uint16_t *parentNode;
    int prevScore;
    int nextScore;

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);

    parentNode = &scrMemTreeGlob.head[size];
    oldNodeValue = scrMemTreeGlob.nodes[*parentNode];

    while (1)
    {
        if (!oldNodeValue.prev)
        {
            oldNode = oldNodeValue.next;
            *parentNode = oldNodeValue.next;
            if (!oldNode)
            {
                break;
            }
            parentNode = &scrMemTreeGlob.nodes[oldNode].next;
        }
        else
        {
            if (oldNodeValue.next)
            {
                prevScore = MT_GetScore(oldNodeValue.prev);
                nextScore = MT_GetScore(oldNodeValue.next);

                iassert(prevScore != nextScore);

                if (prevScore >= nextScore)
                {
                    oldNode = oldNodeValue.prev;
                    *parentNode = oldNode;
                    parentNode = &scrMemTreeGlob.nodes[oldNode].prev;
                }
                else
                {
                    oldNode = oldNodeValue.next;
                    *parentNode = oldNode;
                    parentNode = &scrMemTreeGlob.nodes[oldNode].next;
                }
            }
            else
            {
                oldNode = oldNodeValue.prev;
                *parentNode = oldNode;
                parentNode = &scrMemTreeGlob.nodes[oldNode].prev;
            }
        }
        iassert(oldNode != 0);

        tempNodeValue = oldNodeValue;
        oldNodeValue = scrMemTreeGlob.nodes[oldNode];
        scrMemTreeGlob.nodes[oldNode] = tempNodeValue;
    }
}

MT_FreeIndexStatus MT_TryFreeIndex(
    uint32_t nodeNum,
    int numBytes) noexcept
{
    int size = 0;
    if (nodeNum == 0 || nodeNum >= MEMORY_NODE_COUNT ||
        !MT_TryGetSizeNoReport(numBytes, &size))
    {
        return MT_FreeIndexStatus::InvalidArgumentNoChange;
    }

    PROF_SCOPED("scriptMemory");

    Sys_EnterCriticalSection(CRITSECT_MEMORY_TREE);
    if (!MT_IsCoreStateValidNoReport())
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
    }

    MT_AllocationInfo allocationInfo{};
    const MT_AllocationInfoStatus allocationStatus =
        MT_GetAllocationInfoLockedNoReport(nodeNum, &allocationInfo);
    if (allocationStatus == MT_AllocationInfoStatus::NotAllocatedNoChange)
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::OwnershipMismatchNoChange;
    }
    if (allocationStatus != MT_AllocationInfoStatus::Success)
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
        if (!buddyFound)
        {
            break;
        }

        mergedNode &= ~lowBit;
        ++mergedSize;
    }

    if (!MT_CanAddMemoryNodeNoReport(
            static_cast<uint16_t>(mergedNode),
            mergedSize,
            static_cast<uint16_t>(nodeNum)))
    {
        Sys_LeaveCriticalSection(CRITSECT_MEMORY_TREE);
        return MT_FreeIndexStatus::UnsafeFailure;
    }

    --scrMemTreeGlob.totalAlloc;
    scrMemTreeGlob.totalAllocBuckets -= 1 << size;
    scrMemTreeDebugGlob.mt_usage[nodeNum] = 0;
    scrMemTreeDebugGlob.mt_usage_size[nodeNum] = 0;

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

void MT_FreeIndex(uint32_t nodeNum, int numBytes)
{
    const int size = MT_GetSize(numBytes);
    iassert(size >= 0 && size <= MEMORY_NODE_BITS);
    iassert(nodeNum > 0 && nodeNum < MEMORY_NODE_COUNT);
    (void)size;

    const MT_FreeIndexStatus status = MT_TryFreeIndex(nodeNum, numBytes);
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
    MemoryNode tempNodeValue;
    int node;
    MemoryNode oldNodeValue;
    int nodeNum;
    uint16_t *parentNode;
    int prevScore;
    int nextScore;
    int level;

    iassert(size <= MEMORY_NODE_BITS);

    nodeNum = 0;
    level = MEMORY_NODE_COUNT;
    parentNode = &scrMemTreeGlob.head[size];

    for (node = *parentNode; node; node = *parentNode)
    {
        if (oldNode == node)
        {
            oldNodeValue = scrMemTreeGlob.nodes[oldNode];

            while (1)
            {
                if (oldNodeValue.prev)
                {
                    if (oldNodeValue.next)
                    {
                        prevScore = MT_GetScore(oldNodeValue.prev);
                        nextScore = MT_GetScore(oldNodeValue.next);

                        iassert(prevScore != nextScore);

                        if (prevScore >= nextScore)
                        {
                            oldNode = oldNodeValue.prev;
                            *parentNode = oldNodeValue.prev;
                            parentNode = &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                        }
                        else
                        {
                            oldNode = oldNodeValue.next;
                            *parentNode = oldNodeValue.next;
                            parentNode = &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                        }
                    }
                    else
                    {
                        oldNode = oldNodeValue.prev;
                        *parentNode = oldNodeValue.prev;
                        parentNode = &scrMemTreeGlob.nodes[oldNodeValue.prev].prev;
                    }
                }
                else
                {
                    oldNode = oldNodeValue.next;
                    *parentNode = oldNodeValue.next;

                    if (!oldNodeValue.next)
                    {
                        return true;
                    }

                    parentNode = &scrMemTreeGlob.nodes[oldNodeValue.next].next;
                }

                iassert(oldNode != 0);

                tempNodeValue = oldNodeValue;
                oldNodeValue = scrMemTreeGlob.nodes[oldNode];
                scrMemTreeGlob.nodes[oldNode] = tempNodeValue;
            }
        }

        if (oldNode == nodeNum)
        {
            return false;
        }

        level >>= 1;

        if (oldNode >= nodeNum)
        {
            parentNode = &scrMemTreeGlob.nodes[node].next;
            nodeNum += level;
        }
        else
        {
            parentNode = &scrMemTreeGlob.nodes[node].prev;
            nodeNum -= level;
        }
    }

    return false;
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
    char bits;

    iassert(num != 0);

    union MTnum_t
    {
        int i;
        uint8_t b[4];
    };

    MTnum_t mtnum;

    mtnum.i = MEMORY_NODE_COUNT - num;
    iassert(mtnum.i != 0);

    bits = scrMemTreeGlob.leftBits[mtnum.b[0]];

    if (!mtnum.b[0])
    {
        bits += scrMemTreeGlob.leftBits[mtnum.b[1]];
    }

    return mtnum.i - (scrMemTreeGlob.numBits[mtnum.b[1]] + scrMemTreeGlob.numBits[mtnum.b[0]]) + (1 << bits);
}

int MT_GetSubTreeSize(int nodeNum)
{
    if (!nodeNum)
        return 0;

    return MT_GetSubTreeSize(scrMemTreeGlob.nodes[nodeNum].prev) + MT_GetSubTreeSize(scrMemTreeGlob.nodes[nodeNum].next) + 1;
}

void MT_AddMemoryNode(int newNode, int size)
{
    int node;
    int nodeNum;
    int newScore;
    uint16_t *parentNode;
    int level;
    int score;

    iassert(size >= 0 && size <= MEMORY_NODE_BITS);

    parentNode = &scrMemTreeGlob.head[size];
    node = (uint16_t)*parentNode;

    if (node)
    {
        newScore = MT_GetScore(newNode);
        nodeNum = 0;
        level = MEMORY_NODE_COUNT;
        do
        {
            iassert(newNode != node);
            score = MT_GetScore(node);

            iassert(score != newScore);

            if (score < newScore)
            {
                while (1)
                {
                    iassert(node == *parentNode);
                    iassert(node != newNode);

                    *parentNode = newNode;
                    scrMemTreeGlob.nodes[newNode] = scrMemTreeGlob.nodes[node];
                    if (!node)
                    {
                        break;
                    }
                    level >>= 1;

                    iassert(node != nodeNum);

                    if (node >= nodeNum)
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].next;
                        nodeNum += level;
                    }
                    else
                    {
                        parentNode = &scrMemTreeGlob.nodes[newNode].prev;
                        nodeNum -= level;
                    }
                    newNode = node;
                    node = *parentNode;
                }
                return;
            }
            level >>= 1;

            iassert(newNode != nodeNum);

            if (newNode >= nodeNum)
            {
                parentNode = &scrMemTreeGlob.nodes[node].next;
                nodeNum += level;
            }
            else
            {
                parentNode = &scrMemTreeGlob.nodes[node].prev;
                nodeNum -= level;
            }

            node = *parentNode;
        } while (node);
    }

    *parentNode = newNode;

    scrMemTreeGlob.nodes[newNode].prev = 0;
    scrMemTreeGlob.nodes[newNode].next = 0;
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

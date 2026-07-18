#pragma once

#include <universal/kisak_abi.h>

struct MemoryNode // sizeof=0xC
{                                       // XREF: scrMemTreeGlob_t/r
    uint16_t prev;              // XREF: MT_Init(void)+46/w
    uint16_t next;              // XREF: MT_Init(void)+4E/w
    uint32_t padding[2];            // XREF: MT_RemoveHeadMemoryNode+61/w
};
static_assert(sizeof(MemoryNode) == 12);

#define MEMORY_NODE_BITS 16
#define MEMORY_NODE_COUNT 0x10000
#define NUM_BUCKETS 256

enum class MT_AllocIndexStatus : uint8_t
{
    Success,
    InvalidArgumentNoChange,
    InsufficientCapacityNoChange,
    UnsafeFailure,
};

enum class MT_FreeIndexStatus : uint8_t
{
    Success,
    InvalidArgumentNoChange,
    OwnershipMismatchNoChange,
    UnsafeFailure,
};

struct MT_AllocationInfo
{
    uint8_t type;
    uint8_t size;
    uint16_t reserved;
    uint32_t capacityBytes;
};
RUNTIME_SIZE(MT_AllocationInfo, 0x8, 0x8);

enum class MT_AllocationInfoStatus : uint8_t
{
    Success,
    InvalidArgumentNoChange,
    NotAllocatedNoChange,
    UnsafeFailure,
};

struct KISAK_ALIGNAS(128) scrMemTreeGlob_t // sizeof=0xC0380
{                                       // XREF: .data:scrMemTreeGlob/r
    MemoryNode nodes[MEMORY_NODE_COUNT];            // XREF: MT_Init(void)+46/w
                                        // MT_Init(void)+4E/w ...
    uint8_t leftBits[NUM_BUCKETS];      // XREF: MT_InitBits+89/w
                                        // MT_GetScore+88/r ...
    uint8_t numBits[NUM_BUCKETS];       // XREF: MT_InitBits+59/w
                                        // MT_GetScore+6A/r ...
    uint8_t logBits[NUM_BUCKETS];       // XREF: MT_InitBits+BB/w
                                        // MT_GetSize+55/r ...
    uint16_t head[MEMORY_NODE_BITS + 1];// 0x242E200          // XREF: MT_DumpTree(void)+14B/r
                                        // MT_Init(void)+3A/w ...
    // padding byte
    // padding byte
    int totalAlloc;                     // XREF: MT_DumpTree(void):loc_59E783/r
                                        // MT_DumpTree(void)+1FB/r ...
    int totalAllocBuckets;              // XREF: MT_DumpTree(void):loc_59E7AE/r
};
static_assert(sizeof(scrMemTreeGlob_t) == 0xC0380);

static const char* mt_type_names[22] =
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

int MT_GetSubTreeSize(int nodeNum);
void MT_DumpTree(void);
void MT_FreeIndex(uint32_t nodeNum, int numBytes);
// The try path never reports. Every non-Success result leaves allocation
// metadata, accounting, and all free-tree links unchanged.
[[nodiscard]] MT_FreeIndexStatus MT_TryFreeIndex(
    uint32_t nodeNum,
    int numBytes) noexcept;

// Legacy compatibility queries authenticate only the touched allocation
// interval. Legacy allocation/free mutations authenticate every free-tree
// head plus the mirrored links and membership on paths they consume or
// change; they never rescan the complete fragmented forest or 64K-bucket
// allocation partition. All remain report-free so a caller holding an outer
// engine lock can unwind it before invoking a legacy reporter.
// Typed transaction code must use the complete MT_Try* surface.
[[nodiscard]] MT_FreeIndexStatus MT_TryFreeIndexLegacy(
    uint32_t nodeNum,
    int numBytes) noexcept;

// The query never reports and publishes the complete result only on Success.
// Every other result leaves *outInfo unchanged.
[[nodiscard]] MT_AllocationInfoStatus MT_TryGetAllocationInfo(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo) noexcept;
[[nodiscard]] MT_AllocationInfoStatus MT_TryGetAllocationInfoLegacy(
    uint32_t nodeNum,
    MT_AllocationInfo *outInfo) noexcept;
// Performs one report-free exhaustive allocator preflight without mutation.
[[nodiscard]] bool MT_TryValidateState() noexcept;

void MT_Free(unsigned char* p, int numBytes);
bool MT_Realloc(int oldNumBytes, int newNumbytes);

void MT_Init(void);
// The try path never reports. Every non-Success result leaves both the
// allocator and *outIndex unchanged; UnsafeFailure denotes a failed internal
// invariant preflight rather than ordinary capacity exhaustion.
[[nodiscard]] MT_AllocIndexStatus MT_TryAllocIndex(
    int numBytes,
    int type,
    uint16_t *outIndex) noexcept;
[[nodiscard]] MT_AllocIndexStatus MT_TryAllocIndexLegacy(
    int numBytes,
    int type,
    uint16_t *outIndex) noexcept;
unsigned short MT_AllocIndex(int numBytes, int type);
void* MT_Alloc(int numBytes, int type);

//void TRACK_scr_memorytree(void);
//uint32_t Scr_GetStringUsage(void);

char const* MT_NodeInfoString(uint32_t nodeNum);
int MT_GetScore(int num);
void MT_AddMemoryNode(int newNode, int size);
bool MT_RemoveMemoryNode(int oldNode, uint32_t size);
void MT_RemoveHeadMemoryNode(int size);
void MT_Error(char const* funcName, int numBytes);
int MT_GetSize(int numBytes);


extern scrMemTreeGlob_t scrMemTreeGlob;

#ifdef KISAK_SCRIPT_STRING_PERF_TESTING
void MT_ResetCompleteValidationCountForTesting() noexcept;
[[nodiscard]] uint32_t MT_CompleteValidationCountForTesting() noexcept;
[[nodiscard]] uint32_t MT_CompleteForestValidationCountForTesting() noexcept;
void MT_CorruptAllocationMetadataForTesting(
    uint32_t nodeNum,
    uint8_t type,
    uint8_t size) noexcept;
void MT_CorruptFreeNodeMembershipForTesting(
    uint32_t nodeNum,
    uint8_t membership) noexcept;
#endif

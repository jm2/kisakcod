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

enum class MT_ValidationLeaseStatus : uint8_t
{
    Success,
    Busy,
    InvalidArgument,
    InvalidToken,
    UnsafeFailure,
};

namespace script_string
{
class OwnershipBatch;
}

// Capability proving that lease admission starts at the outer SCRIPT_STRING
// ownership boundary. Production callers cannot construct it, which preserves
// the SCRIPT_STRING -> MEMORY_TREE lock order when the retained allocator lock
// is introduced. OwnershipBatch is the sole production admission authority.
class MT_ValidationLeaseAdmission final
{
private:
    MT_ValidationLeaseAdmission() noexcept = default;
    friend class script_string::OwnershipBatch;

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
public:
    [[nodiscard]] static MT_ValidationLeaseAdmission ForTesting() noexcept
    {
        return {};
    }
#endif
};
RUNTIME_SIZE(MT_ValidationLeaseAdmission, 0x1, 0x1);

// Retains CRITSECT_MEMORY_TREE after complete Basic + Forest + Partition
// validation. Leased operations authenticate the same-thread token and use the
// bounded mirror-aware path validation shared with LegacyLocal operations.
// Finish performs complete validation again before releasing the retained
// acquisition. Destroying an admitted lease without Finish permanently
// freezes allocator access. An exactly authenticated destructor can release
// the retained acquisition after publishing that terminal boundary; a torn
// token leaves the unauthenticated acquisition held.
//
// Storage-lifetime contract: the lease object must remain alive until every
// API call that received its address/reference has returned. In particular,
// callers may not race normal Finish followed by destruction against Begin,
// a leased operation, a snapshot, or a test-only setter. Production admission
// is same-thread while the owning SCRIPT_STRING transaction remains held, so
// it satisfies that contract. Terminal Frozen state makes generic and already-
// blocked abandonment paths fail closed; it does not make arbitrary concurrent
// destruction of the caller-owned token storage valid.
class MT_ValidationLease final
{
public:
    MT_ValidationLease() noexcept = default;
    ~MT_ValidationLease() noexcept;

    MT_ValidationLease(const MT_ValidationLease &) = delete;
    MT_ValidationLease &operator=(const MT_ValidationLease &) = delete;
    MT_ValidationLease(MT_ValidationLease &&) = delete;
    MT_ValidationLease &operator=(MT_ValidationLease &&) = delete;

    // Snapshots authenticate under CRITSECT_MEMORY_TREE. A foreign caller
    // blocks until the owner finishes and then observes the cleared token.
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool poisoned() const noexcept;
    [[nodiscard]] uint64_t serial() const noexcept;
    [[nodiscard]] uint32_t mutationCount() const noexcept;

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
    void SetAuthenticationFieldsForTesting(
        uint64_t serial,
        uint8_t reserved0,
        uint8_t reserved1) noexcept;
    void SetMutationCountForTesting(uint32_t mutationCount) noexcept;
#endif

private:
    friend class script_string::OwnershipBatch;
    friend struct MT_ValidationLeaseAccess;
    friend MT_ValidationLeaseStatus MT_TryBeginValidationLease(
        MT_ValidationLease *lease,
        MT_ValidationLeaseAdmission admission) noexcept;
    friend MT_ValidationLeaseStatus MT_FinishValidationLease(
        MT_ValidationLease *lease) noexcept;
    friend MT_AllocIndexStatus MT_TryAllocIndexLeased(
        MT_ValidationLease &lease,
        int numBytes,
        int type,
        uint16_t *outIndex) noexcept;
    friend MT_AllocationInfoStatus MT_TryGetAllocationInfoLeased(
        MT_ValidationLease &lease,
        uint32_t nodeNum,
        MT_AllocationInfo *outInfo) noexcept;
    friend MT_FreeIndexStatus MT_TryFreeIndexLeased(
        MT_ValidationLease &lease,
        uint32_t nodeNum,
        int numBytes) noexcept;

    // OwnershipBatch calls this only after authenticating its live outer and
    // nested storage addresses. The helper independently authenticates the
    // allocator registry/TLS authority, publishes terminal Frozen state, and
    // releases the retained allocator acquisition only when exact.
    [[nodiscard]] static bool AbandonFromOwnershipBatch(
        MT_ValidationLease &lease) noexcept;

    uint64_t serial_ = 0;
    uint32_t mutationCount_ = 0;
    bool active_ = false;
    bool poisoned_ = false;
    uint8_t reserved_[2]{};
};
RUNTIME_SIZE(MT_ValidationLease, 0x10, 0x10);

[[nodiscard]] MT_ValidationLeaseStatus MT_TryBeginValidationLease(
    MT_ValidationLease *lease,
    MT_ValidationLeaseAdmission admission) noexcept;

// A correctly authenticated owner releases both recursive acquisitions even
// when close validation fails. InvalidToken cannot identify a retained
// acquisition safely and therefore leaves it held.
[[nodiscard]] MT_ValidationLeaseStatus MT_FinishValidationLease(
    MT_ValidationLease *lease) noexcept;

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
    uint8_t reservedFieldAlignment[2];
    int totalAlloc;                     // XREF: MT_DumpTree(void):loc_59E783/r
                                        // MT_DumpTree(void)+1FB/r ...
    int totalAllocBuckets;              // XREF: MT_DumpTree(void):loc_59E7AE/r
    // Keep the intentional 128-byte cache-line extent explicit. MSVC ARM64
    // otherwise diagnoses the implicit tail padding as C4324 under /WX.
    uint8_t reservedCacheLineAlignment[0x54];
};
static_assert(sizeof(scrMemTreeGlob_t) == 0xC0380);

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
[[nodiscard]] MT_FreeIndexStatus MT_TryFreeIndexLeased(
    MT_ValidationLease &lease,
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
[[nodiscard]] MT_AllocationInfoStatus MT_TryGetAllocationInfoLeased(
    MT_ValidationLease &lease,
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
[[nodiscard]] MT_AllocIndexStatus MT_TryAllocIndexLeased(
    MT_ValidationLease &lease,
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

#if defined(KISAK_MEMORY_TREE_VALIDATION_TESTING)
void MT_SetNextValidationLeaseSerialForTesting(uint64_t serial) noexcept;
void MT_SetValidationLeaseRegistryForTesting(
    MT_ValidationLease *lease,
    uint64_t serial) noexcept;
void MT_SetValidationLeaseRegistryMirrorsForTesting(
    uintptr_t address,
    uint64_t serial,
    uintptr_t addressMirror,
    uint64_t serialMirror) noexcept;
void MT_SetValidationLeaseLifecycleForTesting(
    uint8_t lifecycle,
    uint8_t lifecycleMirror) noexcept;
void MT_SetRetainedValidationLeaseAuthenticationForTesting(
    uintptr_t address,
    uint64_t serial,
    uintptr_t addressMirror,
    uint64_t serialMirror) noexcept;
// Test-only recovery for terminal destructor fixtures. Production has no
// corresponding escape hatch. The caller must state whether the fixture
// deliberately retained one authenticated owner-thread acquisition.
void MT_ResetAbandonedValidationLeaseForTesting(
    bool releaseRetainedAcquisition) noexcept;
#endif

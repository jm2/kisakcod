#pragma once

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

namespace physical_memory
{
class AllocationReceipt;
} // namespace physical_memory

namespace pmem_runtime
{
enum class InitializationPhase : std::uint8_t
{
    Uninitialized,
    Initializing,
    Ready,
    Poisoned,
};

enum class InitializationStatus : std::uint8_t
{
    Success,
    AlreadyInitialized,
    Busy,
    ReserveFailed,
    CommitFailed,
    ReleaseFailed,
    Poisoned,
    CorruptState,
};

// Report-free classification of caller-owned storage relative to the hidden
// physical-memory runtime. This preserves the distinction between coherent
// lifecycle states so callers never treat an unpublished or poisoned extent
// as proof that storage is safe to retain.
enum class StorageIsolationStatus : std::uint8_t
{
    Success,
    Uninitialized,
    Busy,
    Poisoned,
    InvalidArgument,
    ProtectedStorageOverlap,
    CorruptState,
};

enum class AllocationStatus : std::uint8_t
{
    Success,
    InvalidRequest,
    NotReady,
    ScopeInactive,
    CorruptState,
    Exhausted,
};

// Public lifecycle state for the opaque checked-allocation receipt. This is
// intentionally a phase value, not a view of the hidden PhysicalMemory owner.
enum class AllocationReceiptPhase : std::uint8_t
{
    Pristine,
    Begun,
    Ended,
    Freed,
};

enum class AllocationReceiptStatus : std::uint8_t
{
    Success,
    AlreadyComplete,
    NotReady,
    InvalidArgument,
    InvalidAllocationType,
    Busy,
    CapacityExceeded,
    ReceiptInUse,
    ReceiptMismatch,
    WrongPhase,
    CorruptState,
};

inline constexpr std::uint32_t DIAGNOSTIC_ENTRIES_PER_PRIM = 32u;
inline constexpr std::size_t DIAGNOSTIC_NAME_CAPACITY = 19u;

enum class DiagnosticEntryKind : std::uint8_t
{
    Unused,
    Allocation,
    Hole,
};

enum class DiagnosticSnapshotStatus : std::uint8_t
{
    NotReady,
    Success,
    CorruptState,
};

// Report-free result for one serialized allocation attempt. additionalBytes
// is exact on Exhausted and zero for every other status. The explicit reserved
// bytes are always zero; persist or compare this native-layout value field by
// field because native64 retains ordinary compiler tail padding.
struct AllocationResult final
{
    std::uint64_t additionalBytes = 0;
    std::uint8_t *address = nullptr;
    AllocationStatus status = AllocationStatus::InvalidRequest;
    std::uint8_t reserved[3]{};
};

RUNTIME_SIZE(AllocationResult, 0x10, 0x18);
RUNTIME_OFFSET(AllocationResult, additionalBytes, 0x0, 0x0);
RUNTIME_OFFSET(AllocationResult, address, 0x8, 0x8);
RUNTIME_OFFSET(AllocationResult, status, 0xC, 0x10);
RUNTIME_OFFSET(AllocationResult, reserved, 0xD, 0x11);

enum class ProcessInitAllocationStatus : std::uint8_t
{
    Success,
    AlreadyComplete,
    NotReady,
    Busy,
    WrongPhase,
    CorruptState,
};

struct DiagnosticEntry final
{
    std::uint32_t bytes = 0;
    char name[DIAGNOSTIC_NAME_CAPACITY]{};
    DiagnosticEntryKind kind = DiagnosticEntryKind::Unused;
};

RUNTIME_SIZE(DiagnosticEntry, 0x18, 0x18);
RUNTIME_OFFSET(DiagnosticEntry, bytes, 0x0, 0x0);
RUNTIME_OFFSET(DiagnosticEntry, name, 0x4, 0x4);
RUNTIME_OFFSET(DiagnosticEntry, kind, 0x17, 0x17);

struct DiagnosticSnapshot final
{
    DiagnosticEntry high[DIAGNOSTIC_ENTRIES_PER_PRIM]{};
    DiagnosticEntry low[DIAGNOSTIC_ENTRIES_PER_PRIM]{};
    std::uint32_t highCount = 0;
    std::uint32_t lowCount = 0;
    std::uint32_t freeBytes = 0;
    DiagnosticSnapshotStatus status = DiagnosticSnapshotStatus::NotReady;
    std::uint8_t reserved[3]{};
};

RUNTIME_SIZE(DiagnosticSnapshot, 0x610, 0x610);
RUNTIME_OFFSET(DiagnosticSnapshot, high, 0x0, 0x0);
RUNTIME_OFFSET(DiagnosticSnapshot, low, 0x300, 0x300);
RUNTIME_OFFSET(DiagnosticSnapshot, highCount, 0x600, 0x600);
RUNTIME_OFFSET(DiagnosticSnapshot, lowCount, 0x604, 0x604);
RUNTIME_OFFSET(DiagnosticSnapshot, freeBytes, 0x608, 0x608);
RUNTIME_OFFSET(DiagnosticSnapshot, status, 0x60C, 0x60C);
RUNTIME_OFFSET(DiagnosticSnapshot, reserved, 0x60D, 0x60D);

[[nodiscard]] InitializationStatus KISAK_CDECL TryInitialize() noexcept;

[[nodiscard]] AllocationResult KISAK_CDECL TryAllocate(
    std::uint32_t size,
    std::uint32_t alignment,
    std::uint32_t type,
    std::uint32_t allocType) noexcept;

[[nodiscard]] DiagnosticSnapshot KISAK_CDECL
TryCaptureDiagnosticSnapshot() noexcept;

// Authenticates the complete current runtime phase, then classifies one
// nonempty caller-supplied range. Success means the coherent Ready runtime's
// retained extent and every fixed hidden control object are disjoint from the
// range. Initializing cannot authenticate the unpublished candidate extent;
// Poisoned authenticates its retained extent but is never usable as Success.
[[nodiscard]] StorageIsolationStatus KISAK_CDECL
TryClassifyStorageIsolation(
    const void *storage,
    std::size_t size) noexcept;

// Returns true only while the runtime is coherently Ready and the complete
// caller-supplied range is disjoint from retained managed memory and hidden
// physical-memory control storage. Null, empty, and overflowing ranges fail.
[[nodiscard]] bool KISAK_CDECL StorageIsOutsideManagedMemory(
    const void *storage,
    std::size_t size) noexcept;

// These report-free operations serialize access to the hidden global
// PhysicalMemory owner and capture name text into its owned-name sidecar.
// Receipt storage remains caller-owned and must stay at one stable address.
// Every bound-phase authenticator also requires the caller's exact retained
// allocation type; a different valid prim is a receipt mismatch. A pristine
// receipt has not selected a prim, but still requires a valid type value. A
// Freed receipt remains authenticatable when a later allocation reuses its
// former structural allocation-list index.
[[nodiscard]] AllocationReceiptStatus KISAK_CDECL
TryBeginAllocationReceipt(
    const char *name,
    std::uint32_t allocType,
    physical_memory::AllocationReceipt *receipt) noexcept;

[[nodiscard]] AllocationReceiptStatus KISAK_CDECL
TryEndAllocationReceipt(
    physical_memory::AllocationReceipt *receipt) noexcept;

[[nodiscard]] AllocationReceiptStatus KISAK_CDECL
TryFreeAllocationReceipt(
    physical_memory::AllocationReceipt *receipt) noexcept;

[[nodiscard]] AllocationReceiptStatus KISAK_CDECL
TryAuthenticateAllocationReceipt(
    const physical_memory::AllocationReceipt *receipt,
    std::uint32_t expectedAllocationType,
    AllocationReceiptPhase expectedPhase) noexcept;

// Authenticates that the complete nonempty range belongs to this exact live
// receipt and exact allocation type. Begun and Ended are the only
// range-bearing phases; no backing-range or PhysicalMemory pointer is exposed.
[[nodiscard]] AllocationReceiptStatus KISAK_CDECL
TryAuthenticateAllocationRange(
    const physical_memory::AllocationReceipt *receipt,
    std::uint32_t expectedAllocationType,
    const void *storage,
    std::size_t size,
    AllocationReceiptPhase expectedPhase) noexcept;

// These report-free operations own the one process-lifetime high-prim `$init`
// allocation. The controller is hidden with the serialized PMem state so no
// caller can copy, free, reset, or mint a second authority for index zero.
// Production enrollment is intentionally deferred to the atomic loader
// cutover that replaces every legacy PMem lifecycle site together.
[[nodiscard]] ProcessInitAllocationStatus KISAK_CDECL
TryBeginProcessInitAllocation() noexcept;

[[nodiscard]] ProcessInitAllocationStatus KISAK_CDECL
TryEndProcessInitAllocation() noexcept;
} // namespace pmem_runtime

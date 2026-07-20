#pragma once

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

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

enum class AllocationStatus : std::uint8_t
{
    Success,
    InvalidRequest,
    NotReady,
    ScopeInactive,
    CorruptState,
    Exhausted,
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

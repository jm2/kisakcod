#pragma once

#include <universal/kisak_abi.h>

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

[[nodiscard]] InitializationStatus KISAK_CDECL TryInitialize() noexcept;

[[nodiscard]] AllocationResult KISAK_CDECL TryAllocate(
    std::uint32_t size,
    std::uint32_t alignment,
    std::uint32_t type,
    std::uint32_t allocType) noexcept;
} // namespace pmem_runtime

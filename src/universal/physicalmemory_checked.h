#pragma once

#include "platform_compat.h"
#include "physicalmemory.h"

#include <cstdint>

namespace physical_memory
{
// These operations are a report-free lifecycle boundary for one named
// PhysicalMemory allocation scope. They do not allocate, invoke callbacks,
// assert, report, or call the legacy PMem Begin/End/Free entry points.
//
// The caller must externally serialize every access to both prims of the
// PhysicalMemory object and to the receipt. The PhysicalMemory object, its
// backing buffer, the stableName pointer identity, and the receipt's stable
// address must remain valid from a successful TryBegin through TryFree. The
// name is an identity token only and is never dereferenced by this API.
enum class AllocationScopeStatus : std::uint8_t
{
    Success,
    InvalidArgument,
    InvalidAllocationType,
    MalformedState,
    Busy,
    CapacityExceeded,
    ReceiptInUse,
    ReceiptMismatch,
    WrongPhase,
    AlreadyComplete,
};

// One receipt authenticates exactly one successful allocation-scope begin.
// It is intentionally noncopyable, nonmovable, and single-use. Keep it at the
// same address until TryFree succeeds; there is no public reset/rebind path.
class AllocationReceipt final
{
public:
    AllocationReceipt() noexcept;
    ~AllocationReceipt() noexcept = default;

    AllocationReceipt(const AllocationReceipt &) = delete;
    AllocationReceipt &operator=(const AllocationReceipt &) = delete;
    AllocationReceipt(AllocationReceipt &&) = delete;
    AllocationReceipt &operator=(AllocationReceipt &&) = delete;

private:
    enum class Phase : std::uint8_t
    {
        Empty,
        Begun,
        Ended,
        Freed,
    };

    friend AllocationScopeStatus TryBegin(
        PhysicalMemory *memory,
        std::uint32_t allocType,
        const char *stableName,
        AllocationReceipt *receipt) noexcept;
    friend AllocationScopeStatus TryEnd(
        AllocationReceipt *receipt) noexcept;
    friend AllocationScopeStatus TryFree(
        AllocationReceipt *receipt) noexcept;

    [[nodiscard]] bool reservedIsZero() const noexcept;
    [[nodiscard]] bool hasValidPhase() const noexcept;
    [[nodiscard]] bool isPristine() const noexcept;
    [[nodiscard]] bool isBound() const noexcept;
    [[nodiscard]] bool matchesEntry(
        const PhysicalMemoryPrim &prim) const noexcept;

    const AllocationReceipt *self_;
    PhysicalMemory *owner_;
    PhysicalMemoryPrim *prim_;
    const char *name_;
    std::uint32_t allocType_;
    std::uint32_t index_;
    std::uint32_t startPos_;
    Phase phase_;
    std::uint8_t reserved_[3];
};

[[nodiscard]] AllocationScopeStatus TryBegin(
    PhysicalMemory *memory,
    std::uint32_t allocType,
    const char *stableName,
    AllocationReceipt *receipt) noexcept;

// A successful end is idempotent. Retrying after End or Free returns
// AlreadyComplete and performs no owner mutation.
[[nodiscard]] AllocationScopeStatus TryEnd(
    AllocationReceipt *receipt) noexcept;

// Free is valid only after End. A successful free is idempotent; retrying it
// returns AlreadyComplete. Any active scope on the selected prim returns Busy.
[[nodiscard]] AllocationScopeStatus TryFree(
    AllocationReceipt *receipt) noexcept;
} // namespace physical_memory

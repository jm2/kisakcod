#pragma once

#include "platform_compat.h"
#include "physicalmemory.h"

#include <cstddef>
#include <cstdint>

namespace physical_memory
{
class AllocationReceipt;
} // namespace physical_memory

namespace pmem_runtime
{
enum class AllocationReceiptPhase : std::uint8_t;

namespace detail
{
// Const-only exact-key predicate for the serialized hidden-owner bridge. The
// caller must hold the physical-memory lock and supply the authoritative
// owner, allocation slot, owned stable-name address, and expected phase.
[[nodiscard]] bool AuthenticateAllocationReceiptNoLock(
    const physical_memory::AllocationReceipt &receipt,
    const PhysicalMemory &owner,
    std::uint32_t allocType,
    std::uint32_t index,
    const char *stableName,
    AllocationReceiptPhase expectedPhase) noexcept;

// Exact receipt authentication plus a bounds-only query over that live
// allocation's own byte interval. No owner or interval pointer is returned.
[[nodiscard]] bool AuthenticateAllocationRangeNoLock(
    const physical_memory::AllocationReceipt &receipt,
    const PhysicalMemory &owner,
    std::uint32_t allocType,
    std::uint32_t index,
    const char *stableName,
    const void *storage,
    std::size_t size,
    AllocationReceiptPhase expectedPhase) noexcept;
} // namespace detail
} // namespace pmem_runtime

namespace db::zone_runtime::detail
{
[[nodiscard]] bool IsPristineRuntimeReceipt(
    const physical_memory::AllocationReceipt &receipt) noexcept;
} // namespace db::zone_runtime::detail

namespace physical_memory
{
// These operations are a report-free lifecycle boundary for one named
// PhysicalMemory allocation scope. They do not allocate, invoke callbacks,
// assert, report, or call the legacy PMem Begin/End/Free entry points.
//
// The caller must externally serialize every access to both prims of the
// PhysicalMemory object and to the receipt. The PhysicalMemory object, its
// backing buffer, the stableName pointer identity, and the receipt's stable
// address must remain valid from a successful TryBegin through every terminal
// retry. The name is an identity token only and is never dereferenced by this
// API.
//
// A checked scope has exclusive lifecycle ownership of its exact allocation
// entry until TryFree succeeds. During that interval callers may use the
// normal allocator to advance the selected prim's position, and may create
// other checked scopes after this one has ended, but must not call legacy
// lifecycle/init helpers or directly replace, remove, or reinitialize checked
// entries. A Freed receipt uses an exact receipt-local canonical encoding.
// It remains authenticatable after its former allocation-list index is reused.
// Without adding an incarnation nonce to the legacy PhysicalMemory ABI,
// bypass/reinitialization could still recreate the same structural tuple.
// A durable generation slot may destroy and reconstruct this single-use
// receipt only after authenticating that exact generation's Freed terminal
// state; it must never reset or recycle receipt bytes while ownership is live.
//
// PhysicalMemory control storage and AllocationReceipt storage must be
// mutually disjoint. Both objects must remain wholly outside the entire
// managed backing range originally supplied during physical-memory
// initialization. High-prim topology can suggest a historical upper bound,
// but PhysicalMemory does not retain an independently authenticated
// initialization extent. This API therefore cannot authenticate or validate
// the separation; the caller that owns the authoritative initialization
// extent must enforce it. The stableName identity must also remain outside any
// reclaimable backing range unless the caller independently guarantees that
// its storage cannot be overwritten or reused through TryFree and every
// terminal retry.
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
    // User-provided so the authority token is not trivially byte-copyable.
    // Destruction deliberately performs no implicit lifecycle work.
    ~AllocationReceipt() noexcept;

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
    // Exact const-only friendship for passive runtime-table authentication.
    friend bool db::zone_runtime::detail::IsPristineRuntimeReceipt(
        const AllocationReceipt &receipt) noexcept;
    friend bool pmem_runtime::detail::AuthenticateAllocationReceiptNoLock(
        const AllocationReceipt &receipt,
        const PhysicalMemory &owner,
        std::uint32_t allocType,
        std::uint32_t index,
        const char *stableName,
        pmem_runtime::AllocationReceiptPhase expectedPhase) noexcept;

    [[nodiscard]] bool reservedIsZero() const noexcept;
    [[nodiscard]] bool hasValidPhase() const noexcept;
    [[nodiscard]] bool hasValidPhaseWitness() const noexcept;
    [[nodiscard]] bool hasCanonicalTerminalState() const noexcept;
    void setCanonicalTerminalState() noexcept;
    [[nodiscard]] bool isPristine() const noexcept;
    [[nodiscard]] bool isBound() const noexcept;
    [[nodiscard]] bool isCanonical() const noexcept;
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
    std::uint8_t phaseWitness_;
    std::uint8_t reserved_[2];
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

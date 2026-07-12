#pragma once

// Bounded reservations for renderer-owned fixed arrays.  The backing counters
// remain exact-width words so frozen engine layouts do not acquire the
// platform-dependent size and alignment of C long or std::atomic.

#include <cstdint>

#include <universal/sys_atomic.h>

namespace gfx::reservation_atomic
{
inline constexpr std::uint32_t kInvalidOffset = UINT32_MAX;

// Claims [offset, offset + count) without ever publishing a counter beyond
// capacity.  This grants ownership of the range; it does not publish payload
// bytes to a consumer, which must still observe the renderer's worker-join
// boundary.  Failure leaves both the counter and output unchanged.  Subtraction
// is used for the capacity check so attacker-controlled/corrupt values cannot
// wrap the range back into bounds.
inline bool TryReserve(
    volatile std::uint32_t *const counter,
    const std::uint32_t count,
    const std::uint32_t capacity,
    std::uint32_t *const offset) noexcept
{
    if (!counter || !offset || count == 0u)
        return false;

    std::uint32_t observed = Sys_AtomicLoad(counter);
    for (;;)
    {
        if (observed > capacity || count > capacity - observed)
            return false;

        const std::uint32_t desired = observed + count;
        const std::uint32_t previous = Sys_AtomicCompareExchange(
            counter,
            desired,
            observed);
        if (previous == observed)
        {
            *offset = observed;
            return true;
        }
        observed = previous;
    }
}

inline bool TryReserveIndex(
    volatile std::uint32_t *const counter,
    const std::uint32_t capacity,
    std::uint32_t *const index) noexcept
{
    return TryReserve(counter, 1u, capacity, index);
}
} // namespace gfx::reservation_atomic

#pragma once

// Fixed-width queue state transitions shared by renderer worker producers and
// consumers.  A short producer guard covers capacity claim, payload copy, tail
// advance, and availability publication.  A short consumer guard covers ready
// claim, copy to aligned private storage, and head advance; handlers execute
// outside both guards, and pending capacity is released only after completion.

#include <cstdint>

#include <universal/sys_atomic.h>

namespace gfx::worker_queue_atomic
{
inline bool TryAddBounded(
    volatile std::uint32_t *const counter,
    const std::uint32_t amount,
    const std::uint32_t capacity) noexcept
{
    if (!counter || amount == 0u || capacity == 0u)
        return false;

    std::uint32_t observed = Sys_AtomicLoad(counter);
    for (;;)
    {
        if (observed > capacity || amount > capacity - observed)
            return false;
        const std::uint32_t desired = observed + amount;
        const std::uint32_t previous = Sys_AtomicCompareExchange(
            counter,
            desired,
            observed);
        if (previous == observed)
            return true;
        observed = previous;
    }
}

inline bool TrySubtractBounded(
    volatile std::uint32_t *const counter,
    const std::uint32_t amount,
    const std::uint32_t capacity) noexcept
{
    if (!counter || amount == 0u || capacity == 0u)
        return false;

    std::uint32_t observed = Sys_AtomicLoad(counter);
    for (;;)
    {
        if (observed > capacity || amount > observed)
            return false;
        const std::uint32_t desired = observed - amount;
        const std::uint32_t previous = Sys_AtomicCompareExchange(
            counter,
            desired,
            observed);
        if (previous == observed)
            return true;
        observed = previous;
    }
}

inline bool TryClaimUpTo(
    volatile std::uint32_t *const available,
    const std::uint32_t maximum,
    const std::uint32_t capacity,
    std::uint32_t *const claimed) noexcept
{
    if (!available || !claimed || maximum == 0u || capacity == 0u)
        return false;

    std::uint32_t observed = Sys_AtomicLoad(available);
    for (;;)
    {
        if (observed == 0u || observed > capacity)
            return false;
        const std::uint32_t amount = observed < maximum
            ? observed
            : maximum;
        const std::uint32_t desired = observed - amount;
        const std::uint32_t previous = Sys_AtomicCompareExchange(
            available,
            desired,
            observed);
        if (previous == observed)
        {
            *claimed = amount;
            return true;
        }
        observed = previous;
    }
}

// Atomically grants a wrapped element range [first, first + count).  The
// returned range may cross the end of the backing array and must be copied in
// two pieces by the caller.  Failure leaves both cursor and output unchanged.
inline bool TryAdvanceCursor(
    volatile std::uint32_t *const cursor,
    const std::uint32_t count,
    const std::uint32_t capacity,
    std::uint32_t *const first) noexcept
{
    if (!cursor || !first || count == 0u || capacity == 0u
        || count > capacity)
    {
        return false;
    }

    std::uint32_t observed = Sys_AtomicLoad(cursor);
    for (;;)
    {
        if (observed >= capacity)
            return false;
        const std::uint32_t remaining = capacity - observed;
        const std::uint32_t desired = count < remaining
            ? observed + count
            : count - remaining;
        const std::uint32_t previous = Sys_AtomicCompareExchange(
            cursor,
            desired,
            observed);
        if (previous == observed)
        {
            *first = observed;
            return true;
        }
        observed = previous;
    }
}

inline bool TryAcquireGuard(volatile std::uint32_t *const guard) noexcept
{
    return guard
        && Sys_AtomicCompareExchange(guard, 1u, 0u) == 0u;
}

inline bool ReleaseGuard(volatile std::uint32_t *const guard) noexcept
{
    return guard
        && Sys_AtomicCompareExchange(guard, 0u, 1u) == 1u;
}
} // namespace gfx::worker_queue_atomic

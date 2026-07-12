#pragma once

// Portable allocation and epoch protocols for the client/server skeleton
// arenas.  Engine structures retain exact-width plain words so their frozen
// layouts do not acquire std::atomic implementation-specific size or alignment.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <universal/sys_atomic.h>

namespace skel_memory_atomic
{
inline constexpr std::uint32_t kInvalidOffset =
    (std::numeric_limits<std::uint32_t>::max)();

struct ArenaView
{
    char *base;
    std::uint32_t capacity;
};

// Reset publication still requires skeleton workers to be externally
// quiescent, but resetters themselves are serialized here.  That prevents a
// second reset from clearing the cursor or DObj skeletons after the first one
// has already published its new epoch.
class ResetGuard
{
public:
    explicit ResetGuard(volatile std::uint32_t *const gate) noexcept
        : gate_(gate), owns_(false)
    {
        if (!gate_)
            return;

        while (Sys_AtomicCompareExchange(gate_, 1u, 0u) != 0u)
        {
        }
        owns_ = true;
    }

    ~ResetGuard()
    {
        if (owns_)
            Sys_AtomicStore(gate_, 0u);
    }

    ResetGuard(const ResetGuard &) = delete;
    ResetGuard &operator=(const ResetGuard &) = delete;

private:
    volatile std::uint32_t *gate_;
    bool owns_;
};

inline constexpr bool IsPowerOfTwo(const std::uint32_t value) noexcept
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

inline bool CheckedAlignUp(
    const std::uint32_t value,
    const std::uint32_t alignment,
    std::uint32_t *const alignedValue) noexcept
{
    if (!alignedValue || value == 0u || !IsPowerOfTwo(alignment))
        return false;

    const std::uint32_t mask = alignment - 1u;
    if (value > kInvalidOffset - mask)
        return false;

    *alignedValue = (value + mask) & ~mask;
    return true;
}

inline ArenaView MakeArenaView(
    char *const storage,
    const std::size_t storageBytes,
    const std::uint32_t alignment) noexcept
{
    if (!storage || storageBytes > kInvalidOffset
        || !IsPowerOfTwo(alignment))
    {
        return {nullptr, 0u};
    }

    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(storage);
    const std::uint32_t mask = alignment - 1u;
    const std::uint32_t misalignment =
        static_cast<std::uint32_t>(address & mask);
    const std::uint32_t padding = (alignment - misalignment) & mask;
    if (padding > storageBytes)
        return {nullptr, 0u};

    return {
        storage + padding,
        static_cast<std::uint32_t>(storageBytes) - padding,
    };
}

// Returns an arena-relative offset.  A failed size check or exhausted arena
// returns kInvalidOffset without modifying cursor.  Callers form a pointer only
// after a successful reservation, so neither overflow nor an invalid cursor can
// participate in pointer arithmetic.
inline std::uint32_t ReserveAligned(
    volatile std::uint32_t *const cursor,
    const std::uint32_t requestedBytes,
    const std::uint32_t capacity,
    const std::uint32_t alignment) noexcept
{
    std::uint32_t reservedBytes = 0u;
    if (!cursor
        || !CheckedAlignUp(requestedBytes, alignment, &reservedBytes)
        || reservedBytes > capacity)
    {
        return kInvalidOffset;
    }

    std::uint32_t observed = Sys_AtomicLoad(cursor);
    for (;;)
    {
        if ((observed & (alignment - 1u)) != 0u
            || observed > capacity
            || reservedBytes > capacity - observed)
        {
            return kInvalidOffset;
        }

        const std::uint32_t desired = observed + reservedBytes;
        const std::uint32_t previous = Sys_AtomicCompareExchange(
            cursor,
            desired,
            observed);
        if (previous == observed)
            return observed;
        observed = previous;
    }
}

inline std::uint32_t LoadEpoch(
    const volatile std::uint32_t *const epoch) noexcept
{
    return Sys_AtomicLoad(epoch);
}

// Advances the 32-bit engine timestamp as an unsigned bit pattern, retaining
// the original two's-complement rollover behavior without signed overflow.
// Zero remains reserved as the uninitialized timestamp.  The optional callback
// runs before any successful max-to-one publication.  Keeping it in the CAS
// retry loop guarantees that even contending resetters cannot reuse epoch one
// without first invalidating state that may still carry that old timestamp.
// A caller that supplies onWrap must hold its arena's ResetGuard across the
// complete base/cursor/epoch publication scope.
inline std::uint32_t AdvanceEpoch(
    volatile std::uint32_t *const epoch,
    void (*const onWrap)() = nullptr) noexcept
{
    std::uint32_t observed = Sys_AtomicLoad(epoch);
    for (;;)
    {
        std::uint32_t desired = observed + 1u;
        if (desired == 0u)
        {
            if (onWrap)
                onWrap();
            desired = 1u;
        }

        const std::uint32_t previous = Sys_AtomicCompareExchange(
            epoch,
            desired,
            observed);
        if (previous == observed)
            return desired;
        observed = previous;
    }
}

inline std::int32_t TimestampFromEpoch(const std::uint32_t epoch) noexcept
{
    return std::bit_cast<std::int32_t>(epoch);
}

inline std::int32_t LoadTimestamp(
    const volatile std::uint32_t *const epoch) noexcept
{
    return TimestampFromEpoch(LoadEpoch(epoch));
}

// Claims the diagnostic once for a timestamp.  The reset contract guarantees
// that workers from an old epoch are quiescent before a new epoch is published.
inline bool ClaimWarning(
    volatile std::uint32_t *const warnedEpoch,
    const std::int32_t timestamp) noexcept
{
    const std::uint32_t epoch = std::bit_cast<std::uint32_t>(timestamp);
    std::uint32_t observed = Sys_AtomicLoad(warnedEpoch);
    for (;;)
    {
        if (observed == epoch)
            return false;

        const std::uint32_t previous = Sys_AtomicCompareExchange(
            warnedEpoch,
            epoch,
            observed);
        if (previous == observed)
            return true;
        observed = previous;
    }
}
} // namespace skel_memory_atomic

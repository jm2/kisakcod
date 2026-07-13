#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <thread>

#include <universal/sys_atomic.h>

// Effects use one signed word as a reader/exclusive-writer gate.  A value of
// -1 owns the exclusive gate, while nonnegative values count cooperative
// iterators.  Keep every transition atomic: mixing raw volatile polling and
// stores with interlocked operations is a data race on non-x86 targets.
inline void FxIteratorBeginCooperative(volatile std::int32_t *const state) noexcept
{
    for (;;)
    {
        const std::int32_t observed = Sys_AtomicLoad(state);
        if (observed < 0 || observed == (std::numeric_limits<std::int32_t>::max)())
        {
            std::this_thread::yield();
            continue;
        }

        if (Sys_AtomicCompareExchange(state, observed + 1, observed) == observed)
            return;
        std::this_thread::yield();
    }
}

inline bool FxIteratorEndCooperative(
    volatile std::int32_t *const state,
    std::int32_t *const remaining) noexcept
{
    if (!remaining)
        return false;

    std::int32_t observed = Sys_AtomicLoad(state);
    while (observed > 0)
    {
        const std::int32_t desired = observed - 1;
        const std::int32_t previous = Sys_AtomicCompareExchange(state, desired, observed);
        if (previous == observed)
        {
            *remaining = desired;
            return true;
        }
        observed = previous;
    }

    return false;
}

inline bool FxIteratorTryBeginExclusive(volatile std::int32_t *const state) noexcept
{
    return Sys_AtomicCompareExchange(state, -1, 0) == 0;
}

inline void FxIteratorWaitBeginExclusive(volatile std::int32_t *const state) noexcept
{
    while (!FxIteratorTryBeginExclusive(state))
    {
        while (Sys_AtomicLoad(state) != 0)
        {
            std::this_thread::yield();
        }
        std::this_thread::yield();
    }
}

inline bool FxIteratorEndExclusive(volatile std::int32_t *const state) noexcept
{
    return Sys_AtomicCompareExchange(state, 0, -1) == -1;
}

static_assert(std::atomic_ref<bool>::is_always_lock_free);
static_assert(std::atomic_ref<bool>::required_alignment <= alignof(bool));

inline bool FxGarbageCollectionRequested(bool *const requested) noexcept
{
    return std::atomic_ref<bool>(*requested).load(std::memory_order_seq_cst);
}

inline void FxRequestGarbageCollection(bool *const requested) noexcept
{
    std::atomic_ref<bool>(*requested).store(true, std::memory_order_seq_cst);
}

inline void FxClearGarbageCollectionRequest(bool *const requested) noexcept
{
    std::atomic_ref<bool>(*requested).store(false, std::memory_order_seq_cst);
}

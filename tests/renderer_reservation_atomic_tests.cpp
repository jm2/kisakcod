#include <gfx_d3d/r_reservation_atomic.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
using gfx::reservation_atomic::TryReserve;
using gfx::reservation_atomic::TryReserveIndex;

int Fail(const char *const message)
{
    std::fprintf(stderr, "renderer reservation test failed: %s\n", message);
    return 1;
}

bool TestExactCapacityAndFailureStability()
{
    volatile std::uint32_t counter = 0u;
    std::uint32_t offset = 99u;

    if (!TryReserve(&counter, 3u, 8u, &offset) || offset != 0u)
        return false;
    if (!TryReserve(&counter, 5u, 8u, &offset) || offset != 3u)
        return false;

    offset = 77u;
    if (TryReserve(&counter, 1u, 8u, &offset)
        || Sys_AtomicLoad(&counter) != 8u
        || offset != 77u)
    {
        return false;
    }

    return true;
}

bool TestRejectedInputsAndCorruptCounters()
{
    volatile std::uint32_t counter = 2u;
    std::uint32_t offset = 55u;

    if (TryReserve(nullptr, 1u, 4u, &offset)
        || TryReserve(&counter, 1u, 4u, nullptr)
        || TryReserve(&counter, 0u, 4u, &offset)
        || Sys_AtomicLoad(&counter) != 2u
        || offset != 55u)
    {
        return false;
    }

    Sys_AtomicStore(&counter, 5u);
    if (TryReserve(&counter, 1u, 4u, &offset)
        || Sys_AtomicLoad(&counter) != 5u
        || offset != 55u)
    {
        return false;
    }

    return true;
}

bool TestNoArithmeticWrap()
{
    constexpr std::uint32_t kMax = UINT32_MAX;
    volatile std::uint32_t counter = kMax - 3u;
    std::uint32_t offset = 0u;

    if (!TryReserve(&counter, 3u, kMax, &offset)
        || offset != kMax - 3u
        || Sys_AtomicLoad(&counter) != kMax)
    {
        return false;
    }

    offset = 41u;
    if (TryReserve(&counter, 1u, kMax, &offset)
        || Sys_AtomicLoad(&counter) != kMax
        || offset != 41u)
    {
        return false;
    }

    Sys_AtomicStore(&counter, 1u);
    return !TryReserve(&counter, kMax, kMax, &offset)
        && Sys_AtomicLoad(&counter) == 1u
        && offset == 41u;
}

bool TestContendedIndicesAreUniqueAndBounded()
{
    constexpr std::uint32_t kCapacity = 1024u;
    constexpr std::uint32_t kThreadCount = 8u;
    constexpr std::uint32_t kAttemptsPerThread = 256u;

    volatile std::uint32_t counter = 0u;
    std::array<std::atomic<std::uint32_t>, kCapacity> claims{};
    std::atomic<std::uint32_t> successes{0u};
    std::atomic<bool> invalidOffset{false};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (std::uint32_t threadIndex = 0u;
         threadIndex < kThreadCount;
         ++threadIndex)
    {
        workers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (std::uint32_t attempt = 0u;
                 attempt < kAttemptsPerThread;
                 ++attempt)
            {
                std::uint32_t index = UINT32_MAX;
                if (!TryReserveIndex(&counter, kCapacity, &index))
                    continue;
                if (index >= kCapacity)
                {
                    invalidOffset.store(true, std::memory_order_relaxed);
                    continue;
                }
                claims[index].fetch_add(1u, std::memory_order_relaxed);
                successes.fetch_add(1u, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread &worker : workers)
        worker.join();

    if (invalidOffset.load(std::memory_order_relaxed)
        || successes.load(std::memory_order_relaxed) != kCapacity
        || Sys_AtomicLoad(&counter) != kCapacity)
    {
        return false;
    }

    for (const std::atomic<std::uint32_t> &claim : claims)
    {
        if (claim.load(std::memory_order_relaxed) != 1u)
            return false;
    }
    return true;
}

bool TestContendedRangesDoNotOverlap()
{
    constexpr std::uint32_t kRangeSize = 2u;
    constexpr std::uint32_t kRangeCount = 1024u;
    constexpr std::uint32_t kCapacity = kRangeSize * kRangeCount;
    constexpr std::uint32_t kThreadCount = 8u;

    volatile std::uint32_t counter = 0u;
    std::array<std::atomic<std::uint32_t>, kCapacity> claims{};
    std::atomic<bool> valid{true};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (std::uint32_t threadIndex = 0u;
         threadIndex < kThreadCount;
         ++threadIndex)
    {
        workers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (;;)
            {
                std::uint32_t offset = UINT32_MAX;
                if (!TryReserve(
                        &counter,
                        kRangeSize,
                        kCapacity,
                        &offset))
                {
                    return;
                }
                if ((offset % kRangeSize) != 0u
                    || offset > kCapacity - kRangeSize)
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }
                for (std::uint32_t index = 0u;
                     index < kRangeSize;
                     ++index)
                {
                    if (claims[offset + index].fetch_add(
                            1u,
                            std::memory_order_relaxed) != 0u)
                    {
                        valid.store(false, std::memory_order_relaxed);
                    }
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread &worker : workers)
        worker.join();
    if (!valid.load(std::memory_order_relaxed)
        || Sys_AtomicLoad(&counter) != kCapacity)
    {
        return false;
    }
    for (const std::atomic<std::uint32_t> &claim : claims)
    {
        if (claim.load(std::memory_order_relaxed) != 1u)
            return false;
    }
    return true;
}

bool TestRendererCapacityProfilesAndOldIndexContract()
{
    // gfxEnt index zero is the renderer's implicit/default entity.  A claim
    // therefore starts at one and must return the old counter value exactly.
    volatile std::uint32_t entityCount = 1u;
    std::uint32_t index = UINT32_MAX;
    for (std::uint32_t expected = 1u; expected < 128u; ++expected)
    {
        if (!TryReserveIndex(&entityCount, 128u, &index)
            || index != expected)
        {
            return false;
        }
    }
    index = 73u;
    if (TryReserveIndex(&entityCount, 128u, &index)
        || Sys_AtomicLoad(&entityCount) != 128u
        || index != 73u)
    {
        return false;
    }

    // Primitive draw-surface writers own fixed 512-dword chunks.  The last
    // exact-fit range succeeds and subsequent failures cannot overshoot 64K.
    volatile std::uint32_t primDrawSurfPos = 65536u - 512u;
    std::uint32_t offset = UINT32_MAX;
    if (!TryReserve(&primDrawSurfPos, 512u, 65536u, &offset)
        || offset != 65536u - 512u
        || Sys_AtomicLoad(&primDrawSurfPos) != 65536u)
    {
        return false;
    }
    offset = 91u;
    return !TryReserve(&primDrawSurfPos, 512u, 65536u, &offset)
        && Sys_AtomicLoad(&primDrawSurfPos) == 65536u
        && offset == 91u;
}
} // namespace

int main()
{
    if (!TestExactCapacityAndFailureStability())
        return Fail("exact capacity and failure stability");
    if (!TestRejectedInputsAndCorruptCounters())
        return Fail("rejected inputs and corrupt counters");
    if (!TestNoArithmeticWrap())
        return Fail("overflow-safe capacity arithmetic");
    if (!TestContendedIndicesAreUniqueAndBounded())
        return Fail("contended indices are unique and bounded");
    if (!TestContendedRangesDoNotOverlap())
        return Fail("contended multi-element ranges do not overlap");
    if (!TestRendererCapacityProfilesAndOldIndexContract())
        return Fail("renderer capacities and old-index return contract");
    return 0;
}

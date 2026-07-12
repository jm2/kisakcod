#include <gfx_d3d/r_worker_queue_atomic.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
namespace queue_atomic = gfx::worker_queue_atomic;

int Fail(const char *const message)
{
    std::fprintf(stderr, "worker queue test failed: %s\n", message);
    return 1;
}

bool Pending(const volatile std::uint32_t *const outstanding)
{
    return Sys_AtomicLoad(outstanding) != 0u;
}

bool TestBoundedCounts()
{
    volatile std::uint32_t count = 0u;
    if (!queue_atomic::TryAddBounded(&count, 3u, 5u)
        || !queue_atomic::TryAddBounded(&count, 2u, 5u)
        || Sys_AtomicLoad(&count) != 5u
        || queue_atomic::TryAddBounded(&count, 1u, 5u)
        || Sys_AtomicLoad(&count) != 5u)
    {
        return false;
    }

    if (!queue_atomic::TrySubtractBounded(&count, 5u, 5u)
        || Sys_AtomicLoad(&count) != 0u
        || queue_atomic::TrySubtractBounded(&count, 1u, 5u))
    {
        return false;
    }

    Sys_AtomicStore(&count, 6u);
    return !queue_atomic::TryAddBounded(&count, 1u, 5u)
        && !queue_atomic::TrySubtractBounded(&count, 1u, 5u)
        && Sys_AtomicLoad(&count) == 6u;
}

bool TestClaimsAndFailureStability()
{
    volatile std::uint32_t available = 11u;
    std::uint32_t claimed = 77u;
    if (!queue_atomic::TryClaimUpTo(
            &available,
            10u,
            17u,
            &claimed)
        || claimed != 10u
        || Sys_AtomicLoad(&available) != 1u)
    {
        return false;
    }
    if (!queue_atomic::TryClaimUpTo(
            &available,
            10u,
            17u,
            &claimed)
        || claimed != 1u
        || Sys_AtomicLoad(&available) != 0u)
    {
        return false;
    }

    claimed = 91u;
    if (queue_atomic::TryClaimUpTo(
            &available,
            10u,
            17u,
            &claimed)
        || claimed != 91u)
    {
        return false;
    }

    Sys_AtomicStore(&available, 18u);
    return !queue_atomic::TryClaimUpTo(
               &available,
               10u,
               17u,
               &claimed)
        && claimed == 91u
        && Sys_AtomicLoad(&available) == 18u;
}

bool TestCursorWrapAndFailureStability()
{
    volatile std::uint32_t cursor = 0u;
    std::uint32_t first = 99u;
    if (!queue_atomic::TryAdvanceCursor(&cursor, 1u, 1u, &first)
        || first != 0u
        || Sys_AtomicLoad(&cursor) != 0u)
    {
        return false;
    }

    Sys_AtomicStore(&cursor, 2u);
    if (!queue_atomic::TryAdvanceCursor(&cursor, 2u, 3u, &first)
        || first != 2u
        || Sys_AtomicLoad(&cursor) != 1u)
    {
        return false;
    }
    if (!queue_atomic::TryAdvanceCursor(&cursor, 2u, 3u, &first)
        || first != 1u
        || Sys_AtomicLoad(&cursor) != 0u)
    {
        return false;
    }

    first = 44u;
    Sys_AtomicStore(&cursor, 3u);
    return !queue_atomic::TryAdvanceCursor(&cursor, 1u, 3u, &first)
        && first == 44u
        && Sys_AtomicLoad(&cursor) == 3u;
}

bool TestGuardsAndBusyAbort()
{
    volatile std::uint32_t guard = 0u;
    volatile std::uint32_t available = 1u;
    volatile std::uint32_t cursor = 0u;
    if (!queue_atomic::TryAcquireGuard(&guard)
        || queue_atomic::TryAcquireGuard(&guard))
    {
        return false;
    }

    // A busy predicate returns while the guard is held, before either ready
    // credits or the read cursor are changed.
    if (!queue_atomic::ReleaseGuard(&guard)
        || Sys_AtomicLoad(&available) != 1u
        || Sys_AtomicLoad(&cursor) != 0u
        || !queue_atomic::TryAcquireGuard(&guard)
        || !queue_atomic::ReleaseGuard(&guard))
    {
        return false;
    }

    Sys_AtomicStore(&guard, 2u);
    return !queue_atomic::TryAcquireGuard(&guard)
        && !queue_atomic::ReleaseGuard(&guard)
        && Sys_AtomicLoad(&guard) == 2u;
}

bool TestProducerTransferAndInlineVisibility()
{
    volatile std::uint32_t outstanding = 0u;
    volatile std::uint32_t inFlight = 0u;
    if (Pending(&outstanding)
        || !queue_atomic::TryAddBounded(
            &outstanding,
            1u,
            UINT32_MAX)
        || !Pending(&outstanding))
    {
        return false;
    }

    // Ring occupancy is independent from the one full-lifetime outstanding
    // count, which stays claimed through queued handler completion.
    if (!queue_atomic::TryAddBounded(&inFlight, 1u, 2u)
        || !Pending(&outstanding)
        || !queue_atomic::TrySubtractBounded(&inFlight, 1u, 2u)
        || !Pending(&outstanding)
        || !queue_atomic::TrySubtractBounded(
            &outstanding,
            1u,
            UINT32_MAX)
        || Pending(&outstanding))
    {
        return false;
    }

    // A full-queue inline handler uses the same outstanding claim even though
    // it never consumes ring capacity.
    return queue_atomic::TryAddBounded(
               &outstanding,
               1u,
               UINT32_MAX)
        && Pending(&outstanding)
        && queue_atomic::TrySubtractBounded(
            &outstanding,
            1u,
            UINT32_MAX)
        && !Pending(&outstanding);
}

bool TestMpmcExactOnceWithWrap()
{
    constexpr std::uint32_t kCapacity = 17u;
    constexpr std::uint32_t kProducerCount = 8u;
    constexpr std::uint32_t kConsumerCount = 8u;
    constexpr std::uint32_t kItemsPerProducer = 1024u;
    constexpr std::uint32_t kItemCount =
        kProducerCount * kItemsPerProducer;

    struct Queue
    {
        volatile std::uint32_t start = 0u;
        volatile std::uint32_t end = 0u;
        volatile std::uint32_t producerGuard = 0u;
        volatile std::uint32_t consumerGuard = 0u;
        volatile std::uint32_t inFlight = 0u;
        volatile std::uint32_t available = 0u;
        std::array<std::uint32_t, kCapacity> payload{};
    } queue;

    std::array<std::atomic<std::uint32_t>, kItemCount> claims{};
    std::atomic<std::uint32_t> consumed{0u};
    std::atomic<bool> valid{true};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(kProducerCount + kConsumerCount);

    for (std::uint32_t producer = 0u;
         producer < kProducerCount;
         ++producer)
    {
        threads.emplace_back([&, producer]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (std::uint32_t item = 0u;
                 item < kItemsPerProducer;
                 ++item)
            {
                const std::uint32_t id =
                    producer * kItemsPerProducer + item;
                for (;;)
                {
                    if (!queue_atomic::TryAcquireGuard(
                            &queue.producerGuard))
                    {
                        std::this_thread::yield();
                        continue;
                    }
                    if (!queue_atomic::TryAddBounded(
                            &queue.inFlight,
                            1u,
                            kCapacity))
                    {
                        if (!queue_atomic::ReleaseGuard(
                                &queue.producerGuard))
                        {
                            valid.store(false, std::memory_order_relaxed);
                        }
                        std::this_thread::yield();
                        continue;
                    }

                    std::uint32_t slot = UINT32_MAX;
                    if (!queue_atomic::TryAdvanceCursor(
                            &queue.end,
                            1u,
                            kCapacity,
                            &slot))
                    {
                        valid.store(false, std::memory_order_relaxed);
                        return;
                    }
                    queue.payload[slot] = id;
                    if (!queue_atomic::TryAddBounded(
                            &queue.available,
                            1u,
                            kCapacity)
                        || !queue_atomic::ReleaseGuard(
                            &queue.producerGuard))
                    {
                        valid.store(false, std::memory_order_relaxed);
                    }
                    break;
                }
            }
        });
    }

    for (std::uint32_t consumer = 0u;
         consumer < kConsumerCount;
         ++consumer)
    {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            std::array<std::uint32_t, 10u> local{};
            while (consumed.load(std::memory_order_relaxed) < kItemCount)
            {
                if (!queue_atomic::TryAcquireGuard(&queue.consumerGuard))
                {
                    std::this_thread::yield();
                    continue;
                }
                std::uint32_t count = 0u;
                if (!queue_atomic::TryClaimUpTo(
                        &queue.available,
                        static_cast<std::uint32_t>(local.size()),
                        kCapacity,
                        &count))
                {
                    if (!queue_atomic::ReleaseGuard(&queue.consumerGuard))
                        valid.store(false, std::memory_order_relaxed);
                    std::this_thread::yield();
                    continue;
                }

                const std::uint32_t first = Sys_AtomicLoad(&queue.start);
                if (first >= kCapacity || count > kCapacity)
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }
                for (std::uint32_t index = 0u; index < count; ++index)
                    local[index] = queue.payload[(first + index) % kCapacity];

                std::uint32_t claimedFirst = UINT32_MAX;
                if (!queue_atomic::TryAdvanceCursor(
                        &queue.start,
                        count,
                        kCapacity,
                        &claimedFirst)
                    || claimedFirst != first
                    || !queue_atomic::ReleaseGuard(&queue.consumerGuard))
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }

                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    const std::uint32_t id = local[index];
                    if (id >= kItemCount
                        || claims[id].fetch_add(
                            1u,
                            std::memory_order_relaxed) != 0u)
                    {
                        valid.store(false, std::memory_order_relaxed);
                    }
                }
                if (!queue_atomic::TrySubtractBounded(
                        &queue.inFlight,
                        count,
                        kCapacity))
                {
                    valid.store(false, std::memory_order_relaxed);
                }
                consumed.fetch_add(count, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (std::thread &thread : threads)
        thread.join();

    if (!valid.load(std::memory_order_relaxed)
        || consumed.load(std::memory_order_relaxed) != kItemCount
        || Sys_AtomicLoad(&queue.inFlight) != 0u
        || Sys_AtomicLoad(&queue.available) != 0u
        || Sys_AtomicLoad(&queue.producerGuard) != 0u
        || Sys_AtomicLoad(&queue.consumerGuard) != 0u
        || Sys_AtomicLoad(&queue.start) >= kCapacity
        || Sys_AtomicLoad(&queue.end) >= kCapacity)
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
} // namespace

int main()
{
    if (!TestBoundedCounts())
        return Fail("bounded counters");
    if (!TestClaimsAndFailureStability())
        return Fail("bounded claims and stable failure outputs");
    if (!TestCursorWrapAndFailureStability())
        return Fail("element cursor wrap and corrupt cursor rejection");
    if (!TestGuardsAndBusyAbort())
        return Fail("queue guard ownership and busy abort");
    if (!TestProducerTransferAndInlineVisibility())
        return Fail("producer transfer and inline fallback visibility");
    if (!TestMpmcExactOnceWithWrap())
        return Fail("MPMC exact-once processing across wrapped reuse");
    return 0;
}

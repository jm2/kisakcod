#include <EffectsCore/fx_iterator_atomic.h>
#include <EffectsCore/fx_runtime.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

static_assert(std::is_same_v<decltype(FxSystem::iteratorCount), volatile std::int32_t>);

namespace
{
bool BasicTransitions()
{
    alignas(4) volatile std::int32_t state = 0;
    std::int32_t remaining = -1;

    if (!FxIteratorTryBeginCooperative(&state))
        return false;
    FxIteratorBeginCooperative(&state);
    if (Sys_AtomicLoad(&state) != 2 || FxIteratorTryBeginExclusive(&state))
        return false;
    if (!FxIteratorEndCooperative(&state, &remaining) || remaining != 1)
        return false;
    if (!FxIteratorEndCooperative(&state, &remaining) || remaining != 0)
        return false;
    if (!FxIteratorTryBeginExclusive(&state) || Sys_AtomicLoad(&state) != -1
        || FxIteratorTryBeginCooperative(&state))
        return false;
    if (FxIteratorTryBeginExclusive(&state))
        return false;
    return FxIteratorEndExclusive(&state) && Sys_AtomicLoad(&state) == 0;
}

bool RejectsInvalidReleases()
{
    alignas(4) volatile std::int32_t state = 0;
    std::int32_t remaining = 17;
    if (FxIteratorEndCooperative(&state, &remaining) || remaining != 17
        || Sys_AtomicLoad(&state) != 0 || FxIteratorEndExclusive(&state))
    {
        return false;
    }

    Sys_AtomicStore(&state, -1);
    if (FxIteratorEndCooperative(&state, &remaining) || remaining != 17
        || Sys_AtomicLoad(&state) != -1)
    {
        return false;
    }
    return FxIteratorEndExclusive(&state);
}

bool DowngradesExclusiveToCooperative()
{
    alignas(4) volatile std::int32_t state = -1;
    std::int32_t remaining = -1;

    if (!FxIteratorDowngradeExclusiveToCooperative(&state)
        || Sys_AtomicLoad(&state) != 1
        || FxIteratorDowngradeExclusiveToCooperative(&state))
    {
        return false;
    }
    return FxIteratorEndCooperative(&state, &remaining)
        && remaining == 0 && Sys_AtomicLoad(&state) == 0;
}

bool GarbageCollectionFlagIsAtomicAndLayoutPreserving()
{
    bool requested = false;
    if (FxGarbageCollectionRequested(&requested))
        return false;
    FxRequestGarbageCollection(&requested);
    if (!FxGarbageCollectionRequested(&requested))
        return false;
    FxClearGarbageCollectionRequest(&requested);
    return !FxGarbageCollectionRequested(&requested) && sizeof(requested) == 1u;
}

bool WaitsAcrossOppositeOwners()
{
    using namespace std::chrono_literals;

    alignas(4) volatile std::int32_t state = 0;
    std::atomic<bool> started{false};
    std::atomic<bool> acquired{false};

    if (!FxIteratorTryBeginExclusive(&state))
        return false;
    std::thread cooperative([&] {
        started.store(true, std::memory_order_release);
        FxIteratorBeginCooperative(&state);
        acquired.store(true, std::memory_order_release);
        std::int32_t remaining = -1;
        if (!FxIteratorEndCooperative(&state, &remaining) || remaining != 0)
            acquired.store(false, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire))
    {
    }
    std::this_thread::sleep_for(2ms);
    if (acquired.load(std::memory_order_acquire) || !FxIteratorEndExclusive(&state))
    {
        cooperative.join();
        return false;
    }
    cooperative.join();
    if (!acquired.load(std::memory_order_acquire))
        return false;

    FxIteratorBeginCooperative(&state);
    started.store(false, std::memory_order_release);
    acquired.store(false, std::memory_order_release);
    std::thread exclusive([&] {
        started.store(true, std::memory_order_release);
        FxIteratorWaitBeginExclusive(&state);
        acquired.store(true, std::memory_order_release);
        if (!FxIteratorEndExclusive(&state))
            acquired.store(false, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire))
    {
    }
    std::this_thread::sleep_for(2ms);
    if (acquired.load(std::memory_order_acquire))
    {
        exclusive.join();
        return false;
    }
    std::int32_t remaining = -1;
    if (!FxIteratorEndCooperative(&state, &remaining) || remaining != 0)
    {
        exclusive.join();
        return false;
    }
    exclusive.join();
    return acquired.load(std::memory_order_acquire) && Sys_AtomicLoad(&state) == 0;
}

bool HandlesSaturatedCountWithoutOverflow()
{
    using namespace std::chrono_literals;

    alignas(4) volatile std::int32_t state = (std::numeric_limits<std::int32_t>::max)();
    std::atomic<bool> started{false};
    std::atomic<bool> acquired{false};
    if (FxIteratorTryBeginCooperative(&state))
        return false;
    std::thread waiter([&] {
        started.store(true, std::memory_order_release);
        FxIteratorBeginCooperative(&state);
        acquired.store(true, std::memory_order_release);
        std::int32_t ignored = 0;
        FxIteratorEndCooperative(&state, &ignored);
    });
    while (!started.load(std::memory_order_acquire))
    {
    }
    std::this_thread::sleep_for(2ms);
    if (acquired.load(std::memory_order_acquire))
    {
        waiter.join();
        return false;
    }

    std::int32_t remaining = 0;
    if (!FxIteratorEndCooperative(&state, &remaining)
        || remaining != (std::numeric_limits<std::int32_t>::max)() - 1)
    {
        waiter.join();
        return false;
    }
    waiter.join();
    return acquired.load(std::memory_order_acquire)
        && Sys_AtomicLoad(&state) == (std::numeric_limits<std::int32_t>::max)() - 1;
}

bool ContentionStress()
{
    constexpr int COOPERATIVE_THREADS = 4;
    constexpr int EXCLUSIVE_THREADS = 2;
    constexpr int ITERATIONS = 2000;

    alignas(4) volatile std::int32_t state = 0;
    std::atomic<int> cooperativeInside{0};
    std::atomic<int> exclusiveInside{0};
    std::atomic<bool> failed{false};
    std::vector<std::thread> threads;

    for (int threadIndex = 0; threadIndex < COOPERATIVE_THREADS; ++threadIndex)
    {
        threads.emplace_back([&] {
            for (int iteration = 0; iteration < ITERATIONS; ++iteration)
            {
                FxIteratorBeginCooperative(&state);
                cooperativeInside.fetch_add(1, std::memory_order_seq_cst);
                if (exclusiveInside.load(std::memory_order_seq_cst) != 0)
                    failed.store(true, std::memory_order_relaxed);
                std::this_thread::yield();
                cooperativeInside.fetch_sub(1, std::memory_order_seq_cst);
                std::int32_t remaining = -1;
                if (!FxIteratorEndCooperative(&state, &remaining) || remaining < 0)
                    failed.store(true, std::memory_order_relaxed);
            }
        });
    }

    for (int threadIndex = 0; threadIndex < EXCLUSIVE_THREADS; ++threadIndex)
    {
        threads.emplace_back([&] {
            for (int iteration = 0; iteration < ITERATIONS; ++iteration)
            {
                FxIteratorWaitBeginExclusive(&state);
                if (exclusiveInside.fetch_add(1, std::memory_order_seq_cst) != 0
                    || cooperativeInside.load(std::memory_order_seq_cst) != 0)
                {
                    failed.store(true, std::memory_order_relaxed);
                }
                std::this_thread::yield();
                exclusiveInside.fetch_sub(1, std::memory_order_seq_cst);
                if (!FxIteratorEndExclusive(&state))
                    failed.store(true, std::memory_order_relaxed);
            }
        });
    }

    for (std::thread &thread : threads)
        thread.join();
    return !failed.load(std::memory_order_relaxed) && Sys_AtomicLoad(&state) == 0;
}
} // namespace

int main()
{
    if (!BasicTransitions())
        return 1;
    if (!RejectsInvalidReleases())
        return 2;
    if (!DowngradesExclusiveToCooperative())
        return 3;
    if (!GarbageCollectionFlagIsAtomicAndLayoutPreserving())
        return 4;
    if (!WaitsAcrossOppositeOwners())
        return 5;
    if (!HandlesSaturatedCountWithoutOverflow())
        return 6;
    if (!ContentionStress())
        return 7;

    std::cout << "fx iterator atomic tests passed\n";
    return 0;
}

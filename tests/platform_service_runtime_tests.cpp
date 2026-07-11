#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

#include <qcommon/sys_event.h>
#include <qcommon/sys_sync.h>
#include <qcommon/sys_time.h>

// The standalone service target intentionally does not link the engine's
// assert/reporting graph.  Preserve debug assertion behavior with a fatal test
// handler so Debug MSVC configurations remain independently linkable.
void MyAssertHandler(const char *, int, int, const char *, ...)
{
    std::abort();
}

namespace
{
bool IsForwardOrEqual(const std::uint32_t before, const std::uint32_t after)
{
    return after - before <= static_cast<std::uint32_t>(
        std::numeric_limits<std::int32_t>::max());
}

bool RunEventWaiterRound(
    SysEventHandle *const event,
    const std::uint32_t expectedSignaledWaiters,
    const char *const eventKind)
{
    constexpr std::uint32_t waiterCount = 4;
    constexpr std::uint32_t waiterTimeoutMilliseconds = 250;

    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> signaledCount{0};
    std::atomic<std::uint32_t> completedCount{0};

    std::vector<std::thread> waiters;
    waiters.reserve(waiterCount);
    for (std::uint32_t waiter = 0; waiter < waiterCount; ++waiter)
    {
        waiters.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            if (Sys_WaitForSingleObjectTimeout(
                    event,
                    waiterTimeoutMilliseconds))
            {
                signaledCount.fetch_add(1);
            }
            completedCount.fetch_add(1);
        });
    }

    while (readyCount.load(std::memory_order_acquire) != waiterCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);
    Sys_SetEvent(event);

    for (std::thread &waiter : waiters)
        waiter.join();

    if (completedCount.load() != waiterCount
        || signaledCount.load() != expectedSignaledWaiters)
    {
        std::fprintf(
            stderr,
            "%s event woke %u of %u waiters; expected %u\n",
            eventKind,
            signaledCount.load(),
            completedCount.load(),
            expectedSignaledWaiters);
        return false;
    }

    return true;
}

bool TestAutoResetEvents()
{
    SysEventHandle event = nullptr;
    Sys_CreateEvent(false, false, &event);
    if (!event)
    {
        std::fputs("auto-reset event creation returned null\n", stderr);
        return false;
    }

    bool valid = true;
    if (Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 5))
    {
        std::fputs("initially-false auto-reset event was signaled\n", stderr);
        valid = false;
    }

    std::atomic<bool> infiniteWaitReady{false};
    std::atomic<bool> infiniteWaitCompleted{false};
    std::thread infiniteWaiter([&]() {
        infiniteWaitReady.store(true, std::memory_order_release);
        Sys_WaitForSingleObject(&event);
        infiniteWaitCompleted.store(true, std::memory_order_release);
    });
    while (!infiniteWaitReady.load(std::memory_order_acquire))
        std::this_thread::yield();
    Sys_SetEvent(&event);
    infiniteWaiter.join();
    if (!infiniteWaitCompleted.load(std::memory_order_acquire))
    {
        std::fputs("infinite event wait did not consume its signal\n", stderr);
        valid = false;
    }

    if (!RunEventWaiterRound(&event, 1, "auto-reset")
        || !RunEventWaiterRound(&event, 1, "auto-reset"))
    {
        valid = false;
    }

    Sys_DestroyEvent(&event);
    if (event)
    {
        std::fputs("auto-reset event destroy did not null its handle\n", stderr);
        valid = false;
    }

    Sys_CreateEvent(false, true, &event);
    if (!event)
    {
        std::fputs("initially-true auto-reset event creation returned null\n", stderr);
        return false;
    }
    if (!Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 0))
    {
        std::fputs("initial auto-reset signal was not consumed exactly once\n", stderr);
        valid = false;
    }

    Sys_SetEvent(&event);
    if (!Sys_WaitForSingleObjectTimeout(&event, 50)
        || Sys_WaitForSingleObjectTimeout(&event, 0))
    {
        std::fputs("set auto-reset signal was not consumed exactly once\n", stderr);
        valid = false;
    }

    Sys_DestroyEvent(&event);
    if (event)
    {
        std::fputs("initially-true auto-reset destroy did not null its handle\n", stderr);
        valid = false;
    }

    return valid;
}

bool TestManualResetEvents()
{
    SysEventHandle event = nullptr;
    Sys_CreateEvent(true, false, &event);
    if (!event)
    {
        std::fputs("manual-reset event creation returned null\n", stderr);
        return false;
    }

    bool valid = true;
    if (Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 5))
    {
        std::fputs("initially-false manual-reset event was signaled\n", stderr);
        valid = false;
    }

    if (!RunEventWaiterRound(&event, 4, "manual-reset"))
        valid = false;

    if (!Sys_WaitForSingleObjectTimeout(&event, 0)
        || !Sys_WaitForSingleObjectTimeout(&event, 0))
    {
        std::fputs("manual-reset event did not retain its signaled state\n", stderr);
        valid = false;
    }

    Sys_ResetEvent(&event);
    if (Sys_WaitForSingleObjectTimeout(&event, 0)
        || Sys_WaitForSingleObjectTimeout(&event, 5))
    {
        std::fputs("manual-reset event remained signaled after reset\n", stderr);
        valid = false;
    }

    Sys_DestroyEvent(&event);
    if (event)
    {
        std::fputs("manual-reset event destroy did not null its handle\n", stderr);
        valid = false;
    }

    return valid;
}

bool TestConcurrentInitialization()
{
    constexpr std::uint32_t threadCount = 8;
    constexpr std::uint32_t initializationWindowMilliseconds = 5'000;
    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> failureCount{0};

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (std::uint32_t worker = 0; worker < threadCount; ++worker)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            const std::uint32_t before = Sys_Milliseconds();
            Sys_EnterCriticalSection(CRITSECT_CONSOLE);
            Sys_LeaveCriticalSection(CRITSECT_CONSOLE);
            const std::uint32_t after = Sys_Milliseconds();
            if (before > initializationWindowMilliseconds
                || !IsForwardOrEqual(before, after))
                failureCount.fetch_add(1);
        });
    }

    while (readyCount.load(std::memory_order_acquire) != threadCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);

    for (std::thread &worker : workers)
        worker.join();

    if (failureCount.load() != 0)
    {
        std::fputs("concurrent platform-service initialization failed\n", stderr);
        return false;
    }
    return true;
}

bool TestTimeServices()
{
    const std::uint32_t rawBeforeYield = Sys_MillisecondsRaw();
    const std::uint32_t relativeBeforeYield = Sys_Milliseconds();
    Sys_Sleep(0);
    const std::uint32_t rawAfterYield = Sys_MillisecondsRaw();
    const std::uint32_t relativeAfterYield = Sys_Milliseconds();

    if (!IsForwardOrEqual(rawBeforeYield, rawAfterYield)
        || !IsForwardOrEqual(relativeBeforeYield, relativeAfterYield))
    {
        std::fputs("platform time moved backwards across Sys_Sleep(0)\n", stderr);
        return false;
    }

    constexpr std::uint32_t shortSleepMilliseconds = 15;
    const auto steadyBeforeSleep = std::chrono::steady_clock::now();
    const std::uint32_t rawBeforeSleep = Sys_MillisecondsRaw();
    const std::uint32_t relativeBeforeSleep = Sys_Milliseconds();
    Sys_Sleep(shortSleepMilliseconds);
    const std::uint32_t rawAfterSleep = Sys_MillisecondsRaw();
    const std::uint32_t relativeAfterSleep = Sys_Milliseconds();
    const auto steadyAfterSleep = std::chrono::steady_clock::now();

    if (!IsForwardOrEqual(rawBeforeSleep, rawAfterSleep)
        || !IsForwardOrEqual(relativeBeforeSleep, relativeAfterSleep))
    {
        std::fputs("platform time moved backwards across a short sleep\n", stderr);
        return false;
    }
    if (rawAfterSleep - rawBeforeSleep == 0
        || relativeAfterSleep - relativeBeforeSleep == 0)
    {
        std::fputs("platform clocks did not advance across a short sleep\n", stderr);
        return false;
    }

    const auto steadyElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        steadyAfterSleep - steadyBeforeSleep);
    if (steadyElapsed.count() < 1)
    {
        std::fputs("Sys_Sleep returned without an observable short delay\n", stderr);
        return false;
    }

    return true;
}

template <typename LockFunction, typename UnlockFunction>
bool TestContendedExclusion(
    const char *const serviceName,
    LockFunction lock,
    UnlockFunction unlock)
{
    constexpr std::uint32_t threadCount = 4;
    constexpr std::uint32_t iterationsPerThread = 256;

    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> insideCount{0};
    std::atomic<std::uint32_t> completedCount{0};
    std::atomic<std::uint32_t> violationCount{0};

    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (std::uint32_t worker = 0; worker < threadCount; ++worker)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (std::uint32_t iteration = 0;
                 iteration < iterationsPerThread;
                 ++iteration)
            {
                lock();
                if (insideCount.fetch_add(1) != 0)
                    violationCount.fetch_add(1);

                if ((iteration & 31U) == 0)
                    std::this_thread::yield();

                completedCount.fetch_add(1);
                if (insideCount.fetch_sub(1) != 1)
                    violationCount.fetch_add(1);
                unlock();
            }
        });
    }

    while (readyCount.load(std::memory_order_acquire) != threadCount)
        Sys_Sleep(0);
    start.store(true, std::memory_order_release);

    for (std::thread &worker : workers)
        worker.join();

    const std::uint32_t expectedCount = threadCount * iterationsPerThread;
    if (completedCount.load() != expectedCount
        || insideCount.load() != 0
        || violationCount.load() != 0)
    {
        std::fprintf(stderr, "%s did not provide mutual exclusion\n", serviceName);
        return false;
    }

    return true;
}

bool TestCriticalSections()
{
    Sys_InitializeCriticalSections();
    Sys_InitializeCriticalSections();

    Sys_EnterCriticalSection(CRITSECT_CONSOLE);
    Sys_EnterCriticalSection(CRITSECT_CONSOLE);
    Sys_LeaveCriticalSection(CRITSECT_CONSOLE);
    Sys_LeaveCriticalSection(CRITSECT_CONSOLE);

    return TestContendedExclusion(
        "recursive critical section",
        []() { Sys_EnterCriticalSection(CRITSECT_CONSOLE); },
        []() { Sys_LeaveCriticalSection(CRITSECT_CONSOLE); });
}

bool TestFastCriticalSection()
{
    FastCriticalSection criticalSection{};
    if (Sys_IsWriteLocked(&criticalSection))
    {
        std::fputs("FastCriticalSection started write-locked\n", stderr);
        return false;
    }

    Sys_LockWrite(&criticalSection);
    if (!Sys_IsWriteLocked(&criticalSection))
    {
        std::fputs("FastCriticalSection did not publish its writer state\n", stderr);
        return false;
    }
    Sys_UnlockWrite(&criticalSection);
    if (Sys_IsWriteLocked(&criticalSection))
    {
        std::fputs("FastCriticalSection retained its writer state after unlock\n", stderr);
        return false;
    }

    const bool excluded = TestContendedExclusion(
        "FastCriticalSection",
        [&criticalSection]() { Sys_LockWrite(&criticalSection); },
        [&criticalSection]() { Sys_UnlockWrite(&criticalSection); });

    if (criticalSection.readCount != 0 || criticalSection.writeCount != 0)
    {
        std::fputs("FastCriticalSection counters were not balanced\n", stderr);
        return false;
    }

    return excluded;
}

bool TestFastCriticalSectionReadersAndWriters()
{
    constexpr std::uint32_t readerCount = 4;
    constexpr std::uint32_t writerCount = 2;
    constexpr std::uint32_t readerIterations = 512;
    constexpr std::uint32_t writerIterations = 256;
    constexpr std::uint64_t payloadMask = 0xA5A55A5AF0F00F0FULL;

    struct SharedPayload
    {
        std::uint64_t generation;
        std::uint64_t check;
    };

    FastCriticalSection criticalSection{};
    SharedPayload payload{0, payloadMask};
    std::atomic<std::uint32_t> readyCount{0};
    std::atomic<bool> start{false};
    std::atomic<std::uint32_t> firstReadersInside{0};
    std::atomic<std::uint32_t> activeReaders{0};
    std::atomic<std::uint32_t> activeWriters{0};
    std::atomic<std::uint32_t> readerOverlapViolations{0};
    std::atomic<std::uint32_t> writerOverlapViolations{0};
    std::atomic<std::uint32_t> writerStateViolations{0};
    std::atomic<std::uint32_t> payloadViolations{0};

    std::vector<std::thread> workers;
    workers.reserve(readerCount + writerCount);

    for (std::uint32_t reader = 0; reader < readerCount; ++reader)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (std::uint32_t iteration = 0;
                 iteration < readerIterations;
                 ++iteration)
            {
                Sys_LockRead(&criticalSection);
                activeReaders.fetch_add(1);
                if (activeWriters.load() != 0)
                    readerOverlapViolations.fetch_add(1);

                if (iteration == 0)
                {
                    firstReadersInside.fetch_add(1, std::memory_order_release);
                    while (firstReadersInside.load(std::memory_order_acquire) != readerCount)
                        std::this_thread::yield();
                }

                const std::uint64_t generation = payload.generation;
                std::this_thread::yield();
                if (payload.check != (generation ^ payloadMask))
                    payloadViolations.fetch_add(1);

                activeReaders.fetch_sub(1);
                Sys_UnlockRead(&criticalSection);
            }
        });
    }

    for (std::uint32_t writer = 0; writer < writerCount; ++writer)
    {
        workers.emplace_back([&]() {
            readyCount.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();

            for (std::uint32_t iteration = 0;
                 iteration < writerIterations;
                 ++iteration)
            {
                Sys_LockWrite(&criticalSection);
                if (!Sys_IsWriteLocked(&criticalSection))
                    writerStateViolations.fetch_add(1);
                if (activeWriters.fetch_add(1) != 0 || activeReaders.load() != 0)
                    writerOverlapViolations.fetch_add(1);

                const std::uint64_t nextGeneration = payload.generation + 1;
                payload.generation = nextGeneration;
                std::this_thread::yield();
                payload.check = nextGeneration ^ payloadMask;

                if (activeWriters.fetch_sub(1) != 1)
                    writerOverlapViolations.fetch_add(1);
                Sys_UnlockWrite(&criticalSection);
            }
        });
    }

    while (readyCount.load(std::memory_order_acquire) != readerCount + writerCount)
        std::this_thread::yield();
    start.store(true, std::memory_order_release);

    for (std::thread &worker : workers)
        worker.join();

    const std::uint64_t expectedGeneration = writerCount * writerIterations;
    if (firstReadersInside.load() != readerCount
        || activeReaders.load() != 0
        || activeWriters.load() != 0
        || readerOverlapViolations.load() != 0
        || writerOverlapViolations.load() != 0
        || writerStateViolations.load() != 0
        || payloadViolations.load() != 0
        || payload.generation != expectedGeneration
        || payload.check != (expectedGeneration ^ payloadMask)
        || criticalSection.readCount != 0
        || criticalSection.writeCount != 0
        || Sys_IsWriteLocked(&criticalSection))
    {
        std::fprintf(
            stderr,
            "FastCriticalSection reader/writer contract failed: "
            "firstReaders=%u activeReaders=%u activeWriters=%u "
            "readerOverlap=%u writerOverlap=%u writerState=%u payload=%u "
            "generation=%llu expected=%llu check=%llu readCount=%u writeCount=%u\n",
            firstReadersInside.load(),
            activeReaders.load(),
            activeWriters.load(),
            readerOverlapViolations.load(),
            writerOverlapViolations.load(),
            writerStateViolations.load(),
            payloadViolations.load(),
            static_cast<unsigned long long>(payload.generation),
            static_cast<unsigned long long>(expectedGeneration),
            static_cast<unsigned long long>(payload.check),
            criticalSection.readCount,
            criticalSection.writeCount);
        return false;
    }

    return true;
}
}

int main()
{
    if (!TestAutoResetEvents())
        return 1;
    if (!TestManualResetEvents())
        return 1;
    if (!TestConcurrentInitialization())
        return 1;
    if (!TestTimeServices())
        return 1;
    if (!TestCriticalSections())
        return 1;
    if (!TestFastCriticalSection())
        return 1;
    if (!TestFastCriticalSectionReadersAndWriters())
        return 1;

    std::puts("platform service runtime contracts passed");
    return 0;
}

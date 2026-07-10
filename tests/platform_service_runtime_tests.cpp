#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

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
}

int main()
{
    if (!TestConcurrentInitialization())
        return 1;
    if (!TestTimeServices())
        return 1;
    if (!TestCriticalSections())
        return 1;
    if (!TestFastCriticalSection())
        return 1;

    std::puts("platform service runtime contracts passed");
    return 0;
}

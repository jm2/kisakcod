#include <qcommon/sys_memory.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace
{
bool Check(const bool condition)
{
    return condition;
}

bool TestReservationLifecycle()
{
    const std::size_t pageSize = Sys_VirtualMemoryPageSize();
    if (!Check(pageSize > 0))
        return false;

    const std::size_t requestedSize = pageSize + 17;
    void *const reservation = Sys_VirtualMemoryReserve(requestedSize);
    if (!Check(reservation != nullptr)
        || !Check(reinterpret_cast<std::uintptr_t>(reservation) % pageSize == 0))
    {
        return false;
    }

    auto *const bytes = static_cast<std::uint8_t *>(reservation);
    if (!Check(Sys_VirtualMemoryCommit(bytes, pageSize)))
        return false;
    std::memset(bytes, 0xA5, pageSize);

    if (!Check(Sys_VirtualMemoryCommit(bytes + pageSize, 17)))
        return false;
    std::memset(bytes + pageSize, 0x5A, 17);

    if (!Check(Sys_VirtualMemoryDecommit(bytes, pageSize))
        || !Check(Sys_VirtualMemoryCommit(bytes, pageSize)))
    {
        return false;
    }

    for (std::size_t index = 0; index < pageSize; ++index)
    {
        if (!Check(bytes[index] == 0))
            return false;
    }
    if (!Check(bytes[pageSize] == 0x5A))
        return false;

    if (!Check(Sys_VirtualMemoryDecommit(bytes + pageSize, 17))
        || !Check(Sys_VirtualMemoryCommit(bytes + pageSize, 17)))
    {
        return false;
    }
    for (std::size_t index = pageSize; index < 2 * pageSize; ++index)
    {
        if (!Check(bytes[index] == 0))
            return false;
    }

    if (!Check(Sys_VirtualMemoryRelease(reservation)))
        return false;
    return Check(!Sys_VirtualMemoryRelease(reservation));
}

bool TestInvalidRequests()
{
    const std::size_t pageSize = Sys_VirtualMemoryPageSize();
    if (!Check(pageSize > 0)
        || !Check(Sys_VirtualMemoryReserve(0) == nullptr)
        || !Check(Sys_VirtualMemoryReserve(
            std::numeric_limits<std::size_t>::max()) == nullptr)
        || !Check(!Sys_VirtualMemoryCommit(nullptr, pageSize))
        || !Check(!Sys_VirtualMemoryDecommit(nullptr, pageSize))
        || !Check(!Sys_VirtualMemoryRelease(nullptr)))
    {
        return false;
    }

    void *const reservation = Sys_VirtualMemoryReserve(pageSize + 1);
    if (!Check(reservation != nullptr))
        return false;

    auto *const bytes = static_cast<std::uint8_t *>(reservation);
    const bool rejected =
        !Sys_VirtualMemoryCommit(bytes, 0)
        && !Sys_VirtualMemoryDecommit(bytes, 0)
        && !Sys_VirtualMemoryCommit(bytes + 1, pageSize)
        && !Sys_VirtualMemoryDecommit(bytes + 1, pageSize)
        && !Sys_VirtualMemoryCommit(bytes + 2 * pageSize, pageSize)
        && !Sys_VirtualMemoryDecommit(bytes + 2 * pageSize, pageSize)
        && !Sys_VirtualMemoryCommit(
            bytes,
            std::numeric_limits<std::size_t>::max())
        && !Sys_VirtualMemoryDecommit(
            bytes,
            std::numeric_limits<std::size_t>::max())
        && !Sys_VirtualMemoryRelease(bytes + pageSize);

    return Check(rejected) && Check(Sys_VirtualMemoryRelease(reservation));
}

bool TestConcurrentIndependentReservations()
{
    constexpr int threadCount = 8;
    constexpr int iterations = 32;
    const std::size_t pageSize = Sys_VirtualMemoryPageSize();
    std::atomic<bool> passed{pageSize > 0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads.emplace_back([pageSize, threadIndex, &passed] {
            for (int iteration = 0; iteration < iterations; ++iteration)
            {
                void *const reservation = Sys_VirtualMemoryReserve(pageSize + 3);
                if (!reservation
                    || !Sys_VirtualMemoryCommit(reservation, pageSize + 3))
                {
                    passed.store(false, std::memory_order_relaxed);
                    if (reservation)
                        Sys_VirtualMemoryRelease(reservation);
                    return;
                }

                auto *const bytes = static_cast<std::uint8_t *>(reservation);
                bytes[0] = static_cast<std::uint8_t>(threadIndex);
                bytes[pageSize + 2] = static_cast<std::uint8_t>(iteration);
                if (bytes[0] != static_cast<std::uint8_t>(threadIndex)
                    || bytes[pageSize + 2] != static_cast<std::uint8_t>(iteration)
                    || !Sys_VirtualMemoryRelease(reservation))
                {
                    passed.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (std::thread &thread : threads)
        thread.join();

    return Check(passed.load(std::memory_order_relaxed));
}
}

int main()
{
    return TestReservationLifecycle()
        && TestInvalidRequests()
        && TestConcurrentIndependentReservations()
        ? 0
        : 1;
}

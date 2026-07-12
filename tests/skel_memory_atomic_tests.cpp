#include <qcommon/skel_memory_atomic.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

namespace
{
using skel_memory_atomic::AdvanceEpoch;
using skel_memory_atomic::CheckedAlignUp;
using skel_memory_atomic::ClaimWarning;
using skel_memory_atomic::LoadEpoch;
using skel_memory_atomic::LoadTimestamp;
using skel_memory_atomic::MakeArenaView;
using skel_memory_atomic::ReserveAligned;
using skel_memory_atomic::ResetGuard;
using skel_memory_atomic::kInvalidOffset;

volatile std::uint32_t g_wrapCallbacks;
volatile std::uint32_t g_resetGate;

void RecordEpochWrap()
{
    Sys_AtomicIncrement(&g_wrapCallbacks);
}

int Fail(const char *const message)
{
    std::fprintf(stderr, "skeleton arena atomic test failed: %s\n", message);
    return 1;
}

bool TestCheckedAlignmentAndArenaCapacity()
{
    std::uint32_t aligned = 0u;
    if (!CheckedAlignUp(1u, 16u, &aligned) || aligned != 16u)
        return false;
    if (!CheckedAlignUp(16u, 16u, &aligned) || aligned != 16u)
        return false;
    if (!CheckedAlignUp(17u, 16u, &aligned) || aligned != 32u)
        return false;
    if (CheckedAlignUp(0u, 16u, &aligned)
        || CheckedAlignUp(1u, 0u, &aligned)
        || CheckedAlignUp(1u, 3u, &aligned)
        || CheckedAlignUp(1u, 16u, nullptr))
    {
        return false;
    }

    constexpr std::uint32_t kMax =
        (std::numeric_limits<std::uint32_t>::max)();
    if (!CheckedAlignUp(kMax - 15u, 16u, &aligned)
        || aligned != kMax - 15u)
    {
        return false;
    }
    if (CheckedAlignUp(kMax - 14u, 16u, &aligned))
        return false;

    alignas(16) char storage[80]{};
    const auto arena = MakeArenaView(storage + 1, 64u, 16u);
    if (arena.base != storage + 16 || arena.capacity != 49u)
        return false;

    const auto tooSmall = MakeArenaView(storage + 1, 14u, 16u);
    return tooSmall.base == nullptr && tooSmall.capacity == 0u;
}

bool TestFailureDoesNotPoisonCursor()
{
    volatile std::uint32_t cursor = 0u;
    if (ReserveAligned(&cursor, 1u, 64u, 16u) != 0u)
        return false;
    if (ReserveAligned(&cursor, 32u, 64u, 16u) != 16u)
        return false;

    if (ReserveAligned(&cursor, 17u, 64u, 16u) != kInvalidOffset
        || LoadEpoch(&cursor) != 48u)
    {
        return false;
    }
    if (ReserveAligned(&cursor, 16u, 64u, 16u) != 48u
        || LoadEpoch(&cursor) != 64u)
    {
        return false;
    }
    if (ReserveAligned(&cursor, 1u, 64u, 16u) != kInvalidOffset
        || LoadEpoch(&cursor) != 64u)
    {
        return false;
    }

    Sys_AtomicStore(&cursor, 65u);
    if (ReserveAligned(&cursor, 1u, 64u, 16u) != kInvalidOffset
        || LoadEpoch(&cursor) != 65u)
    {
        return false;
    }

    Sys_AtomicStore(&cursor, 7u);
    if (ReserveAligned(&cursor, 1u, 64u, 16u) != kInvalidOffset
        || LoadEpoch(&cursor) != 7u)
    {
        return false;
    }
    return ReserveAligned(&cursor, kInvalidOffset, 64u, 16u)
            == kInvalidOffset
        && LoadEpoch(&cursor) == 7u;
}

bool TestReservationContention()
{
    constexpr std::uint32_t kAlignment = 16u;
    constexpr std::uint32_t kSlots = 4096u;
    constexpr std::uint32_t kCapacity = kSlots * kAlignment;
    constexpr std::uint32_t kThreads = 8u;

    volatile std::uint32_t cursor = 0u;
    std::array<std::atomic<std::uint32_t>, kSlots> claims{};
    std::atomic<bool> valid{true};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (std::uint32_t threadIndex = 0u;
         threadIndex < kThreads;
         ++threadIndex)
    {
        workers.emplace_back([&]() {
            for (;;)
            {
                const std::uint32_t offset = ReserveAligned(
                    &cursor,
                    1u,
                    kCapacity,
                    kAlignment);
                if (offset == kInvalidOffset)
                    return;
                if ((offset % kAlignment) != 0u
                    || offset >= kCapacity)
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }

                const std::uint32_t slot = offset / kAlignment;
                if (claims[slot].fetch_add(
                        1u,
                        std::memory_order_relaxed) != 0u)
                {
                    valid.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (std::thread &worker : workers)
        worker.join();

    if (!valid.load(std::memory_order_relaxed)
        || LoadEpoch(&cursor) != kCapacity)
    {
        return false;
    }
    for (const auto &claim : claims)
    {
        if (claim.load(std::memory_order_relaxed) != 1u)
            return false;
    }

    return ReserveAligned(&cursor, 1u, kCapacity, kAlignment)
            == kInvalidOffset
        && LoadEpoch(&cursor) == kCapacity;
}

bool TestEpochRolloverAndWarningClaim()
{
    constexpr std::uint32_t kMax =
        (std::numeric_limits<std::uint32_t>::max)();
    volatile std::uint32_t epoch = kMax - 1u;
    Sys_AtomicStore(&g_wrapCallbacks, 0u);
    if (AdvanceEpoch(&epoch, RecordEpochWrap) != kMax
        || LoadTimestamp(&epoch) != -1
        || Sys_AtomicLoad(&g_wrapCallbacks) != 0u)
    {
        return false;
    }
    if (AdvanceEpoch(&epoch, RecordEpochWrap) != 1u
        || LoadEpoch(&epoch) != 1u
        || Sys_AtomicLoad(&g_wrapCallbacks) != 1u)
    {
        return false;
    }

    // Resetters are serialized around arena, cursor, invalidation, and epoch
    // publication.  Starting both at max therefore runs exactly one callback:
    // the first reset publishes one and the second advances it to two.
    epoch = kMax;
    Sys_AtomicStore(&g_wrapCallbacks, 0u);
    Sys_AtomicStore(&g_resetGate, 0u);
    std::atomic<bool> wrapStart{false};
    std::thread firstWrap([&]() {
        while (!wrapStart.load(std::memory_order_acquire))
            std::this_thread::yield();
        const ResetGuard resetGuard(&g_resetGate);
        AdvanceEpoch(&epoch, RecordEpochWrap);
    });
    std::thread secondWrap([&]() {
        while (!wrapStart.load(std::memory_order_acquire))
            std::this_thread::yield();
        const ResetGuard resetGuard(&g_resetGate);
        AdvanceEpoch(&epoch, RecordEpochWrap);
    });
    wrapStart.store(true, std::memory_order_release);
    firstWrap.join();
    secondWrap.join();
    if (LoadEpoch(&epoch) != 2u
        || Sys_AtomicLoad(&g_wrapCallbacks) != 1u
        || Sys_AtomicLoad(&g_resetGate) != 0u)
    {
        return false;
    }

    constexpr std::uint32_t kThreads = 8u;
    volatile std::uint32_t warnedEpoch = 0u;
    std::atomic<std::uint32_t> claims{0u};
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (std::uint32_t threadIndex = 0u;
         threadIndex < kThreads;
         ++threadIndex)
    {
        workers.emplace_back([&]() {
            if (ClaimWarning(&warnedEpoch, LoadTimestamp(&epoch)))
                claims.fetch_add(1u, std::memory_order_relaxed);
        });
    }
    for (std::thread &worker : workers)
        worker.join();

    if (claims.load(std::memory_order_relaxed) != 1u
        || ClaimWarning(&warnedEpoch, LoadTimestamp(&epoch)))
    {
        return false;
    }

    AdvanceEpoch(&epoch);
    return ClaimWarning(&warnedEpoch, LoadTimestamp(&epoch))
        && !ClaimWarning(&warnedEpoch, LoadTimestamp(&epoch));
}
} // namespace

int main()
{
    if (!TestCheckedAlignmentAndArenaCapacity())
        return Fail("checked alignment or exact arena capacity");
    if (!TestFailureDoesNotPoisonCursor())
        return Fail("failed reservation changed the cursor");
    if (!TestReservationContention())
        return Fail("contention produced overlap or lost capacity");
    if (!TestEpochRolloverAndWarningClaim())
        return Fail("epoch rollover or once-per-epoch warning claim");
    return 0;
}

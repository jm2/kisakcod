#include <cgame/cg_pose_atomic.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
using cg::pose_atomic::Consume;
using cg::pose_atomic::MarkCulled;
using cg::pose_atomic::MarkUsed;
using cg::pose_atomic::Peek;
using cg::pose_atomic::Reset;
using cg::pose_atomic::kCulled;
using cg::pose_atomic::kIdle;
using cg::pose_atomic::kUsed;

int Fail(const char *const message)
{
    std::fprintf(stderr, "pose atomic test failed: %s\n", message);
    return 1;
}

bool TestBasicTransitions()
{
    volatile std::uint32_t state = kIdle;
    MarkUsed(&state);
    if (Peek(&state) != kUsed)
        return false;

    MarkCulled(&state);
    MarkUsed(&state);
    if (Peek(&state) != kCulled || Consume(&state) != kCulled)
        return false;
    if (Peek(&state) != kIdle || Consume(&state) != kIdle)
        return false;

    MarkUsed(&state);
    Reset(&state);
    return Peek(&state) == kIdle;
}

bool TestConcurrentUsedClaims()
{
    constexpr std::uint32_t kThreadCount = 16u;
    volatile std::uint32_t state = kIdle;
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (std::uint32_t index = 0u; index < kThreadCount; ++index)
    {
        workers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            MarkUsed(&state);
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread &worker : workers)
        worker.join();

    return Peek(&state) == kUsed && Consume(&state) == kUsed;
}

bool TestCulledPriorityRace()
{
    for (std::uint32_t iteration = 0u; iteration < 128u; ++iteration)
    {
        volatile std::uint32_t state = kIdle;
        std::atomic<bool> start{false};
        std::thread used([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            MarkUsed(&state);
        });
        std::thread culled([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            MarkCulled(&state);
        });
        start.store(true, std::memory_order_release);
        used.join();
        culled.join();
        if (Peek(&state) != kCulled)
            return false;
    }
    return true;
}

bool TestProducerConsumerRace()
{
    for (std::uint32_t iteration = 0u; iteration < 128u; ++iteration)
    {
        volatile std::uint32_t state = kIdle;
        std::atomic<bool> start{false};
        std::uint32_t consumed = kIdle;
        std::thread producer([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            MarkCulled(&state);
        });
        std::thread consumer([&]() {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            consumed = Consume(&state);
        });
        start.store(true, std::memory_order_release);
        producer.join();
        consumer.join();

        const std::uint32_t pending = Peek(&state);
        const bool consumedRequest = consumed == kCulled && pending == kIdle;
        const bool pendingRequest = consumed == kIdle && pending == kCulled;
        if (!consumedRequest && !pendingRequest)
            return false;
    }
    return true;
}
} // namespace

int main()
{
    if (!TestBasicTransitions())
        return Fail("basic state transitions");
    if (!TestConcurrentUsedClaims())
        return Fail("concurrent used claims");
    if (!TestCulledPriorityRace())
        return Fail("culled priority race");
    if (!TestProducerConsumerRace())
        return Fail("producer/consumer publication race");
    return 0;
}

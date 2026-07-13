#include <EffectsCore/fx_runtime.h>
#include <EffectsCore/fx_visibility_atomic.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <type_traits>

static_assert(sizeof(fx::visibility::PackedBlockerScalars) == 0x4);
static_assert(sizeof(FxVisBlocker) == 0x10);
static_assert(alignof(FxVisBlocker) == 0x4);
static_assert(offsetof(FxVisState, blockerCount) == 0x1000);
static_assert(sizeof(FxVisState) == 0x1010);
static_assert(sizeof(FxSystem) == (KISAK_PTR_BITS == 32 ? 0xA60 : 0xA90));
static_assert(std::is_same_v<decltype(FxVisState::blockerCount), volatile std::int32_t>);

namespace
{
int Fail(const char *const message)
{
    std::fprintf(stderr, "FX visibility atomic test failed: %s\n", message);
    return 1;
}

bool TestAppendBoundariesAndApiShape()
{
    volatile std::int32_t count = 0;
    std::array<std::uint32_t, fx::visibility::kBlockerCapacity> payload{};
    for (std::uint32_t expected = 0;
         expected < fx::visibility::kBlockerCapacity;
         ++expected)
    {
        std::uint32_t index = UINT32_MAX;
        if (!fx::visibility::TryBeginBlockerAppend(&count, &index)
            || index != expected
            || Sys_AtomicLoad(&count) != static_cast<std::int32_t>(expected))
        {
            return false;
        }
        payload[index] = expected + 1000u;
        if (Sys_AtomicLoad(&count) != static_cast<std::int32_t>(expected)
            || !fx::visibility::PublishBlockerAppend(&count, index)
            || Sys_AtomicLoad(&count) != static_cast<std::int32_t>(expected + 1u)
            || payload[index] != expected + 1000u)
        {
            return false;
        }
    }

    std::uint32_t unchanged = 91u;
    if (fx::visibility::TryBeginBlockerAppend(&count, &unchanged)
        || Sys_AtomicLoad(&count)
            != static_cast<std::int32_t>(fx::visibility::kBlockerCapacity)
        || unchanged != 91u)
    {
        return false;
    }

    Sys_AtomicStore(&count, -1);
    if (fx::visibility::TryBeginBlockerAppend(&count, &unchanged)
        || Sys_AtomicLoad(&count) != -1
        || unchanged != 91u)
    {
        return false;
    }

    Sys_AtomicStore(
        &count,
        static_cast<std::int32_t>(fx::visibility::kBlockerCapacity + 1u));
    if (fx::visibility::TryBeginBlockerAppend(&count, &unchanged)
        || Sys_AtomicLoad(&count)
            != static_cast<std::int32_t>(fx::visibility::kBlockerCapacity + 1u)
        || unchanged != 91u)
    {
        return false;
    }

    Sys_AtomicStore(&count, 3);
    return !fx::visibility::TryBeginBlockerAppend(nullptr, &unchanged)
        && !fx::visibility::TryBeginBlockerAppend(&count, nullptr)
        && !fx::visibility::PublishBlockerAppend(nullptr, 3u)
        && !fx::visibility::PublishBlockerAppend(
            &count, fx::visibility::kBlockerCapacity)
        && !fx::visibility::PublishBlockerAppend(&count, 2u)
        && Sys_AtomicLoad(&count) == 3;
}

bool TestPayloadIsWrittenBeforePublication()
{
    constexpr std::uint32_t kRoundCount = 10000u;
    struct Payload
    {
        std::uint32_t sequence;
        std::uint32_t complement;
    } payload{};

    volatile std::int32_t count = 0;
    std::atomic<bool> failed{false};
    std::thread producer([&]() {
        for (std::uint32_t sequence = 1u; sequence <= kRoundCount; ++sequence)
        {
            while (Sys_AtomicLoad(&count) != 0)
            {
                if (failed.load(std::memory_order_relaxed))
                    return;
                std::this_thread::yield();
            }

            std::uint32_t index = UINT32_MAX;
            if (!fx::visibility::TryBeginBlockerAppend(&count, &index)
                || index != 0u)
            {
                failed.store(true, std::memory_order_relaxed);
                return;
            }

            payload.sequence = sequence;
            payload.complement = ~sequence;
            if (Sys_AtomicLoad(&count) != 0
                || !fx::visibility::PublishBlockerAppend(&count, index))
            {
                failed.store(true, std::memory_order_relaxed);
                return;
            }
        }
    });

    for (std::uint32_t expected = 1u; expected <= kRoundCount; ++expected)
    {
        while (Sys_AtomicLoad(&count) != 1)
        {
            if (failed.load(std::memory_order_relaxed))
                break;
            std::this_thread::yield();
        }
        if (failed.load(std::memory_order_relaxed)
            || payload.sequence != expected
            || payload.complement != ~expected)
        {
            failed.store(true, std::memory_order_relaxed);
            break;
        }
        Sys_AtomicStore(&count, 0);
    }

    producer.join();
    return !failed.load(std::memory_order_relaxed);
}

bool TestPackingAndInvalidInputs()
{
    fx::visibility::PackedBlockerScalars packed{12u, 34u};
    if (!fx::visibility::TryPackBlockerScalars(0.0f, 0.0f, &packed)
        || packed.radius != 0u
        || packed.visibility != UINT16_MAX)
    {
        return false;
    }
    if (!fx::visibility::TryPackBlockerScalars(1.5f, 0.5f, &packed)
        || packed.radius != 24u
        || packed.visibility != 32768u)
    {
        return false;
    }

    const float belowRadiusLimit = std::nextafter(
        fx::visibility::kMaximumRadius,
        0.0f);
    if (!fx::visibility::TryPackBlockerScalars(
            belowRadiusLimit,
            std::nextafter(1.0f, 0.0f),
            &packed)
        || packed.radius != UINT16_MAX)
    {
        return false;
    }

    const fx::visibility::PackedBlockerScalars unchanged = packed;
    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const float infinity = (std::numeric_limits<float>::infinity)();
    if (fx::visibility::TryPackBlockerScalars(-1.0f, 0.5f, &packed)
        || fx::visibility::TryPackBlockerScalars(
            fx::visibility::kMaximumRadius, 0.5f, &packed)
        || fx::visibility::TryPackBlockerScalars(nan, 0.5f, &packed)
        || fx::visibility::TryPackBlockerScalars(infinity, 0.5f, &packed)
        || fx::visibility::TryPackBlockerScalars(1.0f, -0.1f, &packed)
        || fx::visibility::TryPackBlockerScalars(1.0f, 1.0f, &packed)
        || fx::visibility::TryPackBlockerScalars(1.0f, nan, &packed)
        || fx::visibility::TryPackBlockerScalars(1.0f, infinity, &packed)
        || fx::visibility::TryPackBlockerScalars(1.0f, 0.5f, nullptr)
        || packed.radius != unchanged.radius
        || packed.visibility != unchanged.visibility)
    {
        return false;
    }

    const float finiteOrigin[3]{1.0f, -2.0f, 3.0f};
    const float invalidOrigin[3]{1.0f, nan, 3.0f};
    return fx::visibility::IsFiniteOrigin(finiteOrigin)
        && !fx::visibility::IsFiniteOrigin(invalidOrigin)
        && !fx::visibility::IsFiniteOrigin(nullptr);
}
} // namespace

int main()
{
    if (!TestAppendBoundariesAndApiShape())
        return Fail("bounded append and publication API");
    if (!TestPayloadIsWrittenBeforePublication())
        return Fail("payload publication ordering");
    if (!TestPackingAndInvalidInputs())
        return Fail("packed scalar validation");
    return 0;
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ui_safety
{
// uiInfo_s is a frozen retail-facing layout: its backing savegame array has
// 512 entries, while the display-order map has only 256. All live counts must
// therefore use the smaller capacity even though the backing storage remains
// unchanged.
inline constexpr std::size_t kSavegameCapacity = 256u;
inline constexpr std::size_t kSavegameStorageLayoutCapacity = 512u;

constexpr bool IsSavegameCountValid(const int savegameCount) noexcept
{
    return savegameCount >= 0
        && static_cast<std::size_t>(savegameCount) <= kSavegameCapacity;
}

constexpr int GetFailClosedSavegameCount(const int savegameCount) noexcept
{
    return IsSavegameCountValid(savegameCount) ? savegameCount : 0;
}

constexpr bool CanAppendSavegame(const int savegameCount) noexcept
{
    return IsSavegameCountValid(savegameCount)
        && static_cast<std::size_t>(savegameCount) < kSavegameCapacity;
}

constexpr bool TryResolveSavegameSlot(
    const int *const displaySavegames,
    const int savegameCount,
    const int displayIndex,
    int *const slotIndex) noexcept
{
    if (!slotIndex)
        return false;

    *slotIndex = -1;
    if (!displaySavegames
        || !IsSavegameCountValid(savegameCount)
        || displayIndex < 0
        || displayIndex >= savegameCount)
    {
        return false;
    }

    const int candidate = displaySavegames[displayIndex];
    if (candidate < 0
        || candidate >= savegameCount)
    {
        return false;
    }

    *slotIndex = candidate;
    return true;
}

constexpr std::uint32_t MonotonicElapsedMilliseconds(
    const int currentTime,
    const int startTime) noexcept
{
    const std::uint32_t elapsed =
        static_cast<std::uint32_t>(currentTime)
        - static_cast<std::uint32_t>(startTime);
    return elapsed
            <= static_cast<std::uint32_t>(
                (std::numeric_limits<int>::max)())
        ? elapsed
        : 0u;
}

constexpr bool InvalidCmdHintExpired(
    const int currentTime,
    const int startTime,
    const int duration) noexcept
{
    if (duration < 0)
        return true;

    return MonotonicElapsedMilliseconds(currentTime, startTime)
        > static_cast<std::uint32_t>(duration);
}

constexpr float InvalidCmdHintBlinkAlpha(
    const int currentTime,
    const int startTime,
    const int blinkInterval) noexcept
{
    if (blinkInterval <= 0)
        return 0.0f;

    const std::uint32_t phase =
        MonotonicElapsedMilliseconds(currentTime, startTime)
        % static_cast<std::uint32_t>(blinkInterval);
    return static_cast<float>(phase) / static_cast<float>(blinkInterval);
}
}

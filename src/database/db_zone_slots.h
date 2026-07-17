#pragma once

#include <cstddef>

namespace db::zone_slots
{
inline constexpr std::size_t kDefaultZoneSlot = 0;
inline constexpr std::size_t kFirstUsableZoneSlot = 1;
inline constexpr std::size_t kUsableZoneSlotCount = 32;
inline constexpr std::size_t kPhysicalZoneSlotCount = 33;

static_assert(kFirstUsableZoneSlot == kDefaultZoneSlot + 1);
static_assert(
    kPhysicalZoneSlotCount
    == kFirstUsableZoneSlot + kUsableZoneSlotCount);

constexpr bool IsUsableZoneSlot(const std::size_t slot) noexcept
{
    return slot >= kFirstUsableZoneSlot
        && slot < kPhysicalZoneSlotCount;
}
} // namespace db::zone_slots

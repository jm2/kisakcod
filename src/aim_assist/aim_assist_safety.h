#pragma once

#include <cstdint>

namespace aim_assist::safety
{
[[nodiscard]] constexpr bool IsOrdinaryEntityNumber(
    std::int32_t entityNumber, std::int32_t ordinaryEntityEnd) noexcept
{
    return entityNumber >= 0 && entityNumber < ordinaryEntityEnd;
}

[[nodiscard]] constexpr std::uint32_t BoundedWeaponIndex(
    std::uint32_t weaponIndex, std::uint32_t weaponCount) noexcept
{
    return weaponIndex < weaponCount ? weaponIndex : 0;
}
} // namespace aim_assist::safety

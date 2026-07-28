#pragma once

#include <cstdint>

namespace bg::weapon_input
{
inline constexpr std::int32_t kFriendlyFireSuppressed = 0x08;
inline constexpr std::int32_t kWeaponsDisabled = 0x80;
inline constexpr std::int32_t kAttackSuppressionMask =
    kFriendlyFireSuppressed | kWeaponsDisabled;

constexpr bool IsAttackSuppressed(const std::int32_t weaponFlags) noexcept
{
    return (weaponFlags & kAttackSuppressionMask) != 0;
}
} // namespace bg::weapon_input

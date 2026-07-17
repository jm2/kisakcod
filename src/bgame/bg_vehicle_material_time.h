#pragma once

#include <bit>
#include <cstdint>
#include <limits>

namespace bg::vehicle_material_time
{
inline constexpr std::int32_t kDisabled = -1;
inline constexpr std::int32_t kDefault = 0;

[[nodiscard]] constexpr bool IsDisabled(const std::int32_t value) noexcept
{
    return value == kDisabled;
}

[[nodiscard]] constexpr std::int32_t FromBits(
    const std::uint32_t value) noexcept
{
    return std::bit_cast<std::int32_t>(value);
}

[[nodiscard]] constexpr std::uint32_t ToBits(
    const std::int32_t value) noexcept
{
    return std::bit_cast<std::uint32_t>(value);
}

// Material time is a signed 32-bit wire value, but enabled values form a
// wrapping sequence. Perform the addition in the corresponding unsigned type
// so crossing INT32_MAX/INT32_MIN is defined. The one reserved bit pattern is
// skipped in the direction of travel so it can only mean "disabled" in a
// snapshot. Entering the enabled path from the disabled state restarts at zero.
[[nodiscard]] constexpr std::int32_t Advance(
    const std::int32_t current,
    const std::int32_t delta) noexcept
{
    const std::int32_t enabledCurrent = IsDisabled(current) ? kDefault : current;
    const std::uint32_t advancedBits = ToBits(enabledCurrent) + ToBits(delta);
    const std::int32_t advanced = FromBits(advancedBits);
    if (!IsDisabled(advanced))
        return advanced;

    return delta < 0 ? -2 : kDefault;
}

// Return the signed modular displacement from `earlier` to `later`. Snapshot
// intervals are far shorter than half of the 32-bit range, making this the
// unambiguous path through a wrap.
[[nodiscard]] constexpr std::int32_t Difference(
    const std::int32_t later,
    const std::int32_t earlier) noexcept
{
    return FromBits(ToBits(later) - ToBits(earlier));
}

[[nodiscard]] inline std::int32_t Interpolate(
    const std::int32_t current,
    const std::int32_t next,
    const float fraction) noexcept
{
    const double scaled = static_cast<double>(Difference(next, current))
        * static_cast<double>(fraction);
    const double minimum =
        static_cast<double>(std::numeric_limits<std::int32_t>::min());
    const double maximum =
        static_cast<double>(std::numeric_limits<std::int32_t>::max());

    // Normal frame interpolation is in [0, 1]. Keep malformed/extrapolated
    // values defined too, instead of relying on an out-of-range float-to-int
    // conversion.
    const std::int32_t offset = scaled != scaled
        ? 0
        : (scaled <= minimum
            ? std::numeric_limits<std::int32_t>::min()
            : (scaled >= maximum
                ? std::numeric_limits<std::int32_t>::max()
                : static_cast<std::int32_t>(scaled)));
    return FromBits(ToBits(current) + ToBits(offset));
}

// R_UpdateMaterialTime expects the interpolated tread position expressed as a
// time offset from the current scene clock. A disabled endpoint means there is
// no trustworthy interpolation interval, so use the renderer's default zero.
[[nodiscard]] inline float ForRender(
    const std::int32_t current,
    const std::int32_t next,
    const float fraction,
    const std::int32_t clientTime) noexcept
{
    if (IsDisabled(current) || IsDisabled(next))
        return 0.0f;

    const std::int32_t interpolated = Interpolate(current, next, fraction);
    return static_cast<float>(Difference(clientTime, interpolated)) * 0.001f;
}
} // namespace bg::vehicle_material_time

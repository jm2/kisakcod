#pragma once

#include <cstdint>
#include <limits>

namespace gfx::image_dimensions
{
struct MipmapResolution final
{
    std::uint16_t width;
    std::uint16_t height;
};

inline constexpr bool TryGetMipmapResolution(
    const int baseWidth,
    const int baseHeight,
    const int mipLevel,
    MipmapResolution *const out) noexcept
{
    constexpr int kMaximumDimension =
        static_cast<int>(std::numeric_limits<std::uint16_t>::max());

    if (!out
        || baseWidth < 1
        || baseWidth > kMaximumDimension
        || baseHeight < 1
        || baseHeight > kMaximumDimension
        || mipLevel < 0
        || mipLevel >= std::numeric_limits<std::uint32_t>::digits)
    {
        return false;
    }

    const std::uint32_t shiftedWidth =
        static_cast<std::uint32_t>(baseWidth) >> mipLevel;
    const std::uint32_t shiftedHeight =
        static_cast<std::uint32_t>(baseHeight) >> mipLevel;
    const MipmapResolution resolution{
        static_cast<std::uint16_t>(shiftedWidth > 0 ? shiftedWidth : 1u),
        static_cast<std::uint16_t>(shiftedHeight > 0 ? shiftedHeight : 1u),
    };
    *out = resolution;
    return true;
}
} // namespace gfx::image_dimensions

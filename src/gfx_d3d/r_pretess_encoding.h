#pragma once

#include <cstdint>

namespace gfx::pretess_encoding
{
// The render command word assigns surface and LOD one byte each and the
// static-model list the upper 16 bits. Keep validation aligned with that wire.
inline constexpr std::uint32_t kPackedByteMaximum = 0xFFu;

inline constexpr bool TryPackSurface(
    const int surfaceIndex,
    const std::uint32_t lod,
    const std::uint16_t listIndex,
    std::uint32_t *const out) noexcept
{
    if (!out
        || surfaceIndex < 0
        || static_cast<std::uint32_t>(surfaceIndex) > kPackedByteMaximum
        || lod > kPackedByteMaximum)
    {
        return false;
    }

    const std::uint32_t packed =
        static_cast<std::uint32_t>(surfaceIndex)
        | (lod << 8u)
        | (static_cast<std::uint32_t>(listIndex) << 16u);
    *out = packed;
    return true;
}
} // namespace gfx::pretess_encoding

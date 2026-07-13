#pragma once

// Fixed-width helpers for the double-buffered FX visibility-blocker protocol.
// FX_GenerateVerts is the single producer for its write buffer.  It obtains the
// current append index without changing the count, writes the complete payload,
// and only then publishes count + 1 through the full-barrier Sys_Atomic API.
// The buffer-pointer handoff remains non-atomic and must be externally ordered
// by the generate-verts worker completion boundary.  These helpers publish only
// payload bytes within the selected buffer.

#include <cmath>
#include <cstdint>
#include <limits>

#include <universal/sys_atomic.h>

namespace fx::visibility
{
inline constexpr std::uint32_t kBlockerCapacity = 256u;
inline constexpr double kRadiusScale = 16.0;
inline constexpr double kVisibilityScale = 65536.0;
inline constexpr float kMaximumRadius = 4096.0f;

struct PackedBlockerScalars
{
    std::uint16_t radius;
    std::uint16_t visibility;
};

// Failure leaves the output unchanged.  Signed storage is retained for the
// frozen runtime layout, but negative/corrupt counts are rejected before they
// can become an array index.
inline bool TryBeginBlockerAppend(
    const volatile std::int32_t *const blockerCount,
    std::uint32_t *const blockerIndex) noexcept
{
    if (!blockerCount || !blockerIndex)
        return false;

    const std::int32_t observed = Sys_AtomicLoad(blockerCount);
    if (observed < 0
        || static_cast<std::uint32_t>(observed) >= kBlockerCapacity)
    {
        return false;
    }

    *blockerIndex = static_cast<std::uint32_t>(observed);
    return true;
}

// Called by the same single producer after blocker[blockerIndex] is complete.
// The sequentially consistent store is the payload publication barrier.  A
// changed count indicates a protocol violation and is rejected rather than
// publishing a count that skips or exposes a slot.
inline bool PublishBlockerAppend(
    volatile std::int32_t *const blockerCount,
    const std::uint32_t blockerIndex) noexcept
{
    if (!blockerCount || blockerIndex >= kBlockerCapacity)
        return false;

    const std::int32_t expected = static_cast<std::int32_t>(blockerIndex);
    if (Sys_AtomicLoad(blockerCount) != expected)
        return false;

    Sys_AtomicStore(blockerCount, expected + 1);
    return true;
}

inline bool IsFiniteOrigin(const float *const origin) noexcept
{
    return origin
        && std::isfinite(origin[0])
        && std::isfinite(origin[1])
        && std::isfinite(origin[2]);
}

// The original scale maps zero opacity to 65536, one beyond uint16_t.  Saturate
// that endpoint to 65535 and reject every input that could make float-to-int
// conversion undefined or wrap through the packed fields.
inline bool TryPackBlockerScalars(
    const float radius,
    const float opacity,
    PackedBlockerScalars *const packed) noexcept
{
    if (!packed
        || !std::isfinite(radius)
        || !std::isfinite(opacity)
        || radius < 0.0f
        || radius >= kMaximumRadius
        || opacity < 0.0f
        || opacity >= 1.0f)
    {
        return false;
    }

    const double scaledRadius = static_cast<double>(radius) * kRadiusScale;
    const double scaledVisibility =
        (1.0 - static_cast<double>(opacity)) * kVisibilityScale;
    if (scaledRadius < 0.0
        || scaledRadius >= kVisibilityScale
        || scaledVisibility < 0.0)
    {
        return false;
    }

    const std::uint32_t radiusWord = static_cast<std::uint32_t>(scaledRadius);
    const double maximumPacked =
        static_cast<double>((std::numeric_limits<std::uint16_t>::max)());
    const std::uint32_t visibilityWord = scaledVisibility >= maximumPacked
        ? (std::numeric_limits<std::uint16_t>::max)()
        : static_cast<std::uint32_t>(scaledVisibility);

    packed->radius = static_cast<std::uint16_t>(radiusWord);
    packed->visibility = static_cast<std::uint16_t>(visibilityWord);
    return true;
}
} // namespace fx::visibility

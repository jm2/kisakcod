#pragma once

namespace actor_grenade_safety
{
constexpr bool IsTargetWithinSafetyRadius(
    const float *const targetPosition,
    const float *const sentientPosition,
    const int explosionRadius) noexcept
{
    // Preserve the old fail-open behavior for invalid negative weapon data.
    if (explosionRadius < 0)
        return false;

    const float deltaY = targetPosition[1] - sentientPosition[1];
    const float deltaZ = targetPosition[2] - sentientPosition[2];
    const float deltaX = targetPosition[0] - sentientPosition[0];
    const float distanceSquared =
        deltaY * deltaY + (deltaZ * deltaZ + deltaX * deltaX);
    const float safetyRadius =
        static_cast<float>(explosionRadius) * 1.1f;
    return distanceSquared <= safetyRadius * safetyRadius;
}
}

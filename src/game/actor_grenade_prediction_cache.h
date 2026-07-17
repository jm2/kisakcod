#pragma once

// A predicted position can legitimately be the world origin, so the position
// itself cannot double as the cache-validity sentinel. The engine's dedicated
// prediction time is the sole validity key.
namespace actor_grenade_prediction_cache
{
constexpr bool IsValid(const int predictLandTime) noexcept
{
    return predictLandTime != 0;
}

constexpr void Invalidate(int &predictLandTime) noexcept
{
    predictLandTime = 0;
}

inline void Store(
    float *const cachedPosition,
    int &cachedTime,
    const float *const predictedPosition,
    const int predictedTime) noexcept
{
    cachedPosition[0] = predictedPosition[0];
    cachedPosition[1] = predictedPosition[1];
    cachedPosition[2] = predictedPosition[2];
    cachedTime = predictedTime;
}
}

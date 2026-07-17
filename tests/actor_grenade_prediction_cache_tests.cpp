#include "game/actor_grenade_prediction_cache.h"

#include <cstdio>

namespace
{
int failures = 0;

void Check(const bool condition, const char *const message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void TestWorldOriginIsValid()
{
    float cachedPosition[3] = {19.0f, 23.0f, 29.0f};
    const float worldOrigin[3] = {0.0f, 0.0f, 0.0f};
    int cachedTime = 0;

    actor_grenade_prediction_cache::Store(
        cachedPosition, cachedTime, worldOrigin, 125);

    Check(
        actor_grenade_prediction_cache::IsValid(cachedTime),
        "a prediction at the world origin must remain valid");
    Check(
        cachedPosition[0] == 0.0f
            && cachedPosition[1] == 0.0f
            && cachedPosition[2] == 0.0f,
        "storing a world-origin prediction must copy every component");
}

void TestInvalidationUsesOnlyTheTimeSentinel()
{
    float cachedPosition[3] = {3.0f, -4.0f, 5.0f};
    int cachedTime = -7;

    Check(
        actor_grenade_prediction_cache::IsValid(cachedTime),
        "every nonzero engine prediction time is valid");
    actor_grenade_prediction_cache::Invalidate(cachedTime);
    Check(
        !actor_grenade_prediction_cache::IsValid(cachedTime),
        "invalidation must clear the dedicated time sentinel");
    Check(
        cachedPosition[0] == 3.0f
            && cachedPosition[1] == -4.0f
            && cachedPosition[2] == 5.0f,
        "invalidation need not destroy the cached position");
}

void TestZeroTimePublishesAnInvalidCache()
{
    float cachedPosition[3] = {};
    const float prediction[3] = {7.0f, 11.0f, 13.0f};
    int cachedTime = 41;

    actor_grenade_prediction_cache::Store(
        cachedPosition, cachedTime, prediction, 0);

    Check(
        !actor_grenade_prediction_cache::IsValid(cachedTime),
        "zero remains the only invalid prediction-time sentinel");
    Check(
        cachedPosition[0] == 7.0f
            && cachedPosition[1] == 11.0f
            && cachedPosition[2] == 13.0f,
        "store must copy a prediction before publishing its validity time");
}
}

int main()
{
    TestWorldOriginIsValid();
    TestInvalidationUsesOnlyTheTimeSentinel();
    TestZeroTimePublishesAnInvalidCache();
    return failures == 0 ? 0 : 1;
}

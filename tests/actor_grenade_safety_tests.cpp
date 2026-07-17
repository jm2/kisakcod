#include "game/actor_grenade_safety.h"

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

void TestSquaredDistanceUsesSquaredExplosionRadius()
{
    const float target[3] = {10.0f, -20.0f, 30.0f};
    const float sentient[3] = {110.0f, -20.0f, 30.0f};

    Check(
        actor_grenade_safety::IsTargetWithinSafetyRadius(
            target, sentient, 100),
        "a target 100 units away must be inside a 100-unit blast radius");
}

void TestTenPercentSafetyMargin()
{
    const float target[3] = {10.0f, -20.0f, 30.0f};
    const float sentient[3] = {115.0f, -20.0f, 30.0f};

    Check(
        actor_grenade_safety::IsTargetWithinSafetyRadius(
            target, sentient, 100),
        "the safety radius must include the configured ten-percent margin");
}

void TestInclusiveThreeDimensionalBoundary()
{
    const float target[3] = {10.0f, -20.0f, 30.0f};
    const float onBoundary[3] = {10.0f, 46.0f, 118.0f};
    const float outsideOnZ[3] = {10.0f, -20.0f, 141.0f};

    Check(
        actor_grenade_safety::IsTargetWithinSafetyRadius(
            target, onBoundary, 100),
        "the exact 110-unit three-dimensional boundary must be unsafe");
    Check(
        !actor_grenade_safety::IsTargetWithinSafetyRadius(
            target, outsideOnZ, 100),
        "a target beyond the safety radius on Z must remain safe");
}

void TestZeroAndInvalidRadii()
{
    const float target[3] = {10.0f, -20.0f, 30.0f};
    const float coincident[3] = {10.0f, -20.0f, 30.0f};
    const float separated[3] = {11.0f, -20.0f, 30.0f};

    Check(
        actor_grenade_safety::IsTargetWithinSafetyRadius(
            target, coincident, 0),
        "zero radius must retain its inclusive coincident boundary");
    Check(
        !actor_grenade_safety::IsTargetWithinSafetyRadius(
            target, separated, 0),
        "zero radius must not cover a separated target");
    Check(
        !actor_grenade_safety::IsTargetWithinSafetyRadius(
            target, coincident, -1),
        "invalid negative weapon radii must preserve fail-open behavior");
}
}

int main()
{
    TestSquaredDistanceUsesSquaredExplosionRadius();
    TestTenPercentSafetyMargin();
    TestInclusiveThreeDimensionalBoundary();
    TestZeroAndInvalidRadii();
    return failures == 0 ? 0 : 1;
}

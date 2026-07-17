#include <bgame/bg_vehicle_material_time.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
using bg::vehicle_material_time::Advance;
using bg::vehicle_material_time::Difference;
using bg::vehicle_material_time::ForRender;
using bg::vehicle_material_time::Interpolate;
using bg::vehicle_material_time::IsDisabled;
using bg::vehicle_material_time::kDefault;
using bg::vehicle_material_time::kDisabled;

int Fail(const char *const message)
{
    std::fprintf(stderr, "vehicle material-time test failed: %s\n", message);
    return 1;
}

bool NearlyEqual(const float lhs, const float rhs)
{
    return std::fabs(lhs - rhs) <= 0.000001f;
}

bool TestSentinelAndZero()
{
    return IsDisabled(kDisabled)
        && !IsDisabled(-2)
        && !IsDisabled(kDefault)
        && Advance(kDisabled, 0) == 0
        && Advance(kDisabled, 7) == 7
        && Advance(0, 0) == 0;
}

bool TestAdvanceAndReservedValue()
{
    constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();

    return Advance(10, 5) == 15
        && Advance(10, -5) == 5
        && Advance(kMax, 1) == kMin
        && Advance(kMin, -1) == kMax
        && Advance(-2, 1) == 0
        && Advance(-3, 2) == 0
        && Advance(0, -1) == -2
        && Advance(1, -2) == -2;
}

bool TestDifferenceAndInterpolation()
{
    constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();
    constexpr std::int32_t kForwardCurrent = kMax - 9;
    constexpr std::int32_t kForwardNext = kMin + 10;
    constexpr std::int32_t kReverseCurrent = kMin + 10;
    constexpr std::int32_t kReverseNext = kMax - 9;

    return Difference(kForwardNext, kForwardCurrent) == 20
        && Difference(kReverseNext, kReverseCurrent) == -20
        && Interpolate(100, 200, 0.0f) == 100
        && Interpolate(100, 200, 0.5f) == 150
        && Interpolate(100, 200, 1.0f) == 200
        && Interpolate(200, 100, 0.5f) == 150
        && Interpolate(kForwardCurrent, kForwardNext, 0.5f) == kMin
        && Interpolate(kReverseCurrent, kReverseNext, 0.5f) == kMin
        && Interpolate(-2, 0, 0.5f) == -1;
}

bool TestRenderConversion()
{
    constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
    constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();

    return NearlyEqual(ForRender(kDisabled, 20, 0.5f, 1000), 0.0f)
        && NearlyEqual(ForRender(20, kDisabled, 0.5f, 1000), 0.0f)
        && NearlyEqual(ForRender(0, 0, 0.5f, 100), 0.1f)
        && NearlyEqual(ForRender(900, 1100, 0.5f, 1200), 0.2f)
        && NearlyEqual(ForRender(-100, -200, 0.5f, 0), 0.15f)
        && NearlyEqual(
            ForRender(kMax - 9, kMin + 10, 0.5f, kMin + 100),
            0.1f);
}
} // namespace

int main()
{
    if (!TestSentinelAndZero())
        return Fail("only -1 is disabled and zero remains enabled");
    if (!TestAdvanceAndReservedValue())
        return Fail("forward/reverse advancement or reserved-value skip");
    if (!TestDifferenceAndInterpolation())
        return Fail("modular difference or interpolation boundaries");
    if (!TestRenderConversion())
        return Fail("render conversion, sentinel endpoint, or wrap handling");
    return 0;
}

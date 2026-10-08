// fx_curve_random_tests.cpp: the production FX curve and random-table code
// (EffectsCore/fxcurve.cpp, EffectsCore/fx_random.cpp) at native width. A curve
// is built the way fx_load_obj.cpp builds one from an .efx file, then sampled
// the way fx_convert.cpp samples it; FX_RandomDir and FX_RandomlyRotateAxis
// must give unit vectors and an orthonormal frame for every table seed. The
// engine's MyAssertHandler aborts here (com_math_test_stubs.cpp), so any
// engine assert the code trips fails the test.

#include <EffectsCore/fx_system.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
int g_failures = 0;
std::vector<void *> g_hunk;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

bool Near(const double a, const double b)
{
    return std::fabs(a - b) < 1e-4;
}

double Dot(const float *const a, const float *const b)
{
    return static_cast<double>(a[0]) * b[0] + static_cast<double>(a[1]) * b[1]
        + static_cast<double>(a[2]) * b[2];
}

void TestCurve1D()
{
    float keys[] = {0.0f, 0.0f, 0.5f, 10.0f, 1.0f, 20.0f};
    const FxCurve *const curve = FxCurve_AllocAndCreateWithKeys(keys, 1, 3);
    Check(curve->dimensionCount == 1 && curve->keyCount == 3, "1d: header");
    Check(std::memcmp(curve->keys, keys, sizeof(keys)) == 0, "1d: keys copied");

    FxCurveIterator iter{};
    FxCurveIterator_Create(&iter, curve);
    const float times[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.1f, 0.9f};
    const double expected[] = {0.0, 5.0, 10.0, 15.0, 20.0, 2.0, 18.0};
    for (std::size_t i = 0; i < sizeof(times) / sizeof(times[0]); ++i)
        Check(Near(FxCurveIterator_SampleTime(&iter, times[i]), expected[i]), "1d: sample");
    FxCurveIterator_Release(&iter);
    Check(iter.master == nullptr, "1d: release clears the master");
}

void TestCurve3D()
{
    float keys[] = {0.0f, 1.0f, 2.0f, 3.0f, 1.0f, 3.0f, 6.0f, 9.0f};
    const FxCurve *const curve = FxCurve_AllocAndCreateWithKeys(keys, 3, 2);
    FxCurveIterator iter{};
    FxCurveIterator_Create(&iter, curve);
    float value[3] = {};
    FxCurveIterator_SampleTimeVec3(&iter, value, 0.5f);
    Check(Near(value[0], 2.0) && Near(value[1], 4.0) && Near(value[2], 6.0), "3d: midpoint");
    FxCurveIterator_SampleTimeVec3(&iter, value, 1.0f);
    Check(Near(value[0], 3.0) && Near(value[1], 6.0) && Near(value[2], 9.0), "3d: end");
    FxCurveIterator_Release(&iter);
}

void TestRandom()
{
    // 0x7EC bytes of floats; FX_RandomDir reads [seed + 10], the rotation [seed + 24].
    constexpr int kTableFloats = 0x7EC / 4;
    for (int seed = 0; seed + 24 < kTableFloats; ++seed)
    {
        float dir[3] = {};
        FX_RandomDir(seed, dir);
        Check(Near(Dot(dir, dir), 1.0), "random dir: unit length");
        Check(Near(dir[2], fx_randomTable[seed + 10] * 2.0 - 1.0), "random dir: height from the table");

        const float axisIn[3][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
        mat3x3 axisOut{};
        FX_RandomlyRotateAxis(axisIn, seed, axisOut);
        Check(Near(axisOut[0][0], 1.0) && Near(axisOut[0][1], 0.0) && Near(axisOut[0][2], 0.0),
            "rotate: forward kept");
        Check(Near(Dot(axisOut[1], axisOut[1]), 1.0) && Near(Dot(axisOut[2], axisOut[2]), 1.0),
            "rotate: unit axes");
        Check(Near(Dot(axisOut[0], axisOut[1]), 0.0) && Near(Dot(axisOut[0], axisOut[2]), 0.0)
                && Near(Dot(axisOut[1], axisOut[2]), 0.0),
            "rotate: orthogonal axes");
    }
}
} // namespace

// The hunk the curve allocator draws from.
uint8_t *__cdecl Hunk_AllocAlign(uint32_t size, int alignment, const char *, int)
{
    // aligned_alloc rejects an alignment below the platform's (macOS).
    const std::size_t align = std::max(static_cast<std::size_t>(alignment), alignof(std::max_align_t));
    void *const block = std::aligned_alloc(align, (static_cast<std::size_t>(size) + align - 1) / align * align);
    g_hunk.push_back(block);
    return static_cast<uint8_t *>(block);
}

int main()
{
    TestCurve1D();
    TestCurve3D();
    TestRandom();
    for (void *const block : g_hunk)
        std::free(block);
    if (g_failures == 0)
        std::puts("fx curve and random contracts passed");
    return g_failures == 0 ? 0 : 1;
}

// aim_assist_clip_bounds_tests.cpp: AimAssist_ConvertToClipBounds (production
// aim_assist/aim_assist.cpp). It takes a box as float (*)[3] over [2][3] (min,
// max) and writes the clip-space box the same way; the decompiled code reached
// the max row as (*bounds)[3..5], past the min row's three floats.

#include <aim_assist/aim_assist.h>

#include <cmath>
#include <cstdio>

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

bool Near(const float a, const float b)
{
    return std::fabs(a - b) < 1e-5f;
}

AimAssistGlobals g_aa;
}

int main()
{
    // Clip space = world space, w = 1 (the y flip is symmetric for this box).
    for (int r = 0; r < 4; ++r)
        g_aa.screenMtx[r][r] = 1.0f;
    const mat4x3 identity = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
    const float bounds[2][3] = {{-0.5f, -0.25f, 0.2f}, {0.5f, 0.25f, 0.6f}};
    float clip[2][3] = {};
    const char inside = AimAssist_ConvertToClipBounds(&g_aa, bounds, identity, clip);

    Check(inside == 1, "a box in front of the eye converts");
    Check(Near(clip[0][0], -0.5f) && Near(clip[0][1], -0.25f) && Near(clip[0][2], 0.2f), "clip min row");
    Check(Near(clip[1][0], 0.5f) && Near(clip[1][1], 0.25f) && Near(clip[1][2], 0.6f), "clip max row");
    if (g_failures)
        return 1;
    std::puts("aim_assist clip bounds: ok");
    return 0;
}

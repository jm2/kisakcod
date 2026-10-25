// r_dpvs_winding_tests.cpp: the portal winding helpers of the production
// gfx_d3d/r_dpvs.cpp (R_ChopPortalWinding, R_FurthestPointOnWinding,
// R_NearestPointOnWinding). Windings are float (*)[3]; the decompiled code
// reached vertex i as (*winding)[3 * i], indexing past vertex 0's three floats.

#include <gfx_d3d/r_dpvs.h>

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
    return std::fabs(a - b) < 1e-3f;
}

// Unit square in z = 0, counter-clockwise.
const float kSquare[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
float g_out[16][3];

DpvsPlane Plane(const float a, const float b, const float c, const float d)
{
    DpvsPlane plane{};
    plane.coeffs[0] = a;
    plane.coeffs[1] = b;
    plane.coeffs[2] = c;
    plane.coeffs[3] = d;
    return plane;
}

// Keep x >= 0.5: half the square, with two new vertices on the cut.
void TestChopKeepsFrontHalf()
{
    const DpvsPlane plane = Plane(1, 0, 0, -0.5f);
    int count = 4;
    const float (*const out)[3] = R_ChopPortalWinding(kSquare, &count, &plane, g_out);
    Check(count == 4, "chopped square keeps four vertices");
    float minX = 1e9f;
    float maxX = -1e9f;
    for (int i = 0; i < count; ++i)
    {
        minX = std::fmin(minX, out[i][0]);
        maxX = std::fmax(maxX, out[i][0]);
        Check(Near(out[i][2], 0.0f), "chopped vertex stays in the plane z = 0");
    }
    Check(Near(minX, 0.5f) && Near(maxX, 1.0f), "chopped square spans x 0.5..1");
}

void TestFurthestAndNearest()
{
    const DpvsPlane plane = Plane(1, 1, 0, 0);
    Check(Near(static_cast<float>(R_FurthestPointOnWinding(kSquare, 4, &plane)), 2.0f), "furthest point is (1,1)");
    Check(Near(static_cast<float>(R_NearestPointOnWinding(kSquare, 4, &plane)), 0.0f), "nearest point is (0,0)");
}
}

int main()
{
    TestChopKeepsFrontHalf();
    TestFurthestAndNearest();
    if (g_failures)
        return 1;
    std::puts("r_dpvs winding: ok");
    return 0;
}

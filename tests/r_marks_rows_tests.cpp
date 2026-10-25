// r_marks_rows_tests.cpp: mark helpers of the production gfx_d3d/r_marks.cpp.
// Each takes an axis, matrix or plane list as a pointer to its first row
// (float (*)[3] or float (*)[4]); the decompiled code reached later rows as
// (*p)[3 * i], (*p)[4 * i] or (*p)[9..11], indexing past row 0.

#include <gfx_d3d/r_marks.h>
#include <xanim/dobj.h>

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

const float kAxis[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
const float kOrigin[3] = {10, 20, 30};

void TestBoundsAndPlanes()
{
    float mins[3];
    float maxs[3];
    R_GetMarkFragmentBounds(kOrigin, kAxis, 2.0f, mins, maxs);
    Check(Near(mins[2], 28.0f) && Near(maxs[2], 32.0f), "bounds read the z axis row");

    float planes[6][4] = {};
    R_GetMarkFragmentClipPlanes(kOrigin, kAxis, 2.0f, planes);
    Check(Near(planes[4][2], 1.0f) && Near(planes[4][3], 28.0f), "plane 4 is +z");
    Check(Near(planes[5][2], -1.0f) && Near(planes[5][3], -32.0f), "plane 5 is -z");
}

void TestTransformPlanes()
{
    const float in[6][4] = {{1, 0, 0, 5}, {0, 0, 1, 7}};
    float matrix[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 2}};
    float out[6][4] = {};
    R_Mark_TransformClipPlanes(in, matrix, out);
    Check(Near(out[1][2], 1.0f) && Near(out[1][3], 5.0f), "plane 1 goes through all four matrix rows");
}

void TestSkelMatToMatrix43()
{
    DObjSkelMat skel{};
    skel.axis[0][0] = 1.0f;
    skel.axis[1][1] = 1.0f;
    skel.axis[2][2] = 1.0f;
    skel.origin[0] = 3.0f;
    skel.origin[1] = 4.0f;
    skel.origin[2] = 5.0f;
    float out[4][3] = {};
    DObjSkelMatToMatrix43(&skel, out);
    Check(Near(out[2][2], 1.0f) && Near(out[3][0], 3.0f) && Near(out[3][2], 5.0f), "origin lands in row 3");
}
}

int main()
{
    TestBoundsAndPlanes();
    TestTransformPlanes();
    TestSkelMatToMatrix43();
    if (g_failures)
        return 1;
    std::puts("r_marks rows: ok");
    return 0;
}

// com_math_matrix44_tests.cpp: MatrixInverse44, InfinitePerspectiveMatrix and
// FinitePerspectiveMatrix (production universal/com_math.cpp). Each writes a
// 4x4 matrix; the decompiled code reached its rows as (*mtx)[0..15] or
// dst[0][0..15], past row 0's four floats.

#include <cmath>
#include <cstdio>

#include <universal/com_math.h>

namespace
{
int g_failures = 0;
void Check(bool ok, const char *what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}
bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-5f;
}
bool Same(const mat4x4 &m, const mat4x4 &want)
{
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            if (!Near(m[r][c], want[r][c]))
                return false;
        }
    }
    return true;
}

void TestInverse()
{
    // diag(2, 4, 5, 1) with a translation row: its inverse is known.
    const mat4x4 src = {{2, 0, 0, 0}, {0, 4, 0, 0}, {0, 0, 5, 0}, {6, 8, 10, 1}};
    const mat4x4 want = {{0.5f, 0, 0, 0}, {0, 0.25f, 0, 0}, {0, 0, 0.2f, 0}, {-3, -2, -2, 1}};
    mat4x4 inv{};
    MatrixInverse44(src, inv);
    Check(Same(inv, want), "MatrixInverse44 inverts every row");
}

void TestFinitePerspective()
{
    const mat4x4 want = {{1, 0, 0, 0}, {0, 2, 0, 0}, {0, 0, 1.5f, 1}, {0, 0, -1.5f, 0}};
    mat4x4 fin{};
    FinitePerspectiveMatrix(fin, 1.0f, 0.5f, 1.0f, 3.0f);
    Check(Same(fin, want), "FinitePerspectiveMatrix writes rows 0-3");
}

void TestInfinitePerspective()
{
    mat4x4 inf{};
    InfinitePerspectiveMatrix(inf, 1.0f, 0.5f, 2.0f);
    const float s = inf[0][0];
    const mat4x4 want = {{s, 0, 0, 0}, {0, 2 * s, 0, 0}, {0, 0, inf[2][2], 1}, {0, 0, -2.0f * inf[2][2], 0}};
    Check(s > 0.0f && Same(inf, want), "InfinitePerspectiveMatrix writes rows 0-3");
}
} // namespace

int main()
{
    TestInverse();
    TestFinitePerspective();
    TestInfinitePerspective();
    if (g_failures == 0)
        std::puts("matrix44: all checks passed");
    return g_failures ? 1 : 0;
}

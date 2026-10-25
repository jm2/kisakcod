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
bool IsOnly(const mat4x4 &m, int r0, int c0, float v0, int r1, int c1, float v1, int r2, int c2, float v2, int r3,
    int c3, float v3, int r4, int c4, float v4)
{
    const int rs[] = {r0, r1, r2, r3, r4};
    const int cs[] = {c0, c1, c2, c3, c4};
    const float vs[] = {v0, v1, v2, v3, v4};
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            float want = 0.0f;
            for (int k = 0; k < 5; ++k)
            {
                if (rs[k] == r && cs[k] == c)
                    want = vs[k];
            }
            if (!Near(m[r][c], want))
                return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    // diag(2, 4, 5, 1) with a translation row: its inverse is known.
    const mat4x4 src = {{2, 0, 0, 0}, {0, 4, 0, 0}, {0, 0, 5, 0}, {6, 8, 10, 1}};
    mat4x4 inv{};
    MatrixInverse44(src, inv);
    const mat4x4 want = {{0.5f, 0, 0, 0}, {0, 0.25f, 0, 0}, {0, 0, 0.2f, 0}, {-3, -2, -2, 1}};
    bool same = true;
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
            same = same && Near(inv[r][c], want[r][c]);
    }
    Check(same, "MatrixInverse44 inverts every row");

    mat4x4 fin{};
    FinitePerspectiveMatrix(fin, 1.0f, 0.5f, 1.0f, 3.0f);
    Check(IsOnly(fin, 0, 0, 1.0f, 1, 1, 2.0f, 2, 2, 1.5f, 2, 3, 1.0f, 3, 2, -1.5f),
        "FinitePerspectiveMatrix writes rows 0-3");

    mat4x4 inf{};
    InfinitePerspectiveMatrix(inf, 1.0f, 0.5f, 2.0f);
    Check(Near(inf[1][1] * 0.5f, inf[0][0]) && Near(inf[2][3], 1.0f) && Near(inf[3][2], -2.0f * inf[2][2])
            && inf[0][0] > 0.0f && Near(inf[0][1], 0.0f) && Near(inf[3][3], 0.0f),
        "InfinitePerspectiveMatrix writes rows 0-3");

    if (g_failures == 0)
        std::puts("matrix44: all checks passed");
    return g_failures ? 1 : 0;
}

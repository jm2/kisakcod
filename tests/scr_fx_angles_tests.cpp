// scr_fx_angles_tests.cpp: Scr_SetFxAngles (production g_scr_main_mp.cpp),
// which turns playFx's forward and up vectors into angles. axis is three
// rows of three floats: the decompiled code reached the up row as
// (*axis)[6] and the derived row as (*axis)[3], past row 0's three floats.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <game_mp/g_public_mp.h>

void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file ? file : "?", line, fmt ? fmt : "");
    std::exit(3);
}
int g_scrErrors = 0;
void __cdecl Scr_Error(const char *) { ++g_scrErrors; }
char *__cdecl va(const char *format, ...) { return const_cast<char *>(format); }

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
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
} // namespace

int main()
{
    // Forward +y with a tilted up vector: up is made orthogonal to forward,
    // the left row is their cross product, and the angles face yaw 90.
    float axis[3][3] = {{0, 1, 0}, {9, 9, 9}, {0, 0.5f, 1}};
    float angles[3] = {};
    Scr_SetFxAngles(2, axis, angles);
    Check(Near(axis[2][0], 0) && Near(axis[2][1], 0) && Near(axis[2][2], 1), "up is orthonormalised against forward");
    Check(Near(axis[1][0], -1) && Near(axis[1][1], 0) && Near(axis[1][2], 0), "left is up x forward");
    Check(Near(angles[0], 0) && Near(angles[1], 90) && Near(angles[2], 0), "the angles face forward with up up");
    Check(!g_scrErrors, "independent vectors raise no script error");

    float parallel[3][3] = {{0, 0, 1}, {}, {0, 0, 2}};
    Scr_SetFxAngles(2, parallel, angles);
    Check(g_scrErrors == 1, "parallel forward and up raise a script error");

    if (!g_failures)
        std::printf("fx angles: all checks passed\n");
    return g_failures ? 1 : 0;
}

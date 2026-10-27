// cubemap_shot_tests.cpp: the six cubemapShot face suffixes. The production
// helper is the same one CL_CubemapShot_f uses to name env/%s%s.tga.
#include <cstdio>
#include <cstring>

#include <client/client.h>
#include <client_mp/client_mp.h>
#include <gfx_d3d/r_reflection_probe.h>

#define WEAK __attribute__((weak))

WEAK const char *__cdecl CL_CubemapShotFaceSuffix(CubemapShot shot);

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

int main()
{
    const char *const expected[6] = {"_rt", "_lf", "_bk", "_ft", "_up", "_dn"};
    const auto suffix = &CL_CubemapShotFaceSuffix;
    if (!suffix)
    {
        std::fputs("CL_CubemapShotFaceSuffix is missing\n", stderr);
        return 1;
    }
    for (int shot = CUBEMAPSHOT_RIGHT; shot < CUBEMAPSHOT_COUNT; ++shot)
        CHECK(std::strcmp(suffix(static_cast<CubemapShot>(shot)), expected[shot - 1]) == 0);
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("cubemap shot faces: ok");
    return 0;
}

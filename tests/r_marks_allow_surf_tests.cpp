// r_marks_allow_surf_tests.cpp: the world-mark surface filter of the production
// gfx_d3d/r_marks.cpp. R_AABBTreeSurfaces_r calls allowSurf as
// int (*)(int, void *); the decompiled table held R_AllowMarks, which returns
// bool and takes a const Material *, cast to that type. Calling through the
// cast is undefined (UBSan -fsanitize=function), and on x64 and arm64 the
// upper bits of a bool return are unspecified.

#include <gfx_d3d/r_marks.h>
#include <gfx_d3d/r_init.h>

#include <cstdio>

extern int(__cdecl *allowSurf[1])(int, void *);

r_global_permanent_t rgp;

namespace
{
GfxWorld g_world;
GfxSurface g_surfaces[2];
Material g_receiver;
Material g_noMarks;
Material g_mark;
}

int main()
{
    g_receiver.info.surfaceTypeBits = 0x6;
    g_noMarks.info.surfaceTypeBits = 0x6;
    g_noMarks.info.gameFlags = 4; // no marks on this surface
    g_mark.info.surfaceTypeBits = 0x2;
    g_surfaces[0].material = &g_receiver;
    g_surfaces[1].material = &g_noMarks;
    g_world.dpvs.surfaces = g_surfaces;
    rgp.world = &g_world;

    const int allowed = allowSurf[0](0, &g_mark);
    const int refused = allowSurf[0](1, &g_mark);
    if (allowed != 1 || refused != 0)
    {
        std::fprintf(stderr, "FAIL allowSurf: receiver %d, no-marks surface %d\n", allowed, refused);
        return 1;
    }
    std::puts("r_marks allowSurf: ok");
    return 0;
}

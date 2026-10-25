// fx_beam_tests.cpp: FX_Beam_GenerateVerts (production
// EffectsCore/fx_beam.cpp) on a beam with no segments. A tracer shorter than
// cg_tracerScrewDist / 8 gets segmentCount 0, so the colour lerp's alpha is
// segIter / 0: NaN, then inf. x86's float-to-int cast gives INT_MIN there, and
// the clamp makes every lerped colour 0; the cast itself is undefined
// behaviour (UBSan float-cast-overflow), so the engine must not perform it.

#include <EffectsCore/fx_system.h>
#include <cgame_mp/cg_local_mp.h>
#include <gfx_d3d/r_scene.h>
#include <gfx_d3d/r_drawsurf.h>
#include <gfx_d3d/r_gfx.h>

#include <cstdio>
#include <cstring>

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

r_double_index_t g_indices[64];
GfxPackedVertex g_verts[64];
float g_args[8][4];
FxBeamInfo g_info;
}

cg_s cgArray[1];

uint32_t __cdecl R_GetLocalClientNum()
{
    return 0;
}

char __cdecl R_ReserveCodeMeshIndices(int, r_double_index_t **indicesOut)
{
    *indicesOut = g_indices;
    return 1;
}

char __cdecl R_ReserveCodeMeshVerts(int, uint16_t *baseVertex)
{
    *baseVertex = 0;
    return 1;
}

char __cdecl R_ReserveCodeMeshArgs(int, uint32_t *argOffsetOut)
{
    *argOffsetOut = 0;
    return 1;
}

void __cdecl R_AddCodeMeshDrawSurf(Material *, r_double_index_t *, uint32_t, uint32_t, uint32_t, const char *)
{
}

float (*__cdecl R_GetCodeMeshArgs(uint32_t))[4]
{
    return g_args;
}

GfxPackedVertex *__cdecl R_GetCodeMeshVerts(uint16_t)
{
    return g_verts;
}

int main()
{
    cgArray[0].refdef.tanHalfFovX = 1.0f;
    cgArray[0].refdef.tanHalfFovY = 1.0f;

    // Camera at the origin looking down +x; the beam crosses in front of it.
    FxBeam &beam = g_info.beams[0];
    beam.begin[0] = 100.0f;
    beam.begin[1] = -1.0f;
    beam.end[0] = 100.0f;
    beam.end[1] = 1.0f;
    beam.beginColor.packed = 0xFFFFFFFFu;
    beam.endColor.packed = 0xFF000000u;
    beam.beginRadius = 1.0f;
    beam.endRadius = 1.0f;
    beam.segmentCount = 0;
    g_info.beamCount = 1;

    FxGenerateVertsCmd cmd{};
    cmd.beamInfo = &g_info;
    cmd.viewaxis[0][0] = 1.0f;
    cmd.viewaxis[1][1] = 1.0f;
    cmd.viewaxis[2][2] = 1.0f;
    std::memset(g_verts, 0xAB, sizeof(g_verts));
    FX_Beam_GenerateVerts(&cmd);

    // Vertex 0 is the begin cap; 1 and 2 are segment 0's pair, lerped.
    Check(g_verts[0].color.packed == 0xFFFFFFFFu, "begin cap keeps the begin colour");
    Check(g_verts[1].color.packed == 0 && g_verts[2].color.packed == 0, "zero-segment lerp gives x86's colour 0");
    if (g_failures)
        return 1;
    std::puts("fx_beam: ok");
    return 0;
}

// rb_rotate_st_tests.cpp: RB_StretchPicRotateSTCmd (production
// gfx_d3d/rb_backend.cpp), the rotated-texture HUD quad. When the quad does not
// fit, RB_CheckTessOverflow flushes and resets the tess counts; the decompiled
// command read the counts before that check, so after a flush it wrote the
// quad past tess.verts and left the new batch's indices unset.

#include <gfx_d3d/rb_backend.h>
#include <gfx_d3d/rb_state.h>
#include <gfx_d3d/r_rendercmds.h>

#include <cstdio>

GfxCmdBufSourceState gfxCmdBufSourceState;
GfxCmdBufState gfxCmdBufState;

// The flush a full tess batch triggers: draw it (nothing to draw here) and reset.
void __cdecl RB_TessOverflow()
{
    tess.vertexCount = 0;
    tess.indexCount = 0;
}

void __cdecl RB_SetTessTechnique(const Material *, MaterialTechniqueType)
{
}

void __cdecl R_TrackPrims(GfxCmdBufState *, GfxPrimStatsTarget)
{
}

namespace
{
GfxCmdStretchPicRotateST g_cmd;
}

int main()
{
    gfxCmdBufSourceState.viewMode = VIEW_MODE_2D;
    tess.vertexCount = 5448; // 4 more do not fit in 5450
    tess.indexCount = 12;
    g_cmd.header.byteCount = sizeof(g_cmd);
    g_cmd.w = 10.0f;
    g_cmd.h = 10.0f;
    g_cmd.radiusST = 1.0f;
    g_cmd.scaleFinalS = 1.0f;
    g_cmd.scaleFinalT = 1.0f;
    GfxRenderCommandExecState execState{&g_cmd};
    RB_StretchPicRotateSTCmd(&execState);

    const bool counts = tess.vertexCount == 4 && tess.indexCount == 6;
    const bool indices = tess.indices[0] == 3 && tess.indices[1] == 0 && tess.indices[2] == 2
        && tess.indices[3] == 2 && tess.indices[4] == 0 && tess.indices[5] == 1;
    if (!counts || !indices)
    {
        std::fputs("FAIL the quad after a flush starts the new batch at vertex 0\n", stderr);
        return 1;
    }
    std::puts("rb_backend rotate ST: ok");
    return 0;
}

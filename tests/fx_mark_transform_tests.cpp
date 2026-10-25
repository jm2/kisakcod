// fx_mark_transform_tests.cpp: FX_ExpandMarkVerts_Transform_GfxWorldVertex_
// (production EffectsCore/fx_marks.cpp), the vertex expander for marks on
// moving brush models. The decompiled code copied one byte of the mark's
// 4-byte colour and one of the two lightmap coordinates; the non-transform
// expander copies both whole.

#include <EffectsCore/fx_system.h>
#include <gfx_d3d/r_drawsurf.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

void __cdecl FX_ExpandMarkVerts_Transform_GfxWorldVertex_(
    FxMarksSystem *marksSystem,
    const FxMark *mark,
    uint16_t baseVertex,
    const float (*matrixTransform)[3]);

namespace
{
GfxWorldVertex g_verts[4];
FxMarksSystem g_marks;
FxMark g_mark;
}

GfxWorldVertex *__cdecl R_GetMarkMeshVerts(uint16_t)
{
    return g_verts;
}

int main()
{
    FxMarkPoint &point = g_marks.pointGroups[0].pointGroup.points[0];
    point.xyz[0] = 1.0f;
    point.lmapCoord[0] = 0.25f;
    point.lmapCoord[1] = 0.75f;
    point.normal[2] = 1.0f;
    g_marks.pointGroups[0].pointGroup.next = 0xFFFF; // the only group
    g_mark.radius = 1.0f;
    g_mark.texCoordAxis[0] = 1.0f;
    const uint8_t colour[4] = {0x11, 0x22, 0x33, 0x44};
    std::copy_n(colour, 4, g_mark.nativeColor);
    g_mark.points = 0;
    g_mark.pointCount = 1;
    const float identity[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
    FX_ExpandMarkVerts_Transform_GfxWorldVertex_(&g_marks, &g_mark, 0, identity);

    const bool colourOk = std::equal(colour, colour + 4, g_verts[0].color.array);
    const bool lmapOk = g_verts[0].lmapCoord[0] == 0.25f && g_verts[0].lmapCoord[1] == 0.75f;
    if (!colourOk || !lmapOk)
    {
        std::fprintf(stderr, "FAIL transform mark vertex: colour %s, lightmap %s\n", colourOk ? "ok" : "wrong",
            lmapOk ? "ok" : "wrong");
        return 1;
    }
    std::puts("fx transform mark verts: ok");
    return 0;
}

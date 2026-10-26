// fx_mark_convert_tests.cpp: the two marked-type contracts in
// EffectsCore/fx_marks.cpp that issue #837 fixes.
//
// FX_GenerateMarkVertsForMark_MatrixFromPlacement takes float (*)[3] but the
// decompiled code wrote (*outTransform)[9..11]; those are row 3 written
// through a float[3] index, which is out of bounds for the declared type.
// FX_ExpandMarkVerts_NoTransform_GfxWorldVertex_ copied the two lightmap
// coordinates as one 8-byte load/store from a 4-byte-aligned FxMarkPoint
// field; it now copies two floats.
//
// The value checks below pass either way; the sanitizer is what separates the
// two spellings, so this target is meaningful in the ASan+UBSan build.

#include <EffectsCore/fx_system.h>
#include <gfx_d3d/r_drawsurf.h>
#include <gfx_d3d/r_marks.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

void __cdecl FX_ExpandMarkVerts_NoTransform_GfxWorldVertex_(
    FxMarksSystem *marksSystem,
    const FxMark *mark,
    uint16_t baseVertex);
void FX_GenerateMarkVertsForMark_MatrixFromPlacement(
    const GfxPlacement *placement,
    const float *viewOffset,
    float (*outTransform)[3]);

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

// The production function lives in gfx_d3d/r_marks.cpp, which this target does
// not link. Row 3 is the translation row the caller patches with viewOffset.
void __cdecl DObjSkelMatToMatrix43(const DObjSkelMat *, float (*outMatrix)[3])
{
    for (int row = 0; row < 4; ++row)
    {
        outMatrix[row][0] = row == 3 ? 100.0f : (row == 0 ? 1.0f : 0.0f);
        outMatrix[row][1] = row == 3 ? 200.0f : (row == 1 ? 1.0f : 0.0f);
        outMatrix[row][2] = row == 3 ? 300.0f : (row == 2 ? 1.0f : 0.0f);
    }
}

namespace
{
int checkMarkVerts()
{
    FxMarkPoint &point = g_marks.pointGroups[0].pointGroup.points[0];
    point.xyz[0] = 1.0f;
    point.lmapCoord[0] = 0.25f;
    point.lmapCoord[1] = 0.75f;
    point.normal[2] = 1.0f;
    g_marks.pointGroups[0].pointGroup.next = 0xFFFF; // the only group
    g_mark.radius = 1.0f;
    g_mark.texCoordAxis[0] = 1.0f;
    g_mark.origin[0] = 0.0f;
    g_mark.points = 0;
    g_mark.pointCount = 1;
    FX_ExpandMarkVerts_NoTransform_GfxWorldVertex_(&g_marks, &g_mark, 0);

    if (g_verts[0].lmapCoord[0] != 0.25f || g_verts[0].lmapCoord[1] != 0.75f)
    {
        std::fprintf(stderr, "FAIL no-transform mark vertex: lightmap %g,%g\n", g_verts[0].lmapCoord[0],
            g_verts[0].lmapCoord[1]);
        return 1;
    }
    return 0;
}

int checkPlacementTransform()
{
    GfxPlacement placement{};
    placement.quat[3] = 1.0f;
    const float viewOffset[3] = {1.0f, 2.0f, 3.0f};
    float transform[4][3] = {};
    FX_GenerateMarkVertsForMark_MatrixFromPlacement(&placement, viewOffset, transform);

    const float expected[3] = {101.0f, 202.0f, 303.0f};
    if (!std::equal(expected, expected + 3, transform[3]))
    {
        std::fprintf(stderr, "FAIL placement transform: row 3 %g,%g,%g\n", transform[3][0], transform[3][1],
            transform[3][2]);
        return 1;
    }
    return 0;
}
}

int main()
{
    const int marks = checkMarkVerts();
    const int placement = checkPlacementTransform();
    if (marks != 0 || placement != 0)
        return 1;
    std::puts("fx mark type contracts: ok");
    return 0;
}

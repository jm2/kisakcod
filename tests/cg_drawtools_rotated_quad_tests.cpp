// cg_drawtools_rotated_quad_tests.cpp: CG_DrawRotatedQuadPic
// (production cgame/cg_drawtools.cpp) walks all four rows of a
// const float (*)[2]. The decompile spelled each row as (*verts)[2 * i] and
// [2 * i + 1], past the end of the two-float row, which UBSan reports as an
// out-of-bounds index. With unit scales and angle 0 the command must receive
// the input rows unchanged.

#include <cmath>
#include <cstdio>

#include <cgame/cg_local.h>
#include <gfx_d3d/r_rendercmds.h>
#include <ui/ui_shared.h>

namespace
{
int g_failures = 0;
int g_quadCalls = 0;
float g_quad[4][2];
bool Near(float a, float b) { return std::fabs(a - b) < 1.0e-5f; }

#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

// The render-command boundary. Only the submitted quad is inspected.
void __cdecl R_AddCmdDrawQuadPic(const float (*verts)[2], const float *, Material *)
{
    ++g_quadCalls;
    for (int i = 0; i < 4; ++i)
    {
        g_quad[i][0] = verts[i][0];
        g_quad[i][1] = verts[i][1];
    }
}

int main()
{
    ScreenPlacement scrPlace{};
    scrPlace.scaleRealToVirtual[0] = scrPlace.scaleRealToVirtual[1] = 1.0f;
    scrPlace.scaleVirtualToReal[0] = scrPlace.scaleVirtualToReal[1] = 1.0f;

    const float verts[4][2] = {{-12.0f, -7.0f}, {33.0f, -7.0f}, {33.0f, 21.0f}, {-12.0f, 21.0f}};
    const float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CG_DrawRotatedQuadPic(&scrPlace, 0.0f, 0.0f, verts, 0.0f, color, nullptr);

    CHECK(g_quadCalls == 1);
    for (int i = 0; i < 4; ++i)
    {
        CHECK(Near(g_quad[i][0], verts[i][0]));
        CHECK(Near(g_quad[i][1], verts[i][1]));
    }

    if (g_failures == 0)
        std::printf("cg rotated quad: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

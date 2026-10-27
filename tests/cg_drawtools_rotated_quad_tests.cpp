// cg_drawtools_rotated_quad_tests.cpp: the quad geometry in
// production cgame/cg_drawtools.cpp. CG_DrawRotatedQuadPic walks all four rows
// of a const float (*)[2]. CG_Draw2DLine builds its perpendicular from the
// original normalized line direction; an implementation that overwrites one
// component before using the other produces a non-perpendicular quad.

#include <cmath>
#include <cstdio>

#include <cgame/cg_local.h>
#include <gfx_d3d/r_rendercmds.h>
#include <ui/ui_shared.h>
#include <universal/com_math.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
int g_quadCalls = 0;
float g_quad[4][2];
bool Near(float a, float b) { return std::fabs(a - b) < 1.0e-5f; }

#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

WEAK double __cdecl ScrPlace_ApplyX(const ScreenPlacement *, float x, int32_t) { return x; }
WEAK double __cdecl ScrPlace_ApplyY(const ScreenPlacement *, float y, int32_t) { return y; }
WEAK float __cdecl Vec2NormalizeTo(const vec2r v, vec2r out)
{
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1]);
    if (length == 0.0f)
    {
        out[0] = out[1] = 0.0f;
        return 0.0f;
    }
    out[0] = v[0] / length;
    out[1] = v[1] / length;
    return length;
}

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

    g_quadCalls = 0;
    CG_Draw2DLine(&scrPlace, 0.0f, 0.0f, 10.0f, 0.0f, 2.0f, 0, 0, color, nullptr);
    CHECK(g_quadCalls == 1);
    CHECK(Near(g_quad[0][0], 0.0f));
    CHECK(Near(g_quad[0][1], 1.0f));
    CHECK(Near(g_quad[1][0], 10.0f));
    CHECK(Near(g_quad[1][1], 1.0f));
    CHECK(Near(g_quad[2][0], 10.0f));
    CHECK(Near(g_quad[2][1], -1.0f));
    CHECK(Near(g_quad[3][0], 0.0f));
    CHECK(Near(g_quad[3][1], -1.0f));

    if (g_failures == 0)
        std::printf("cg drawtools geometry: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

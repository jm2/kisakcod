// r_point_light_partition_tests.cpp: R_EmitPointLightPartitionSurfs
// (gfx_d3d/r_light.cpp) copies each visible point light into its partition,
// which the back end draws (R_DrawPointLitSurfs). The copy used x86's
// sizeof(GfxLight), 0x40. At 64-bit, def is the pointer at offset 0x40, so the
// partition's light kept a null def, and the first point-lit draw crashed in
// R_SetLightProperties.
//
// The engine boundary is weak: --gc-sections drops what the check never
// reaches. The world and its draw lists are not needed for one light.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_light.h>
#include <gfx_d3d/r_pretess.h>
#include <gfx_d3d/r_rendercmds.h>
#include <gfx_d3d/r_scene.h>
#include <qcommon/com_bsp.h>
#include <universal/q_shared.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

GfxBackEndData g_backEnd;
GfxViewInfo g_viewInfo;
GfxLightDef *const kDef = reinterpret_cast<GfxLightDef *>(std::uintptr_t{0x1DEF0});
} // namespace

// The globals the emitter reads, owned here. A point light never reaches
// the spot-light branch that reads r_spotLightShadows and comWorld.
GfxScene scene;
GfxBackEndData *frontEndDataOut = &g_backEnd;
const dvar_t *r_spotLightShadows;
ComWorld comWorld;

// Engine boundary: each emitted draw surface counts one.
WEAK void __cdecl R_InitDrawSurfListInfo(GfxDrawSurfListInfo *info) { std::memset(info, 0, sizeof(*info)); }
WEAK void __cdecl R_EmitDrawSurfList(GfxDrawSurf *, uint32_t drawSurfCount) { frontEndDataOut->drawSurfCount += drawSurfCount; }
WEAK char *QDECL va(const char *, ...)
{
    static char empty[1];
    return empty;
}
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::exit(1);
}

int main()
{
    GfxLight light{};
    light.type = GFX_LIGHT_TYPE_OMNI;
    light.radius = 256.0f;
    light.exponent = 7;
    light.spotShadowIndex = 3;
    light.def = kDef;
    const GfxLight *visible[] = {&light};
    scene.visLight[0].drawSurfCount = 2;
    const float viewOrigin[4] = {};

    const int count = R_EmitPointLightPartitionSurfs(&g_viewInfo, visible, 1, viewOrigin);
    CHECK(count == 1);
    const PointLightPartition &partition = g_viewInfo.pointLightPartitions[0];
    CHECK(partition.light.def == kDef);
    CHECK(partition.light.type == GFX_LIGHT_TYPE_OMNI && partition.light.radius == 256.0f
          && partition.light.exponent == 7 && partition.light.spotShadowIndex == 3);
    CHECK(partition.info.light == &partition.light && partition.info.drawSurfCount == 2);

    if (g_failures == 0)
        std::printf("r point light partition: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

// r_add_staticmodel_lod_tests.cpp: the static model skin passes of the
// production gfx_d3d/r_add_staticmodel.cpp. Their callers hold the per-surface
// LOD lists as uint16_t[4][4][128] and the counts as uint16_t[4][4], passed as
// pointers to the first row. The decompiled code reached surface s as
// (*list)[4 * s] and (*count)[4 * s], indexing past row 0. With every count
// zero the passes draw nothing, but they still form those row addresses.

#include <gfx_d3d/r_add_staticmodel.h>
#include <gfx_d3d/r_bsp.h>
#include <gfx_d3d/r_buffers.h>
#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_init.h>
#include <gfx_d3d/r_pretess.h>
#include <gfx_d3d/r_rendercmds.h>
#include <gfx_d3d/r_staticmodelcache.h>
#include <xanim/xmodel.h>

#include <cstdio>
#include <cstdlib>

// The draw paths these passes reach only for a non-zero count; with every
// count zero none of them may run.
DxGlobals dx;
GfxMetrics gfxMetrics;
GfxBuffers gfxBuf;
const dvar_t *r_pretess;
[[noreturn]] static void Unreached()
{
    std::fputs("FAIL a draw path ran with every count zero\n", stderr);
    std::abort();
}
void Com_Memcpy(void *, const void *, const size_t) { Unreached(); }
void __cdecl MyAssertHandler(const char *, int, int, const char *, ...) { Unreached(); }
char *__cdecl va(const char *, ...) { Unreached(); }
uint16_t *__cdecl R_AllocPreTessIndices(int) { Unreached(); }
const MaterialTechnique *__cdecl Material_GetTechnique(const Material *, MaterialTechniqueType) { Unreached(); }
int __cdecl R_AllocDrawSurf(GfxDelayedCmdBuf *, GfxDrawSurf, GfxDrawSurfList *, uint32_t) { Unreached(); }
void __cdecl R_WritePrimDrawSurfInt(GfxDelayedCmdBuf *, uint32_t) { Unreached(); }
void __cdecl R_AddDelayedStaticModelDrawSurf(GfxDelayedCmdBuf *, XSurface *, uint8_t *, uint32_t) { Unreached(); }
GfxCachedSModelSurf *__cdecl R_GetCachedSModelSurf(uint32_t) { Unreached(); }
int __cdecl XModelGetSurfaces(const XModel *, XSurface **, int) { Unreached(); }
XSurface *__cdecl XModelGetSurface(const XModel *, int, int) { Unreached(); }
Material **__cdecl XModelGetSkins(const XModel *, int) { Unreached(); }

namespace
{
uint16_t g_lodList[4][4][128];
uint16_t g_lodCount[4][4];
}

int main()
{
    R_SkinStaticModelsCamera(nullptr, 0, g_lodList, g_lodCount, nullptr);
    R_SkinStaticModelsShadow(nullptr, g_lodList, g_lodCount, nullptr);
    std::puts("r_add_staticmodel lod lists: ok");
    return 0;
}

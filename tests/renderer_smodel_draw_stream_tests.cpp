// Static-model groups in the prim draw-surf stream (gfx_d3d/r_add_cmdbuf.cpp,
// r_prim_draw_surf_stream.h). The engine's writer path reserves each group
// with R_AllocDrawSurf(R_StaticModelGroupDwords(count)) and writes it with
// R_AddDelayedStaticModelDrawSurf. R_GetNextStaticModelSurf and
// R_GetNextStaticModelCachedSurf read it back through R_ReadStaticModelGroup.
// The XSurface pointer used to be written as one uint32_t and read back with a
// one-dword skip, which truncates it on 64-bit and desynchronises every group
// after the first. The surface pointers here have their high bits set on
// 64-bit, so a truncating write, a short reservation or a one-dword skip
// fails a check.

#include <cstdio>
#include <cstdlib>

#include <gfx_d3d/r_add_staticmodel.h>
#include <gfx_d3d/r_prim_draw_surf_stream.h>
#include <gfx_d3d/r_rendercmds.h>

GfxBackEndData *frontEndDataOut;

namespace
{
int g_failures = 0;
int g_asserts = 0;

void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

XSurface *FakeSurface(uint64_t bits)
{
    // Never dereferenced: only the pointer value travels through the stream.
    return reinterpret_cast<XSurface *>(static_cast<uintptr_t>(bits));
}

struct Group
{
    XSurface *xsurf;
    uint32_t count;
    uint16_t list[5];
};
}

void MyAssertHandler(const char *, int, int, const char *, ...)
{
    ++g_asserts;
}

void R_WarnOncePerFrame(GfxWarningType, ...)
{
    ++g_asserts;
}

int main()
{
    frontEndDataOut = static_cast<GfxBackEndData *>(std::calloc(1, sizeof(GfxBackEndData)));
    if (!frontEndDataOut)
        return 2;

    // Odd and even counts, so the uint16_t list padding is covered too.
    Group groups[] = {
        { FakeSurface(0x123456789ABCDEF0ull), 3, { 5, 6, 7 } },
        { FakeSurface(0xFEDCBA9876543210ull), 1, { 9 } },
        { FakeSurface(0x0000000100000004ull), 4, { 1, 2, 3, 4 } },
        { FakeSurface(0x7FFFFFFF80000008ull), 5, { 10, 11, 12, 13, 14 } },
    };

    GfxDelayedCmdBuf delayedCmdBuf;
    R_InitDelayedCmdBuf(&delayedCmdBuf);
    GfxDrawSurf drawSurfs[4] = {};
    GfxDrawSurfList drawSurfList{ drawSurfs, drawSurfs + 4 };
    GfxDrawSurf drawSurf{};
    drawSurf.packed = 0x1234;

    for (Group &group : groups)
    {
        Check(R_AllocDrawSurf(&delayedCmdBuf, drawSurf, &drawSurfList, R_StaticModelGroupDwords(group.count)) == 1,
              "R_AllocDrawSurf reserves the group");
        R_AddDelayedStaticModelDrawSurf(&delayedCmdBuf, group.xsurf, reinterpret_cast<uint8_t *>(group.list), group.count);
    }
    R_EndCmdBuf(&delayedCmdBuf);
    Check(g_asserts == 0, "the writes fit their reservations");
    Check(drawSurfList.current == drawSurfs + 1, "one draw surf heads the stream");

    const uint32_t *pos = &frontEndDataOut->primDrawSurfsBuf[drawSurfs[0].fields.objectId];
    for (const Group &group : groups)
    {
        uint32_t count = 0;
        const uint16_t *list = nullptr;
        XSurface *xsurf = R_ReadStaticModelGroup(pos, &count, &list);
        Check(xsurf == group.xsurf, "the XSurface pointer round-trips");
        Check(count == group.count, "the smodel count round-trips");
        bool listMatches = list != nullptr;
        for (uint32_t i = 0; listMatches && i < group.count; ++i)
            listMatches = list[i] == group.list[i];
        Check(listMatches, "the smodel list round-trips");
    }
    uint32_t count = 1;
    const uint16_t *list = nullptr;
    Check(R_ReadStaticModelGroup(pos, &count, &list) == nullptr && count == 0, "the stream ends after the last group");

    std::free(frontEndDataOut);
    if (g_failures)
        return 1;
    std::printf("renderer-smodel-draw-stream: all checks passed\n");
    return 0;
}

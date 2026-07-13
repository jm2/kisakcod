#include "r_bsp.h"
#include "r_rendercmds.h"
#include "r_drawsurf.h"
#include "r_reservation_atomic.h"

void __cdecl R_InitDelayedCmdBuf(GfxDelayedCmdBuf *delayedCmdBuf)
{
    delayedCmdBuf->primDrawSurfPos = -1;
    delayedCmdBuf->primDrawSurfSize = 0;
    delayedCmdBuf->drawSurfKey.packed = 0xFFFFFFFFFFFFFFFF;
    //*(uint32_t *)&delayedCmdBuf->drawSurfKey.fields = -1;
    //HIDWORD(delayedCmdBuf->drawSurfKey.packed) = -1;
}

void __cdecl R_EndCmdBuf(GfxDelayedCmdBuf *delayedCmdBuf)
{
    if ((HIDWORD(delayedCmdBuf->drawSurfKey.packed) & *(uint32_t *)&delayedCmdBuf->drawSurfKey.fields) != -1)
    {
        if (!delayedCmdBuf->primDrawSurfSize)
            MyAssertHandler(
                ".\\r_add_cmdbuf.cpp",
                32,
                0,
                "%s\n\t(delayedCmdBuf->primDrawSurfSize) = %i",
                "(delayedCmdBuf->primDrawSurfSize > 0)",
                delayedCmdBuf->primDrawSurfSize);
        *(uint32_t *)&delayedCmdBuf->drawSurfKey.fields = -1;
        HIDWORD(delayedCmdBuf->drawSurfKey.packed) = -1;
        frontEndDataOut->primDrawSurfsBuf[delayedCmdBuf->primDrawSurfPos++] = 0;
        --delayedCmdBuf->primDrawSurfSize;
    }
}

int __cdecl R_AllocDrawSurf(
    GfxDelayedCmdBuf *delayedCmdBuf,
    GfxDrawSurf drawSurf,
    GfxDrawSurfList *drawSurfList,
    uint32_t size)
{
    constexpr uint32_t kPrimitiveBlockSize = 512u;
    uint32_t primDrawSurfPos = UINT32_MAX; // [esp+10h] [ebp-4h]

    iassert( size < kPrimitiveBlockSize );
    if (size >= kPrimitiveBlockSize)
    {
        // Preserve the delayed-buffer state machine: an active command owns
        // one terminator slot and must be closed before the block is rejected.
        R_EndCmdBuf(delayedCmdBuf);
        delayedCmdBuf->primDrawSurfSize = 0;
        R_WarnOncePerFrame(R_WARN_PRIM_DRAW_SURF_BUFFER_SIZE);
        return 0;
    }
    if (delayedCmdBuf->drawSurfKey.packed != drawSurf.packed)
        R_EndCmdBuf(delayedCmdBuf);
    if (delayedCmdBuf->primDrawSurfSize > size)
    {
        primDrawSurfPos = delayedCmdBuf->primDrawSurfPos;
    }
    else
    {
        R_EndCmdBuf(delayedCmdBuf);
        if (!gfx::reservation_atomic::TryReserve(
                &frontEndDataOut->primDrawSurfPos,
                kPrimitiveBlockSize,
                static_cast<uint32_t>(ARRAY_COUNT(frontEndDataOut->primDrawSurfsBuf)),
                &primDrawSurfPos))
        {
            delayedCmdBuf->primDrawSurfSize = 0;
            R_WarnOncePerFrame(R_WARN_PRIM_DRAW_SURF_BUFFER_SIZE);
            return 0;
        }
        delayedCmdBuf->primDrawSurfPos = primDrawSurfPos;
        delayedCmdBuf->primDrawSurfSize = kPrimitiveBlockSize;
    }
    if (delayedCmdBuf->drawSurfKey.packed == drawSurf.packed)
        return 1;
    if (drawSurfList->current < drawSurfList->end)
    {
        delayedCmdBuf->drawSurfKey = drawSurf;
        bcassert(primDrawSurfPos, (1 << MTL_SORT_OBJECT_ID_BITS));
        *(uint32_t *)&drawSurf.fields = (uint16_t)primDrawSurfPos | *(uint32_t *)&drawSurf.fields & 0xFFFF0000;
        drawSurfList->current->fields = drawSurf.fields;
        ++drawSurfList->current;
        return 1;
    }
    else
    {
        R_WarnOncePerFrame(R_WARN_MAX_SCENE_DRAWSURFS, "R_AllocDrawSurf");
        return 0;
    }
}

void __cdecl R_WritePrimDrawSurfInt(GfxDelayedCmdBuf *delayedCmdBuf, uint32_t value)
{
    iassert( delayedCmdBuf->primDrawSurfSize );
    if (delayedCmdBuf->primDrawSurfPos < 0)
        MyAssertHandler(
            ".\\r_add_cmdbuf.cpp",
            145,
            0,
            "%s\n\t(delayedCmdBuf->primDrawSurfPos) = %i",
            "(delayedCmdBuf->primDrawSurfPos >= 0)",
            delayedCmdBuf->primDrawSurfPos);
    --delayedCmdBuf->primDrawSurfSize;
    frontEndDataOut->primDrawSurfsBuf[delayedCmdBuf->primDrawSurfPos++] = value;
}

void __cdecl R_WritePrimDrawSurfData(GfxDelayedCmdBuf *delayedCmdBuf, uint8_t *data, uint32_t count)
{
    if (delayedCmdBuf->primDrawSurfSize < count)
        MyAssertHandler(
            ".\\r_add_cmdbuf.cpp",
            162,
            0,
            "%s\n\t(delayedCmdBuf->primDrawSurfSize) = %i",
            "(delayedCmdBuf->primDrawSurfSize >= count)",
            delayedCmdBuf->primDrawSurfSize);
    iassert( delayedCmdBuf->primDrawSurfPos >= 0 );
    delayedCmdBuf->primDrawSurfSize -= count;
    memcpy((uint8_t *)&frontEndDataOut->primDrawSurfsBuf[delayedCmdBuf->primDrawSurfPos], data, 4 * count);
    delayedCmdBuf->primDrawSurfPos += count;
}

#include "database.h"

#include <universal/physicalmemory.h>
#include <qcommon/qcommon.h>
#ifndef KISAK_DEDI_HEADLESS
#include <gfx_d3d/r_buffers.h>
#endif

int32_t g_block_mem_type[9] =
{ 0, 1, 1, 2, 1, 1, 2, 2, 2 };

const char *g_block_mem_name[9] =
{
  "temp",
  "runtime",
  "large_runtime",
  "physical_runtime",
  "virtual",
  "large",
  "physical",
  "vertex",
  "index"
};

#ifdef KISAK_DEDI_HEADLESS
namespace
{
void DB_ClearHeadlessGeometryBufferState(XZoneMemory *zoneMem)
{
    iassert(zoneMem);

    zoneMem->lockedVertexData = nullptr;
    zoneMem->lockedIndexData = nullptr;
    zoneMem->vertexBuffer = nullptr;
    zoneMem->indexBuffer = nullptr;
}
}
#endif

void __cdecl DB_RecoverGeometryBuffers(XZoneMemory *zoneMem)
{
#ifdef KISAK_DEDI_HEADLESS
    // Blocks 7 and 8 remain ordinary CPU-backed zone streams.  A headless
    // process never creates a second GPU copy when renderer state is rebuilt.
    DB_ClearHeadlessGeometryBufferState(zoneMem);
#else
    uint8_t *lockedData; // [esp+4h] [ebp-4h]
    uint8_t *lockedDataa; // [esp+4h] [ebp-4h]

    if (zoneMem->blocks[7].size)
    {
        iassert(zoneMem->blocks[7].data);
        iassert(zoneMem->vertexBuffer == NULL);
        lockedData = (uint8_t *)R_AllocStaticVertexBuffer((IDirect3DVertexBuffer9 **)&zoneMem->vertexBuffer, zoneMem->blocks[7].size);
        memcpy(lockedData, zoneMem->blocks[7].data, zoneMem->blocks[7].size);
        if (zoneMem->lockedVertexData)
            zoneMem->lockedVertexData = lockedData;
        else
            R_FinishStaticVertexBuffer((IDirect3DVertexBuffer9 *)zoneMem->vertexBuffer);
    }
    if (zoneMem->blocks[8].size)
    {
        iassert(zoneMem->blocks[8].data);
        iassert(zoneMem->indexBuffer == NULL);
        lockedDataa = (uint8_t *)R_AllocStaticIndexBuffer((IDirect3DIndexBuffer9 **)&zoneMem->indexBuffer, zoneMem->blocks[8].size);
        memcpy(lockedDataa, zoneMem->blocks[8].data, zoneMem->blocks[8].size);
        if (zoneMem->lockedIndexData)
            zoneMem->lockedIndexData = lockedDataa;
        else
            R_FinishStaticIndexBuffer((IDirect3DIndexBuffer9 *)zoneMem->indexBuffer);
    }
#endif
}

void __cdecl DB_ReleaseGeometryBuffers(XZoneMemory *zoneMem)
{
#ifdef KISAK_DEDI_HEADLESS
    // CPU zone blocks are released by the physical-memory owner.  Only clear
    // the renderer-facing state here so no stale lock marker can survive.
    DB_ClearHeadlessGeometryBufferState(zoneMem);
#else
    IDirect3DVertexBuffer9 *vb; // [esp+0h] [ebp-8h]
    IDirect3DIndexBuffer9 *ib; // [esp+4h] [ebp-4h]

    if (zoneMem->vertexBuffer)
    {
        vb = (IDirect3DVertexBuffer9 *)zoneMem->vertexBuffer;
        if (zoneMem->lockedVertexData)
            R_UnlockVertexBuffer(vb);
        R_FreeStaticVertexBuffer(vb);
        zoneMem->vertexBuffer = 0;
    }
    if (zoneMem->indexBuffer)
    {
        ib = (IDirect3DIndexBuffer9 *)zoneMem->indexBuffer;
        if (zoneMem->lockedIndexData)
            R_UnlockIndexBuffer(ib);
        R_FreeStaticIndexBuffer(ib);
        zoneMem->indexBuffer = 0;
    }
#endif
}

void __cdecl DB_AllocXZoneMemory(
    uint32_t *blockSize,
    const char *filename,
    XZoneMemory *zoneMem,
    uint32_t allocType)
{
    int32_t OverAllocatedSize; // [esp+20h] [ebp-14h]
    uint32_t blockIndex; // [esp+28h] [ebp-Ch]
    uint8_t *buf; // [esp+2Ch] [ebp-8h]
    uint32_t size; // [esp+30h] [ebp-4h]

    if (!blockSize || !filename || !zoneMem)
    {
        Com_Error(ERR_DROP, "Invalid fast-file zone allocation request");
        return;
    }

    for (blockIndex = 0; blockIndex < 9; ++blockIndex)
    {
        if (zoneMem->blocks[blockIndex].size || zoneMem->blocks[blockIndex].data)
        {
            Com_Error(ERR_DROP, "Fast-file zone block %u was already allocated", blockIndex);
            return;
        }
        size = blockSize[blockIndex];
        if (size)
        {
            buf = DB_MemAlloc(size, g_block_mem_type[blockIndex], allocType);
            if (!buf)
            {
                OverAllocatedSize = PMem_GetOverAllocatedSize();
                Com_Error(
                    ERR_DROP,
                    "Could not allocate %.2f MB of type '%s' for zone '%s' needed an additional %.2f MB",
                    (double)size * 0.00000095367431640625,
                    g_block_mem_name[blockIndex],
                    filename,
                    (double)OverAllocatedSize * 0.00000095367431640625);
                return;
            }
            zoneMem->blocks[blockIndex].size = size;
            zoneMem->blocks[blockIndex].data = buf;
        }
    }
#ifdef KISAK_DEDI_HEADLESS
    // Do not alias the renderer lock pointers to blocks 7 or 8.  Stream loads
    // still materialize those blocks; DB_CloneStreamData(nullptr) deliberately
    // skips the absent GPU mirror.
    DB_ClearHeadlessGeometryBufferState(zoneMem);
#else
    if (zoneMem->vertexBuffer)
        MyAssertHandler(".\\database\\db_memory.cpp", 104, 0, "%s", "zoneMem->vertexBuffer == NULL");
    if (zoneMem->lockedVertexData)
        MyAssertHandler(".\\database\\db_memory.cpp", 105, 0, "%s", "zoneMem->lockedVertexData == NULL");
    if (zoneMem->blocks[7].size)
        zoneMem->lockedVertexData = (uint8_t *)R_AllocStaticVertexBuffer((IDirect3DVertexBuffer9 **)&zoneMem->vertexBuffer, zoneMem->blocks[7].size);
    if (zoneMem->indexBuffer)
        MyAssertHandler(".\\database\\db_memory.cpp", 110, 0, "%s", "zoneMem->indexBuffer == NULL");
    if (zoneMem->lockedIndexData)
        MyAssertHandler(".\\database\\db_memory.cpp", 111, 0, "%s", "zoneMem->lockedIndexData == NULL");
    if (zoneMem->blocks[8].size)
        zoneMem->lockedIndexData = (uint8_t *)R_AllocStaticIndexBuffer((IDirect3DIndexBuffer9 **)&zoneMem->indexBuffer, zoneMem->blocks[8].size);
#endif
}

uint8_t *__cdecl DB_MemAlloc(uint32_t size, uint32_t type, uint32_t allocType)
{
    if (type <= 1)
    {
        if (size > UINT32_MAX - 15)
            return nullptr;
        return PMem_TryAlloc(size + 15, 0x1000u, 4, allocType);
    }
    if (type != DM_MEMORY_PHYSICAL)
        return nullptr;
    return PMem_TryAlloc(size, 0x1000u, 0x404u, allocType);
}

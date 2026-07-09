#include "database.h"
#include "db_validation.h"

namespace
{
bool DB_GetBlock(uint32_t index, const XBlock **block)
{
    if (!block || !g_streamZoneMem || index >= ARRAY_COUNT(g_streamZoneMem->blocks))
        return false;

    *block = &g_streamZoneMem->blocks[index];
    return !(*block)->size || (*block)->data != nullptr;
}
}

uint32_t g_streamDelayIndex;
XBlock * g_streamBlocks;
uint8_t *g_streamPosArray[9];
StreamDelayInfo g_streamDelayArray[4096];
uint32_t g_streamPosIndex;
XZoneMemory *g_streamZoneMem;
uint8_t *g_streamPos;

StreamPosInfo g_streamPosStack[64];
uint32_t g_streamPosStackIndex;

void __cdecl DB_InitStreams(XZoneMemory *zoneMem)
{
    int32_t i; // [esp+0h] [ebp-4h]

    if (!zoneMem)
    {
        Com_Error(ERR_DROP, "Cannot initialize fast-file streams without zone memory");
        return;
    }
    for (i = 0; i < ARRAY_COUNT(zoneMem->blocks); ++i)
    {
        if (zoneMem->blocks[i].size && !zoneMem->blocks[i].data)
        {
            Com_Error(ERR_DROP, "Fast-file stream block %d has size but no storage", i);
            return;
        }
    }

    g_streamZoneMem = zoneMem;
    g_streamPos = zoneMem->blocks[0].data;
    g_streamPosIndex = 0;
    g_streamDelayIndex = 0;
    g_streamPosStackIndex = 0;
    for (i = 0; i < 9; ++i)
        g_streamPosArray[i] = zoneMem->blocks[i].data;
}

void __cdecl DB_PushStreamPos(uint32_t index)
{
    const XBlock *targetBlock = nullptr;
    if (index >= ARRAY_COUNT(g_streamPosArray)
        || g_streamPosIndex >= ARRAY_COUNT(g_streamPosArray)
        || g_streamPosStackIndex >= ARRAY_COUNT(g_streamPosStack)
        || !DB_IsStreamRangeValid(g_streamPos, 0)
        || !DB_GetBlock(index, &targetBlock)
        || !db::validation::SpanWithinBlock(
            reinterpret_cast<uintptr_t>(targetBlock->data),
            targetBlock->size,
            reinterpret_cast<uintptr_t>(g_streamPosArray[index]),
            0))
    {
        Com_Error(ERR_DROP, "Invalid fast-file stream push (index %u, current %u, depth %u)", index, g_streamPosIndex, g_streamPosStackIndex);
        return;
    }

    g_streamPosStack[g_streamPosStackIndex].index = g_streamPosIndex;
    DB_SetStreamIndex(index);

    g_streamPosStack[g_streamPosStackIndex++].pos = g_streamPos;
}

void __cdecl DB_CloneStreamData(uint8_t *destStart)
{
    if (!destStart)
        return;

    const XBlock *block = nullptr;
    if (!DB_GetBlock(g_streamPosIndex, &block))
    {
        Com_Error(ERR_DROP, "Invalid fast-file stream clone block %u", g_streamPosIndex);
        return;
    }

    uint8_t *sourceStart = g_streamPosArray[g_streamPosIndex];
    const uintptr_t blockBase = reinterpret_cast<uintptr_t>(block->data);
    const uintptr_t source = reinterpret_cast<uintptr_t>(sourceStart);
    const uintptr_t position = reinterpret_cast<uintptr_t>(g_streamPos);
    if (position < source
        || !db::validation::SpanWithinBlock(blockBase, block->size, source, 0)
        || !db::validation::SpanWithinBlock(blockBase, block->size, position, 0)
        || !db::validation::SpanWithinBlock(blockBase, block->size, source, static_cast<uint32_t>(position - source)))
    {
        Com_Error(ERR_DROP, "Invalid fast-file stream clone range");
        return;
    }

    memcpy(
        &destStart[source - blockBase],
        sourceStart,
        position - source);
}

void __cdecl DB_SetStreamIndex(uint32_t index)
{
    const XBlock *targetBlock = nullptr;
    if (index >= ARRAY_COUNT(g_streamPosArray)
        || g_streamPosIndex >= ARRAY_COUNT(g_streamPosArray)
        || !DB_IsStreamRangeValid(g_streamPos, 0)
        || !DB_GetBlock(index, &targetBlock)
        || !db::validation::SpanWithinBlock(
            reinterpret_cast<uintptr_t>(targetBlock->data),
            targetBlock->size,
            reinterpret_cast<uintptr_t>(g_streamPosArray[index]),
            0))
    {
        Com_Error(ERR_DROP, "Invalid fast-file stream index transition (%u -> %u)", g_streamPosIndex, index);
        return;
    }

    if (index != g_streamPosIndex)
    {
        if (g_streamPosIndex == 7)
        {
            DB_CloneStreamData(g_streamZoneMem->lockedVertexData);
        }
        else if (g_streamPosIndex == 8)
        {
            DB_CloneStreamData(g_streamZoneMem->lockedIndexData);
        }
        g_streamPosArray[g_streamPosIndex] = g_streamPos;
        g_streamPosIndex = index;
        g_streamPos = g_streamPosArray[index];
        if (!DB_IsStreamRangeValid(g_streamPos, 0))
        {
            Com_Error(ERR_DROP, "Fast-file stream %u has an invalid position", index);
            return;
        }
    }
}

void __cdecl DB_PopStreamPos()
{
    if (!g_streamPosStackIndex)
    {
        Com_Error(ERR_DROP, "Fast-file stream stack underflow");
        return;
    }
    --g_streamPosStackIndex;
    if (g_streamPosStack[g_streamPosStackIndex].index >= ARRAY_COUNT(g_streamPosArray))
    {
        Com_Error(ERR_DROP, "Invalid saved fast-file stream index %u", g_streamPosStack[g_streamPosStackIndex].index);
        return;
    }
    if (!g_streamPosIndex)
        g_streamPos = g_streamPosStack[g_streamPosStackIndex].pos;
    DB_SetStreamIndex(g_streamPosStack[g_streamPosStackIndex].index);
}

uint8_t *__cdecl DB_GetStreamPos()
{
    return g_streamPos;
}

bool __cdecl DB_IsStreamRangeValid(const void *ptr, uint32_t size)
{
    const XBlock *block = nullptr;
    if (!DB_GetBlock(g_streamPosIndex, &block))
        return false;

    return db::validation::SpanWithinBlock(
        reinterpret_cast<uintptr_t>(block->data),
        block->size,
        reinterpret_cast<uintptr_t>(ptr),
        size);
}

bool __cdecl DB_IsZoneRangeValid(const void *ptr, uint32_t size)
{
    if (!g_streamZoneMem)
        return false;

    for (uint32_t index = 0; index < ARRAY_COUNT(g_streamZoneMem->blocks); ++index)
    {
        const XBlock *block = nullptr;
        if (DB_GetBlock(index, &block)
            && db::validation::SpanWithinBlock(
                reinterpret_cast<uintptr_t>(block->data),
                block->size,
                reinterpret_cast<uintptr_t>(ptr),
                size))
        {
            return true;
        }
    }
    return false;
}

uint8_t *__cdecl DB_AllocStreamPos(int32_t alignment)
{
    if (alignment < 0 || !DB_IsStreamRangeValid(g_streamPos, 0))
    {
        Com_Error(ERR_DROP, "Invalid fast-file stream alignment state");
        return nullptr;
    }

    uintptr_t aligned = 0;
    if (!db::validation::AlignUp(reinterpret_cast<uintptr_t>(g_streamPos), static_cast<uintptr_t>(alignment), &aligned)
        || !DB_IsStreamRangeValid(reinterpret_cast<const void *>(aligned), 0))
    {
        Com_Error(ERR_DROP, "Fast-file stream alignment exceeds its block");
        return nullptr;
    }
    g_streamPos = reinterpret_cast<uint8_t *>(aligned);
    return g_streamPos;
}

void __cdecl DB_IncStreamPos(int32_t size)
{
    if (size < 0 || !DB_IsStreamRangeValid(g_streamPos, static_cast<uint32_t>(size)))
    {
        Com_Error(ERR_DROP, "Fast-file stream advance of %d bytes exceeds block %u", size, g_streamPosIndex);
        return;
    }

    g_streamPos += size;
}

const void **__cdecl DB_InsertPointer()
{
    const void **pData; // [esp+0h] [ebp-4h]

    DB_PushStreamPos(4);
    pData = (const void **)DB_AllocStreamPos(3);
    if (!pData)
        return nullptr;
    DB_IncStreamPos(4);
    DB_PopStreamPos();
    return pData;
}

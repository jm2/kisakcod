#include "database.h"
#include "db_validation.h"

namespace
{
db::relocation::AliasRegistry g_aliasRegistry;
db::relocation::DirectResolver g_directResolver;

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

    // Do not retain relocation state or stream cursors from a prior zone if
    // validation of the replacement zone fails.
    g_aliasRegistry.Reset(nullptr, 0);
    g_directResolver.Reset(nullptr, 0);
    g_streamZoneMem = nullptr;
    g_streamPos = nullptr;
    g_streamPosIndex = 0;
    g_streamDelayIndex = 0;
    g_streamPosStackIndex = 0;
    for (i = 0; i < ARRAY_COUNT(g_streamPosArray); ++i)
        g_streamPosArray[i] = nullptr;

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

    db::relocation::BlockView relocationBlocks[db::relocation::kBlockCount];
    for (i = 0; i < ARRAY_COUNT(zoneMem->blocks); ++i)
    {
        relocationBlocks[i].base = reinterpret_cast<uintptr_t>(zoneMem->blocks[i].data);
        relocationBlocks[i].size = zoneMem->blocks[i].size;
    }
    g_aliasRegistry.Reset(relocationBlocks, ARRAY_COUNT(relocationBlocks));
    g_directResolver.Reset(relocationBlocks, ARRAY_COUNT(relocationBlocks));

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

DBAliasHandle __cdecl DB_RegisterPointerSlot(
    const void *slot,
    DBAliasKind kind)
{
    DBAliasHandle handle;
    const db::relocation::Status status = g_aliasRegistry.RegisterSlot(
        reinterpret_cast<uintptr_t>(slot),
        kind,
        &handle);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Cannot register fast-file pointer slot: %s",
            db::relocation::StatusName(status));
        return {};
    }
    return handle;
}

DBAliasHandle __cdecl DB_InsertPointer(DBAliasKind kind)
{
    uint32_t *slot = nullptr;

    DB_PushStreamPos(4);
    slot = reinterpret_cast<uint32_t *>(DB_AllocStreamPos(3));
    if (!slot)
    {
        DB_PopStreamPos();
        return {};
    }
    if (!DB_IsStreamRangeValid(slot, static_cast<uint32_t>(sizeof(*slot))))
    {
        Com_Error(ERR_DROP, "Fast-file alias slot exceeds stream block 4");
        DB_PopStreamPos();
        return {};
    }
    *slot = 0;
    DB_IncStreamPos(4);
    DB_PopStreamPos();

    return DB_RegisterPointerSlot(slot, kind);
}

void __cdecl DB_SetInsertedPointer(
    DBAliasHandle handle,
    DBAliasKind expectedKind,
    const void *pointer,
    uint32_t metadata)
{
    if (expectedKind != DBAliasKind::XStringPointerSlot
        && db::relocation::RequiresExactStartPublication(expectedKind))
    {
        Com_Error(
            ERR_DROP,
            "Completed fast-file objects require DB_CompleteObject validation");
        return;
    }
    if (expectedKind == DBAliasKind::SoundData
        && !DB_IsZoneRangeValid(pointer, metadata))
    {
        Com_Error(ERR_DROP, "Fast-file sound alias source is outside zone memory");
        return;
    }
    if (expectedKind == DBAliasKind::XStringPointerSlot
        && !DB_IsZoneRangeValid(pointer, 4))
    {
        Com_Error(ERR_DROP, "Fast-file completed string holder is outside zone memory");
        return;
    }
    if (expectedKind == DBAliasKind::XStringPointerSlot)
    {
        uint32_t stringPointer = 0;
        memcpy(&stringPointer, pointer, sizeof(stringPointer));
        uint32_t stringBytes = 0;
        const db::relocation::Status stringStatus =
            DB_ValidateStreamCString(
                reinterpret_cast<const void *>(static_cast<uintptr_t>(stringPointer)),
                &stringBytes);
        if (stringStatus != db::relocation::Status::Ok)
        {
            Com_Error(
                ERR_DROP,
                "Fast-file completed string holder has invalid contents: %s",
                db::relocation::StatusName(stringStatus));
            return;
        }
        if (stringBytes <= 1)
        {
            Com_Error(ERR_DROP, "Fast-file completed string holder has no value");
            return;
        }
    }
    const db::relocation::Status status = g_aliasRegistry.Publish(
        handle,
        expectedKind,
        reinterpret_cast<uintptr_t>(pointer),
        metadata);
    if (status != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Cannot publish fast-file alias slot: %s",
            db::relocation::StatusName(status));
    }
}

bool __cdecl DB_CompleteObject(
    DBAliasHandle handle,
    DBAliasKind expectedKind,
    const void *pointer,
    uint32_t metadata,
    uint32_t materializedBytes)
{
    if (!db::relocation::RequiresExactStartPublication(expectedKind)
        || expectedKind == DBAliasKind::XStringPointerSlot)
    {
        Com_Error(ERR_DROP, "Invalid completed fast-file object kind");
        return false;
    }

    uint32_t headerBytes = 0;
    switch (expectedKind)
    {
    case DBAliasKind::MaterialVertexDeclaration:
        if (metadata != disk32::kMaterialVertexDeclarationBytes
            || materializedBytes != disk32::kMaterialVertexDeclarationBytes)
        {
            Com_Error(ERR_DROP, "Invalid completed material vertex declaration schema");
            return false;
        }
        headerBytes = disk32::kMaterialVertexDeclarationBytes;
        break;
    case DBAliasKind::MaterialTechnique:
        if (metadata != disk32::kMaterialTechniqueSchema)
        {
            Com_Error(ERR_DROP, "Invalid completed material technique schema");
            return false;
        }
        headerBytes = disk32::kMaterialTechniqueHeaderBytes;
        break;
    case DBAliasKind::MaterialVertexShader:
        if (metadata != disk32::kMaterialVertexShaderBytes
            || materializedBytes != disk32::kMaterialVertexShaderBytes)
        {
            Com_Error(ERR_DROP, "Invalid completed material vertex shader schema");
            return false;
        }
        headerBytes = disk32::kMaterialVertexShaderBytes;
        break;
    case DBAliasKind::MaterialPixelShader:
        if (metadata != disk32::kMaterialPixelShaderBytes
            || materializedBytes != disk32::kMaterialPixelShaderBytes)
        {
            Com_Error(ERR_DROP, "Invalid completed material pixel shader schema");
            return false;
        }
        headerBytes = disk32::kMaterialPixelShaderBytes;
        break;
    case DBAliasKind::MaterialWater:
        if (metadata != disk32::kMaterialWaterBytes
            || materializedBytes != disk32::kMaterialWaterBytes)
        {
            Com_Error(ERR_DROP, "Invalid completed material water schema");
            return false;
        }
        headerBytes = disk32::kMaterialWaterBytes;
        break;
    case DBAliasKind::MaterialTextureTable:
        if (metadata != materializedBytes
            || metadata < disk32::kMaterialTextureDefBytes
            || metadata > UINT32_C(255) * disk32::kMaterialTextureDefBytes
            || metadata % disk32::kMaterialTextureDefBytes != 0)
        {
            Com_Error(ERR_DROP, "Invalid completed material texture-table schema");
            return false;
        }
        headerBytes = metadata;
        break;
    default:
        Com_Error(ERR_DROP, "Unsupported completed fast-file object schema");
        return false;
    }

    const db::relocation::BlockMask aliasBlock =
        db::relocation::BlockBit(db::relocation::kAliasBlock);
    db::relocation::Status completionStatus =
        g_directResolver.ValidateAddress(
            reinterpret_cast<uintptr_t>(pointer),
            headerBytes,
            4,
            aliasBlock);
    if (completionStatus != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Fast-file completed object header is invalid: %s",
            db::relocation::StatusName(completionStatus));
        return false;
    }

    if (expectedKind == DBAliasKind::MaterialTechnique)
    {
        uint16_t passCount = 0;
        memcpy(
            &passCount,
            static_cast<const uint8_t *>(pointer) + 6,
            sizeof(passCount));
        uint32_t expectedBytes = 0;
        if (!db::validation::MaterialTechniqueDiskBytes(passCount, &expectedBytes)
            || materializedBytes != expectedBytes)
        {
            Com_Error(ERR_DROP, "Invalid completed material technique extent");
            return false;
        }
    }

    completionStatus = g_directResolver.ValidateAddress(
        reinterpret_cast<uintptr_t>(pointer),
        materializedBytes,
        4,
        aliasBlock);
    if (completionStatus != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Fast-file completed object span is invalid: %s",
            db::relocation::StatusName(completionStatus));
        return false;
    }

    completionStatus = g_aliasRegistry.Publish(
        handle,
        expectedKind,
        reinterpret_cast<uintptr_t>(pointer),
        metadata);
    if (completionStatus != db::relocation::Status::Ok)
    {
        Com_Error(
            ERR_DROP,
            "Cannot publish completed fast-file object: %s",
            db::relocation::StatusName(completionStatus));
        return false;
    }
    return true;
}

db::relocation::Status __cdecl DB_ResolveInsertedPointer(
    disk32::PointerToken token,
    DBAliasKind expectedKind,
    uint32_t expectedMetadata,
    uintptr_t *pointer)
{
    return g_aliasRegistry.Resolve(token, expectedKind, expectedMetadata, pointer);
}

db::relocation::Status __cdecl DB_MarkStreamRangeMaterialized(
    const void *pointer,
    uint32_t size)
{
    if (!pointer)
        return db::relocation::Status::InvalidArgument;

    return g_directResolver.MarkMaterialized(
        reinterpret_cast<uintptr_t>(pointer),
        size);
}

db::relocation::Status __cdecl DB_ValidateStreamAddress(
    const void *pointer,
    uint64_t requiredBytes,
    size_t alignment,
    db::relocation::BlockMask allowedBlocks)
{
    return g_directResolver.ValidateAddress(
        reinterpret_cast<uintptr_t>(pointer),
        requiredBytes,
        alignment,
        allowedBlocks);
}

db::relocation::Status __cdecl DB_RegisterStreamCString(
    const void *pointer,
    uint32_t byteCount)
{
    if (!pointer)
        return db::relocation::Status::InvalidArgument;

    return g_directResolver.RegisterCString(
        reinterpret_cast<uintptr_t>(pointer),
        byteCount);
}

db::relocation::Status __cdecl DB_ValidateStreamCString(
    const void *pointer,
    uint32_t *byteCount)
{
    return g_directResolver.ValidateCStringAddress(
        reinterpret_cast<uintptr_t>(pointer),
        byteCount);
}

db::relocation::Status __cdecl DB_ResolveOffsetBytes(
    disk32::PointerToken token,
    uint64_t requiredBytes,
    size_t alignment,
    db::relocation::BlockMask allowedBlocks,
    uintptr_t *pointer)
{
    return g_directResolver.ResolveBytes(
        token,
        requiredBytes,
        alignment,
        allowedBlocks,
        pointer);
}

db::relocation::Status __cdecl DB_ResolveOffsetCString(
    disk32::PointerToken token,
    db::relocation::BlockMask allowedBlocks,
    uintptr_t *pointer,
    uint32_t *byteCount)
{
    return g_directResolver.ResolveCString(
        token,
        allowedBlocks,
        pointer,
        byteCount);
}

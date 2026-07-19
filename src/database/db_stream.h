#pragma once

#include <database/db_disk32.h>
#include <database/db_relocation.h>
#include <database/db_stream_state.h>
#include <database/db_zone_memory.h>
#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

using DBAliasHandle = db::relocation::AliasHandle;
using DBAliasKind = db::relocation::AliasKind;

void __cdecl DB_InitStreams(XZoneMemory *zoneMem);
void __cdecl DB_PushStreamPos(std::uint32_t index);
void __cdecl DB_SetStreamIndex(std::uint32_t index);
void __cdecl DB_PopStreamPos();
std::uint8_t *__cdecl DB_GetStreamPos();
bool __cdecl DB_IsStreamRangeValid(const void *ptr, std::uint32_t size);
bool __cdecl DB_IsZoneRangeValid(const void *ptr, std::uint32_t size);
std::uint8_t *__cdecl DB_AllocStreamPos(std::int32_t alignment);
void __cdecl DB_IncStreamPos(std::int32_t size);
DBAliasHandle __cdecl DB_RegisterPointerSlot(
    const void *slot,
    DBAliasKind kind);
DBAliasHandle __cdecl DB_InsertPointer(DBAliasKind kind);
void __cdecl DB_SetInsertedPointer(
    DBAliasHandle handle,
    DBAliasKind expectedKind,
    const void *pointer,
    std::uint32_t metadata = 0);
bool __cdecl DB_CompleteObject(
    DBAliasHandle handle,
    DBAliasKind expectedKind,
    const void *pointer,
    std::uint32_t metadata,
    std::uint32_t materializedBytes);
db::relocation::Status __cdecl DB_ResolveInsertedPointer(
    disk32::PointerToken token,
    DBAliasKind expectedKind,
    std::uint32_t expectedMetadata,
    std::uintptr_t *pointer);
db::relocation::Status __cdecl DB_MarkStreamRangeMaterialized(
    const void *pointer,
    std::uint32_t size);
db::relocation::Status __cdecl DB_ValidateStreamAddress(
    const void *pointer,
    std::uint64_t requiredBytes,
    std::size_t alignment,
    db::relocation::BlockMask allowedBlocks);
db::relocation::Status __cdecl DB_RegisterStreamCString(
    const void *pointer,
    std::uint32_t byteCount);
db::relocation::Status __cdecl DB_ValidateStreamCString(
    const void *pointer,
    std::uint32_t *byteCount);
db::relocation::Status __cdecl DB_ResolveOffsetBytes(
    disk32::PointerToken token,
    std::uint64_t requiredBytes,
    std::size_t alignment,
    db::relocation::BlockMask allowedBlocks,
    std::uintptr_t *pointer);
db::relocation::Status __cdecl DB_ResolveOffsetCString(
    disk32::PointerToken token,
    db::relocation::BlockMask allowedBlocks,
    std::uintptr_t *pointer,
    std::uint32_t *byteCount);

void __cdecl Load_Stream(
    bool atStreamStart,
    std::uint8_t *ptr,
    std::int32_t size);
void __cdecl Load_StreamArray(
    bool atStreamStart,
    std::uint8_t *ptr,
    std::int32_t count,
    std::uint32_t stride);
void __cdecl Load_DelayStream();
void __cdecl DB_ConvertOffsetToAlias(
    std::uint32_t *data,
    DBAliasKind expectedKind,
    std::uint32_t expectedMetadata = 0);
void __cdecl DB_ConvertOffsetToPointer(
    std::uint32_t *data,
    std::uint64_t requiredBytes,
    std::size_t alignment,
    db::relocation::BlockMask allowedBlocks);
void __cdecl DB_ConvertOffsetToCString(
    std::uint32_t *data,
    db::relocation::BlockMask allowedBlocks);
void __cdecl DB_ConvertOffsetToTempString(
    std::uint32_t *data,
    db::relocation::BlockMask allowedBlocks);
std::uint32_t __cdecl Load_XStringCustom(char **str);
void __cdecl Load_TempStringCustom(char **str);

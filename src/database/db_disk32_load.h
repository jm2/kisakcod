#pragma once

// 64-bit asset loaders that read retail records through the generated disk32
// mirrors (docs/design/FASTFILE_LOADER.md). The 32-bit build keeps the
// in-place Load_* functions in db_load.cpp, so Windows x86 behaviour does not
// change; these exist only on 64-bit targets.

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

struct RawFile;
struct StringTable;
struct XAssetList;

#if KISAK_ARCH_64BIT
// Loads one RawFile asset header. slot is the native XAssetHeader slot: on
// entry it holds the asset's 4-byte disk32 pointer token, zero-extended; on
// return it holds the native RawFile published through Load_RawFileAsset.
// The disk32 record and its bytes are streamed exactly as the 32-bit loader
// streams them, so every block offset stays the retail one. Malformed input
// raises Com_Error(ERR_DROP) before anything reads past a checked extent.
void __cdecl DB_LoadRawFilePtrDisk32(bool atStreamStart, RawFile **slot);

// Loads one StringTable asset header under the same slot contract. The disk32
// record, its name and its value tokens stream into the current block (4), as
// the 32-bit loader streams them. The native table and its 8-byte value
// pointers live in DB_AllocZoneNative storage, and the record is published as
// a completed object whose offset tokens resolve to that native table.
void __cdecl DB_LoadStringTablePtrDisk32(bool atStreamStart, StringTable **slot);

// Native storage for loader output whose layout differs from its disk32
// bytes (docs/design/FASTFILE_LOADER.md, "Native arenas"). It is carved from
// the loading zone's physical-memory allocation, so it lives exactly as long as
// the zone's runtime blocks. Null when that memory is exhausted or no zone is
// loading, or when the request does not fit the allocator; the caller raises
// ERR_DROP, since exhaustion is not a fallback.
std::uint8_t *__cdecl DB_AllocZoneNative(std::size_t size, std::size_t alignment);

// Loads a zone's retail XAssetList (db_disk32_envelope.cpp) in place of
// Load_XAssetListCustom and Load_XAssetArrayCustom: the 16-byte root, its
// script strings, then the 8-byte XAsset records, each dispatched through
// Load_XAsset. list receives the native root, with each header slot as
// DB_LoadRawFilePtrDisk32 describes. A malformed root, script string or asset
// type raises ERR_DROP before any asset loads.
void __cdecl DB_LoadXAssetListDisk32(XAssetList *list);
#endif

#pragma once

// 64-bit asset loaders that read retail records through the generated disk32
// mirrors (docs/design/FASTFILE_LOADER.md). The 32-bit build keeps the
// in-place Load_* functions in db_load.cpp, so Windows x86 behaviour does not
// change; these exist only on 64-bit targets.

#include <universal/kisak_abi.h>

struct RawFile;
struct XAssetList;

#if KISAK_ARCH_64BIT
// Loads one RawFile asset header. slot is the native XAssetHeader slot: on
// entry it holds the asset's 4-byte disk32 pointer token, zero-extended; on
// return it holds the native RawFile published through Load_RawFileAsset.
// The disk32 record and its bytes are streamed exactly as the 32-bit loader
// streams them, so every block offset stays the retail one. Malformed input
// raises Com_Error(ERR_DROP) before anything reads past a checked extent.
void __cdecl DB_LoadRawFilePtrDisk32(bool atStreamStart, RawFile **slot);

// Loads a zone's retail XAssetList (db_disk32_envelope.cpp) in place of
// Load_XAssetListCustom and Load_XAssetArrayCustom: the 16-byte root, its
// script strings, then the 8-byte XAsset records, each dispatched through
// Load_XAsset. list receives the native root, with each header slot as
// DB_LoadRawFilePtrDisk32 describes. A malformed root, script string or asset
// type raises ERR_DROP before any asset loads.
void __cdecl DB_LoadXAssetListDisk32(XAssetList *list);
#endif

#pragma once

// 64-bit asset loaders that read retail records through the generated disk32
// mirrors (docs/design/FASTFILE_LOADER.md). The 32-bit build keeps the
// in-place Load_* functions in db_load.cpp, so Windows x86 behaviour does not
// change; these exist only on 64-bit targets.

#include <universal/kisak_abi.h>

struct RawFile;

#if KISAK_ARCH_64BIT
// Loads one RawFile asset header. slot is the native XAssetHeader slot: on
// entry it holds the asset's 4-byte disk32 pointer token, zero-extended; on
// return it holds the native RawFile published through Load_RawFileAsset.
// The disk32 record and its bytes are streamed exactly as the 32-bit loader
// streams them, so every block offset stays the retail one. Malformed input
// raises Com_Error(ERR_DROP) before anything reads past a checked extent.
void __cdecl DB_LoadRawFilePtrDisk32(bool atStreamStart, RawFile **slot);
#endif

struct MapEnts;

#if KISAK_ARCH_64BIT
// Loads one MapEnts (db_disk32_mapents.cpp) under the same slot contract: the
// slot holds the zero-extended disk32 token on entry and the native MapEnts
// published through Load_MapEntsAsset on return. The slot nested in clipMap_t
// takes the same contract once that family converts (wave 4).
void __cdecl DB_LoadMapEntsPtrDisk32(bool atStreamStart, MapEnts **slot);
#endif

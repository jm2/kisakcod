#pragma once

// 64-bit asset loaders that read retail records through the generated disk32
// mirrors (docs/design/FASTFILE_LOADER.md). The 32-bit build keeps the
// in-place Load_* functions in db_load.cpp, so Windows x86 behaviour does not
// change; these exist only on 64-bit targets.

#include <universal/kisak_abi.h>

#include <cstddef>
#include <cstdint>

struct ComWorld;
struct Font_s;
struct FxEffectDef;
struct FxImpactTable;
struct GameWorldMp;
struct GfxImage;
struct GfxLightDef;
struct LoadedSound;
struct LocalizeEntry;
struct MapEnts;
struct Material;
struct MaterialTechniqueSet;
struct MenuList;
struct PhysPreset;
struct RawFile;
struct SndCurve;
struct snd_alias_list_t;
struct StringTable;
struct XAnimParts;
struct XModel;
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

// Loads one PhysPreset (db_disk32_physpreset.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native PhysPreset published through Load_PhysPresetAsset on return. The
// retail bool byte converts as nonzero = true.
void __cdecl DB_LoadPhysPresetPtrDisk32(bool atStreamStart, PhysPreset **slot);

// Loads one LocalizeEntry (db_disk32_localize.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native LocalizeEntry published through Load_LocalizeEntryAsset on return.
void __cdecl DB_LoadLocalizeEntryPtrDisk32(bool atStreamStart, LocalizeEntry **slot);

// Loads one MapEnts (db_disk32_mapents.cpp) under the same slot contract: the
// slot holds the zero-extended disk32 token on entry and the native MapEnts
// published through Load_MapEntsAsset on return. The slot nested in clipMap_t
// takes the same contract once that family converts (wave 4).
void __cdecl DB_LoadMapEntsPtrDisk32(bool atStreamStart, MapEnts **slot);

// Loads one GfxImage (db_disk32_image.cpp) under the same slot contract: the
// slot holds the zero-extended disk32 token on entry and the native GfxImage
// published through Load_GfxImageAsset on return. A texture's load definition
// and pixels are streamed and checked in the temp block but never kept, so the
// native texture is null on every 64-bit target, as on the headless server.
void __cdecl DB_LoadGfxImagePtrDisk32(bool atStreamStart, GfxImage **slot);

// Loads one GameWorldMp (db_disk32_gameworldmp.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native GameWorldMp published through Load_GameWorldMpAsset on return.
void __cdecl DB_LoadGameWorldMpPtrDisk32(bool atStreamStart, GameWorldMp **slot);

// Loads one SndCurve (db_disk32_soundcurve.cpp) under the same slot contract:
// the slot holds the zero-extended disk32 token on entry and the native
// SndCurve published through Load_SndCurveAsset on return.
void __cdecl DB_LoadSndCurvePtrDisk32(bool atStreamStart, SndCurve **slot);

// Loads one ComWorld (db_disk32_comworld.cpp) under the same slot contract:
// the slot holds the zero-extended disk32 token on entry and the native
// ComWorld published through Load_ComWorldAsset on return. Its primary lights
// live in DB_AllocZoneNative storage.
void __cdecl DB_LoadComWorldPtrDisk32(bool atStreamStart, ComWorld **slot);

// Loads one LoadedSound (db_disk32_loadedsound.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native LoadedSound published through Load_LoadedSoundAsset on return. As
// the 32-bit headless load, the sound data is streamed and its aliases are
// checked, but the native record owns no playback buffer.
void __cdecl DB_LoadLoadedSoundPtrDisk32(bool atStreamStart, LoadedSound **slot);

// Loads one XAnimParts (db_disk32_xanimparts.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native XAnimParts published through Load_XAnimPartsAsset on return. Its
// arrays stay in block 4, and its bone and notetrack names hold interned
// script-string ids.
void __cdecl DB_LoadXAnimPartsPtrDisk32(bool atStreamStart, XAnimParts **slot);

// Loads one GfxLightDef (db_disk32_lightdef.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native GfxLightDef published through Load_LightDefAsset on return. Its
// attenuation image loads as DB_LoadGfxImagePtrDisk32 loads a header.
void __cdecl DB_LoadGfxLightDefPtrDisk32(bool atStreamStart, GfxLightDef **slot);

// Loads one FxImpactTable (db_disk32_impactfx.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native FxImpactTable published through Load_FxImpactTableAsset on return.
// Its 12 entries live in DB_AllocZoneNative storage. FX has no 64-bit loader
// yet, so a non-null effect token raises ERR_DROP.
void __cdecl DB_LoadFxImpactTablePtrDisk32(bool atStreamStart, FxImpactTable **slot);

// Loads one Font_s (db_disk32_font.cpp) under the same slot contract: the
// slot holds the zero-extended disk32 token on entry and the native Font_s
// published through Load_FontAsset on return. Its glyphs keep their layout and
// stay in block 4. Material has no 64-bit loader yet, so a non-null material
// token raises ERR_DROP.
void __cdecl DB_LoadFontPtrDisk32(bool atStreamStart, Font_s **slot);

// Loads one MenuList (db_disk32_menulist.cpp) under the same slot contract:
// the slot holds the zero-extended disk32 token on entry and the native
// MenuList published through Load_MenuListAsset on return. Its menu pointers
// live in DB_AllocZoneNative storage. Menu has no 64-bit loader yet, so a
// non-null menu token raises ERR_DROP.
void __cdecl DB_LoadMenuListPtrDisk32(bool atStreamStart, MenuList **slot);

// Loads one snd_alias_list_t (db_disk32_sound.cpp) under the same slot
// contract: the slot holds the zero-extended disk32 token on entry and the
// native list published through Load_snd_alias_list_Asset on return. Its
// aliases, sound files and speaker maps live in DB_AllocZoneNative storage,
// and their offset tokens resolve to those native objects.
void __cdecl DB_LoadSndAliasListPtrDisk32(bool atStreamStart, snd_alias_list_t **slot);

// Loads one MaterialTechniqueSet (db_disk32_techniqueset.cpp) under the same
// slot contract: the slot holds the zero-extended disk32 token on entry and
// the native set published through Load_MaterialTechniqueSetAsset on return.
// Its techniques and vertex declarations live in DB_AllocZoneNative storage.
void __cdecl DB_LoadMaterialTechniqueSetPtrDisk32(bool atStreamStart, MaterialTechniqueSet **slot);

// Loads one Material (db_disk32_material.cpp) under the same slot contract:
// the slot holds the zero-extended disk32 token on entry and the native
// Material published through Load_MaterialAsset on return. Its technique set
// loads through TechniqueSet's step; its constants and state bits stay in
// block 4.
void __cdecl DB_LoadMaterialPtrDisk32(bool atStreamStart, Material **slot);

// Loads one XModel (db_disk32_xmodel.cpp) under the same slot contract: the
// slot holds the zero-extended disk32 token on entry and the native XModel
// published through Load_XModelAsset on return. Its bone arrays stay in
// block 4, bone names as interned script-string ids.
void __cdecl DB_LoadXModelPtrDisk32(bool atStreamStart, XModel **slot);

// Loads one FX effect (db_disk32_fx.cpp) under the same slot contract: the
// slot holds the zero-extended disk32 token on entry and the native
// FxEffectDef published through Load_FxEffectDefAsset on return. Its elements
// live in DB_AllocZoneNative storage; their samples stay in block 4.
void __cdecl DB_LoadFxEffectDefHandleDisk32(bool atStreamStart, const FxEffectDef **slot);

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

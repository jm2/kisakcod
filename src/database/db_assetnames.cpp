#include "database.h"
#include <game/g_bsp.h>

// Asset size/name handlers below take sizeof() of the renderer-held asset
// records; xanim.h holds them by pointer only since the D3D include cut
// (KPI K5), so their complete types come from the renderer headers directly.
#include <gfx_d3d/r_bsp.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_gfx.h>
#include <gfx_d3d/r_material.h>

#include <type_traits>

//int32_t marker_db_assetnames 828ddeec     db_assetnames.obj

const char *(__cdecl *DB_XAssetGetNameHandler[33])(const XAssetHeader *) =
{
    // KISAKTODO: these got Identical COMDAT folded into 1 function because name is usually the 1st field.
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_ImageGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    0,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_StringTableGetName,
    DB_LocalizeEntryGetName,
    DB_StringTableGetName,
    0,
    DB_StringTableGetName,
    DB_StringTableGetName,
    0,
    0,
    0,
    0,
    DB_StringTableGetName,
    DB_StringTableGetName
};

void(__cdecl *DB_XAssetSetNameHandler[33])(XAssetHeader *, const char *) =
{
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_ImageSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    0,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_StringTableSetName,
    DB_LocalizeEntrySetName,
    DB_StringTableSetName,
    0,
    DB_StringTableSetName,
    DB_StringTableSetName,
    0,
    0,
    0,
    0,
    DB_StringTableSetName,
    DB_StringTableSetName
};

// One size per asset type, taken from its XAssetHeader member. The retail
// table inherited the compiler's identical-function folding, so types of equal
// ILP32 size shared one function; five of those aliases (PhysPreset,
// LoadedSound, both clip maps, LightDef) are the wrong size at 64-bit
// (docs/design/FASTFILE_LOADER.md, "Clone size table").
template <typename T>
static int32_t __cdecl DB_SizeofXAsset()
{
    return static_cast<int32_t>(sizeof(T));
}
#define DB_SIZEOF_XASSET(member) DB_SizeofXAsset<std::remove_pointer_t<decltype(XAssetHeader::member)>>

int(__cdecl *DB_GetXAssetSizeHandler[33])() =
{
    DB_SIZEOF_XASSET(xmodelPieces),
    DB_SIZEOF_XASSET(physPreset),
    DB_SIZEOF_XASSET(parts),
    DB_SIZEOF_XASSET(model),
    DB_SIZEOF_XASSET(material),
    DB_SIZEOF_XASSET(techniqueSet),
    DB_SIZEOF_XASSET(image),
    DB_SIZEOF_XASSET(sound),
    DB_SIZEOF_XASSET(sndCurve),
    DB_SIZEOF_XASSET(loadSnd),
    DB_SIZEOF_XASSET(clipMap),
    DB_SIZEOF_XASSET(clipMap), // ASSET_TYPE_CLIPMAP_PVS
    DB_SIZEOF_XASSET(comWorld),
    DB_SIZEOF_XASSET(gameWorldSp),
    DB_SIZEOF_XASSET(gameWorldMp),
    DB_SIZEOF_XASSET(mapEnts),
    DB_SIZEOF_XASSET(gfxWorld),
    DB_SIZEOF_XASSET(lightDef),
    0,
    DB_SIZEOF_XASSET(font),
    DB_SIZEOF_XASSET(menuList),
    DB_SIZEOF_XASSET(menu),
    DB_SIZEOF_XASSET(localize),
    DB_SIZEOF_XASSET(weapon),
    0,
    DB_SIZEOF_XASSET(fx),
    DB_SIZEOF_XASSET(impactFx),
    0,
    0,
    0,
    0,
    DB_SIZEOF_XASSET(rawfile),
    DB_SIZEOF_XASSET(stringTable),
};

void __cdecl DB_StringTableSetName(XAssetHeader *header, const char *name)
{
    header->xmodelPieces->name = name;
}

const char *__cdecl DB_ImageGetName(const XAssetHeader *header)
{
    return header->image->name;
}

void __cdecl DB_ImageSetName(XAssetHeader *header, const char *name)
{
    //header->xmodelPieces[2].pieces = name;
    //header->xmodelPieces[2].name = name;
    header->image->name = name;
}

const char *__cdecl DB_StringTableGetName(const XAssetHeader *header)
{
    return header->stringTable->name;
}

const char *__cdecl DB_LocalizeEntryGetName(const XAssetHeader *header)
{
    return header->localize->name;
}

void __cdecl DB_LocalizeEntrySetName(XAssetHeader *header, const char *name)
{
    header->localize->name = name;
}

const char *__cdecl DB_GetXAssetHeaderName(int32_t type, const XAssetHeader *header)
{
    const char *name; // [esp+0h] [ebp-4h]

    iassert(header);
    iassert(DB_XAssetGetNameHandler[type]);
    iassert(header->data);

    name = DB_XAssetGetNameHandler[type](header);

    iassert(name);
    //if (!name)
    //{
    //    MyAssertHandler(".\\database\\db_assetnames.cpp", 594, 0, "%s\n\t%s", "name", 
    //      va("Name not found for asset type %s\n", g_assetNames[type]));
    //}
    return name;
}

const char *__cdecl DB_GetXAssetName(const XAsset *asset)
{
    iassert(asset);
    return DB_GetXAssetHeaderName(asset->type, &asset->header);
}

void __cdecl DB_SetXAssetName(XAsset *asset, const char *name)
{
    if (!DB_XAssetSetNameHandler[asset->type])
        MyAssertHandler(".\\database\\db_assetnames.cpp", 608, 0, "%s", "DB_XAssetSetNameHandler[asset->type]");
    DB_XAssetSetNameHandler[asset->type](&asset->header, name);
}

int32_t __cdecl DB_GetXAssetTypeSize(int32_t type)
{
    if (!DB_GetXAssetSizeHandler[type])
        MyAssertHandler(".\\database\\db_assetnames.cpp", 615, 0, "%s", "DB_GetXAssetSizeHandler[type]");
    return DB_GetXAssetSizeHandler[type]();
}

const char *__cdecl DB_GetXAssetTypeName(uint32_t type)
{
    if (type > 0x20)
        MyAssertHandler(".\\database\\db_assetnames.cpp", 621, 0, "%s", "type >= 0 && type < ASSET_TYPE_COUNT");
    return g_assetNames[type];
}


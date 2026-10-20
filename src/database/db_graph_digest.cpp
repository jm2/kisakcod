#include "database/db_graph_digest.h"

#include "database/database.h"

#include <game/g_bsp.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/gfx_lightdef.h>
#include <universal/q_shared.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
using db::graph_hash::Digest;
using db::graph_hash::GraphHashBuilder;

// The family record that frames the per-asset digests.
constexpr std::uint32_t kFamilyRecord = 0x4B344641u; // "K4FA"
enum FamilyField : std::uint32_t
{
    kFamilyType = 1,
    kFamilyCount,
    kFamilyAsset,
};

enum RawFileField : std::uint32_t
{
    kRawFileName = 1,
    kRawFileLen,
    kRawFileHasBuffer,
    kRawFileBuffer,
};

// A reference to another asset is its name: the referenced asset has its own
// family digest.
void FieldAsset(GraphHashBuilder &builder, std::uint32_t tag, XAssetType type, const void *asset)
{
    XAssetHeader header;
    header.data = const_cast<void *>(asset);
    builder.FieldString(tag, asset ? DB_GetXAssetHeaderName(type, &header) : nullptr);
}

// The buffer holds len bytes and a terminator; the bytes are the content.
void HashRawFile(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const RawFile &raw = *header.rawfile;
    builder.FieldString(kRawFileName, raw.name);
    builder.FieldI64(kRawFileLen, raw.len);
    builder.FieldU64(kRawFileHasBuffer, raw.buffer != nullptr);
    builder.FieldBytes(kRawFileBuffer, raw.buffer, raw.buffer && raw.len > 0 ? static_cast<std::size_t>(raw.len) : 0);
}

enum StringTableField : std::uint32_t
{
    kStringTableName = 1,
    kStringTableColumns,
    kStringTableRows,
    kStringTableValue,
};

// Row-major values, as the table stores them.
void HashStringTable(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const StringTable &table = *header.stringTable;
    builder.FieldString(kStringTableName, table.name);
    builder.FieldI64(kStringTableColumns, table.columnCount);
    builder.FieldI64(kStringTableRows, table.rowCount);
    const int values = table.values && table.columnCount > 0 && table.rowCount > 0 ? table.columnCount * table.rowCount : 0;
    for (int i = 0; i < values; ++i)
        builder.FieldString(kStringTableValue, table.values[i]);
}

enum PhysPresetField : std::uint32_t
{
    kPhysPresetName = 1,
    kPhysPresetType,
    kPhysPresetMass,
    kPhysPresetBounce,
    kPhysPresetFriction,
    kPhysPresetBulletForceScale,
    kPhysPresetExplosiveForceScale,
    kPhysPresetSndAliasPrefix,
    kPhysPresetPiecesSpreadFraction,
    kPhysPresetPiecesUpwardVelocity,
    kPhysPresetTempDefaultToCylinder,
};

void HashPhysPreset(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const PhysPreset &preset = *header.physPreset;
    builder.FieldString(kPhysPresetName, preset.name);
    builder.FieldI64(kPhysPresetType, preset.type);
    builder.FieldF32(kPhysPresetMass, preset.mass);
    builder.FieldF32(kPhysPresetBounce, preset.bounce);
    builder.FieldF32(kPhysPresetFriction, preset.friction);
    builder.FieldF32(kPhysPresetBulletForceScale, preset.bulletForceScale);
    builder.FieldF32(kPhysPresetExplosiveForceScale, preset.explosiveForceScale);
    builder.FieldString(kPhysPresetSndAliasPrefix, preset.sndAliasPrefix);
    builder.FieldF32(kPhysPresetPiecesSpreadFraction, preset.piecesSpreadFraction);
    builder.FieldF32(kPhysPresetPiecesUpwardVelocity, preset.piecesUpwardVelocity);
    builder.FieldU64(kPhysPresetTempDefaultToCylinder, preset.tempDefaultToCylinder);
}

enum LocalizeEntryField : std::uint32_t
{
    kLocalizeEntryName = 1,
    kLocalizeEntryValue,
};

void HashLocalizeEntry(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const LocalizeEntry &entry = *header.localize;
    builder.FieldString(kLocalizeEntryName, entry.name);
    builder.FieldString(kLocalizeEntryValue, entry.value);
}

enum MapEntsField : std::uint32_t
{
    kMapEntsName = 1,
    kMapEntsChars,
    kMapEntsString,
};

// numEntityChars counts the terminator; those bytes are the content.
void HashMapEnts(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const MapEnts &ents = *header.mapEnts;
    builder.FieldString(kMapEntsName, ents.name);
    builder.FieldI64(kMapEntsChars, ents.numEntityChars);
    builder.FieldBytes(kMapEntsString, ents.entityString,
                       ents.entityString && ents.numEntityChars > 0 ? static_cast<std::size_t>(ents.numEntityChars) : 0);
}

enum SndCurveField : std::uint32_t
{
    kSndCurveName = 1,
    kSndCurveKnotCount,
    kSndCurveKnot,
};

// Only the first knotCount knots are the curve.
void HashSndCurve(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const SndCurve &curve = *header.sndCurve;
    builder.FieldString(kSndCurveName, curve.filename);
    builder.FieldI64(kSndCurveKnotCount, curve.knotCount);
    const int knots = curve.knotCount < 0 ? 0 : curve.knotCount > 8 ? 8 : curve.knotCount;
    for (int i = 0; i < knots; ++i)
    {
        builder.FieldF32(kSndCurveKnot, curve.knots[i][0]);
        builder.FieldF32(kSndCurveKnot, curve.knots[i][1]);
    }
}

enum GameWorldMpField : std::uint32_t
{
    kGameWorldMpName = 1,
};

void HashGameWorldMp(GraphHashBuilder &builder, const XAssetHeader &header)
{
    builder.FieldString(kGameWorldMpName, header.gameWorldMp->name);
}

enum LightDefField : std::uint32_t
{
    kLightDefName = 1,
    kLightDefImage,
    kLightDefSamplerState,
    kLightDefLmapLookupStart,
};

void HashLightDef(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const GfxLightDef &light = *header.lightDef;
    builder.FieldString(kLightDefName, light.name);
    FieldAsset(builder, kLightDefImage, ASSET_TYPE_IMAGE, light.attenuation.image);
    builder.FieldU64(kLightDefSamplerState, light.attenuation.samplerState);
    builder.FieldI64(kLightDefLmapLookupStart, light.lmapLookupStart);
}

using Walker = void (*)(GraphHashBuilder &, const XAssetHeader &);

Walker WalkerFor(const std::int32_t assetType)
{
    switch (assetType)
    {
    case ASSET_TYPE_RAWFILE:
        return HashRawFile;
    case ASSET_TYPE_STRINGTABLE:
        return HashStringTable;
    case ASSET_TYPE_PHYSPRESET:
        return HashPhysPreset;
    case ASSET_TYPE_LOCALIZE_ENTRY:
        return HashLocalizeEntry;
    case ASSET_TYPE_MAP_ENTS:
        return HashMapEnts;
    case ASSET_TYPE_SOUND_CURVE:
        return HashSndCurve;
    case ASSET_TYPE_GAMEWORLD_MP:
        return HashGameWorldMp;
    case ASSET_TYPE_LIGHT_DEF:
        return HashLightDef;
    default:
        return nullptr;
    }
}

void __cdecl CollectAsset(XAssetHeader header, void *assets)
{
    static_cast<std::vector<XAssetHeader> *>(assets)->push_back(header);
}

void PrintAsset(const char *name, const Digest &digest)
{
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "  %s %s\n", hex, name);
}
} // namespace

Digest DB_GraphHashAsset(const std::int32_t assetType, const XAssetHeader &header)
{
    GraphHashBuilder builder;
    builder.BeginRecord(static_cast<std::uint32_t>(assetType));
    if (const Walker walker = WalkerFor(assetType))
        walker(builder, header);
    builder.EndRecord();
    return builder.Finish();
}

int DB_GraphHashFamily(
    const std::int32_t assetType,
    Digest &digest,
    void (*perAsset)(const char *name, const Digest &digest))
{
    if (!WalkerFor(assetType))
        return -1;

    // Pool order follows the name hash and the load order; name order is the
    // same on every target.
    std::vector<XAssetHeader> assets;
    DB_EnumXAssets(static_cast<XAssetType>(assetType), CollectAsset, &assets, false);
    std::sort(assets.begin(), assets.end(), [assetType](const XAssetHeader &a, const XAssetHeader &b) {
        return std::strcmp(DB_GetXAssetHeaderName(assetType, &a), DB_GetXAssetHeaderName(assetType, &b)) < 0;
    });

    GraphHashBuilder family;
    family.BeginRecord(kFamilyRecord);
    family.FieldU64(kFamilyType, static_cast<std::uint64_t>(assetType));
    family.FieldU64(kFamilyCount, assets.size());
    for (const XAssetHeader &header : assets)
    {
        const Digest asset = DB_GraphHashAsset(assetType, header);
        if (perAsset)
            perAsset(DB_GetXAssetHeaderName(assetType, &header), asset);
        family.FieldBytes(kFamilyAsset, asset.data(), asset.size());
    }
    family.EndRecord();
    digest = family.Finish();
    return static_cast<int>(assets.size());
}

void __cdecl DB_GraphHash_f()
{
    const char *const family = Cmd_Argv(1);
    std::int32_t type = 0;
    while (type < ASSET_TYPE_COUNT && I_stricmp(family, DB_GetXAssetTypeName(type)))
        ++type;
    const bool list = Cmd_Argc() > 2 && !I_stricmp(Cmd_Argv(2), "list");

    Digest digest{};
    const int count = type < ASSET_TYPE_COUNT ? DB_GraphHashFamily(type, digest, list ? PrintAsset : nullptr) : -1;
    if (count < 0)
    {
        Com_Printf(CON_CHANNEL_DONT_FILTER, "usage: db_graphhash <family> [list]; families:");
        for (type = 0; type < ASSET_TYPE_COUNT; ++type)
        {
            if (WalkerFor(type))
                Com_Printf(CON_CHANNEL_DONT_FILTER, " %s", DB_GetXAssetTypeName(type));
        }
        Com_Printf(CON_CHANNEL_DONT_FILTER, "\n");
        return;
    }
    char hex[db::graph_hash::kHexDigestBytes];
    db::graph_hash::FormatDigestHex(digest, hex);
    Com_Printf(CON_CHANNEL_DONT_FILTER, "db_graphhash %s: %d assets, sha256 %s\n", DB_GetXAssetTypeName(type), count, hex);
}

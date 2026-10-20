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

enum FontField : std::uint32_t
{
    kFontName = 1,
    kFontPixelHeight,
    kFontGlyphCount,
    kFontMaterial,
    kFontGlowMaterial,
    kFontGlyph,
};

void HashFont(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const Font_s &font = *header.font;
    builder.FieldString(kFontName, font.fontName);
    builder.FieldI64(kFontPixelHeight, font.pixelHeight);
    builder.FieldI64(kFontGlyphCount, font.glyphCount);
    FieldAsset(builder, kFontMaterial, ASSET_TYPE_MATERIAL, font.material);
    FieldAsset(builder, kFontGlowMaterial, ASSET_TYPE_MATERIAL, font.glowMaterial);
    const int glyphs = font.glyphs && font.glyphCount > 0 ? font.glyphCount : 0;
    for (int i = 0; i < glyphs; ++i)
    {
        const Glyph &glyph = font.glyphs[i];
        builder.BeginRecord(kFontGlyph);
        builder.FieldU64(1, glyph.letter);
        builder.FieldI64(2, glyph.x0);
        builder.FieldI64(3, glyph.y0);
        builder.FieldU64(4, glyph.dx);
        builder.FieldU64(5, glyph.pixelWidth);
        builder.FieldU64(6, glyph.pixelHeight);
        builder.FieldF32(7, glyph.s0);
        builder.FieldF32(8, glyph.t0);
        builder.FieldF32(9, glyph.s1);
        builder.FieldF32(10, glyph.t1);
        builder.EndRecord();
    }
}

enum MenuListField : std::uint32_t
{
    kMenuListName = 1,
    kMenuListCount,
    kMenuListMenu,
};

// The menus are named references: each is its own Menu asset.
void HashMenuList(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const MenuList &list = *header.menuList;
    builder.FieldString(kMenuListName, list.name);
    builder.FieldI64(kMenuListCount, list.menuCount);
    const int menus = list.menus && list.menuCount > 0 ? list.menuCount : 0;
    for (int i = 0; i < menus; ++i)
        FieldAsset(builder, kMenuListMenu, ASSET_TYPE_MENU, list.menus[i]);
}

enum ComWorldField : std::uint32_t
{
    kComWorldName = 1,
    kComWorldInUse,
    kComWorldLightCount,
    kComWorldLight,
};

void HashComWorld(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const ComWorld &world = *header.comWorld;
    builder.FieldString(kComWorldName, world.name);
    builder.FieldI64(kComWorldInUse, world.isInUse);
    builder.FieldU64(kComWorldLightCount, world.primaryLightCount);
    const std::uint32_t lights = world.primaryLights ? world.primaryLightCount : 0;
    for (std::uint32_t i = 0; i < lights; ++i)
    {
        const ComPrimaryLight &light = world.primaryLights[i];
        builder.BeginRecord(kComWorldLight);
        builder.FieldU64(1, light.type);
        builder.FieldU64(2, light.canUseShadowMap);
        builder.FieldU64(3, light.exponent);
        builder.FieldU64(4, light.unused);
        for (const float value : light.color)
            builder.FieldF32(5, value);
        for (const float value : light.dir)
            builder.FieldF32(6, value);
        for (const float value : light.origin)
            builder.FieldF32(7, value);
        builder.FieldF32(8, light.radius);
        builder.FieldF32(9, light.cosHalfFovOuter);
        builder.FieldF32(10, light.cosHalfFovInner);
        builder.FieldF32(11, light.cosHalfFovExpanded);
        builder.FieldF32(12, light.rotationLimit);
        builder.FieldF32(13, light.translationLimit);
        builder.FieldString(14, light.defName);
        builder.EndRecord();
    }
}

enum LoadedSoundField : std::uint32_t
{
    kLoadedSoundName = 1,
    kLoadedSoundFormat,
    kLoadedSoundDataLen,
    kLoadedSoundRate,
    kLoadedSoundBits,
    kLoadedSoundChannels,
    kLoadedSoundSamples,
    kLoadedSoundBlockSize,
    kLoadedSoundHasData,
    kLoadedSoundData,
};

// The info pointers are run-time addresses; data_len bytes of data are the content.
void HashLoadedSound(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const LoadedSound &sound = *header.loadSnd;
    const _AILSOUNDINFO_COD4 &info = sound.sound.info;
    builder.FieldString(kLoadedSoundName, sound.name);
    builder.FieldI64(kLoadedSoundFormat, info.format);
    builder.FieldU64(kLoadedSoundDataLen, info.data_len);
    builder.FieldU64(kLoadedSoundRate, info.rate);
    builder.FieldI64(kLoadedSoundBits, info.bits);
    builder.FieldI64(kLoadedSoundChannels, info.channels);
    builder.FieldU64(kLoadedSoundSamples, info.samples);
    builder.FieldU64(kLoadedSoundBlockSize, info.block_size);
    builder.FieldU64(kLoadedSoundHasData, sound.sound.data != nullptr);
    builder.FieldBytes(kLoadedSoundData, sound.sound.data, sound.sound.data ? info.data_len : 0);
}

// A sound file is its type plus its loaded sound's name, or its streamed
// file's directory and name (SAT_LOADED 1, SAT_STREAMED 2).
void HashSoundFile(GraphHashBuilder &builder, const SoundFile *file)
{
    builder.FieldU64(5, file != nullptr);
    if (!file)
        return;
    builder.FieldU64(6, file->type);
    builder.FieldU64(7, file->exists);
    if (file->type == 1)
        FieldAsset(builder, 8, ASSET_TYPE_LOADED_SOUND, file->u.loadSnd);
    else
    {
        builder.FieldString(9, file->u.streamSnd.filename.info.raw.dir);
        builder.FieldString(10, file->u.streamSnd.filename.info.raw.name);
    }
}

// The speaker map is inline in the alias, not an asset: every field of it.
void HashSpeakerMap(GraphHashBuilder &builder, const SpeakerMap *map)
{
    builder.FieldU64(26, map != nullptr);
    if (!map)
        return;
    builder.FieldU64(27, map->isDefault);
    builder.FieldString(28, map->name);
    for (const MSSChannelMap(&row)[2] : map->channelMaps)
    {
        for (const MSSChannelMap &channels : row)
        {
            builder.FieldI64(29, channels.speakerCount);
            for (const MSSSpeakerLevels &speaker : channels.speakers)
            {
                builder.FieldI64(30, speaker.speaker);
                builder.FieldI64(31, speaker.numLevels);
                builder.FieldF32(32, speaker.levels[0]);
                builder.FieldF32(33, speaker.levels[1]);
            }
        }
    }
}

// Each alias in order, as a record: its names, its sound file, its scalars,
// its falloff curve by name, and its speaker map.
void HashSound(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const snd_alias_list_t &list = *header.sound;
    builder.FieldString(1, list.aliasName);
    builder.FieldI64(2, list.count);
    const int aliases = list.head && list.count > 0 ? list.count : 0;
    for (int i = 0; i < aliases; ++i)
    {
        const snd_alias_t &alias = list.head[i];
        builder.BeginRecord(3);
        builder.FieldString(1, alias.aliasName);
        builder.FieldString(2, alias.subtitle);
        builder.FieldString(3, alias.secondaryAliasName);
        builder.FieldString(4, alias.chainAliasName);
        HashSoundFile(builder, alias.soundFile);
        builder.FieldI64(11, alias.sequence);
        for (const float value : {alias.volMin, alias.volMax, alias.pitchMin, alias.pitchMax, alias.distMin, alias.distMax})
            builder.FieldF32(12, value);
        builder.FieldI64(13, alias.flags);
        for (const float value : {alias.slavePercentage, alias.probability, alias.lfePercentage, alias.centerPercentage})
            builder.FieldF32(14, value);
        builder.FieldI64(15, alias.startDelay);
        FieldAsset(builder, 16, ASSET_TYPE_SOUND_CURVE, alias.volumeFalloffCurve);
        for (const float value : {alias.envelopMin, alias.envelopMax, alias.envelopPercentage})
            builder.FieldF32(17, value);
        HashSpeakerMap(builder, alias.speakerMap);
        builder.EndRecord();
    }
}

// A bool's stored byte, as 0 or 1: a retail byte other than 1 reads as true
// on every target, but loading it as bool would be undefined.
void FieldBool(GraphHashBuilder &builder, std::uint32_t tag, const bool &value)
{
    // A bool's object representation may be read through unsigned char.
    const unsigned char raw = *reinterpret_cast<const unsigned char *>(&value);
    builder.FieldU64(tag, raw != 0);
}

// A script string is its text: each load assigns its own indices.
void FieldScriptString(GraphHashBuilder &builder, std::uint32_t tag, std::uint32_t index)
{
    builder.FieldString(tag, index ? SL_ConvertToString(index) : nullptr);
}

// Frame indices are 16-bit once an animation has 256 frames.
std::size_t XAnimIndexWidth(const XAnimParts &parts)
{
    return parts.numframes >= 0x100 ? 2 : 1;
}

// A delta translation: frame0 when its size is 0, else its bounds, size + 1
// indices and size + 1 vectors (8-bit when smallTrans).
void HashXAnimTrans(GraphHashBuilder &builder, const XAnimPartTrans *trans, std::size_t indexWidth)
{
    builder.FieldU64(1, trans != nullptr);
    if (!trans)
        return;
    builder.FieldU64(2, trans->size);
    builder.FieldU64(3, trans->smallTrans);
    if (!trans->size)
    {
        builder.FieldBytes(4, trans->u.frame0, sizeof(trans->u.frame0));
        return;
    }
    const std::size_t frames = trans->size + 1u;
    builder.FieldBytes(5, trans->u.frames.mins, sizeof(trans->u.frames.mins));
    builder.FieldBytes(6, trans->u.frames.size, sizeof(trans->u.frames.size));
    builder.FieldBytes(7, &trans->u.frames.indices, frames * indexWidth);
    builder.FieldBytes(8, trans->u.frames.frames._1, trans->u.frames.frames._1 ? frames * 3 * (trans->smallTrans ? 1 : 2) : 0);
}

// A delta rotation: frame0 when its size is 0, else size + 1 indices and size + 1 pairs.
void HashXAnimQuat(GraphHashBuilder &builder, const XAnimDeltaPartQuat *quat, std::size_t indexWidth)
{
    builder.FieldU64(9, quat != nullptr);
    if (!quat)
        return;
    builder.FieldU64(10, quat->size);
    if (!quat->size)
    {
        builder.FieldBytes(11, quat->u.frame0, sizeof(quat->u.frame0));
        return;
    }
    const std::size_t frames = quat->size + 1u;
    builder.FieldBytes(12, &quat->u.frames.indices, frames * indexWidth);
    builder.FieldBytes(13, quat->u.frames.frames, quat->u.frames.frames ? frames * sizeof(*quat->u.frames.frames) : 0);
}

// count elements of a layout-invariant array as bytes; none when it is null.
template <typename T>
void FieldArray(GraphHashBuilder &builder, std::uint32_t tag, const T *data, std::size_t count)
{
    builder.FieldBytes(tag, data, data ? count * sizeof(T) : 0);
}

// The data and index arrays, each as the bytes its count covers.
void HashXAnimArrays(GraphHashBuilder &builder, const XAnimParts &parts, std::size_t indexWidth)
{
    FieldArray(builder, 14, parts.dataByte, parts.dataByteCount);
    FieldArray(builder, 15, parts.dataShort, parts.dataShortCount);
    FieldArray(builder, 16, parts.dataInt, parts.dataIntCount);
    FieldArray(builder, 17, parts.randomDataShort, parts.randomDataShortCount);
    FieldArray(builder, 18, parts.randomDataByte, parts.randomDataByteCount);
    FieldArray(builder, 19, parts.randomDataInt, parts.randomDataIntCount);
    builder.FieldBytes(20, parts.indices.data, parts.indices.data ? parts.indexCount * indexWidth : 0);
}

// The notetracks: each name as text and its time.
void HashXAnimNotify(GraphHashBuilder &builder, const XAnimParts &parts)
{
    const int notes = parts.notify ? parts.notifyCount : 0;
    for (int i = 0; i < notes; ++i)
    {
        const auto &[noteName, noteTime] = parts.notify[i];
        builder.BeginRecord(21);
        FieldScriptString(builder, 1, noteName);
        builder.FieldF32(2, noteTime);
        builder.EndRecord();
    }
}

// The header scalars, the bone and notetrack names as text, and every array
// as the bytes its count covers (the counts the loader streams).
void HashXAnimParts(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const XAnimParts &parts = *header.parts;
    builder.FieldString(1, parts.name);
    for (const unsigned count : {parts.dataByteCount, parts.dataShortCount, parts.dataIntCount,
                                 parts.randomDataByteCount, parts.randomDataIntCount, parts.numframes})
        builder.FieldU64(2, count);
    FieldBool(builder, 3, parts.bLoop);
    FieldBool(builder, 4, parts.bDelta);
    builder.FieldBytes(5, parts.boneCount, sizeof(parts.boneCount));
    builder.FieldU64(6, parts.notifyCount);
    builder.FieldU64(7, parts.assetType);
    FieldBool(builder, 8, parts.isDefault);
    builder.FieldU64(9, parts.randomDataShortCount);
    builder.FieldU64(10, parts.indexCount);
    builder.FieldF32(11, parts.framerate);
    builder.FieldF32(12, parts.frequency);
    const int bones = parts.names ? parts.boneCount[9] : 0;
    for (int i = 0; i < bones; ++i)
        FieldScriptString(builder, 13, parts.names[i]);
    const std::size_t indexWidth = XAnimIndexWidth(parts);
    HashXAnimArrays(builder, parts, indexWidth);
    HashXAnimNotify(builder, parts);
    builder.FieldU64(22, parts.deltaPart != nullptr);
    if (parts.deltaPart)
    {
        builder.BeginRecord(23);
        HashXAnimTrans(builder, parts.deltaPart->trans, indexWidth);
        HashXAnimQuat(builder, parts.deltaPart->quat, indexWidth);
        builder.EndRecord();
    }
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
    case ASSET_TYPE_FONT:
        return HashFont;
    case ASSET_TYPE_MENULIST:
        return HashMenuList;
    case ASSET_TYPE_COMWORLD:
        return HashComWorld;
    case ASSET_TYPE_LOADED_SOUND:
        return HashLoadedSound;
    case ASSET_TYPE_SOUND:
        return HashSound;
    case ASSET_TYPE_XANIMPARTS:
        return HashXAnimParts;
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

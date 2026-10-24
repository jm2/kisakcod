// K4 walkers (docs/design/FASTFILE_LOADER.md). Hash what both widths keep, in
// a width-free form: a reference as the referenced asset's name (FieldAsset), a
// bool through its stored byte, script strings as text, layout-invariant arrays
// as bytes (FieldArray), and only counted elements, since 64-bit can keep an
// empty array where x86 keeps null. Never hash pointers, renderer handles or
// runtime clocks, and hash schema word runs as n*4 bytes from their first
// member, never up to the next pointer (64-bit padding).
//
// Adding a family: pin its synthetic-zone digest with MSVC at x86 and x64 (the
// two must agree; the zone-load test runs only on the Linux i386, ASan+UBSan
// amd64 and macOS arm64 legs). A pin models load-time rewrites, not just the
// record bytes: script-string slots become interned ids (zone index 0 is the
// zone's first string), and runtime state such as XSurface.zoneHandle, which
// the loader overwrites with the zone's slot, stays out of the walk. Lift its guard in
// IsK4Verified (renderer families and LoadedSound inside the
// KISAK_DEDI_HEADLESS block), and grep tests/ for the family: the layout
// tests' unconverted list, the envelope test's guarded family and the zone
// guard tests may still assume it is refused.

#include "database/db_graph_digest.h"

#include "database/database.h"

#include <game/g_bsp.h>
#include <physics/phys_local.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/fft.h>
#include <universal/gfx_image.h>
#include <universal/gfx_lightdef.h>
#include <universal/gfx_material.h>
#include <universal/q_shared.h>
#include <xanim/dobj.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
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

// The record as a load keeps it. A headless server consumes the pixels and
// keeps no texture (DB_FinalizeHeadlessTextureLoad at x86, FinalizeTexture at
// 64-bit), so only the texture's presence is hashed, never a pointer.
void HashImage(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const GfxImage &image = *header.image;
    builder.FieldString(1, image.name);
    builder.FieldI64(2, image.mapType);
    builder.FieldU64(3, image.texture.basemap != nullptr);
    builder.FieldBytes(4, image.picmip.platform, sizeof(image.picmip.platform));
    FieldBool(builder, 5, image.noPicmip);
    builder.FieldU64(6, image.semantic);
    builder.FieldU64(7, image.track);
    builder.FieldI64(8, image.cardMemory.platform[0]);
    builder.FieldI64(9, image.cardMemory.platform[1]);
    builder.FieldU64(10, image.width);
    builder.FieldU64(11, image.height);
    builder.FieldU64(12, image.depth);
    builder.FieldU64(13, image.category);
    FieldBool(builder, 14, image.delayLoadPixels);
}

// TS_WATER_MAP (gfx_d3d/r_image.h): the texture is a water, not an image.
constexpr std::uint8_t kWaterMapSemantic = 0xB;

// A water as both headless loads keep it: the grid, its parameters, the
// image by name, and M * N amplitudes and frequencies at source resolution
// (only a client picmips them). writable.floatTime is the renderer's clock.
void HashWater(GraphHashBuilder &builder, const water_t *water)
{
    builder.FieldU64(19, water != nullptr);
    if (!water)
        return;
    builder.FieldI64(20, water->M);
    builder.FieldI64(21, water->N);
    builder.FieldF32(22, water->Lx);
    builder.FieldF32(23, water->Lz);
    builder.FieldF32(24, water->gravity);
    builder.FieldF32(25, water->windvel);
    builder.FieldF32(26, water->winddir[0]);
    builder.FieldF32(26, water->winddir[1]);
    builder.FieldF32(27, water->amplitude);
    for (const float value : water->codeConstant)
        builder.FieldF32(28, value);
    FieldAsset(builder, 29, ASSET_TYPE_IMAGE, water->image);
    const std::size_t samples = water->M > 0 && water->N > 0
        ? static_cast<std::size_t>(water->M) * static_cast<std::size_t>(water->N)
        : 0;
    FieldArray(builder, 30, water->H0, samples);
    FieldArray(builder, 31, water->wTerm, samples);
}

// A texture: its name hash and bounds, sampler state and semantic, then the
// water or the image by name.
void HashMaterialTexture(GraphHashBuilder &builder, const MaterialTextureDef &texture)
{
    builder.FieldU64(13, texture.nameHash);
    builder.FieldU64(14, static_cast<unsigned char>(texture.nameStart));
    builder.FieldU64(15, static_cast<unsigned char>(texture.nameEnd));
    builder.FieldU64(16, texture.samplerState);
    builder.FieldU64(17, texture.semantic);
    if (texture.semantic == kWaterMapSemantic)
        HashWater(builder, texture.u.water);
    else
        FieldAsset(builder, 18, ASSET_TYPE_IMAGE, texture.u.image);
}

// The info as loaded (no headless code sorts materials), the state-bit
// entries and flags, the technique set by name, then each table. Constants
// and state bits hold no pointers, so they hash as bytes.
void HashMaterial(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const Material &material = *header.material;
    const MaterialInfo &info = material.info;
    builder.FieldString(1, info.name);
    builder.FieldU64(2, info.gameFlags);
    builder.FieldU64(3, info.sortKey);
    builder.FieldU64(4, info.textureAtlasRowCount);
    builder.FieldU64(4, info.textureAtlasColumnCount);
    builder.FieldU64(5, info.drawSurf.packed);
    builder.FieldU64(6, info.surfaceTypeBits);
    builder.FieldU64(7, info.hashIndex);
    builder.FieldBytes(8, material.stateBitsEntry, sizeof(material.stateBitsEntry));
    builder.FieldU64(9, material.stateFlags);
    builder.FieldU64(9, material.cameraRegion);
    FieldAsset(builder, 10, ASSET_TYPE_TECHNIQUE_SET, material.techniqueSet);
    builder.FieldU64(11, material.textureCount);
    const std::size_t textures = material.textureTable ? material.textureCount : 0;
    for (std::size_t i = 0; i < textures; ++i)
        HashMaterialTexture(builder, material.textureTable[i]);
    builder.FieldU64(32, material.constantCount);
    FieldArray(builder, 33, material.constantTable, material.constantCount);
    builder.FieldU64(34, material.stateBitsCount);
    FieldArray(builder, 35, material.stateBitsTable, material.stateBitsCount);
}

// A vertex declaration's stream count and routing table, and whether a stream
// is optional. Not the renderer's declaration handles, nor isLoaded, which
// records whether they were built.
void HashVertexDecl(GraphHashBuilder &builder, const MaterialVertexDeclaration *decl)
{
    builder.FieldU64(10, decl != nullptr);
    if (!decl)
        return;
    builder.FieldU64(11, decl->streamCount);
    FieldBool(builder, 12, decl->hasOptionalSource);
    builder.FieldBytes(14, decl->routing.data, sizeof(decl->routing.data));
}

// A shader: its name, its load definition and programSize program DWORDs.
// No headless load creates the renderer handle.
template <typename Shader>
void HashShader(GraphHashBuilder &builder, std::uint32_t tag, const Shader *shader)
{
    builder.FieldU64(tag, shader != nullptr);
    if (!shader)
        return;
    const auto &loadDef = shader->prog.loadDef;
    builder.FieldString(tag + 1, shader->name);
    builder.FieldU64(tag + 2, loadDef.programSize);
    builder.FieldU64(tag + 3, loadDef.loadForRenderer);
    builder.FieldBytes(tag + 4, loadDef.program, loadDef.program ? loadDef.programSize * sizeof(std::uint32_t) : 0);
}

// An argument: a literal's four floats (types 1 and 7), else the code
// constant, code sampler or name hash in the union's first four bytes.
void HashShaderArgument(GraphHashBuilder &builder, const MaterialShaderArgument &argument)
{
    builder.FieldU64(25, argument.type);
    builder.FieldU64(26, argument.dest);
    if (argument.type == 1 || argument.type == 7)
        FieldArray(builder, 27, argument.u.literalConst, 4);
    else
        builder.FieldU64(28, argument.u.nameHash);
}

// A pass: its declaration, shaders, counts and arguments. A 64-bit load
// keeps an empty argument array where x86 keeps null, so only the counted
// arguments are hashed.
void HashMaterialPass(GraphHashBuilder &builder, const MaterialPass &pass)
{
    HashVertexDecl(builder, pass.vertexDecl);
    HashShader(builder, 15, pass.vertexShader);
    HashShader(builder, 20, pass.pixelShader);
    builder.FieldU64(29, pass.perPrimArgCount);
    builder.FieldU64(29, pass.perObjArgCount);
    builder.FieldU64(29, pass.stableArgCount);
    builder.FieldU64(30, pass.customSamplerFlags);
    const std::size_t arguments =
        pass.args ? static_cast<std::size_t>(pass.perPrimArgCount) + pass.perObjArgCount + pass.stableArgCount : 0;
    for (std::size_t i = 0; i < arguments; ++i)
        HashShaderArgument(builder, pass.args[i]);
}

void HashTechnique(GraphHashBuilder &builder, const MaterialTechnique *technique)
{
    builder.FieldU64(6, technique != nullptr);
    if (!technique)
        return;
    builder.FieldString(7, technique->name);
    builder.FieldU64(8, technique->flags);
    builder.FieldU64(9, technique->passCount);
    for (std::uint32_t i = 0; i < technique->passCount; ++i)
        HashMaterialPass(builder, technique->passArray[i]);
}

// The set's name, world vertex format and spare byte, then its 34 techniques
// in order. Renderer state is left out like the handles: hasBeenUploaded
// (set once shaders upload) and remappedTechniqueSet (the renderer's variant;
// a headless server maps each set to itself).
void HashTechniqueSet(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const MaterialTechniqueSet &set = *header.techniqueSet;
    builder.FieldString(1, set.name);
    builder.FieldU64(2, set.worldVertFormat);
    builder.FieldU64(4, set.unused[0]);
    for (const MaterialTechnique *technique : set.techniques)
        HashTechnique(builder, technique);
}

// count asset references of one type, each by name.
template <typename T>
void FieldAssets(GraphHashBuilder &builder, std::uint32_t tag, XAssetType type, T *const *slots, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        FieldAsset(builder, tag, type, slots[i]);
}

void FieldStrings(GraphHashBuilder &builder, std::uint32_t tag, const char *const *strings, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        builder.FieldString(tag, strings[i]);
}

void FieldScriptStrings(GraphHashBuilder &builder, std::uint32_t tag, const std::uint16_t *indices, std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
        FieldScriptString(builder, tag, indices[i]);
}

// words 4-byte scalars and enums from offset, as bytes: one of the runs the
// 64-bit loader copies whole (disk32/21-weapon.schema), so they sit alike at
// both widths. A run never reaches the padding before a 64-bit pointer.
void FieldRun(GraphHashBuilder &builder, std::uint32_t tag, const WeaponDef &weapon, std::size_t offset,
              std::size_t words)
{
    builder.FieldBytes(tag, reinterpret_cast<const unsigned char *>(&weapon) + offset, words * 4);
}

// pickupSound through putawaySoundPlayer: 45 consecutive sound references,
// as the 64-bit loader asserts (db_disk32_weapon.cpp).
constexpr std::size_t kWeaponSounds = 45;
constexpr std::size_t kBounceSurfaces = 29; // one bounce sound per surface type

// The names, models, animation names, script strings and the first run.
void HashWeaponHead(GraphHashBuilder &builder, const WeaponDef &weapon)
{
    builder.FieldString(1, weapon.szInternalName);
    builder.FieldString(1, weapon.szDisplayName);
    builder.FieldString(1, weapon.szOverlayName);
    FieldAssets(builder, 2, ASSET_TYPE_XMODEL, weapon.gunXModel, std::size(weapon.gunXModel));
    FieldAsset(builder, 2, ASSET_TYPE_XMODEL, weapon.handXModel);
    FieldStrings(builder, 3, weapon.szXAnims, std::size(weapon.szXAnims));
    builder.FieldString(1, weapon.szModeName);
    FieldScriptStrings(builder, 4, weapon.hideTags, std::size(weapon.hideTags));
    FieldScriptStrings(builder, 4, weapon.notetrackSoundMapKeys, std::size(weapon.notetrackSoundMapKeys));
    FieldScriptStrings(builder, 4, weapon.notetrackSoundMapValues, std::size(weapon.notetrackSoundMapValues));
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, playerAnimType), 9);
}

// The effects, sounds, bounce table and reticles.
void HashWeaponEffects(GraphHashBuilder &builder, const WeaponDef &weapon)
{
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.viewFlashEffect);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.worldFlashEffect);
    FieldAssets(builder, 7, ASSET_TYPE_SOUND, &weapon.pickupSound, kWeaponSounds);
    builder.FieldU64(8, weapon.bounceSound != nullptr);
    if (weapon.bounceSound)
        FieldAssets(builder, 7, ASSET_TYPE_SOUND, weapon.bounceSound, kBounceSurfaces);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.viewShellEjectEffect);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.worldShellEjectEffect);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.viewLastShotEjectEffect);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.worldLastShotEjectEffect);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.reticleCenter);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.reticleSide);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, iReticleCenterSize), 38);
}

// The world models, icons, ammo and clip names, overlays and alt weapon.
void HashWeaponModels(GraphHashBuilder &builder, const WeaponDef &weapon)
{
    FieldAssets(builder, 2, ASSET_TYPE_XMODEL, weapon.worldModel, std::size(weapon.worldModel));
    FieldAsset(builder, 2, ASSET_TYPE_XMODEL, weapon.worldClipModel);
    FieldAsset(builder, 2, ASSET_TYPE_XMODEL, weapon.rocketModel);
    FieldAsset(builder, 2, ASSET_TYPE_XMODEL, weapon.knifeModel);
    FieldAsset(builder, 2, ASSET_TYPE_XMODEL, weapon.worldKnifeModel);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.hudIcon);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, hudIconRatio), 1);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.ammoCounterIcon);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, ammoCounterIconRatio), 3);
    builder.FieldString(1, weapon.szAmmoName);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, iAmmoIndex), 1);
    builder.FieldString(1, weapon.szClipName);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, iClipIndex), 4);
    builder.FieldString(1, weapon.szSharedAmmoCapName);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, iSharedAmmoCapIndex), 59);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.overlayMaterial);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.overlayMaterialLowRes);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, overlayReticle), 56);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.killIcon);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, killIconRatio), 2);
    FieldAsset(builder, 9, ASSET_TYPE_MATERIAL, weapon.dpadIcon);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, dpadIconRatio), 5);
    builder.FieldString(1, weapon.szAltWeaponName);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, altWeaponIndex), 17);
}

// The projectile's model, effects and sounds.
void HashWeaponProjectile(GraphHashBuilder &builder, const WeaponDef &weapon)
{
    FieldAsset(builder, 2, ASSET_TYPE_XMODEL, weapon.projectileModel);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, projExplosion), 1);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.projExplosionEffect);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, projExplosionEffectForceNormalUp), 1);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.projDudEffect);
    FieldAsset(builder, 7, ASSET_TYPE_SOUND, weapon.projExplosionSound);
    FieldAsset(builder, 7, ASSET_TYPE_SOUND, weapon.projDudSound);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, bProjImpactExplode), 66);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.projTrailEffect);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, vProjectileColor), 6);
    FieldAsset(builder, 6, ASSET_TYPE_FX, weapon.projIgnitionEffect);
    FieldAsset(builder, 7, ASSET_TYPE_SOUND, weapon.projIgnitionSound);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, fAdsAimPitch), 40);
}

// The accuracy graphs (each name, then its knots and the original knots,
// count pairs each), the hint and script strings, and the last runs.
void HashWeaponTail(GraphHashBuilder &builder, const WeaponDef &weapon)
{
    FieldStrings(builder, 10, weapon.accuracyGraphName, std::size(weapon.accuracyGraphName));
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, accuracyGraphKnotCount), 22);
    for (std::size_t graph = 0; graph < std::size(weapon.accuracyGraphKnots); ++graph)
    {
        const int knots = weapon.accuracyGraphKnotCount[graph];
        const int original = weapon.originalAccuracyGraphKnotCount[graph];
        FieldArray(builder, 11, weapon.accuracyGraphKnots[graph], knots > 0 ? static_cast<std::size_t>(knots) : 0);
        FieldArray(builder, 12, weapon.originalAccuracyGraphKnots[graph],
                   original > 0 ? static_cast<std::size_t>(original) : 0);
    }
    builder.FieldString(1, weapon.szUseHintString);
    builder.FieldString(1, weapon.dropHintString);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, iUseHintStringIndex), 4);
    builder.FieldString(1, weapon.szScript);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, fOOPosAnimLength), 28);
    builder.FieldString(1, weapon.fireRumble);
    builder.FieldString(1, weapon.meleeImpactRumble);
    FieldRun(builder, 5, weapon, offsetof(WeaponDef, adsDofStart), 2);
}

// Every field in record order: references by name, strings and script
// strings as text, and the scalar runs as bytes.
void HashWeapon(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const WeaponDef &weapon = *header.weapon;
    HashWeaponHead(builder, weapon);
    HashWeaponEffects(builder, weapon);
    HashWeaponModels(builder, weapon);
    HashWeaponProjectile(builder, weapon);
    HashWeaponTail(builder, weapon);
}

// GfxPackedVertex (gfx_d3d/) is 32 bytes at both widths and holds no
// pointer: position, binormal sign, color, texture coordinates, normal and
// tangent.
constexpr std::size_t kPackedVertexBytes = 32;

// A rigid vertex list's collision tree: its transform, then its nodes and
// leaves, which hold no pointer.
void HashCollisionTree(GraphHashBuilder &builder, const XSurfaceCollisionTree *tree)
{
    builder.FieldU64(30, tree != nullptr);
    if (!tree)
        return;
    builder.FieldBytes(31, tree->trans, sizeof(tree->trans));
    builder.FieldBytes(31, tree->scale, sizeof(tree->scale));
    builder.FieldU64(32, tree->nodeCount);
    FieldArray(builder, 33, tree->nodes, tree->nodeCount);
    builder.FieldU64(32, tree->leafCount);
    FieldArray(builder, 34, tree->leafs, tree->leafCount);
}

// A surface: its counts and indices, its skinning (vertex counts by
// influence, then 1, 3, 5 or 7 blend values per vertex), its vertices and its
// rigid vertex lists. Not zoneHandle: a load replaces it with the slot of the
// zone being loaded (Load_GetCurrentZoneHandle), which load order decides.
void HashXSurface(GraphHashBuilder &builder, const XSurface &surface)
{
    builder.FieldU64(20, surface.tileMode);
    FieldBool(builder, 21, surface.deformed);
    builder.FieldU64(22, surface.vertCount);
    builder.FieldU64(22, surface.triCount);
    builder.FieldU64(24, surface.baseTriIndex);
    builder.FieldU64(24, surface.baseVertIndex);
    FieldArray(builder, 25, surface.triIndices, surface.triCount * std::size_t{3});
    std::size_t blendValues = 0;
    for (std::size_t bucket = 0; bucket < std::size(surface.vertInfo.vertCount); ++bucket)
    {
        const int count = surface.vertInfo.vertCount[bucket];
        builder.FieldI64(26, count);
        blendValues += count > 0 ? (2 * bucket + 1) * static_cast<std::size_t>(count) : 0;
    }
    FieldArray(builder, 27, surface.vertInfo.vertsBlend, blendValues);
    builder.FieldBytes(28, surface.verts0, surface.verts0 ? surface.vertCount * kPackedVertexBytes : 0);
    builder.FieldU64(29, surface.vertListCount);
    const std::size_t lists = surface.vertList ? surface.vertListCount : 0;
    for (std::size_t i = 0; i < lists; ++i)
    {
        const XRigidVertList &list = surface.vertList[i];
        builder.FieldU64(35, list.boneOffset);
        builder.FieldU64(35, list.vertCount);
        builder.FieldU64(35, list.triOffset);
        builder.FieldU64(35, list.triCount);
        HashCollisionTree(builder, list.collisionTree);
    }
    builder.FieldBytes(36, surface.partBits, sizeof(surface.partBits));
}

// The bones: names as text, then each array as the bytes its count covers.
// Parents, rotations and translations exist for non-root bones only.
void HashXModelBones(GraphHashBuilder &builder, const XModel &model)
{
    const std::size_t bones = model.numBones;
    const std::size_t nonRoot = model.numBones >= model.numRootBones ? model.numBones - model.numRootBones : 0;
    builder.FieldU64(2, model.numBones);
    builder.FieldU64(2, model.numRootBones);
    FieldScriptStrings(builder, 3, model.boneNames, model.boneNames ? bones : 0);
    FieldArray(builder, 4, model.parentList, nonRoot);
    FieldArray(builder, 5, model.quats, nonRoot * 4);
    FieldArray(builder, 6, model.trans, nonRoot * 4);
    FieldArray(builder, 7, model.partClassification, bones);
    FieldArray(builder, 8, model.baseMat, bones);
    FieldArray(builder, 9, model.boneInfo, bones);
}

// Each LOD's distance, surface range, part bits and index. Not
// smcIndexPlusOne or smcAllocBits: the renderer's static model cache sets
// them.
void HashXModelLods(GraphHashBuilder &builder, const XModel &model)
{
    for (const XModelLodInfo &lod : model.lodInfo)
    {
        builder.FieldF32(10, lod.dist);
        builder.FieldU64(11, lod.numsurfs);
        builder.FieldU64(11, lod.surfIndex);
        builder.FieldBytes(12, lod.partBits, sizeof(lod.partBits));
        builder.FieldU64(13, lod.lod);
        builder.FieldU64(13, lod.unused);
    }
}

// A physics brush: its bounds and sides (each side's plane as its 20 bytes),
// then its axial materials, adjacency, edge counts and planes.
void HashBrush(GraphHashBuilder &builder, const BrushWrapper *brush)
{
    builder.FieldU64(40, brush != nullptr);
    if (!brush)
        return;
    builder.FieldBytes(41, brush->mins, sizeof(brush->mins));
    builder.FieldI64(42, brush->contents);
    builder.FieldBytes(41, brush->maxs, sizeof(brush->maxs));
    builder.FieldU64(43, brush->numsides);
    const std::size_t sides = brush->sides ? brush->numsides : 0;
    for (std::size_t i = 0; i < sides; ++i)
    {
        const cbrushside_t &side = brush->sides[i];
        builder.FieldU64(44, side.plane != nullptr);
        FieldArray(builder, 45, side.plane, side.plane ? 1 : 0);
        builder.FieldU64(46, side.materialNum);
        builder.FieldI64(46, side.firstAdjacentSideOffset);
        builder.FieldU64(46, side.edgeCount);
    }
    builder.FieldBytes(47, brush->axialMaterialNum, sizeof(brush->axialMaterialNum));
    builder.FieldI64(48, brush->totalEdgeCount);
    FieldArray(builder, 49, brush->baseAdjacentSide,
               brush->totalEdgeCount > 0 ? static_cast<std::size_t>(brush->totalEdgeCount) : 0);
    builder.FieldBytes(50, brush->firstAdjacentSideOffsets, sizeof(brush->firstAdjacentSideOffsets));
    builder.FieldBytes(50, brush->edgeCount, sizeof(brush->edgeCount));
    FieldArray(builder, 51, brush->planes, brush->numsides);
}

// The physics geometry: each geom's type, orientation, offset, half lengths
// and brush, then the mass.
void HashPhysGeoms(GraphHashBuilder &builder, const PhysGeomList *list)
{
    builder.FieldU64(52, list != nullptr);
    if (!list)
        return;
    builder.FieldU64(53, list->count);
    const std::size_t geoms = list->geoms ? list->count : 0;
    for (std::size_t i = 0; i < geoms; ++i)
    {
        const PhysGeomInfo &geom = list->geoms[i];
        builder.FieldI64(54, geom.type);
        builder.FieldBytes(55, geom.orientation, sizeof(geom.orientation));
        builder.FieldBytes(55, geom.offset, sizeof(geom.offset));
        builder.FieldBytes(55, geom.halfLengths, sizeof(geom.halfLengths));
        HashBrush(builder, geom.brush);
    }
    builder.FieldBytes(56, list->mass.centerOfMass, sizeof(list->mass.centerOfMass));
    builder.FieldBytes(56, list->mass.momentsOfInertia, sizeof(list->mass.momentsOfInertia));
    builder.FieldBytes(56, list->mass.productsOfInertia, sizeof(list->mass.productsOfInertia));
}

// The collision surfaces: each surface's triangles (48 bytes each, no
// pointer), bounds, bone, contents and flags.
void HashCollSurfs(GraphHashBuilder &builder, const XModel &model)
{
    builder.FieldI64(14, model.numCollSurfs);
    const int surfaces = model.collSurfs ? model.numCollSurfs : 0;
    for (int i = 0; i < surfaces; ++i)
    {
        const XModelCollSurf_s &surface = model.collSurfs[i];
        builder.FieldI64(15, surface.numCollTris);
        FieldArray(builder, 16, surface.collTris,
                   surface.numCollTris > 0 ? static_cast<std::size_t>(surface.numCollTris) : 0);
        builder.FieldBytes(17, surface.mins, sizeof(surface.mins));
        builder.FieldBytes(17, surface.maxs, sizeof(surface.maxs));
        builder.FieldI64(18, surface.boneIdx);
        builder.FieldI64(18, surface.contents);
        builder.FieldI64(18, surface.surfFlags);
    }
}

// The model in record order: its scalars, bones, surfaces with their
// materials by name, LODs, collision surfaces, the preset by name and the
// physics geometry.
void HashXModel(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const XModel &model = *header.model;
    builder.FieldString(1, model.name);
    HashXModelBones(builder, model);
    builder.FieldU64(19, model.numsurfs);
    builder.FieldU64(19, model.lodRampType);
    const std::size_t surfaces = model.surfs ? model.numsurfs : 0;
    for (std::size_t i = 0; i < surfaces; ++i)
        HashXSurface(builder, model.surfs[i]);
    FieldAssets(builder, 37, ASSET_TYPE_MATERIAL, model.materialHandles, model.materialHandles ? model.numsurfs : 0);
    HashXModelLods(builder, model);
    HashCollSurfs(builder, model);
    builder.FieldI64(38, model.contents);
    builder.FieldF32(38, model.radius);
    builder.FieldBytes(39, model.mins, sizeof(model.mins));
    builder.FieldBytes(39, model.maxs, sizeof(model.maxs));
    builder.FieldI64(57, model.numLods);
    builder.FieldI64(57, model.collLod);
    builder.FieldI64(57, model.memUsage);
    builder.FieldU64(57, model.flags);
    FieldBool(builder, 58, model.bad);
    FieldAsset(builder, 59, ASSET_TYPE_PHYSPRESET, model.physPreset);
    HashPhysGeoms(builder, model.physGeoms);
}

// A pointer into one of the clip map's arrays as its element index: -1 when
// null, -2 when it points outside the array.
template <typename T>
void FieldIndex(GraphHashBuilder &builder, std::uint32_t tag, const T *element, const T *base, std::size_t count)
{
    std::int64_t index = -1;
    if (element)
        index = base && element >= base && element < base + count ? element - base : -2;
    builder.FieldI64(tag, index);
}

// The pieces in record order: the name, then each piece's model by name and
// its offset.
void HashXModelPiecesBody(GraphHashBuilder &builder, const XModelPieces *pieces)
{
    builder.FieldU64(1, pieces != nullptr);
    if (!pieces)
        return;
    builder.FieldString(2, pieces->name);
    builder.FieldI64(3, pieces->numpieces);
    const int count = pieces->pieces ? pieces->numpieces : 0;
    for (int i = 0; i < count; ++i)
    {
        FieldAsset(builder, 4, ASSET_TYPE_XMODEL, pieces->pieces[i].model);
        builder.FieldBytes(5, pieces->pieces[i].offset, sizeof(pieces->pieces[i].offset));
    }
}

void HashXModelPieces(GraphHashBuilder &builder, const XAssetHeader &header)
{
    HashXModelPiecesBody(builder, header.xmodelPieces);
}

// A leaf without its trailing padding.
void HashClipLeaf(GraphHashBuilder &builder, const cLeaf_t &leaf)
{
    builder.FieldU64(20, leaf.firstCollAabbIndex);
    builder.FieldU64(20, leaf.collAabbCount);
    builder.FieldI64(20, leaf.brushContents);
    builder.FieldI64(20, leaf.terrainContents);
    builder.FieldBytes(21, leaf.mins, sizeof(leaf.mins));
    builder.FieldBytes(21, leaf.maxs, sizeof(leaf.maxs));
    builder.FieldI64(22, leaf.leafBrushNode);
    builder.FieldI64(22, leaf.cluster);
}

void HashClipModel(GraphHashBuilder &builder, const cmodel_t &model)
{
    builder.FieldBytes(23, model.mins, sizeof(model.mins));
    builder.FieldBytes(23, model.maxs, sizeof(model.maxs));
    builder.FieldF32(23, model.radius);
    HashClipLeaf(builder, model.leaf);
}

// A clip-map brush: its sides and adjacent-side bytes as indices into the
// map's arrays.
void HashClipBrush(GraphHashBuilder &builder, const clipMap_t &map, const cbrush_t *brush)
{
    builder.FieldU64(24, brush != nullptr);
    if (!brush)
        return;
    builder.FieldBytes(25, brush->mins, sizeof(brush->mins));
    builder.FieldI64(25, brush->contents);
    builder.FieldBytes(25, brush->maxs, sizeof(brush->maxs));
    builder.FieldU64(26, brush->numsides);
    FieldIndex(builder, 26, brush->sides, map.brushsides, map.numBrushSides);
    builder.FieldBytes(27, brush->axialMaterialNum, sizeof(brush->axialMaterialNum));
    FieldIndex(builder, 28, brush->baseAdjacentSide, map.brushEdges, map.numBrushEdges);
    builder.FieldBytes(29, brush->firstAdjacentSideOffsets, sizeof(brush->firstAdjacentSideOffsets));
    builder.FieldBytes(29, brush->edgeCount, sizeof(brush->edgeCount));
}

// A leaf-brush node holds brush indices when its count is positive, else the
// split that leads to its children.
void HashLeafBrushNode(GraphHashBuilder &builder, const cLeafBrushNode_s &node)
{
    builder.FieldU64(30, node.axis);
    builder.FieldI64(30, node.leafBrushCount);
    builder.FieldI64(30, node.contents);
    if (node.leafBrushCount > 0)
    {
        FieldArray(builder, 31, node.data.leaf.brushes, static_cast<std::size_t>(node.leafBrushCount));
        return;
    }
    builder.FieldF32(32, node.data.children.dist);
    builder.FieldF32(32, node.data.children.range);
    builder.FieldBytes(32, node.data.children.childOffset, sizeof(node.data.children.childOffset));
}

// A dynamic entity's definition, its pieces in full: a clip map's pieces live
// in its own record, not in the pool.
void HashDynEntityDef(GraphHashBuilder &builder, const DynEntityDefView &def)
{
    builder.FieldI64(33, def.type);
    builder.FieldBytes(34, def.quat, 4 * sizeof(float));
    builder.FieldBytes(34, def.origin, 3 * sizeof(float));
    FieldAsset(builder, 35, ASSET_TYPE_XMODEL, def.xModel);
    builder.FieldU64(36, def.brushModel);
    builder.FieldU64(36, def.physicsBrushModel);
    FieldAsset(builder, 37, ASSET_TYPE_FX, def.destroyFx);
    HashXModelPiecesBody(builder, def.destroyPieces);
    FieldAsset(builder, 38, ASSET_TYPE_PHYSPRESET, def.physPreset);
    builder.FieldI64(39, def.health);
    builder.FieldBytes(40, def.mass, 9 * sizeof(float));
    builder.FieldI64(41, def.contents);
}

// A signed count, or 0 when it is negative.
std::size_t Count(const int count)
{
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}

// The tree: planes, static models, materials, sides, edges, nodes, leaves and
// their brushes and surfaces.
void HashClipTree(GraphHashBuilder &builder, const clipMap_t &map)
{
    const std::size_t planes = Count(map.planeCount);
    builder.FieldI64(3, map.planeCount);
    FieldArray(builder, 4, map.planes, planes);
    builder.FieldU64(5, map.numStaticModels);
    const std::size_t staticModels = map.staticModelList ? map.numStaticModels : 0;
    for (std::size_t i = 0; i < staticModels; ++i)
    {
        const cStaticModel_s &model = map.staticModelList[i];
        FieldAsset(builder, 6, ASSET_TYPE_XMODEL, model.xmodel);
        builder.FieldBytes(7, model.origin, sizeof(model.origin));
        builder.FieldBytes(7, model.invScaledAxis, sizeof(model.invScaledAxis));
        builder.FieldBytes(7, model.absmin, sizeof(model.absmin));
        builder.FieldBytes(7, model.absmax, sizeof(model.absmax));
    }
    builder.FieldU64(8, map.numMaterials);
    FieldArray(builder, 8, map.materials, map.numMaterials);
    builder.FieldU64(9, map.numBrushSides);
    const std::size_t sides = map.brushsides ? map.numBrushSides : 0;
    for (std::size_t i = 0; i < sides; ++i)
    {
        const cbrushside_t &side = map.brushsides[i];
        FieldIndex(builder, 10, side.plane, map.planes, planes);
        builder.FieldU64(10, side.materialNum);
        builder.FieldI64(10, side.firstAdjacentSideOffset);
        builder.FieldU64(10, side.edgeCount);
    }
    builder.FieldU64(11, map.numBrushEdges);
    FieldArray(builder, 11, map.brushEdges, map.numBrushEdges);
    builder.FieldU64(12, map.numNodes);
    const std::size_t nodes = map.nodes ? map.numNodes : 0;
    for (std::size_t i = 0; i < nodes; ++i)
    {
        FieldIndex(builder, 13, map.nodes[i].plane, map.planes, planes);
        builder.FieldBytes(13, map.nodes[i].children, sizeof(map.nodes[i].children));
    }
    builder.FieldU64(14, map.numLeafs);
    const std::size_t leafs = map.leafs ? map.numLeafs : 0;
    for (std::size_t i = 0; i < leafs; ++i)
        HashClipLeaf(builder, map.leafs[i]);
    builder.FieldU64(15, map.leafbrushNodesCount);
    const std::size_t leafBrushNodes = map.leafbrushNodes ? map.leafbrushNodesCount : 0;
    for (std::size_t i = 0; i < leafBrushNodes; ++i)
        HashLeafBrushNode(builder, map.leafbrushNodes[i]);
    builder.FieldU64(16, map.numLeafBrushes);
    FieldArray(builder, 16, map.leafbrushes, map.numLeafBrushes);
    builder.FieldU64(17, map.numLeafSurfaces);
    FieldArray(builder, 17, map.leafsurfaces, map.numLeafSurfaces);
}

// The collision geometry: vertices, triangles, borders, partitions and AABB
// trees.
void HashClipCollision(GraphHashBuilder &builder, const clipMap_t &map)
{
    builder.FieldU64(18, map.vertCount);
    FieldArray(builder, 18, map.verts, map.vertCount);
    builder.FieldI64(19, map.triCount);
    const std::size_t triIndices = 3 * Count(map.triCount);
    FieldArray(builder, 19, map.triIndices, triIndices);
    // One bit per triangle edge, in whole 32-bit words.
    FieldArray(builder, 19, map.triEdgeIsWalkable, (triIndices + 31) / 32 * 4);
    const std::size_t borders = Count(map.borderCount);
    builder.FieldI64(42, map.borderCount);
    FieldArray(builder, 42, map.borders, borders);
    builder.FieldI64(43, map.partitionCount);
    const int partitions = map.partitions ? map.partitionCount : 0;
    for (int i = 0; i < partitions; ++i)
    {
        const CollisionPartition &partition = map.partitions[i];
        builder.FieldU64(44, partition.triCount);
        builder.FieldU64(44, partition.borderCount);
        builder.FieldI64(44, partition.firstTri);
        FieldIndex(builder, 44, partition.borders, map.borders, borders);
    }
    builder.FieldI64(45, map.aabbTreeCount);
    FieldArray(builder, 45, map.aabbTrees, Count(map.aabbTreeCount));
}

// The clip map in record order. Planes are 20 bytes with no pointer, so a
// side's or node's plane is its index. Left out: each static model's
// world-sector link and the dynamic entities' pose, client and collision
// lists, which the game writes at run time.
void HashClipMap(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const clipMap_t &map = *header.clipMap;
    builder.FieldString(1, map.name);
    builder.FieldI64(2, map.isInUse);
    HashClipTree(builder, map);
    HashClipCollision(builder, map);
    builder.FieldU64(46, map.numSubModels);
    const std::size_t subModels = map.cmodels ? map.numSubModels : 0;
    for (std::size_t i = 0; i < subModels; ++i)
        HashClipModel(builder, map.cmodels[i]);
    builder.FieldU64(47, map.numBrushes);
    const std::size_t brushes = map.brushes ? map.numBrushes : 0;
    for (std::size_t i = 0; i < brushes; ++i)
        HashClipBrush(builder, map, &map.brushes[i]);
    builder.FieldI64(48, map.numClusters);
    builder.FieldI64(48, map.clusterBytes);
    FieldArray(builder, 49, map.visibility, Count(map.numClusters) * Count(map.clusterBytes));
    builder.FieldI64(50, map.vised);
    FieldAsset(builder, 51, ASSET_TYPE_MAP_ENTS, map.mapEnts);
    HashClipBrush(builder, map, map.box_brush);
    HashClipModel(builder, map.box_model);
    for (int list = 0; list < 2; ++list)
    {
        builder.FieldU64(52, map.dynEntCount[list]);
        const std::size_t defs = map.dynEntDefList[list] ? map.dynEntCount[list] : 0;
        for (std::size_t i = 0; i < defs; ++i)
            HashDynEntityDef(builder, DB_DynEntityDefView(map.dynEntDefList[list], static_cast<int32_t>(i)));
    }
    builder.FieldU64(53, map.checksum);
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
    case ASSET_TYPE_IMAGE:
        return HashImage;
    case ASSET_TYPE_MATERIAL:
        return HashMaterial;
    case ASSET_TYPE_TECHNIQUE_SET:
        return HashTechniqueSet;
    case ASSET_TYPE_WEAPON:
        return HashWeapon;
    case ASSET_TYPE_XMODEL:
        return HashXModel;
    case ASSET_TYPE_XMODELPIECES:
        return HashXModelPieces;
    case ASSET_TYPE_CLIPMAP_PVS:
        return HashClipMap;
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

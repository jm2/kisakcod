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

#include <database/db_stream.h>
#include <database/db_validation.h>
#include <game/g_bsp.h>
#include <physics/phys_local.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <universal/dyn_entity_def.h>
#include <universal/fft.h>
#include <universal/fx_effect_def.h>
#include <universal/gfx_image.h>
#include <universal/gfx_lightdef.h>
#include <universal/gfx_material.h>
#include <universal/gfx_world.h>
#include <universal/q_shared.h>
#include <xanim/dobj.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <vector>

// The walker hashes the vertex buffer as bytes: the record holds no pointer
// and no padding, and is the same size on both widths.
RUNTIME_SIZE(GfxWorldVertex, 0x2C, 0x2C);

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

// A menu's window, items and type data, in loader order. A pointer is the
// referenced asset's name; a string is its text. Layout-invariant word runs
// hash as bytes from their first member, never up to the next pointer.
enum MenuWindowField : std::uint32_t
{
    kMenuWindowName = 1,
    kMenuWindowRects,      // rect and rectClient, 48 bytes
    kMenuWindowGroup,
    kMenuWindowScalars,    // style through staticFlags, 24 bytes
    kMenuWindowColors,     // the four 4-float colors
    kMenuWindowBackground,
};

// dynamicFlags and nextTime are animation state the client advances, so they
// stay out of the walk; they sit between the scalar run and the colors.
void HashMenuWindow(GraphHashBuilder &builder, std::uint32_t tag, const windowDef_t &window)
{
    builder.BeginRecord(tag);
    builder.FieldString(kMenuWindowName, window.name);
    builder.FieldBytes(kMenuWindowRects, &window.rect, sizeof(window.rect) * 2);
    builder.FieldString(kMenuWindowGroup, window.group);
    builder.FieldBytes(kMenuWindowScalars, &window.style, 6 * sizeof(std::int32_t));
    builder.FieldBytes(kMenuWindowColors, window.foreColor, 16 * sizeof(float));
    FieldAsset(builder, kMenuWindowBackground, ASSET_TYPE_MATERIAL, window.background);
    builder.EndRecord();
}

enum MenuKeyHandlerField : std::uint32_t
{
    kMenuKeyHandler = 1,
    kMenuKeyHandlerKey,
    kMenuKeyHandlerAction,
};

// Each handler's key and script; next only leads to the following handler.
void HashMenuKeyHandlers(GraphHashBuilder &builder, std::uint32_t tag, const ItemKeyHandler *handler)
{
    builder.BeginRecord(tag);
    for (; handler; handler = handler->next)
    {
        builder.BeginRecord(kMenuKeyHandler);
        builder.FieldI64(kMenuKeyHandlerKey, handler->key);
        builder.FieldString(kMenuKeyHandlerAction, handler->action);
        builder.EndRecord();
    }
    builder.EndRecord();
}

enum MenuExpressionField : std::uint32_t
{
    kMenuStatementEntry = 1,
    kMenuExpressionType,
    kMenuExpressionOperator,
    kMenuExpressionDataType,
    kMenuExpressionValue,
};

// A statement: its entry count, then each entry's operator or operand. A
// string operand is its text; an int or float one is its bits.
void HashMenuStatement(GraphHashBuilder &builder, std::uint32_t tag, const statement_s &statement)
{
    builder.BeginRecord(tag);
    builder.FieldI64(1, statement.numEntries);
    const int entries = statement.entries && statement.numEntries > 0 ? statement.numEntries : 0;
    for (int i = 0; i < entries; ++i)
    {
        const expressionEntry &entry = *statement.entries[i];
        builder.BeginRecord(kMenuStatementEntry);
        builder.FieldI64(kMenuExpressionType, entry.type);
        if (entry.type)
        {
            const Operand &operand = entry.data.operand;
            builder.FieldI64(kMenuExpressionDataType, operand.dataType);
            if (operand.dataType == VAL_STRING)
                builder.FieldString(kMenuExpressionValue, operand.internals.string);
            else if (operand.dataType == VAL_FLOAT)
                builder.FieldF32(kMenuExpressionValue, operand.internals.floatVal);
            else
                builder.FieldI64(kMenuExpressionValue, operand.internals.intVal);
        }
        else
        {
            builder.FieldI64(kMenuExpressionOperator, entry.data.op);
        }
        builder.EndRecord();
    }
    builder.EndRecord();
}

enum MenuTypeDataField : std::uint32_t
{
    kMenuItemTypeDataListBox = 1,
    kMenuItemTypeDataEditField,
    kMenuItemTypeDataMulti,
    kMenuItemTypeDataEnumDvar,
    kMenuItemTypeDataColumn,
    kMenuItemTypeDataMultiEntry,
};

// mousePos through numColumns (eight words, the two floats in their bit
// patterns), then the counted columns.
void HashMenuListBox(GraphHashBuilder &builder, const listBoxDef_s &box)
{
    builder.BeginRecord(kMenuItemTypeDataListBox);
    builder.FieldBytes(1, &box.mousePos, 8 * sizeof(std::int32_t));
    for (int i = 0; i < box.numColumns && i < 16; ++i)
    {
        builder.BeginRecord(kMenuItemTypeDataColumn);
        builder.FieldBytes(1, &box.columnInfo[i], sizeof(columnInfo_s));
        builder.EndRecord();
    }
    builder.FieldString(3, box.doubleClick);
    builder.FieldBytes(4, &box.notselectable, 3 * sizeof(std::int32_t));
    builder.FieldBytes(5, box.selectBorder, sizeof(box.selectBorder));
    builder.FieldBytes(6, box.disableColor, sizeof(box.disableColor));
    FieldAsset(builder, 7, ASSET_TYPE_MATERIAL, box.selectIcon);
    builder.EndRecord();
}

// Every member is a scalar; the struct has no pointer.
void HashMenuEditField(GraphHashBuilder &builder, const editFieldDef_s &field)
{
    builder.BeginRecord(kMenuItemTypeDataEditField);
    builder.FieldBytes(1, &field.minVal, sizeof(field));
    builder.EndRecord();
}

// The counted entries: each dvar name, its string and its value.
void HashMenuMulti(GraphHashBuilder &builder, const multiDef_s &multi)
{
    builder.BeginRecord(kMenuItemTypeDataMulti);
    builder.FieldI64(1, multi.count);
    for (int i = 0; i < multi.count && i < 32; ++i)
    {
        builder.BeginRecord(kMenuItemTypeDataMultiEntry);
        builder.FieldString(1, multi.dvarList[i]);
        builder.FieldString(2, multi.dvarStr[i]);
        builder.FieldF32(3, multi.dvarValue[i]);
        builder.EndRecord();
    }
    builder.FieldI64(2, multi.strDef);
    builder.EndRecord();
}

// itemDefData_t holds a list box (6), an edit field (0, 4, 9, 10, 11, 14, 16,
// 17, 18), a multi-value (12) or an enum dvar's name (13), as the loader
// builds it.
void HashMenuItemTypeData(GraphHashBuilder &builder, const itemDef_s &item)
{
    if (!item.typeData.data)
        return;
    switch (item.type)
    {
    case 6:
        HashMenuListBox(builder, *item.typeData.listBox);
        break;
    case 12:
        HashMenuMulti(builder, *item.typeData.multi);
        break;
    case 13:
        builder.BeginRecord(kMenuItemTypeDataEnumDvar);
        builder.FieldString(1, item.typeData.enumDvarName);
        builder.EndRecord();
        break;
    default:
        HashMenuEditField(builder, *item.typeData.editField);
        break;
    }
}

enum MenuItemField : std::uint32_t
{
    kMenuItemRecord = 1,
    kMenuItemWindow,
    kMenuItemTextRect,
    kMenuItemScalars,      // type through gameMsgWindowMode, 44 bytes
    kMenuItemText,
    kMenuItemFlags,
    kMenuItemScripts,      // the ten scripts after parent
    kMenuItemOnKey,
    kMenuItemEnableDvar,
    kMenuItemDvarFlags,
    kMenuItemFocusSound,
    kMenuItemSpecial,
    kMenuItemCursorPos,
    kMenuItemTypeData,
    kMenuItemImageTrack,
    kMenuItemVisibleExp,
    kMenuItemTextExp,
    kMenuItemMaterialExp,
    kMenuItemRectXExp,
    kMenuItemRectYExp,
    kMenuItemRectWExp,
    kMenuItemRectHExp,
    kMenuItemForecolorAExp,
};

// An item: its window, scalars, scripts, key handlers, focus sound, type data
// and statements. parent is the menu the publisher attaches, not file data.
void HashMenuItem(GraphHashBuilder &builder, const itemDef_s &item)
{
    builder.BeginRecord(kMenuItemRecord);
    HashMenuWindow(builder, kMenuItemWindow, item.window);
    builder.FieldBytes(kMenuItemTextRect, item.textRect, sizeof(item.textRect));
    builder.FieldBytes(kMenuItemScalars, &item.type, 11 * sizeof(std::int32_t));
    builder.FieldString(kMenuItemText, item.text);
    builder.FieldI64(kMenuItemFlags, item.itemFlags);
    const char *const scripts[] = {item.mouseEnterText, item.mouseExitText, item.mouseEnter, item.mouseExit,
                                   item.action,        item.onAccept,     item.onFocus,   item.leaveFocus,
                                   item.dvar,          item.dvarTest};
    for (const char *script : scripts)
        builder.FieldString(kMenuItemScripts, script);
    HashMenuKeyHandlers(builder, kMenuItemOnKey, item.onKey);
    builder.FieldString(kMenuItemEnableDvar, item.enableDvar);
    builder.FieldI64(kMenuItemDvarFlags, item.dvarFlags);
    FieldAsset(builder, kMenuItemFocusSound, ASSET_TYPE_SOUND, item.focusSound);
    builder.FieldF32(kMenuItemSpecial, item.special);
    HashMenuItemTypeData(builder, item);
    HashMenuStatement(builder, kMenuItemVisibleExp, item.visibleExp);
    HashMenuStatement(builder, kMenuItemTextExp, item.textExp);
    HashMenuStatement(builder, kMenuItemMaterialExp, item.materialExp);
    HashMenuStatement(builder, kMenuItemRectXExp, item.rectXExp);
    HashMenuStatement(builder, kMenuItemRectYExp, item.rectYExp);
    HashMenuStatement(builder, kMenuItemRectWExp, item.rectWExp);
    HashMenuStatement(builder, kMenuItemRectHExp, item.rectHExp);
    HashMenuStatement(builder, kMenuItemForecolorAExp, item.forecolorAExp);
    builder.EndRecord();
}

enum MenuDefField : std::uint32_t
{
    kMenuDefWindow = 1,
    kMenuDefFont,
    kMenuDefScalars,       // fullScreen, itemCount, fontIndex
    kMenuDefFadeCycle,
    kMenuDefFade,          // fadeClamp through blurRadius, 16 bytes
    kMenuDefOnOpen,
    kMenuDefOnClose,
    kMenuDefOnEsc,
    kMenuDefOnKey,
    kMenuDefVisibleExp,
    kMenuDefAllowedBinding,
    kMenuDefSoundName,
    kMenuDefImageTrack,
    kMenuDefColors,
    kMenuDefRectXExp,
    kMenuDefRectYExp,
    kMenuDefItem,
};

// The menu: its window, scalars, scripts, key handlers, statements, then each
// counted item. cursorItem is navigation state the client advances (Menu_Init
// and Menu_SetCursorItem write it), so it stays out of the walk. fadeCycle is
// loader-copied - schema 24-menu.schema carries it in the fontIndex..blurRadius
// run - and no code advances it, so it is hashed like every other disk field.
void HashMenu(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const menuDef_t &menu = *header.menu;
    HashMenuWindow(builder, kMenuDefWindow, menu.window);
    builder.FieldString(kMenuDefFont, menu.font);
    builder.FieldBytes(kMenuDefScalars, &menu.fullScreen, 3 * sizeof(std::int32_t));
    builder.FieldI64(kMenuDefFadeCycle, menu.fadeCycle);
    builder.FieldBytes(kMenuDefFade, &menu.fadeClamp, 4 * sizeof(float));
    builder.FieldString(kMenuDefOnOpen, menu.onOpen);
    builder.FieldString(kMenuDefOnClose, menu.onClose);
    builder.FieldString(kMenuDefOnEsc, menu.onESC);
    HashMenuKeyHandlers(builder, kMenuDefOnKey, menu.onKey);
    HashMenuStatement(builder, kMenuDefVisibleExp, menu.visibleExp);
    builder.FieldString(kMenuDefAllowedBinding, menu.allowedBinding);
    builder.FieldString(kMenuDefSoundName, menu.soundName);
    builder.FieldBytes(kMenuDefColors, menu.focusColor, 8 * sizeof(float));
    HashMenuStatement(builder, kMenuDefRectXExp, menu.rectXExp);
    HashMenuStatement(builder, kMenuDefRectYExp, menu.rectYExp);
    const int items = menu.items && menu.itemCount > 0 ? menu.itemCount : 0;
    for (int i = 0; i < items; ++i)
        HashMenuItem(builder, *menu.items[i]);
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

// ---- ClipMapPvs ----

// One record tag per nested record, so a field tag cannot collide with a
// neighbouring record's.
constexpr std::uint32_t kClipMapRecord = 1;
constexpr std::uint32_t kClipMapStaticModelRecord = 2;
constexpr std::uint32_t kClipMapBrushSideRecord = 3;
constexpr std::uint32_t kClipMapNodeRecord = 4;
constexpr std::uint32_t kClipMapLeafBrushNodeRecord = 5;
constexpr std::uint32_t kClipMapPartitionRecord = 6;
constexpr std::uint32_t kClipMapBrushRecord = 7;
constexpr std::uint32_t kClipMapDynEntityRecord = 8;
constexpr std::uint32_t kClipMapPiecesRecord = 9;
constexpr std::uint32_t kClipMapPieceRecord = 10;

enum ClipMapField : std::uint32_t
{
    kClipMapName = 1,
    kClipMapChecksum,
    kClipMapPlaneCount,
    kClipMapPlanes,
    kClipMapStaticModelCount,
    kClipMapStaticModelXModel,
    kClipMapStaticModelOrigin,
    kClipMapStaticModelAxis,
    kClipMapStaticModelAbsMin,
    kClipMapStaticModelAbsMax,
    kClipMapMaterialCount,
    kClipMapMaterials,
    kClipMapBrushSideCount,
    kClipMapBrushSidePlane,
    kClipMapBrushSideMaterial,
    kClipMapBrushSideAdjacentOffset,
    kClipMapBrushSideEdgeCount,
    kClipMapBrushEdgeCount,
    kClipMapBrushEdges,
    kClipMapNodeCount,
    kClipMapNodePlane,
    kClipMapNodeChildren,
    kClipMapLeafCount,
    kClipMapLeafs,
    kClipMapLeafBrushNodeCount,
    kClipMapLeafBrushAxis,
    kClipMapLeafBrushCount,
    kClipMapLeafBrushContents,
    kClipMapLeafBrushIndices,
    kClipMapLeafBrushChildren,
    kClipMapLeafBrushesCount,
    kClipMapLeafBrushes,
    kClipMapLeafSurfaceCount,
    kClipMapLeafSurfaces,
    kClipMapVertexCount,
    kClipMapVerts,
    kClipMapTriangleCount,
    kClipMapTriIndices,
    kClipMapWalkable,
    kClipMapBorderCount,
    kClipMapBorders,
    kClipMapPartitionCount,
    kClipMapPartitionTriCount,
    kClipMapPartitionBorderCount,
    kClipMapPartitionFirstTri,
    kClipMapPartitionBorders,
    kClipMapAabbTreeCount,
    kClipMapAabbTrees,
    kClipMapSubModelCount,
    kClipMapCmodels,
    kClipMapBrushCount,
    kClipMapBrushMins,
    kClipMapBrushContents,
    kClipMapBrushMaxs,
    kClipMapBrushSides,
    kClipMapBrushAxialMaterials,
    kClipMapBrushAdjacency,
    kClipMapBrushAdjacentOffsets,
    kClipMapBrushEdgeCounts,
    kClipMapClusterCount,
    kClipMapClusterBytes,
    kClipMapVisibility,
    kClipMapMapEnts,
    kClipMapBoxBrush,
    kClipMapBoxModel,
    kClipMapDynEntCount,
    kClipMapDynType,
    kClipMapDynPose,
    kClipMapDynModel,
    kClipMapDynBrushModel,
    kClipMapDynPhysicsBrushModel,
    kClipMapDynDestroyFx,
    kClipMapDynDestroyPieces,
    kClipMapDynPhysPreset,
    kClipMapDynHealth,
    kClipMapDynMass,
    kClipMapDynContents,
    kClipMapPiecesName,
    kClipMapPiecesCount,
    kClipMapPieceModel,
    kClipMapPieceOffset,
};

// A span whose records keep their layout at both widths: the struct's count,
// then its elements as bytes. A null span keeps no bytes even when the count
// is nonzero, exactly as the loader leaves it.
void HashClipMapSpan(GraphHashBuilder &builder, std::uint32_t countTag, std::uint32_t bytesTag, const void *data,
                     std::int64_t count, std::size_t elementBytes)
{
    builder.FieldI64(countTag, count);
    builder.FieldBytes(bytesTag, data, data && count > 0 ? static_cast<std::size_t>(count) * elementBytes : 0);
}

// A static model: its model by name, then the placement fields. Not
// writable.nextModelInWorldSector: the renderer links placed models into
// world sectors at run time.
void HashClipMapStaticModel(GraphHashBuilder &builder, const cStaticModel_s &model)
{
    builder.BeginRecord(kClipMapStaticModelRecord);
    FieldAsset(builder, kClipMapStaticModelXModel, ASSET_TYPE_XMODEL, model.xmodel);
    builder.FieldBytes(kClipMapStaticModelOrigin, model.origin, sizeof(model.origin));
    builder.FieldBytes(kClipMapStaticModelAxis, model.invScaledAxis, sizeof(model.invScaledAxis));
    builder.FieldBytes(kClipMapStaticModelAbsMin, model.absmin, sizeof(model.absmin));
    builder.FieldBytes(kClipMapStaticModelAbsMax, model.absmax, sizeof(model.absmax));
    builder.EndRecord();
}

// A brush side: the plane it names by value, since the loader resolves the
// offset into block 4 and the plane array is hashed on its own too.
void HashClipMapBrushSide(GraphHashBuilder &builder, const cbrushside_t &side)
{
    builder.BeginRecord(kClipMapBrushSideRecord);
    builder.FieldBytes(kClipMapBrushSidePlane, side.plane, side.plane ? sizeof(cplane_s) : 0);
    builder.FieldU64(kClipMapBrushSideMaterial, side.materialNum);
    builder.FieldI64(kClipMapBrushSideAdjacentOffset, side.firstAdjacentSideOffset);
    builder.FieldU64(kClipMapBrushSideEdgeCount, side.edgeCount);
    builder.EndRecord();
}

void HashClipMapNode(GraphHashBuilder &builder, const cNode_t &node)
{
    builder.BeginRecord(kClipMapNodeRecord);
    builder.FieldBytes(kClipMapNodePlane, node.plane, node.plane ? sizeof(cplane_s) : 0);
    for (const std::int16_t child : node.children)
        builder.FieldI64(kClipMapNodeChildren, child);
    builder.EndRecord();
}

// A leaf-brush node: with brushes it names brush indices, without them it
// holds the split children the loader copies whole.
void HashClipMapLeafBrushNode(GraphHashBuilder &builder, const cLeafBrushNode_s &node)
{
    builder.BeginRecord(kClipMapLeafBrushNodeRecord);
    builder.FieldU64(kClipMapLeafBrushAxis, node.axis);
    builder.FieldI64(kClipMapLeafBrushCount, node.leafBrushCount);
    builder.FieldI64(kClipMapLeafBrushContents, node.contents);
    if (node.leafBrushCount > 0)
    {
        builder.FieldBytes(kClipMapLeafBrushIndices, node.data.leaf.brushes,
                           node.data.leaf.brushes ? static_cast<std::size_t>(node.leafBrushCount) * sizeof(uint16_t) : 0);
    }
    else
    {
        builder.FieldBytes(kClipMapLeafBrushChildren, &node.data.children, sizeof(node.data.children));
    }
    builder.EndRecord();
}

void HashClipMapPartition(GraphHashBuilder &builder, const CollisionPartition &partition)
{
    builder.BeginRecord(kClipMapPartitionRecord);
    builder.FieldU64(kClipMapPartitionTriCount, partition.triCount);
    builder.FieldU64(kClipMapPartitionBorderCount, partition.borderCount);
    builder.FieldI64(kClipMapPartitionFirstTri, partition.firstTri);
    builder.FieldBytes(kClipMapPartitionBorders, partition.borders,
                       partition.borders && partition.borderCount > 0
                           ? static_cast<std::size_t>(partition.borderCount) * sizeof(CollisionBorder)
                           : 0);
    builder.EndRecord();
}

// A brush: its sides as the records they name, then the adjacency extent the
// loader derives from the side offsets, which is how many edge bytes the
// brush owns.
void HashClipMapBrush(GraphHashBuilder &builder, const cbrush_t &brush)
{
    builder.BeginRecord(kClipMapBrushRecord);
    builder.FieldBytes(kClipMapBrushMins, brush.mins, sizeof(brush.mins));
    builder.FieldI64(kClipMapBrushContents, brush.contents);
    builder.FieldBytes(kClipMapBrushMaxs, brush.maxs, sizeof(brush.maxs));
    builder.FieldU64(kClipMapBrushSides, brush.numsides);
    for (std::uint32_t i = 0; brush.sides && i < brush.numsides; ++i)
        HashClipMapBrushSide(builder, brush.sides[i]);
    builder.FieldBytes(kClipMapBrushAxialMaterials, brush.axialMaterialNum, sizeof(brush.axialMaterialNum));
    std::uint32_t adjacency = 0;
    if (!db::validation::ClipMapBrushAdjacencyPrefixExtent(brush, &adjacency) || !brush.baseAdjacentSide)
        adjacency = 0;
    builder.FieldBytes(kClipMapBrushAdjacency, adjacency ? brush.baseAdjacentSide : nullptr, adjacency);
    builder.FieldBytes(kClipMapBrushAdjacentOffsets, brush.firstAdjacentSideOffsets, sizeof(brush.firstAdjacentSideOffsets));
    builder.FieldBytes(kClipMapBrushEdgeCounts, brush.edgeCount, sizeof(brush.edgeCount));
    builder.EndRecord();
}

// Model pieces: a completed object the def names, hashed by content so a
// shared object and a duplicated one agree. Counted pieces only; the loader
// loads all 32 strings of a multi def but pieces are exactly numpieces.
void HashClipMapPieces(GraphHashBuilder &builder, const XModelPieces &pieces)
{
    builder.BeginRecord(kClipMapPiecesRecord);
    builder.FieldString(kClipMapPiecesName, pieces.name);
    const std::int64_t count = pieces.numpieces > 0 ? pieces.numpieces : 0;
    builder.FieldI64(kClipMapPiecesCount, count);
    for (std::int64_t i = 0; pieces.pieces && i < count; ++i)
    {
        builder.BeginRecord(kClipMapPieceRecord);
        FieldAsset(builder, kClipMapPieceModel, ASSET_TYPE_XMODEL, pieces.pieces[i].model);
        builder.FieldBytes(kClipMapPieceOffset, pieces.pieces[i].offset, sizeof(pieces.pieces[i].offset));
        builder.EndRecord();
    }
    builder.EndRecord();
}

// A dynamic entity's def: its model, effect, pieces and preset by name, its
// pose, mass and contents by value. Not the pose/client/collision arrays the
// loader keeps in block 1: those are per-round run-time state.
void HashClipMapDynEntity(GraphHashBuilder &builder, const DynEntityDef &def)
{
    builder.BeginRecord(kClipMapDynEntityRecord);
    builder.FieldI64(kClipMapDynType, static_cast<std::int64_t>(def.type));
    builder.FieldBytes(kClipMapDynPose, &def.pose, sizeof(def.pose));
    FieldAsset(builder, kClipMapDynModel, ASSET_TYPE_XMODEL, def.xModel);
    builder.FieldU64(kClipMapDynBrushModel, def.brushModel);
    builder.FieldU64(kClipMapDynPhysicsBrushModel, def.physicsBrushModel);
    FieldAsset(builder, kClipMapDynDestroyFx, ASSET_TYPE_FX, def.destroyFx);
    builder.FieldU64(kClipMapDynDestroyPieces, def.destroyPieces != nullptr);
    if (def.destroyPieces)
        HashClipMapPieces(builder, *def.destroyPieces);
    FieldAsset(builder, kClipMapDynPhysPreset, ASSET_TYPE_PHYSPRESET, def.physPreset);
    builder.FieldI64(kClipMapDynHealth, def.health);
    for (const float value : def.mass.centerOfMass)
        builder.FieldF32(kClipMapDynMass, value);
    for (const float value : def.mass.momentsOfInertia)
        builder.FieldF32(kClipMapDynMass, value);
    for (const float value : def.mass.productsOfInertia)
        builder.FieldF32(kClipMapDynMass, value);
    builder.FieldI64(kClipMapDynContents, def.contents);
    builder.EndRecord();
}

// ClipMap and ClipMapPvs: the collision world, its brushes and the dynamic
// entities. Not isInUse or vised (run-time flags), nor the dynamic entities'
// block-1 pose/client/collision arrays.
void HashClipMapPvs(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const clipMap_t &map = *header.clipMap;
    builder.BeginRecord(kClipMapRecord);
    builder.FieldString(kClipMapName, map.name);
    builder.FieldU64(kClipMapChecksum, map.checksum);

    HashClipMapSpan(builder, kClipMapPlaneCount, kClipMapPlanes, map.planes, map.planeCount, sizeof(cplane_s));

    const std::int64_t staticModels = map.numStaticModels;
    builder.FieldI64(kClipMapStaticModelCount, staticModels);
    for (std::int64_t i = 0; map.staticModelList && i < staticModels; ++i)
        HashClipMapStaticModel(builder, map.staticModelList[i]);

    HashClipMapSpan(builder, kClipMapMaterialCount, kClipMapMaterials, map.materials, map.numMaterials,
                    sizeof(dmaterial_t));

    const std::int64_t sides = map.numBrushSides;
    builder.FieldI64(kClipMapBrushSideCount, sides);
    for (std::int64_t i = 0; map.brushsides && i < sides; ++i)
        HashClipMapBrushSide(builder, map.brushsides[i]);

    HashClipMapSpan(builder, kClipMapBrushEdgeCount, kClipMapBrushEdges, map.brushEdges, map.numBrushEdges, 1);

    const std::int64_t nodes = map.numNodes;
    builder.FieldI64(kClipMapNodeCount, nodes);
    for (std::int64_t i = 0; map.nodes && i < nodes; ++i)
        HashClipMapNode(builder, map.nodes[i]);

    HashClipMapSpan(builder, kClipMapLeafCount, kClipMapLeafs, map.leafs, map.numLeafs, sizeof(cLeaf_t));

    const std::int64_t leafBrushNodes = map.leafbrushNodesCount;
    builder.FieldI64(kClipMapLeafBrushNodeCount, leafBrushNodes);
    for (std::int64_t i = 0; map.leafbrushNodes && i < leafBrushNodes; ++i)
        HashClipMapLeafBrushNode(builder, map.leafbrushNodes[i]);

    HashClipMapSpan(builder, kClipMapLeafBrushesCount, kClipMapLeafBrushes, map.leafbrushes, map.numLeafBrushes,
                    sizeof(std::uint16_t));
    HashClipMapSpan(builder, kClipMapLeafSurfaceCount, kClipMapLeafSurfaces, map.leafsurfaces, map.numLeafSurfaces,
                    sizeof(std::uint32_t));
    HashClipMapSpan(builder, kClipMapVertexCount, kClipMapVerts, map.verts, map.vertCount, 3 * sizeof(float));

    const std::int64_t triangleCount = map.triCount > 0 ? map.triCount : 0;
    builder.FieldI64(kClipMapTriangleCount, triangleCount);
    builder.FieldBytes(kClipMapTriIndices, map.triIndices,
                       map.triIndices && triangleCount ? static_cast<std::size_t>(triangleCount) * 3 * sizeof(std::uint16_t)
                                                       : 0);
    const std::int64_t walkableBytes = 4 * ((3 * triangleCount + 31) / 32);
    builder.FieldBytes(kClipMapWalkable, map.triEdgeIsWalkable,
                       map.triEdgeIsWalkable ? static_cast<std::size_t>(walkableBytes) : 0);

    HashClipMapSpan(builder, kClipMapBorderCount, kClipMapBorders, map.borders, map.borderCount,
                    sizeof(CollisionBorder));

    const std::int64_t partitions = map.partitionCount;
    builder.FieldI64(kClipMapPartitionCount, partitions);
    for (std::int64_t i = 0; map.partitions && i < partitions; ++i)
        HashClipMapPartition(builder, map.partitions[i]);

    HashClipMapSpan(builder, kClipMapAabbTreeCount, kClipMapAabbTrees, map.aabbTrees, map.aabbTreeCount,
                    sizeof(CollisionAabbTree));
    HashClipMapSpan(builder, kClipMapSubModelCount, kClipMapCmodels, map.cmodels, map.numSubModels, sizeof(cmodel_t));

    const std::int64_t brushes = map.numBrushes;
    builder.FieldI64(kClipMapBrushCount, brushes);
    for (std::int64_t i = 0; map.brushes && i < brushes; ++i)
        HashClipMapBrush(builder, map.brushes[i]);

    builder.FieldI64(kClipMapClusterCount, map.numClusters);
    builder.FieldI64(kClipMapClusterBytes, map.clusterBytes);
    const std::int64_t visibilityBytes =
        map.numClusters > 0 && map.clusterBytes > 0 ? static_cast<std::int64_t>(map.numClusters) * map.clusterBytes : 0;
    builder.FieldBytes(kClipMapVisibility, map.visibility,
                       map.visibility ? static_cast<std::size_t>(visibilityBytes) : 0);

    FieldAsset(builder, kClipMapMapEnts, ASSET_TYPE_MAP_ENTS, map.mapEnts);
    builder.FieldU64(kClipMapBoxBrush, map.box_brush != nullptr);
    if (map.box_brush)
        HashClipMapBrush(builder, *map.box_brush);
    builder.FieldBytes(kClipMapBoxModel, &map.box_model, sizeof(map.box_model));

    for (int list = 0; list < 2; ++list)
    {
        const std::int64_t defs = map.dynEntCount[list];
        builder.FieldI64(kClipMapDynEntCount, defs);
        for (std::int64_t i = 0; map.dynEntDefList[list] && i < defs; ++i)
            HashClipMapDynEntity(builder, map.dynEntDefList[list][i]);
    }
    builder.EndRecord();
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

// FX element types, in the order the loader's fast-file schema names them.
constexpr std::uint8_t kFxElemModel = 5;
constexpr std::uint8_t kFxElemOmniLight = 6;
constexpr std::uint8_t kFxElemSpotLight = 7;
constexpr std::uint8_t kFxElemSound = 8;
constexpr std::uint8_t kFxElemDecal = 9;
constexpr std::uint8_t kFxElemRunner = 10;

void HashFxFloatRange(GraphHashBuilder &builder, std::uint32_t tag, const FxFloatRange &range)
{
    builder.FieldF32(tag, range.base);
    builder.FieldF32(tag, range.amplitude);
}

void HashFxIntRange(GraphHashBuilder &builder, std::uint32_t tag, const FxIntRange &range)
{
    builder.FieldI64(tag, range.base);
    builder.FieldI64(tag, range.amplitude);
}

// A visual's value depends on the element type that selects the union member.
void HashFxVisual(GraphHashBuilder &builder, std::uint32_t tag, std::uint8_t type, const FxElemVisuals &visual)
{
    switch (type)
    {
    case kFxElemModel:
        FieldAsset(builder, tag, ASSET_TYPE_XMODEL, visual.model);
        break;
    case kFxElemRunner:
        FieldAsset(builder, tag, ASSET_TYPE_FX, visual.effectDef.handle);
        break;
    case kFxElemSound:
        builder.FieldString(tag, visual.soundName);
        break;
    default:
        FieldAsset(builder, tag, ASSET_TYPE_MATERIAL, visual.material);
        break;
    }
}

// A light has no stored visual; a decal has two materials per mark; otherwise
// one visual sits in the record and a larger set is an array.
void HashFxVisuals(GraphHashBuilder &builder, const FxElemDef &elem)
{
    if (elem.elemType == kFxElemOmniLight || elem.elemType == kFxElemSpotLight || !elem.visualCount)
        return;
    if (elem.elemType == kFxElemDecal)
    {
        const std::uint8_t count = elem.visuals.markArray ? elem.visualCount : 0;
        for (std::uint8_t mark = 0; mark < count; ++mark)
        {
            FieldAsset(builder, 27, ASSET_TYPE_MATERIAL, elem.visuals.markArray[mark].materials[0]);
            FieldAsset(builder, 27, ASSET_TYPE_MATERIAL, elem.visuals.markArray[mark].materials[1]);
        }
        return;
    }
    if (elem.visualCount == 1)
    {
        HashFxVisual(builder, 27, elem.elemType, elem.visuals.instance);
        return;
    }
    const std::uint8_t count = elem.visuals.array ? elem.visualCount : 0;
    for (std::uint8_t visual = 0; visual < count; ++visual)
        HashFxVisual(builder, 27, elem.elemType, elem.visuals.array[visual]);
}

void HashFxTrail(GraphHashBuilder &builder, const FxTrailDef *trail)
{
    builder.FieldU64(35, trail != nullptr);
    if (!trail)
        return;
    builder.FieldI64(36, trail->scrollTimeMsec);
    builder.FieldI64(37, trail->repeatDist);
    builder.FieldI64(38, trail->splitDist);
    builder.FieldI64(39, trail->vertCount);
    builder.FieldBytes(40, trail->verts,
        trail->verts && trail->vertCount > 0 ? static_cast<std::size_t>(trail->vertCount) * sizeof(FxTrailVertex) : 0);
    builder.FieldI64(41, trail->indCount);
    builder.FieldBytes(42, trail->inds,
        trail->inds && trail->indCount > 0 ? static_cast<std::size_t>(trail->indCount) * sizeof(std::uint16_t) : 0);
}

// One element: the scalar ranges, the sample arrays, the type-selected
// visuals, the effects it names, and its trail. Samples and trail bytes have
// the same layout at both widths.
void HashFxElement(GraphHashBuilder &builder, const FxElemDef &elem)
{
    builder.FieldI64(1, elem.flags);
    builder.FieldI64(2, elem.spawn.looping.intervalMsec);
    builder.FieldI64(3, elem.spawn.looping.count);
    HashFxFloatRange(builder, 4, elem.spawnRange);
    HashFxFloatRange(builder, 5, elem.fadeInRange);
    HashFxFloatRange(builder, 6, elem.fadeOutRange);
    builder.FieldF32(7, elem.spawnFrustumCullRadius);
    HashFxIntRange(builder, 8, elem.spawnDelayMsec);
    HashFxIntRange(builder, 9, elem.lifeSpanMsec);
    for (const FxFloatRange &range : elem.spawnOrigin)
        HashFxFloatRange(builder, 10, range);
    HashFxFloatRange(builder, 11, elem.spawnOffsetRadius);
    HashFxFloatRange(builder, 12, elem.spawnOffsetHeight);
    for (const FxFloatRange &range : elem.spawnAngles)
        HashFxFloatRange(builder, 13, range);
    for (const FxFloatRange &range : elem.angularVelocity)
        HashFxFloatRange(builder, 14, range);
    HashFxFloatRange(builder, 15, elem.initialRotation);
    HashFxFloatRange(builder, 16, elem.gravity);
    HashFxFloatRange(builder, 17, elem.reflectionFactor);
    builder.FieldU64(18, elem.atlas.behavior);
    builder.FieldU64(18, elem.atlas.index);
    builder.FieldU64(18, elem.atlas.fps);
    builder.FieldU64(18, elem.atlas.loopCount);
    builder.FieldU64(18, elem.atlas.colIndexBits);
    builder.FieldU64(18, elem.atlas.rowIndexBits);
    builder.FieldI64(18, elem.atlas.entryCount);
    builder.FieldU64(19, elem.elemType);
    builder.FieldU64(20, elem.visualCount);
    builder.FieldU64(21, elem.velIntervalCount);
    builder.FieldU64(22, elem.visStateIntervalCount);
    const int velCount = elem.velSamples ? elem.velIntervalCount + 1 : 0;
    builder.FieldI64(23, velCount);
    builder.FieldBytes(24, elem.velSamples, static_cast<std::size_t>(velCount) * sizeof(FxElemVelStateSample));
    const int visCount = elem.visSamples ? elem.visStateIntervalCount + 1 : 0;
    builder.FieldI64(25, visCount);
    builder.FieldBytes(26, elem.visSamples, static_cast<std::size_t>(visCount) * sizeof(FxElemVisStateSample));
    HashFxVisuals(builder, elem);
    builder.FieldBytes(28, elem.collMins, sizeof(elem.collMins));
    builder.FieldBytes(29, elem.collMaxs, sizeof(elem.collMaxs));
    FieldAsset(builder, 30, ASSET_TYPE_FX, elem.effectOnImpact.handle);
    FieldAsset(builder, 31, ASSET_TYPE_FX, elem.effectOnDeath.handle);
    FieldAsset(builder, 32, ASSET_TYPE_FX, elem.effectEmitted.handle);
    HashFxFloatRange(builder, 33, elem.emitDist);
    HashFxFloatRange(builder, 34, elem.emitDistVariance);
    HashFxTrail(builder, elem.trailDef);
    builder.FieldU64(43, elem.sortOrder);
    builder.FieldU64(44, elem.lightingFrac);
    builder.FieldU64(45, elem.useItemClip);
}

void HashFxEffectDef(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const FxEffectDef &effect = *header.fx;
    builder.FieldString(1, effect.name);
    builder.FieldI64(2, effect.flags);
    builder.FieldI64(3, effect.totalSize);
    builder.FieldI64(4, effect.msecLoopingLife);
    builder.FieldI64(5, effect.elemDefCountLooping);
    builder.FieldI64(6, effect.elemDefCountOneShot);
    builder.FieldI64(7, effect.elemDefCountEmission);
    builder.FieldU64(8, effect.elemDefs != nullptr);
    const int count = effect.elemDefs ? effect.elemDefCountLooping + effect.elemDefCountOneShot + effect.elemDefCountEmission : 0;
    builder.FieldI64(9, count);
    for (int index = 0; index < count; ++index)
    {
        builder.BeginRecord(10);
        HashFxElement(builder, effect.elemDefs[index]);
        builder.EndRecord();
    }
}

// ImpactFx has no per-entry scalar: its twelve surface entries are arrays of
// effect references, hashed by the referenced effect's name.
void HashFxImpactTable(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const FxImpactTable &table = *header.impactFx;
    builder.FieldString(1, table.name);
    builder.FieldU64(2, table.table != nullptr);
    const std::uint32_t count = table.table ? 12u : 0u;
    builder.FieldU64(3, count);
    for (std::uint32_t surface = 0; surface < count; ++surface)
    {
        builder.BeginRecord(4);
        for (const FxEffectDef *effect : table.table[surface].nonflesh)
            FieldAsset(builder, 1, ASSET_TYPE_FX, effect);
        for (const FxEffectDef *effect : table.table[surface].flesh)
            FieldAsset(builder, 2, ASSET_TYPE_FX, effect);
        builder.EndRecord();
    }
}

// ---- XModelPieces ----

// XModelPieces is a completed object the loader publishes through the block-4
// relocation registry, never through the asset pool: only DynEntityDef's
// destroyPieces names one (db_load.cpp Load_XModelPiecesPtr). The family
// source below therefore enumerates the registry's published objects rather
// than DB_EnumXAssets, which is structurally empty for this type. The fields
// are the same ones the ClipMap dyn-entity walk hashes inline; a shared object
// and a duplicated one agree because the hash is by content, not identity.
constexpr std::uint32_t kPiecesPresent = 1;
constexpr std::uint32_t kPiecesName = 2;
constexpr std::uint32_t kPiecesCount = 3;
constexpr std::uint32_t kPiecesEntryRecord = 1;
constexpr std::uint32_t kPiecesEntryModel = 1;
constexpr std::uint32_t kPiecesEntryOffset = 2;

void HashXModelPiecesContent(GraphHashBuilder &builder, const XModelPieces &pieces)
{
    builder.FieldString(kPiecesName, pieces.name);
    const std::int64_t count = pieces.numpieces > 0 ? pieces.numpieces : 0;
    builder.FieldI64(kPiecesCount, count);
    for (std::int64_t i = 0; pieces.pieces && i < count; ++i)
    {
        builder.BeginRecord(kPiecesEntryRecord);
        FieldAsset(builder, kPiecesEntryModel, ASSET_TYPE_XMODEL, pieces.pieces[i].model);
        builder.FieldBytes(kPiecesEntryOffset, pieces.pieces[i].offset, sizeof(pieces.pieces[i].offset));
        builder.EndRecord();
    }
}

void HashXModelPieces(GraphHashBuilder &builder, const XAssetHeader &header)
{
    builder.FieldU64(kPiecesPresent, header.xmodelPieces != nullptr);
    if (header.xmodelPieces)
        HashXModelPiecesContent(builder, *header.xmodelPieces);
}

void CollectPublishedPieces(void *context, std::uintptr_t native)
{
    auto *assets = static_cast<std::vector<XAssetHeader> *>(context);
    XAssetHeader header;
    header.xmodelPieces = reinterpret_cast<XModelPieces *>(native);
    assets->push_back(header);
}

using Walker = void (*)(GraphHashBuilder &, const XAssetHeader &);

// GfxWorld: the renderer's world as the disk32 loader leaves it at 64-bit.
// Records the loader converts into native storage are walked by content, never
// by native bytes: a surface and a draw instance widen, a portal's cell is a
// pointer, and an AABB tree's childrenOffset is rewritten to the native
// stride, so it hashes as the child index it names. Block-1 buffers the
// renderer fills (visibility bytes, LOD and draw keys, cell bits, textures,
// D3D vertex buffers) and runtime values (usageCount, the portal writable
// state) stay out. Every field the loader copies from disk is hashed,
// including the static-model cache indices, except a leaf's unused offset.
constexpr std::uint32_t kGfxWorldRecord = 1;
constexpr std::uint32_t kGfxWorldSunParseRecord = 2;
constexpr std::uint32_t kGfxWorldDpvsPlanesRecord = 3;
constexpr std::uint32_t kGfxWorldDpvsStaticRecord = 4;
constexpr std::uint32_t kGfxWorldDpvsDynamicRecord = 5;
constexpr std::uint32_t kGfxWorldLightGridRecord = 6;
constexpr std::uint32_t kGfxWorldCellRecord = 7;
constexpr std::uint32_t kGfxWorldTreeRecord = 8;
constexpr std::uint32_t kGfxWorldPortalRecord = 9;
constexpr std::uint32_t kGfxWorldSurfaceRecord = 10;
constexpr std::uint32_t kGfxWorldSModelInstRecord = 11;
constexpr std::uint32_t kGfxWorldSModelDrawRecord = 12;
constexpr std::uint32_t kGfxWorldCullGroupRecord = 13;
constexpr std::uint32_t kGfxWorldProbeRecord = 14;
constexpr std::uint32_t kGfxWorldLightmapRecord = 15;
constexpr std::uint32_t kGfxWorldBrushModelRecord = 16;
constexpr std::uint32_t kGfxWorldMaterialMemoryRecord = 17;
constexpr std::uint32_t kGfxWorldSunFlareRecord = 18;
constexpr std::uint32_t kGfxWorldShadowGeomRecord = 19;
constexpr std::uint32_t kGfxWorldLightRegionRecord = 20;
constexpr std::uint32_t kGfxWorldLightRegionHullRecord = 21;
constexpr std::uint32_t kGfxWorldLightRecord = 22;

enum GfxWorldField : std::uint32_t
{
    kGfxWorldName = 1,
    kGfxWorldBaseName,
    kGfxWorldPlaneCount,
    kGfxWorldNodeCount,
    kGfxWorldIndexCount,
    kGfxWorldIndex,
    kGfxWorldSurfaceCount,
    kGfxWorldSkySurfCount,
    kGfxWorldSkyStartSurf,
    kGfxWorldSkyImage,
    kGfxWorldSkySamplerState,
    kGfxWorldVertexCount,
    kGfxWorldVertex,
    kGfxWorldVertexLayerDataSize,
    kGfxWorldVertexLayerData,
    kGfxWorldSunParse,
    kGfxWorldSunLightPresent,
    kGfxWorldSunLightObject,
    kGfxWorldSunColorFromBsp,
    kGfxWorldSunPrimaryLightIndex,
    kGfxWorldPrimaryLightCount,
    kGfxWorldCullGroupCount,
    kGfxWorldReflectionProbeCount,
    kGfxWorldReflectionProbe,
    kGfxWorldDpvsPlanes,
    kGfxWorldCellBitsCount,
    kGfxWorldCell,
    kGfxWorldLightmapCount,
    kGfxWorldLightmap,
    kGfxWorldLightGrid,
    kGfxWorldModelCount,
    kGfxWorldModel,
    kGfxWorldMins,
    kGfxWorldMaxs,
    kGfxWorldChecksum,
    kGfxWorldMaterialMemoryCount,
    kGfxWorldMaterialMemory,
    kGfxWorldSunFlare,
    kGfxWorldOutdoorLookup,
    kGfxWorldOutdoorImage,
    kGfxWorldShadowGeom,
    kGfxWorldLightRegion,
    kGfxWorldDpvsStatic,
    kGfxWorldDpvsDynamic,
};

enum GfxWorldSunParseField : std::uint32_t
{
    kSunParseName = 1,
    kSunParseAmbientScale,
    kSunParseAmbientColor,
    kSunParseDiffuseFraction,
    kSunParseSunLight,
    kSunParseSunColor,
    kSunParseDiffuseColor,
    kSunParseDiffuseColorSet,
    kSunParseAngles,
};

enum GfxLightField : std::uint32_t
{
    kLightType = 1,
    kLightCanUseShadowMap,
    kLightUnused,
    kLightColor,
    kLightDir,
    kLightOrigin,
    kLightRadius,
    kLightOuter,
    kLightInner,
    kLightExponent,
    kLightSpotShadow,
    kLightDef,
};

enum GfxProbeField : std::uint32_t
{
    kProbeOrigin = 1,
    kProbeImage,
};

enum GfxCellField : std::uint32_t
{
    kCellMins = 1,
    kCellMaxs,
    kCellTreeCount,
    kCellTree,
    kCellPortalCount,
    kCellPortal,
    kCellCullGroupCount,
    kCellCullGroup,
    kCellProbeCount,
    kCellProbe,
};

enum GfxTreeField : std::uint32_t
{
    kTreeMins = 1,
    kTreeMaxs,
    kTreeChildCount,
    kTreeSurfaceCount,
    kTreeStartSurf,
    kTreeSurfaceCountNoDecal,
    kTreeStartSurfNoDecal,
    kTreeSmodelCount,
    kTreeChildIndex,
    kTreeSmodel,
};

enum GfxPortalField : std::uint32_t
{
    kPortalPlane = 1,
    kPortalCell,
    kPortalVertexCount,
    kPortalVertex,
    kPortalHullAxis,
};

enum GfxSurfaceField : std::uint32_t
{
    kSurfaceVertexLayerData = 1,
    kSurfaceFirstVertex,
    kSurfaceVertexCount,
    kSurfaceTriCount,
    kSurfaceBaseIndex,
    kSurfaceMaterial,
    kSurfaceLightmap,
    kSurfaceProbe,
    kSurfacePrimaryLight,
    kSurfaceFlags,
    kSurfaceBounds,
};

enum GfxSModelInstField : std::uint32_t
{
    kSModelInstMins = 1,
    kSModelInstMaxs,
    kSModelInstGroundLighting,
};

enum GfxSModelDrawField : std::uint32_t
{
    kSModelDrawCullDist = 1,
    kSModelDrawOrigin,
    kSModelDrawAxis,
    kSModelDrawScale,
    kSModelDrawModel,
    kSModelDrawProbe,
    kSModelDrawPrimaryLight,
    kSModelDrawLightingHandle,
    kSModelDrawFlags,
    kSModelDrawCacheIndex,
};

enum GfxCullGroupField : std::uint32_t
{
    kCullGroupMins = 1,
    kCullGroupMaxs,
    kCullGroupSurfaceCount,
    kCullGroupStartSurf,
};

enum GfxLightmapField : std::uint32_t
{
    kLightmapPrimary = 1,
    kLightmapSecondary,
};

enum GfxLightGridField : std::uint32_t
{
    kLightGridHasRegions = 1,
    kLightGridSunPrimary,
    kLightGridMins,
    kLightGridMaxs,
    kLightGridRowAxis,
    kLightGridColAxis,
    kLightGridRowCount,
    kLightGridRow,
    kLightGridRawSize,
    kLightGridRaw,
    kLightGridEntryCount,
    kLightGridEntry,
    kLightGridColorCount,
    kLightGridColor,
};

enum GfxBrushModelField : std::uint32_t
{
    kBrushMins = 1,
    kBrushMaxs,
    kBrushBounds,
    kBrushSurfaceCount,
    kBrushStartSurf,
    kBrushSurfaceCountNoDecal,
};

enum GfxMaterialMemoryField : std::uint32_t
{
    kMaterialMemoryMaterial = 1,
    kMaterialMemoryBytes,
};

enum GfxSunFlareField : std::uint32_t
{
    kSunFlareValid = 1,
    kSunFlareSprite,
    kSunFlareFlare,
    kSunFlareValue,
};

enum GfxShadowGeomField : std::uint32_t
{
    kShadowGeomSurfaceCount = 1,
    kShadowGeomSurface,
    kShadowGeomSmodelCount,
    kShadowGeomSmodel,
};

enum GfxLightRegionField : std::uint32_t
{
    kLightRegionHullCount = 1,
    kLightRegionHull,
};

enum GfxLightRegionHullField : std::uint32_t
{
    kHullMidPoint = 1,
    kHullHalfSize,
    kHullAxisCount,
    kHullAxis,
};

enum GfxLightRegionAxisField : std::uint32_t
{
    kAxisDir = 1,
    kAxisMidPoint,
    kAxisHalfSize,
};

enum GfxDpvsPlanesField : std::uint32_t
{
    kDpvsPlanesCellCount = 1,
    kDpvsPlanesPlanes,
    kDpvsPlanesNodes,
};

enum GfxDpvsStaticField : std::uint32_t
{
    kDpvsSmodelCount = 1,
    kDpvsStaticSurfaceCount,
    kDpvsStaticSurfaceCountNoDecal,
    kDpvsLitBegin,
    kDpvsLitEnd,
    kDpvsDecalBegin,
    kDpvsDecalEnd,
    kDpvsEmissiveBegin,
    kDpvsEmissiveEnd,
    kDpvsSmodelVisCount,
    kDpvsSurfaceVisCount,
    kDpvsSortedSurface,
    kDpvsSmodelInst,
    kDpvsSurface,
    kDpvsCullGroup,
    kDpvsSmodelDraw,
};

enum GfxDpvsDynamicField : std::uint32_t
{
    kDpvsDynWordCount = 1,
    kDpvsDynClientCount,
};

void HashGfxLight(GraphHashBuilder &builder, const GfxLight &light)
{
    builder.BeginRecord(kGfxWorldLightRecord);
    builder.FieldU64(kLightType, light.type);
    builder.FieldU64(kLightCanUseShadowMap, light.canUseShadowMap);
    builder.FieldBytes(kLightUnused, light.unused, sizeof(light.unused));
    for (const float value : light.color)
        builder.FieldF32(kLightColor, value);
    for (const float value : light.dir)
        builder.FieldF32(kLightDir, value);
    for (const float value : light.origin)
        builder.FieldF32(kLightOrigin, value);
    builder.FieldF32(kLightRadius, light.radius);
    builder.FieldF32(kLightOuter, light.cosHalfFovOuter);
    builder.FieldF32(kLightInner, light.cosHalfFovInner);
    builder.FieldI64(kLightExponent, light.exponent);
    builder.FieldU64(kLightSpotShadow, light.spotShadowIndex);
    FieldAsset(builder, kLightDef, ASSET_TYPE_LIGHT_DEF, light.def);
    builder.EndRecord();
}

void HashGfxProbe(GraphHashBuilder &builder, const GfxReflectionProbe &probe)
{
    builder.BeginRecord(kGfxWorldProbeRecord);
    for (const float value : probe.origin)
        builder.FieldF32(kProbeOrigin, value);
    FieldAsset(builder, kProbeImage, ASSET_TYPE_IMAGE, probe.reflectionImage);
    builder.EndRecord();
}

// childrenOffset is a byte offset at the native stride; the index it names is
// the width-free value. A leaf hashes as zero: its offset carries no topology,
// and x86 rewrites it to zero while disk32 keeps the disk value. A positive
// offset is a stride multiple; anything else is the unusable sentinel.
std::int64_t GfxTreeChildIndex(const GfxAabbTree &tree)
{
    constexpr std::int64_t stride = static_cast<std::int64_t>(sizeof(GfxAabbTree));
    if (!tree.childCount)
        return 0;
    if (tree.childrenOffset <= 0 || tree.childrenOffset % stride)
        return tree.childrenOffset;
    return tree.childrenOffset / stride;
}

void HashGfxTree(GraphHashBuilder &builder, const GfxAabbTree &tree)
{
    builder.BeginRecord(kGfxWorldTreeRecord);
    for (const float value : tree.mins)
        builder.FieldF32(kTreeMins, value);
    for (const float value : tree.maxs)
        builder.FieldF32(kTreeMaxs, value);
    builder.FieldU64(kTreeChildCount, tree.childCount);
    builder.FieldU64(kTreeSurfaceCount, tree.surfaceCount);
    builder.FieldU64(kTreeStartSurf, tree.startSurfIndex);
    builder.FieldU64(kTreeSurfaceCountNoDecal, tree.surfaceCountNoDecal);
    builder.FieldU64(kTreeStartSurfNoDecal, tree.startSurfIndexNoDecal);
    builder.FieldU64(kTreeSmodelCount, tree.smodelIndexCount);
    builder.FieldI64(kTreeChildIndex, GfxTreeChildIndex(tree));
    const std::int64_t smodels = tree.smodelIndexes ? tree.smodelIndexCount : 0;
    for (std::int64_t i = 0; i < smodels; ++i)
        builder.FieldU64(kTreeSmodel, tree.smodelIndexes[i]);
    builder.EndRecord();
}

// A portal's cell is a pointer; hash its index into the world's cell array.
void HashGfxPortal(GraphHashBuilder &builder, const GfxWorld &world, const GfxPortal &portal)
{
    builder.BeginRecord(kGfxWorldPortalRecord);
    // The span includes DpvsPlane's trailing pad byte: the loader copies the
    // 20-byte disk record whole, pad included, so both widths hash it.
    builder.FieldBytes(kPortalPlane, &portal.plane, sizeof(portal.plane));
    std::int64_t cell = -1;
    if (portal.cell && world.cells)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(world.cells);
        const auto address = reinterpret_cast<std::uintptr_t>(portal.cell);
        if (address >= base && (address - base) % sizeof(GfxCell) == 0)
            cell = static_cast<std::int64_t>((address - base) / sizeof(GfxCell));
    }
    builder.FieldI64(kPortalCell, cell);
    builder.FieldU64(kPortalVertexCount, portal.vertexCount);
    const std::int64_t values = portal.vertices ? static_cast<std::int64_t>(portal.vertexCount) * 3 : 0;
    for (std::int64_t i = 0; i < values; ++i)
        builder.FieldF32(kPortalVertex, portal.vertices[i / 3][i % 3]);
    for (const auto &axis : portal.hullAxis)
        for (const float value : axis)
            builder.FieldF32(kPortalHullAxis, value);
    builder.EndRecord();
}

void HashGfxCell(GraphHashBuilder &builder, const GfxWorld &world, const GfxCell &cell)
{
    builder.BeginRecord(kGfxWorldCellRecord);
    for (const float value : cell.mins)
        builder.FieldF32(kCellMins, value);
    for (const float value : cell.maxs)
        builder.FieldF32(kCellMaxs, value);
    builder.FieldI64(kCellTreeCount, cell.aabbTreeCount);
    for (std::int64_t i = 0; cell.aabbTree && i < cell.aabbTreeCount; ++i)
        HashGfxTree(builder, cell.aabbTree[i]);
    builder.FieldI64(kCellPortalCount, cell.portalCount);
    for (std::int64_t i = 0; cell.portals && i < cell.portalCount; ++i)
        HashGfxPortal(builder, world, cell.portals[i]);
    builder.FieldI64(kCellCullGroupCount, cell.cullGroupCount);
    for (std::int64_t i = 0; cell.cullGroups && i < cell.cullGroupCount; ++i)
        builder.FieldI64(kCellCullGroup, cell.cullGroups[i]);
    builder.FieldU64(kCellProbeCount, cell.reflectionProbeCount);
    builder.FieldBytes(kCellProbe, cell.reflectionProbes, cell.reflectionProbes ? cell.reflectionProbeCount : 0);
    builder.EndRecord();
}

void HashGfxSurface(GraphHashBuilder &builder, const GfxSurface &surface)
{
    builder.BeginRecord(kGfxWorldSurfaceRecord);
    builder.FieldI64(kSurfaceVertexLayerData, surface.tris.vertexLayerData);
    builder.FieldI64(kSurfaceFirstVertex, surface.tris.firstVertex);
    builder.FieldU64(kSurfaceVertexCount, surface.tris.vertexCount);
    builder.FieldU64(kSurfaceTriCount, surface.tris.triCount);
    builder.FieldI64(kSurfaceBaseIndex, surface.tris.baseIndex);
    FieldAsset(builder, kSurfaceMaterial, ASSET_TYPE_MATERIAL, surface.material);
    builder.FieldU64(kSurfaceLightmap, surface.lightmapIndex);
    builder.FieldU64(kSurfaceProbe, surface.reflectionProbeIndex);
    builder.FieldU64(kSurfacePrimaryLight, surface.primaryLightIndex);
    builder.FieldU64(kSurfaceFlags, surface.flags);
    for (const auto &bound : surface.bounds)
        for (const float value : bound)
            builder.FieldF32(kSurfaceBounds, value);
    builder.EndRecord();
}

void HashGfxSModelInst(GraphHashBuilder &builder, const GfxStaticModelInst &inst)
{
    builder.BeginRecord(kGfxWorldSModelInstRecord);
    for (const float value : inst.mins)
        builder.FieldF32(kSModelInstMins, value);
    for (const float value : inst.maxs)
        builder.FieldF32(kSModelInstMaxs, value);
    builder.FieldU64(kSModelInstGroundLighting, inst.groundLighting.packed);
    builder.EndRecord();
}

// smodelCacheIndex is copied from disk by both loaders; the renderer rewrites
// it only after load, so the loader's value is part of the digest.
void HashGfxSModelDraw(GraphHashBuilder &builder, const GfxStaticModelDrawInst &inst)
{
    builder.BeginRecord(kGfxWorldSModelDrawRecord);
    builder.FieldF32(kSModelDrawCullDist, inst.cullDist);
    for (const float value : inst.placement.origin)
        builder.FieldF32(kSModelDrawOrigin, value);
    for (const auto &axis : inst.placement.axis)
        for (const float value : axis)
            builder.FieldF32(kSModelDrawAxis, value);
    builder.FieldF32(kSModelDrawScale, inst.placement.scale);
    FieldAsset(builder, kSModelDrawModel, ASSET_TYPE_XMODEL, inst.model);
    builder.FieldU64(kSModelDrawProbe, inst.reflectionProbeIndex);
    builder.FieldU64(kSModelDrawPrimaryLight, inst.primaryLightIndex);
    builder.FieldU64(kSModelDrawLightingHandle, inst.lightingHandle);
    builder.FieldU64(kSModelDrawFlags, inst.flags);
    for (const std::uint16_t cacheIndex : inst.smodelCacheIndex)
        builder.FieldU64(kSModelDrawCacheIndex, cacheIndex);
    builder.EndRecord();
}

void HashGfxCullGroup(GraphHashBuilder &builder, const GfxCullGroup &group)
{
    builder.BeginRecord(kGfxWorldCullGroupRecord);
    for (const float value : group.mins)
        builder.FieldF32(kCullGroupMins, value);
    for (const float value : group.maxs)
        builder.FieldF32(kCullGroupMaxs, value);
    builder.FieldI64(kCullGroupSurfaceCount, group.surfaceCount);
    builder.FieldI64(kCullGroupStartSurf, group.startSurfIndex);
    builder.EndRecord();
}

void HashGfxLightmap(GraphHashBuilder &builder, const GfxLightmapArray &lightmap)
{
    builder.BeginRecord(kGfxWorldLightmapRecord);
    FieldAsset(builder, kLightmapPrimary, ASSET_TYPE_IMAGE, lightmap.primary);
    FieldAsset(builder, kLightmapSecondary, ASSET_TYPE_IMAGE, lightmap.secondary);
    builder.EndRecord();
}

void HashGfxBrushModel(GraphHashBuilder &builder, const GfxBrushModel &model)
{
    builder.BeginRecord(kGfxWorldBrushModelRecord);
    for (const float value : model.writable.mins)
        builder.FieldF32(kBrushMins, value);
    for (const float value : model.writable.maxs)
        builder.FieldF32(kBrushMaxs, value);
    for (const auto &bound : model.bounds)
        for (const float value : bound)
            builder.FieldF32(kBrushBounds, value);
    builder.FieldU64(kBrushSurfaceCount, model.surfaceCount);
    builder.FieldU64(kBrushStartSurf, model.startSurfIndex);
    builder.FieldU64(kBrushSurfaceCountNoDecal, model.surfaceCountNoDecal);
    builder.EndRecord();
}

void HashGfxMaterialMemory(GraphHashBuilder &builder, const MaterialMemory &memory)
{
    builder.BeginRecord(kGfxWorldMaterialMemoryRecord);
    FieldAsset(builder, kMaterialMemoryMaterial, ASSET_TYPE_MATERIAL, memory.material);
    builder.FieldI64(kMaterialMemoryBytes, memory.memory);
    builder.EndRecord();
}

// The flare's 21-word run holds floats and times in schema order.
void HashGfxSunFlare(GraphHashBuilder &builder, const sunflare_t &sun)
{
    builder.BeginRecord(kGfxWorldSunFlareRecord);
    builder.FieldU64(kSunFlareValid, sun.hasValidData);
    FieldAsset(builder, kSunFlareSprite, ASSET_TYPE_MATERIAL, sun.spriteMaterial);
    FieldAsset(builder, kSunFlareFlare, ASSET_TYPE_MATERIAL, sun.flareMaterial);
    builder.FieldF32(kSunFlareValue, sun.spriteSize);
    builder.FieldF32(kSunFlareValue, sun.flareMinSize);
    builder.FieldF32(kSunFlareValue, sun.flareMinDot);
    builder.FieldF32(kSunFlareValue, sun.flareMaxSize);
    builder.FieldF32(kSunFlareValue, sun.flareMaxDot);
    builder.FieldF32(kSunFlareValue, sun.flareMaxAlpha);
    builder.FieldI64(kSunFlareValue, sun.flareFadeInTime);
    builder.FieldI64(kSunFlareValue, sun.flareFadeOutTime);
    builder.FieldF32(kSunFlareValue, sun.blindMinDot);
    builder.FieldF32(kSunFlareValue, sun.blindMaxDot);
    builder.FieldF32(kSunFlareValue, sun.blindMaxDarken);
    builder.FieldI64(kSunFlareValue, sun.blindFadeInTime);
    builder.FieldI64(kSunFlareValue, sun.blindFadeOutTime);
    builder.FieldF32(kSunFlareValue, sun.glareMinDot);
    builder.FieldF32(kSunFlareValue, sun.glareMaxDot);
    builder.FieldF32(kSunFlareValue, sun.glareMaxLighten);
    builder.FieldI64(kSunFlareValue, sun.glareFadeInTime);
    builder.FieldI64(kSunFlareValue, sun.glareFadeOutTime);
    for (const float value : sun.sunFxPosition)
        builder.FieldF32(kSunFlareValue, value);
    builder.EndRecord();
}

void HashGfxShadowGeometry(GraphHashBuilder &builder, const GfxShadowGeometry &geometry)
{
    builder.BeginRecord(kGfxWorldShadowGeomRecord);
    builder.FieldU64(kShadowGeomSurfaceCount, geometry.surfaceCount);
    for (std::int64_t i = 0; geometry.sortedSurfIndex && i < geometry.surfaceCount; ++i)
        builder.FieldU64(kShadowGeomSurface, geometry.sortedSurfIndex[i]);
    builder.FieldU64(kShadowGeomSmodelCount, geometry.smodelCount);
    for (std::int64_t i = 0; geometry.smodelIndex && i < geometry.smodelCount; ++i)
        builder.FieldU64(kShadowGeomSmodel, geometry.smodelIndex[i]);
    builder.EndRecord();
}

void HashGfxLightRegionHull(GraphHashBuilder &builder, const GfxLightRegionHull &hull)
{
    builder.BeginRecord(kGfxWorldLightRegionHullRecord);
    for (const float value : hull.kdopMidPoint)
        builder.FieldF32(kHullMidPoint, value);
    for (const float value : hull.kdopHalfSize)
        builder.FieldF32(kHullHalfSize, value);
    builder.FieldU64(kHullAxisCount, hull.axisCount);
    for (std::int64_t i = 0; hull.axis && i < hull.axisCount; ++i)
    {
        const GfxLightRegionAxis &axis = hull.axis[i];
        for (const float value : axis.dir)
            builder.FieldF32(kAxisDir, value);
        builder.FieldF32(kAxisMidPoint, axis.midPoint);
        builder.FieldF32(kAxisHalfSize, axis.halfSize);
    }
    builder.EndRecord();
}

void HashGfxLightRegion(GraphHashBuilder &builder, const GfxLightRegion &region)
{
    builder.BeginRecord(kGfxWorldLightRegionRecord);
    builder.FieldU64(kLightRegionHullCount, region.hullCount);
    for (std::int64_t i = 0; region.hulls && i < region.hullCount; ++i)
        HashGfxLightRegionHull(builder, region.hulls[i]);
    builder.EndRecord();
}

void HashGfxLightGrid(GraphHashBuilder &builder, const GfxWorld &world)
{
    const GfxLightGrid &grid = world.lightGrid;
    builder.BeginRecord(kGfxWorldLightGridRecord);
    builder.FieldU64(kLightGridHasRegions, grid.hasLightRegions);
    builder.FieldU64(kLightGridSunPrimary, grid.sunPrimaryLightIndex);
    for (const std::uint16_t value : grid.mins)
        builder.FieldU64(kLightGridMins, value);
    for (const std::uint16_t value : grid.maxs)
        builder.FieldU64(kLightGridMaxs, value);
    builder.FieldU64(kLightGridRowAxis, grid.rowAxis);
    builder.FieldU64(kLightGridColAxis, grid.colAxis);
    std::int64_t rows = 0;
    if (grid.rowDataStart && grid.rowAxis < 3)
        rows = static_cast<std::int64_t>(grid.maxs[grid.rowAxis]) - grid.mins[grid.rowAxis] + 1;
    builder.FieldI64(kLightGridRowCount, rows);
    for (std::int64_t i = 0; i < rows; ++i)
        builder.FieldU64(kLightGridRow, grid.rowDataStart[i]);
    builder.FieldU64(kLightGridRawSize, grid.rawRowDataSize);
    builder.FieldBytes(kLightGridRaw, grid.rawRowData, grid.rawRowData && grid.rawRowDataSize ? grid.rawRowDataSize : 0);
    builder.FieldU64(kLightGridEntryCount, grid.entryCount);
    builder.FieldBytes(kLightGridEntry, grid.entries,
                       grid.entries && grid.entryCount ? static_cast<std::size_t>(grid.entryCount) * sizeof(GfxLightGridEntry)
                                                       : 0);
    builder.FieldU64(kLightGridColorCount, grid.colorCount);
    builder.FieldBytes(kLightGridColor, grid.colors,
                       grid.colors && grid.colorCount ? static_cast<std::size_t>(grid.colorCount) * sizeof(GfxLightGridColors)
                                                       : 0);
    builder.EndRecord();
}

void HashGfxSunParse(GraphHashBuilder &builder, const SunLightParseParams &params)
{
    builder.BeginRecord(kGfxWorldSunParseRecord);
    builder.FieldBytes(kSunParseName, params.name, sizeof(params.name));
    builder.FieldF32(kSunParseAmbientScale, params.ambientScale);
    for (const float value : params.ambientColor)
        builder.FieldF32(kSunParseAmbientColor, value);
    builder.FieldF32(kSunParseDiffuseFraction, params.diffuseFraction);
    builder.FieldF32(kSunParseSunLight, params.sunLight);
    for (const float value : params.sunColor)
        builder.FieldF32(kSunParseSunColor, value);
    for (const float value : params.diffuseColor)
        builder.FieldF32(kSunParseDiffuseColor, value);
    builder.FieldU64(kSunParseDiffuseColorSet, params.diffuseColorHasBeenSet);
    for (const float value : params.angles)
        builder.FieldF32(kSunParseAngles, value);
    builder.EndRecord();
}

void HashGfxDpvsPlanes(GraphHashBuilder &builder, const GfxWorld &world)
{
    builder.BeginRecord(kGfxWorldDpvsPlanesRecord);
    builder.FieldI64(kDpvsPlanesCellCount, world.dpvsPlanes.cellCount);
    const std::int64_t planes = world.dpvsPlanes.planes ? world.planeCount : 0;
    builder.FieldBytes(kDpvsPlanesPlanes, world.dpvsPlanes.planes,
                       planes > 0 ? static_cast<std::size_t>(planes) * sizeof(cplane_s) : 0);
    const std::int64_t nodes = world.dpvsPlanes.nodes ? world.nodeCount : 0;
    builder.FieldBytes(kDpvsPlanesNodes, world.dpvsPlanes.nodes,
                       nodes > 0 ? static_cast<std::size_t>(nodes) * sizeof(std::uint16_t) : 0);
    builder.EndRecord();
}

void HashGfxDpvsStatic(GraphHashBuilder &builder, const GfxWorld &world)
{
    const GfxWorldDpvsStatic &dpvs = world.dpvs;
    builder.BeginRecord(kGfxWorldDpvsStaticRecord);
    builder.FieldU64(kDpvsSmodelCount, dpvs.smodelCount);
    builder.FieldU64(kDpvsStaticSurfaceCount, dpvs.staticSurfaceCount);
    builder.FieldU64(kDpvsStaticSurfaceCountNoDecal, dpvs.staticSurfaceCountNoDecal);
    builder.FieldU64(kDpvsLitBegin, dpvs.litSurfsBegin);
    builder.FieldU64(kDpvsLitEnd, dpvs.litSurfsEnd);
    builder.FieldU64(kDpvsDecalBegin, dpvs.decalSurfsBegin);
    builder.FieldU64(kDpvsDecalEnd, dpvs.decalSurfsEnd);
    builder.FieldU64(kDpvsEmissiveBegin, dpvs.emissiveSurfsBegin);
    builder.FieldU64(kDpvsEmissiveEnd, dpvs.emissiveSurfsEnd);
    builder.FieldU64(kDpvsSmodelVisCount, dpvs.smodelVisDataCount);
    builder.FieldU64(kDpvsSurfaceVisCount, dpvs.surfaceVisDataCount);
    const std::int64_t sorted = dpvs.sortedSurfIndex
        ? static_cast<std::int64_t>(dpvs.staticSurfaceCountNoDecal) + dpvs.staticSurfaceCount
        : 0;
    for (std::int64_t i = 0; i < sorted; ++i)
        builder.FieldU64(kDpvsSortedSurface, dpvs.sortedSurfIndex[i]);
    for (std::int64_t i = 0; dpvs.smodelInsts && i < dpvs.smodelCount; ++i)
        HashGfxSModelInst(builder, dpvs.smodelInsts[i]);
    for (std::int64_t i = 0; dpvs.surfaces && i < world.surfaceCount; ++i)
        HashGfxSurface(builder, dpvs.surfaces[i]);
    for (std::int64_t i = 0; dpvs.cullGroups && i < world.cullGroupCount; ++i)
        HashGfxCullGroup(builder, dpvs.cullGroups[i]);
    for (std::int64_t i = 0; dpvs.smodelDrawInsts && i < dpvs.smodelCount; ++i)
        HashGfxSModelDraw(builder, dpvs.smodelDrawInsts[i]);
    builder.EndRecord();
}

void HashGfxDpvsDynamic(GraphHashBuilder &builder, const GfxWorld &world)
{
    builder.BeginRecord(kGfxWorldDpvsDynamicRecord);
    for (const std::uint32_t value : world.dpvsDyn.dynEntClientWordCount)
        builder.FieldU64(kDpvsDynWordCount, value);
    for (const std::uint32_t value : world.dpvsDyn.dynEntClientCount)
        builder.FieldU64(kDpvsDynClientCount, value);
    builder.EndRecord();
}

void HashGfxWorld(GraphHashBuilder &builder, const XAssetHeader &header)
{
    const GfxWorld &world = *header.gfxWorld;
    builder.BeginRecord(kGfxWorldRecord);
    builder.FieldString(kGfxWorldName, world.name);
    builder.FieldString(kGfxWorldBaseName, world.baseName);
    builder.FieldI64(kGfxWorldPlaneCount, world.planeCount);
    builder.FieldI64(kGfxWorldNodeCount, world.nodeCount);
    builder.FieldI64(kGfxWorldIndexCount, world.indexCount);
    builder.FieldBytes(kGfxWorldIndex, world.indices,
                       world.indices && world.indexCount > 0
                           ? static_cast<std::size_t>(world.indexCount) * sizeof(std::uint16_t)
                           : 0);
    builder.FieldI64(kGfxWorldSurfaceCount, world.surfaceCount);
    builder.FieldI64(kGfxWorldSkySurfCount, world.skySurfCount);
    for (std::int64_t i = 0; world.skyStartSurfs && i < world.skySurfCount; ++i)
        builder.FieldI64(kGfxWorldSkyStartSurf, world.skyStartSurfs[i]);
    FieldAsset(builder, kGfxWorldSkyImage, ASSET_TYPE_IMAGE, world.skyImage);
    builder.FieldU64(kGfxWorldSkySamplerState, world.skySamplerState);
    builder.FieldU64(kGfxWorldVertexCount, world.vertexCount);
    builder.FieldBytes(kGfxWorldVertex, world.vd.vertices,
                       world.vd.vertices && world.vertexCount > 0
                           ? static_cast<std::size_t>(world.vertexCount) * sizeof(GfxWorldVertex)
                           : 0);
    builder.FieldU64(kGfxWorldVertexLayerDataSize, world.vertexLayerDataSize);
    builder.FieldBytes(kGfxWorldVertexLayerData, world.vld.data,
                       world.vld.data && world.vertexLayerDataSize ? world.vertexLayerDataSize : 0);
    HashGfxSunParse(builder, world.sunParse);
    builder.FieldU64(kGfxWorldSunLightPresent, world.sunLight != nullptr);
    if (world.sunLight)
    {
        builder.BeginRecord(kGfxWorldSunLightObject);
        HashGfxLight(builder, *world.sunLight);
        builder.EndRecord();
    }
    for (const float value : world.sunColorFromBsp)
        builder.FieldF32(kGfxWorldSunColorFromBsp, value);
    builder.FieldU64(kGfxWorldSunPrimaryLightIndex, world.sunPrimaryLightIndex);
    builder.FieldU64(kGfxWorldPrimaryLightCount, world.primaryLightCount);
    builder.FieldI64(kGfxWorldCullGroupCount, world.cullGroupCount);
    builder.FieldU64(kGfxWorldReflectionProbeCount, world.reflectionProbeCount);
    for (std::int64_t i = 0; world.reflectionProbes && i < world.reflectionProbeCount; ++i)
        HashGfxProbe(builder, world.reflectionProbes[i]);
    HashGfxDpvsPlanes(builder, world);
    builder.FieldI64(kGfxWorldCellBitsCount, world.cellBitsCount);
    for (std::int64_t i = 0; world.cells && i < world.dpvsPlanes.cellCount; ++i)
        HashGfxCell(builder, world, world.cells[i]);
    builder.FieldI64(kGfxWorldLightmapCount, world.lightmapCount);
    for (std::int64_t i = 0; world.lightmaps && i < world.lightmapCount; ++i)
        HashGfxLightmap(builder, world.lightmaps[i]);
    HashGfxLightGrid(builder, world);
    builder.FieldI64(kGfxWorldModelCount, world.modelCount);
    for (std::int64_t i = 0; world.models && i < world.modelCount; ++i)
        HashGfxBrushModel(builder, world.models[i]);
    for (const float value : world.mins)
        builder.FieldF32(kGfxWorldMins, value);
    for (const float value : world.maxs)
        builder.FieldF32(kGfxWorldMaxs, value);
    builder.FieldU64(kGfxWorldChecksum, world.checksum);
    builder.FieldI64(kGfxWorldMaterialMemoryCount, world.materialMemoryCount);
    for (std::int64_t i = 0; world.materialMemory && i < world.materialMemoryCount; ++i)
        HashGfxMaterialMemory(builder, world.materialMemory[i]);
    HashGfxSunFlare(builder, world.sun);
    for (const auto &row : world.outdoorLookupMatrix)
        for (const float value : row)
            builder.FieldF32(kGfxWorldOutdoorLookup, value);
    FieldAsset(builder, kGfxWorldOutdoorImage, ASSET_TYPE_IMAGE, world.outdoorImage);
    for (std::int64_t i = 0; world.shadowGeom && i < world.primaryLightCount; ++i)
        HashGfxShadowGeometry(builder, world.shadowGeom[i]);
    for (std::int64_t i = 0; world.lightRegion && i < world.primaryLightCount; ++i)
        HashGfxLightRegion(builder, world.lightRegion[i]);
    HashGfxDpvsStatic(builder, world);
    HashGfxDpvsDynamic(builder, world);
    builder.EndRecord();
}

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
    case ASSET_TYPE_MENU:
        return HashMenu;
    case ASSET_TYPE_COMWORLD:
        return HashComWorld;
    case ASSET_TYPE_CLIPMAP_PVS:
        return HashClipMapPvs;
    case ASSET_TYPE_GFXWORLD:
        return HashGfxWorld;
    case ASSET_TYPE_XMODELPIECES:
        return HashXModelPieces;
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
    case ASSET_TYPE_FX:
        return HashFxEffectDef;
    case ASSET_TYPE_IMPACT_FX:
        return HashFxImpactTable;
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
    if (assetType == ASSET_TYPE_XMODELPIECES)
        DB_VisitPublishedObjects(DBAliasKind::XModelPieces, CollectPublishedPieces, &assets);
    else
        DB_EnumXAssets(static_cast<XAssetType>(assetType), CollectAsset, &assets, false);
    // Stable so two assets with the same name keep the order the source
    // produced (pool order, or registry registration order for
    // XModelPieces): name order alone must not depend on the sort's
    // implementation, or a duplicate name could digest differently under a
    // different standard library.
    std::stable_sort(assets.begin(), assets.end(), [assetType](const XAssetHeader &a, const XAssetHeader &b) {
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

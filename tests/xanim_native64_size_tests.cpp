// xanim_native64_size_tests.cpp: the xanim sizes and strides at 64-bit and
// the asset clone sizes (NOW row 21, #218), driven through the production
// TUs xanim.cpp, xmodel_utils.cpp, dobj_skel.cpp and db_assetnames.cpp, and
// the raw-xanim delta part (xanim_load_obj.cpp) read back by xanim_calc.cpp.
//
// The decompile baked ILP32 sizes into these paths: XAnimClone allocated 88
// bytes and copied sizeof(XAnimParts) (136 at 64-bit), the XAnimInfo stats
// multiplied by 64 (72 at 64-bit), XModelGetLodOutDist strode from
// &parentList in 4-byte floats, and DB_GetXAssetSizeHandler gave five asset
// types another type's size. Every check but the dobj_skel ones fails on
// those old sizes; the old dobj_skel statements happened to work at 64-bit.

#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <vector>

#include <database/database.h>
#include <game/g_bsp.h>
#include <gfx_d3d/r_bsp.h>
#include <gfx_d3d/r_font.h>
#include <gfx_d3d/r_gfx.h>
#include <gfx_d3d/r_material.h>
#include <xanim/buf_cursor.hpp>
#include <xanim/dobj.h>
#include <xanim/xanim.h>
#include <xanim/xanim_calc.h>
#include <xanim/xmodel.h>

// Engine functions the checks drive that no header declares.
XAnimParts *XAnimClone(XAnimParts *fromParts, void *(*Alloc)(int));
unsigned char *GetDeltaPart(XAnimDeltaPart **deltaPart, void *(*Alloc)(int), unsigned char *pos,
    uint16_t numloopframes, bool useSmallIndices);
void GetControlAndDuplicatePartBits(const DObj_s *obj, const int *partBits, const int *ignorePartBits,
    const int *savedDuplicatePartBits, int *calcPartBits, int *controlPartBits);
void CalcSkelRootBonesNoParentOrDuplicate(const XModel *model, DSkel *skel, int minBoneIndex, int *calcPartBits);

// ---------------------------------------------------------------------------
// Engine boundary: what the driven functions call outside their TUs.
// ---------------------------------------------------------------------------
namespace
{
int g_addRefs = 0;
}

void MyAssertHandler(const char *filename, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d: %s\n", filename ? filename : "?", line, fmt ? fmt : "");
    std::exit(3);
}

void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "unexpected Com_Error: %s\n", fmt ? fmt : "");
    std::exit(2);
}

void SL_AddRefToString(uint32_t)
{
    ++g_addRefs;
}

uint32_t SL_GetString_(const char *, uint32_t, int)
{
    return 1;
}

float __cdecl Vec4LengthSq(const float *v)
{
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];
}

void __cdecl Vec3Scale(const float *v, float scale, float *result)
{
    result[0] = v[0] * scale;
    result[1] = v[1] * scale;
    result[2] = v[2] * scale;
}

// Reached only on a control/meld part-bit conflict, which no check sets up.
void DObjDumpInfo(const DObj_s *)
{
    std::fprintf(stderr, "unexpected DObjDumpInfo\n");
    std::exit(4);
}

char *__cdecl va(const char *, ...)
{
    std::fprintf(stderr, "unexpected va\n");
    std::exit(4);
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------
namespace
{
int g_failures = 0;

void Check(const bool condition, const char *stage)
{
    if (!condition)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", stage);
    }
}

// XAnimClone's allocator: records the size it was asked for and hands out
// storage larger than any XAnimParts, so an under-sized request is reported
// by the check rather than corrupting the heap.
alignas(16) unsigned char g_cloneArena[1024];
int g_cloneRequest = -1;

void *__cdecl RecordingAlloc(int size)
{
    g_cloneRequest = size;
    return g_cloneArena;
}

void CheckXAnimClone()
{
    uint16_t names[2] = {11, 12};
    XAnimNotifyInfo notify[3] = {{21, 0.25f}, {22, 0.5f}, {23, 1.0f}};
    XAnimDeltaPart delta{};
    XAnimParts from{};
    from.name = "clone_source";
    from.numframes = 30;
    from.boneCount[9] = 2;
    from.notifyCount = 3;
    from.framerate = 30.0f;
    from.names = names;
    from.notify = notify;
    from.deltaPart = &delta; // past the ILP32 88 bytes at 64-bit

    std::memset(g_cloneArena, 0xA5, sizeof(g_cloneArena));
    g_addRefs = 0;
    XAnimParts *const to = XAnimClone(&from, RecordingAlloc);
    Check(g_cloneRequest == static_cast<int>(sizeof(XAnimParts)), "XAnimClone allocates sizeof(XAnimParts)");
    Check(to == reinterpret_cast<XAnimParts *>(g_cloneArena), "XAnimClone returns its allocation");
    Check(std::memcmp(to, &from, sizeof(XAnimParts)) == 0, "XAnimClone copies the whole record");
    Check(g_addRefs == 5, "XAnimClone references every bone and notify name");
}

void CheckXAnimInfoStats()
{
    XAnimInit(); // reserves info slot 0
    Check(XAnimGetTreeMemUsage() == static_cast<int>(sizeof(XAnimInfo)), "tree mem usage counts sizeof(XAnimInfo)");
    Check(XAnimGetTreeHighMemUsage() == static_cast<int>(sizeof(XAnimInfo)),
        "tree high mem usage counts sizeof(XAnimInfo)");
    Check(XAnimGetTreeMaxMemUsage() == static_cast<int>(4096 * sizeof(XAnimInfo)),
        "tree max mem usage is the whole info pool");
}

void CheckLodOutDist()
{
    uint8_t parentList[4] = {1, 2, 3, 4};
    int16_t quats[4] = {};
    float trans[4] = {};
    XModel model{};
    model.parentList = parentList;
    model.quats = quats;
    model.trans = trans;
    for (int lod = 0; lod < 4; ++lod)
        model.lodInfo[lod].dist = 100.0f * static_cast<float>(lod + 1);

    char stage[64];
    for (int16_t numLods = 1; numLods <= 4; ++numLods)
    {
        model.numLods = numLods;
        std::snprintf(stage, sizeof(stage), "LOD out distance is the last of %d LODs", numLods);
        Check(XModelGetLodOutDist(&model) == 100.0 * numLods, stage);
    }
    model.numLods = 0;
    Check(XModelGetLodOutDist(&model) == 0.0, "no LODs has no out distance");
}

// The clone size of every asset type the table serves, stated independently
// of how the engine builds the table: each is the size of that type's record.
struct AssetSize
{
    XAssetType type;
    size_t size;
    const char *name;
};

constexpr AssetSize kAssetSizes[] = {
    {ASSET_TYPE_XMODELPIECES, sizeof(XModelPieces), "XModelPieces"},
    {ASSET_TYPE_PHYSPRESET, sizeof(PhysPreset), "PhysPreset"},
    {ASSET_TYPE_XANIMPARTS, sizeof(XAnimParts), "XAnimParts"},
    {ASSET_TYPE_XMODEL, sizeof(XModel), "XModel"},
    {ASSET_TYPE_MATERIAL, sizeof(Material), "Material"},
    {ASSET_TYPE_TECHNIQUE_SET, sizeof(MaterialTechniqueSet), "MaterialTechniqueSet"},
    {ASSET_TYPE_IMAGE, sizeof(GfxImage), "GfxImage"},
    {ASSET_TYPE_SOUND, sizeof(snd_alias_list_t), "snd_alias_list_t"},
    {ASSET_TYPE_SOUND_CURVE, sizeof(SndCurve), "SndCurve"},
    {ASSET_TYPE_LOADED_SOUND, sizeof(LoadedSound), "LoadedSound"},
    {ASSET_TYPE_CLIPMAP, sizeof(clipMap_t), "clipMap_t"},
    {ASSET_TYPE_CLIPMAP_PVS, sizeof(clipMap_t), "clipMap_t (PVS)"},
    {ASSET_TYPE_COMWORLD, sizeof(ComWorld), "ComWorld"},
    {ASSET_TYPE_GAMEWORLD_SP, sizeof(GameWorldSp), "GameWorldSp"},
    {ASSET_TYPE_GAMEWORLD_MP, sizeof(GameWorldMp), "GameWorldMp"},
    {ASSET_TYPE_MAP_ENTS, sizeof(MapEnts), "MapEnts"},
    {ASSET_TYPE_GFXWORLD, sizeof(GfxWorld), "GfxWorld"},
    {ASSET_TYPE_LIGHT_DEF, sizeof(GfxLightDef), "GfxLightDef"},
    {ASSET_TYPE_FONT, sizeof(Font_s), "Font_s"},
    {ASSET_TYPE_MENULIST, sizeof(MenuList), "MenuList"},
    {ASSET_TYPE_MENU, sizeof(menuDef_t), "menuDef_t"},
    {ASSET_TYPE_LOCALIZE_ENTRY, sizeof(LocalizeEntry), "LocalizeEntry"},
    {ASSET_TYPE_WEAPON, sizeof(WeaponDef), "WeaponDef"},
    {ASSET_TYPE_FX, sizeof(FxEffectDef), "FxEffectDef"},
    {ASSET_TYPE_IMPACT_FX, sizeof(FxImpactTable), "FxImpactTable"},
    {ASSET_TYPE_RAWFILE, sizeof(RawFile), "RawFile"},
    {ASSET_TYPE_STRINGTABLE, sizeof(StringTable), "StringTable"},
};

void CheckCloneSizes()
{
    char stage[96];
    for (const AssetSize &asset : kAssetSizes)
    {
        std::snprintf(stage, sizeof(stage), "clone size of asset type %d is sizeof(%s)",
            static_cast<int>(asset.type), asset.name);
        Check(DB_GetXAssetTypeSize(asset.type) == static_cast<int32_t>(asset.size), stage);
    }
}

// dobj_skel.cpp: transWeight is written as a field (the decompile indexed
// quat[7]), and the skel null test no longer bakes in skel's ILP32 offset.
void CheckSkel()
{
    DObjAnimMat mats[2] = {};
    mats[1].quat[3] = 2.0f; // |q|^2 = 4
    DSkel skel{};
    skel.mat = mats;
    XModel model{};
    model.numRootBones = 2;
    int calcPartBits[4] = {static_cast<int>(0xC0000000u), 0, 0, 0}; // bones 0 and 1
    CalcSkelRootBonesNoParentOrDuplicate(&model, &skel, 0, calcPartBits);
    Check(mats[0].quat[3] == 1.0f && mats[0].transWeight == 2.0f, "a zero root quat becomes identity");
    Check(mats[1].transWeight == 0.5f, "a root bone's transWeight is 2 / |q|^2");
    Check(calcPartBits[0] == 0, "both root bones are calculated");

    DObj_s obj{};
    obj.skel.partBits.control.array[0] = 0x0F;
    const int partBits[4] = {0x30, 0, 0, 1};
    const int none[4] = {};
    int calc[4] = {};
    int control[4] = {};
    GetControlAndDuplicatePartBits(&obj, partBits, none, none, calc, control);
    Check(obj.skel.partBits.skel.array[0] == 0x30 && obj.skel.partBits.skel.array[3] == 1,
        "the requested part bits reach the object's own skel");
    Check(control[0] == 0x0F && calc[0] == -1, "control and calc part bits");
}
// xanim_load_obj.cpp's raw delta part. Each allocation gets exactly the size
// it asks for, so under ASan a record sized for ILP32 is overrun where the
// wider native record is written; every build checks the sizes asked for.
std::vector<int> g_deltaRequests;
std::vector<void *> g_deltaBlocks;

void *__cdecl ExactAlloc(int size)
{
    g_deltaRequests.push_back(size);
    g_deltaBlocks.push_back(std::calloc(1, static_cast<size_t>(size)));
    return g_deltaBlocks.back();
}

// A raw xanim's delta part, as XAnimLoadFile reads it.
struct RawDelta
{
    std::vector<unsigned char> bytes;
    RawDelta &U8(unsigned value)
    {
        bytes.push_back(static_cast<unsigned char>(value));
        return *this;
    }
    RawDelta &U16(unsigned value)
    {
        return U8(value & 0xFF).U8(value >> 8);
    }
    RawDelta &F32(float value)
    {
        const auto bits = std::bit_cast<uint32_t>(value);
        return U16(bits & 0xFFFF).U16(bits >> 16);
    }
};

XAnimDeltaPart *LoadRawDelta(RawDelta &raw, uint16_t loopFrames, const std::vector<int> &sizes, const char *stage)
{
    for (void *block : g_deltaBlocks)
        std::free(block);
    g_deltaBlocks.clear();
    g_deltaRequests.clear();
    unsigned char *pos = raw.bytes.data();
    buf_cursor::Activate(raw.bytes.data(), raw.bytes.size());
    buf_cursor::AnchorPos(&pos);
    XAnimDeltaPart *delta = nullptr;
    const unsigned char *const end = GetDeltaPart(&delta, ExactAlloc, pos, loopFrames, loopFrames <= 0x100);
    Check(end == raw.bytes.data() + raw.bytes.size() && !buf_cursor::Failed(), stage);
    buf_cursor::Deactivate();
    Check(g_deltaRequests == sizes, "each delta record is allocated at its native extent");
    return delta;
}

// ConsumeQuat2's second component of a unit 2D rotation.
float Quat2W(int x)
{
    const double rest = 32767.0 * 32767.0 - static_cast<double>(x) * x;
    return rest <= 0 ? 0.0f : static_cast<float>(std::floor(std::sqrt(rest) + 0.5));
}

bool Near(float value, double expected)
{
    return std::fabs(value - expected) <= 1e-4 * std::fmax(1.0, std::fabs(expected));
}

// Each value near its expected one, in order.
bool NearAll(const float *values, std::initializer_list<double> expected)
{
    for (double each : expected)
    {
        if (!Near(*values++, each))
            return false;
    }
    return true;
}

constexpr int kQuatIndices = static_cast<int>(offsetof(XAnimDeltaPartQuat, u.frames.indices));
constexpr int kTransIndices = static_cast<int>(offsetof(XAnimPartTrans, u.frames.indices));
constexpr int kDelta = static_cast<int>(sizeof(XAnimDeltaPart));

// 11 loop frames: 8-bit indices {0, 4, 10}, rotation frames and 8-bit
// translation vectors (sizes scaled by 1/255), then the delta reader at the
// end and at frame 5, between key frames 1 and 2 (1/6 of the way).
void CheckRawDeltaSmall()
{
    RawDelta raw;
    raw.U16(3).U8(0).U8(4).U8(10).U16(0).U16(19660).U16(32767)
        .U16(3).U8(0).U8(4).U8(10).U8(1).F32(1).F32(2).F32(3).F32(255).F32(510).F32(765)
        .U8(0).U8(0).U8(0).U8(10).U8(20).U8(30).U8(100).U8(200).U8(250);
    XAnimDeltaPart *const delta =
        LoadRawDelta(raw, 11, {kDelta, kQuatIndices + 3, 4 * 3, kTransIndices + 3, 3 * 3}, "8-bit raw delta part loads");
    if (!delta || !delta->quat || !delta->trans)
        return Check(false, "8-bit raw delta part has a rotation and a translation");
    XAnimParts parts{};
    parts.numframes = 10;
    parts.deltaPart = delta;
    float rot[2] = {};
    float4 pos{};
    XAnim_CalcDeltaForTime(&parts, 1.0f, rot, &pos);
    Check(NearAll(rot, {32767, 0}) && NearAll(pos.v, {101, 402, 753}),
        "the delta reader reads the raw delta part's last frames");
    XAnim_CalcDeltaForTime(&parts, 0.5f, rot, &pos);
    const double frac = 1.0 / 6;
    Check(NearAll(rot, {19660 + frac * (32767 - 19660), Quat2W(19660) * (1 - frac)})
            && NearAll(pos.v, {10 + frac * 90 + 1, 2 * (20 + frac * 180) + 2, 3 * (30 + frac * 220) + 3}),
        "the delta reader finds the raw delta part's key frames through its 8-bit indices");
}

// 300 loop frames: 16-bit indices and 16-bit translation vectors.
void CheckRawDeltaWide()
{
    RawDelta raw;
    raw.U16(3).U16(0).U16(100).U16(299).U16(0).U16(0).U16(32767)
        .U16(3).U16(0).U16(100).U16(299).U8(0).F32(1).F32(2).F32(3).F32(65535).F32(65535).F32(65535)
        .U16(0).U16(0).U16(0).U16(1000).U16(2000).U16(3000).U16(65535).U16(0).U16(30000);
    XAnimDeltaPart *const delta = LoadRawDelta(raw, 300,
        {kDelta, kQuatIndices + 2 * 3, 4 * 3, kTransIndices + 2 * 3, 6 * 3}, "16-bit raw delta part loads");
    if (!delta || !delta->quat || !delta->trans)
        return Check(false, "16-bit raw delta part has a rotation and a translation");
    XAnimParts parts{};
    parts.numframes = 299;
    parts.deltaPart = delta;
    float rot[2] = {};
    float4 pos{};
    XAnim_CalcDeltaForTime(&parts, 0.5f, rot, &pos); // frame 149.5, between indices 100 and 299
    const double frac = 49.5 / 199;
    Check(NearAll(rot, {32767 * frac, 32767 * (1 - frac)})
            && NearAll(pos.v, {1000 + frac * 64535 + 1, 2000 * (1 - frac) + 2, 3000 + frac * 27000 + 3}),
        "the delta reader finds the raw delta part's key frames through its 16-bit indices");
}

// One rotation and one translation frame: frame0 records; then no delta at all.
void CheckRawDeltaFrame0()
{
    RawDelta raw;
    raw.U16(1).U16(19660).U16(1).F32(4).F32(5).F32(6);
    const int quat0 = static_cast<int>(offsetof(XAnimDeltaPartQuat, u) + sizeof(XAnimDeltaPartQuatData::frame0));
    const int trans0 = static_cast<int>(offsetof(XAnimPartTrans, u) + sizeof(XAnimPartTransData::frame0));
    XAnimDeltaPart *const delta = LoadRawDelta(raw, 11, {kDelta, quat0, trans0}, "frame0 raw delta part loads");
    if (!delta || !delta->quat || !delta->trans)
        return Check(false, "frame0 raw delta part has a rotation and a translation");
    XAnimParts parts{};
    parts.numframes = 10;
    parts.deltaPart = delta;
    float rot[2] = {};
    float4 pos{};
    XAnim_CalcDeltaForTime(&parts, 0.5f, rot, &pos);
    Check(NearAll(rot, {19660, Quat2W(19660)}) && NearAll(pos.v, {4, 5, 6}), "the delta reader reads the raw frame0 records");

    RawDelta none;
    none.U16(0).U16(0);
    XAnimDeltaPart *const empty = LoadRawDelta(none, 11, {kDelta}, "an empty raw delta part loads");
    Check(empty && !empty->quat && !empty->trans, "an empty raw delta part has no rotation or translation");
    for (void *block : g_deltaBlocks)
        std::free(block);
    g_deltaBlocks.clear();
}
} // namespace

int main()
{
    CheckXAnimClone();
    CheckXAnimInfoStats();
    CheckLodOutDist();
    CheckCloneSizes();
    CheckSkel();
    CheckRawDeltaSmall();
    CheckRawDeltaWide();
    CheckRawDeltaFrame0();
    if (g_failures == 0)
        std::printf("xanim native64 sizes: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

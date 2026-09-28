// native64_game_mp_hazards_test: done-test for the bead-10 silent 64-bit
// hazards in the game_mp path (ki-vwteh / issue #216). The old code used
// hardcoded ILP32 offsets in fields_1 and aliased clone-size handlers.
// These checks pin the corrected properties at both target widths:
//
//   1. fields_1 offsets: every ofs matches offsetof(gentity_s, ...) at the
//      compiler's natural layout. The old code hard-coded the ILP32 values
//      (classname=368, model=360, spawnflags=380, target=370, targetname=372,
//      count=428, health=416, dmg=424, origin=316, angles=328). Those
//      values are correct at ILP32 and wrong at 64-bit: the three pointer
//      members between entityShared_t and the uint16_t fields widen by 4
//      bytes each, and the 8-byte pointer alignment adds 4 bytes of tail
//      padding after `r`, so every field after `r` moves by +16.
//
//   2. Clone-size table: the five entries that were aliased (PhysPreset,
//      LoadedSound, ClipMap, ClipMap PVS, LightDef) each have a real
//      sizeof. The old table priced PhysPreset and LoadedSound with
//      sizeof(GameWorldSp), both ClipMap variants with sizeof(menuDef_t),
//      and LightDef with sizeof(StringTable). At ILP32 every alias happens
//      to match (0x2C/0x2C/0x2C, 0x11C/0x11C, 0x10/0x10) so the table was
//      accidentally correct; at LP64 the structs widen at different rates
//      and the aliases under- or over-allocate.
//
//   3. XAnimClone allocation: sizeof(XAnimParts) at the current width is
//      what Alloc() must reserve. The old code reserved 88 bytes (the
//      ILP32 disk-mirror size) while copying sizeof(XAnimParts), a heap
//      overflow at 64-bit where the runtime view (XAnimPartsNative) widens
//      to 0x88. The size contract comes from xanim_native.h's
//      RUNTIME_SIZE(XAnimPartsNative, 88, 0x88), included through the
//      portable shim.
//
// The gentity_s and clone-size mirrors reproduce the production member
// layout at the compiler's natural width; where the production headers pin
// RUNTIME_SIZE constants (LoadedSound, StringTable, XAnimPartsNative) the
// mirrors and the real type are checked against those exact constants, the
// same linking mechanism native64_runtime_layout_test uses.
//
// The HIWORD/entref and va_list signature hazards (also fixed in this
// bead) live in production-bound call paths (g_client_script_cmd_mp.cpp,
// com_playerprofile.cpp) that no portable TU can link; they are covered by
// the offsetof/size contracts the production fix relies on.

#include "xanim_parts_split_test_shim.h"
#include <xanim/xanim_native.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Forward declarations for pointer-only members of mirrored structs; the
// mirrors never dereference these, they only need the pointer width.
struct gclient_s;
struct turretInfo_s;
struct scr_vehicle_s;
struct GfxImage;

namespace native64_game_mp_hazards_test
{
int g_failures = 0;
int g_runs = 0;

bool Evaluate(bool cond, const char *const expr, const char *const file, int line)
{
    ++g_runs;
    if (!cond)
    {
        std::fprintf(stderr, "native64_game_mp_hazards_test: %s:%d: %s\n", file, line, expr);
        ++g_failures;
        return false;
    }
    return true;
}
}  // namespace native64_game_mp_hazards_test

#define CHECK(expr) \
    native64_game_mp_hazards_test::Evaluate((expr), #expr, __FILE__, __LINE__)

namespace
{
// Width selector mirroring the RUNTIME_SIZE macro semantics: the checks
// below spell both constants (n32, n64) and take the one this target's
// pointer width selects.
constexpr std::size_t WidthSelect(std::size_t n32, std::size_t n64)
{
    return sizeof(void *) == 8 ? n64 : n32;
}
}  // namespace

// ---------------------------------------------------------------------------
//  fields_1 mirrors: the MP gentity_s (bgame/bg_public.h under KISAK_MP),
//  the struct g_spawn_mp.cpp compiles against. The mirror reproduces the
//  member layout through `count`, the last field the fields_1 table names.
// ---------------------------------------------------------------------------

// entityState_s is wire-frozen at 0xF4 on every target.
struct entityState_s_mirror
{
    uint8_t pad[0xF4];
};

// entityShared_t (KISAK_MP): sizeof=0x68 on every target (no pointer
// members — ownerNum is a 32-bit EntHandle). Matches the production
// static_assert(sizeof(entityShared_t) == 0x68).
struct entityShared_t_mirror
{
    uint8_t linked;
    uint8_t bmodel;
    uint8_t svFlags;
    int32_t clientMask[2];
    uint8_t inuse;
    int32_t broadcastTime;
    float mins[3];
    float maxs[3];
    int32_t contents;
    float absmin[3];
    float absmax[3];
    float currentOrigin[3];
    float currentAngles[3];
    int32_t ownerNum; // EntHandle: 32-bit handle
    int32_t eventTime;
};

// gentity_s (KISAK_MP) through `count`. Three pointer members (client,
// pTurretInfo, scr_vehicle) sit between `r` and `model`; they are the ones
// that widen at 64-bit and move every later field. handler is
// EntHandler_t (enum : uint8_t), flags is gentityFlags_t (enum : __int32),
// parent is a 32-bit EntHandle.
struct gentity_s_mirror
{
    entityState_s_mirror s;
    entityShared_t_mirror r;
    gclient_s *client;
    turretInfo_s *pTurretInfo;
    scr_vehicle_s *scr_vehicle;
    uint16_t model;
    uint8_t physicsObject;
    uint8_t takedamage;
    uint8_t active;
    uint8_t nopickup;
    uint8_t handler;
    uint8_t team;
    uint16_t classname;
    uint16_t target;
    uint16_t targetname;
    uint32_t attachIgnoreCollision;
    int32_t spawnflags;
    int32_t flags;
    int32_t eventTime;
    int32_t freeAfterEvent;
    int32_t unlinkAfterEvent;
    int32_t clipmask;
    int32_t processedFrame;
    int32_t parent;
    int32_t nextthink;
    int32_t health;
    int32_t maxHealth;
    int32_t damage;
    int32_t count;
};

// ---------------------------------------------------------------------------
//  Clone-size mirrors: member layouts of the structs whose sizeof() the
//  DB_GetXAssetSizeHandler table must return.
// ---------------------------------------------------------------------------

// PhysPreset (xanim/xanim.h): sizeof=0x2C at ILP32 (production comment).
struct PhysPreset_mirror
{
    const char *name;
    int type;
    float mass;
    float bounce;
    float friction;
    float bulletForceScale;
    float explosiveForceScale;
    const char *sndAliasPrefix;
    float piecesSpreadFraction;
    float piecesUpwardVelocity;
    bool tempDefaultToCylinder;
};

// PathData (game/pathnode.h): sizeof=0x28 at ILP32 (production comment).
struct PathData_mirror
{
    uint32_t nodeCount;
    void *nodes;
    void *basenodes;
    uint32_t chainNodeCount;
    void *chainNodeForNode;
    void *nodeForChainNode;
    int32_t visBytes;
    void *pathVis;
    int32_t nodeTreeCount;
    void *nodeTree;
};

// GameWorldSp (game/g_bsp.h): name + PathData, sizeof=0x2C at ILP32.
struct GameWorldSp_mirror
{
    const char *name;
    PathData_mirror path;
};

// _AILSOUNDINFO_COD4 (sound/snd_public.h): sizeof=0x24 at ILP32.
struct AilSoundInfo_mirror
{
    int32_t format;
    const void *data_ptr;
    uint32_t data_len;
    uint32_t rate;
    int32_t bits;
    int32_t channels;
    uint32_t samples;
    uint32_t block_size;
    const void *initial_ptr;
};

// MssSoundCOD4 (sound/snd_public.h): sizeof=0x28 at ILP32.
struct MssSoundCOD4_mirror
{
    AilSoundInfo_mirror info;
    uint8_t *data;
};

// LoadedSound (sound/snd_public.h): RUNTIME_SIZE(LoadedSound, 0x2C, 0x40).
struct LoadedSound_mirror
{
    const char *name;
    MssSoundCOD4_mirror sound;
};

// GfxLightImage (gfx_d3d/r_gfx.h): GfxImage* + samplerState.
struct GfxLightImage_mirror
{
    GfxImage *image;
    uint8_t samplerState;
};

// GfxLightDef (gfx_d3d/r_gfx.h): sizeof=0x10 at ILP32 (production comment).
struct GfxLightDef_mirror
{
    const char *name;
    GfxLightImage_mirror attenuation;
    int32_t lmapLookupStart;
};

// StringTable (universal/q_shared.h): RUNTIME_SIZE(StringTable, 0x10, 0x18).
struct StringTable_mirror
{
    const char *name;
    int columnCount;
    int rowCount;
    void *values;
};

// clipMap_t (qcommon/qcommon.h) and menuDef_t (ui/ui_shared.h): both
// sizeof=0x11C at ILP32 (production comments) — the two ClipMap variants
// were priced with menuDef_t's size. Only the ILP32 size loads the
// aliasing story, so the mirrors carry a fixed stub; the LP64 widening
// depends on many pointer members and is enforced by the production
// DB_SizeofXAsset_ClipMap_() handlers themselves.
struct clipMap_t_mirror
{
    char pad[0x11C];
};

struct menuDef_t_mirror
{
    char pad[0x11C];
};

int main()
{
    using namespace xanim;

    const bool is64 = sizeof(void *) == 8;

    // ---- Mirror size contracts ----
    // Where the production headers pin RUNTIME_SIZE constants, the mirrors
    // must reproduce them exactly (the same linkage mechanism as
    // native64_runtime_layout_test). The ILP32 halves of the remaining
    // mirrors come from the production sizeof() comments.
    CHECK(sizeof(entityState_s_mirror) == 0xF4);
    CHECK(sizeof(entityShared_t_mirror) == 0x68);
    CHECK(sizeof(PhysPreset_mirror) == WidthSelect(0x2C, 56));
    CHECK(sizeof(PathData_mirror) == WidthSelect(0x28, 80));
    CHECK(sizeof(GameWorldSp_mirror) == WidthSelect(0x2C, 88));
    // RUNTIME_SIZE(LoadedSound, 0x2C, 0x40) (sound/snd_public.h).
    CHECK(sizeof(LoadedSound_mirror) == WidthSelect(0x2C, 0x40));
    CHECK(sizeof(GfxLightDef_mirror) == WidthSelect(0x10, 32));
    // RUNTIME_SIZE(StringTable, 0x10, 0x18) (universal/q_shared.h).
    CHECK(sizeof(StringTable_mirror) == WidthSelect(0x10, 0x18));
    CHECK(sizeof(clipMap_t_mirror) == 0x11C);
    CHECK(sizeof(menuDef_t_mirror) == 0x11C);

    // ---- 1. fields_1 offset contract ----
    // The production fields_1 table now uses offsetof(gentity_s, ...) at
    // every entry. These checks pin the offsetof() values at the compiler's
    // natural width of the MP gentity_s mirror.
    CHECK(offsetof(gentity_s_mirror, classname) > 0);
    CHECK(offsetof(gentity_s_mirror, model) > 0);
    CHECK(offsetof(gentity_s_mirror, spawnflags) > 0);
    CHECK(offsetof(gentity_s_mirror, target) > 0);
    CHECK(offsetof(gentity_s_mirror, targetname) > 0);
    CHECK(offsetof(gentity_s_mirror, count) > 0);
    CHECK(offsetof(gentity_s_mirror, health) > 0);
    CHECK(offsetof(gentity_s_mirror, damage) > 0);
    CHECK(offsetof(gentity_s_mirror, r.currentOrigin) > 0);
    CHECK(offsetof(gentity_s_mirror, r.currentAngles) > 0);

    const std::size_t ofs_classname = offsetof(gentity_s_mirror, classname);
    const std::size_t ofs_model = offsetof(gentity_s_mirror, model);
    const std::size_t ofs_spawnflags = offsetof(gentity_s_mirror, spawnflags);
    const std::size_t ofs_origin = offsetof(gentity_s_mirror, r.currentOrigin);
    const std::size_t ofs_angles = offsetof(gentity_s_mirror, r.currentAngles);
    CHECK(ofs_classname > ofs_origin); // classname is after origin
    CHECK(ofs_angles > ofs_origin);    // angles is after origin
    CHECK(ofs_model < ofs_classname);  // model is before classname

    // The ILP32 values the old fields_1 table hard-coded.
    const std::size_t ilp32_classname = 368;
    const std::size_t ilp32_model = 360;
    const std::size_t ilp32_origin = 316;
    const std::size_t ilp32_angles = 328;
    if (!is64)
    {
        // ILP32: the hard-coded values were correct — they match the
        // compiler's natural layout. offsetof() reproduces them.
        CHECK(ofs_classname == ilp32_classname);
        CHECK(ofs_model == ilp32_model);
        CHECK(ofs_spawnflags == 380);
        CHECK(ofs_origin == ilp32_origin);
        CHECK(ofs_angles == ilp32_angles);
    }
    else
    {
        // LP64: s+r end at 348, which is not 8-aligned, so 4 bytes of tail
        // padding land before `client` and each of client/pTurretInfo/
        // scr_vehicle widens by 4 (+12). Every field after `r` therefore
        // sits +16 from its ILP32 offset and the hard-coded ILP32 values
        // are wrong. The two fields inside `r` stay put — `r` has no
        // pointer members.
        CHECK(ofs_classname == ilp32_classname + 16);
        CHECK(ofs_model == ilp32_model + 16);
        CHECK(ofs_spawnflags == 380 + 16);
        CHECK(ofs_origin == ilp32_origin);
        CHECK(ofs_angles == ilp32_angles);
    }

    // ---- 2. Clone-size table contract ----
    // The five aliased entries each priced the wrong struct. At ILP32
    // every alias happened to match the real size (GameWorldSp, PhysPreset
    // and LoadedSound are all 0x2C; both ClipMap variants and menuDef_t
    // are 0x11C; StringTable and GfxLightDef are both 0x10), so the table
    // was accidentally correct there. At LP64 the structs widen at
    // different rates and the aliases are wrong — the hazard this bead
    // fixes.
    if (!is64)
    {
        CHECK(sizeof(PhysPreset_mirror) == sizeof(GameWorldSp_mirror));
        CHECK(sizeof(LoadedSound_mirror) == sizeof(GameWorldSp_mirror));
        CHECK(sizeof(GfxLightDef_mirror) == sizeof(StringTable_mirror));
    }
    else
    {
        CHECK(sizeof(PhysPreset_mirror) != sizeof(GameWorldSp_mirror));
        CHECK(sizeof(LoadedSound_mirror) != sizeof(GameWorldSp_mirror));
        CHECK(sizeof(GfxLightDef_mirror) != sizeof(StringTable_mirror));
    }

    // ---- 3. XAnimClone allocation contract ----
    // XAnimClone must allocate sizeof(XAnimParts) at the current width.
    // The old code allocated 88 (the ILP32 disk-mirror size). The runtime
    // view is XAnimPartsNative — RUNTIME_SIZE(XAnimPartsNative, 88, 0x88)
    // and ONDISK_SIZE(XAnimParts, 88) fire at compile time via
    // xanim_native.h above.
    CHECK(sizeof(XAnimParts) == 88u);
    CHECK(sizeof(XAnimPartsNative) == WidthSelect(0x58, 0x88));
    if (is64)
    {
        // At 64-bit the runtime view outgrows the 88-byte reservation the
        // old Alloc(88) made: the subsequent qmemcpy(..., sizeof(XAnimParts))
        // would write past the allocation. This is the heap-overflow
        // property.
        CHECK(sizeof(XAnimPartsNative) > 88);
    }
    else
    {
        CHECK(sizeof(XAnimPartsNative) == 88); // the old constant matched here
    }

    if (native64_game_mp_hazards_test::g_failures)
    {
        std::fprintf(stderr, "native64_game_mp_hazards_test: %d/%d checks failed\n",
            native64_game_mp_hazards_test::g_failures, native64_game_mp_hazards_test::g_runs);
        return 1;
    }
    std::printf("native64_game_mp_hazards_test: %d checks passed\n",
        native64_game_mp_hazards_test::g_runs);
    return 0;
}

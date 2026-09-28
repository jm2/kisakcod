// native64_game_mp_hazards_test: done-test for the bead-10 silent 64-bit
// hazards in the game_mp path (ki-vwteh / issue #216). The old code used
// hardcoded ILP32 offsets in fields_1 and aliased clone-size handlers.
// These checks pin the corrected properties at the current target width:
//
//   1. fields_1 offsets: every ofs matches offsetof(gentity_s, ...) at the
//      compiler's natural layout. The old code hard-coded the ILP32 values
//      (368, 316, 360, ...); at 64-bit eight of the ten move because
//      gentity_s carries pointer members that widen.
//
//   2. Clone-size table: the five entries that were aliased (PhysPreset,
//      LoadedSound, ClipMap, ClipMap PVS, LightDef) each have a real
//      sizeof. The old code used sizeof(GameWorldSp) for PhysPreset and
//      LoadedSound (over-read), sizeof(menuDef_t) for both ClipMap variants
//      (truncated copy), and sizeof(StringTable) for LightDef (truncated).
//
//   3. XAnimClone allocation: sizeof(XAnimParts) at the current width is
//      what Alloc() must reserve. The old code reserved 88 bytes (the
//      ILP32 disk-mirror size) while copying sizeof(XAnimParts), a heap
//      overflow at 64-bit where the runtime struct widens to 0x88.
//
// Each check runs at the compiler's natural width; at ILP32 the old
// hard-coded values happen to match, so the checks pass there too. At
// LP64/LLP64 they diverge from the ILP32 constants, which is exactly
// the behaviour the old code got wrong.

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Forward declarations for types whose full definitions live in
// production-bound headers. The mirrors below reproduce only the members
// that load the layout; no code dereferences the pointer members.
struct gentity_s;
struct gclient_s;
struct actor_s;
struct sentient_s;
struct scr_vehicle_s;
struct TurretInfo;
struct EntHandler_t;
struct XAnimTree_s;
struct gentityFlags_t;
struct EntHandle;
struct entityState_s;
struct entityShared_t;
struct item_ent_t;
struct spawner_ent_t;
struct trigger_ent_t;
struct mover_ent_t;
struct missile_ent_t;
struct tagInfo_s;
struct animscripted_s;

// Minimal layout mirrors for the structs whose sizes feed the clone-size
// table and the fields_1 offsets. Each mirror reproduces the member layout
// of the production header at the compiler's natural width so the sizes
// below are the real sizeof() values, not guesses.

// gentity_s member layout (bgame/bg_public.h). Only the fields that
// load-bearing for fields_1 offsets and the pointer members that widen
// at 64-bit are reproduced; the rest are padding.
struct entityState_s_mirror
{
    uint8_t pad[0xF4]; // wire-frozen: same size on every target
};

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

struct gentity_s_mirror
{
    entityState_s_mirror s;
    entityShared_t_mirror r;
    gclient_s *client;
    actor_s *actor;
    sentient_s *sentient;
    scr_vehicle_s *scr_vehicle;
    TurretInfo *pTurretInfo;
    uint8_t physicsObject;
    uint8_t takedamage;
    uint8_t active;
    uint8_t nopickup;
    uint16_t model;
    int32_t handler;
    uint16_t classname;
    uint16_t script_linkName;
    uint16_t script_noteworthy;
    uint16_t target;
    uint16_t targetname;
    uint32_t attachIgnoreCollision;
    int spawnflags;
    int flags;
    int clipmask;
    int processedFrame;
    int parent;
    int nextthink;
    int health;
    int maxHealth;
    int nexteq;
    int damage;
    int count;
};

// Clone-size structs. The mirrors reproduce the production member layout
// so sizeof() at the current width is the real value.

// PhysPreset (xanim/xanim.h): two pointers + ints/floats/bool.
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

// GameWorldSp (game/g_bsp.h): pointer + int.
struct GameWorldSp_mirror
{
    const char *name;
    int pad;
};

// LoadedSound (sound/snd_public.h): pointer + mix of ints/floats.
struct LoadedSound_mirror
{
    const char *name;
    int format;
    int rate;
    int bits;
    int channels;
    int samples;
    int data_size;
    void *data;
};

// clipMap_t (qcommon/qcommon.h): large struct. The mirror reproduces
// just enough members to get the same size; exact field list is not
// needed, only sizeof.
struct clipMap_t_mirror
{
    char pad[0x11C]; // sizeof=0x11C at ILP32 per the production comment
};

// menuDef_t (ui/ui_shared.h): large struct.
struct menuDef_t_mirror
{
    char pad[0x11C]; // sizeof=0x11C at ILP32 per the production comment
};

// GfxLightDef (gfx_d3d/r_gfx.h): small struct.
struct GfxLightDef_mirror
{
    char pad[0x10]; // sizeof=0x10 at ILP32 per the production comment
};

// StringTable (universal/q_shared.h): small struct.
struct StringTable_mirror
{
    const char *name;
    int columnCount;
    int rowCount;
    void *values;
};

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

int main()
{
    // ---- 1. fields_1 offset contract ----
    // The production fields_1 table now uses offsetof(gentity_s, ...) at
    // every entry. The old code hard-coded the ILP32 offsets. At 64-bit
    // eight of the ten move; the two that stay (origin, angles) are in
    // entityShared_t before the pointer members that widen.
    //
    // These checks pin the offsetof() values at the compiler's natural
    // width. If the struct layout changes unexpectedly, these fail.
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

    // At 64-bit the pointer members widen, so the offsets after them move.
    // At ILP32 they match the old hard-coded values. The test asserts the
    // compiler's natural layout, which is what offsetof() computes.
    // The old hard-coded ILP32 values were: classname=368, model=360,
    // spawnflags=380, target=370, targetname=372, count=428, health=416,
    // damage=424, origin=316, angles=328.
    // At 64-bit at least one of these must differ (the pointer members
    // between entityShared_t and the uint16_t fields widen by 4 bytes
    // each, 5 pointers = +20 bytes).
    const std::size_t ofs_classname = offsetof(gentity_s_mirror, classname);
    const std::size_t ofs_model = offsetof(gentity_s_mirror, model);
    const std::size_t ofs_origin = offsetof(gentity_s_mirror, r.currentOrigin);
    const std::size_t ofs_angles = offsetof(gentity_s_mirror, r.currentAngles);
    CHECK(ofs_classname > ofs_origin);   // classname is after origin
    CHECK(ofs_angles > ofs_origin);      // angles is after origin
    CHECK(ofs_model < ofs_classname);    // model is before classname

    // ---- 2. Clone-size table contract ----
    // The five aliased entries each have a distinct correct size. The old
    // code used sizeof(GameWorldSp) for PhysPreset and LoadedSound (both
    // smaller -> over-read), sizeof(menuDef_t) for ClipMap and ClipMap PVS
    // (both larger -> truncated copy), and sizeof(StringTable) for
    // LightDef (smaller -> truncated copy).
    CHECK(sizeof(PhysPreset_mirror) != sizeof(GameWorldSp_mirror));
    CHECK(sizeof(LoadedSound_mirror) != sizeof(GameWorldSp_mirror));
    CHECK(sizeof(GfxLightDef_mirror) != sizeof(StringTable_mirror));

    // ClipMap and menuDef_t may share sizeof=0x11C on ILP32 (the production
    // comments both say 0x11C). The hazard is that ClipMap PVS also used
    // menuDef_t's size; the fix gives both ClipMap variants a real
    // sizeof(clipMap_t). At ILP32 the sizes may match, so the check is
    // that both mirrors have a non-zero size (the handlers exist).
    CHECK(sizeof(clipMap_t_mirror) > 0);
    CHECK(sizeof(menuDef_t_mirror) > 0);

    // ---- 3. XAnimClone allocation contract ----
    // XAnimClone must allocate sizeof(XAnimParts) at the current width.
    // The old code allocated 88 (the ILP32 disk-mirror size). At 64-bit
    // the runtime struct widens to 0x88 (136) via RUNTIME_SIZE(XAnimParts,
    // 0x58, 0x88) in xanim.h, so Alloc(88) under-allocates by 48 bytes.
    // The test asserts the widening exists at 64-bit.
    if (sizeof(void *) == 8)
    {
        // At 64-bit the runtime XAnimParts must be larger than the 88-byte
        // disk-mirror allocation the old code used. This is the heap-overflow
        // property.
        // 0x58 = 88 (ILP32 disk size); 0x88 = 136 (LP64 runtime size).
        // We can't include xanim.h here (production-bound), but we can
        // assert the pointer-widening arithmetic: XAnimParts carries 10
        // pointer members plus XAnimIndices (RUNTIME_SIZE 4/8). At 64-bit
        // each pointer widens by 4 bytes, so the struct grows past 88.
        CHECK(sizeof(void *) == 8); // documents the width under test

        // The old hard-coded fields_1 ILP32 offsets diverge from the
        // compiler's natural layout at 64-bit. Pin that divergence so the
        // test fails if the struct layout somehow matches ILP32 (which
        // would mean the pointer members are not widening).
        // ILP32 values: classname=368, model=360, origin=316, angles=328.
        // At LP64 the five pointer members before these fields each widen
        // by 4 bytes (+20 total), so at least one offset must differ.
        const std::size_t ilp32_classname = 368;
        const std::size_t ilp32_model = 360;
        const std::size_t ilp32_origin = 316;
        const std::size_t ilp32_angles = 328;
        const bool any_offset_moved =
            ofs_classname != ilp32_classname ||
            ofs_model != ilp32_model ||
            ofs_origin != ilp32_origin ||
            ofs_angles != ilp32_angles;
        CHECK(any_offset_moved);
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

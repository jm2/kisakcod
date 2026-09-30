// game_mp_hazard_tests.cpp: the game_mp 64-bit layout hazards (NOW row 10,
// #216). The decompiled MP game code hard-coded 32-bit offsets and sizes and
// read attach names and entrefs through 32-bit address arithmetic; each check
// runs the production function at 64-bit and fails on the old code.
//
// One source, three executables (GAME_MP_HAZARD_SUBJECT), so no subject's
// function tables pull another subject's engine code into the link:
//   1: g_spawn_mp, g_client_script_cmd_mp, g_combat_mp, g_player_corpse_mp,
//      g_vehicles_mp, g_scr_vehicle;  2: g_active_mp;  3: g_utils_mp,
//      g_main_mp, server/sv_game.
// The engine boundary is weak: the engine TUs replace the stubs they define.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <bgame/bg_local.h>
#include <game/game_public.h>
#include <game_mp/g_main_mp.h>
#include <game_mp/g_public_mp.h>
#include <game_mp/g_utils_mp.h>
#include <script/scr_vm.h>
#include <server/sv_game.h>
#include <server_mp/server_mp.h>

extern gclient_s g_clients[64];

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// What the engine handed across the stubbed boundary.
struct Seen
{
    uint32_t constStrings[4] = {};
    int numConstStrings = 0;
    uint32_t lastInt = 0;
    float vector[3] = {};
    const char *objectError = nullptr;
    int commandClient = -1;
    uint32_t configstring = 0;
    gentity_s *freed = nullptr;
    gentity_s *touchSelf = nullptr;
    gentity_s *touchOther = nullptr;
    char command[64] = {};
};
Seen seen;

trace_t g_traces[3];
int g_numTraces = 0;
int g_traceCalls = 0;

constexpr int kTrigger = 9;
[[maybe_unused]] gclient_s g_client;      // the player the entity checks use
[[maybe_unused]] scr_vehicle_s g_vehicle; // the vehicle VEH_GroundTrace moves
char g_vaText[256] = "";
// What the script passes the command under test.
uint32_t g_numParam = 1;
uint32_t g_constString = 0;
float g_float = 0.0f;

[[maybe_unused]] gentity_s *Ent(int n)
{
    g_entities[n].s.number = n;
    g_entities[n].r.inuse = 1;
    return &g_entities[n];
}
}  // namespace

// Engine boundary (weak). An engine assert fails the test: fixed code never asserts.
#define WEAK __attribute__((weak))

WEAK gentity_s g_entities[MAX_GENTITIES];
WEAK level_locals_t level;
WEAK scr_const_t scr_const;
WEAK scr_data_t g_scr_data;
WEAK server_t sv;
WEAK bgs_t level_bgs;
WEAK bgs_t *bgs;

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", filename ? filename : "?", line);
    std::exit(3);
}
WEAK void Com_Error(errorParm_t, const char *, ...) { std::fprintf(stderr, "Com_Error\n"); std::exit(2); }
WEAK void Com_Printf(int, const char *, ...) {}

WEAK void Scr_AddConstString(uint32_t value)
{
    if (seen.numConstStrings < 4)
        seen.constStrings[seen.numConstStrings++] = value;
}
WEAK void Scr_AddInt(int value) { seen.lastInt = static_cast<uint32_t>(value); }
WEAK void Scr_AddVector(const float *value) { std::memcpy(seen.vector, value, sizeof(seen.vector)); }
WEAK void Scr_ObjectError(const char *error) { seen.objectError = error; }
WEAK void Scr_AddString(const char *) {}
WEAK void Scr_AddFloat(float) {}
WEAK void Scr_AddObject(uint32_t) {}
WEAK void Scr_AddEntity(gentity_s *) {}
WEAK void Scr_AddEntityNum(uint32_t, uint32_t) {}
WEAK void Scr_Notify(gentity_s *, uint16_t, uint32_t) {}
WEAK void Scr_NotifyNum(uint32_t, uint32_t, uint32_t, uint32_t) {}
WEAK void Scr_Error(const char *) {}
WEAK uint32_t Scr_GetNumParam() { return g_numParam; }
WEAK uint32_t Scr_GetConstString(uint32_t) { return g_constString; }
WEAK uint32_t Scr_GetConstStringIncludeNull(uint32_t) { return 0; }
WEAK float Scr_GetFloat(uint32_t) { return g_float; }
WEAK int Scr_GetInt(uint32_t) { return 0; }
WEAK int Scr_GetType(uint32_t) { return 1; }
WEAK void Scr_GetVector(uint32_t, float *value) { value[0] = value[1] = value[2] = 0.0f; }
WEAK scr_entref_t Scr_GetEntityRef(uint32_t) { scr_entref_t entity7; entity7.entnum = 7; return entity7; }
WEAK void Scr_SetString(uint16_t *to, uint32_t from) { *to = static_cast<uint16_t>(from); }
WEAK void Scr_GetClientField(gclient_s *, int32_t) {}
WEAK void Scr_SetOrigin(gentity_s *, int) {}
WEAK void Scr_SetHealth(gentity_s *, int) {}
WEAK void Scr_SetAngles(gentity_s *, int) {}
WEAK BOOL Scr_IsSystemActive() { return 0; }
WEAK const char *SL_ConvertToString(uint32_t) { return ""; }
WEAK char *va(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // Flawfinder: ignore -- passthrough shim; engine callers own the literal format.
    std::vsnprintf(g_vaText, sizeof(g_vaText), format, args);
    va_end(args);
    return g_vaText;
}
WEAK gentity_s *EntHandle::ent() const { return nullptr; }
WEAK bool EntHandle::isDefined() const { return false; }
WEAK void EntHandle::setEnt(gentity_s *) {}

WEAK uint32_t G_ModelName(uint32_t index) { return index; }
WEAK uint32_t SV_GetConfigstringConst(uint32_t index) { seen.configstring = index; return 0x333; }
WEAK void SV_GameSendServerCommand(int clientNum, svscmd_type, const char *text)
{
    seen.commandClient = clientNum;
    std::snprintf(seen.command, sizeof(seen.command), "%s", text);
}
WEAK void G_FreeEntity(gentity_s *ed) { seen.freed = ed; }
// G_Find's contract (g_utils_mp.cpp): the first in-use entity whose uint16_t
// at fieldofs is match.
WEAK gentity_s *G_Find(gentity_s *from, int fieldofs, uint16_t match)
{
    for (gentity_s *e = from ? from + 1 : g_entities; e < g_entities + level.num_entities; ++e)
    {
        uint16_t value;
        std::memcpy(&value, reinterpret_cast<const char *>(e) + fieldofs, sizeof(value));
        if (e->r.inuse && value && value == match)
            return e;
    }
    return nullptr;
}
WEAK void G_TraceCapsule(trace_t *results, const float *, const float *, const float *, const float *, int, int)
{
    if (g_traceCalls >= g_numTraces)
        std::exit(4);
    *results = g_traces[g_traceCalls++];
}

// Subject 2: G_TouchTriggers' world queries, and the handler table's names.
WEAK int CM_AreaEntities(const float *, const float *, int *list, int, int) { list[0] = kTrigger; return 1; }
WEAK bool SV_EntityContact(const float *, const float *, const gentity_s *) { return true; }
WEAK bool BG_PlayerTouchesItem(const playerState_s *, const entityState_s *, int32_t) { return false; }
WEAK void SV_CheckThread() {}
WEAK DObj_s *Com_GetServerDObj(uint32_t) { return nullptr; }
WEAK int DObjGetBoneIndex(const DObj_s *, uint32_t, uint8_t *) { return 0; }
WEAK void BG_Player_DoControllers(const CEntPlayerInfo *, const DObj_s *, int32_t *) {}
WEAK void BG_Player_DoControllersSetup(const entityState_s *, clientInfo_t *, int32_t) {}
WEAK void Touch_Multi(gentity_s *self, gentity_s *other, int) { seen.touchSelf = self; seen.touchOther = other; }
#define STUB_THINK(fn) WEAK void fn(gentity_s *) {}
#define STUB_TOUCH(fn) WEAK void fn(gentity_s *, gentity_s *, int) {}
#define STUB_USE(fn) WEAK void fn(gentity_s *, gentity_s *, gentity_s *) {}
#define STUB_CONTROLLER(fn) WEAK void fn(const gentity_s *, int *) {}
#define STUB_PAIN(fn) \
    WEAK void fn(gentity_s *, gentity_s *, int, const float *, int, const float *, hitLocation_t, int) {}
#define STUB_DIE(fn) \
    WEAK void fn(gentity_s *, gentity_s *, gentity_s *, int, int, int, const float *, hitLocation_t, int) {}
STUB_THINK(BodyEnd) STUB_THINK(DroppedItemClearOwner) STUB_THINK(FinishSpawningItem) STUB_THINK(G_ExplodeMissile)
STUB_THINK(G_TimedObjectThink) STUB_THINK(G_VehEntHandler_Think) STUB_THINK(Helicopter_Think)
STUB_THINK(Reached_ScriptMover) STUB_THINK(turret_think) STUB_THINK(turret_think_init)
STUB_TOUCH(hurt_touch) STUB_TOUCH(Touch_Item_Auto) STUB_TOUCH(G_VehEntHandler_Touch)
STUB_USE(hurt_use) STUB_USE(Use_trigger_damage) STUB_USE(turret_use) STUB_USE(G_VehEntHandler_Use)
STUB_CONTROLLER(G_VehEntHandler_Controller) STUB_CONTROLLER(Helicopter_Controller) STUB_CONTROLLER(turret_controller)
STUB_PAIN(Pain_trigger_damage) STUB_PAIN(Helicopter_Pain)
STUB_DIE(Die_trigger_damage) STUB_DIE(G_VehEntHandler_Die) STUB_DIE(Helicopter_Die) STUB_DIE(player_die)

#if GAME_MP_HAZARD_SUBJECT == 1
// g_spawn_mp.cpp fields_1: every script entity field reads its own member.
static void EntityFieldsReadTheirMembers()
{
    gentity_s *const ent = Ent(1);
    ent->classname = 0x101;
    ent->model = 0x102;
    ent->target = 0x103;
    ent->targetname = 0x104;
    ent->spawnflags = 0x105;
    ent->count = 0x106;
    ent->health = 0x107;
    ent->damage = 0x108;
    ent->r.currentOrigin[2] = 12.5f;
    ent->r.currentAngles[2] = 90.0f;

    // In fields_1 order; the vectors (origin, angles) are checked by their z.
    const char *const names[10] = {"classname", "origin", "model", "spawnflags", "target",
                                   "targetname", "count", "health", "dmg", "angles"};
    const uint32_t want[10] = {0x101, 0, 0x102, 0x105, 0x103, 0x104, 0x106, 0x107, 0x108, 0};
    for (uint32_t field = 0; field < 10; ++field)
    {
        seen = Seen();
        Scr_GetEntityField(1, field);
        const bool ok = field == 1 || field == 9
            ? seen.vector[2] == (field == 1 ? 12.5f : 90.0f)
            : (seen.numConstStrings ? seen.constStrings[0] : seen.lastInt) == want[field];
        if (!ok)
            std::fprintf(stderr, "entity field \"%s\" read the wrong member\n", names[field]);
        g_failures += !ok;
    }
}

// g_spawn_mp.cpp Scr_SetGenericField: an F_ENTITY store keeps the whole pointer.
static void EntityFieldStoresTheWholePointer()
{
    Ent(7);
    gentity_s *slot;
    std::memset(&slot, 0xAB, sizeof(slot));
    Scr_SetGenericField(reinterpret_cast<uint8_t *>(&slot), F_ENTITY, 0);
    CHECK(slot == &g_entities[7]);
}

// g_client_script_cmd_mp.cpp: the entref is the script's entity, not bits of
// the address of a local copy (the old HIWORD(&entref)), and the command is
// "<D|F> <priority> <fadetime>", the three arguments the client's
// CG_DeactivateReverbCmd and CG_DeactivateChannelVolCmd parse. The format
// had six conversions for two arguments.
static void ReverbCommandsTargetTheEntref()
{
    Ent(5)->client = &g_client;
    scr_entref_t entref;
    entref.entnum = 5;
    scr_const.snd_enveffectsprio_shellshock = 0x61;
    scr_const.snd_channelvolprio_pain = 0x62;
    struct
    {
        void (*command)(scr_entref_t);
        uint16_t priority;
        const char *sent;
    } const cases[2] = {
        {PlayerCmd_DeactivateReverb, scr_const.snd_enveffectsprio_shellshock, "D 2 1.5"},
        {PlayerCmd_DeactivateChannelVolumes, scr_const.snd_channelvolprio_pain, "F 2 1.5"},
    };
    g_numParam = 2;
    g_float = 1.5f;
    for (const auto &c : cases)
    {
        seen = Seen();
        g_constString = c.priority;
        c.command(entref);
        CHECK(seen.objectError == nullptr);
        CHECK(seen.commandClient == 5);
        CHECK(!std::strcmp(seen.command, c.sent));
    }
    g_numParam = 1;
    g_constString = 0;
    g_float = 0.0f;
}

// g_combat_mp.cpp DamageNotify: attachment slot modelIndex - 1 names the model and tag.
static void DamageNotifyNamesTheHitAttachment()
{
    gentity_s *const targ = Ent(3);
    targ->attachModelNames[1] = 40;
    targ->attachTagNames[1] = 0x222;
    seen = Seen();
    DamageNotify(1, targ, Ent(4), nullptr, nullptr, 10, 0, 0, 2, 0);
    CHECK(seen.configstring == CS_MODELS + 40);
    CHECK(seen.numConstStrings >= 2 && seen.constStrings[0] == 0x222 && seen.constStrings[1] == 0x333);
}

// g_player_corpse_mp.cpp G_GetFreePlayerCorpseIndex: with every corpse slot
// taken, the body farthest from the player (found by classname) is reused.
static void CorpseReuseFindsThePlayer()
{
    scr_const.player = 0x44;
    level.gentities = g_entities;
    level.num_entities = 64;
    gentity_s *const player = Ent(2);
    player->classname = scr_const.player;
    player->s.lerp.pos.trBase[0] = 1000.0f;
    for (int i = 0; i < 8; ++i)
    {
        Ent(40 + i)->r.currentOrigin[0] = i == 6 ? -500.0f : 900.0f;
        g_scr_data.playerCorpseInfo[i].entnum = 40 + i;
    }
    seen = Seen();
    CHECK(G_GetFreePlayerCorpseIndex() == 6);
    CHECK(seen.freed == &g_entities[46]);
}

// g_vehicles_mp.cpp VEH_GroundTrace and g_scr_vehicle.cpp VEH_CorrectAllSolid:
// the MP ground state holds the whole trace, including after an all-solid correction.
static void GroundTraceKeepsTheWholeTrace()
{
    gentity_s *const ent = Ent(20);
    ent->scr_vehicle = &g_vehicle;
    g_vehicle.phys.vel[2] = -1.0f;
    trace_t ground;
    ground.fraction = 0.5f;
    ground.normal[2] = 1.0f;
    ground.walkable = true;

    g_traces[0] = ground; // plain hit: the flags after trace_t's pointer member come along
    g_numTraces = 1;
    g_traceCalls = 0;
    s_phys_0 = VehicleLocalPhysics();
    VEH_GroundTrace(ent);
    CHECK(s_phys_0.groundTrace.walkable && s_phys_0.hasGround && s_phys_0.onGround);

    trace_t stuck;
    stuck.allsolid = stuck.startsolid = true;
    ground.surfaceFlags = 0x77;
    g_traces[0] = stuck;   // all solid at the start ...
    g_traces[1] = trace_t(); // ... free at the first correction offset ...
    g_traces[2] = ground;  // ... and the corrected ground trace
    g_numTraces = 3;
    g_traceCalls = 0;
    s_phys_0 = VehicleLocalPhysics();
    VEH_GroundTrace(ent);
    CHECK(!s_phys_0.groundTrace.allsolid && s_phys_0.groundTrace.surfaceFlags == 0x77 && s_phys_0.onGround);
}

#elif GAME_MP_HAZARD_SUBJECT == 2
// g_active_mp.cpp G_TouchTriggers: the touched trigger's own handler runs.
static void TriggersDispatchTheirOwnHandler()
{
    gentity_s *const player = Ent(0);
    player->client = &g_client;
    gentity_s *const trigger = Ent(kTrigger);
    trigger->r.contents = 0x40000000; // CONTENTS_TRIGGER, part of MASK_TRIGGER
    trigger->handler = ENT_HANDLER_TRIGGER_MULTIPLE;
    seen = Seen();
    G_TouchTriggers(player);
    CHECK(seen.touchSelf == trigger && seen.touchOther == player);
}

#elif GAME_MP_HAZARD_SUBJECT == 3
// g_utils_mp.cpp G_Spawn and g_main_mp.cpp G_GetClientSize: the strides the
// server walks game memory with are the real struct sizes.
static void GameDataStridesMatchTheStructs()
{
    level.gentities = g_entities;
    level.clients = g_clients;
    level.num_entities = 72;
    gentity_s *const spawned = G_Spawn();
    CHECK(spawned == &g_entities[72]);
    CHECK(SV_GentityNum(72) == spawned);
    CHECK(SV_GameClientNum(1) == &g_clients[1].ps);
    CHECK(G_GetClientSize() == static_cast<int>(sizeof(gclient_s)));
}
#endif

int main()
{
#if GAME_MP_HAZARD_SUBJECT == 1
    EntityFieldsReadTheirMembers();
    EntityFieldStoresTheWholePointer();
    ReverbCommandsTargetTheEntref();
    DamageNotifyNamesTheHitAttachment();
    CorpseReuseFindsThePlayer();
    GroundTraceKeepsTheWholeTrace();
#elif GAME_MP_HAZARD_SUBJECT == 2
    TriggersDispatchTheirOwnHandler();
#elif GAME_MP_HAZARD_SUBJECT == 3
    GameDataStridesMatchTheStructs();
#endif
    std::printf("game_mp_hazard_tests: %d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}

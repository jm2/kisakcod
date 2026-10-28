// game_mp_hazard_tests.cpp: the game_mp 64-bit layout hazards (NOW row 10,
// #216). The decompiled MP game code hard-coded 32-bit offsets and sizes and
// read attach names and entrefs through 32-bit address arithmetic; each check
// runs the production function at 64-bit and fails on the old code.
//
// One source, five executables (GAME_MP_HAZARD_SUBJECT), so no subject's
// function tables pull another subject's engine code into the link:
//   1: g_spawn_mp, g_client_script_cmd_mp, g_combat_mp, g_player_corpse_mp,
//      g_vehicles_mp, g_scr_vehicle;  2: g_active_mp;  3: g_utils_mp,
//      g_main_mp, server/sv_game;  4: g_client_script_cmd_mp alone for the
//      damage clamp; 5: g_cmds_mp for ClientCommand, callvote and vote. 3
//      also checks that level shutdown pops an open scriptdata read session.
// The engine boundary is weak: the engine TUs replace the stubs they define.

#include <cstdarg>
#include <cctype>
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
#include <script/scr_variable.h>
#include <script/scr_vm.h>
#include <server/sv_game.h>
#include <server_mp/server_mp.h>

extern gclient_s g_clients[64];
extern short s_numVehicleInfos;

namespace
{
int g_failures = 0;
int g_comErrors = 0;
bool g_allowComErrorReturn = false;
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
    int addedEntNum = -1;
    bool madeArray = false;
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
int g_offset = -1; // what Scr_GetOffset returns for the key
int g_scrIntArgs[10] = {};
int g_scrTypes[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

#if GAME_MP_HAZARD_SUBJECT == 5
dvar_t g_allowVoteDvar, g_oldVotingDvar, g_cheatsDvar, g_deadChatDvar;
dvar_t g_dedicatedDvar, g_gametypeDvar, g_mapnameDvar, g_useFastFileDvar;
gclient_s g_levelClients[1];

struct CommandTrace
{
    int count = 0;
    int clients[4] = {};
    char text[4][1024] = {};
    int configCount = 0;
    int configIndex[8] = {};
    char configValue[8][1024] = {};
};
CommandTrace g_commandTrace;
const char *g_cmdArgs[3] = {};
int g_cmdArgc = 0;

void RecordCommand(int clientNum, const char *text)
{
    if (g_commandTrace.count < 4)
    {
        const int index = g_commandTrace.count;
        g_commandTrace.clients[index] = clientNum;
        std::snprintf(g_commandTrace.text[index], sizeof(g_commandTrace.text[index]), "%s", text);
    }
    ++g_commandTrace.count;
}
#endif

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

#if GAME_MP_HAZARD_SUBJECT == 5
WEAK const dvar_t *g_allowVote = &g_allowVoteDvar; WEAK const dvar_t *g_oldVoting = &g_oldVotingDvar;
WEAK const dvar_t *g_cheats = &g_cheatsDvar; WEAK const dvar_t *g_deadChat = &g_deadChatDvar;
WEAK const dvar_t *g_dedicated = &g_dedicatedDvar; WEAK const dvar_t *g_gametype = &g_gametypeDvar;
WEAK const dvar_t *useFastFile = &g_useFastFileDvar;
#endif

WEAK void MyAssertHandler(const char *filename, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at %s:%d\n", filename ? filename : "?", line);
    std::exit(3);
}
WEAK void Com_Error(errorParm_t, const char *, ...)
{
    ++g_comErrors;
    if (g_allowComErrorReturn)
        return;
    std::fprintf(stderr, "Com_Error\n");
    std::exit(2);
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK char *Com_LoadInfoString(char *, const char *, const char *, char *loadBuffer) { return loadBuffer; }
WEAK bool ParseConfigStringToStruct(
    uint8_t *, const cspField_t *, int, char *, int,
    int (__cdecl *)(uint8_t *, const char *, const int),
    void (__cdecl *)(uint8_t *, const char *))
{
    return true;
}
WEAK void BG_StringCopy(uint8_t *, const char *) {}
WEAK int G_SoundAliasIndex(char *) { return 0; }
WEAK int I_stricmp(const char *lhs, const char *rhs)
{
    while (*lhs && *rhs)
    {
        const int left = std::tolower(static_cast<unsigned char>(*lhs++));
        const int right = std::tolower(static_cast<unsigned char>(*rhs++));
        if (left != right)
            return left - right;
    }
    return std::tolower(static_cast<unsigned char>(*lhs))
        - std::tolower(static_cast<unsigned char>(*rhs));
}

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
WEAK void Scr_AddEntityNum(uint32_t entnum, uint32_t) { seen.addedEntNum = static_cast<int>(entnum); }
WEAK void Scr_Notify(gentity_s *, uint16_t, uint32_t) {}
WEAK void Scr_NotifyNum(uint32_t, uint32_t, uint32_t, uint32_t) {}
WEAK void Scr_Error(const char *) {}
WEAK const char *Scr_GetString(uint32_t) { return "key"; }
WEAK int Scr_GetOffset(uint32_t, const char *) { return g_offset; }
WEAK void Scr_MakeArray() { seen.madeArray = true; }
WEAK void Scr_AddArray() {}
WEAK uint32_t Scr_GetNumParam() { return g_numParam; }
WEAK uint32_t Scr_GetConstString(uint32_t) { return g_constString; }
WEAK uint32_t Scr_GetConstStringIncludeNull(uint32_t) { return 0; }
WEAK float Scr_GetFloat(uint32_t) { return g_float; }
WEAK int Scr_GetInt(uint32_t index) { return index < 10 ? g_scrIntArgs[index] : 0; }
WEAK int Scr_GetType(uint32_t index) { return index < 10 ? g_scrTypes[index] : 1; }
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
WEAK uint32_t G_GetWeaponIndexForName(const char *) { return 0; }
WEAK int32_t G_MeansOfDeathFromScriptParam(uint32_t) { return MOD_CRUSH; }
WEAK int32_t G_GetHitLocationIndexFromString(uint16_t) { return HITLOC_NONE; }
WEAK uint32_t Scr_GetPointerType(uint32_t) { return 0; } WEAK gentity_s *Scr_GetEntity(uint32_t) { return nullptr; }
WEAK void Scr_PlayerLastStand(gentity_s *, gentity_s *, gentity_s *, int32_t, uint32_t, uint32_t,
                              const float *, hitLocation_t, int32_t) {}
WEAK gentity_s *G_TempEntity(const float *, int) { return &g_entities[0]; } WEAK uint8_t DirToByte(const float *) { return 0; }
WEAK WeaponDef *BG_GetWeaponDef(uint32_t) { return nullptr; } WEAK const dvar_t *g_knockback;
WEAK const dvar_t *player_dmgtimer_timePerPoint; WEAK const dvar_t *player_dmgtimer_maxTime;

WEAK gentity_s *EntHandle::ent() const { return nullptr; }
WEAK bool EntHandle::isDefined() const { return false; }
WEAK void EntHandle::setEnt(gentity_s *) {}

WEAK uint32_t G_ModelName(uint32_t index) { return index; }
WEAK uint32_t SV_GetConfigstringConst(uint32_t index) { seen.configstring = index; return 0x333; }
WEAK void SV_GameSendServerCommand(int clientNum, svscmd_type, const char *text)
{
#if GAME_MP_HAZARD_SUBJECT == 5
    RecordCommand(clientNum, text);
#else
    seen.commandClient = clientNum;
    std::snprintf(seen.command, sizeof(seen.command), "%s", text);
#endif
}
WEAK void G_FreeEntity(gentity_s *ed) { seen.freed = ed; }
WEAK void TeleportPlayer(gentity_s *, float *, float *) {}
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
STUB_CONTROLLER(G_PlayerController)
STUB_PAIN(Pain_trigger_damage) STUB_PAIN(Helicopter_Pain)
STUB_DIE(Die_trigger_damage) STUB_DIE(G_VehEntHandler_Die) STUB_DIE(Helicopter_Die) STUB_DIE(player_die)

#if GAME_MP_HAZARD_SUBJECT == 5
// g_cmds_mp.cpp engine boundary: the command token stream, server replies,
// configstrings and the few game helpers reached by the command switch.
WEAK int SV_Cmd_Argc() { return g_cmdArgc; }
WEAK const char *SV_Cmd_Argv(int arg) { return arg >= 0 && arg < g_cmdArgc ? g_cmdArgs[arg] : ""; }
WEAK void SV_Cmd_ArgvBuffer(int arg, char *buffer, int bufferLength)
{
    std::snprintf(buffer, bufferLength, "%s", SV_Cmd_Argv(arg));
}
WEAK void SV_SetConfigstring(int index, const char *value)
{
    if (g_commandTrace.configCount < 8)
    {
        const int slot = g_commandTrace.configCount;
        g_commandTrace.configIndex[slot] = index;
        std::snprintf(g_commandTrace.configValue[slot], sizeof(g_commandTrace.configValue[slot]), "%s", value);
    }
    ++g_commandTrace.configCount;
}
WEAK void SV_GetConfigstring(uint32_t, char *buffer, int bufferSize)
{
    if (bufferSize)
        buffer[0] = 0;
}
WEAK int Dvar_GetInt(const char *) { return 0; }
WEAK const char *Dvar_GetString(const char *) { return ""; }
WEAK const dvar_t *Dvar_RegisterString(const char *, const char *value, uint16_t, const char *)
{
    g_mapnameDvar.current.string = value;
    return &g_mapnameDvar;
}
WEAK bool SV_MapExists(char *) { return true; }
WEAK void Cbuf_AddText(int, const char *) {}
WEAK bool Scr_IsValidGameType(const char *) { return true; }
WEAK char *Scr_GetGameTypeNameForScript(const char *) { return const_cast<char *>("dm"); }
WEAK void Scr_VoteCalled(gentity_s *, char *, char *, char *) {}
WEAK void Scr_PlayerVote(gentity_s *, char *) {}
WEAK char *SV_GetGuid(int) { return const_cast<char *>("guid"); }
WEAK void G_LogPrintf(const char *, ...) {} WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {} WEAK char *I_CleanStr(char *value) { return value; }
WEAK void I_strncpyz(char *dest, const char *src, int size) { std::snprintf(dest, size, "%s", src); }
WEAK int I_strnicmp(const char *lhs, const char *rhs, int count)
{
    for (int i = 0; i < count && (*lhs || *rhs); ++i)
    {
        const int left = std::tolower(static_cast<unsigned char>(*lhs++));
        const int right = std::tolower(static_cast<unsigned char>(*rhs++));
        if (left != right)
            return left - right;
    }
    return 0;
}
WEAK bool OnSameTeam(gentity_s *, gentity_s *) { return true; } WEAK bool G_ClientCanSpectateTeam(gclient_s *, team_t) { return false; }
WEAK int SV_GetClientPing(int) { return 0; } WEAK int SV_GetArchivedClientInfo(int, int *, playerState_s *, clientState_s *) { return 0; }
WEAK void G_PrintEntities() {} WEAK char *vtos(const float *) { return const_cast<char *>(""); }
WEAK void G_SetOrigin(gentity_s *, const float *) {} WEAK void SetClientViewAngle(gentity_s *, const float *) {}
WEAK int32_t Add_Ammo(gentity_s *, uint32_t, uint8_t, int32_t, int32_t) { return 0; } WEAK const gitem_s *G_FindItem(const char *, int32_t) { return nullptr; }
WEAK void G_SpawnItem(gentity_s *, const gitem_s *) {} WEAK void G_GetItemClassname(const gitem_s *, uint16_t *) {}
WEAK int32_t G_GivePlayerWeapon(playerState_s *, int32_t, uint8_t) { return 0; } WEAK void G_SelectWeaponIndex(int32_t, int32_t) {}
WEAK uint32_t BG_GetNumWeapons() { return 0; } WEAK bool BG_CanPlayerHaveWeapon(uint32_t) { return false; }
WEAK int32_t BG_TakePlayerWeapon(playerState_s *, uint32_t, int32_t) { return 0; } WEAK int32_t BG_AmmoForWeapon(uint32_t) { return 0; }
WEAK int32_t BG_ClipForWeapon(uint32_t) { return 0; } WEAK void Touch_Item(gentity_s *, gentity_s *, int32_t) {}
WEAK gentity_s *G_Spawn() { return nullptr; }
WEAK int Com_sprintf(char *dest, uint32_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // flawfinder: ignore (test-only wrapper forwarding the production TU literal format)
    const int written = size ? std::vsnprintf(dest, size, format, args) : 0;
    va_end(args);
    return written;
}
#endif

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

// g_vehicles_mp.cpp G_VehUnlinkPlayer: a player script-linked to a vehicle
// without a seat is left alone (it was an ERR_DROP), and a seated player whose
// vehicle was freed is released without touching the missing vehicle.
static void VehicleUnlinkHandlesUnseatedAndFreed()
{
    gentity_s *const vehicle = Ent(21);
    vehicle->scr_vehicle = nullptr;
    gentity_s *const player = Ent(6);
    player->client = &g_client;

    g_client.ps.pm_flags = 0;
    g_client.ps.viewlocked_entNum = 21;
    G_VehUnlinkPlayer(vehicle, player);
    CHECK(g_client.ps.pm_flags == 0 && g_client.ps.viewlocked_entNum == 21);

    g_client.ps.pm_flags = PMF_VEHICLE_ATTACHED;
    G_VehUnlinkPlayer(vehicle, player);
    CHECK((g_client.ps.pm_flags & PMF_VEHICLE_ATTACHED) == 0);
    CHECK(g_client.ps.viewlocked_entNum == ENTITYNUM_NONE);
    g_client.ps.pm_flags = 0;
    g_client.ps.viewlocked_entNum = 0;
}

// g_spawn_mp.cpp Scr_GetEnt / Scr_GetEntArray: a client field's key comes back
// from Scr_GetOffset as (index | 0xC000). It finds nothing, where the old code
// indexed fields_1 with it, 768 KB past the 11-entry table.
static void GetEntWithAClientFieldFindsNothing()
{
    level.num_entities = 64;
    Ent(9)->targetname = 0x55;
    g_constString = 0x55;
    g_numParam = 2;

    g_offset = 0xC000 | 3;
    seen = Seen();
    Scr_GetEnt();
    CHECK(seen.addedEntNum == -1);
    Scr_GetEntArray();
    CHECK(seen.addedEntNum == -1 && !seen.madeArray);

    g_offset = 5; // targetname, an entity field: still found
    seen = Seen();
    Scr_GetEnt();
    CHECK(seen.addedEntNum == 9);
    seen = Seen();
    Scr_GetEntArray();
    CHECK(seen.madeArray && seen.addedEntNum == 9);

    g_offset = -1;
    g_constString = 0;
    g_numParam = 1;
}

// #258 item 1: hintStrings must carry retail's fifth entry. The MP consumers
// (GScr_SetCursorHint, trigger_use cursorhint) loop to index 4, so a four-entry
// table is an out-of-bounds read. This is the compiled table every game TU gets
// from game_public.h; the consumer functions themselves need the script VM.
static void HintTableIncludesFriendly()
{
    constexpr std::size_t count = sizeof(hintStrings) / sizeof(hintStrings[0]);
    CHECK(count == 5);
    if (count == 5)
    {
        CHECK(!std::strcmp(hintStrings[4], "HINT_FRIENDLY"));
        CHECK(!std::strcmp(hintStrings[3], "HINT_HEALTH"));
    }
}

// #258 item 2: the MP G_LoadVehicle writes the next s_vehicleInfos slot before
// it bumps s_numVehicleInfos. The 33rd distinct type must be an ERR_DROP, not a
// write past the 32-entry table (which an ASan build reports as a global
// overflow and a release build silently corrupts).
static void VehicleTableRejectsThe33rdType()
{
    s_numVehicleInfos = 0;
    char name[32];
    for (int i = 0; i < 32; ++i)
    {
        std::snprintf(name, sizeof(name), "test_vehicle_%i", i);
        CHECK(G_LoadVehicle(name) == i);
    }
    CHECK(s_numVehicleInfos == 32);

    g_comErrors = 0;
    g_allowComErrorReturn = true;
    CHECK(G_LoadVehicle("test_vehicle_32") == 0);
    g_allowComErrorReturn = false;
    CHECK(g_comErrors == 1);
    CHECK(s_numVehicleInfos == 32);
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

// g_main_mp.cpp G_AllocXAnimServer: the XAnim allocator the game hands to
// bgs_t::AllocXAnim and XAnimCreateTree has their void *(int) type and
// forwards the size to the hunk (called as AllocXAnim calls it).
uint32_t g_xanimRequested = 0;
uint8_t g_xanimBlock[64];
uint8_t *__cdecl Hunk_AllocXAnimServer(uint32_t size)
{
    g_xanimRequested = size;
    return g_xanimBlock;
}
static void XAnimAllocatorHasItsCallersType()
{
    void *(__cdecl *const alloc)(int32_t) = G_AllocXAnimServer;
    CHECK(alloc(48) == g_xanimBlock);
    CHECK(g_xanimRequested == 48u);
}

// g_main_mp.cpp G_BgGetXModel / G_BgCreateDObj: the model and DObj callbacks
// G_InitGame hands to level_bgs have bgs_t's own types (called through them,
// as the bgame code calls them) and forward to SV_XModelGet / G_CreateDObj.
const char *g_precached = nullptr;
uint8_t *__cdecl Hunk_Alloc(uint32_t, const char *, int) { return g_xanimBlock; } // SV_XModelGet's allocators
XModel *const kModel = reinterpret_cast<XModel *>(g_xanimBlock);
XModel *__cdecl XModelPrecache(char *name, void *(__cdecl *)(int), void *(__cdecl *)(int))
{
    g_precached = name;
    return kModel;
}
DObjModel_s *g_dobjModels = nullptr;
uint32_t g_dobjHandle = 0;
int g_dobjNumModels = 0;
DObj_s *__cdecl Com_ServerDObjCreate(DObjModel_s *dobjModels, uint16_t numModels, XAnimTree_s *, uint32_t handle)
{
    g_dobjModels = dobjModels;
    g_dobjNumModels = numModels;
    g_dobjHandle = handle;
    return nullptr;
}
static void BgCallbacksHaveTheirCallersTypes()
{
    bgs_t bgs{};
    bgs.GetXModel = G_BgGetXModel;
    bgs.CreateDObj = G_BgCreateDObj;
    const char name[] = "body_mp_fake";
    CHECK(bgs.GetXModel(name) == kModel);
    CHECK(g_precached == name);
    DObjModel_s models[2]{};
    bgs.CreateDObj(models, 2, nullptr, 17, -1, nullptr);
    CHECK(g_dobjModels == models);
    CHECK(g_dobjNumModels == 2);
    CHECK(g_dobjHandle == 17u);
}

// The rest of G_ShutdownGame's engine boundary, reached now that the test
// calls it. None of these run in the shutdown under test: level.num_entities
// is 0 and freeScripts is 0, so the entity, tree and script paths stay empty.
WEAK void Com_SafeServerDObjFree(uint32_t) {}
WEAK void Com_FreeWeaponInfoMemory(int) {}
WEAK int Com_sprintf(char *dest, uint32_t size, const char *format, ...)
{
#if GAME_MP_HAZARD_SUBJECT == 5
    va_list args;
    va_start(args, format);
    // flawfinder: ignore (test-only wrapper forwarding the production TU literal format)
    const int written = size ? std::vsnprintf(dest, size, format, args) : 0;
    va_end(args);
    return written;
#else
    if (size)
        dest[0] = 0;
    return 0;
#endif
}
WEAK XAnimTree_s *DObjGetTree(const DObj_s *) { return nullptr; }
WEAK int32_t EntHandle::entnum() { return -1; }
WEAK void EntHandle::Shutdown() {}
WEAK void EntHandleDissociate(gentity_s *) {}
WEAK uint32_t FS_Write(const char *, uint32_t, int) { return 0; }
WEAK void FS_FCloseFile(int) {}
WEAK void G_FreeTurret(gentity_s *) {}
WEAK void G_VehFreeEntity(gentity_s *) {}
WEAK void G_VehUnlinkPlayer(gentity_s *, gentity_s *) {}
WEAK void GScr_FreeScripts() {}
WEAK void Hunk_ClearToMarkLow(int) {}
WEAK void HudElem_DestroyAll() {}
WEAK void MT_Free(void *, int) {}
WEAK void Mantle_ShutdownAnims() {}
WEAK void Missile_FreeAttractorRefs(gentity_s *) {}
WEAK void PlayerCorpse_Free(gentity_s *) {}
WEAK void Scr_FreeEntity(gentity_s *) {}
WEAK void Scr_FreeScripts(unsigned char) {}
WEAK void Scr_ShutdownSystem(uint8_t, int) {}
WEAK void SetClientViewAngle(gentity_s *, const float *) {}
WEAK void SV_FreeClientScriptPers() {}
WEAK void SV_UnlinkEntity(gentity_s *) {}
WEAK void XAnimClearTree(XAnimTree_s *) {}
WEAK void XAnimFreeTree(XAnimTree_s *, void (*)(void *, int)) {}
WEAK void Z_VirtualFree(void *) {}
WEAK bool Sys_IsMainThread() { return true; }
WEAK bool Sys_IsRenderThread() { return false; }
WEAK bool Sys_IsDatabaseThread() { return false; }
WEAK void track_shutdown(int) {}

// g_main_mp.cpp G_ShutdownGame: a read-mode scriptdata file holds the CSV
// parse session until it is closed. Level shutdown must pop that session, or
// later session-less parsers (the ban check) inherit CSV mode.
static void ShutdownPopsOpenScriptReadSession()
{
    char scriptBuffer[2] = {};
    Com_InitParse();
    level.openScriptIOFileBuffers[0] = scriptBuffer;
    Com_BeginParseSession("open_script_read");
    Com_SetCSV(1);
    CHECK(Com_GetParseThreadInfo()->parseInfoNum == 1);

    G_ShutdownGame(0);

    CHECK(level.openScriptIOFileBuffers[0] == nullptr);
    CHECK(Com_GetParseThreadInfo()->parseInfoNum == 0);
    const char *line = "GUID name";
    CHECK(!std::strcmp(Com_Parse(&line)->token, "GUID"));
}
#elif GAME_MP_HAZARD_SUBJECT == 4
// g_client_script_cmd_mp.cpp finishPlayerDamage: exactly lethal damage drops
// health to 0. The old unsigned comparison treated 0 as below the signed
// sentinel and clamped it to -999; the signed comparison leaves it at 0.
static void FinishPlayerDamageClampsLethalHealth()
{
    static dvar_t zeroDvar;
    g_knockback = &zeroDvar;
    player_dmgtimer_timePerPoint = &zeroDvar;
    player_dmgtimer_maxTime = &zeroDvar;

    gentity_s *const self = Ent(4);
    self->client = &g_client;
    self->handler = ENT_HANDLER_NULL;
    self->health = 10;
    self->flags = static_cast<gentityFlags_t>(0);
    g_client.lastStand = 0;
    g_client.ps.pm_type = PM_NORMAL;
    g_client.ps.perks = 0;
    // cppcheck-suppress y2038-unsafe-call (fixture writes zero server time; no libc time call)
    level.time = 0;
    g_scrIntArgs[2] = 10;
    g_scrIntArgs[3] = 4;
    g_scrTypes[0] = g_scrTypes[1] = g_scrTypes[6] = g_scrTypes[7] = 0;

    scr_entref_t entref;
    entref.entnum = 4;
    entref.classnum = 0;
    PlayerCmd_finishPlayerDamage(entref);
    CHECK(self->health == 0);
}

#elif GAME_MP_HAZARD_SUBJECT == 5
static void RunClientCommand(const char *arg0, const char *arg1 = nullptr, const char *arg2 = nullptr)
{
    g_cmdArgs[0] = arg0;
    g_cmdArgs[1] = arg1;
    g_cmdArgs[2] = arg2;
    g_cmdArgc = arg1 ? (arg2 ? 3 : 2) : 1;
    ClientCommand(0);
}

static void ResetCommandFixture()
{
    g_commandTrace = CommandTrace();
    g_allowVoteDvar.current.enabled = true;
    g_oldVotingDvar.current.enabled = true;
    g_cheatsDvar.current.enabled = true;
    g_deadChatDvar.current.enabled = true;
    g_dedicatedDvar.current.integer = 0;
    g_gametypeDvar.current.string = "dm";
    g_useFastFileDvar.current.enabled = false;

    level.clients = g_levelClients;
    level.maxclients = 1;
    level.numConnectedClients = 2;
    // cppcheck-suppress y2038-unsafe-call (fixture writes zero server time; no libc time call)
    level.time = 0;
    level.voteTime = 0;
    level.voteExecuteTime = 0;
    level.voteYes = 0;
    level.voteNo = 0;

    g_entities[0] = gentity_s();
    g_levelClients[0] = gclient_s();
    gentity_s *const ent = Ent(0);
    ent->client = &g_levelClients[0];
    ent->client->sess.connected = CON_CONNECTED;
    ent->client->sess.sessionState = static_cast<sessionState_t>(0);
    ent->client->sess.cs.team = TEAM_ALLIES;
    ent->client->ps.pm_type = PM_NORMAL;
    std::snprintf(ent->client->sess.cs.name, sizeof(ent->client->sess.cs.name), "Player");
}

static const char *ConfigValue(int index)
{
    for (int i = 0; i < g_commandTrace.configCount && i < 8; ++i)
    {
        if (g_commandTrace.configIndex[i] == index)
            return g_commandTrace.configValue[i];
    }
    return nullptr;
}

// ClientCommand say -> Cmd_Say_f -> ConcatArgs -> G_Say -> G_SayTo: exact server-command bytes and 150-byte chat copy boundary.
static void ClientCommandSaysBoundedBytes()
{
    ResetCommandFixture();
    RunClientCommand("say", "hello");
    CHECK(g_commandTrace.count == 1);
    CHECK(g_commandTrace.clients[0] == 0);
    CHECK(!std::strcmp(g_commandTrace.text[0], "h \"\x15\x15^8Player^7: ^7hello\""));

    char longChat[600];
    std::memset(longChat, 'A', sizeof(longChat) - 1);
    longChat[sizeof(longChat) - 1] = 0;
    ResetCommandFixture();
    RunClientCommand("say", longChat);
    CHECK(g_commandTrace.count == 1);
    CHECK(g_commandTrace.text[0][0] == 'h' && g_commandTrace.text[0][1] == ' '
          && g_commandTrace.text[0][2] == '"');
    const size_t textLen = strnlen(g_commandTrace.text[0], sizeof(g_commandTrace.text[0]));
    CHECK(textLen > 0 && g_commandTrace.text[0][textLen - 1] == '"');
    CHECK(textLen + 1 < sizeof(g_commandTrace.text[0]));
}

// ClientCommand callvote -> Cmd_CallVote_f: fixed vote command/display strings/configstrings, then a 300-byte map against the 256/1024 bounds.
static void CallVoteBuildsBoundedBytes()
{
    ResetCommandFixture();
    RunClientCommand("callvote", "map_restart");
    CHECK(!std::strcmp(level.voteString, "fast_restart"));
    CHECK(!std::strcmp(level.voteDisplayString, "GAME_VOTE_MAPRESTART"));
    CHECK(level.voteTime == 30000 && level.voteYes == 1 && level.voteNo == 0);
    CHECK(g_commandTrace.count == 1 && g_commandTrace.clients[0] == -1);
    CHECK(!std::strcmp(g_commandTrace.text[0], "e \"GAME_CALLEDAVOTE\x15Player\""));
    CHECK(ConfigValue(13) && !std::strcmp(ConfigValue(13), "30000 0"));
    CHECK(ConfigValue(14) && !std::strcmp(ConfigValue(14), "GAME_VOTE_MAPRESTART"));
    CHECK(ConfigValue(15) && !std::strcmp(ConfigValue(15), "1"));
    CHECK(ConfigValue(16) && !std::strcmp(ConfigValue(16), "0"));

    char longMap[301];
    std::memset(longMap, 'm', sizeof(longMap) - 1);
    longMap[sizeof(longMap) - 1] = 0;
    ResetCommandFixture();
    RunClientCommand("callvote", "map", longMap);
    CHECK(!std::strncmp(level.voteString, "map ", 4));
    const size_t voteLen = strnlen(level.voteString, sizeof(level.voteString));
    CHECK(voteLen == 4 + 255);
    CHECK(voteLen < sizeof(level.voteString));
}

// ClientCommand vote -> Cmd_Vote_f: the 64-byte token selects yes/no; the counter goes to configstring 15 or 16.
static void VoteUpdatesConfigstringBytes()
{
    ResetCommandFixture();
    level.voteTime = 30000;
    level.voteYes = 1;
    g_levelClients[0].ps.eFlags = 0;
    RunClientCommand("vote", "yes");
    CHECK(level.voteYes == 2);
    CHECK(ConfigValue(15) && !std::strcmp(ConfigValue(15), "2"));
    CHECK(g_commandTrace.count == 1 && g_commandTrace.clients[0] == 0);
    CHECK(!std::strcmp(g_commandTrace.text[0], "e \"GAME_VOTECAST\""));

    ResetCommandFixture();
    level.voteTime = 30000;
    level.voteNo = 0;
    g_levelClients[0].ps.eFlags = 0;
    RunClientCommand("vote", "no");
    CHECK(level.voteNo == 1);
    CHECK(ConfigValue(16) && !std::strcmp(ConfigValue(16), "1"));
    CHECK(g_commandTrace.count == 1 && g_commandTrace.clients[0] == 0);
    CHECK(!std::strcmp(g_commandTrace.text[0], "e \"GAME_VOTECAST\""));
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
    VehicleUnlinkHandlesUnseatedAndFreed();
    GetEntWithAClientFieldFindsNothing();
    HintTableIncludesFriendly();
    VehicleTableRejectsThe33rdType();
#elif GAME_MP_HAZARD_SUBJECT == 2
    TriggersDispatchTheirOwnHandler();
#elif GAME_MP_HAZARD_SUBJECT == 4
    FinishPlayerDamageClampsLethalHealth();
#elif GAME_MP_HAZARD_SUBJECT == 5
    ClientCommandSaysBoundedBytes();
    CallVoteBuildsBoundedBytes();
    VoteUpdatesConfigstringBytes();
#elif GAME_MP_HAZARD_SUBJECT == 3
    GameDataStridesMatchTheStructs();
    XAnimAllocatorHasItsCallersType();
    BgCallbacksHaveTheirCallersTypes();
    ShutdownPopsOpenScriptReadSession();
#endif
    std::printf("game_mp_hazard_tests: %d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}

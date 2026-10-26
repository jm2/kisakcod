// Exercises the production spawnclient command (#817) with engine calls stubbed.
#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>

#include <game/game_public.h>
#include <game_mp/g_main_mp.h>
#include <game_mp/g_public_mp.h>
#include <game_mp/g_utils_mp.h>
#include <qcommon/cmd.h>
#include <server/sv_game.h>

namespace
{
int g_failures;

void Check(bool ok, const char *expr, int line)
{
    if (!ok)
    {
        std::fprintf(stderr, "line %d: CHECK(%s)\n", line, expr);
        ++g_failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

gclient_s g_clients[64];
dvar_s g_fakeCheats;
char g_console[4096];
std::string g_args[2];
int g_argc;
constexpr int SPAWN_ENT = 900;
std::string g_stringNames[8];
int g_stringCount;
int g_spawnCalls;
int g_spawnNum;
int g_spawnHealth;
float g_spawnOrigin[3];
int g_giveCalls;
int g_initializeAmmoCalls;
int g_selectWeaponCalls;
int g_selectWeaponNum;
int g_weaponIndex;
bgs_t g_previousBgs;
bgs_t *g_bgsSeen;

uint16_t InternString(const char *name)
{
    for (int i = 0; i < g_stringCount; ++i)
    {
        if (g_stringNames[i] == name)
            return static_cast<uint16_t>(100 + i);
    }
    g_stringNames[g_stringCount] = name;
    return static_cast<uint16_t>(100 + g_stringCount++);
}

void SetSpawnClass(const char *name)
{
    g_entities[SPAWN_ENT].classname = InternString(name);
}

void Reset()
{
    std::memset(g_clients, 0, sizeof(g_clients));
    std::memset(g_console, 0, sizeof(g_console));
    std::memset(g_entities, 0, sizeof(gentity_s) * MAX_GENTITIES);
    g_argc = 2;
    g_args[0] = "spawnclient";
    g_stringCount = 0;
    g_spawnCalls = 0;
    g_spawnNum = -1;
    g_spawnHealth = -1;
    g_giveCalls = 0;
    g_initializeAmmoCalls = 0;
    g_selectWeaponCalls = 0;
    g_selectWeaponNum = -1;
    g_weaponIndex = 37;
    bgs = &g_previousBgs;
    level.clients = g_clients;
    level.maxclients = 4;
    g_fakeCheats.current.enabled = true;
    g_cheats = &g_fakeCheats;
    gentity_s *spot = &g_entities[SPAWN_ENT];
    spot->r.inuse = 1;
    spot->r.currentOrigin[0] = 100.0f;
    spot->r.currentOrigin[1] = 200.0f;
    spot->r.currentOrigin[2] = 300.0f;
    spot->r.currentAngles[1] = 45.0f;
    SetSpawnClass("mp_tdm_spawn");
}

void Spectator(int clientNum)
{
    gclient_s *client = &g_clients[clientNum];
    client->sess.connected = CON_CONNECTED;
    client->sess.sessionState = SESS_STATE_SPECTATOR;
    client->sess.cs.team = TEAM_FREE;
    g_entities[clientNum].client = client;
    g_entities[clientNum].r.inuse = 1;
}

void Run(const char *arg)
{
    g_args[1] = arg;
    Svcmd_SpawnClient_f();
}
} // namespace

#define WEAK __attribute__((weak))

WEAK gentity_s g_entities[MAX_GENTITIES];
WEAK level_locals_t level;
WEAK scr_const_t scr_const;
WEAK const dvar_t *g_cheats;
WEAK bgs_t level_bgs;
WEAK bgs_t *bgs;

WEAK void MyAssertHandler(const char *, int line, int, const char *, ...)
{
    std::fprintf(stderr, "engine assert at line %d\n", line);
    std::exit(3);
}

WEAK void Com_Printf(int, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const char *end = std::find(g_console, g_console + sizeof(g_console), '\0');
    const size_t used = static_cast<size_t>(end - g_console);
    // Flawfinder: ignore -- passthrough console shim; callers own the literal format.
    std::vsnprintf(g_console + used, sizeof(g_console) - used, fmt, args);
    va_end(args);
}

WEAK int Cmd_Argc() { return g_argc; }
WEAK const char *Cmd_Argv(int index) { return index >= 0 && index < g_argc ? g_args[index].c_str() : ""; }
WEAK int I_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
WEAK uint32_t GScr_AllocString(const char *name) { return InternString(name); }

WEAK gentity_s *G_Find(gentity_s *, int field, uint16_t match)
{
    for (gentity_s &ent : g_entities)
    {
        if (ent.r.inuse && field == static_cast<int>(offsetof(gentity_s, classname)) && ent.classname == match)
            return &ent;
    }
    return nullptr;
}

WEAK void ClientSpawn(gentity_s *ent, const float *origin, const float *)
{
    ++g_spawnCalls;
    g_bgsSeen = bgs;
    g_spawnNum = static_cast<int>(ent - g_entities);
    std::memcpy(g_spawnOrigin, origin, sizeof(g_spawnOrigin));
    g_spawnHealth = ent->health;
    ent->client->ps.weapon = 0;
    std::memset(ent->client->ps.ammo, 0, sizeof(ent->client->ps.ammo));
    std::memset(ent->client->ps.ammoclip, 0, sizeof(ent->client->ps.ammoclip));
}

WEAK void ClientUserinfoChanged(uint32_t) {}
WEAK void CalculateRanks() {}
WEAK uint32_t G_GetWeaponIndexForName(const char *name)
{
    return std::strcmp(name, "mp5_mp") == 0 ? static_cast<uint32_t>(g_weaponIndex) : 0;
}
WEAK int32_t G_GivePlayerWeapon(playerState_s *, int32_t, uint8_t)
{
    ++g_giveCalls;
    return 1;
}
WEAK void G_InitializeAmmo(gentity_s *ent, int32_t, uint8_t, int32_t)
{
    ++g_initializeAmmoCalls;
    ent->client->ps.ammo[0] = 90;
    ent->client->ps.ammoclip[0] = 30;
}
WEAK void G_SelectWeaponIndex(int32_t clientNum, int32_t weapon)
{
    ++g_selectWeaponCalls;
    g_selectWeaponNum = clientNum * 1000 + weapon;
}

int main()
{
    Reset();
    g_fakeCheats.current.enabled = false;
    Spectator(0);
    Run("0");
    CHECK(g_spawnCalls == 0 && g_clients[0].sess.sessionState == SESS_STATE_SPECTATOR
          && std::strstr(g_console, "refused: sv_cheats is 0"));

    Reset();
    Spectator(0);
    Run("0");
    CHECK(g_spawnCalls == 1 && g_spawnNum == 0 && g_spawnHealth == 100);
    CHECK(g_spawnOrigin[0] == 100.0f && g_spawnOrigin[1] == 200.0f && g_spawnOrigin[2] == 309.0f);
    CHECK(g_clients[0].sess.sessionState == SESS_STATE_PLAYING && g_clients[0].sess.cs.team == TEAM_AXIS);
    CHECK(g_clients[0].sess.maxHealth == 100 && g_entities[0].health == 100 && g_entities[0].maxHealth == 100);
    CHECK(g_clients[0].ps.weapon == g_weaponIndex && g_giveCalls == 1 && g_initializeAmmoCalls == 1);
    CHECK(g_clients[0].ps.ammo[0] == 90 && g_clients[0].ps.ammoclip[0] == 30);
    CHECK(g_selectWeaponCalls == 1 && g_selectWeaponNum == g_weaponIndex);
    CHECK(g_bgsSeen == &level_bgs && bgs == &g_previousBgs);
    CHECK(std::strstr(g_console, "spawnclient: client 0 spawned") && std::strstr(g_console, "spawn=mp_tdm_spawn"));

    Reset();
    SetSpawnClass("script_model");
    Spectator(0);
    Run("0");
    CHECK(g_spawnCalls == 0 && g_clients[0].sess.sessionState == SESS_STATE_SPECTATOR
          && std::strstr(g_console, "no known spawn point"));

    Reset();
    g_weaponIndex = 0;
    Spectator(0);
    Run("0");
    CHECK(g_spawnCalls == 1 && g_clients[0].sess.sessionState == SESS_STATE_PLAYING
          && std::strstr(g_console, "spawned without a weapon"));

    Reset();
    Run("4");
    CHECK(g_spawnCalls == 0 && std::strstr(g_console, "out of range"));

    if (g_failures)
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}

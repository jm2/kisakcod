// Dev-only console command: spawnclient <clientnum>.
// #817: ClientConnect leaves a command-line client in SESS_STATE_SPECTATOR, so
// no played round can be captured. This applies the state a map script normally
// supplies and calls ClientSpawn. It is a characterisation tool, not the fix.
#include "g_public_mp.h"

#include <cstddef>
#include <cstdlib>

#include <game_mp/g_utils_mp.h>
#include <qcommon/cmd.h>
#include <server/sv_game.h>

namespace
{
constexpr int DEV_SPAWN_MAX_HEALTH = 100;
constexpr const char *DEV_SPAWN_WEAPON = "mp5_mp";
constexpr const char *DEV_SPAWN_CLASSES[] = {
    "mp_tdm_spawn", "mp_dm_spawn", "mp_dom_spawn", "info_player_start"};

const char *DevSpawnTeamName(team_t team)
{
    return team == TEAM_AXIS ? "axis" : team == TEAM_ALLIES ? "allies"
         : team == TEAM_SPECTATOR ? "spectator" : "free";
}

// Pick the team with fewer connected players, axis on a tie.
team_t DevSpawnPickTeam(team_t current)
{
    if (current == TEAM_AXIS || current == TEAM_ALLIES)
        return current;
    int counts[2] = {};
    for (int i = 0; i < level.maxclients; ++i)
    {
        const gclient_s *client = &level.clients[i];
        if (client->sess.connected != CON_CONNECTED || client->sess.sessionState == SESS_STATE_SPECTATOR)
            continue;
        if (client->sess.cs.team == TEAM_AXIS)
            ++counts[0];
        else if (client->sess.cs.team == TEAM_ALLIES)
            ++counts[1];
    }
    return counts[0] <= counts[1] ? TEAM_AXIS : TEAM_ALLIES;
}

gentity_s *DevSpawnFindPoint(const char *&spawnClass)
{
    const int field = static_cast<int>(offsetof(gentity_s, classname));
    for (const char *candidate : DEV_SPAWN_CLASSES)
    {
        const uint16_t classId = static_cast<uint16_t>(GScr_AllocString(candidate));
        if (classId && G_Find(nullptr, field, classId))
        {
            spawnClass = candidate;
            return G_Find(nullptr, field, classId);
        }
    }
    spawnClass = nullptr;
    return nullptr;
}

void DevSpawnReport(int clientNum, const char *result, team_t team, int maxHealth, int weapon, const char *spawnClass)
{
    Com_Printf(0, "spawnclient: client %i %s (team=%s maxHealth=%i weapon=%i spawn=%s)\n",
               clientNum, result, DevSpawnTeamName(team), maxHealth, weapon, spawnClass ? spawnClass : "-");
}

bool DevSpawnClient(int clientNum)
{
    gentity_s *ent = &g_entities[clientNum];
    gclient_s *client = &level.clients[clientNum];
    if (client->sess.connected != CON_CONNECTED || !ent->r.inuse)
    {
        DevSpawnReport(clientNum, "refused: client is not connected", TEAM_FREE, 0, 0, nullptr);
        return false;
    }
    const char *spawnClass = nullptr;
    gentity_s *spot = DevSpawnFindPoint(spawnClass);
    if (!spot)
    {
        DevSpawnReport(clientNum, "refused: no known spawn point", client->sess.cs.team, client->sess.maxHealth, 0, nullptr);
        return false;
    }
    client->sess.cs.team = DevSpawnPickTeam(client->sess.cs.team);
    if (client->sess.maxHealth <= 0) client->sess.maxHealth = DEV_SPAWN_MAX_HEALTH;

    // ClientSpawn preserves sess but clears the rest of gclient_s, including
    // health and weapon state, so set these first and re-arm the weapon after.
    client->sess.sessionState = SESS_STATE_PLAYING;
    ent->maxHealth = client->sess.maxHealth;
    ent->health = client->sess.maxHealth;
    ClientUserinfoChanged(static_cast<uint32_t>(clientNum));
    CalculateRanks();
    float origin[3] = {spot->r.currentOrigin[0], spot->r.currentOrigin[1], spot->r.currentOrigin[2] + 9.0f};

    // Console commands run between server frames, after G_RunFrame clears bgs.
    // ClientSpawn -> ClientEndFrame -> BG_UpdatePlayerDObj needs it; level_bgs
    // already carries the latest frame's clock values.
    bgs_t *savedBgs = bgs;
    bgs = &level_bgs;
    ClientSpawn(ent, origin, spot->r.currentAngles);
    bgs = savedBgs;

    const int weapon = static_cast<int>(G_GetWeaponIndexForName(DEV_SPAWN_WEAPON));
    const bool armed = weapon && G_GivePlayerWeapon(&client->ps, weapon, 0);
    if (armed)
    {
        G_InitializeAmmo(ent, weapon, 0, 0);
        client->ps.weapon = weapon;
        G_SelectWeaponIndex(clientNum, weapon);
    }
    DevSpawnReport(clientNum, armed ? "spawned" : "spawned without a weapon",
                   client->sess.cs.team, client->sess.maxHealth, armed ? weapon : 0, spawnClass);
    return true;
}
} // namespace

void __cdecl Svcmd_SpawnClient_f()
{
    if (!g_cheats || !g_cheats->current.enabled)
    {
        Com_Printf(0, "spawnclient: refused: sv_cheats is 0 (dev-only command)\n");
        return;
    }
    if (Cmd_Argc() < 2) { Com_Printf(0, "Usage: spawnclient <clientnum>\n"); return; }
    const char *arg = Cmd_Argv(1);
    char *end = nullptr;
    const long parsed = std::strtol(arg, &end, 10);
    if (end == arg || *end != '\0')
    {
        Com_Printf(0, "spawnclient: refused: '%s' is not a client number\n", arg);
        return;
    }
    if (parsed < 0 || parsed >= level.maxclients)
    {
        Com_Printf(0, "spawnclient: refused: client number %li is out of range [0, %i)\n", parsed, level.maxclients);
        return;
    }
    DevSpawnClient(static_cast<int>(parsed));
}

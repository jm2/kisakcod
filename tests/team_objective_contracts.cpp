// Production declarations and dispatch, with small engine context doubles.
#include <bgame/bg_target_protocol.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#ifndef _MSC_VER
#define __cdecl
#define __int32 std::int32_t
#endif
namespace {
void Require(bool value, const char *expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "team/objective contract line %d: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(value) Require(bool(value), #value, __LINE__)
namespace mp {
#define KISAK_MP 1
#include "team_enum.inc"
#undef KISAK_MP
}
namespace sp {
#define KISAK_SP 1
#include "team_enum.inc"
#undef KISAK_SP
}
static_assert(mp::TEAM_FREE == 0 && mp::TEAM_BAD == 0 && mp::TEAM_AXIS == 1
    && mp::TEAM_ALLIES == 2 && mp::TEAM_SPECTATOR == 3 && mp::TEAM_NUM_TEAMS == 4);
static_assert(sp::TEAM_FREE == 0 && sp::TEAM_BAD == 0 && sp::TEAM_AXIS == 1
    && sp::TEAM_ALLIES == 2 && sp::TEAM_NEUTRAL == 3 && sp::TEAM_DEAD == 4
    && sp::TEAM_NUM_TEAMS == 5);
static_assert(sizeof(mp::team_t) == 4 && sizeof(sp::team_t) == 4);
#include "objective_count.inc"
#include "objective_enum.inc"
#include "objective_record.inc"
static_assert(MAX_OBJECTIVES == 16);
static_assert(OBJST_EMPTY == 0 && OBJST_ACTIVE == 1 && OBJST_INVISIBLE == 2
    && OBJST_DONE == 3 && OBJST_CURRENT == 4 && OBJST_FAILED == 5 && OBJST_NUMSTATES == 6);
static_assert(sizeof(objectiveState_t) == 4 && sizeof(objective_t) == 28);
static_assert(offsetof(objective_t, origin) == 4 && offsetof(objective_t, entNum) == 16
    && offsetof(objective_t, teamNum) == 20 && offsetof(objective_t, icon) == 24);
enum {
#include "objective_cs_first.inc"
#include "objective_cs_last.inc"
};
static_assert(CS_OBJECTIVES == 11 && CS_OBJECTIVES_LAST == 26);
#include "objective_info_record.inc"
#include "objective_target_record.inc"
struct ObjectiveArrayBoundary {
#include "objective_adjacent_arrays.inc"
};
static_assert(std::extent_v<decltype(ObjectiveArrayBoundary::objectives)> == 16);
static_assert(offsetof(ObjectiveArrayBoundary, targets)
    == sizeof(ObjectiveArrayBoundary::objectives));
namespace mp {
struct cg_s {
#include "scoreboard_team_arrays.inc"
    int numScores;
};
static_assert(std::extent_v<decltype(cg_s::teamScores)> == 4);
static_assert(std::extent_v<decltype(cg_s::teamPings)> == 4);
static_assert(std::extent_v<decltype(cg_s::teamPlayers)> == 4);
cg_s scores{};
cg_s *CG_GetLocalClientGlobals(int localClientNum) { CHECK(localClientNum == 0); return &scores; }
#include "scoreboard_lines_body.inc"
void CheckScoreboardLines()
{
    for (unsigned int teams = 0; teams < 16; ++teams) {
        scores.numScores = 7;
        int expected = 7;
        for (int team = 0; team < 4; ++team) {
            scores.teamScores[team] = 100 + team;
            scores.teamPings[team] = 200 + team;
            scores.teamPlayers[team] = int((teams >> team) & 1u);
            expected += scores.teamPlayers[team];
        }
        CHECK(CG_ScoreboardTotalLines(0) == expected);
    }
}
struct playerState_s {
#include "player_objective_array.inc"
};
struct gclient_s {
    playerState_s ps;
    struct { struct { int team; } cs; } sess;
};
struct gentity_s { struct { bool inuse; } r; gclient_s *client; };
struct {
    int maxclients;
    gentity_s *gentities;
#include "level_objective_array.inc"
} level{};
static_assert(std::extent_v<decltype(playerState_s::objective)> == 16);
static_assert(std::extent_v<decltype(level.objectives)> == 16);
void MyAssertHandler(const char *, int, int, const char *, ...) { CHECK(false); }
#include "objective_clients_body.inc"
void CheckObjectiveResults(const gclient_s (&clients)[5], const objective_t &untouched, int restriction)
{
    for (int client = 0; client < 5; ++client) {
        for (int slot = 0; slot < 16; ++slot) {
            objective_t expected = untouched;
            if (client < 4) {
                expected.state = OBJST_EMPTY;
                if (slot % 8 != 0 && (restriction == 0 || restriction == client))
                    expected = level.objectives[slot];
            }
            CHECK(std::memcmp(&clients[client].ps.objective[slot], &expected, sizeof(expected)) == 0);
        }
    }
}
void CheckObjectiveVisibility()
{
    gclient_s clients[5]{};
    gentity_s entities[5]{};
    for (int client = 0; client < 5; ++client) {
        clients[client].sess.cs.team = client;
        entities[client].client = &clients[client];
        entities[client].r.inuse = client < 4;
    }
    level.maxclients = 5;
    level.gentities = entities;
    objective_t untouched{};
    untouched.state = OBJST_CURRENT;
    untouched.icon = 99;
    for (int restriction = 0; restriction < 4; ++restriction) {
        for (int slot = 0; slot < 16; ++slot) {
            auto &obj = level.objectives[slot];
            obj.state = static_cast<objectiveState_t>(slot % 8);
            obj.teamNum = restriction;
            obj.entNum = slot;
            obj.icon = 100 + slot;
            for (int component = 0; component < 3; ++component) obj.origin[component] = float(slot + component);
        }
        for (auto &client : clients) for (auto &obj : client.ps.objective) obj = untouched;
        G_UpdateObjectiveToClients();
        CheckObjectiveResults(clients, untouched, restriction);
    }
    level.gentities = nullptr;
}
} // namespace mp
} // namespace
void RunTeamObjectiveContracts()
{
    mp::CheckScoreboardLines();
    mp::CheckObjectiveVisibility();
}

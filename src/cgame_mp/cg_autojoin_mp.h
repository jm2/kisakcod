#pragma once

#include <cstdint>

// #817: a client that connects from the command line has no UI, so nothing
// ever answers the script menus that drive team and class selection. The game
// script then never sets sessionstate "playing", never calls ClientSpawn, and
// the client stays a spectator for the whole session.
//
// These rules pick the response the client sends for an opened script menu,
// using the same "mr" the menu system sends (Script_ScriptMenuResponse). They
// live in their own translation unit so the table is testable without a
// client, a server or retail data.
struct cgAutoJoinRules_t
{
    const char *teamMenuPrefix;   // e.g. "team": matches "team_marinesopfor"
    const char *teamResponse;     // e.g. "autoassign"
    const char *classMenuPrefix;  // e.g. "changeclass": matches "changeclass_marines"
    const char *classResponse;    // e.g. "assault_mp,0"
};

// One answer per menu slot per server id, so a wrong response cannot loop.
// The state is per connection: CG_Init clears it, and a changed server id
// (map restart) clears it again.
struct cgAutoJoinAnswerState_t
{
    int32_t serverId;
    uint8_t answeredMenus[32];
};

enum cgAutoJoinKind_t
{
    CG_AUTOJOIN_NONE,
    CG_AUTOJOIN_TEAM,
    CG_AUTOJOIN_CLASS,
};

// Returns which join step this script menu belongs to and, through `response`,
// the answer to send. Prefixes match case-insensitively; an empty prefix or an
// empty response disables that step.
cgAutoJoinKind_t CG_AutoJoinResponse(const cgAutoJoinRules_t &rules, const char *menuName,
                                     const char **response);

// Clears every answered slot, so a new connection answers its join menus.
void CG_AutoJoinResetAnswers(cgAutoJoinAnswerState_t *state);

// Claims `menuIndex` for `serverId`. Returns false when it was already
// answered, which is what keeps a response from being sent twice.
bool CG_AutoJoinClaimAnswer(cgAutoJoinAnswerState_t *state, int32_t serverId, uint32_t menuIndex);

// Called from CG_Init: a new connection must answer its join menus even when
// it reconnects to the same server id.
void CG_AutoJoinResetConnection();

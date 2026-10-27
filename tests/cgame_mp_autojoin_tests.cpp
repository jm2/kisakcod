// #817: the auto-join rules decide which script-menu response the client sends
// when it has no UI. The decision is a pure function, so every branch runs here
// without a client, a server or retail data.
#include <cgame_mp/cg_autojoin_mp.h>

#include <cstdio>
#include <cstring>

namespace
{
int failures;

void CheckResponse(const cgAutoJoinRules_t &rules, const char *menu, cgAutoJoinKind_t expectedKind,
                   const char *expected, const char *what)
{
    const char *response = nullptr;
    const cgAutoJoinKind_t kind = CG_AutoJoinResponse(rules, menu, &response);
    const bool ok = kind == expectedKind
        && (expected ? (response && std::strcmp(response, expected) == 0) : response == nullptr);
    if (!ok)
    {
        std::printf("FAIL: %s (menu \"%s\": expected kind %i \"%s\", got kind %i \"%s\")\n",
                    what, menu ? menu : "(null)", static_cast<int>(expectedKind),
                    expected ? expected : "(none)", static_cast<int>(kind),
                    response ? response : "(none)");
        ++failures;
    }
}
} // namespace

int main()
{
    const cgAutoJoinRules_t defaults = {"team", "autoassign", "changeclass", "assault_mp,0"};

    CheckResponse(defaults, "team_marinesopfor", CG_AUTOJOIN_TEAM, "autoassign",
                  "team menu takes the team response");
    CheckResponse(defaults, "TEAM_MARINESOPFOR", CG_AUTOJOIN_TEAM, "autoassign",
                  "menu match is case-insensitive");
    CheckResponse(defaults, "changeclass_marines", CG_AUTOJOIN_CLASS, "assault_mp,0",
                  "per-team class menu takes the class response");
    CheckResponse(defaults, "changeclass_opfor", CG_AUTOJOIN_CLASS, "assault_mp,0",
                  "class prefix accepts either team suffix");
    CheckResponse(defaults, "changeclass", CG_AUTOJOIN_CLASS, "assault_mp,0",
                  "the flat class menu is the one the client re-targets for spawning");
    CheckResponse(defaults, "changeweapon", CG_AUTOJOIN_NONE, nullptr,
                  "an unrelated script menu is not answered");
    CheckResponse(defaults, "changeteam", CG_AUTOJOIN_NONE, nullptr,
                  "a menu that merely contains 'team' is not answered");
    CheckResponse(defaults, "class_marines", CG_AUTOJOIN_NONE, nullptr,
                  "the listed-but-empty class menu is not answered");
    CheckResponse(defaults, "", CG_AUTOJOIN_NONE, nullptr, "an empty menu name is not answered");
    CheckResponse(defaults, nullptr, CG_AUTOJOIN_NONE, nullptr, "a null menu name is not answered");

    const cgAutoJoinRules_t noTeamPrefix = {"", "autoassign", "changeclass", "assault_mp,0"};
    CheckResponse(noTeamPrefix, "team_marines_mp", CG_AUTOJOIN_NONE, nullptr,
                  "an empty team prefix disables the team step");

    const cgAutoJoinRules_t emptyTeamResponse = {"team", "", "changeclass", "assault_mp,0"};
    CheckResponse(emptyTeamResponse, "team_marines_mp", CG_AUTOJOIN_NONE, nullptr,
                  "an empty team response disables the team step");

    const cgAutoJoinRules_t classOnly = {"spectate", "free", "changeclass", "assault_mp,0"};
    CheckResponse(classOnly, "team_marines_mp", CG_AUTOJOIN_NONE, nullptr,
                  "a non-matching team prefix leaves the menu alone");
    CheckResponse(classOnly, "spectatemenu", CG_AUTOJOIN_TEAM, "free",
                  "the configured prefix is what matches");

    // The response is queued into the command buffer as text, so a value from a
    // server-set dvar must be one token or not be sent at all.
    if (!CG_AutoJoinResponseIsValid("assault_mp,0") || !CG_AutoJoinResponseIsValid("autoassign"))
    {
        std::printf("FAIL: a menu answer token is a valid response\n");
        ++failures;
    }
    if (CG_AutoJoinResponseIsValid("") || CG_AutoJoinResponseIsValid(nullptr))
    {
        std::printf("FAIL: an empty response is not valid\n");
        ++failures;
    }
    const char *hostile[] = {"assault_mp,0; quit", "a b", "\"q\"", "a\\b", "a/b", "a\nb",
                             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for (const char *value : hostile)
    {
        if (CG_AutoJoinResponseIsValid(value))
        {
            std::printf("FAIL: \"%s\" must not be accepted as a response\n", value);
            ++failures;
        }
    }
    const cgAutoJoinRules_t hostileClass = {"team", "autoassign", "changeclass", "assault_mp,0; quit"};
    CheckResponse(hostileClass, "changeclass", CG_AUTOJOIN_NONE, nullptr,
                  "a response carrying a command separator disables the step");

    cgAutoJoinAnswerState_t answers;
    CG_AutoJoinResetAnswers(&answers);
    if (!CG_AutoJoinClaimAnswer(&answers, 7, 8))
    {
        std::printf("FAIL: the first answer to a slot is claimed\n");
        ++failures;
    }
    if (CG_AutoJoinClaimAnswer(&answers, 7, 8))
    {
        std::printf("FAIL: the same slot is not answered twice for one server id\n");
        ++failures;
    }
    if (!CG_AutoJoinClaimAnswer(&answers, 7, 15))
    {
        std::printf("FAIL: a different slot is still answered\n");
        ++failures;
    }
    if (!CG_AutoJoinClaimAnswer(&answers, 8, 8))
    {
        std::printf("FAIL: a new server id clears the answered slots\n");
        ++failures;
    }
    CG_AutoJoinResetAnswers(&answers);
    if (!CG_AutoJoinClaimAnswer(&answers, 8, 8))
    {
        std::printf("FAIL: a new connection answers the same slot again\n");
        ++failures;
    }
    if (CG_AutoJoinClaimAnswer(&answers, 8, 32))
    {
        std::printf("FAIL: an out-of-range menu slot is never claimed\n");
        ++failures;
    }

    if (failures)
    {
        std::printf("cg autojoin: %i checks failed\n", failures);
        return 1;
    }
    std::printf("cg autojoin: all checks passed\n");
    return 0;
}

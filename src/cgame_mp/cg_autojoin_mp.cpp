#include "cg_autojoin_mp.h"

#include <cctype>
#include <cstring>

namespace
{
int AutoJoinToLower(int ch)
{
    return std::tolower(static_cast<unsigned char>(ch));
}

bool AutoJoinPrefixMatches(const char *prefix, const char *menuName)
{
    if (!prefix || !*prefix || !menuName)
        return false;
    for (size_t i = 0; prefix[i]; ++i)
    {
        if (!menuName[i] || AutoJoinToLower(menuName[i]) != AutoJoinToLower(prefix[i]))
            return false;
    }
    return true;
}
} // namespace

cgAutoJoinKind_t CG_AutoJoinResponse(const cgAutoJoinRules_t &rules, const char *menuName,
                                     const char **response)
{
    if (response)
        *response = nullptr;
    if (!menuName || !*menuName)
        return CG_AUTOJOIN_NONE;
    if (AutoJoinPrefixMatches(rules.teamMenuPrefix, menuName)
        && rules.teamResponse && *rules.teamResponse)
    {
        if (response)
            *response = rules.teamResponse;
        return CG_AUTOJOIN_TEAM;
    }
    if (AutoJoinPrefixMatches(rules.classMenuPrefix, menuName)
        && rules.classResponse && *rules.classResponse)
    {
        if (response)
            *response = rules.classResponse;
        return CG_AUTOJOIN_CLASS;
    }
    return CG_AUTOJOIN_NONE;
}

void CG_AutoJoinResetAnswers(cgAutoJoinAnswerState_t *state)
{
    if (!state)
        return;
    state->serverId = 0;
    memset(state->answeredMenus, 0, sizeof(state->answeredMenus));
}

bool CG_AutoJoinClaimAnswer(cgAutoJoinAnswerState_t *state, int32_t serverId, uint32_t menuIndex)
{
    if (!state || menuIndex >= sizeof(state->answeredMenus))
        return false;
    if (serverId != state->serverId)
    {
        state->serverId = serverId;
        memset(state->answeredMenus, 0, sizeof(state->answeredMenus));
    }
    if (state->answeredMenus[menuIndex])
        return false;
    state->answeredMenus[menuIndex] = 1;
    return true;
}

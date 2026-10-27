#include "cg_autojoin_mp.h"

#include <cctype>
#include <cstddef>

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
    for (std::size_t i = 0; prefix[i]; ++i)
    {
        if (!menuName[i] || AutoJoinToLower(menuName[i]) != AutoJoinToLower(prefix[i]))
            return false;
    }
    return true;
}
bool AutoJoinResponseCharIsValid(int ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')
           || ch == '_' || ch == '.' || ch == ',' || ch == '-';
}
} // namespace

bool CG_AutoJoinResponseIsValid(const char *response)
{
    if (!response || !*response)
        return false;
    std::size_t length = 0;
    for (; response[length]; ++length)
    {
        if (length >= 63 || !AutoJoinResponseCharIsValid(static_cast<unsigned char>(response[length])))
            return false;
    }
    return true;
}

cgAutoJoinKind_t CG_AutoJoinResponse(const cgAutoJoinRules_t &rules, const char *menuName,
                                     const char **response)
{
    if (response)
        *response = nullptr;
    if (!menuName || !*menuName)
        return CG_AUTOJOIN_NONE;
    if (AutoJoinPrefixMatches(rules.teamMenuPrefix, menuName)
        && CG_AutoJoinResponseIsValid(rules.teamResponse))
    {
        if (response)
            *response = rules.teamResponse;
        return CG_AUTOJOIN_TEAM;
    }
    if (AutoJoinPrefixMatches(rules.classMenuPrefix, menuName)
        && CG_AutoJoinResponseIsValid(rules.classResponse))
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
    for (uint8_t &answered : state->answeredMenus)
        answered = 0;
}

bool CG_AutoJoinClaimAnswer(cgAutoJoinAnswerState_t *state, int32_t serverId, uint32_t menuIndex)
{
    if (!state || menuIndex >= sizeof(state->answeredMenus))
        return false;
    if (serverId != state->serverId)
    {
        CG_AutoJoinResetAnswers(state);
        state->serverId = serverId;
    }
    if (state->answeredMenus[menuIndex])
        return false;
    state->answeredMenus[menuIndex] = 1;
    return true;
}

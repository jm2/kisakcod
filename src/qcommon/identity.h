#pragma once

#include <cstdint>
#include <cstring>
#include <limits>

namespace kisak::identity
{
inline bool IsHexGuid(const char *identity)
{
    if (!identity || std::strlen(identity) != 32)
        return false;

    for (const char *ch = identity; *ch; ++ch)
    {
        if ((*ch < '0' || *ch > '9')
            && (*ch < 'a' || *ch > 'f')
            && (*ch < 'A' || *ch > 'F'))
        {
            return false;
        }
    }
    return true;
}

inline bool ParseSteamId(const char *identity, std::uint64_t *steamId)
{
    if (!identity || !*identity || !steamId)
        return false;

    std::uint64_t value = 0;
    int digits = 0;
    for (const char *ch = identity; *ch; ++ch)
    {
        if (*ch < '0' || *ch > '9' || ++digits > 20)
            return false;

        const std::uint64_t digit = static_cast<std::uint64_t>(*ch - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }

    if (!value)
        return false;
    *steamId = value;
    return true;
}
}

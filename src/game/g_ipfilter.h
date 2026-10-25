#pragma once

// The IP ban filters behind addip, removeip and g_banIPs: parsing a filter
// string and matching an IPv4 address against one. Header-only and
// engine-free, so the tests drive it directly.

#include <cstdint>
#include <cstring>

namespace g_ipfilter
{
// Parses "a.b.c.d" into a mask and compare value. Octets are separated by any
// single non-digit; a string may stop after fewer than four octets. A zero
// octet, or one left out, matches anything. Each octet is 1-3 digits and at
// most 255; anything else fails and leaves the outputs untouched.
inline bool Parse(const char *s, uint32_t *mask, uint32_t *compare)
{
    if (!s || !mask || !compare)
        return false;
    uint8_t m[4] = {};
    uint8_t b[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        if (*s < '0' || *s > '9')
            return false;
        uint32_t value = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9')
        {
            if (++digits > 3)
                return false;
            value = value * 10 + static_cast<uint32_t>(*s++ - '0');
        }
        if (value > 255)
            return false;
        b[i] = static_cast<uint8_t>(value);
        if (b[i])
            m[i] = 0xFF;
        if (!*s)
            break;
        ++s;
    }
    std::memcpy(mask, m, sizeof(*mask));
    std::memcpy(compare, b, sizeof(*compare));
    return true;
}

// True when the four address bytes fall inside the filter.
inline bool Matches(uint32_t mask, uint32_t compare, const uint8_t ip[4])
{
    uint32_t address = 0;
    std::memcpy(&address, ip, sizeof(address));
    return (address & mask) == compare;
}
} // namespace g_ipfilter

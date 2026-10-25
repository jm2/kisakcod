#pragma once

// The IP ban filters behind addip, removeip and g_banIPs: parsing a filter
// string and matching an IPv4 address against one. Header-only and
// engine-free, so the tests drive it directly.

#include <cstdint>

namespace g_ipfilter
{
// The four bytes as a uint32_t in memory order, as the engine stores filters.
inline uint32_t PackBytes(const uint8_t bytes[4])
{
    uint32_t packed = 0;
    uint8_t *const out = reinterpret_cast<uint8_t *>(&packed);
    for (int i = 0; i < 4; ++i)
        out[i] = bytes[i];
    return packed;
}

// Reads one octet of 1-3 digits, at most 255, and advances s past it.
inline bool ParseOctet(const char **s, uint8_t *octet)
{
    uint32_t value = 0;
    int digits = 0;
    while (**s >= '0' && **s <= '9')
    {
        if (++digits > 3)
            return false;
        value = value * 10 + static_cast<uint32_t>(*(*s)++ - '0');
    }
    if (!digits || value > 255)
        return false;
    *octet = static_cast<uint8_t>(value);
    return true;
}

// Parses "a.b.c.d" into a mask and compare value. Octets are separated by any
// single non-digit; a string may stop after fewer than four octets. A zero
// octet, or one left out, matches anything. Anything else fails and leaves
// the outputs untouched.
inline bool Parse(const char *s, uint32_t *mask, uint32_t *compare)
{
    if (!s || !mask || !compare)
        return false;
    uint8_t m[4] = {};
    uint8_t b[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        if (!ParseOctet(&s, &b[i]))
            return false;
        m[i] = b[i] ? 0xFF : 0;
        if (!*s)
            break;
        ++s;
    }
    *mask = PackBytes(m);
    *compare = PackBytes(b);
    return true;
}

// True when the four address bytes fall inside the filter.
inline bool Matches(uint32_t mask, uint32_t compare, const uint8_t ip[4])
{
    return (PackBytes(ip) & mask) == compare;
}
} // namespace g_ipfilter

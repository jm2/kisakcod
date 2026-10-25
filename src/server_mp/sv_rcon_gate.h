#pragma once

// The password check and the rate limits SVC_RemoteCommand applies before it
// looks at an rcon packet. Header-only and engine-free, so the tests drive it
// with their own clock.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sv_rcon
{
// The identity a throttle slot is keyed on: the address type and IPv4 bytes.
// The port is left out, so one host cannot get fresh slots by changing ports.
struct Source
{
    uint32_t type = 0;
    uint8_t ip[4] = {};

    bool operator==(const Source &other) const
    {
        return type == other.type && std::memcmp(ip, other.ip, sizeof(ip)) == 0;
    }
};

// Compares in time that depends only on the longer of the two lengths, never
// on where the first mismatch is.
inline bool PasswordMatches(const char *given, const char *expected)
{
    if (!given || !expected)
        return false;
    const size_t givenLen = std::strlen(given);
    const size_t expectedLen = std::strlen(expected);
    const size_t len = givenLen > expectedLen ? givenLen : expectedLen;
    uint32_t diff = givenLen != expectedLen ? 1u : 0u;
    for (size_t i = 0; i < len; ++i)
    {
        const uint8_t a = i < givenLen ? static_cast<uint8_t>(given[i]) : 0u;
        const uint8_t b = i < expectedLen ? static_cast<uint8_t>(expected[i]) : 0u;
        diff |= static_cast<uint32_t>(a ^ b);
    }
    return diff == 0;
}

// One attempt per source every kSourceIntervalMsec, and at most
// kGlobalAttempts attempts per kGlobalWindowMsec from all sources together. A
// source that floods only throttles itself until it fills the global budget.
class Gate
{
public:
    static constexpr uint32_t kSourceIntervalMsec = 500;
    static constexpr uint32_t kGlobalWindowMsec = 1000;
    static constexpr uint32_t kGlobalAttempts = 10;
    static constexpr size_t kTrackedSources = 64;

    // True when an attempt from source at time nowMsec may be processed; an
    // allowed attempt is charged to both limits. nowMsec may wrap.
    bool Allow(const Source &source, int32_t nowMsec)
    {
        const uint32_t now = static_cast<uint32_t>(nowMsec);
        if (!windowStarted_ || now - windowStart_ >= kGlobalWindowMsec)
        {
            windowStarted_ = true;
            windowStart_ = now;
            windowAttempts_ = 0;
        }

        Slot *slot = nullptr;
        Slot *oldest = nullptr;
        for (Slot &candidate : slots_)
        {
            if (candidate.used && candidate.source == source)
            {
                slot = &candidate;
                break;
            }
            if (!oldest || !candidate.used
                || (oldest->used && now - candidate.last > now - oldest->last))
            {
                oldest = &candidate;
            }
        }
        if (slot && now - slot->last < kSourceIntervalMsec)
            return false;
        if (windowAttempts_ >= kGlobalAttempts)
            return false;

        if (!slot)
        {
            slot = oldest;
            slot->used = true;
            slot->source = source;
        }
        slot->last = now;
        ++windowAttempts_;
        return true;
    }

private:
    struct Slot
    {
        Source source;
        uint32_t last = 0;
        bool used = false;
    };

    Slot slots_[kTrackedSources];
    uint32_t windowStart_ = 0;
    uint32_t windowAttempts_ = 0;
    bool windowStarted_ = false;
};
} // namespace sv_rcon

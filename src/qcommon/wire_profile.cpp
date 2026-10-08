#include "wire_profile.h"

#include "qcommon.h"

namespace
{
const char *kWireProfileNames[] = {"steam18", "fork", nullptr};

struct WireIdentity
{
    int protocol;
    const char *gameName;
    const char *shortVersion;
};

const WireIdentity kWireIdentities[] = {
    // steam18: protocol, gamename and shortversion are community-sourced.
    {7, "Call of Duty 4", "1.8"}, // confirm by capture (NET_STEAM18 §5)
    // fork: what the fork advertised before profiles existed.
    {1, "KisakCoD4", "1.0"},
};

const WireIdentity &Identity()
{
    return kWireIdentities[Com_GetWireProfile()];
}
} // namespace

// The first caller registers it, after the command line's "+set" has run, and
// later calls reuse it: re-registering would apply a latched value mid-game.
const dvar_s *Com_RegisterWireProfile()
{
    static const dvar_s *net_wireProfile;
    if (!net_wireProfile)
    {
        net_wireProfile = Dvar_RegisterEnum(
            "net_wireProfile",
            kWireProfileNames,
            WIRE_PROFILE_STEAM18,
            DVAR_LATCH,
            "Wire identity: steam18 (retail Steam 1.8) or fork");
    }
    return net_wireProfile;
}

WireProfile Com_GetWireProfile()
{
    const int profile = Com_RegisterWireProfile()->current.integer;
    return profile == WIRE_PROFILE_FORK ? WIRE_PROFILE_FORK : WIRE_PROFILE_STEAM18;
}

int Com_WireProtocol()
{
    return Identity().protocol;
}

const char *Com_WireGameName()
{
    return Identity().gameName;
}

const char *Com_WireShortVersion()
{
    return Identity().shortVersion;
}

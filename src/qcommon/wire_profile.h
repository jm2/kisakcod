#pragma once

// The wire identity this instance speaks (NET_STEAM18.md §7). One latched
// dvar, net_wireProfile, picks it at startup; protocol, gamename and
// shortversion all follow it.

struct dvar_s;

enum WireProfile : int
{
    WIRE_PROFILE_STEAM18 = 0, // retail Steam 1.8.13620, the default
    WIRE_PROFILE_FORK = 1,    // the fork's own identity; opt-in
};

const dvar_s *Com_RegisterWireProfile();
WireProfile Com_GetWireProfile();

int Com_WireProtocol();
const char *Com_WireGameName();
const char *Com_WireShortVersion();

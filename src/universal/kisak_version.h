#pragma once

// The project's own version, shown to players: the version dvar, the startup
// banner, the console prompt and the UI build corner. The wire identity
// (shortversion, protocol, gamename) is separate and follows net_wireProfile
// (qcommon/wire_profile.cpp), so retail clients still match it.
// A release commit carries the release's version (0.1.0); the commit after
// the release moves master to the next -dev version.
#define KISAK_VERSION "0.1.0"

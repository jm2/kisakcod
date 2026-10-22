#pragma once

// The project's own version, shown to players: the version dvar, the startup
// banner, the console prompt and the UI build corner. The wire identity
// (shortversion, protocol, gamename) is separate and follows net_wireProfile
// (qcommon/wire_profile.cpp), so retail clients still match it.
// The first packaged release is 0.1.0; builds before it are 0.1.0-dev.
#define KISAK_VERSION "0.1.0-dev"

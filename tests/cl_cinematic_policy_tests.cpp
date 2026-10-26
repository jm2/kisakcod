// cl_cinematic_policy_tests.cpp: the decision CIN_PlayCinematic asks before a
// cinematic takes a multiplayer client's connection state
// (client/cl_cinematic_policy.h). A client that is not connecting plays its
// intro; from the first connecting state on, a cinematic is skipped so the
// server's reliable stream keeps a state it can be parsed from (issue #818).
// Single player never skips.

#include <client/cl_cinematic_policy.h>

#include <cstdint>
#include <cstdio>
#include <initializer_list>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

// The multiplayer connstate_t (client_mp/client_mp.h).
enum : int
{
    kDisconnected = 0,
    kCinematic = 1,
    kLogo = 2,
    kConnecting = 3,
    kChallenging = 4,
    kConnected = 5,
    kSendingStats = 6,
    kLoading = 7,
    kPrimed = 8,
    kActive = 9,
};
} // namespace

int main()
{
    using cl_cinematic::SkipsForConnect;

    // The menu intro: nothing is connecting, so the intro plays and may take
    // CA_CINEMATIC. This is the playback issue #818 must not regress.
    for (int state : {kDisconnected, kCinematic, kLogo})
        CHECK(!SkipsForConnect(state, kConnecting));

    // A startup +connect runs before the queued intro, and the intro must not
    // overwrite the connection state CL_PacketEvent gates on.
    for (int state = kConnecting; state <= kActive; ++state)
        CHECK(SkipsForConnect(state, kConnecting));

    // The startup order itself: +connect sets CA_CONNECTING, then the queued
    // "cinematic IW_logo" must be skipped and leave CA_CONNECTING behind.
    int state = kDisconnected;
    CHECK(!SkipsForConnect(state, kConnecting));
    state = kConnecting; // CL_Connect_f, from the startup command
    CHECK(SkipsForConnect(state, kConnecting));
    if (!SkipsForConnect(state, kConnecting))
        state = kCinematic;
    CHECK(state == kConnecting);

    // Single player passes no reachable connecting state.
    CHECK(!SkipsForConnect(kActive, INT32_MAX));
    for (int singlePlayerState = kDisconnected; singlePlayerState <= kActive; ++singlePlayerState)
        CHECK(!SkipsForConnect(singlePlayerState, INT32_MAX));

    if (g_failures == 0)
        std::printf("cl cinematic policy: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

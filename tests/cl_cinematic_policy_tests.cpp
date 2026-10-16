// cl_cinematic_policy_tests.cpp: the decision CIN_PlayCinematic asks before a
// cinematic takes a multiplayer client's connection state
// (client/cl_cinematic_policy.h). A build without Bink skips a cinematic from
// the first connecting state on; a build with Bink never does. This build's
// answer follows its width: no 64-bit Bink exists.

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
    kActive = 9,
};
} // namespace

int main()
{
    using cl_cinematic::SkipsForConnect;

    // Without Bink: the menu, a cinematic and a logo play it; a connect does not.
    for (int state : {kDisconnected, kCinematic, kLogo})
        CHECK(!SkipsForConnect(state, kConnecting, false));
    for (int state = kConnecting; state <= kActive; ++state)
        CHECK(SkipsForConnect(state, kConnecting, false));

    // With Bink (Win32), retail order is kept in every state.
    for (int state = kDisconnected; state <= kActive; ++state)
        CHECK(!SkipsForConnect(state, kConnecting, true));

    // Single player passes no reachable connecting state.
    CHECK(!SkipsForConnect(kActive, INT32_MAX, false));

    // This build: only 32-bit clients play Bink.
    CHECK(cl_cinematic::kBuildPlaysBink == (sizeof(void *) == 4));

    if (g_failures == 0)
        std::printf("cl cinematic policy: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

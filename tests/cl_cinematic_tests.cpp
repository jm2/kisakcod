// cl_cinematic_tests.cpp: the "cinematic" command (client/cl_cin.cpp) in a
// multiplayer client. A cinematic takes the client's connection state
// (CA_CINEMATIC). Com_Init queues the intro chain (COM_PlayIntroMovies) and
// then runs a startup +devmap at once, so the intro used to start mid-connect
// and the client ignored the server's connectResponse. In a build without
// Bink, which this 64-bit test is (cl_cinematic_policy.h), a cinematic must
// now leave a connecting or connected client alone, and still play otherwise.
//
// The engine boundary is weak: --gc-sections drops what the checks never
// reach.

#include <cstdio>
#include <cstdlib>

#include <client/client.h>
#include <client_mp/client_mp.h>
#include <qcommon/cmd.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

int g_playbacks = 0; // R_Cinematic_StartPlayback calls
dvar_s g_volumeScale{};
char g_name[] = "IW_logo";

int Play(connstate_t state)
{
    clientUIActives[0].connectionState = state;
    g_playbacks = 0;
    CL_PlayCinematic_f();
    return g_playbacks;
}
} // namespace

// The globals the command reads, owned here.
clientUIActive_t clientUIActives[1];
clientStatic_t cls;
const dvar_s *snd_cinematicVolumeScale = &g_volumeScale;

// Engine boundary.
WEAK const char *__cdecl Cmd_Argv(int argIndex) { return argIndex == 1 ? g_name : ""; }
WEAK double __cdecl SND_GetVolumeNormalized() { return 1.0; }
WEAK void R_Cinematic_StartPlayback(char *, uint32_t, float) { ++g_playbacks; }
WEAK int __cdecl UI_SetActiveMenu(int, uiMenuCommand_t) { return 0; }
WEAK void Con_Close(int) {}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::exit(1);
}

int main()
{
    // Disconnected (the main menu) or after a logo: the intro plays and the
    // client enters CA_CINEMATIC.
    CHECK(Play(CA_DISCONNECTED) == 1 && clientUIActives[0].connectionState == CA_CINEMATIC);
    CHECK(Play(CA_LOGO) == 1 && clientUIActives[0].connectionState == CA_CINEMATIC);

    // Mid-connect or connected: the cinematic is skipped and the state kept,
    // so the loopback connectResponse still finds the client challenging.
    for (connstate_t state : {CA_CONNECTING, CA_CHALLENGING, CA_CONNECTED, CA_LOADING, CA_ACTIVE})
        CHECK(Play(state) == 0 && clientUIActives[0].connectionState == state);

    if (g_failures == 0)
        std::printf("cl cinematic: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

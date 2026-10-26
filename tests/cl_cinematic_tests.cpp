// cl_cinematic_tests.cpp: the "cinematic" command (client/cl_cin.cpp) in a
// multiplayer client. A cinematic takes the client's connection state
// (CA_CINEMATIC). Com_Init queues the intro chain (COM_PlayIntroMovies) and
// then runs a startup +connect or +devmap at once, so by the time the queued
// intro executes the client is already connecting; a cinematic there used to
// overwrite CA_CONNECTING and the client dropped the server's whole reliable
// stream (issue #818). The intro still plays from the menu, where no connect
// is pending.
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
int g_uiMenuCalls = 0;
int g_conCloseCalls = 0;
dvar_s g_volumeScale{};
char g_name[] = "IW_logo";

int Play(connstate_t state)
{
    clientUIActives[0].connectionState = state;
    g_playbacks = 0;
    g_uiMenuCalls = 0;
    g_conCloseCalls = 0;
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
WEAK int __cdecl UI_SetActiveMenu(int, uiMenuCommand_t) { ++g_uiMenuCalls; return 0; }
WEAK void Con_Close(int) { ++g_conCloseCalls; }
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::exit(1);
}

int main()
{
    cls.uiStarted = true;

    // Disconnected (the main menu) or after a logo: the intro plays and the
    // client enters CA_CINEMATIC. This is the normal playback to keep.
    CHECK(Play(CA_DISCONNECTED) == 1 && clientUIActives[0].connectionState == CA_CINEMATIC);
    CHECK(g_uiMenuCalls == 1 && g_conCloseCalls == 1);
    CHECK(Play(CA_LOGO) == 1 && clientUIActives[0].connectionState == CA_CINEMATIC);

    // Mid-connect or connected: the cinematic is skipped and nothing is torn
    // down, so CA_CONNECTING survives until the server's stream moves it.
    for (connstate_t state : {CA_CONNECTING, CA_CHALLENGING, CA_CONNECTED, CA_SENDINGSTATS, CA_LOADING, CA_PRIMED, CA_ACTIVE})
    {
        CHECK(Play(state) == 0);
        CHECK(clientUIActives[0].connectionState == state);
        CHECK(g_uiMenuCalls == 0 && g_conCloseCalls == 0);
    }

    if (g_failures == 0)
        std::printf("cl cinematic: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

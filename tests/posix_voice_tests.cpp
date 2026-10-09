// posix_voice_tests.cpp: the silent POSIX voice backend
// (_platform/posix/posix_voice.cpp), called through the qcommon/sys_local.h
// declarations the client compiles against. Each check is what a caller
// relies on to run with voice chat off: incoming voice never marks a client
// as talking, recording never starts, and there is no local level or data.

#include <cstdio>

#include <qcommon/sys_local.h>

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))
} // namespace

int main()
{
    Voice_Init();

    unsigned __int8 packet[32] = {};
    for (uint32_t client = 0; client < 64; ++client)
    {
        Voice_IncomingVoiceData(static_cast<unsigned __int8>(client), packet, sizeof(packet));
        CHECK(!Voice_IsClientTalking(client));
    }
    Voice_Playback();

    CHECK(Voice_StartRecording() == 0);
    CHECK(Voice_GetLocalVoiceData() == 0);
    CHECK(!Voice_SendVoiceData());
    CHECK(Voice_GetVoiceLevel() == 0.0);
    CHECK(Voice_StopRecording() == 0);

    Voice_Shutdown();

    if (g_failures == 0)
        std::printf("posix voice: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}

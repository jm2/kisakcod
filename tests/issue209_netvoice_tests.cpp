// Issue #209: production qcommon/server_mp bounds and eviction contracts.
//
// FakeLag_GetFreeSlot used to search only outbound packets for the oldest
// slot. With all 512 slots holding inbound packets the search never moved
// off slot 0, so the eviction discarded an arbitrary packet instead of the
// oldest queued one. These checks fill the real laggedPackets array and call
// the production FakeLag_GetFreeSlot.
//
// SV_ClientHasClientMuted and SV_ClientWantsVoiceData used to rely on
// MyAssertHandler, which is inert in release builds, before indexing
// svs.clients. The invalid-index checks below exercise the production
// functions and fail closed instead of reading out of bounds.
//
// The production TUs are linked with --gc-sections; only the functions and
// stubs these checks reach survive the link.

#include <qcommon/net_chan_mp.h>
#include <server_mp/server_mp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

serverStatic_t svs;
extern fakedLatencyPackets_t laggedPackets[512];

void __cdecl Z_VirtualFree(void *) {}
void Com_Printf(int, const char *, ...) {}
void MyAssertHandler(const char *, int, int, const char *, ...) {}
std::uint32_t KISAK_CDECL Sys_Milliseconds() { return 1000u; }
void KISAK_CDECL Sys_Sleep(std::uint32_t) {}
char Sys_SendPacket(int, unsigned char *, netadr_t) { return 0; }

namespace
{
int g_failures = 0;

#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

void ResetPackets()
{
    std::memset(laggedPackets, 0, sizeof(laggedPackets));
}

void FillInboundPackets(int oldestSlot)
{
    for (int i = 0; i < 512; ++i)
    {
        laggedPackets[i].outbound = false;
        laggedPackets[i].length = 1;
        laggedPackets[i].startTime = 5000 + i;
    }
    laggedPackets[oldestSlot].startTime = 500;
}

void CheckAllInboundEviction()
{
    ResetPackets();
    FillInboundPackets(317);

    const uint32_t slot = FakeLag_GetFreeSlot();

    CHECK(slot == 317u);
    CHECK(laggedPackets[317].length == 0u);
    CHECK(laggedPackets[0].length == 1u);
}

void CheckInboundOldestBeatsNewerOutbound()
{
    ResetPackets();
    FillInboundPackets(211);
    laggedPackets[42].outbound = true;
    laggedPackets[42].startTime = 4000;

    const uint32_t slot = FakeLag_GetFreeSlot();

    CHECK(slot == 211u);
    CHECK(laggedPackets[211].length == 0u);
    CHECK(laggedPackets[42].length == 1u);
}

void CheckVoiceBounds()
{
    svs.clients[3].muteList[5] = true;
    svs.clients[3].sendVoice = true;

    CHECK(SV_ClientHasClientMuted(3, 5));
    CHECK(!SV_ClientHasClientMuted(3, 4));
    CHECK(SV_ClientWantsVoiceData(3));
    CHECK(!SV_ClientWantsVoiceData(4));

    CHECK(SV_ClientHasClientMuted(64u, 0u));
    CHECK(SV_ClientHasClientMuted(0u, 64u));
    CHECK(!SV_ClientWantsVoiceData(64u));

    CHECK(SV_ClientHasClientMuted(0xFFFFFFFFu, 0u));
    CHECK(SV_ClientHasClientMuted(0u, 0xFFFFFFFFu));
    CHECK(!SV_ClientWantsVoiceData(0xFFFFFFFFu));
}
} // namespace

int main()
{
    CheckAllInboundEviction();
    CheckInboundOldestBeatsNewerOutbound();
    CheckVoiceBounds();

    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("issue209 fakelag eviction and voice bounds: ok\n");
    return 0;
}

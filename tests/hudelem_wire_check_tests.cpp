// hudelem_wire_check_tests.cpp: a hudelem alignment from the wire (#187). The
// production MSG_WriteDeltaPlayerstate (qcommon/sv_msg_write_mp.cpp) sends a
// playerstate, and the production MSG_ReadDeltaPlayerstate (qcommon/msg_mp.cpp)
// reads it back: an alignment of 3, which would index past the three-entry
// scale table in AlignHudElemX/Y, overflows the message in Release builds.
//
// The rest of the engine boundary is weak, and --gc-sections drops the code
// no check reaches.
#include <cstdio>
#include <cstring>
#include <memory>

#include <bgame/bg_public.h>
#include <bgame/bg_hudelem.h>
#include <game_mp/g_public_mp.h>
#include <server_mp/server_mp.h>
#include <qcommon/msg_mp.h>
#include <qcommon/qcommon.h>
#include <qcommon/sv_msg_write_mp.h>
#include <qcommon/sys_time.h>
#include <universal/com_math.h>

#define WEAK __attribute__((weak))

namespace
{
int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

dvar_s g_off{};
} // namespace

serverStatic_t svs;
serverStaticHeader_t svsHeader;
const dvar_t *msg_hudelemspew = &g_off;
const dvar_t *sv_debugPacketContents = &g_off;
const dvar_t *sv_debugPlayerstate = &g_off;
WEAK std::uint32_t KISAK_CDECL Sys_Milliseconds() { return 0; }
WEAK void *__cdecl Sys_GetValue(int) { return nullptr; }
WEAK float Vec3LengthSq(const float *v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; }
WEAK void Vec3Sub(const float *a, const float *b, float *out)
{
    for (int i = 0; i < 3; ++i)
        out[i] = a[i] - b[i];
}
WEAK void MyAssertHandler(const char *, int, int, const char *, ...) {}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK void __cdecl Com_Error(errorParm_t, const char *, ...) {}

namespace
{
// Sends a playerstate whose first current hudelem has the given alignment and
// reads it back; true when the reader rejects it.
bool Rejected(int alignOrg)
{
    auto from = std::make_unique<playerState_s>();
    auto to = std::make_unique<playerState_s>();
    auto read = std::make_unique<playerState_s>();
    std::memset(from.get(), 0, sizeof(playerState_s));
    std::memset(to.get(), 0, sizeof(playerState_s));
    to->hud.current[0].type = HE_TYPE_TEXT;
    to->hud.current[0].alignOrg = alignOrg;

    static uint8_t buffer[0x20000];
    msg_t msg{};
    MSG_Init(&msg, buffer, sizeof(buffer));
    static clientHeader_t header{};
    SnapshotInfo_s info{};
    info.client = &header;
    MSG_WriteDeltaPlayerstate(&info, &msg, 0, from.get(), to.get());
    CHECK(!msg.overflowed);

    msg_t in{};
    MSG_Init(&in, buffer, sizeof(buffer));
    in.cursize = msg.cursize;
    MSG_ReadDeltaPlayerstate(0, &in, 0, from.get(), read.get(), false);
    if (!in.overflowed)
        CHECK(read->hud.current[0].alignOrg == alignOrg);
    return in.overflowed != 0;
}
} // namespace

int main()
{
    for (int alignX = 0; alignX < 4; ++alignX)
    {
        for (int alignY = 0; alignY < 4; ++alignY)
            CHECK(Rejected(alignX << 2 | alignY) == (alignX == 3 || alignY == 3));
    }
    if (g_failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("hudelem wire check: ok");
    return 0;
}

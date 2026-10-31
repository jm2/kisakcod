// msg_snapshot_delta_tests.cpp: the MP snapshot entity delta codec, production
// both ways (qcommon/sv_msg_write_mp.cpp writes, qcommon/msg_mp.cpp reads), as
// the headless server compiles it. A delta written for each entity type must
// decode to the same entityState_s; a removal, an unchanged entity and a
// hostile lastChanged index past the field list must decode as the client
// expects. The client-state, archived-entity and playerstate deltas round-trip
// the same way. Every byte-truncated copy of a written delta, held in a heap
// block of exactly its size so ASan sees a read past the end, must overflow
// the message. These readers parse every snapshot an (untrusted) server sends.

#include <qcommon/msg_mp.h>
#include <qcommon/sv_msg_write_mp.h>
#include <qcommon/ent.h>

#include <bgame/bg_public.h>
#include <bgame/bg_hudelem.h>
#include <game_mp/g_public_mp.h>
#include <server_mp/server_mp.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_time.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

#define WEAK __attribute__((weak))

// The engine boundary, as the hudelem wire test (#187) draws it: debug dvars
// off, prints silent. --gc-sections drops what no check reaches.
namespace
{
dvar_s g_off{};
}
serverStatic_t svs;
serverStaticHeader_t svsHeader;
const dvar_t *msg_dumpEnts = &g_off;
const dvar_t *msg_printEntityNums = &g_off;
const dvar_t *sv_debugPacketContents = &g_off;
const dvar_t *msg_hudelemspew = &g_off;
const dvar_t *sv_debugPlayerstate = &g_off;
WEAK std::uint32_t KISAK_CDECL Sys_Milliseconds() { return 0; }
WEAK float Vec3LengthSq(const float *v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; }
WEAK void Vec3Sub(const float *a, const float *b, float *out)
{
    for (int i = 0; i < 3; ++i)
        out[i] = a[i] - b[i];
}
WEAK void Com_DPrintf(int, const char *, ...) {}
WEAK void Com_PrintWarning(int, const char *, ...) {}
WEAK void *__cdecl Sys_GetValue(int) { return nullptr; }
WEAK char *__cdecl BG_GetEntityTypeName(int32_t)
{
    static char name[] = "";
    return name;
}
WEAK void MyAssertHandler(const char *, int, int, const char *, ...) {}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void Com_PrintError(int, const char *, ...) {}
WEAK void __cdecl Com_Error(errorParm_t, const char *, ...) {}

namespace
{
int g_failures = 0;

void Check(const bool ok, const char *const what)
{
    if (!ok)
    {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s\n", what);
    }
}

unsigned char g_buffer[4096];

entityState_s MakeEntity(const int eType)
{
    entityState_s s{};
    s.number = 37;
    s.eType = eType;
    s.lerp.pos.trBase[0] = 128.0f;
    // Origins travel as whole units, so the fixture uses whole units.
    s.lerp.pos.trBase[1] = -64.0f;
    s.lerp.pos.trBase[2] = 12.0f;
    s.lerp.apos.trBase[1] = 90.0f;
    s.otherEntityNum = 5;
    s.groundEntityNum = 1022;
    s.eventParm = 7;
    s.weapon = 3;
    s.clientNum = 2;
    s.partBits[3] = 0x80000001u; // the last word, so a short copy shows
    return s;
}

// Writes `to` as a forced delta from `from` and decodes it back, framed as the
// snapshot writer frames it.
bool RoundTrip(const entityState_s &from, const entityState_s &to, entityState_s *decoded, msg_t *readMsg)
{
    msg_t msg{};
    MSG_Init(&msg, g_buffer, sizeof(g_buffer));
    SnapshotInfo_s snapInfo{};
    MSG_WriteEntityDeltaForEType(&snapInfo, &msg, 1000, to.eType, &from, &to, 1);
    if (msg.overflowed)
        return false;
    MSG_InitReadOnly(readMsg, g_buffer, msg.cursize);
    MSG_BeginReading(readMsg);
    // The writer frames each entity with its index, which the client reads
    // before the delta (CL_ParsePacketEntities).
    const uint32_t number = MSG_ReadEntityIndex(readMsg, 10);
    *decoded = from;
    return number == static_cast<uint32_t>(to.number)
        && MSG_ReadDeltaEntity(readMsg, 1000, const_cast<entityState_s *>(&from), decoded, number) == 0
        && !readMsg->overflowed;
}

void TestRoundTripEachEntityType()
{
    const int types[] = {ET_GENERAL, ET_PLAYER, ET_MISSILE};
    for (const int eType : types)
    {
        const entityState_s from{};
        const entityState_s to = MakeEntity(eType);
        entityState_s decoded{};
        msg_t readMsg{};
        Check(RoundTrip(from, to, &decoded, &readMsg), "entity delta decodes without overflow");
        Check(std::memcmp(&decoded, &to, sizeof(to)) == 0, "decoded entity equals the written one");
    }
}

// Bit 1 = removal: the reader reports it and leaves `to` alone.
void TestRemovalBit()
{
    g_buffer[0] = 0x01;
    msg_t msg{};
    MSG_InitReadOnly(&msg, g_buffer, 1);
    MSG_BeginReading(&msg);
    entityState_s from = MakeEntity(ET_GENERAL);
    entityState_s to{};
    Check(MSG_ReadDeltaEntity(&msg, 1000, &from, &to, 37) == 1, "removal bit reads as remove");
}

// Bits 0,0 = unchanged: `to` becomes a copy of `from`.
void TestUnchangedEntity()
{
    g_buffer[0] = 0x00;
    msg_t msg{};
    MSG_InitReadOnly(&msg, g_buffer, 1);
    MSG_BeginReading(&msg);
    entityState_s from = MakeEntity(ET_MISSILE);
    entityState_s to{};
    Check(MSG_ReadDeltaEntity(&msg, 1000, &from, &to, 37) == 0, "unchanged entity reads as present");
    Check(std::memcmp(&to, &from, sizeof(to)) == 0, "unchanged entity copies the baseline");
}

// A lastChanged index past the entity type's field list must fail the
// message, not walk past the NetField table.
void TestHostileLastChanged()
{
    msg_t msg{};
    MSG_Init(&msg, g_buffer, sizeof(g_buffer));
    MSG_WriteBit0(&msg);   // not removed
    MSG_WriteBit1(&msg);   // changed
    MSG_WriteBits(&msg, 61, 6); // lastChanged = 61, more than any field list
    for (int i = 0; i < 64; ++i)
        MSG_WriteBit0(&msg);
    msg_t readMsg{};
    MSG_InitReadOnly(&readMsg, g_buffer, msg.cursize);
    MSG_BeginReading(&readMsg);
    entityState_s from{};
    entityState_s to{};
    MSG_ReadDeltaEntity(&readMsg, 1000, &from, &to, 37);
    Check(readMsg.overflowed != 0, "lastChanged past the field list overflows the message");
}

// A written message, and readers over it in a heap block of exactly `size`
// bytes, so ASan reports any read past the end.
struct Wire
{
    unsigned char bytes[0x4000];
    int size = 0;
};

using Decode = std::function<void(msg_t *)>;

bool DecodeFrom(const Wire &wire, const int size, const Decode &decode)
{
    auto block = std::make_unique<unsigned char[]>(size ? size : 1);
    std::copy_n(wire.bytes, size, block.get());
    msg_t msg{};
    MSG_InitReadOnly(&msg, block.get(), size);
    MSG_BeginReading(&msg);
    msg.lastEntityRef = -1; // as MSG_Init leaves it for the writer
    decode(&msg);
    return msg.overflowed == 0;
}

// Each byte-truncated copy overflows; the whole message, decoded last so its
// result is what the caller compares, doesn't.
void CheckTruncations(const Wire &wire, const Decode &decode, const char *const what)
{
    int safe = 0;
    for (int size = 0; size < wire.size; ++size)
        safe += DecodeFrom(wire, size, decode) ? 0 : 1;
    if (safe != wire.size)
        std::fprintf(stderr, "%s: %d of %d truncations decoded\n", what, wire.size - safe, wire.size);
    Check(safe == wire.size, "every truncated copy overflows the message");
    Check(DecodeFrom(wire, wire.size, decode), what);
}

msg_t BeginWrite(Wire *wire)
{
    msg_t msg{};
    MSG_Init(&msg, wire->bytes, sizeof(wire->bytes));
    return msg;
}

void EndWrite(Wire *wire, const msg_t &msg)
{
    Check(!msg.overflowed, "the writer fits the delta");
    wire->size = msg.cursize;
}

clientState_s MakeClient()
{
    clientState_s s{};
    s.clientIndex = 9;
    s.team = TEAM_AXIS;
    s.modelindex = 12;
    s.attachModelIndex[5] = 3;
    s.attachTagIndex[5] = 4;
    I_strncpyz(s.name, "kc-win-arm64", sizeof(s.name));
    s.rank = 54;
    s.prestige = 2;
    s.perks = 0x40000801;
    s.attachedVehEntNum = 1023; // the last field, so a short delta shows
    return s;
}

// CL_ParseClients reads the change bit and index, then MSG_ReadDeltaClient.
int ReadClient(msg_t *msg, clientState_s *from, clientState_s *to)
{
    if (MSG_ReadBit(msg) != 1)
        return -1;
    const uint32_t number = MSG_ReadEntityIndex(msg, 6);
    return number >= 64 ? -1 : MSG_ReadDeltaClient(msg, 1000, from, to, number);
}

void TestClientDeltas()
{
    const clientState_s to = MakeClient();
    SnapshotInfo_s snapInfo{};
    Wire wire;
    msg_t msg = BeginWrite(&wire);
    MSG_WriteDeltaClient(&snapInfo, &msg, 1000, nullptr, const_cast<clientState_s *>(&to), 1);
    EndWrite(&wire, msg);
    clientState_s decoded{};
    CheckTruncations(wire, [&](msg_t *m) { ReadClient(m, nullptr, &decoded); }, "client delta decodes");
    DecodeFrom(wire, wire.size, [&](msg_t *m) { Check(ReadClient(m, nullptr, &decoded) == 0, "client is present"); });
    Check(std::memcmp(&decoded, &to, sizeof(to)) == 0, "decoded client equals the written one");

    // Forced, unchanged: the reader copies the baseline.
    msg = BeginWrite(&wire);
    MSG_WriteDeltaClient(&snapInfo, &msg, 1000, const_cast<clientState_s *>(&to), const_cast<clientState_s *>(&to), 1);
    EndWrite(&wire, msg);
    clientState_s unchanged{};
    DecodeFrom(wire, wire.size, [&](msg_t *m) {
        Check(ReadClient(m, const_cast<clientState_s *>(&to), &unchanged) == 0, "unchanged client is present");
    });
    Check(std::memcmp(&unchanged, &to, sizeof(to)) == 0, "unchanged client copies the baseline");

    // A client that left: the writer sends its removal.
    msg = BeginWrite(&wire);
    MSG_WriteDeltaClient(&snapInfo, &msg, 1000, const_cast<clientState_s *>(&to), nullptr, 1);
    EndWrite(&wire, msg);
    DecodeFrom(wire, wire.size, [&](msg_t *m) {
        Check(ReadClient(m, const_cast<clientState_s *>(&to), &unchanged) == 1, "removed client reads as remove");
    });
}

// The archived entities the server's snapshot cache decodes
// (SV_GetArchivedClientInfo), framed by their 10-bit index.
void TestArchivedEntityDelta()
{
    archivedEntity_s from{};
    archivedEntity_s to{};
    to.s = MakeEntity(ET_PLAYER);
    to.r.svFlags = 0x41;
    to.r.clientMask[1] = 0x80000000u;
    to.r.absmin[0] = -16.0f;
    to.r.absmax[2] = 72.0f;
    SnapshotInfo_s snapInfo{};
    Wire wire;
    msg_t msg = BeginWrite(&wire);
    MSG_WriteDeltaArchivedEntity(&snapInfo, &msg, 1000, &from, &to, 1);
    EndWrite(&wire, msg);
    archivedEntity_s decoded{};
    const auto read = [&](msg_t *m) {
        const uint32_t number = MSG_ReadEntityIndex(m, 10);
        if (number < 1024 && MSG_ReadDeltaArchivedEntity(m, 1000, &from, &decoded, number) != 0)
            m->overflowed = 1;
    };
    CheckTruncations(wire, read, "archived entity delta decodes");
    Check(std::memcmp(&decoded, &to, sizeof(to)) == 0, "decoded archived entity equals the written one");
}

// A playerstate touching each block the reader decodes: the field list, the
// stats, ammo and clip masks, the objectives, both hudelem arrays and the
// weapon models. Archived, as the server's snapshot cache writes it, so the
// origin is always sent.
std::unique_ptr<playerState_s> MakePlayerstate()
{
    auto ps = std::make_unique<playerState_s>();
    std::memset(ps.get(), 0, sizeof(playerState_s));
    ps->commandTime = 123456;
    ps->pm_type = PM_NORMAL;
    ps->origin[0] = 512.0f;
    ps->origin[1] = -256.0f;
    ps->origin[2] = 40.0f;
    ps->viewangles[1] = 45.0f;
    ps->clientNum = 9;
    ps->weapon = 7;
    ps->stats[0] = 100;
    ps->stats[3] = 31;
    ps->stats[4] = 200;
    ps->ammo[37] = 90;
    ps->ammoclip[127] = 30;
    ps->objective[15].state = OBJST_ACTIVE;
    ps->objective[15].origin[0] = 64.0f;
    ps->objective[15].entNum = 1023;
    // The writer sends hudelems up to the first free one, so all 31 are in use.
    for (int i = 0; i < 31; ++i)
    {
        ps->hud.archival[i].type = HE_TYPE_TEXT;
        ps->hud.archival[i].x = static_cast<float>(10 * i);
    }
    ps->hud.current[0].type = HE_TYPE_VALUE;
    ps->hud.current[0].y = -20.0f;
    ps->hud.current[0].alignOrg = 5;
    ps->weaponmodels[127] = 4;
    ps->deltaTime = 50;
    return ps;
}

void TestPlayerstateDeltas()
{
    const auto from = std::make_unique<playerState_s>();
    std::memset(from.get(), 0, sizeof(playerState_s));
    const auto to = MakePlayerstate();
    SnapshotInfo_s snapInfo{};
    snapInfo.archived = true;
    Wire wire;
    msg_t msg = BeginWrite(&wire);
    MSG_WriteDeltaPlayerstate(&snapInfo, &msg, 1000, from.get(), to.get());
    EndWrite(&wire, msg);
    const auto decoded = std::make_unique<playerState_s>();
    const auto read = [&](msg_t *m) { MSG_ReadDeltaPlayerstate(0, m, 1000, from.get(), decoded.get(), false); };
    CheckTruncations(wire, read, "playerstate delta decodes");
    Check(std::memcmp(decoded.get(), to.get(), sizeof(playerState_s)) == 0, "decoded playerstate equals the written one");

    // No change: the reader leaves the baseline.
    msg = BeginWrite(&wire);
    MSG_WriteDeltaPlayerstate(&snapInfo, &msg, 1000, to.get(), to.get());
    EndWrite(&wire, msg);
    DecodeFrom(wire, wire.size, [&](msg_t *m) { MSG_ReadDeltaPlayerstate(0, m, 1000, to.get(), decoded.get(), false); });
    Check(std::memcmp(decoded.get(), to.get(), sizeof(playerState_s)) == 0, "unchanged playerstate keeps the baseline");

    // A writer out of room flags the message instead of writing past it.
    auto small = std::make_unique<unsigned char[]>(16);
    MSG_Init(&msg, small.get(), 16);
    MSG_WriteDeltaPlayerstate(&snapInfo, &msg, 1000, from.get(), to.get());
    Check(msg.overflowed != 0, "a playerstate that doesn't fit overflows the writer");
}

// Hostile lastChanged counts past the client and playerstate field lists, and
// a hudelem field index past its list, must overflow the message.
void TestHostileFieldCounts()
{
    Wire wire;
    msg_t msg = BeginWrite(&wire);
    MSG_WriteBit1(&msg); // changed
    MSG_WriteBit0(&msg); // index: full
    MSG_WriteBits(&msg, 9, 6);
    MSG_WriteBit0(&msg); // not removed
    MSG_WriteBit1(&msg); // delta
    MSG_WriteBits(&msg, 31, 5); // lastChanged 31 of 24 fields
    for (int i = 0; i < 64; ++i)
        MSG_WriteBit0(&msg);
    EndWrite(&wire, msg);
    clientState_s to{};
    Check(!DecodeFrom(wire, wire.size, [&](msg_t *m) { ReadClient(m, nullptr, &to); }),
        "client lastChanged past the field list overflows the message");

    const auto from = std::make_unique<playerState_s>();
    std::memset(from.get(), 0, sizeof(playerState_s));
    const auto ps = std::make_unique<playerState_s>();
    msg = BeginWrite(&wire);
    MSG_WriteBit1(&msg);         // origin sent
    MSG_WriteBits(&msg, 255, 8); // lastChanged 255 of 141 fields
    for (int i = 0; i < 64; ++i)
        MSG_WriteBit0(&msg);
    EndWrite(&wire, msg);
    Check(!DecodeFrom(wire, wire.size, [&](msg_t *m) { MSG_ReadDeltaPlayerstate(0, m, 1000, from.get(), ps.get(), false); }),
        "playerstate lastChanged past the field list overflows the message");

    msg = BeginWrite(&wire);
    MSG_WriteBit1(&msg);      // origin sent
    MSG_WriteBits(&msg, 0, 8); // no fields
    for (int i = 0; i < 1 + 1 + 8 + 1; ++i)
        MSG_WriteBit0(&msg); // no stats, ammo, clips or objectives
    MSG_WriteBit1(&msg);      // hudelems
    MSG_WriteBits(&msg, 1, 5); // one archival hudelem
    MSG_WriteBits(&msg, 63, 6); // field index 63 of 40
    for (int i = 0; i < 64; ++i)
        MSG_WriteBit0(&msg);
    EndWrite(&wire, msg);
    Check(!DecodeFrom(wire, wire.size, [&](msg_t *m) { MSG_ReadDeltaPlayerstate(0, m, 1000, from.get(), ps.get(), false); }),
        "hudelem field index past its list overflows the message");
}
}

int main()
{
    TestRoundTripEachEntityType();
    TestRemovalBit();
    TestUnchangedEntity();
    TestHostileLastChanged();
    TestClientDeltas();
    TestArchivedEntityDelta();
    TestPlayerstateDeltas();
    TestHostileFieldCounts();
    if (g_failures)
        return 1;
    std::puts("msg snapshot entity deltas: ok");
    return 0;
}

// msg_snapshot_delta_tests.cpp: the MP snapshot entity delta codec, production
// both ways (qcommon/sv_msg_write_mp.cpp writes, qcommon/msg_mp.cpp reads), as
// the headless server compiles it. A delta written for each entity type must
// decode to the same entityState_s; a removal, an unchanged entity and a
// hostile lastChanged index past the field list must decode as the client
// expects. These readers parse every snapshot an (untrusted) server sends.

#include <qcommon/msg_mp.h>
#include <qcommon/sv_msg_write_mp.h>
#include <qcommon/ent.h>

#include <server_mp/server_mp.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_time.h>

#include <cstdio>
#include <cstring>

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
WEAK std::uint32_t KISAK_CDECL Sys_Milliseconds() { return 0; }
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
}

int main()
{
    TestRoundTripEachEntityType();
    TestRemovalBit();
    TestUnchangedEntity();
    TestHostileLastChanged();
    if (g_failures)
        return 1;
    std::puts("msg snapshot entity deltas: ok");
    return 0;
}

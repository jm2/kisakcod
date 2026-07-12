#ifndef KISAK_MP
#error This File is MultiPlayer Only
#endif

#include "net_chan_mp.h"
#include "qcommon.h"
#ifndef KISAK_DEDI_HEADLESS
#include <client_mp/client_mp.h>
#endif
#include <win32/win_local.h>
#include "cmd.h"
#include <universal/com_files.h>
#include <server_mp/server_mp.h>
#include <win32/win_net.h>
#ifndef KISAK_DEDI_HEADLESS
#include <cgame_mp/cg_local_mp.h>
#endif
#include <universal/profile.h>
#include <universal/com_math.h>
#include <universal/sys_atomic.h>
#include <limits>


const dvar_t *showpackets;
const dvar_t *fakelag_target;
const dvar_t *fakelag_packetloss;
const dvar_t *fakelag_currentjitter;
const dvar_t *fakelag_jitter;
const dvar_t *fakelag_current;
const dvar_t *msg_dumpEnts;
const dvar_t *net_profile;
const dvar_t *net_lanauthorize;
const dvar_t *packetDebug;
const dvar_t *showdrop;
const dvar_t *fakelag_jitterinterval;
const dvar_t *net_showprofile;
const dvar_t *msg_printEntityNums;
const dvar_t *msg_hudelemspew;
// 
// struct fakedLatencyPackets_t *laggedPackets 82ff2720     net_chan_mp.obj
// int g_qport              83020760     net_chan_mp.obj
// BOOL fakelagInitialized  83020768     net_chan_mp.obj
uint8_t tempNetchanPacketBuf[131072];
loopback_t loopbacks[2];
fakedLatencyPackets_t laggedPackets[512];
bool fakelagInitialized;

namespace
{
constexpr uint32_t LOOPBACK_QUEUE_COUNT = 2u;
constexpr uint32_t LOOPBACK_MESSAGE_COUNT = 16u;
constexpr uint32_t LOOPBACK_MESSAGE_MASK = LOOPBACK_MESSAGE_COUNT - 1u;

static_assert(ARRAY_COUNT(loopbacks) == LOOPBACK_QUEUE_COUNT);
static_assert(ARRAY_COUNT(loopbacks[0].msgs) == LOOPBACK_MESSAGE_COUNT);
static_assert((LOOPBACK_MESSAGE_COUNT & LOOPBACK_MESSAGE_MASK) == 0u);

// send/get publish the queue state, while this lock protects each non-atomic
// slot payload from being overwritten as a wrapped consumer copies it.
volatile uint32_t s_loopbackLocks[LOOPBACK_QUEUE_COUNT];

bool Net_LoopbackQueueIndex(netsrc_t sock, uint32_t *queueIndex)
{
    if (!queueIndex)
        return false;

    const int32_t value = static_cast<int32_t>(sock);
    if (value < 0 || static_cast<uint32_t>(value) >= LOOPBACK_QUEUE_COUNT)
        return false;

    *queueIndex = static_cast<uint32_t>(value);
    return true;
}

bool Net_ResolveLoopbackRoute(
    netsrc_t sock,
    const netadr_t &to,
    uint32_t *queueIndex,
    int32_t *returnPort)
{
    if (!queueIndex || !returnPort)
        return false;

    if (sock == NS_CLIENT1)
    {
        *queueIndex = static_cast<uint32_t>(NS_SERVER);
        *returnPort = static_cast<int32_t>(NS_CLIENT1);
        return true;
    }

    if (sock != NS_SERVER || to.port >= LOOPBACK_QUEUE_COUNT)
        return false;

    *queueIndex = static_cast<uint32_t>(to.port);
    *returnPort = static_cast<int32_t>(NS_CLIENT1);
    return true;
}

void Net_LockLoopback(uint32_t queueIndex)
{
    while (Sys_AtomicCompareExchange(
               &s_loopbackLocks[queueIndex], 1u, 0u)
           != 0u)
    {
        Sys_Sleep(0);
    }
}

void Net_UnlockLoopback(uint32_t queueIndex)
{
    Sys_AtomicStore(&s_loopbackLocks[queueIndex], 0u);
}
}

ClientSnapshotData s_clientSnapshotData[64];

int net_iProfilingOn;

int g_qport;

const char *netsrcString[2] =
{
    "client1",
    "server"
};

static bool __cdecl Net_AnyLocalClientsRunning()
{
#ifdef KISAK_DEDI_HEADLESS
    return false;
#else
    return CL_AnyLocalClientsRunning();
#endif
}

static void __cdecl Net_PrintClientProfileStats(int localClientNum, int bPrintToConsole)
{
#ifndef KISAK_DEDI_HEADLESS
    CL_Netchan_PrintProfileStats(localClientNum, bPrintToConsole);
#else
    (void)localClientNum;
    (void)bPrintToConsole;
#endif
}

static void __cdecl Net_AddClientOOBProfilePacket(netsrc_t sock, int length)
{
#ifndef KISAK_DEDI_HEADLESS
    CL_Netchan_AddOOBProfilePacket(sock, length);
#else
    (void)sock;
    (void)length;
#endif
}

const char *NET_AdrToString(netadr_t a)
{
    static	char	s[64];

    Com_sprintf(s, sizeof(s), "unknown");

    if (a.type == NA_LOOPBACK) {
        Com_sprintf(s, sizeof(s), "loopback");
    }
    else if (a.type == NA_BOT) {
        Com_sprintf(s, sizeof(s), "bot");
    }
    else if (a.type == NA_IP) {
        Com_sprintf(s, sizeof(s), "%i.%i.%i.%i:%i",
            a.ip[0], a.ip[1], a.ip[2], a.ip[3], BigShort(a.port));
    }
    else if (a.type == NA_BAD) {
        Com_sprintf(s, sizeof(s), "BAD");
    }
    else {
        Com_sprintf(s, sizeof(s), "%02x%02x%02x%02x.%02x%02x%02x%02x%02x%02x:%i",
            a.ipx[0], a.ipx[1], a.ipx[2], a.ipx[3], a.ipx[4], a.ipx[5], a.ipx[6], a.ipx[7], a.ipx[8], a.ipx[9],
            BigShort(a.port));
    }

    return s;
}

void __cdecl NetProf_PrepProfiling(netProfileInfo_t *prof)
{
    if (net_profile->current.integer)
    {
        if (!net_iProfilingOn)
        {
            if (!com_sv_running->current.enabled || Net_AnyLocalClientsRunning() && net_profile->current.integer == 2)
                net_iProfilingOn = 1;
            else
                net_iProfilingOn = 2;
            Com_Printf(16, "Net Profiling turned on: %s\n", WeaponStateNames[net_iProfilingOn + 26]);
            memset((uint8_t *)prof, 0, sizeof(netProfileInfo_t));
        }
    }
    else if (net_iProfilingOn)
    {
        net_iProfilingOn = 0;
        Com_Printf(16, "Net Profiling turned off\n");
        memset((uint8_t *)prof, 0, sizeof(netProfileInfo_t));
    }
}

void __cdecl NetProf_AddPacket(netProfileStream_t *pProfStream, int iSize, int bFragment)
{
    netProfilePacket_t *pPacket; // [esp+0h] [ebp-4h]

    iassert( net_iProfilingOn );
    pProfStream->iCurrPacket = (pProfStream->iCurrPacket + 1) % 60;
    pPacket = &pProfStream->packets[pProfStream->iCurrPacket];
    pPacket->iTime = Sys_Milliseconds();
    pPacket->iSize = iSize;
    pPacket->bFragment = bFragment;
}

void __cdecl NetProf_NewSendPacket(netchan_t *pChan, int iSize, int bFragment)
{
    if (net_iProfilingOn)
    {
        NetProf_AddPacket(&pChan->prof.send, iSize, bFragment);
        if ((net_showprofile->current.integer & 2) != 0)
        {
            if (bFragment)
                Com_Printf(16, "[%s] send%s: %i\n", netsrcString[pChan->sock], " fragment", iSize);
            else
                Com_Printf(16, "[%s] send%s: %i\n", netsrcString[pChan->sock], "", iSize);
        }
    }
}

void __cdecl NetProf_NewRecievePacket(netchan_t *pChan, int iSize, int bFragment)
{
    if (net_iProfilingOn)
    {
        NetProf_AddPacket(&pChan->prof.recieve, iSize, bFragment);
        if ((net_showprofile->current.integer & 2) != 0)
        {
            if (bFragment)
                Com_Printf(16, "[%s] recieve%s: %i\n", netsrcString[pChan->sock], " fragment", iSize);
            else
                Com_Printf(16, "[%s] recieve%s: %i\n", netsrcString[pChan->sock], "", iSize);
        }
    }
}

void __cdecl NetProf_UpdateStatistics(netProfileStream_t *pStream)
{
    int v1; // esi
    int iTimeSpan; // [esp+4h] [ebp-2Ch]
    int iTotalBytes; // [esp+8h] [ebp-28h]
    int iLargestSize; // [esp+Ch] [ebp-24h]
    int iOldestPacket; // [esp+10h] [ebp-20h]
    int iSmallestSize; // [esp+14h] [ebp-1Ch]
    int iNumFragments; // [esp+18h] [ebp-18h]
    int i; // [esp+1Ch] [ebp-14h]
    int iNumPackets; // [esp+20h] [ebp-10h]
    int iOldestPacketTime; // [esp+2Ch] [ebp-4h]

    iassert( pStream );
    iassert( net_iProfilingOn );
    iNumPackets = 0;
    iNumFragments = 0;
    iOldestPacket = -1;
    iOldestPacketTime = Sys_Milliseconds();
    iTotalBytes = 0;
    iSmallestSize = 9999;
    iLargestSize = 0;
    for (i = 0; i < 60; ++i)
    {
        if (pStream->packets[i].iTime && (int)Sys_Milliseconds() <= pStream->packets[i].iTime + 1000)
        {
            ++iNumPackets;
            if (pStream->packets[i].bFragment)
                ++iNumFragments;
            if (pStream->packets[i].iTime < iOldestPacketTime)
            {
                iOldestPacket = i;
                iOldestPacketTime = pStream->packets[i].iTime;
            }
            iTotalBytes += pStream->packets[i].iSize;
            if (pStream->packets[i].iSize < iSmallestSize)
                iSmallestSize = pStream->packets[i].iSize;
            if (pStream->packets[i].iSize > iLargestSize)
                iLargestSize = pStream->packets[i].iSize;
        }
    }
    if (iNumPackets)
    {
        if (iNumFragments)
            pStream->iFragmentPercentage = 100 * iNumFragments / iNumPackets;
        else
            pStream->iFragmentPercentage = 0;
        pStream->iLargestPacket = iLargestSize;
        pStream->iSmallestPacket = iSmallestSize;
        v1 = pStream->iLastBPSCalcTime + 100;
        if (v1 < (int)Sys_Milliseconds())
        {
            iTimeSpan = Sys_Milliseconds() - iOldestPacketTime;
            if (iOldestPacket != -1)
            {
                iTotalBytes -= pStream->packets[iOldestPacket].iSize;
                --iNumPackets;
                if (pStream->packets[iOldestPacket].bFragment)
                    --iNumFragments;
            }
            if (iTimeSpan >= 1 && iNumPackets)
            {
                if (iTotalBytes)
                    pStream->iBytesPerSecond = (int)((double)iTotalBytes / ((double)iTimeSpan * EQUAL_EPSILON));
                else
                    pStream->iBytesPerSecond = 0;
                pStream->iLastBPSCalcTime = Sys_Milliseconds();
            }
            else
            {
                pStream->iBytesPerSecond = 0;
            }
        }
        pStream->iCountedPackets = iNumPackets;
        pStream->iCountedFragments = iNumFragments;
    }
    else
    {
        pStream->iBytesPerSecond = 0;
        pStream->iLastBPSCalcTime = 0;
        pStream->iCountedPackets = 0;
        pStream->iCountedFragments = 0;
        pStream->iFragmentPercentage = 0;
        pStream->iLargestPacket = 0;
        pStream->iSmallestPacket = 0;
    }
}

void __cdecl Net_DisplayProfile(int localClientNum)
{
    if (!net_profile->current.integer)
        Dvar_SetInt((dvar_s *)net_profile, 2 - com_sv_running->current.enabled);
    if (net_iProfilingOn)
    {
        if (net_iProfilingOn == 1)
        {
            Net_PrintClientProfileStats(localClientNum, 0);
        }
        else
        {
            SV_Netchan_PrintProfileStats(0);
        }
    }
}

char __cdecl FakeLag_DestroyPacket(uint32_t packet)
{
    if (packet >= ARRAY_COUNT(laggedPackets))
        return 0;

    if (laggedPackets[packet].data)
        Z_VirtualFree(laggedPackets[packet].data);
    memset(&laggedPackets[packet], 0, sizeof(laggedPackets[packet]));
    return 1;
}

void __cdecl FakeLag_SendPacket_Real(uint32_t packet)
{
    if (packet >= ARRAY_COUNT(laggedPackets))
        return;

    fakedLatencyPackets_t &queued = laggedPackets[packet];
    if (!queued.outbound
        || !queued.data
        || queued.length == 0u
        || queued.length > static_cast<uint32_t>(
            (std::numeric_limits<int32_t>::max)()))
    {
        FakeLag_DestroyPacket(packet);
        return;
    }

    NET_SendPacket(
        queued.sock,
        static_cast<int32_t>(queued.length),
        queued.data,
        queued.addr);
    FakeLag_DestroyPacket(packet);
}

void __cdecl FakeLag_Init()
{
    if (!fakelagInitialized)
    {
        memset((uint8_t *)laggedPackets, 0, sizeof(laggedPackets));
        fakelagInitialized = 1;
    }
}

uint32_t __cdecl FakeLag_GetFreeSlot()
{
    int packet; // [esp+0h] [ebp-Ch]
    int packeta; // [esp+0h] [ebp-Ch]
    uint32_t oldest; // [esp+4h] [ebp-8h]
    int oldestTime; // [esp+8h] [ebp-4h]

    for (packet = 0; packet < 512; ++packet)
    {
        if (!laggedPackets[packet].length)
            return packet;
    }
    oldest = 0;
    oldestTime = Sys_Milliseconds();
    for (packeta = 0; packeta < 512; ++packeta)
    {
        if (laggedPackets[packeta].outbound && laggedPackets[packeta].startTime < oldestTime)
        {
            oldest = packeta;
            oldestTime = laggedPackets[packeta].startTime;
        }
    }
    Com_Printf(16, "fake lag buffer is full, you should increase FAKELATENCY_MAX_PACKETS_HELD or reduce your latency\n");
    if (laggedPackets[oldest].outbound)
        FakeLag_SendPacket_Real(oldest);
    else
        FakeLag_DestroyPacket(oldest);
    return oldest;
}

bool __cdecl FakeLag_HostingGameOrParty()
{
    return com_sv_running->current.enabled;
}

uint32_t __cdecl FakeLag_SendPacket(netsrc_t sock, int length, uint8_t *data, netadr_t to)
{
    static int lastCall;

    __int64 v4; // rax
    const char *v6; // [esp+Ch] [ebp-28h]
    const char *v7; // [esp+10h] [ebp-24h]
    int v8; // [esp+14h] [ebp-20h]
    int jitter; // [esp+1Ch] [ebp-18h]
    uint32_t slot; // [esp+20h] [ebp-14h]
    uint32_t now; // [esp+24h] [ebp-10h]
    int change; // [esp+28h] [ebp-Ch]

    if (length <= 0 || !data)
        return static_cast<uint32_t>(-2);

    now = Sys_Milliseconds();
    if (fakelag_jitter->current.integer + fakelag_target->current.integer != fakelag_current->current.integer)
    {
        v4 = fakelag_current->current.integer - (fakelag_currentjitter->current.integer + fakelag_target->current.integer);
        if ((int)((HIDWORD(v4) ^ v4) - HIDWORD(v4)) < (int)(now - lastCall))
            v8 = (HIDWORD(v4) ^ v4) - HIDWORD(v4);
        else
            v8 = now - lastCall;
        change = v8;
        if (fakelag_currentjitter->current.integer + fakelag_target->current.integer < fakelag_current->current.integer)
            change = -v8;
        Dvar_SetInt(fakelag_current, change + fakelag_current->current.integer);
    }
    if (fakelag_packetloss->current.value > 0.0 && fakelag_packetloss->current.value >= flrand(0.0, 1.0))
        return -1;
    if (fakelagInitialized
        && (fakelag_current->current.integer || fakelag_currentjitter->current.integer)
        && (!FakeLag_HostingGameOrParty() || to.type != NA_IP))
    {
        slot = FakeLag_GetFreeSlot();
        uint8_t *const queuedData = reinterpret_cast<uint8_t *>(
            Z_VirtualAlloc(length, "FakeLag_SendPacket", 0));
        if (!queuedData)
            return static_cast<uint32_t>(-2);

        memcpy(queuedData, data, static_cast<size_t>(length));
        fakedLatencyPackets_t &queued = laggedPackets[slot];
        memset(&queued, 0, sizeof(queued));
        queued.outbound = true;
        queued.sock = sock;
        queued.loopback = to.type == NA_LOOPBACK;
        queued.addr = to;
        queued.data = queuedData;
        queued.msg.data = queuedData;
        if (fakelag_jitter->current.integer)
            jitter = irand(0, lastCall - now);
        else
            jitter = 0;
        queued.startTime = jitter + Sys_Milliseconds();
        queued.length = static_cast<uint32_t>(length);
        if (showpackets->current.integer && (showpackets->current.integer > 1 || !queued.loopback))
        {
            if (sock == NS_SERVER)
            {
                v7 = "server";
            }
            else
            {
                if (sock >= NS_SERVER)
                    v6 = "";
                else
                    v6 = "client";
                v7 = v6;
            }
            if (queued.loopback)
                Com_Printf(16, "[%u] adding outbound %s packet for %s\n", now, "loopback", v7);
            else
                Com_Printf(16, "[%u] adding outbound %s packet for %s\n", now, "network", v7);
        }
        lastCall = now;
        return slot;
    }
    else if (NET_SendPacket(sock, length, data, to))
    {
        return -1;
    }
    else
    {
        return -2;
    }
}

uint32_t __cdecl FakeLag_QueueIncomingPacket(bool loopback, netsrc_t sock, netadr_t *from, msg_t *msg)
{
    static int lastCall_0;

    __int64 v4; // rax
    const char *v6; // [esp+14h] [ebp-24h]
    const char *v7; // [esp+18h] [ebp-20h]
    int v8; // [esp+1Ch] [ebp-1Ch]
    int jitter; // [esp+24h] [ebp-14h]
    uint32_t slot; // [esp+28h] [ebp-10h]
    uint32_t now; // [esp+2Ch] [ebp-Ch]
    int change; // [esp+30h] [ebp-8h]

    if (!from
        || !msg
        || !msg->data
        || msg->cursize <= 0
        || msg->maxsize < msg->cursize)
    {
        return static_cast<uint32_t>(-1);
    }

    now = Sys_Milliseconds();
    if (fakelag_jitter->current.integer + fakelag_target->current.integer != fakelag_current->current.integer)
    {
        v4 = fakelag_current->current.integer - (fakelag_currentjitter->current.integer + fakelag_target->current.integer);
        if ((int)((HIDWORD(v4) ^ v4) - HIDWORD(v4)) < (int)(now - lastCall_0))
            v8 = (HIDWORD(v4) ^ v4) - HIDWORD(v4);
        else
            v8 = now - lastCall_0;
        change = v8;
        if (fakelag_currentjitter->current.integer + fakelag_target->current.integer < fakelag_current->current.integer)
            change = -v8;
        Dvar_SetInt(fakelag_current, change + fakelag_current->current.integer);
    }
    if (fakelag_packetloss->current.value > 0.0 && fakelag_packetloss->current.value >= flrand(0.0, 1.0))
        return -1;
    slot = FakeLag_GetFreeSlot();
    uint8_t *const queuedData = reinterpret_cast<uint8_t *>(
        Z_VirtualAlloc(msg->cursize, "FakeLag_QueueIncomingPacket", 0));
    if (!queuedData)
        return static_cast<uint32_t>(-1);

    memcpy(queuedData, msg->data, static_cast<size_t>(msg->cursize));
    fakedLatencyPackets_t &queued = laggedPackets[slot];
    memset(&queued, 0, sizeof(queued));
    queued.outbound = false;
    queued.loopback = loopback;
    queued.sock = sock;
    queued.addr = *from;
    queued.data = queuedData;
    qmemcpy(&queued.msg, msg, sizeof(queued.msg));
    queued.msg.data = queuedData;
    if (fakelag_jitter->current.integer)
        jitter = irand(0, lastCall_0 - now);
    else
        jitter = 0;
    queued.startTime = jitter + Sys_Milliseconds();
    queued.length = static_cast<uint32_t>(msg->cursize);
    if (showpackets->current.integer && (showpackets->current.integer > 1 || !queued.loopback))
    {
        if (sock == NS_SERVER)
        {
            v7 = "server";
        }
        else
        {
            if (sock >= NS_SERVER)
                v6 = "";
            else
                v6 = "client";
            v7 = v6;
        }
        if (loopback)
            Com_Printf(16, "[%u] adding incoming %s packet for %s\n", now, "loopback", v7);
        else
            Com_Printf(16, "[%u] adding incoming %s packet for %s\n", now, "network", v7);
    }
    lastCall_0 = now;
    return slot;
}

void __cdecl FakeLag_ReceivePackets()
{
    netadr_t from; // [esp+0h] [ebp-44h] BYREF
    msg_t msg; // [esp+18h] [ebp-2Ch] BYREF
    int clientNum; // [esp+40h] [ebp-4h]

    if (fakelag_current->current.integer)
    {
        MSG_Init(&msg, tempNetchanPacketBuf, 0x20000);
        while (NET_GetLoopPacket_Real(NS_SERVER, &from, &msg))
            FakeLag_QueueIncomingPacket(1, NS_SERVER, &from, &msg);
        MSG_Init(&msg, tempNetchanPacketBuf, 0x20000);
        for (clientNum = 0; clientNum < 1; ++clientNum)
        {
            while (NET_GetLoopPacket_Real((netsrc_t)clientNum, &from, &msg))
                FakeLag_QueueIncomingPacket(1, (netsrc_t)clientNum, &from, &msg);
        }
        MSG_Init(&msg, tempNetchanPacketBuf, 0x20000);
        while (Sys_GetPacket(&from, &msg))
            FakeLag_QueueIncomingPacket(0, NS_PACKET, &from, &msg);
    }
}

int __cdecl FakeLag_GetPacket(bool loopback, netsrc_t sock, netadr_t *net_from, msg_t *net_message)
{
    signed int packet; // [esp+0h] [ebp-8h]
    signed int now; // [esp+4h] [ebp-4h]

    if (!net_from
        || !net_message
        || !net_message->data
        || net_message->maxsize < 0)
    {
        return 0;
    }

    const int32_t destinationCapacity = net_message->maxsize;
    now = Sys_Milliseconds();
    for (packet = 0; ; ++packet)
    {
        if (packet >= 512)
            return 0;
        if (laggedPackets[packet].length
            && !laggedPackets[packet].outbound
            && laggedPackets[packet].sock == sock
            && laggedPackets[packet].loopback == loopback
            && (FakeLag_HostingGameOrParty() && !laggedPackets[packet].loopback
                || laggedPackets[packet].startTime + fakelag_current->current.integer / 2 < now))
        {
            break;
        }
    }
    fakedLatencyPackets_t &queued = laggedPackets[packet];
    const int32_t queuedSize = queued.msg.cursize;
    if (!queued.data
        || queued.msg.data != queued.data
        || queued.length > static_cast<uint32_t>(destinationCapacity)
        || queuedSize < 0
        || static_cast<uint32_t>(queuedSize) != queued.length
        || queued.msg.readcount < 0
        || queued.msg.readcount > queuedSize)
    {
        FakeLag_DestroyPacket(static_cast<uint32_t>(packet));
        return 0;
    }

    if (showpackets->current.integer && (showpackets->current.integer > 1 || !queued.loopback))
        Com_Printf(
            16,
            "[%i] delivering incoming packet from %i (time: %i) (%ims latency)\n",
            now,
            queued.startTime,
            queued.startTime,
            now - queued.startTime);
    *net_from = queued.addr;
    net_message->bit = queued.msg.bit;
    net_message->cursize = queuedSize;
    net_message->overflowed = queued.msg.overflowed;
    net_message->readcount = queued.msg.readcount;
    if (queued.length != 0u)
        memcpy(net_message->data, queued.data, queued.length);
    FakeLag_DestroyPacket(static_cast<uint32_t>(packet));
    return 1;
}

int nextChangeTime;
void __cdecl FakeLag_Frame()
{
    double v0; // st7
    int v1; // esi
    double v2; // [esp+Ch] [ebp-18h]
    double integer; // [esp+18h] [ebp-Ch]
    signed int now; // [esp+20h] [ebp-4h]

    now = Sys_Milliseconds();
    if (fakelag_jitter->current.integer > 0 && now > nextChangeTime)
    {
        integer = (double)fakelag_jitter->current.integer;
        v0 = flrand(0.0, 1.0) * integer;
        Dvar_SetInt((dvar_s *)fakelag_currentjitter, (int)v0);
        v1 = fakelag_jitterinterval->current.integer + now;
        v2 = (double)(2 * fakelag_jitterinterval->current.integer);
        nextChangeTime = (int)(flrand(0.0, 1.0) * v2) + v1;
    }
    FakeLag_ReceivePackets();
    FakeLag_SendLaggedPackets();
}

int __cdecl FakeLag_SendLaggedPackets()
{
    uint32_t v1; // eax
    int startTime; // [esp-8h] [ebp-24h]
    uint32_t v3; // [esp-4h] [ebp-20h]
    const char *v4; // [esp+0h] [ebp-1Ch]
    const char *v5; // [esp+4h] [ebp-18h]
    const char *v6; // [esp+8h] [ebp-14h]
    signed int packet; // [esp+Ch] [ebp-10h]
    signed int loopbackPacketTime; // [esp+10h] [ebp-Ch]
    signed int networkPacketTime; // [esp+14h] [ebp-8h]
    int numSent; // [esp+18h] [ebp-4h]

    if (!fakelagInitialized)
        return 0;
    loopbackPacketTime = Sys_Milliseconds() - fakelag_current->current.integer / 2;
    if (FakeLag_HostingGameOrParty())
        networkPacketTime = Sys_Milliseconds();
    else
        networkPacketTime = Sys_Milliseconds() - fakelag_current->current.integer / 2;
    numSent = 0;
    for (packet = 0; packet < 512; ++packet)
    {
        if (laggedPackets[packet].length
            && laggedPackets[packet].outbound
            && (laggedPackets[packet].loopback && laggedPackets[packet].startTime <= loopbackPacketTime
                || !laggedPackets[packet].loopback && laggedPackets[packet].startTime <= networkPacketTime))
        {
            if (showpackets->current.integer && (showpackets->current.integer > 1 || !laggedPackets[packet].loopback))
            {
                if (laggedPackets[packet].sock == NS_SERVER)
                {
                    v6 = "server";
                }
                else
                {
                    if (laggedPackets[packet].sock >= NS_SERVER)
                        v5 = "";
                    else
                        v5 = "client";
                    v6 = v5;
                }
                if (laggedPackets[packet].loopback)
                    v4 = "loopback";
                else
                    v4 = "network";
                v3 = Sys_Milliseconds() - laggedPackets[packet].startTime;
                startTime = laggedPackets[packet].startTime;
                v1 = Sys_Milliseconds();
                Com_Printf(16, "[%u] delivering outbound %s packet for %s (from %i) (%ums delay)\n", v1, v4, v6, startTime, v3);
            }
            FakeLag_SendPacket_Real(packet);
            ++numSent;
        }
    }
    return numSent;
}

void __cdecl FakeLag_Shutdown()
{
    signed int packet; // [esp+0h] [ebp-4h]

    if (fakelagInitialized)
    {
        for (packet = 0; packet < 512; ++packet)
        {
            if (laggedPackets[packet].length)
            {
                if (laggedPackets[packet].outbound)
                    FakeLag_SendPacket_Real(packet);
                else
                    FakeLag_DestroyPacket(packet);
            }
        }
        fakelagInitialized = 0;
    }
}




void __cdecl Net_SetQPort_f()
{
    const char *v0; // eax

    if (Cmd_Argc() < 1)
        Com_PrintError(16, "setqport usage: setqport <qport>\n");
    v0 = Cmd_Argv(1);
    g_qport = (__int16)atoi(v0);
}

void __cdecl Net_GetQPort_f()
{
    Com_Printf(16, "qport = %i\n", g_qport);
}

cmd_function_s Net_DumpProfile_f_VAR;
cmd_function_s MSG_DumpNetFieldChanges_f_VAR;
cmd_function_s Net_GetQPort_f_VAR;
cmd_function_s Net_SetQPort_f_VAR;

void __cdecl Netchan_Init(__int16 port)
{
    DvarLimits min; // [esp+4h] [ebp-10h]

    showpackets = Dvar_RegisterInt("showpackets", 0, (DvarLimits)0x200000000LL, DVAR_NOFLAG, "Show packets");
    showdrop = Dvar_RegisterBool("showdrop", 0, DVAR_NOFLAG, "Show dropped packets");
    packetDebug = Dvar_RegisterBool("packetDebug", 0, DVAR_NOFLAG, "Enable packet debugging information");
    g_qport = port;
    net_profile = Dvar_RegisterInt("net_profile", 0, (DvarLimits)0x200000000LL, DVAR_NOFLAG, "Profile network performance");
    net_showprofile = Dvar_RegisterInt(
        "net_showprofile",
        0,
        (DvarLimits)0x300000000LL,
        DVAR_NOFLAG,
        "Show network profiling display");
    net_lanauthorize = Dvar_RegisterBool("net_lanauthorize", 0, DVAR_NOFLAG, "Authorise CD keys when using a LAN");
    msg_printEntityNums = Dvar_RegisterBool("msg_printEntityNums", 0, DVAR_NOFLAG, "Print entity numbers");
    msg_dumpEnts = Dvar_RegisterBool("msg_dumpEnts", 0, DVAR_NOFLAG, "Print snapshot entity info");
    msg_hudelemspew = Dvar_RegisterBool("msg_hudelemspew", 0, DVAR_NOFLAG, "Debug hudelem fields changing");
    fakelag_current = Dvar_RegisterInt("fakelag_current", 0, (DvarLimits)0x3E700000000LL, DVAR_NOFLAG, "Current fake lag value");
    fakelag_target = Dvar_RegisterInt(
        "fakelag_target",
        0,
        (DvarLimits)0x3E700000000LL,
        DVAR_NOFLAG,
        "Target value for lag debugging");
    fakelag_currentjitter = Dvar_RegisterInt(
        "fakelag_currentjitter",
        0,
        (DvarLimits)0x3E700000000LL,
        DVAR_NOFLAG,
        "Current jitter amount for lag debugging");
    fakelag_jitter = Dvar_RegisterInt(
        "fakelag_jitter",
        0,
        (DvarLimits)0x3E700000000LL,
        DVAR_NOFLAG,
        "Amount of jitter for lag debugging");
    fakelag_jitterinterval = Dvar_RegisterInt(
        "fakelag_jitterinterval",
        2000,
        (DvarLimits)0xEA6000000000LL,
        DVAR_NOFLAG,
        "jitter interval for lag debugging");
    min.value.max = 1.0;
    min.value.min = 0.0;
    fakelag_packetloss = Dvar_RegisterFloat("fakelag_packetloss", 0.0, min, DVAR_NOFLAG, "Packet loss for lag debugging");
    FakeLag_Init();
    Cmd_AddCommandInternal("net_dumpprofile", Net_DumpProfile_f, &Net_DumpProfile_f_VAR);
        Cmd_AddCommandInternal("net_dumpnetfieldchanges", MSG_DumpNetFieldChanges_f, &MSG_DumpNetFieldChanges_f_VAR);
    Cmd_AddCommandInternal("getqport", Net_GetQPort_f, &Net_GetQPort_f_VAR);
    Cmd_AddCommandInternal("setqport", Net_SetQPort_f, &Net_SetQPort_f_VAR);
}

void __cdecl Net_DumpProfile_f()
{
  if (net_iProfilingOn)
  {
    if (net_iProfilingOn == 1)
    {
      Net_PrintClientProfileStats(0, 1);
    }
    else
    {
      SV_Netchan_PrintProfileStats(1);
    }
  }
  else
  {
    Com_Printf(0, "Network profiling is not on. Set net_profile to turn on network profiling\n");
  }
}

void __cdecl Netchan_Setup(
    netsrc_t sock,
    netchan_t *chan,
    netadr_t adr,
    int qport,
    char *outgoingBuffer,
    int outgoingBufferSize,
    char *incomingBuffer,
    int incomingBufferSize)
{
    memset((uint8_t *)chan, 0, sizeof(netchan_t));
    chan->sock = sock;
    chan->remoteAddress = adr;
    chan->qport = qport;
    iassert( adr.type == NA_BOT || qport != 0 );
    chan->incomingSequence = 0;
    chan->outgoingSequence = 1;
    chan->unsentBuffer = (uint8_t *)outgoingBuffer;
    chan->unsentBufferSize = outgoingBufferSize;
    chan->fragmentBuffer = (uint8_t *)incomingBuffer;
    chan->fragmentBufferSize = incomingBufferSize;
    NetProf_PrepProfiling(&chan->prof);
}

bool __cdecl Netchan_TransmitNextFragment(netchan_t *chan)
{
    int fragmentLength; // [esp+30h] [ebp-5ACh]
    msg_t send; // [esp+34h] [ebp-5A8h] BYREF
    uint8_t send_buf[1400]; // [esp+5Ch] [ebp-580h] BYREF
    int res; // [esp+5D8h] [ebp-4h]

    PROF_SCOPED("SendPacket");

    NetProf_PrepProfiling(&chan->prof);
    MSG_Init(&send, send_buf, 1400);
    MSG_WriteLong(&send, chan->outgoingSequence | 0x80000000);
    if (chan->sock < NS_SERVER)
        MSG_WriteShort(&send, chan->qport);
    fragmentLength = 1300;
    if (chan->unsentFragmentStart + 1300 > chan->unsentLength)
        fragmentLength = chan->unsentLength - chan->unsentFragmentStart;
    MSG_WriteLong(&send, chan->unsentFragmentStart);
    MSG_WriteShort(&send, fragmentLength);
    MSG_WriteData(&send, &chan->unsentBuffer[chan->unsentFragmentStart], fragmentLength);
    res = (int)FakeLag_SendPacket(chan->sock, send.cursize, send.data, chan->remoteAddress) >= -1;
    NetProf_NewSendPacket(chan, send.cursize, 1);
    if (showpackets->current.integer && (showpackets->current.integer > 1 || chan->remoteAddress.type != NA_LOOPBACK))
        Com_Printf(
            16,
            "[%s] send %4i : s=%i fragment=%i,%i\n",
            netsrcString[chan->sock],
            send.cursize,
            chan->outgoingSequence - 1,
            chan->unsentFragmentStart,
            fragmentLength);
    chan->unsentFragmentStart += fragmentLength;
    if (chan->unsentFragmentStart == chan->unsentLength && fragmentLength != 1300)
    {
        ++chan->outgoingSequence;
        chan->unsentFragments = 0;
    }

    return res > 0;
}

bool __cdecl Netchan_Transmit(netchan_t *chan, int length, char *data)
{
    int file; // [esp+4Ch] [ebp-5ACh]
    msg_t send; // [esp+50h] [ebp-5A8h] BYREF
    uint8_t send_buf[1400]; // [esp+78h] [ebp-580h] BYREF
    int res; // [esp+5F4h] [ebp-4h]

    PROF_SCOPED("SendPacket");

    if (length > 0x20000)
    {
        file = FS_FOpenFileWrite((char*)"badpacket.dat");
        if (file)
        {
            FS_Write(data, length, file);
            FS_FCloseFile(file);
        }
        Com_Error(ERR_DROP, "Netchan_Transmit: length = %i", length);
    }
    chan->unsentFragmentStart = 0;
    if (length < 1300)
    {
        NetProf_PrepProfiling(&chan->prof);
        MSG_Init(&send, send_buf, 1400);
        MSG_WriteLong(&send, chan->outgoingSequence);
        ++chan->outgoingSequence;
        if (chan->sock < NS_SERVER)
            MSG_WriteShort(&send, chan->qport);
        if (packetDebug->current.enabled)
            Com_Printf(16, "Adding %i byte payload to packet\n", length);
        MSG_WriteData(&send, (uint8_t *)data, length);
        if (packetDebug->current.enabled)
            Com_Printf(16, "Sending %i byte packet\n", send.cursize);
        res = (int)FakeLag_SendPacket(chan->sock, send.cursize, send.data, chan->remoteAddress) >= -1;
        NetProf_NewSendPacket(chan, send.cursize, 0);
        if (showpackets->current.integer && (showpackets->current.integer > 1 || chan->remoteAddress.type != NA_LOOPBACK))
            Com_Printf(
                16,
                "[%s] send->%u.%u.%u.%u (%4i bytes) : s=%i ack=%i\n",
                netsrcString[chan->sock],
                chan->remoteAddress.ip[0],
                chan->remoteAddress.ip[1],
                chan->remoteAddress.ip[2],
                chan->remoteAddress.ip[3],
                send.cursize,
                chan->outgoingSequence - 1,
                chan->incomingSequence);
        return res > 0;
    }
    else
    {
        chan->unsentFragments = 1;
        chan->unsentLength = length;
        if (chan->unsentBufferSize <= length)
            MyAssertHandler(
                ".\\qcommon\\net_chan_mp.cpp",
                1228,
                0,
                "%s\n\t(length) = %i",
                "(chan->unsentBufferSize > length)",
                length);
        Com_Memcpy((char *)chan->unsentBuffer, data, length);
        Netchan_TransmitNextFragment(chan);
        return 1;
    }
}

int __cdecl Netchan_Process(netchan_t *chan, msg_t *msg)
{
    const char *v2; // eax
    const char *v4; // eax
    const char *v5; // eax
    const char *v6; // eax
    const char *v7; // eax
    int dropped; // [esp-8h] [ebp-1Ch]
    int incomingSequence; // [esp-4h] [ebp-18h]
    int v10; // [esp-4h] [ebp-18h]
    signed int fragmentLength; // [esp+0h] [ebp-14h]
    int fragmented; // [esp+4h] [ebp-10h]
    int sequence; // [esp+8h] [ebp-Ch]
    int fragmentStart; // [esp+10h] [ebp-4h]

    NetProf_PrepProfiling(&chan->prof);
    MSG_BeginReading(msg);
    sequence = MSG_ReadLong(msg);
    if (sequence < 0)
    {
        sequence &= ~0x80000000;
        fragmented = 1;
    }
    else
    {
        fragmented = 0;
    }

    if (chan->sock == NS_SERVER)
        MSG_ReadShort(msg); // qport (unused, but in protocol)

    if (fragmented)
    {
        fragmentStart = MSG_ReadLong(msg);
        fragmentLength = MSG_ReadShort(msg);
    }
    else
    {
        fragmentStart = 0;
        fragmentLength = 0;
    }
    NetProf_NewRecievePacket(chan, msg->cursize, fragmented);
    if (showpackets->current.integer && (showpackets->current.integer > 1 || chan->remoteAddress.type != NA_LOOPBACK))
    {
        if (fragmented)
            Com_Printf(
                16,
                "[%s] recv %4i : s=%i fragment=%i,%i\n",
                netsrcString[chan->sock],
                msg->cursize,
                sequence,
                fragmentStart,
                fragmentLength);
        else
            Com_Printf(16, "[%s] recv %4i : s=%i\n", netsrcString[chan->sock], msg->cursize, sequence);
    }
    if (sequence <= chan->incomingSequence)
    {
        if (showdrop->current.enabled
            || showpackets->current.integer && (showpackets->current.integer > 1 || chan->remoteAddress.type != NA_LOOPBACK))
        {
            incomingSequence = chan->incomingSequence;
            v2 = NET_AdrToString(chan->remoteAddress);
            Com_Printf(
                16,
                "[%s] %s: Out of order packet %i at %i\n",
                netsrcString[chan->sock],
                v2,
                sequence,
                incomingSequence);
        }
        return 0;
    }
    chan->dropped = sequence - (chan->incomingSequence + 1);
    if (chan->dropped > 0
        && (showdrop->current.enabled
            || showpackets->current.integer && (showpackets->current.integer > 1 || chan->remoteAddress.type != NA_LOOPBACK)))
    {
        dropped = chan->dropped;
        v4 = NET_AdrToString(chan->remoteAddress);
        Com_Printf(16, "[%s] %s: Dropped %i packets at %i\n", netsrcString[chan->sock], v4, dropped, sequence);
    }
    if (!fragmented)
    {
    LABEL_52:
        chan->incomingSequence = sequence;
        return 1;
    }
    if (sequence != chan->fragmentSequence)
    {
        chan->fragmentSequence = sequence;
        chan->fragmentLength = 0;
    }
    if (fragmentStart != chan->fragmentLength)
    {
        if (showdrop->current.enabled
            || showpackets->current.integer && (showpackets->current.integer > 1 || chan->remoteAddress.type != NA_LOOPBACK))
        {
            v5 = NET_AdrToString(chan->remoteAddress);
            Com_Printf(16, "%s:Dropped a message fragment\n", v5);
        }
        return 0;
    }
    if (fragmentLength >= 0
        && fragmentLength + msg->readcount <= msg->cursize
        && fragmentLength + chan->fragmentLength <= chan->fragmentBufferSize)
    {
        memcpy(&chan->fragmentBuffer[chan->fragmentLength], &msg->data[msg->readcount], fragmentLength);
        chan->fragmentLength += fragmentLength;
        if (fragmentLength == 1300)
            return 0;
        if (chan->fragmentLength > msg->maxsize)
        {
            v10 = chan->fragmentLength;
            v7 = NET_AdrToString(chan->remoteAddress);
            Com_Printf(16, "%s:fragmentLength %i > msg->maxsize\n", v7, v10);
            return 0;
        }
        *(uint32_t *)msg->data = sequence;
        memcpy(msg->data + 4, chan->fragmentBuffer, chan->fragmentLength);
        msg->cursize = chan->fragmentLength + 4;
        chan->fragmentLength = 0;
        MSG_BeginReading(msg);
        MSG_ReadLong(msg);
        goto LABEL_52;
    }
    if (showdrop->current.enabled
        || showpackets->current.integer && (showpackets->current.integer > 1 || chan->remoteAddress.type != NA_LOOPBACK))
    {
        v6 = NET_AdrToString(chan->remoteAddress);
        Com_Printf(16, "%s:illegal fragment length\n", v6);
    }
    return 0;
}

int __cdecl NET_CompareBaseAdrSigned(netadr_t *a, netadr_t *b)
{
    if (a->type != b->type)
        return a->type - b->type;
    switch (a->type)
    {
    case NA_LOOPBACK:
        return a->port - b->port;
    case NA_BOT:
        return a->port - b->port;
    case NA_IP:
        return memcmp((const char *)a->ip, (const char *)b->ip, 4);
    case NA_IPX:
        return memcmp((const char *)a->ipx, (const char *)b->ipx, 10);
    }
    Com_Printf(16, "NET_CompareBaseAdrSigned: bad address type\n");
    return 0;
}

bool __cdecl NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    return NET_CompareBaseAdrSigned(&a, &b) == 0;
}

int __cdecl NET_CompareAdrSigned(netadr_t *a, netadr_t *b)
{
    if (a->type != b->type)
        return a->type - b->type;
    switch (a->type)
    {
    case NA_LOOPBACK:
        return 0;
    case NA_IP:
        if (a->port == b->port)
            return memcmp((const char *)a->ip, (const char *)b->ip, 4);
        else
            return a->port - b->port;
    case NA_IPX:
        if (a->port == b->port)
            return memcmp((const char *)a->ipx, (const char *)b->ipx, 10);
        else
            return a->port - b->port;
    default:
        Com_Printf(16, "NET_CompareAdrSigned: bad address type\n");
        return 0;
    }
}

bool __cdecl NET_CompareAdr(netadr_t a, netadr_t b)
{
    return NET_CompareAdrSigned(&a, &b) == 0;
}

bool __cdecl NET_IsLocalAddress(netadr_t adr)
{
    return adr.type == NA_LOOPBACK || adr.type == NA_BOT;
}

int __cdecl NET_GetClientPacket(netadr_t *net_from, msg_t *net_message)
{
    if (fakelagInitialized && fakelag_current->current.integer)
        return FakeLag_GetPacket(0, NS_PACKET, net_from, net_message);
    else
        return Sys_GetPacket(net_from, net_message);
}

int __cdecl NET_GetServerPacket(netadr_t *net_from, msg_t *net_message)
{
    return Sys_GetPacket(net_from, net_message);
}

int __cdecl NET_GetLoopPacket_Real(netsrc_t sock, netadr_t *net_from, msg_t *net_message)
{
    uint32_t queueIndex;
    if (!Net_LoopbackQueueIndex(sock, &queueIndex)
        || !net_from
        || !net_message
        || !net_message->data
        || net_message->maxsize < 0)
    {
        return 0;
    }

    loopback_t *const loop = &loopbacks[queueIndex];
    Net_LockLoopback(queueIndex);

    const uint32_t send = Sys_AtomicLoad(&loop->send);
    uint32_t get = Sys_AtomicLoad(&loop->get);
    const uint32_t pending = send - get;
    if (pending > LOOPBACK_MESSAGE_COUNT)
    {
        get = send - LOOPBACK_MESSAGE_COUNT;
        Sys_AtomicStore(&loop->get, get);
    }

    if (get == send)
    {
        Net_UnlockLoopback(queueIndex);
        return 0;
    }

    const uint32_t slot = get & LOOPBACK_MESSAGE_MASK;
    const loopmsg_t &message = loop->msgs[slot];
    const int32_t dataLength = message.datalen;
    if (dataLength < 0
        || static_cast<uint32_t>(dataLength) > sizeof(message.data)
        || dataLength > net_message->maxsize
        || message.port < 0
        || message.port > UINT16_MAX)
    {
        // A malformed entry must not wedge the consumer on the same slot.
        Sys_AtomicStore(&loop->get, get + 1u);
        Net_UnlockLoopback(queueIndex);
        return 0;
    }

    if (dataLength != 0)
        memcpy(net_message->data, message.data, static_cast<uint32_t>(dataLength));
    net_message->cursize = dataLength;
    memset(net_from, 0, sizeof(*net_from));
    net_from->type = NA_LOOPBACK;
    net_from->port = static_cast<uint16_t>(message.port);

    // Finish copying before the slot can be reused by a wrapped producer.
    Sys_AtomicStore(&loop->get, get + 1u);
    Net_UnlockLoopback(queueIndex);
    return 1;
}

int __cdecl NET_GetLoopPacket(netsrc_t sock, netadr_t *net_from, msg_t *net_message)
{
    uint32_t queueIndex;
    if (!Net_LoopbackQueueIndex(sock, &queueIndex)
        || !net_from
        || !net_message
        || !net_message->data
        || net_message->maxsize < 0)
    {
        return 0;
    }

    if (fakelagInitialized && fakelag_current->current.integer)
        return FakeLag_GetPacket(1, sock, net_from, net_message);
    else
        return NET_GetLoopPacket_Real(sock, net_from, net_message);
}


void __cdecl NET_SendLoopPacket(netsrc_t sock, uint32_t length, uint8_t *data, netadr_t to)
{
    uint32_t queueIndex;
    int32_t returnPort;
    if (!data
        || length > sizeof(loopbacks[0].msgs[0].data)
        || !Net_ResolveLoopbackRoute(
            sock, to, &queueIndex, &returnPort))
    {
        return;
    }

    loopback_t *const loop = &loopbacks[queueIndex];
    Net_LockLoopback(queueIndex);

    const uint32_t send = Sys_AtomicLoad(&loop->send);
    loopmsg_t &message = loop->msgs[send & LOOPBACK_MESSAGE_MASK];
    if (length != 0u)
        memcpy(message.data, data, length);
    message.datalen = static_cast<int32_t>(length);
    message.port = returnPort;

    // Publish only after the complete slot payload is visible.
    Sys_AtomicStore(&loop->send, send + 1u);
    Net_UnlockLoopback(queueIndex);
}

char __cdecl NET_SendPacket(netsrc_t sock, int length, uint8_t *data, netadr_t to)
{
    if (length < 0 || !data)
        return 0;

    uint32_t packetMarker = 0;
    if (static_cast<uint32_t>(length) >= sizeof(packetMarker))
        memcpy(&packetMarker, data, sizeof(packetMarker));

    uint32_t sourceIndex;
    const char *const sourceName = Net_LoopbackQueueIndex(sock, &sourceIndex)
        ? netsrcString[sourceIndex]
        : "unknown";
    if (showpackets->current.integer && packetMarker == UINT32_MAX)
        Com_Printf(16, "[%s] send packet %4i\n", sourceName, length);
    if (to.type == NA_LOOPBACK)
    {
        uint32_t queueIndex;
        int32_t returnPort;
        if (static_cast<uint32_t>(length)
                > sizeof(loopbacks[0].msgs[0].data)
            || !Net_ResolveLoopbackRoute(
                sock, to, &queueIndex, &returnPort))
        {
            return 0;
        }

        NET_SendLoopPacket(sock, static_cast<uint32_t>(length), data, to);
        return 1;
    }
    else if (to.type == NA_BAD)
    {
        return 0;
    }
    else if (to.type)
    {
        return Sys_SendPacket(length, data, to);
    }
    else
    {
        return 0;
    }
}

bool __cdecl NET_OutOfBandPrint(netsrc_t sock, netadr_t adr, const char *data)
{
    const char *v3; // eax
    const char *v4; // eax
    int v6; // [esp+0h] [ebp-5Ch]
    int res; // [esp+54h] [ebp-8h]

    tempNetchanPacketBuf[0] = -1;
    tempNetchanPacketBuf[1] = -1;
    tempNetchanPacketBuf[2] = -1;
    tempNetchanPacketBuf[3] = -1;
    if (showpackets->current.integer && (showpackets->current.integer > 1 || adr.type != NA_LOOPBACK))
    {
        v3 = NET_AdrToString(adr);
        Com_DPrintf(16, "OOB Print->%s: %s\n", v3, data);
    }
    if (strlen(data) + 1 <= 0x1FFFC)
    {
        strcpy((char *)&tempNetchanPacketBuf[4], data);
        v6 = strlen((const char *)tempNetchanPacketBuf);
        res = (int)FakeLag_SendPacket(sock, v6, tempNetchanPacketBuf, adr) >= -1;

        if (sock == NS_SERVER)
            SV_Netchan_AddOOBProfilePacket(v6);
        else
            Net_AddClientOOBProfilePacket(sock, v6);
        return res > 0;
    }
    else
    {
        Com_DPrintf(16, "OOB Packet is %i bytes - too large to send\n", strlen(data));
        if (!alwaysfails)
        {
            v4 = va("OOB Packet is %i bytes - too large to send\n", strlen(data));
            MyAssertHandler(".\\qcommon\\net_chan_mp.cpp", 1818, 0, v4);
        }
        return 0;
    }
}

bool __cdecl NET_OutOfBandData(netsrc_t sock, netadr_t adr, const uint8_t *format, int len)
{
    int mbuf_20; // [esp+14h] [ebp-20h]
    int i; // [esp+28h] [ebp-Ch]
    int res; // [esp+2Ch] [ebp-8h]

    tempNetchanPacketBuf[0] = -1;
    tempNetchanPacketBuf[1] = -1;
    tempNetchanPacketBuf[2] = -1;
    tempNetchanPacketBuf[3] = -1;
    for (i = 0; i < len; ++i)
        tempNetchanPacketBuf[i + 4] = format[i];
    mbuf_20 = len + 4;
    if (showpackets->current.integer && (showpackets->current.integer > 1 || adr.type != NA_LOOPBACK))
        Com_DPrintf(16, "OOB Data->%u.%u.%u.%u: %i bytes\n", adr.ip[0], adr.ip[1], adr.ip[2], adr.ip[3], mbuf_20);
    res = (int)FakeLag_SendPacket(sock, mbuf_20, tempNetchanPacketBuf, adr) >= -1;

    if (sock == NS_SERVER)
        SV_Netchan_AddOOBProfilePacket(mbuf_20);
    else
        Net_AddClientOOBProfilePacket(sock, mbuf_20);
    return res > 0;
}

bool __cdecl NET_OutOfBandVoiceData(netsrc_t sock, netadr_t adr, uint8_t *format, uint32_t len)
{
    int mbuf_20; // [esp+14h] [ebp-1Ch]
    int res; // [esp+28h] [ebp-8h]

    tempNetchanPacketBuf[0] = -1;
    tempNetchanPacketBuf[1] = -1;
    tempNetchanPacketBuf[2] = -1;
    tempNetchanPacketBuf[3] = -1;
    if ((int)(len + 4) >= 0x20000)
        MyAssertHandler(
            ".\\qcommon\\net_chan_mp.cpp",
            1952,
            0,
            "%s",
            "len + 4 < static_cast<int>( sizeof( tempNetchanPacketBuf ) )");
    memcpy(&tempNetchanPacketBuf[4], format, len);
    mbuf_20 = len + 4;
    res = (int)FakeLag_SendPacket(sock, len + 4, tempNetchanPacketBuf, adr) >= -1;

    if (sock == NS_SERVER)
        SV_Netchan_AddOOBProfilePacket(mbuf_20);
    else
        Net_AddClientOOBProfilePacket(sock, mbuf_20);
    return res > 0;
}

int __cdecl NET_StringToAdr(char *s, netadr_t *a)
{
    char *v3; // eax
    __int16 v4; // ax
    uint16_t v5; // ax
    char base[1024]; // [esp+18h] [ebp-408h] BYREF
    char *port; // [esp+41Ch] [ebp-4h]

    if (!strcmp(s, "localhost"))
    {
        a->type = NA_BOT;
        *(uint32_t *)a->ip = 0;
        *(uint32_t *)&a->port = 0;
        *(uint32_t *)&a->ipx[2] = 0;
        *(uint32_t *)&a->ipx[6] = 0;
        a->type = NA_LOOPBACK;
        return 1;
    }
    else
    {
        I_strncpyz(base, s, 1024);
        v3 = strstr(base, ":");
        port = v3;
        if (v3)
            *port++ = 0;
        if (Sys_StringToAdr(base, a))
        {
            if (a->ip[0] == 255 && a->ip[1] == 255 && a->ip[2] == 255 && a->ip[3] == 255)
            {
                a->type = NA_BAD;
                return 0;
            }
            else
            {
                if (port)
                {
                    v4 = atoi(port);
                    v5 = BigShort(v4);
                }
                else
                {
                    v5 = BigShort(28960);
                }
                a->port = v5;
                return 1;
            }
        }
        else
        {
            a->type = NA_BAD;
            return 0;
        }
    }
}

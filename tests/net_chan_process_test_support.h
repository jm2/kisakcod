// Shared fixtures for the production Netchan_Process contracts.
//
// Split out of net_chan_process_tests.cpp so the span-capacity contracts can
// live in their own translation unit (net_chan_process_span_tests.cpp)
// without duplicating fixture state; the original single-TU layout tripped
// Codacy's per-file length limit. The contract semantics are unchanged --
// everything here is byte-for-byte the code that used to sit in the tests'
// anonymous namespace, hoisted into an inline namespace so both test TUs
// share one instance of the module-level buffers (C++17 inline variables).
//
// This header is only compiled on the ILP32 Win32 leg (see
// tests/CMakeLists.txt), alongside the production netchan TUs it declares.

#pragma once

#include <qcommon/net_chan_mp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace netchan_test
{
// MAX_MSGLEN-sized reassembly/destination buffers, as shipped.
inline constexpr int32_t kMaxMsgLen = 0x40000;

// The engine's fragment threshold: a fragment of exactly this many payload
// bytes means "more fragments expected".
inline constexpr int kFragmentFullLength = 1300;

inline constexpr uint16_t kQPort = 0x1234;

inline constexpr uint8_t kSentinel = 0xCD;

inline int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

// ---------------------------------------------------------------------------
// Inert dvars. Netchan_Process gates every print path on
// showpackets/showdrop and NetProf_PrepProfiling dereferences net_profile
// unconditionally, so the pointers (defined in net_chan_mp.cpp and left null
// until Netchan_Init runs) must point at benign zero-valued dvars before the
// first call. Keeping them at zero also holds the production print paths
// inert, exactly as in a default server.
// ---------------------------------------------------------------------------

inline dvar_t MakeInertDvar(const char *name)
{
    dvar_t dvar{};
    dvar.name = name;
    dvar.description = "net-chan reassembly test inert dvar";
    dvar.current.integer = 0;
    return dvar;
}

struct InertDvars
{
    dvar_t showpackets = MakeInertDvar("showpackets");
    dvar_t showdrop = MakeInertDvar("showdrop");
    dvar_t packetDebug = MakeInertDvar("packetDebug");
    dvar_t net_profile = MakeInertDvar("net_profile");
    dvar_t net_showprofile = MakeInertDvar("net_showprofile");
    dvar_t net_lanauthorize = MakeInertDvar("net_lanauthorize");
    dvar_t msg_printEntityNums = MakeInertDvar("msg_printEntityNums");
    dvar_t msg_dumpEnts = MakeInertDvar("msg_dumpEnts");
    dvar_t msg_hudelemspew = MakeInertDvar("msg_hudelemspew");
    dvar_t fakelag_target = MakeInertDvar("fakelag_target");
    dvar_t fakelag_packetloss = MakeInertDvar("fakelag_packetloss");
    dvar_t fakelag_currentjitter = MakeInertDvar("fakelag_currentjitter");
    dvar_t fakelag_jitter = MakeInertDvar("fakelag_jitter");
    dvar_t fakelag_current = MakeInertDvar("fakelag_current");
    dvar_t fakelag_jitterinterval = MakeInertDvar("fakelag_jitterinterval");
};

inline const InertDvars &Inert()
{
    static const InertDvars instance;
    return instance;
}

inline void InstallInertDvars()
{
    showpackets = &Inert().showpackets;
    showdrop = &Inert().showdrop;
    packetDebug = &Inert().packetDebug;
    net_profile = &Inert().net_profile;
    net_showprofile = &Inert().net_showprofile;
    net_lanauthorize = &Inert().net_lanauthorize;
    msg_printEntityNums = &Inert().msg_printEntityNums;
    msg_dumpEnts = &Inert().msg_dumpEnts;
    msg_hudelemspew = &Inert().msg_hudelemspew;
    fakelag_target = &Inert().fakelag_target;
    fakelag_packetloss = &Inert().fakelag_packetloss;
    fakelag_currentjitter = &Inert().fakelag_currentjitter;
    fakelag_jitter = &Inert().fakelag_jitter;
    fakelag_current = &Inert().fakelag_current;
    fakelag_jitterinterval = &Inert().fakelag_jitterinterval;
}

// ---------------------------------------------------------------------------
// Channel fixture: production Netchan_Setup over MAX_MSGLEN-sized buffers.
// ---------------------------------------------------------------------------

// MAX_MSGLEN-sized reassembly buffers shared by every fixture: production
// Netchan_Setup stores these pointers in each channel, so reassembly state
// written by one contract is observable through this module-level buffer
// (the out-of-order/recovery contracts assert surviving bytes across calls).
inline uint8_t g_fragmentBuffer[kMaxMsgLen];
inline uint8_t g_unsentBuffer[16];

struct ChanFixture
{
    netchan_t chan{};

    explicit ChanFixture(netsrc_t sock)
    {
        netadr_t address{};
        address.type = NA_IP;
        address.ip[0] = 127;
        address.ip[1] = 0;
        address.ip[2] = 0;
        address.ip[3] = 1;
        address.port = 28960;
        Netchan_Setup(sock, &chan, address, kQPort,
                      reinterpret_cast<char *>(g_unsentBuffer),
                      static_cast<int>(sizeof(g_unsentBuffer)),
                      reinterpret_cast<char *>(g_fragmentBuffer), kMaxMsgLen);
    }
};

// ---------------------------------------------------------------------------
// Wire-format packet builder. Uses the production MSG writers so the packet
// shape is pinned to the shipped protocol from the writing side as well.
// ---------------------------------------------------------------------------

struct PacketBuilder
{
    msg_t msg{};

    PacketBuilder(uint8_t *buffer, int size, uint32_t sequence, netsrc_t sock)
    {
        std::memset(buffer, kSentinel, static_cast<size_t>(size));
        MSG_Init(&msg, buffer, size);
        MSG_WriteLong(&msg, static_cast<int>(sequence));
        if (sock == NS_SERVER)
            MSG_WriteShort(&msg, static_cast<int16_t>(kQPort));
    }

    void AddFragmentHeader(uint32_t fragmentStart, uint16_t fragmentLength)
    {
        MSG_WriteLong(&msg, static_cast<int>(fragmentStart));
        MSG_WriteShort(&msg, static_cast<int16_t>(fragmentLength));
    }

    void AddPayload(const uint8_t *payload, int length)
    {
        MSG_WriteData(&msg, const_cast<uint8_t *>(payload),
                      static_cast<uint32_t>(length));
    }
};

inline uint32_t FragmentSequence(uint32_t sequence)
{
    return sequence | 0x80000000u;
}

// What production stores in chan->fragmentSequence: the wire sequence with
// the fragment bit masked off before recording.
inline uint32_t UnmaskedSequence(uint32_t sequence)
{
    return sequence & ~0x80000000u;
}

inline void FillPattern(uint8_t *buffer, int length, uint8_t seed)
{
    for (int i = 0; i < length; ++i)
        buffer[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
}

inline bool PatternMatches(const uint8_t *buffer, int length, uint8_t seed)
{
    for (int i = 0; i < length; ++i)
    {
        if (buffer[i] != static_cast<uint8_t>(seed + static_cast<uint8_t>(i)))
            return false;
    }
    return true;
}

inline bool LittleEndianPrefixMatches(const uint8_t *buffer, uint32_t sequence)
{
    return buffer[0] == static_cast<uint8_t>(sequence)
        && buffer[1] == static_cast<uint8_t>(sequence >> 8)
        && buffer[2] == static_cast<uint8_t>(sequence >> 16)
        && buffer[3] == static_cast<uint8_t>(sequence >> 24);
}
} // namespace netchan_test

// Defined in net_chan_process_span_tests.cpp: the stage-1 regression block
// (complete output span versus msg->maxsize, boundary capacities, no-write
// rejections). Invoked last by RunNetChanProcessContracts, preserving the
// original contract order.
int RunNetChanSpanContracts();

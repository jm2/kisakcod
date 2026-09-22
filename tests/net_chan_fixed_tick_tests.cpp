// Fixed-tick paired-channel simulation harness (contract 4, bead ki-zv15h).
//
// Split out of net_chan_capture_tick_tests.cpp so each contract group lives
// in its own translation unit; the original single-TU layout tripped Codacy's
// per-file length limit. The contract semantics are unchanged -- everything
// here is byte-for-byte the code that used to sit in the capture TU's
// anonymous namespace; the shared channel/capture machinery it uses lives in
// net_chan_capture_tick_support.h, and the large mutable statics stay at TU
// scope here exactly as before (the ILP32 stack is small; SimulationState
// embeds MAX_MSGLEN-sized channel buffers).
//
// This file is only compiled on the ILP32 Win32 leg (see
// tests/CMakeLists.txt); the loopback queue routing it relies on lives in
// the production net_chan_mp.cpp linked there.

#include "net_chan_capture_tick_support.h"

#include <cstdint>

using namespace netchan_test;

namespace
{
constexpr int kSimTicks = 8;

struct TickScript
{
    int serverSmall;
    int serverLarge;
    int clientSmall;
    int clientLarge;
};

// The 8-tick workload. Fragmenting messages start at tick 2 and 5 (server)
// and tick 6 (client) and pump exactly one fragment per tick afterwards --
// the engine's SV/CL_Netchan_TransmitNextFragment per-frame pacing. A side
// with fragments in flight cannot start a new message that tick, which is
// why the server's tick-8 small message is absent: its second large message
// is still pumping its final fragment.
constexpr TickScript kTickScript[kSimTicks] = {
    {24, 0, 40, 0},     {56, 2620, 72, 0}, {0, 0, 88, 0},
    {0, 0, 104, 0},     {61, 3901, 120, 0}, {0, 0, 136, 1500},
    {0, 0, 0, 0},       {0, 0, 152, 0},
};

// Per-tick drain outcomes pinned by the workload above: a mid-message
// fragment returns 0 from Netchan_Process (no complete message) and a
// completing fragment delivers.
const int kToClientDelivered[kSimTicks] = {1, 1, 0, 1, 1, 0, 0, 1};
const int kToClientIncomplete[kSimTicks] = {0, 1, 1, 0, 1, 1, 1, 0};
const int kToServerDelivered[kSimTicks] = {1, 1, 1, 1, 1, 1, 1, 1};
const int kToServerIncomplete[kSimTicks] = {0, 0, 0, 0, 0, 1, 0, 0};

struct SimulationState
{
    ChannelSide client;
    ChannelSide server;
    CaptureStore capture;
    PayloadLog toClientPayloads;
    PayloadLog toServerPayloads;
    DrainStats toClient[kSimTicks]{};
    DrainStats toServer[kSimTicks]{};
};

void SetupPair(SimulationState &sim)
{
    sim.client.Setup(NS_CLIENT1, 28961);
    sim.server.Setup(NS_SERVER, NS_CLIENT1);
    sim.capture = CaptureStore{};
    sim.toClientPayloads = PayloadLog{};
    sim.toServerPayloads = PayloadLog{};
}

uint8_t TickSeed(bool fromServer, int tick)
{
    return static_cast<uint8_t>((fromServer ? 0x10 : 0xB0) + tick * 7);
}

// Netchan_Transmit's documented maximum payload, at TU-scope static storage
// instead of as a function-local static (Codacy local-static finding; the
// va_buffers remediation in net_chan_process_test_stubs.cpp documents the
// idiom). One shared scratch object refilled per send, identical behavior --
// only the declaration location moved.
static uint8_t sendPayload[0x20000];

void SendPatterned(ChannelSide &side, int length, uint8_t seed)
{
    FillPattern(sendPayload, length, seed);
    Netchan_Transmit(&side.chan, length,
                     reinterpret_cast<char *>(sendPayload));
}

// One engine frame's sends: fragments in flight get exactly one
// TransmitNextFragment per tick; otherwise the small message goes first and
// a fragmenting message may be started (the engine's message-then-snapshot
// order).
void PumpSendTick(ChannelSide &side, int smallLength, int largeLength,
                  uint8_t seed)
{
    if (side.chan.unsentFragments)
    {
        Netchan_TransmitNextFragment(&side.chan);
        return;
    }
    if (smallLength > 0)
        SendPatterned(side, smallLength, seed);
    if (largeLength > 0)
        SendPatterned(side, largeLength, static_cast<uint8_t>(seed ^ 0x5A));
}

void RunTick(SimulationState &sim, int tick)
{
    const TickScript &script = kTickScript[tick];
    PumpSendTick(sim.server, script.serverSmall, script.serverLarge,
                 TickSeed(true, tick));
    PumpSendTick(sim.client, script.clientSmall, script.clientLarge,
                 TickSeed(false, tick));
    sim.toClient[tick] =
        sim.client.Drain(&sim.capture, &sim.toClientPayloads);
    sim.toServer[tick] =
        sim.server.Drain(&sim.capture, &sim.toServerPayloads);
}

void RunSimulation(SimulationState &sim)
{
    SetupPair(sim);
    for (int tick = 0; tick < kSimTicks; ++tick)
        RunTick(sim, tick);
}

bool PayloadLogsMatch(const PayloadLog &actual, const PayloadLog::Entry *expected,
                      int expectedCount)
{
    if (actual.count != expectedCount)
        return false;
    for (int i = 0; i < expectedCount; ++i)
    {
        if (actual.entries[i].length != expected[i].length
            || actual.entries[i].digest != expected[i].digest)
            return false;
    }
    return true;
}

uint32_t DigestPattern(uint8_t seed, int length)
{
    uint32_t hash = kFnvBasis;
    for (int i = 0; i < length; ++i)
    {
        hash ^= static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
        hash *= kFnvPrime;
    }
    return hash;
}

// The two independent simulation runs of the fixed-tick contracts, at
// TU-scope static storage instead of as function-local statics (Codacy
// local-static finding; the va_buffers remediation in
// net_chan_process_test_stubs.cpp documents the idiom -- SimulationState
// embeds MAX_MSGLEN-sized channel buffers that cannot live on the ILP32
// stack). Distinct objects; RunSimulation's SetupPair re-initializes each
// exactly as before.
static SimulationState fixedTickRunA;
static SimulationState fixedTickRunB;

// Per-tick drain outcomes and the captured frame count pinned by the
// scripted workload.
int FixedTickDrainStatsContract()
{
    for (int tick = 0; tick < kSimTicks; ++tick)
    {
        if (fixedTickRunA.toClient[tick].delivered != kToClientDelivered[tick]
            || fixedTickRunA.toClient[tick].incomplete
                   != kToClientIncomplete[tick]
            || fixedTickRunA.toServer[tick].delivered
                   != kToServerDelivered[tick]
            || fixedTickRunA.toServer[tick].incomplete
                   != kToServerIncomplete[tick])
            return fail("fixed-tick drain stats deviate from the workload");
    }
    if (fixedTickRunA.capture.overflowed
        || fixedTickRunA.capture.count != 19)
        return fail(
            "fixed-tick capture frame count deviates from the workload");
    return 0;
}

// Final channel state after the 8-tick run: every message delivered, no
// drops, no fragments left in flight.
int FixedTickChannelStateContract()
{
    if (fixedTickRunA.client.chan.incomingSequence != 5
        || fixedTickRunA.server.chan.incomingSequence != 8
        || fixedTickRunA.client.chan.dropped != 0
        || fixedTickRunA.server.chan.dropped != 0
        || fixedTickRunA.client.chan.unsentFragments
        || fixedTickRunA.server.chan.unsentFragments)
        return fail(
            "fixed-tick final channel state deviates from the workload");
    return 0;
}

// Every delivered body must be byte-identical to the scripted pattern, in
// delivery order: server smalls at ticks 1/2/5, the first large completing
// at tick 4, the second at tick 8; the client's smalls, its large completing
// at tick 7, and its final small at tick 8.
int FixedTickPayloadContract()
{
    const PayloadLog::Entry expectedToClient[5] = {
        {24, DigestPattern(TickSeed(true, 0), 24)},
        {56, DigestPattern(TickSeed(true, 1), 56)},
        {2620, DigestPattern(static_cast<uint8_t>(TickSeed(true, 1) ^ 0x5A), 2620)},
        {61, DigestPattern(TickSeed(true, 4), 61)},
        {3901, DigestPattern(static_cast<uint8_t>(TickSeed(true, 4) ^ 0x5A), 3901)},
    };
    const PayloadLog::Entry expectedToServer[8] = {
        {40, DigestPattern(TickSeed(false, 0), 40)},
        {72, DigestPattern(TickSeed(false, 1), 72)},
        {88, DigestPattern(TickSeed(false, 2), 88)},
        {104, DigestPattern(TickSeed(false, 3), 104)},
        {120, DigestPattern(TickSeed(false, 4), 120)},
        {136, DigestPattern(TickSeed(false, 5), 136)},
        {1500, DigestPattern(static_cast<uint8_t>(TickSeed(false, 5) ^ 0x5A), 1500)},
        {152, DigestPattern(TickSeed(false, 7), 152)},
    };
    if (!PayloadLogsMatch(fixedTickRunA.toClientPayloads, expectedToClient, 5)
        || !PayloadLogsMatch(fixedTickRunA.toServerPayloads, expectedToServer,
                             8))
        return fail("a delivered payload deviates from the scripted body");

    // Determinism: two independent runs must produce byte-identical traces.
    if (fixedTickRunA.capture.digest != fixedTickRunB.capture.digest
        || !PayloadLogsMatch(fixedTickRunB.toClientPayloads, expectedToClient,
                             5)
        || !PayloadLogsMatch(fixedTickRunB.toServerPayloads, expectedToServer,
                             8))
        return fail("fixed-tick simulation traces are not deterministic");

    return 0;
}

int FixedTickContracts()
{
    InstallInertDvars();
    RunSimulation(fixedTickRunA);
    RunSimulation(fixedTickRunB);

    if (FixedTickDrainStatsContract() != 0)
        return 1;
    if (FixedTickChannelStateContract() != 0)
        return 1;
    return FixedTickPayloadContract();
}
} // namespace

// Contract 4: the fixed-tick paired-channel simulation. Invoked last by
// RunNetChanCaptureTickContracts (net_chan_capture_tick_tests.cpp),
// preserving the original contract order.
int RunNetChanFixedTickContracts()
{
    return FixedTickContracts();
}

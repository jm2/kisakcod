// Netchan-level capture validation and the fixed-tick simulation harness
// (bead ki-zv15h).
//
// The earlier stages validated Netchan_Process against hand-built packets,
// and the unmerged net-capture-certification design (ki-dyqxl) validated
// Codec-level capture round-trips. The remaining gap is at the NETCHAN
// level, and this file closes it on the engine's actual ILP32 leg:
//
//   1. Capture fidelity: a scripted send session runs through PRODUCTION
//      Netchan_Transmit / Netchan_TransmitNextFragment into the engine's
//      loopback queues; the receiving side's drain records the wire frames
//      as a capture whose shape is pinned exactly (frame counts, sizes,
//      sequence fields, fragment offsets, payload slices).
//   2. Capture replay: the recorded frames are replayed through
//      Netchan_Process on a fresh channel and must reproduce the identical
//      delivery outcome -- a replayed capture is indistinguishable from live
//      wire bytes at the production interface.
//   3. Lossy capture replay: a capture with a missing mid-message fragment
//      must reject cleanly and hold exact reassembly state; replaying the
//      retransmission frames (as they appear on a real wire capture)
//      completes the message byte-exactly.
//   4. Fixed-tick simulation: a paired client<->server channel set runs a
//      scripted 8-tick workload where a fragmenting message pumps exactly
//      one fragment per tick (SV/CL_Netchan_TransmitNextFragment pacing),
//      both directions are validated per tick, and two independent runs
//      must produce byte-identical traces.
//
// Every packet flows through production code only: Netchan_Transmit emits
// into the loopback queues (the inert fakelag dvars keep FakeLag_SendPacket
// on the direct NET_SendPacket path and fakelagInitialized stays false, so
// NET_GetLoopPacket drains the real queues), and Netchan_Process validates
// the receive side. Sys_Milliseconds is stubbed to a constant by
// net_chan_process_test_stubs.cpp; there is no wall clock and no RNG, so
// runs are fully deterministic.
//
// This file is only compiled on the ILP32 Win32 leg (see
// tests/CMakeLists.txt); the loopback queue routing it relies on lives in
// the production net_chan_mp.cpp linked there.

#include "net_chan_process_test_support.h"

#include <algorithm>
#include <cstdint>

using namespace netchan_test;

namespace
{
// loopmsg_t's data capacity: production NET_SendLoopPacket rejects frames
// larger than this, so no captured wire frame can exceed it.
constexpr int kLoopbackFrameMax = 1400;

constexpr int kSimTicks = 8;

// FNV-1a over a byte range: the trace equality primitive for both captured
// wire frames and delivered payload bodies.
constexpr uint32_t kFnvBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

uint32_t DigestBytes(const uint8_t *bytes, int length)
{
    uint32_t hash = kFnvBasis;
    for (int i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
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

struct CapturedFrame
{
    netsrc_t sock;
    int32_t length;
    uint8_t data[kLoopbackFrameMax];
};

struct CaptureStore
{
    CapturedFrame frames[64];
    int count = 0;
    uint32_t digest = kFnvBasis;
    bool overflowed = false;

    void Record(netsrc_t source, const uint8_t *bytes, int length)
    {
        digest ^= static_cast<uint32_t>(source);
        digest *= kFnvPrime;
        digest ^= static_cast<uint32_t>(length);
        digest *= kFnvPrime;
        digest ^= DigestBytes(bytes, length);
        digest *= kFnvPrime;
        if (count >= static_cast<int>(
                         sizeof(frames) / sizeof(frames[0])))
        {
            overflowed = true;
            return;
        }
        if (length > static_cast<int>(sizeof(frames[0].data)))
        {
            overflowed = true;
            return;
        }
        CapturedFrame &frame = frames[count++];
        frame.sock = source;
        frame.length = length;
        // std::copy instead of memcpy (Codacy CWE-120): byte-wise identical for
        // memcpy's non-overlapping contract, without the analyzer-untrackable
        // raw length copy.
        std::copy(bytes, bytes + static_cast<size_t>(length), frame.data);
    }
};

// Delivered payload bodies in delivery order: (length, FNV-1a) pairs the
// fixed-tick contract compares against the scripted workload's expected
// pattern digests.
struct PayloadLog
{
    struct Entry
    {
        int length;
        uint32_t digest;
    };

    Entry entries[32];
    int count = 0;

    void Append(int length, uint32_t digest)
    {
        if (count >= static_cast<int>(sizeof(entries) / sizeof(entries[0])))
            return;
        entries[count].length = length;
        entries[count].digest = digest;
        ++count;
    }
};

struct DrainStats
{
    // Netchan_Process's return value: 1 delivers a complete message, 0 means
    // "no complete message from this frame" -- both a pending mid-message
    // fragment and a genuine rejection (gap, illegal fragment) return 0.
    int delivered;
    int incomplete;
};

// One engine endpoint: a production channel plus its private buffers.
// Buffers are members (static storage at the call sites) because the ILP32
// stack is 1 MiB and the reassembly/recv buffers are MAX_MSGLEN-sized.
struct ChannelSide
{
    netchan_t chan{};
    uint8_t unsent[0x8000];
    uint8_t fragment[kMaxMsgLen];
    uint8_t recv[kMaxMsgLen];
    msg_t drainMessage{};

    void Setup(netsrc_t sock, uint16_t remotePort)
    {
        netadr_t address{};
        address.type = NA_LOOPBACK;
        address.port = remotePort;
        Netchan_Setup(sock, &chan, address, kQPort,
                      reinterpret_cast<char *>(unsent),
                      static_cast<int>(sizeof(unsent)),
                      reinterpret_cast<char *>(fragment), kMaxMsgLen);
        MSG_Init(&drainMessage, recv, kMaxMsgLen);
    }

    // Drains every pending loopback frame for this side through production
    // Netchan_Process, recording wire frames and delivered payload bodies
    // when sinks are provided. Mirrors the engine's packet pump shape.
    DrainStats Drain(CaptureStore *capture, PayloadLog *delivered)
    {
        DrainStats stats{};
        netadr_t from{};
        while (NET_GetLoopPacket(chan.sock, &from, &drainMessage))
        {
            if (capture)
                capture->Record(chan.sock, drainMessage.data,
                                drainMessage.cursize);
            if (Netchan_Process(&chan, &drainMessage))
            {
                ++stats.delivered;
                // A completion rebuilds the message as prefix + payload with
                // the reader at 4; an unfragmented delivery leaves the
                // reader after the sequence (and qport, server side). Either
                // way the body is [readcount, cursize).
                if (delivered)
                    delivered->Append(
                        drainMessage.cursize - drainMessage.readcount,
                        DigestBytes(drainMessage.data + drainMessage.readcount,
                                    drainMessage.cursize
                                        - drainMessage.readcount));
            }
            else
            {
                ++stats.incomplete;
            }
        }
        return stats;
    }
};

// Replays recorded wire frames through Netchan_Process on a channel that
// never touched the live queues: the capture-replay shape.
DrainStats ReplayFrames(netchan_t *chan, const CapturedFrame *frames,
                        int count, uint8_t *scratch, int scratchSize)
{
    DrainStats stats{};
    msg_t message{};
    for (int i = 0; i < count; ++i)
    {
        if (frames[i].length > scratchSize)
        {
            ++stats.incomplete;
            continue;
        }
        // Mirror NET_GetLoopPacket_Real's shape: frame bytes land in a
        // writable MAX_MSGLEN-sized buffer, cursize carries the frame
        // length, and Netchan_Process's MSG_BeginReading resets the reader.
        MSG_Init(&message, scratch, scratchSize);
        // std::copy instead of memcpy (Codacy CWE-120): byte-wise identical for
        // memcpy's non-overlapping contract, without the analyzer-untrackable
        // raw length copy.
        std::copy(frames[i].data,
                  frames[i].data + static_cast<size_t>(frames[i].length),
                  scratch);
        message.cursize = frames[i].length;
        if (Netchan_Process(chan, &message))
            ++stats.delivered;
        else
            ++stats.incomplete;
    }
    return stats;
}

bool RequireFrameHeader(const CapturedFrame &frame, uint32_t sequence,
                        int32_t fragmentStart, int32_t fragmentLength,
                        const uint8_t *payload, int payloadLength)
{
    const int headerLength = fragmentLength != 0 ? 10 : 4;
    if (frame.length != headerLength + payloadLength)
        return false;
    if (!LittleEndianPrefixMatches(frame.data, sequence))
        return false;
    if (fragmentLength != 0)
    {
        if (!LittleEndianPrefixMatches(frame.data + 4,
                                       static_cast<uint32_t>(fragmentStart)))
            return false;
        if (frame.data[8] != static_cast<uint8_t>(fragmentLength)
            || frame.data[9] != static_cast<uint8_t>(fragmentLength >> 8))
            return false;
    }
    return std::memcmp(frame.data + headerLength, payload,
                       static_cast<size_t>(payloadLength)) == 0;
}
} // namespace

// ---------------------------------------------------------------------------
// Contracts 1-3: capture fidelity, clean replay, lossy replay + recovery.
// ---------------------------------------------------------------------------

namespace
{
// Shared fixtures for the capture-fidelity contracts, at TU-scope static
// storage instead of as function-local statics (Codacy local-static finding;
// the va_buffers remediation in net_chan_process_test_stubs.cpp documents
// the idiom): the ILP32 stack is small, and the capture store is ~90 KB with
// MAX_MSGLEN-sized scratch, so these objects cannot live on the stack. One
// named field per original function-local static keeps every object distinct
// exactly as before -- hoisting distinct same-named statics under one shared
// name would merge test state -- and each contract re-initializes what it
// consumes exactly as it did at its old call site.
struct CaptureFidelityState
{
    ChannelSide client;    // live drain endpoint (contract 1)
    ChannelSide server;    // scripted send session (contract 1)
    CaptureStore capture;  // ~90 KB: static storage, ILP32 stack is small
    uint8_t small[64];
    uint8_t large[3000];
    ChannelSide replay; // clean-replay channel (contract 2)
    uint8_t replayScratch[kMaxMsgLen]; // shared replay scratch (contracts 2-3)
    CaptureStore lossy;    // lossy capture (contract 3)
    ChannelSide lossyChan; // lossy-replay channel (contract 3)
};

static CaptureFidelityState fidelity;

// Drives the production send path over the scripted session: one
// unfragmented small message plus one 3000-byte fragmented message, then
// pins the resulting outgoingSequence.
int SendCapturedSession()
{
    Netchan_Transmit(&fidelity.server.chan,
                     static_cast<int>(sizeof(fidelity.small)),
                     reinterpret_cast<char *>(fidelity.small));
    Netchan_Transmit(&fidelity.server.chan,
                     static_cast<int>(sizeof(fidelity.large)),
                     reinterpret_cast<char *>(fidelity.large));
    while (fidelity.server.chan.unsentFragments)
        Netchan_TransmitNextFragment(&fidelity.server.chan);
    if (fidelity.server.chan.outgoingSequence != 3)
        return fail("fragmented session left the wrong outgoingSequence");
    return 0;
}

// Pins the captured wire shape: the unfragmented small frame and the three
// fragment frames of the large message.
int CaptureFrameShapesMatch()
{
    if (!RequireFrameHeader(fidelity.capture.frames[0], 1, 0, 0,
                            fidelity.small,
                            static_cast<int>(sizeof(fidelity.small))))
        return fail("unfragmented capture frame deviates from the wire shape");
    struct FragmentShape
    {
        int32_t start;
        int32_t length;
    };
    const FragmentShape shapes[3] = {{0, 1300}, {1300, 1300}, {2600, 400}};
    for (int i = 0; i < 3; ++i)
    {
        if (!RequireFrameHeader(fidelity.capture.frames[1 + i],
                                FragmentSequence(2u), shapes[i].start,
                                shapes[i].length,
                                fidelity.large + shapes[i].start,
                                shapes[i].length))
            return fail("fragment capture frame deviates from the wire shape");
    }
    return 0;
}

// The completing fragment rebuilds the message in the drain buffer:
// sequence prefix + byte-exact reassembled payload.
int LiveReassemblyMatches()
{
    if (fidelity.client.drainMessage.cursize != 3004
        || !LittleEndianPrefixMatches(fidelity.client.recv, 2u)
        || !PatternMatches(fidelity.client.recv + 4, 3000, 0x80))
        return fail("live reassembly payload deviates from the sent body");
    return 0;
}

// Contract 1: the production send path emits the exact wire shape.
int CaptureWireShapeContract()
{
    fidelity.client.Setup(NS_CLIENT1, 28961); // client sends route to the server queue
    fidelity.server.Setup(NS_SERVER, NS_CLIENT1); // to.port 0 == the client queue

    const int sent = SendCapturedSession();
    if (sent != 0)
        return sent;

    const DrainStats liveStats =
        fidelity.client.Drain(&fidelity.capture, nullptr);
    if (fidelity.capture.overflowed || fidelity.capture.count != 4)
        return fail("capture frame count deviates from the scripted session");
    // The two pending mid-message fragments return 0 as well.
    if (liveStats.delivered != 2 || liveStats.incomplete != 2)
        return fail("live drain outcome deviates from the scripted session");

    const int shapes = CaptureFrameShapesMatch();
    if (shapes != 0)
        return shapes;

    if (fidelity.client.chan.incomingSequence != 2
        || fidelity.client.chan.dropped != 0)
        return fail("live channel state deviates after the session");

    return LiveReassemblyMatches();
}

// Contract 2: replaying the capture into a fresh channel reproduces the
// identical delivery outcome.
int CleanReplayContract()
{
    fidelity.replay.Setup(NS_CLIENT1, 28961);
    const DrainStats replayStats =
        ReplayFrames(&fidelity.replay.chan, fidelity.capture.frames,
                     fidelity.capture.count, fidelity.replayScratch,
                     kMaxMsgLen);
    if (replayStats.delivered != 2 || replayStats.incomplete != 2
        || fidelity.replay.chan.incomingSequence != 2
        || fidelity.replay.chan.dropped != 0)
        return fail("capture replay outcome deviates from the live drain");
    if (!PatternMatches(fidelity.replay.recv + 4, 3000, 0x80))
        return fail("replayed reassembly payload deviates from the sent body");
    return 0;
}

// Contract 3, lossy leg: a capture missing the second fragment must reject
// the tail and hold exact reassembly state.
int LossyReplayContract()
{
    fidelity.lossy = CaptureStore{};
    for (int i = 0; i < fidelity.capture.count; ++i)
    {
        if (i == 2) // the fragment covering bytes [1300, 2600)
            continue;
        fidelity.lossy.Record(fidelity.capture.frames[i].sock,
                              fidelity.capture.frames[i].data,
                              fidelity.capture.frames[i].length);
    }
    fidelity.lossyChan.Setup(NS_CLIENT1, 28961);
    const DrainStats lossyStats =
        ReplayFrames(&fidelity.lossyChan.chan, fidelity.lossy.frames,
                     fidelity.lossy.count, fidelity.replayScratch,
                     kMaxMsgLen);
    // f1 pending plus the gapped f3 both return 0.
    if (lossyStats.delivered != 1 || lossyStats.incomplete != 2
        || fidelity.lossyChan.chan.incomingSequence != 1
        || fidelity.lossyChan.chan.fragmentSequence != 2
        || fidelity.lossyChan.chan.fragmentLength != 1300)
        return fail("lossy capture replay state deviates from wire semantics");
    return 0;
}

// Contract 3, recovery leg: replaying the retransmission frames (as they
// appear on a real wire capture) completes the message byte-exactly.
int LossyRecoveryContract()
{
    const DrainStats recoveryStats =
        ReplayFrames(&fidelity.lossyChan.chan, &fidelity.capture.frames[2], 2,
                     fidelity.replayScratch, kMaxMsgLen);
    if (recoveryStats.delivered != 1 || recoveryStats.incomplete != 1
        || fidelity.lossyChan.chan.incomingSequence != 2
        || fidelity.lossyChan.chan.fragmentLength != 0
        || !PatternMatches(fidelity.lossyChan.recv + 4, 3000, 0x80))
        return fail("retransmission replay did not complete the message");
    return 0;
}

int CaptureFidelityContracts()
{
    InstallInertDvars();
    FillPattern(fidelity.small, static_cast<int>(sizeof(fidelity.small)),
                0x40);
    FillPattern(fidelity.large, static_cast<int>(sizeof(fidelity.large)),
                0x80);

    if (CaptureWireShapeContract() != 0)
        return 1;
    if (CleanReplayContract() != 0)
        return 1;
    if (LossyReplayContract() != 0)
        return 1;
    return LossyRecoveryContract();
}
} // namespace

// ---------------------------------------------------------------------------
// Contract 4: the fixed-tick paired-channel simulation harness.
// ---------------------------------------------------------------------------

namespace
{
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

// Defined in net_chan_process_tests.cpp and wired from
// net_chan_reassembly_tests.cpp main under
// KISAKCOD_NET_CHAN_PROCESS_TESTS_AVAILABLE: runs after the process and
// span contracts.
int RunNetChanCaptureTickContracts()
{
    if (CaptureFidelityContracts() != 0)
        return 1;
    if (FixedTickContracts() != 0)
        return 1;
    return 0;
}

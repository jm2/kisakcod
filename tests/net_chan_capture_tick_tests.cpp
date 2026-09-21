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
// The shared channel/capture machinery lives in
// net_chan_capture_tick_support.h, and the fixed-tick simulation harness
// (contract 4) lives in its own translation unit,
// net_chan_fixed_tick_tests.cpp: the original single-TU layout tripped
// Codacy's per-file length limit. The contract semantics are unchanged --
// every assertion, digest, ordering check and failure path is byte-for-byte
// the code that passed the earlier rounds.
//
// This file is only compiled on the ILP32 Win32 leg (see
// tests/CMakeLists.txt); the loopback queue routing it relies on lives in
// the production net_chan_mp.cpp linked there.

#include "net_chan_process_test_support.h"
#include "net_chan_capture_tick_support.h"

#include <cstdint>

using namespace netchan_test;

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
        ReplayFrames(fidelity.replay, fidelity.capture.frames,
                     fidelity.capture.count);
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
        ReplayFrames(fidelity.lossyChan, fidelity.lossy.frames,
                     fidelity.lossy.count);
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
        ReplayFrames(fidelity.lossyChan, &fidelity.capture.frames[2], 2);
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

// Declared in net_chan_reassembly_tests.cpp and wired from its main under
// KISAKCOD_NET_CHAN_PROCESS_TESTS_AVAILABLE: runs after the process and
// span contracts. Invokes the fixed-tick simulation contracts
// (net_chan_fixed_tick_tests.cpp) last, preserving the original contract
// order.
int RunNetChanCaptureTickContracts()
{
    if (CaptureFidelityContracts() != 0)
        return 1;
    if (RunNetChanFixedTickContracts() != 0)
        return 1;
    return 0;
}

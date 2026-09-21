// Shared machinery for the netchan-level capture-fidelity and fixed-tick
// simulation contracts (bead ki-zv15h).
//
// Split out of net_chan_capture_tick_tests.cpp so the fixed-tick simulation
// harness can live in its own translation unit (net_chan_fixed_tick_tests.cpp)
// without duplicating fixture state; the original single-TU layout tripped
// Codacy's per-file length limit. The contract semantics are unchanged --
// everything here is byte-for-byte the code that used to sit in the capture
// TU's anonymous namespace, hoisted into inline definitions so both test TUs
// share one instance (C++17 inline functions/variables). The large mutable
// statics deliberately stay at TU scope in their consumers (see
// CaptureFidelityState in net_chan_capture_tick_tests.cpp and sendPayload /
// fixedTickRunA/B in net_chan_fixed_tick_tests.cpp): they are distinct
// objects re-initialized by each contract exactly as before.
//
// This header is only compiled on the ILP32 Win32 leg (see
// tests/CMakeLists.txt), alongside the production netchan TUs it declares.

#pragma once

#include "net_chan_process_test_support.h"

#include <algorithm>
#include <cstdint>

namespace netchan_test
{
// loopmsg_t's data capacity: production NET_SendLoopPacket rejects frames
// larger than this, so no captured wire frame can exceed it.
inline constexpr int kLoopbackFrameMax = 1400;

// FNV-1a over a byte range: the trace equality primitive for both captured
// wire frames and delivered payload bodies.
inline constexpr uint32_t kFnvBasis = 2166136261u;
inline constexpr uint32_t kFnvPrime = 16777619u;

inline uint32_t DigestBytes(const uint8_t *bytes, int length)
{
    uint32_t hash = kFnvBasis;
    for (int i = 0; i < length; ++i)
    {
        hash ^= bytes[i];
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
inline DrainStats ReplayFrames(netchan_t *chan, const CapturedFrame *frames,
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

inline bool RequireFrameHeader(const CapturedFrame &frame, uint32_t sequence,
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
} // namespace netchan_test

// Defined in net_chan_fixed_tick_tests.cpp: contract 4, the paired-channel
// fixed-tick simulation. Invoked last by RunNetChanCaptureTickContracts,
// preserving the original contract order.
int RunNetChanFixedTickContracts();

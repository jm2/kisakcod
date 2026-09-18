// Netchan_Process fragment-reassembly contracts (production function).
//
// Issue #123, stage 2: the predicate-level contracts in
// net_chan_reassembly_tests.cpp pin Netchan_ReassembledSpanFits at compile
// time on every target. This file drives the PRODUCTION Netchan_Process
// itself -- the same way the engine calls it -- over wire-format packets
// built with the production MSG writers, covering:
//
//   * valid complete and fragmented messages (both socks, qport placement),
//   * fragment ordering, duplicate/old sequences and gap accounting,
//   * invalid input (illegal/negative fragment lengths, truncated fragment
//     headers) and the resulting failure state.
//
// The span-capacity block of the original single-TU layout (boundary
// destination capacities around the complete output span versus msg->maxsize,
// and the stage-1 no-write rejection regression) now lives in
// net_chan_process_span_tests.cpp, sharing these fixtures through
// net_chan_process_test_support.h; Codacy's per-file length limit drove the
// split. RunNetChanProcessContracts invokes the span contracts last, so the
// original contract order is unchanged.
//
// The production TUs (qcommon/net_chan_mp.cpp, qcommon/msg_mp.cpp,
// qcommon/huffman.cpp) are linked into this target only on the ILP32 Win32
// leg (the engine's actual target); elsewhere this file is not compiled and
// the target keeps its predicate-only shape. No production logic is copied
// or reimplemented here, and the wire bytes are written with the production
// MSG writers so the packet shape stays pinned to the shipped protocol.

#include "net_chan_process_test_support.h"

#include <cstdint>

using namespace netchan_test;

namespace
{
// ---------------------------------------------------------------------------
// 1. Valid complete (unfragmented) messages on both socks.
// ---------------------------------------------------------------------------

bool unfragmentedServerPacketAccepted()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[64];
    uint8_t payload[3];
    FillPattern(payload, 3, 0x40);

    PacketBuilder packet(storage, static_cast<int>(sizeof(storage)), 1u,
                         NS_SERVER);
    packet.AddPayload(payload, 3);

    if (Netchan_Process(&fixture.chan, &packet.msg) != 1)
        return false;
    if (fixture.chan.incomingSequence != 1)
        return false;
    if (fixture.chan.dropped != 0)
        return false;
    // The unfragmented path must not touch the packet bytes: the payload
    // stays where the wire put it (after sequence and qport).
    if (!PatternMatches(&storage[6], 3, 0x40))
        return false;
    if (packet.msg.readcount != 6)
        return false;
    return true;
}

bool unfragmentedClientPacketAccepted()
{
    ChanFixture fixture(NS_CLIENT1);
    uint8_t storage[64];
    uint8_t payload[5];
    FillPattern(payload, 5, 0x80);

    // NS_CLIENT1 packets carry no qport field: the payload starts right
    // after the sequence.
    PacketBuilder packet(storage, static_cast<int>(sizeof(storage)), 7u,
                         NS_CLIENT1);
    packet.AddPayload(payload, 5);

    if (Netchan_Process(&fixture.chan, &packet.msg) != 1)
        return false;
    if (fixture.chan.incomingSequence != 7)
        return false;
    if (!PatternMatches(&storage[4], 5, 0x80))
        return false;
    if (packet.msg.readcount != 4)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// 2. Sequence/reliable semantics: gap accounting and ordering.
// ---------------------------------------------------------------------------

bool gapAccounted()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[64];

    PacketBuilder packet(storage, static_cast<int>(sizeof(storage)), 5u,
                         NS_SERVER);
    if (Netchan_Process(&fixture.chan, &packet.msg) != 1)
        return false;
    // Packets 1..4 never arrived: dropped must report them.
    if (fixture.chan.dropped != 4)
        return false;
    if (fixture.chan.incomingSequence != 5)
        return false;
    return true;
}

bool duplicateSequenceRejected()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[64];

    PacketBuilder first(storage, static_cast<int>(sizeof(storage)), 2u,
                        NS_SERVER);
    if (Netchan_Process(&fixture.chan, &first.msg) != 1)
        return false;

    PacketBuilder duplicate(storage, static_cast<int>(sizeof(storage)), 2u,
                            NS_SERVER);
    if (Netchan_Process(&fixture.chan, &duplicate.msg) != 0)
        return false;
    if (fixture.chan.incomingSequence != 2)
        return false;
    return true;
}

bool oldSequenceRejected()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[64];

    PacketBuilder current(storage, static_cast<int>(sizeof(storage)), 3u,
                          NS_SERVER);
    if (Netchan_Process(&fixture.chan, &current.msg) != 1)
        return false;

    PacketBuilder late(storage, static_cast<int>(sizeof(storage)), 1u,
                       NS_SERVER);
    if (Netchan_Process(&fixture.chan, &late.msg) != 0)
        return false;
    if (fixture.chan.incomingSequence != 3)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// 3. Valid fragmented reassembly: ordering, content, prefix, sequence state.
//    Split into phase helpers to stay under Codacy's per-function complexity
//    and length limits; the phases chain in order and preserve every
//    assertion of the original single-function contract.
// ---------------------------------------------------------------------------

// Bootstrap plus fragment 1: pending reassembly state after a full-length
// fragment, before the sequence itself has advanced.
bool firstFragmentLeavesPendingReassembly(ChanFixture &fixture,
                                          uint8_t *storage,
                                          const uint8_t *firstPayload)
{
    // Advance the sequence so the reassembly itself has no gap.
    PacketBuilder bootstrap(storage, kMaxMsgLen, 99u, NS_SERVER);
    if (Netchan_Process(&fixture.chan, &bootstrap.msg) != 1)
        return false;

    // Fragment 1: full-length fragment, reassembly continues. The sequence
    // advances only when the message completes, so incomingSequence is still
    // 99 here.
    PacketBuilder fragment1(storage, kMaxMsgLen, FragmentSequence(100u),
                            NS_SERVER);
    fragment1.AddFragmentHeader(0u, kFragmentFullLength);
    fragment1.AddPayload(firstPayload, kFragmentFullLength);
    if (Netchan_Process(&fixture.chan, &fragment1.msg) != 0)
        return false;
    if (fixture.chan.incomingSequence != 99)
        return false;
    if (fixture.chan.fragmentLength != kFragmentFullLength)
        return false;
    if (!PatternMatches(g_fragmentBuffer, kFragmentFullLength, 0x10))
        return false;
    return true;
}

// Fragment 2 completes the message: production returns it with the complete
// output span -- four-byte prefix plus the reassembled payload.
bool completingFragmentWritesFullSpan(ChanFixture &fixture, uint8_t *storage,
                                      const uint8_t *secondPayload,
                                      msg_t *completingPacket)
{
    PacketBuilder fragment2(storage, kMaxMsgLen, FragmentSequence(100u),
                            NS_SERVER);
    fragment2.AddFragmentHeader(kFragmentFullLength, 100u);
    fragment2.AddPayload(secondPayload, 100);
    if (Netchan_Process(&fixture.chan, &fragment2.msg) != 1)
        return false;

    if (fragment2.msg.cursize != kFragmentFullLength + 100 + 4)
        return false;
    if (!LittleEndianPrefixMatches(storage, UnmaskedSequence(100u)))
        return false;
    if (!PatternMatches(&storage[4], kFragmentFullLength, 0x10))
        return false;
    if (!PatternMatches(&storage[4 + kFragmentFullLength], 100, 0x90))
        return false;
    *completingPacket = fragment2.msg;
    return true;
}

// Sequence/reliable state after completion, plus the reader position the
// production completion leaves behind.
bool completionAdvancesSequenceAndState(ChanFixture &fixture,
                                        msg_t *completingPacket,
                                        const uint8_t *firstPayload)
{
    if (fixture.chan.incomingSequence != 100)
        return false;
    if (fixture.chan.dropped != 0)
        return false;
    if (fixture.chan.fragmentLength != 0)
        return false;

    // The production completion leaves the reader positioned past the
    // prefix: the next read yields the first payload long.
    const int expectedFirstLong = static_cast<int>(firstPayload[0])
        | (static_cast<int>(firstPayload[1]) << 8)
        | (static_cast<int>(firstPayload[2]) << 16)
        | (static_cast<int>(firstPayload[3]) << 24);
    if (MSG_ReadLong(completingPacket) != expectedFirstLong)
        return false;
    if (completingPacket->readcount != 8)
        return false;
    return true;
}

bool fragmentedReassemblyAccepted()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[kMaxMsgLen];
    uint8_t firstPayload[kFragmentFullLength];
    uint8_t secondPayload[100];
    FillPattern(firstPayload, kFragmentFullLength, 0x10);
    FillPattern(secondPayload, 100, 0x90);

    if (!firstFragmentLeavesPendingReassembly(fixture, storage, firstPayload))
        return false;
    msg_t completingPacket{};
    if (!completingFragmentWritesFullSpan(fixture, storage, secondPayload,
                                          &completingPacket))
        return false;
    return completionAdvancesSequenceAndState(fixture, &completingPacket,
                                              firstPayload);
}

bool singleFragmentKeepsReassemblyState()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[kMaxMsgLen];
    uint8_t payload[kFragmentFullLength];
    FillPattern(payload, kFragmentFullLength, 0x20);

    PacketBuilder fragment(storage, kMaxMsgLen, FragmentSequence(3u),
                           NS_SERVER);
    fragment.AddFragmentHeader(0u, kFragmentFullLength);
    fragment.AddPayload(payload, kFragmentFullLength);

    if (Netchan_Process(&fixture.chan, &fragment.msg) != 0)
        return false;
    if (fixture.chan.fragmentLength != kFragmentFullLength)
        return false;
    if (fixture.chan.incomingSequence != 0)
        return false;
    // The gap against the still-pending message is reported immediately,
    // even though the sequence itself has not advanced yet.
    if (fixture.chan.dropped != 2)
        return false;
    if (!PatternMatches(g_fragmentBuffer, kFragmentFullLength, 0x20))
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// 4. Fragment ordering enforcement and recovery.
// ---------------------------------------------------------------------------

bool outOfOrderFragmentRejectedAndRecovers()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[kMaxMsgLen];
    uint8_t payload[kFragmentFullLength];
    FillPattern(payload, kFragmentFullLength, 0x30);

    PacketBuilder fragment1(storage, kMaxMsgLen, FragmentSequence(9u),
                            NS_SERVER);
    fragment1.AddFragmentHeader(0u, kFragmentFullLength);
    fragment1.AddPayload(payload, kFragmentFullLength);
    if (Netchan_Process(&fixture.chan, &fragment1.msg) != 0)
        return false;

    // Wrong offset for the accumulated length: rejected, and the buffered
    // fragment data plus its length must survive untouched.
    uint8_t stray[16];
    FillPattern(stray, 16, 0x50);
    PacketBuilder fragmentWrong(storage, kMaxMsgLen, FragmentSequence(9u),
                                NS_SERVER);
    fragmentWrong.AddFragmentHeader(5u, 16u);
    fragmentWrong.AddPayload(stray, 16);
    if (Netchan_Process(&fixture.chan, &fragmentWrong.msg) != 0)
        return false;
    if (fixture.chan.fragmentLength != kFragmentFullLength)
        return false;

    // The correct continuation still completes the message.
    uint8_t tail[32];
    FillPattern(tail, 32, 0x70);
    PacketBuilder fragment2(storage, kMaxMsgLen, FragmentSequence(9u),
                            NS_SERVER);
    fragment2.AddFragmentHeader(kFragmentFullLength, 32u);
    fragment2.AddPayload(tail, 32);
    if (Netchan_Process(&fixture.chan, &fragment2.msg) != 1)
        return false;
    if (fragment2.msg.cursize != kFragmentFullLength + 32 + 4)
        return false;
    if (!PatternMatches(&storage[4], kFragmentFullLength, 0x30))
        return false;
    if (!PatternMatches(&storage[4 + kFragmentFullLength], 32, 0x70))
        return false;
    return true;
}

bool newSequenceResetsPartialReassembly()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[kMaxMsgLen];
    uint8_t payload[kFragmentFullLength];
    FillPattern(payload, kFragmentFullLength, 0x60);

    // Partial reassembly from a message that never completes: a full-length
    // fragment means "more fragments expected", so the reassembly stays
    // pending (a short fragment at offset 0 would complete immediately).
    PacketBuilder fragment1(storage, kMaxMsgLen, FragmentSequence(10u),
                            NS_SERVER);
    fragment1.AddFragmentHeader(0u, kFragmentFullLength);
    fragment1.AddPayload(payload, kFragmentFullLength);
    if (Netchan_Process(&fixture.chan, &fragment1.msg) != 0)
        return false;
    if (fixture.chan.fragmentLength != kFragmentFullLength)
        return false;

    // A different sequence restarts the fragment buffer from zero.
    PacketBuilder fragment2(storage, kMaxMsgLen, FragmentSequence(11u),
                            NS_SERVER);
    fragment2.AddFragmentHeader(0u, 50u);
    fragment2.AddPayload(payload, 50);
    if (Netchan_Process(&fixture.chan, &fragment2.msg) != 1)
        return false;
    if (fragment2.msg.cursize != 50 + 4)
        return false;
    if (fixture.chan.incomingSequence != 11)
        return false;
    // Sequence 11 against incoming 0 reports the whole gap, including the
    // abandoned partial message: historical reliable semantics.
    if (fixture.chan.dropped != 10)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// 5. Invalid input and failure-state behavior.
// ---------------------------------------------------------------------------

bool negativeFragmentLengthRejected()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[64];

    // 0xFFFF reads back as a negative fragment length through the production
    // reader; the length guard must reject it without buffering anything.
    PacketBuilder fragment(storage, static_cast<int>(sizeof(storage)),
                           FragmentSequence(4u), NS_SERVER);
    fragment.AddFragmentHeader(0u, 0xFFFFu);

    if (Netchan_Process(&fixture.chan, &fragment.msg) != 0)
        return false;
    if (fixture.chan.fragmentLength != 0)
        return false;
    // Production masks the wire sequence before recording it, so the stored
    // fragmentSequence is 4, not 0x80000004 (refinery review, Codex finding).
    if (fixture.chan.fragmentSequence
        != static_cast<int>(UnmaskedSequence(4u)))
        return false;
    return true;
}

bool fragmentLengthBeyondPacketRejected()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[64];
    uint8_t payload[10];
    FillPattern(payload, 10, 0x60);

    // The fragment header claims 100 payload bytes but only 10 are present.
    PacketBuilder fragment(storage, static_cast<int>(sizeof(storage)),
                           FragmentSequence(5u), NS_SERVER);
    fragment.AddFragmentHeader(0u, 100u);
    fragment.AddPayload(payload, 10);

    if (Netchan_Process(&fixture.chan, &fragment.msg) != 0)
        return false;
    if (fixture.chan.fragmentLength != 0)
        return false;
    if (fixture.chan.incomingSequence != 0)
        return false;
    return true;
}

bool truncatedFragmentHeaderFailsSafely()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[64];
    FillPattern(storage, static_cast<int>(sizeof(storage)), kSentinel);

    // Sequence, qport, and fragmentStart present; only the fragmentLength
    // short is cut off. The earlier header reads succeed, so execution
    // reaches the fragment-length validation itself: the truncated length
    // reads back as -1, the length guard rejects the packet ("illegal
    // fragment length"), the sequence does NOT advance (only completing
    // packets advance it), but the gap counter is reported -- historical
    // reliable semantics. (Refinery review: a bare-sequence packet never
    // reaches this guard -- production rejects it at the fragment offset
    // check first -- so this packet carries the full header minus the final
    // short.)
    PacketBuilder packet(storage, static_cast<int>(sizeof(storage)),
                         FragmentSequence(6u), NS_SERVER);
    MSG_WriteLong(&packet.msg, 0); // fragmentStart only; fragmentLength missing

    if (Netchan_Process(&fixture.chan, &packet.msg) != 0)
        return false;
    if (fixture.chan.incomingSequence != 0)
        return false;
    if (fixture.chan.fragmentLength != 0)
        return false;
    // Production masks the wire sequence before recording it (refinery
    // review, Codex finding).
    if (fixture.chan.fragmentSequence
        != static_cast<int>(UnmaskedSequence(6u)))
        return false;
    if (fixture.chan.dropped != 5)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// Contract runners. Grouped by concern to stay under Codacy's per-function
// complexity limit; the invocation order in RunNetChanProcessContracts below
// is exactly the original single-runner order (span contracts last, from
// net_chan_process_span_tests.cpp).
// ---------------------------------------------------------------------------

int RunUnfragmentedPacketContracts()
{
    if (!unfragmentedServerPacketAccepted())
        return fail("server-sock unfragmented packet was not accepted cleanly");
    if (!unfragmentedClientPacketAccepted())
        return fail("client-sock unfragmented packet was not accepted cleanly");
    if (!gapAccounted())
        return fail("gap across an in-order packet was not reported in chan->dropped");
    if (!duplicateSequenceRejected())
        return fail("duplicate sequence was accepted or advanced state");
    if (!oldSequenceRejected())
        return fail("old sequence was accepted or advanced state");
    return 0;
}

int RunFragmentReassemblyContracts()
{
    if (!fragmentedReassemblyAccepted())
        return fail("valid two-fragment reassembly produced wrong output span, prefix, or state");
    if (!singleFragmentKeepsReassemblyState())
        return fail("a full-length fragment did not leave pending reassembly state");
    if (!outOfOrderFragmentRejectedAndRecovers())
        return fail("fragment ordering was not enforced or recovery reassembly corrupted output");
    if (!newSequenceResetsPartialReassembly())
        return fail("a new sequence did not restart the fragment buffer cleanly");
    return 0;
}

int RunInvalidFragmentContracts()
{
    if (!negativeFragmentLengthRejected())
        return fail("a negative fragment length was accepted or corrupted state");
    if (!fragmentLengthBeyondPacketRejected())
        return fail("a fragment length beyond the packet bytes was accepted or corrupted state");
    if (!truncatedFragmentHeaderFailsSafely())
        return fail("a truncated fragment header did not fail safely");
    return 0;
}
} // namespace

// Entry point invoked from net_chan_reassembly_tests.cpp when the target is
// linked with the production netchan TUs (ILP32 Win32 leg).
int RunNetChanProcessContracts()
{
    InstallInertDvars();

    if (RunUnfragmentedPacketContracts() != 0)
        return 1;
    if (RunFragmentReassemblyContracts() != 0)
        return 1;
    if (RunInvalidFragmentContracts() != 0)
        return 1;
    return RunNetChanSpanContracts();
}

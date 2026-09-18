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
//   * boundary destination capacities around the complete output span
//     (sequence prefix + reassembled payload) versus msg->maxsize,
//   * invalid input (illegal/negative fragment lengths, truncated fragment
//     headers) and the resulting failure state,
//   * the stage-1 regression: a reassembly whose COMPLETE span exceeds
//     msg->maxsize must be rejected before EITHER the four-byte sequence
//     prefix or the payload is written, leaving the destination bytes,
//     cursize, and sequence state untouched.
//
// The production TUs (qcommon/net_chan_mp.cpp, qcommon/msg_mp.cpp,
// qcommon/huffman.cpp) are linked into this target only on the ILP32 Win32
// leg (the engine's actual target); elsewhere this file is not compiled and
// the target keeps its predicate-only shape. No production logic is copied
// or reimplemented here, and the wire bytes are written with the production
// MSG writers so the packet shape stays pinned to the shipped protocol.

#include <qcommon/net_chan_mp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
// MAX_MSGLEN-sized reassembly/destination buffers, as shipped.
constexpr int32_t kMaxMsgLen = 0x40000;

// The engine's fragment threshold: a fragment of exactly this many payload
// bytes means "more fragments expected".
constexpr int kFragmentFullLength = 1300;

constexpr uint16_t kQPort = 0x1234;

constexpr uint8_t kSentinel = 0xCD;

int fail(const char *message)
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

dvar_t MakeInertDvar(const char *name)
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

const InertDvars &Inert()
{
    static const InertDvars instance;
    return instance;
}

void InstallInertDvars()
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

uint8_t g_fragmentBuffer[kMaxMsgLen];
uint8_t g_unsentBuffer[16];

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

uint32_t FragmentSequence(uint32_t sequence)
{
    return sequence | 0x80000000u;
}

uint32_t UnmaskedSequence(uint32_t sequence)
{
    return sequence & ~0x80000000u;
}

void FillPattern(uint8_t *buffer, int length, uint8_t seed)
{
    for (int i = 0; i < length; ++i)
        buffer[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
}

bool PatternMatches(const uint8_t *buffer, int length, uint8_t seed)
{
    for (int i = 0; i < length; ++i)
    {
        if (buffer[i] != static_cast<uint8_t>(seed + static_cast<uint8_t>(i)))
            return false;
    }
    return true;
}

bool LittleEndianPrefixMatches(const uint8_t *buffer, uint32_t sequence)
{
    return buffer[0] == static_cast<uint8_t>(sequence)
        && buffer[1] == static_cast<uint8_t>(sequence >> 8)
        && buffer[2] == static_cast<uint8_t>(sequence >> 16)
        && buffer[3] == static_cast<uint8_t>(sequence >> 24);
}

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
// ---------------------------------------------------------------------------

bool fragmentedReassemblyAccepted()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[kMaxMsgLen];

    // Advance the sequence so the reassembly itself has no gap.
    PacketBuilder bootstrap(storage, kMaxMsgLen, 99u, NS_SERVER);
    if (Netchan_Process(&fixture.chan, &bootstrap.msg) != 1)
        return false;

    uint8_t firstPayload[kFragmentFullLength];
    uint8_t secondPayload[100];
    FillPattern(firstPayload, kFragmentFullLength, 0x10);
    FillPattern(secondPayload, 100, 0x90);

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

    // Fragment 2: short fragment completes the message.
    PacketBuilder fragment2(storage, kMaxMsgLen, FragmentSequence(100u),
                            NS_SERVER);
    fragment2.AddFragmentHeader(kFragmentFullLength, 100u);
    fragment2.AddPayload(secondPayload, 100);
    if (Netchan_Process(&fixture.chan, &fragment2.msg) != 1)
        return false;

    // Complete output span: four-byte prefix plus 1400 payload bytes.
    if (fragment2.msg.cursize != kFragmentFullLength + 100 + 4)
        return false;
    if (!LittleEndianPrefixMatches(storage, UnmaskedSequence(100u)))
        return false;
    if (!PatternMatches(&storage[4], kFragmentFullLength, 0x10))
        return false;
    if (!PatternMatches(&storage[4 + kFragmentFullLength], 100, 0x90))
        return false;

    // Sequence/reliable state after completion.
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
    if (MSG_ReadLong(&fragment2.msg) != expectedFirstLong)
        return false;
    if (fragment2.msg.readcount != 8)
        return false;
    return true;
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
    if (fixture.chan.fragmentSequence != static_cast<int>(FragmentSequence(4u)))
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

    // Sequence present, fragment header cut off: the fragment header reads
    // fail (overflowed), the packet is rejected, the sequence does NOT
    // advance (only completing packets advance it), but the gap counter is
    // reported -- historical reliable semantics.
    msg_t packet;
    std::memset(&packet, 0, sizeof(packet));
    packet.data = storage;
    packet.maxsize = static_cast<int>(sizeof(storage));
    MSG_WriteLong(&packet, static_cast<int>(FragmentSequence(6u)));
    // Truncate AFTER writing: MSG_WriteLong advances cursize, so resetting
    // it here is what cuts the packet down to the bare sequence long.
    packet.cursize = 4; // sequence long only; qport/fragment header missing

    if (Netchan_Process(&fixture.chan, &packet) != 0)
        return false;
    if (fixture.chan.incomingSequence != 0)
        return false;
    if (fixture.chan.fragmentLength != 0)
        return false;
    if (fixture.chan.fragmentSequence != static_cast<int>(FragmentSequence(6u)))
        return false;
    if (fixture.chan.dropped != 5)
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// 6. The stage-1 regression: the COMPLETE output span (prefix + payload) is
//    validated against msg->maxsize before either write, and a rejection
//    leaves the destination bytes and cursize untouched.
// ---------------------------------------------------------------------------

uint8_t g_expectedStorage[kMaxMsgLen];
int g_expectedCursize = 0;

// Runs a two-fragment reassembly into a destination of the given capacity
// (fragment payloads 1300 + 100 = 1400 bytes, sequence 50). Snapshots the
// destination after the completing fragment is crafted, then runs the
// production call. Returns the production verdict (true == completed), and
// reports the completing packet's cursize afterwards.
bool runCompletingReassembly(uint8_t *storage, int destinationCapacity,
                             netchan_t *chan, int *finalCursize)
{
    uint8_t firstPayload[kFragmentFullLength];
    uint8_t secondPayload[100];
    FillPattern(firstPayload, kFragmentFullLength, 0x11);
    FillPattern(secondPayload, 100, 0x22);

    PacketBuilder fragment1(storage, destinationCapacity,
                            FragmentSequence(50u), NS_SERVER);
    fragment1.AddFragmentHeader(0u, kFragmentFullLength);
    fragment1.AddPayload(firstPayload, kFragmentFullLength);
    if (Netchan_Process(chan, &fragment1.msg) != 0)
        return false;

    PacketBuilder fragment2(storage, destinationCapacity,
                            FragmentSequence(50u), NS_SERVER);
    fragment2.AddFragmentHeader(kFragmentFullLength, 100u);
    fragment2.AddPayload(secondPayload, 100);
    std::memcpy(g_expectedStorage, storage,
                static_cast<size_t>(destinationCapacity));
    g_expectedCursize = fragment2.msg.cursize;
    const int verdict = Netchan_Process(chan, &fragment2.msg);
    *finalCursize = fragment2.msg.cursize;
    return verdict == 1;
}

bool overflowSpanRejectedWithoutWrite()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[kMaxMsgLen];

    const int reassembledLength = kFragmentFullLength + 100; // 1400
    // One byte SHORT of the complete span: prefix (4) + 1400 = 1404 > 1403.
    const int destinationCapacity = reassembledLength + 3;
    int finalCursize = -1;

    if (runCompletingReassembly(storage, destinationCapacity, &fixture.chan,
                                &finalCursize))
        return false; // an overflowing reassembly span was accepted

    if (fixture.chan.incomingSequence != 0)
        return false; // rejected: sequence must not advance
    if (fixture.chan.fragmentLength != reassembledLength)
        return false; // buffered data survives; only completion resets it
    // The stage-1 contract: neither prefix nor payload written anywhere.
    if (std::memcmp(storage, g_expectedStorage,
                    static_cast<size_t>(destinationCapacity)) != 0)
        return false;
    if (finalCursize != g_expectedCursize)
        return false;
    return true;
}

bool boundarySpanAccepted()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[kMaxMsgLen];

    const int reassembledLength = kFragmentFullLength + 100; // 1400
    // EXACT complete-span capacity: prefix (4) + 1400 = 1404 == 1404.
    const int destinationCapacity = reassembledLength + 4;
    int finalCursize = -1;

    if (!runCompletingReassembly(storage, destinationCapacity, &fixture.chan,
                                 &finalCursize))
        return false;

    if (fixture.chan.incomingSequence != 50)
        return false;
    if (fixture.chan.fragmentLength != 0)
        return false;
    if (finalCursize != reassembledLength + 4)
        return false;
    if (!LittleEndianPrefixMatches(storage, UnmaskedSequence(50u)))
        return false;
    if (!PatternMatches(&storage[4], kFragmentFullLength, 0x11))
        return false;
    if (!PatternMatches(&storage[4 + kFragmentFullLength], 100, 0x22))
        return false;
    return true;
}

bool subPrefixCapacityFragmentRejected()
{
    ChanFixture fixture(NS_SERVER);
    // The packet itself needs its normal storage; the DESTINATION capacity
    // that Netchan_Process validates is msg->maxsize, so craft normally and
    // then point the message at a sub-prefix capacity.
    uint8_t storage[16];

    PacketBuilder fragment(storage, static_cast<int>(sizeof(storage)),
                           FragmentSequence(7u), NS_SERVER);
    fragment.AddFragmentHeader(0u, 0u);
    fragment.msg.maxsize = 3; // cannot even hold the bare sequence prefix

    std::memcpy(g_expectedStorage, storage, sizeof(storage));
    g_expectedCursize = fragment.msg.cursize;
    if (Netchan_Process(&fixture.chan, &fragment.msg) != 0)
        return false;
    if (std::memcmp(storage, g_expectedStorage, sizeof(storage)) != 0)
        return false;
    if (fragment.msg.cursize != g_expectedCursize)
        return false;
    if (fixture.chan.incomingSequence != 0)
        return false;
    return true;
}

bool barePrefixCapacityAccepted()
{
    ChanFixture fixture(NS_SERVER);
    uint8_t storage[16];

    PacketBuilder fragment(storage, static_cast<int>(sizeof(storage)),
                           FragmentSequence(8u), NS_SERVER);
    fragment.AddFragmentHeader(0u, 0u);
    fragment.msg.maxsize = 4; // exactly the bare sequence prefix

    if (Netchan_Process(&fixture.chan, &fragment.msg) != 1)
        return false;
    if (fragment.msg.cursize != 4)
        return false;
    if (!LittleEndianPrefixMatches(storage, UnmaskedSequence(8u)))
        return false;
    if (fixture.chan.incomingSequence != 8)
        return false;
    if (fixture.chan.fragmentLength != 0)
        return false;
    return true;
}
} // namespace

// Entry point invoked from net_chan_reassembly_tests.cpp when the target is
// linked with the production netchan TUs (ILP32 Win32 leg).
int RunNetChanProcessContracts()
{
    InstallInertDvars();

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
    if (!fragmentedReassemblyAccepted())
        return fail("valid two-fragment reassembly produced wrong output span, prefix, or state");
    if (!singleFragmentKeepsReassemblyState())
        return fail("a full-length fragment did not leave pending reassembly state");
    if (!outOfOrderFragmentRejectedAndRecovers())
        return fail("fragment ordering was not enforced or recovery reassembly corrupted output");
    if (!newSequenceResetsPartialReassembly())
        return fail("a new sequence did not restart the fragment buffer cleanly");
    if (!negativeFragmentLengthRejected())
        return fail("a negative fragment length was accepted or corrupted state");
    if (!fragmentLengthBeyondPacketRejected())
        return fail("a fragment length beyond the packet bytes was accepted or corrupted state");
    if (!truncatedFragmentHeaderFailsSafely())
        return fail("a truncated fragment header did not fail safely");
    if (!overflowSpanRejectedWithoutWrite())
        return fail("an overflowing reassembly touched the destination buffer or state");
    if (!boundarySpanAccepted())
        return fail("the exact-capacity reassembly span was rejected or wrote past its end");
    if (!subPrefixCapacityFragmentRejected())
        return fail("a reassembly into a sub-prefix destination was not rejected without writes");
    if (!barePrefixCapacityAccepted())
        return fail("a bare-prefix completion was rejected or wrote more than the prefix");

    return 0;
}

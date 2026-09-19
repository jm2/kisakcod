// Stage-1 regression contracts for the production Netchan_Process: the
// COMPLETE output span (four-byte sequence prefix + reassembled payload) is
// validated against msg->maxsize before EITHER write, and a rejection leaves
// the destination bytes, cursize, and sequence state untouched.
//
// Split out of net_chan_process_tests.cpp for Codacy's per-file length
// limit; the shared fixtures live in net_chan_process_test_support.h and the
// contract order is preserved (these run last, exactly as in the original
// single-TU runner).
//
// This file is only compiled on the ILP32 Win32 leg (see
// tests/CMakeLists.txt).

#include "net_chan_process_test_support.h"

#include <algorithm>
#include <cstdint>

using namespace netchan_test;

namespace
{
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
    std::copy(storage, storage + destinationCapacity, g_expectedStorage);
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

    std::copy(storage, storage + sizeof(storage), g_expectedStorage);
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

// Invoked last by RunNetChanProcessContracts (net_chan_process_tests.cpp),
// preserving the original single-TU contract order.
int RunNetChanSpanContracts()
{
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

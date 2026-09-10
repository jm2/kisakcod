// Netchan fragment-reassembly output-span contracts.
//
// Issue #123: Netchan_Process validated the reassembled PAYLOAD alone against
// msg->maxsize, then wrote a four-byte sequence prefix PLUS that payload into
// msg->data. A complete reassembly landing in the window
// (maxsize-4, maxsize] therefore wrote up to four bytes past the end of the
// destination buffer. The bounds decision now lives in the production
// function Netchan_ReassembledSpanFits (qcommon/net_chan_mp.h) and
// Netchan_Process consults it before EITHER write.
//
// These tests invoke that production function directly, the same way
// huffman_tests.cpp exercises qcommon/huffman.cpp: no copy of the logic, no
// reimplementation. The function is deliberately expressed with exact-width
// int32_t operands and a subtraction guarded by an earlier comparison, so its
// verdict cannot wrap for ANY inputs and is bit-identical on ILP32 and
// native64 targets; the static_assert table below pins those verdicts at
// compile time on every target that builds this file.

#include <qcommon/net_chan_mp.h>

#include <cstdint>
#include <cstdio>
#include <limits>

namespace
{
int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}

constexpr int32_t i32max = std::numeric_limits<int32_t>::max();
constexpr int32_t i32min = std::numeric_limits<int32_t>::min();

// The verdict table is evaluated at compile time: if the arithmetic ever
// becomes target- or width-dependent, the build breaks here instead of
// drifting at runtime.
#define EXPECT_FITS(capacity, length) \
    static_assert(Netchan_ReassembledSpanFits((capacity), (length)), \
                  #capacity " must hold prefix + " #length)
#define EXPECT_REJECTED(capacity, length) \
    static_assert(!Netchan_ReassembledSpanFits((capacity), (length)), \
                  #capacity " must NOT hold prefix + " #length)
} // namespace

// ---------------------------------------------------------------------------
// 1. Valid traffic: complete and fragmented reassembly spans a real
//    destination accepts. MAX_MSGLEN-sized buffers (0x40000) and
//    fragment-payload multiples of 1300 must behave exactly as before the
//    fix -- the tightened check may only reject spans that previously
//    overflowed the buffer.
// ---------------------------------------------------------------------------

EXPECT_FITS(0x40000, 0);
EXPECT_FITS(0x40000, 1);
EXPECT_FITS(0x40000, 1300);
EXPECT_FITS(0x40000, 1300 * 2);
EXPECT_FITS(0x40000, 0x40000 - 4); // largest span the buffer can hold

namespace
{
bool validTrafficSpansFit()
{
    const int32_t typicalCapacity = 0x40000;
    const int32_t lengths[] = {0, 1, 1299, 1300, 1301, 1300 * 2, typicalCapacity / 2, typicalCapacity - 4};
    for (const int32_t length : lengths)
    {
        if (!Netchan_ReassembledSpanFits(typicalCapacity, length))
            return false;
    }
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// 2. Boundary capacities: the prefix must be counted exactly.
// ---------------------------------------------------------------------------

EXPECT_FITS(4, 0);        // capacity holds the bare prefix
EXPECT_FITS(5, 1);
EXPECT_REJECTED(5, 2);
EXPECT_REJECTED(3, 0);    // prefix alone does not fit
EXPECT_REJECTED(0x40000, 0x40000 - 3); // one byte past the writable span

// ---------------------------------------------------------------------------
// 3. Invalid destination capacities: negative and sub-prefix capacities must
//    fail safely for every payload size, including zero.
// ---------------------------------------------------------------------------

EXPECT_REJECTED(0, 0);
EXPECT_REJECTED(1, 0);
EXPECT_REJECTED(2, 0);
EXPECT_REJECTED(3, 0);
EXPECT_REJECTED(-1, 0);
EXPECT_REJECTED(i32min, 0);
EXPECT_REJECTED(i32min, 1300);

// ---------------------------------------------------------------------------
// 4. Invalid payload lengths: negative accumulated lengths must be rejected
//    regardless of how large the destination is.
// ---------------------------------------------------------------------------

EXPECT_REJECTED(0x40000, -1);
EXPECT_REJECTED(i32max, -1);
EXPECT_REJECTED(i32max, i32min);

// ---------------------------------------------------------------------------
// 5. Overflow safety at the int32 extremes: the old call site computed the
//    span as payload + 4 (and compared payload > maxsize), which wraps for
//    extreme inputs. The production function must return clean, wrap-free
//    verdicts at INT32_MAX / INT32_MIN. constexpr evaluation would trap on
//    signed overflow, so a passing compile is itself evidence of safety.
// ---------------------------------------------------------------------------

EXPECT_FITS(i32max, i32max - 4);
EXPECT_REJECTED(i32max, i32max - 3);
EXPECT_REJECTED(i32max, i32max);
EXPECT_REJECTED(i32max, i32min);

// ---------------------------------------------------------------------------
// 6. Old-vs-new behavioral delta: for every capacity C >= 4 the accepted
//    payload set is exactly [0, C-4]. The window (C-4, C] is precisely the
//    set the historical check accepted but then overflowed when writing the
//    prefix plus payload; everything it accepted as safely writable before
//    ([0, C-4]) must still be accepted.
// ---------------------------------------------------------------------------

namespace
{
bool acceptedPayloadSetIsExact()
{
    const int32_t capacities[] = {4, 5, 6, 1400, 0x4000, 0x40000};
    for (const int32_t capacity : capacities)
    {
        for (int32_t length = 0; length <= capacity - 4; ++length)
        {
            if (capacity - 4 >= 8 && length < capacity - 4 - 8)
                continue; // spot-check near the boundary to keep the sweep tight
            if (!Netchan_ReassembledSpanFits(capacity, length))
                return false;
        }
        for (int32_t length = capacity - 3; length <= capacity; ++length)
        {
            if (Netchan_ReassembledSpanFits(capacity, length))
                return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    if (!validTrafficSpansFit())
        return fail("a valid reassembly span was rejected -- wire behavior regressed");

    if (!acceptedPayloadSetIsExact())
        return fail("accepted payload set deviates from [0, capacity-4]");

    // Runtime re-check of the extreme inputs the static table covers, so the
    // production function is also exercised as running code (and would trip
    // sanitizer-instrumented runs on any UB).
    if (!Netchan_ReassembledSpanFits(i32max, i32max - 4))
        return fail("INT32_MAX-capacity boundary span was rejected");
    if (Netchan_ReassembledSpanFits(i32max, i32max))
        return fail("INT32_MAX payload into INT32_MAX capacity was accepted");
    if (Netchan_ReassembledSpanFits(i32min, 0))
        return fail("negative destination capacity was accepted");
    if (Netchan_ReassembledSpanFits(0x40000, -1))
        return fail("negative payload length was accepted");
    if (!Netchan_ReassembledSpanFits(0x40000, 0x40000 - 4))
        return fail("exact-capacity span was rejected");

    return 0;
}

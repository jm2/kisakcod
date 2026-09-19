// MP message bit-codec wire-format contracts.
//
// The commercial MP protocol serializes commands and entity state with the
// bit-level primitives in src/qcommon/msg_mp.cpp. Since ki-pyb5 those
// primitives live in src/qcommon/msg_bits_mp.cpp (verbatim code motion), a
// target-neutral TU that this test LINKS -- together with the production
// Huffman codec (huffman.cpp) -- so the wire contracts are checked against the
// real retail codec, not a reimplementation.
//
// The suite is split across two translation units sharing the harness in
// msg_wire_test_harness.hpp (same stubs, same reporter, one binary):
//   - this TU: bit packing/order, byte framing, scalar quantization, the
//     production Huffman block codec and the first-difference reporter
//     self-test,
//   - msg_wire_delta_framing_tests.cpp: key-XOR deltas, the usercmd delta
//     classes, string framing and split-buffer reads.
//
// What is pinned, with synthetic fixtures machine-verified against the
// production encoder:
//
//   1. bit packing order/width: LSB-first, little-endian byte fill
//      (MSG_WriteBits/ReadBits, WriteBit0/1, ReadBit, GetUsedBitCount),
//   2. byte-field framing: WriteByte/Short/Long (little-endian raw stores)
//      and the interleaved bit/byte cursor scheme -- bit fields pack into the
//      first bytes' low bits while byte fields append at msg->cursize, and
//      the bit stream jumps to the byte at the END when it exhausts a byte
//      (mirrored on the read side via readcount),
//   3. scalar quantization: WriteAngle16/ReadAngle16 and the meleeChargeYaw
//      182.0444488525391 / 0.0054931640625 (= 65536/360 and 360/65536) pair,
//   4. key-XOR delta coding: WriteDeltaKey/ReadDeltaKey (+Byte/Short
//      variants) and kbitmask masking (msg_wire_delta_framing_tests.cpp),
//   5. usercmd delta coding: MSG_WriteDeltaUsercmdKey/ReadDeltaUsercmdKey
//      sequence and delta behavior -- identical-command, low-path
//      (angle-only), high-path (buttons/weapon/melee) and serverTime-jump
//      classes, all byte-compared against fixtures machine-verified against
//      the production encoder (every golden below was cross-checked by
//      encoding the same inputs through msg_bits_mp.cpp and comparing)
//      (msg_wire_delta_framing_tests.cpp),
//   6. the production Huffman block codec through its MSG entry points
//      (MSG_WriteBitsCompress/ReadBitsCompress) incl. training accounting,
//   7. string framing (WriteString/BigString, I_CleanChar on the wire,
//      overflow guards) and split-buffer reads (MSG_InitReadOnlySplit)
//      (msg_wire_delta_framing_tests.cpp).
//
// msg_t contracts the engine relies on (pinned by test_read_data_and_splits):
//   - MSG_Init zeroes the struct; MSG_InitReadOnlySplit does NOT -- engine
//     call sites pass zero-initialized msg_t, and so must tests (a garbage
//     readcount makes the cross-split path read out of bounds).
//   - MSG_ReadData crossing the primary/split boundary is only correct when
//     readcount == cursize at entry (the split index is readcount - cursize;
//     reading mid-buffer is a retail quirk that is undefined, not pinned).
//
// First-difference reporting. compareWire() is the reusable reporter demanded
// by the roadmap for commercial-fixture comparisons: it locates the first
// differing BYTE and BIT and attributes it to a named field span. The later
// command-driven simulation-parity stage (#127) reuses it to report the first
// differing field and tick.
//
// Tie handling. Unlike the Huffman codebook (see
// huffman_wire_contract_tests.cpp), every fixture here is chosen to be free of
// implementation-defined float ties: the angle quantization literals multiply
// exactly representable floats and truncate unambiguous doubles on every IEEE
// host, so byte-exact goldens are pinned unconditionally. No new fork
// baseline that already differs from retail is blessed by this file.
//
// These are still sanitized synthetic fixtures. They lock the wire bytes the
// fork emits and catch accidental drift; they are NOT the commercial-binary
// byte-comparison certification gate in docs/NETWORK_COMPATIBILITY.md, which
// requires authentic 1.7/Steam-1.8 references (#122) and remains blocked on
// #122 evidence.
//
// Link stubs live in the shared harness header (msg_wire_test_harness.hpp)
// together with their fidelity notes.

#include "msg_wire_test_harness.hpp"

#include <qcommon/sv_msg_write_mp.h> // huffBytesSeen

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------- stubs ----
// Defined here (declared in the shared harness header). See that header for
// the fidelity notes on each stub.

namespace
{
// Stub clock for link-time symbol resolution and tracing paths: advances 7 ms
// per call so callers never observe a frozen clock.
std::uint32_t g_stubClockMs = 1000;
} // namespace

void MyAssertHandler(const char *filename, int line, int type, const char *fmt, ...)
{
    (void)fmt;
    std::fprintf(stderr, "unexpected production assert at %s:%d (type %d)\n",
                 filename, line, type);
    std::abort();
}

void QDECL Com_Printf(int channel, const char *fmt, ...)
{
    (void)channel;
    (void)fmt;
}

void Com_PrintError(int channel, const char *fmt, ...)
{
    (void)channel;
    (void)fmt;
}

std::uint32_t KISAK_CDECL Sys_Milliseconds()
{
    g_stubClockMs += 7;
    return g_stubClockMs;
}

// Verbatim semantics of universal/q_shared.cpp:518.
uint8_t I_CleanChar(uint8_t character)
{
    if (character == 146)
        return 39;
    return character;
}

// Verbatim semantics of universal/q_shared.cpp:45: strncpy's
// copy-until-NUL-then-zero-fill, then a forced terminator at destsize-1.
// Written as explicit loops -- rather than strncpy -- so the test harness
// uses no MS-banned string primitives; behavior is byte-identical.
void I_strncpyz(char *dest, const char *src, int destsize)
{
    const std::size_t limit = static_cast<std::size_t>(destsize) - 1;
    std::size_t i = 0;
    for (; i < limit && src[i] != '\0'; ++i)
        dest[i] = src[i];
    for (; i < limit; ++i)
        dest[i] = '\0';
    dest[limit] = '\0';
}

// ---------------------------------------------------------------- tests ----

// Width/order of the bit-packing primitive: GetMinBitCountForNum is the
// field-width selector shared by the entity/playerstate delta encoders.
void test_bit_width_selection()
{
    struct Case { std::uint32_t in; int want; };
    const Case cases[] = {
        {0u, 0},          {1u, 1},          {2u, 2},          {3u, 2},
        {4u, 3},          {7u, 3},          {8u, 4},          {255u, 8},
        {256u, 9},        {0x3FFu, 10},     {0x400u, 11},     {0xFFFFu, 16},
        {0x10000u, 17},   {0x7FFFFFFFu, 31}, {0x80000000u, 32}, {0xFFFFFFFFu, 32},
    };
    for (const Case &c : cases)
        CHECK(GetMinBitCountForNum(c.in) == c.want);
}

void test_bit_packing_order()
{
    msg_t m;
    std::uint8_t buf[16];
    makeMsg(m, buf, 16);

    MSG_WriteBits(&m, 0x5, 3);    // 101b -> bits 0..2
    MSG_WriteBits(&m, 0x1A, 5);   // 11010b -> bits 3..7: exactly one byte
    const std::uint8_t want[] = {0xD5};
    const WireSpan spans[] = {
        {"lo3", 0, 0, 3},
        {"hi5", 0, 3, 5},
    };
    compareWire(m, "WriteBits LSB-first packing", want, sizeof(want), spans, 2);
    CHECK(MSG_GetUsedBitCount(&m) == 8);

    MSG_BeginReading(&m);
    CHECK(MSG_ReadBits(&m, 3) == 0x5);
    CHECK(MSG_ReadBits(&m, 5) == 0x1A);

    // 32-bit spill plus a following bit: exact byte order across the spill.
    msg_t m2;
    std::uint8_t buf2[16];
    makeMsg(m2, buf2, 16);
    MSG_WriteBits(&m2, -1, 32);
    MSG_WriteBits(&m2, 1, 1);
    const std::uint8_t want2[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x01};
    compareWire(m2, "WriteBits 32-bit spill", want2, sizeof(want2));

    MSG_BeginReading(&m2);
    CHECK(MSG_ReadBits(&m2, 32) == -1);
    CHECK(MSG_ReadBits(&m2, 1) == 1);

    // Single-bit primitives produce the same layout as WriteBits.
    msg_t m3;
    std::uint8_t buf3[16];
    makeMsg(m3, buf3, 16);
    MSG_WriteBit1(&m3);
    MSG_WriteBit0(&m3);
    MSG_WriteBit1(&m3);
    const std::uint8_t want3[] = {0x05};
    compareWire(m3, "WriteBit0/WriteBit1", want3, sizeof(want3));
    MSG_BeginReading(&m3);
    CHECK(MSG_ReadBit(&m3) == 1);
    CHECK(MSG_ReadBit(&m3) == 0);
    CHECK(MSG_ReadBit(&m3) == 1);
}

// The interleaved bit/byte cursor scheme: a bit opens byte0, a byte-field
// byte lands at cursize, and the next bits keep packing into byte0's low
// bits. This is the retail layout the usercmd encoder depends on.
void test_bit_byte_interleave()
{
    msg_t m;
    std::uint8_t buf[16];
    makeMsg(m, buf, 16);

    MSG_WriteBit1(&m);          // data[0] bit0 (cursize 1)
    MSG_WriteByte(&m, 0xAB);    // data[1] = 0xAB (cursize 2), bit cursor untouched
    MSG_WriteBit1(&m);          // data[0] bit1
    MSG_WriteBit1(&m);          // data[0] bit2
    const std::uint8_t want[] = {0x07, 0xAB};
    const WireSpan spans[] = {
        {"bitflag", 0, 0, 3},
        {"rawbyte", 1, 0, 8},
    };
    compareWire(m, "bit/byte cursor interleave", want, sizeof(want), spans, 2);

    MSG_BeginReading(&m);
    CHECK(MSG_ReadBit(&m) == 1);
    CHECK(MSG_ReadByte(&m) == 0xAB);
    CHECK(MSG_ReadBit(&m) == 1);
    CHECK(MSG_ReadBit(&m) == 1);
}

void test_scalar_framing()
{
    msg_t m;
    std::uint8_t buf[32];
    makeMsg(m, buf, 32);

    MSG_WriteByte(&m, 0xAB);
    MSG_WriteShort(&m, 0x1234);
    MSG_WriteLong(&m, static_cast<int>(0x89ABCDE7u));
    const std::uint8_t want[] = {0xAB, 0x34, 0x12, 0xE7, 0xCD, 0xAB, 0x89};
    const WireSpan spans[] = {
        {"byte", 0, 0, 8}, {"short", 1, 0, 16}, {"long", 3, 0, 32},
    };
    compareWire(m, "WriteByte/Short/Long little-endian", want, sizeof(want), spans, 3);

    MSG_BeginReading(&m);
    CHECK(MSG_ReadByte(&m) == 0xAB);
    CHECK(MSG_ReadShort(&m) == 0x1234);
    CHECK(MSG_ReadLong(&m) == static_cast<int>(0x89ABCDE7u));

    // Byte-boundary exhaustion and EOF sentinels.
    msg_t tiny;
    std::uint8_t tinyBuf[1];
    makeMsg(tiny, tinyBuf, 1);
    MSG_WriteByte(&tiny, 1);
    MSG_WriteByte(&tiny, 2);
    CHECK(tiny.overflowed == 1);
    MSG_BeginReading(&tiny);
    CHECK(MSG_ReadByte(&tiny) == 1);
    CHECK(MSG_ReadByte(&tiny) == -1);
    CHECK(tiny.overflowed == 1);
}

// Angle16 scalar quantization: 182.0444488525391 = 65536/360 on write and
// 0.0054931640625 = 360/65536 on read. Fixtures avoid float ties by
// construction (exactly representable operands, unambiguous truncation).
void test_angle16_quantization()
{
    msg_t m;
    std::uint8_t buf[32];
    makeMsg(m, buf, 32);

    MSG_WriteAngle16(&m, 90.0f);   // (int)(90 * 182.0444488525391) = 16384
    MSG_WriteAngle16(&m, 0.0f);    // 0
    MSG_WriteAngle16(&m, 1.0f);    // 182
    MSG_WriteAngle16(&m, -180.0f); // -32768 -> 0x8000
    const std::uint8_t want[] = {
        0x00, 0x40, // 16384
        0x00, 0x00, // 0
        0xB6, 0x00, // 182
        0x00, 0x80, // -32768
    };
    const WireSpan spans[] = {
        {"angle16[0]", 0, 0, 16}, {"angle16[1]", 2, 0, 16},
        {"angle16[2]", 4, 0, 16}, {"angle16[3]", 6, 0, 16},
    };
    compareWire(m, "WriteAngle16 quantization", want, sizeof(want), spans, 4);

    MSG_BeginReading(&m);
    const double kScale = 0.0054931640625; // exactly 360/65536
    CHECK(MSG_ReadAngle16(&m) == 16384 * kScale);   // exactly 90.0
    CHECK(MSG_ReadAngle16(&m) == 0.0);
    CHECK(MSG_ReadAngle16(&m) == 182 * kScale);     // exactly 4095/4096
    CHECK(MSG_ReadAngle16(&m) == -32768 * kScale);  // exactly -180.0
}

// The production Huffman block codec through the MSG entry points, plus the
// training histogram the snapshot profiler dumps.
void test_huffman_msg_block()
{
    // Dedicated byte values so the training histogram is unaffected by the
    // string tests below (which do not train).
    std::uint8_t payload[512];
    for (int i = 0; i < 512; ++i)
        payload[i] = static_cast<std::uint8_t>((i % 2) ? 0xFE : 0x77);

    std::uint8_t compressed[1024];
    const int compressedSize =
        MSG_WriteBitsCompress(true, payload, sizeof(payload), compressed, sizeof(compressed));
    // The default (untrained) tree may expand pathological inputs: this
    // alternating two-symbol payload encodes to 544 bytes. Only validity,
    // round-trip fidelity and the training histogram are contractual here.
    CHECK(compressedSize > 0);
    CHECK(huffBytesSeen[0x77] == 256);
    CHECK(huffBytesSeen[0xFE] == 256);

    std::uint8_t restored[512];
    const int restoredSize =
        MSG_ReadBitsCompress(compressed, compressedSize, restored, sizeof(restored));
    CHECK(restoredSize == static_cast<int>(sizeof(payload)));
    CHECK(std::memcmp(restored, payload, sizeof(payload)) == 0);

    // Oversized output buffer request fails closed.
    std::uint8_t small[4];
    CHECK(MSG_WriteBitsCompress(false, payload, sizeof(payload), small, sizeof(small)) == -1);
}

// The reporter itself: a flipped bit must be located and attributed.
void test_compare_wire_reporter()
{
    msg_t m;
    std::uint8_t buf[4];
    makeMsg(m, buf, 4);
    MSG_WriteByte(&m, 0x01);
    MSG_WriteByte(&m, 0x00);

    const std::uint8_t expected[] = {0x03, 0x00};
    const WireSpan spans[] = {
        {"flag", 0, 0, 1},
        {"other", 0, 1, 7},
        {"tail", 1, 0, 8},
    };
    const WireDiff d = compareWireNegative(m, expected, sizeof(expected), spans, 3);
    CHECK(!d.ok);
    CHECK(d.byteIndex == 0);
    CHECK(d.bitIndex == 1);
    CHECK(d.fieldName != nullptr && std::strcmp(d.fieldName, "other") == 0);
    CHECK(d.producedByte == 0x01);
    CHECK(d.expectedByte == 0x03);

    // Size drift is reported at the first out-of-range byte.
    const std::uint8_t expectedLong[] = {0x01, 0x00, 0x00};
    const WireDiff d2 = compareWireNegative(m, expectedLong, sizeof(expectedLong), nullptr, 0);
    CHECK(!d2.ok);
    CHECK(d2.byteIndex == 2);

    // Multi-byte span attribution: a 16-bit span must name drift in its
    // LATER bytes too, not only the byte it starts in.
    msg_t mPair;
    std::uint8_t bufPair[4];
    makeMsg(mPair, bufPair, 4);
    MSG_WriteByte(&mPair, 0x5A);
    MSG_WriteByte(&mPair, 0xA5);
    const WireSpan pairSpans[] = {
        {"pair16", 0, 0, 16},
        {"after", 2, 0, 8},
    };

    // Drift in the second byte of the span: 0xA5 vs 0x25 -> bit 7.
    const std::uint8_t expectedPair[] = {0x5A, 0x25};
    const WireDiff d3 = compareWireNegative(mPair, expectedPair, sizeof(expectedPair), pairSpans, 2);
    CHECK(!d3.ok);
    CHECK(d3.byteIndex == 1);
    CHECK(d3.bitIndex == 7);
    CHECK(d3.fieldName != nullptr && std::strcmp(d3.fieldName, "pair16") == 0);

    // Drift in the first byte of the same span: 0x5A vs 0x4A -> bit 4.
    const std::uint8_t expectedPairFirst[] = {0x4A, 0xA5};
    const WireDiff d4 = compareWireNegative(mPair, expectedPairFirst, sizeof(expectedPairFirst), pairSpans, 2);
    CHECK(!d4.ok);
    CHECK(d4.byteIndex == 0);
    CHECK(d4.bitIndex == 4);
    CHECK(d4.fieldName != nullptr && std::strcmp(d4.fieldName, "pair16") == 0);
}

// Multi-byte STRING span attribution: a 4-byte string span (32 bits) must
// name drift in its LAST byte too — this is the byte-count-vs-bit-length
// trap: 4 here means 4 BITS, which stops at byte 0 and reports (unmapped)
// for every later byte of the string.
void test_string_span_attribution()
{
    msg_t mStr;
    std::uint8_t bufStr[8];
    makeMsg(mStr, bufStr, 8);
    MSG_WriteByte(&mStr, 'a');
    MSG_WriteByte(&mStr, 'b');
    MSG_WriteByte(&mStr, 'c');
    MSG_WriteByte(&mStr, 0); // string terminator
    const WireSpan strSpans[] = {
        {"stringy", 0, 0, 4 * 8},
        {"after", 4, 0, 8},
    };

    // Drift in the terminator byte (byte 3, bit 0): produced 0x00 vs 0x01.
    const std::uint8_t expectedStr[] = {'a', 'b', 'c', 0x01};
    const WireDiff d5 = compareWireNegative(mStr, expectedStr, sizeof(expectedStr), strSpans, 2);
    CHECK(!d5.ok);
    CHECK(d5.byteIndex == 3);
    CHECK(d5.bitIndex == 0);
    CHECK(d5.fieldName != nullptr && std::strcmp(d5.fieldName, "stringy") == 0);

    // And mid-string (byte 2, bit 1): produced 'c' vs 0x61 -> bit 1.
    const std::uint8_t expectedStrMid[] = {'a', 'b', 0x61, 0x00};
    const WireDiff d6 = compareWireNegative(mStr, expectedStrMid, sizeof(expectedStrMid), strSpans, 2);
    CHECK(!d6.ok);
    CHECK(d6.byteIndex == 2);
    CHECK(d6.bitIndex == 1);
    CHECK(d6.fieldName != nullptr && std::strcmp(d6.fieldName, "stringy") == 0);
}

int main()
{
    // MSG_Init lazily builds the production Huffman tree from msg_hData on
    // first use; every case below runs against that shared retail codebook.
    test_bit_width_selection();
    test_bit_packing_order();
    test_bit_byte_interleave();
    test_scalar_framing();
    test_angle16_quantization();
    test_delta_key_xor();
    test_usercmd_identical();
    test_usercmd_low_path();
    test_usercmd_high_path();
    test_usercmd_time_jump();
    test_usercmd_selected_location();
    test_huffman_msg_block();
    test_string_framing();
    test_read_data_and_splits();
    test_compare_wire_reporter();
    test_string_span_attribution();

    if (g_failed)
    {
        std::fprintf(stderr, "msg-wire-format-contracts: FAILED\n");
        return 1;
    }
    std::printf("msg-wire-format-contracts: all contracts held\n");
    return 0;
}

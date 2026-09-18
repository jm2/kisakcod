// MP message bit-codec wire-format contracts.
//
// The commercial MP protocol serializes commands and entity state with the
// bit-level primitives in src/qcommon/msg_mp.cpp. Since ki-pyb5 those
// primitives live in src/qcommon/msg_bits_mp.cpp (verbatim code motion), a
// target-neutral TU that this test LINKS -- together with the production
// Huffman codec (huffman.cpp) -- so the wire contracts are checked against the
// real retail codec, not a reimplementation.
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
//      variants) and kbitmask masking,
//   5. usercmd delta coding: MSG_WriteDeltaUsercmdKey/ReadDeltaUsercmdKey
//      sequence and delta behavior -- identical-command, low-path
//      (angle-only), high-path (buttons/weapon/melee) and serverTime-jump
//      classes, all byte-compared against fixtures machine-verified against
//      the production encoder (every golden below was cross-checked by
//      encoding the same inputs through msg_bits_mp.cpp and comparing),
//   6. the production Huffman block codec through its MSG entry points
//      (MSG_WriteBitsCompress/ReadBitsCompress) incl. training accounting,
//   7. string framing (WriteString/BigString, I_CleanChar on the wire,
//      overflow guards) and split-buffer reads (MSG_InitReadOnlySplit).
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
// Test-side link stubs (kept minimal and pinned to the production bodies):
//   - I_CleanChar / I_strncpyz mirror universal/q_shared.cpp verbatim
//     semantics (q_shared.cpp itself pulls 32-bit-pinned engine headers and
//     cannot link into a hosted test binary); the wire effect of cleaning is
//     pinned by a golden, so drift in these stubs is caught.
//   - Com_Printf / Com_PrintError / Sys_Milliseconds are silent no-op sinks;
//     the codec uses them for tracing only.
//   - MyAssertHandler is fail-closed, as in the other wire-contract tests.

#include <qcommon/msg_mp.h>
#include <qcommon/sv_msg_write_mp.h> // huffBytesSeen
#include <qcommon/huffman.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------- stubs ----

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
    static std::uint32_t fakeClock = 1000;
    fakeClock += 7;
    return fakeClock;
}

// Verbatim semantics of universal/q_shared.cpp:518.
uint8_t I_CleanChar(uint8_t character)
{
    if (character == 146)
        return 39;
    return character;
}

// Verbatim semantics of universal/q_shared.cpp:45.
void I_strncpyz(char *dest, const char *src, int destsize)
{
    std::strncpy(dest, src, static_cast<std::size_t>(destsize) - 1);
    dest[destsize - 1] = 0;
}

// --------------------------------------------------------------- harness ---

namespace
{
bool g_failed = false;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond))                                                             \
        {                                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failed = true;                                                     \
        }                                                                        \
    } while (0)

// A named run of bits on the wire, located by byte index and starting bit
// within that byte. compareWire() attributes the first differing bit to the
// span covering it so a drift report names the field, not just the offset.
struct WireSpan
{
    const char *name;
    std::size_t byte;
    std::size_t bit;
    std::size_t bits;
};

struct WireDiff
{
    bool ok = true;
    std::size_t byteIndex = 0;
    std::size_t bitIndex = 0;
    const char *fieldName = nullptr;
    unsigned producedByte = 0;
    unsigned expectedByte = 0;
    std::size_t producedSize = 0;
    std::size_t expectedSize = 0;
};

const char *spanFor(const WireSpan *spans, std::size_t spanCount,
                    std::size_t byteIndex, std::size_t bitIndex)
{
    for (std::size_t i = 0; i < spanCount; ++i)
    {
        const WireSpan &s = spans[i];
        if (s.byte == byteIndex && bitIndex >= s.bit && bitIndex < s.bit + s.bits)
            return s.name;
    }
    return "(unmapped)";
}

// Quiet comparison core: computes the first-difference description without
// printing or touching g_failed, so the reporter itself can be self-tested
// without poisoning the run (compareWireNegative).
WireDiff compareWireDiff(const msg_t &m, const std::uint8_t *expected,
                         std::size_t expectedSize,
                         const WireSpan *spans = nullptr, std::size_t spanCount = 0)
{
    WireDiff d;
    d.producedSize = static_cast<std::size_t>(m.cursize);
    d.expectedSize = expectedSize;

    if (m.overflowed)
    {
        d.ok = false;
        return d;
    }
    if (m.cursize != static_cast<int>(expectedSize))
    {
        d.ok = false;
        d.byteIndex = static_cast<std::size_t>(
            m.cursize < static_cast<int>(expectedSize) ? m.cursize : static_cast<int>(expectedSize));
        return d;
    }
    for (std::size_t i = 0; i < expectedSize; ++i)
    {
        if (m.data[i] != expected[i])
        {
            d.ok = false;
            d.byteIndex = i;
            d.producedByte = m.data[i];
            d.expectedByte = expected[i];
            for (std::size_t b = 0; b < 8; ++b)
            {
                if (((m.data[i] >> b) & 1u) != ((expected[i] >> b) & 1u))
                {
                    d.bitIndex = b;
                    break;
                }
            }
            d.fieldName = spanFor(spans, spanCount, i, d.bitIndex);
            return d;
        }
    }
    return d;
}

// Byte-compares the encoded message against the fixture and reports the first
// differing byte, the bit within it, and the named field covering that bit.
// This is the reporter the commercial-fixture stage (#127/#122) builds on; it
// never silently passes a size or content drift.
WireDiff compareWire(const msg_t &m, const char *what,
                     const std::uint8_t *expected, std::size_t expectedSize,
                     const WireSpan *spans = nullptr, std::size_t spanCount = 0)
{
    WireDiff d = compareWireDiff(m, expected, expectedSize, spans, spanCount);
    if (d.ok)
        return d;
    g_failed = true;
    if (m.overflowed)
        std::fprintf(stderr, "FAIL %s: encoder set overflowed\n", what);
    else if (m.cursize != static_cast<int>(expectedSize))
        std::fprintf(stderr,
                     "FAIL %s: size drift, produced %d bytes, expected %zu; "
                     "first difference would be at byte %zu\n",
                     what, m.cursize, expectedSize, d.byteIndex);
    else
        std::fprintf(stderr,
                     "FAIL %s: first differing byte %zu (bit %zu) in field %s: "
                     "produced 0x%02X, expected 0x%02X\n",
                     what, d.byteIndex, d.bitIndex,
                     d.fieldName ? d.fieldName : "(unmapped)",
                     d.producedByte, d.expectedByte);
    return d;
}

// Non-failing variant for the reporter's own self-test.
WireDiff compareWireNegative(const msg_t &m, const std::uint8_t *expected,
                             std::size_t expectedSize,
                             const WireSpan *spans = nullptr, std::size_t spanCount = 0)
{
    return compareWireDiff(m, expected, expectedSize, spans, spanCount);
}

msg_t makeMsg(msg_t &m, std::uint8_t *storage, int capacity)
{
    MSG_Init(&m, storage, capacity);
    return m;
}

usercmd_s baseCmd()
{
    usercmd_s c{};
    c.serverTime = 1000;
    c.buttons = 0;
    c.angles[0] = 100;
    c.angles[1] = 200;
    c.angles[2] = 300;
    c.weapon = 1;
    c.offHandIndex = 2;
    c.forwardmove = 0;
    c.rightmove = 0;
    c.meleeChargeYaw = 0.0f;
    c.meleeChargeDist = 0;
    c.selectedLocation[0] = 0;
    c.selectedLocation[1] = 0;
    return c;
}

} // namespace

// ---------------------------------------------------------------- tests ----

namespace
{
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

void test_delta_key_xor()
{
    const int key = 0x5A;
    msg_t m;
    std::uint8_t buf[32];
    makeMsg(m, buf, 32);

    // Unchanged -> single 0 bit.
    MSG_WriteDeltaKey(&m, key, 7, 7, 8);
    // Changed -> 1 bit + (key ^ new) masked to 8 bits: 0x5A ^ 0x2C = 0x76,
    // LSB-first from bit 2: bits 3,4,6,7 set + carry bit into byte1 bit0.
    MSG_WriteDeltaKey(&m, key, 0, 0x2C, 8);
    const std::uint8_t want[] = {0xDA, 0x01};
    const WireSpan spans[] = {
        {"unchanged-flag", 0, 0, 1},
        {"changed-flag", 0, 1, 1},
        {"xored-payload", 0, 2, 6},
        {"xored-payload-carry", 1, 0, 1},
    };
    compareWire(m, "WriteDeltaKey", want, sizeof(want), spans, 4);

    MSG_BeginReading(&m);
    CHECK(MSG_ReadDeltaKey(&m, key, 7, 8) == 7);
    CHECK(MSG_ReadDeltaKey(&m, key, 0, 8) == 0x2C);

    // Byte/Short variants: the flag is a single bit, the payload a RAW byte
    // (WriteByte) / RAW short (WriteShort), both key-XORed.
    msg_t m2;
    std::uint8_t buf2[32];
    makeMsg(m2, buf2, 32);
    MSG_WriteDeltaKeyByte(&m2, static_cast<char>(0x33), static_cast<char>(0),
                          static_cast<char>(0x55));
    MSG_WriteDeltaKeyShort(&m2, static_cast<__int16>(0x1234), static_cast<__int16>(0),
                           static_cast<__int16>(0x5678));
    // 0x33^0x55 = 0x66 raw byte after flag bit0 -> {0x01, 0x66};
    // 0x1234^0x5678 = 0x444C raw short after flag bit1 -> byte0 0x03, {4C, 44}.
    const std::uint8_t want2[] = {0x03, 0x66, 0x4C, 0x44};
    const WireSpan spans2[] = {
        {"byte-flag", 0, 0, 1}, {"short-flag", 0, 1, 1},
        {"byte-payload", 1, 0, 8},
        {"short-payload", 2, 0, 16},
    };
    compareWire(m2, "WriteDeltaKeyByte/Short", want2, sizeof(want2), spans2, 4);

    MSG_BeginReading(&m2);
    CHECK(MSG_ReadDeltaKeyByte(&m2, static_cast<std::uint8_t>(0x33), 0) == 0x55);
    CHECK(MSG_ReadDeltaKeyShort(&m2, static_cast<__int16>(0x1234), 0) == 0x5678);

    // WriteKey/ReadKey (unconditional XOR write).
    msg_t m3;
    std::uint8_t buf3[8];
    makeMsg(m3, buf3, 8);
    MSG_WriteKey(&m3, 0xFF, 0x0F, 8); // writes 0xF0
    const std::uint8_t want3[] = {0xF0};
    compareWire(m3, "WriteKey", want3, sizeof(want3));
    MSG_BeginReading(&m3);
    CHECK(MSG_ReadKey(&m3, 0xFF, 8) == 0x0F);
}

// MSG_SetDefaultUserCmd stays in msg_mp.cpp (it dereferences the 32-bit-pinned
// playerState_s from bgame/bg_local.h), so only the wire codec side of the
// usercmd path is linked here. The delta codec itself only needs usercmd_s,
// which is pointer-free and fully defined in msg_mp.h.
void test_usercmd_identical()
{
    const int key = 0x12345678;
    usercmd_s from = baseCmd();
    usercmd_s to = baseCmd();
    to.serverTime = from.serverTime; // delta 0

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    const std::uint8_t want[] = {0x01, 0x00};
    const WireSpan spans[] = {
        {"servertime-flag", 0, 0, 1}, {"servertime-delta", 1, 0, 8},
        {"changed-flag", 0, 1, 1},
    };
    compareWire(m, "usercmd identical", want, sizeof(want), spans, 3);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == to.serverTime);
    CHECK(out.buttons == to.buttons);
    CHECK(out.angles[0] == to.angles[0]);
    CHECK(out.angles[1] == to.angles[1]);
    CHECK(out.angles[2] == to.angles[2]);
    CHECK(out.weapon == to.weapon);
    CHECK(out.offHandIndex == to.offHandIndex);
    CHECK(out.forwardmove == to.forwardmove);
    CHECK(out.rightmove == to.rightmove);
}

// Low path: only the low bits change (angles + hor move), buttons>>1,
// weapon, offHand, angles[2] and melee are all unchanged. keyb is derived
// from the NEW serverTime; the hor-move nibble's last bits jump past the
// byte-field gap exactly like retail.
void test_usercmd_low_path()
{
    const int key = 0x12345678;
    usercmd_s from = baseCmd();
    usercmd_s to = baseCmd();
    to.serverTime = 1010;
    to.angles[0] = 150;
    to.angles[1] = 250;

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    // Machine-verified against the production encoder: keyb = 1010 ^ 0x12345678
    // = 0x1234558A.
    //   byte0: bit0 servertime-flag=1, bit1 changed=1, bit2 lowpath=0,
    //          bit3 buttons0=0, bit4 angles0-changed=1, bit5 angles1-changed=1,
    //          bit6 hor-changed=0 -> 0x33
    //   byte1: raw serverTime delta 10 -> 0x0A
    //   bytes2-3: WriteShort(keyb ^ 150 = 0x1234551C) -> 1C 55
    //   bytes4-5: WriteShort(keyb ^ 250 = 0x12345570) -> 70 55
    const std::uint8_t want[] = {0x33, 0x0A, 0x1C, 0x55, 0x70, 0x55};
    const WireSpan spans[] = {
        {"servertime-flag", 0, 0, 1}, {"servertime-delta", 1, 0, 8},
        {"changed-flag", 0, 1, 1}, {"lowpath-flag", 0, 2, 1},
        {"buttons-bit0", 0, 3, 1}, {"angles0-flag", 0, 4, 1},
        {"angles1-flag", 0, 5, 1}, {"hor-flag", 0, 6, 1},
        {"angles0-xored", 2, 0, 16}, {"angles1-xored", 4, 0, 16},
    };
    compareWire(m, "usercmd low path", want, sizeof(want), spans, 10);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 1010);
    CHECK(out.buttons == to.buttons);
    CHECK(out.angles[0] == 150);
    CHECK(out.angles[1] == 250);
    CHECK(out.angles[2] == 300);
    CHECK(out.weapon == 1);
    CHECK(out.offHandIndex == 2);
    CHECK(out.forwardmove == 0);
    CHECK(out.rightmove == 0);
    CHECK(out.meleeChargeDist == 0);
}

// High path: buttons>>1 differs (melee press), weapon changes, angles[2]
// changes, melee charge set. Exercises the keya-derived fields, the 20-bit
// buttons>>1 field, the 7-bit weapon/offhand fields and the melee gating on
// BUTTON_MELEE.
void test_usercmd_high_path()
{
    const int key = static_cast<int>(0xDEADBEEFu);
    usercmd_s from = baseCmd();
    from.serverTime = 5000;
    usercmd_s to = baseCmd();
    to.serverTime = 5005;
    to.buttons = BUTTON_MELEE; // 4 -- buttons>>1 differs from 0
    to.weapon = 5;
    to.angles[2] = 35;
    to.meleeChargeYaw = 1.0f;
    to.meleeChargeDist = 10;

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    // Machine-verified against the production encoder: keya = 5005 ^ 0xDEADBEEF
    // = 0xDEADAD62 (the encoder truncates every key to __int16 -> 0xAD62).
    //   byte0  0x89: st-flag=1, changed=1, highpath=1, buttons0=1,
    //                a0/a1/hor/a2 flags=0
    //   byte1  0x05: serverTime delta
    //   b2-3  41 AD: WriteShort(keya ^ 35 = 0xAD62^0x23 = 0xAD41)
    //   b4    0xC1: 20-bit buttons>>1 payload head + flags
    //   b5    0x5A: payload bits 8,10,11,13
    //   b6    0xFB: payload tail + weapon flag + weapon payload
    //   b7    0xD9: melee flag + dist flag + yaw-short carry bits
    //   b8-9  D4 AD: WriteShort(keya ^ 182 = 0xAD62^0xB6 = 0xADD4)
    //   b10   0x68: 8-bit dist payload (keya ^ 10)
    const std::uint8_t want[] = {0x89, 0x05, 0x41, 0xAD, 0xC1, 0x5A,
                                 0xFB, 0xD9, 0xD4, 0xAD, 0x68};
    const WireSpan spans[] = {
        {"buttons>>1-20bit", 4, 0, 8}, {"buttons>>1-mid", 5, 0, 8},
        {"buttons>>1-tail+weapon", 6, 0, 8}, {"offhand+flags", 7, 0, 8},
        {"angles2-xored", 2, 0, 16}, {"meleeYaw-xored", 8, 0, 16},
        {"meleeDist-xored", 10, 0, 8},
    };
    compareWire(m, "usercmd high path", want, sizeof(want), spans, 7);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 5005);
    CHECK(out.buttons == BUTTON_MELEE);
    CHECK(out.angles[0] == 100);
    CHECK(out.angles[1] == 200);
    CHECK(out.angles[2] == 35);
    CHECK(out.weapon == 5);
    CHECK(out.offHandIndex == 2);
    CHECK(out.meleeChargeDist == 10);
    // Retail asymmetry, pinned exactly: the encoder XORs the quantized yaw
    // with keya truncated to __int16 (0xAD62), but the decoder's
    // MSG_ReadDeltaKeyShort takes __int16 key and sign-extends it in its int
    // return, so the recovered quantized value is 0xFFFF00B6 = -65354, not
    // 182. Retail client and server both decode this identically, so the
    // simulation agrees; the wire-visible decode is -65354 * 360/65536.
    CHECK(out.meleeChargeYaw == -359.000244140625);
}

// ServerTime jump >= 0x100 takes the explicit-long branch.
void test_usercmd_time_jump()
{
    const int key = 0x12345678;
    usercmd_s from = baseCmd();
    usercmd_s to = baseCmd();
    to.serverTime = 2000; // delta 1000

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    const std::uint8_t want[] = {0x00, 0xD0, 0x07, 0x00, 0x00};
    const WireSpan spans[] = {
        {"servertime-flag", 0, 0, 1}, {"servertime-full", 1, 0, 32},
        {"changed-flag", 0, 1, 1},
    };
    compareWire(m, "usercmd time jump", want, sizeof(want), spans, 3);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 2000);
    CHECK(out.buttons == to.buttons);
    CHECK(out.angles[0] == to.angles[0]);
}

// selectedLocation bytes are gated on BUTTON_LOC_CONFIRM (0x10000) and XORed
// with keya; an odd key exercises the inverted flag parities.
void test_usercmd_selected_location()
{
    const int key = 0x00000001;
    usercmd_s from = baseCmd();
    from.serverTime = 500;
    usercmd_s to = baseCmd();
    to.serverTime = 500;
    to.buttons = BUTTON_LOC_CONFIRM; // 0x10000
    to.selectedLocation[0] = 7;
    to.selectedLocation[1] = 9;

    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);
    MSG_WriteDeltaUsercmdKey(&m, key, &from, &to);

    // Machine-verified against the production encoder: keya = 500 ^ 1 = 501
    // = 0x1F5; the 20-bit buttons>>1 payload is keya ^ 0x8000 = 0x81F5 and the
    // selLoc payloads are the raw bytes keya ^ 7 = 0xF2 and keya ^ 9 = 0xFC
    // (each preceded by its flag bit).
    const std::uint8_t want[] = {0x09, 0x00, 0xEB, 0x03, 0x81, 0xF2, 0x01, 0xFC};
    const WireSpan spans[] = {
        {"buttons>>1-20bit", 2, 0, 8}, {"buttons>>1-mid", 3, 0, 8},
        {"payload-tail+flags", 4, 0, 8}, {"selloc0-xored", 5, 0, 8},
        {"selloc1-xored", 6, 0, 8},
    };
    compareWire(m, "usercmd selectedLocation", want, sizeof(want), spans, 5);

    MSG_BeginReading(&m);
    usercmd_s out{};
    MSG_ReadDeltaUsercmdKey(&m, key, &from, &out);
    CHECK(out.serverTime == 500);
    CHECK(out.buttons == BUTTON_LOC_CONFIRM);
    CHECK(out.selectedLocation[0] == 7);
    CHECK(out.selectedLocation[1] == 9);
    CHECK(out.weapon == 1);
    CHECK(out.angles[0] == 100);
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

void test_string_framing()
{
    msg_t m;
    std::uint8_t buf[64];
    makeMsg(m, buf, 64);

    MSG_WriteString(&m, "hi");
    MSG_WriteString(&m, "a\x92" "c"); // 146 cleans to 39 ('\'') on the wire
    const std::uint8_t want[] = {'h', 'i', 0, 'a', '\'', 'c', 0};
    const WireSpan spans[] = {
        {"string0", 0, 0, 3}, {"string1", 3, 0, 4},
    };
    compareWire(m, "WriteString + I_CleanChar", want, sizeof(want), spans, 2);

    MSG_BeginReading(&m);
    CHECK(std::strcmp(MSG_ReadString(&m), "hi") == 0);
    CHECK(std::strcmp(MSG_ReadString(&m), "a'c") == 0);

    // Oversized string collapses to a single NUL byte.
    msg_t m2;
    std::uint8_t buf2[8];
    makeMsg(m2, buf2, 8);
    std::string big(1024, 'x');
    MSG_WriteString(&m2, big.c_str());
    const std::uint8_t want2[] = {0x00};
    compareWire(m2, "WriteString overflow guard", want2, sizeof(want2));

    // BigString write side + the read-side '%' -> '.' remap.
    msg_t m3;
    std::uint8_t buf3[64];
    makeMsg(m3, buf3, 64);
    char bigmsg[16];
    std::strcpy(bigmsg, "a%b");
    MSG_WriteBigString(&m3, bigmsg);
    const std::uint8_t want3[] = {'a', '%', 'b', 0};
    compareWire(m3, "WriteBigString", want3, sizeof(want3));
    MSG_BeginReading(&m3);
    CHECK(std::strcmp(MSG_ReadBigString(&m3), "a.b") == 0);

    // ReadStringLine stops at '\n'.
    msg_t m4;
    std::uint8_t buf4[8];
    makeMsg(m4, buf4, 8);
    char lineData[] = {'A', '\n', 'B', 0};
    MSG_WriteData(&m4, reinterpret_cast<std::uint8_t *>(lineData), 4);
    MSG_BeginReading(&m4);
    CHECK(std::strcmp(MSG_ReadStringLine(&m4), "A") == 0);
}

void test_read_data_and_splits()
{
    // MSG_ReadData across the primary/split boundary (netchan reassembly
    // layout), including the ki-gu2 negative-length guard.
    //
    // Contracts pinned here: (1) MSG_InitReadOnlySplit does NOT zero the
    // msg_t -- engine call sites pass zero-initialized structs and a garbage
    // readcount makes the split path read out of bounds, so tests must
    // zero-init; (2) a boundary-crossing MSG_ReadData is well-defined when
    // readcount == cursize at entry (the split index is readcount - cursize;
    // entering mid-buffer is a retail quirk that stays unpinned).
    std::uint8_t primary[2] = {0x11, 0x22};
    std::uint8_t split[2] = {0x33, 0x44};
    msg_t m{};
    MSG_InitReadOnlySplit(&m, primary, 2, split, 2);

    // Sequential contract: consume the primary half, then span into the
    // split half.
    CHECK(MSG_ReadByte(&m) == 0x11);
    CHECK(MSG_ReadByte(&m) == 0x22);
    std::uint8_t out[2] = {};
    MSG_ReadData(&m, out, 2);
    CHECK(!m.overflowed);
    CHECK(out[0] == 0x33 && out[1] == 0x44);

    // One-shot read fully inside the primary buffer.
    msg_t m1{};
    MSG_InitReadOnlySplit(&m1, primary, 2, split, 2);
    std::uint8_t out1[2] = {};
    MSG_ReadData(&m1, out1, 2);
    CHECK(!m1.overflowed);
    CHECK(out1[0] == 0x11 && out1[1] == 0x22);

    msg_t m2{};
    MSG_InitReadOnlySplit(&m2, primary, 2, split, 2);
    std::uint8_t out2[4] = {};
    MSG_ReadData(&m2, out2, -1);
    CHECK(m2.overflowed == 1);

    msg_t m3{};
    MSG_InitReadOnlySplit(&m3, primary, 2, split, 2);
    std::uint8_t out3[8];
    MSG_ReadData(&m3, out3, 8);
    CHECK(m3.overflowed == 1);

    // MSG_Discard / MSG_ClearLastReferencedEntity bookkeeping.
    msg_t m4;
    std::uint8_t buf4[4];
    makeMsg(m4, buf4, 4);
    MSG_ClearLastReferencedEntity(&m4);
    CHECK(m4.lastEntityRef == -1);
    MSG_WriteByte(&m4, 7);
    MSG_Discard(&m4);
    CHECK(m4.overflowed == 1);
    CHECK(m4.cursize == m4.readcount);
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
}

} // namespace

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

    if (g_failed)
    {
        std::fprintf(stderr, "msg-wire-format-contracts: FAILED\n");
        return 1;
    }
    std::printf("msg-wire-format-contracts: all contracts held\n");
    return 0;
}

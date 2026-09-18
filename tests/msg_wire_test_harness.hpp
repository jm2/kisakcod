// MP message bit-codec wire-contract test harness.
//
// Shared by the two wire-contract translation units:
//   - msg_wire_contract_tests.cpp        bit packing/order, byte framing,
//                                        scalar quantization, Huffman block
//                                        codec, reporter self-test
//   - msg_wire_delta_framing_tests.cpp   key-XOR deltas, usercmd delta
//                                        classes, string framing, split
//                                        buffer reads
// Both link the production codec (src/qcommon/msg_bits_mp.cpp + huffman.cpp).
//
// The header is .hpp (not .h): this repository mixes C and C++ translation
// units and static analysis parses .h as C (see .codacy.yaml), while this
// harness is C++-only (inline functions, std:: types).
//
// Test-side link stubs (kept minimal and pinned to the production bodies):
//   - I_CleanChar / I_strncpyz mirror universal/q_shared.cpp verbatim
//     semantics (q_shared.cpp itself pulls 32-bit-pinned engine headers and
//     cannot link into a hosted test binary); the wire effect of cleaning is
//     pinned by a golden, so drift in these stubs is caught. I_strncpyz is
//     written as explicit loops rather than strncpy so the harness uses no
//     MS-banned string primitives; the behavior is byte-identical (copy
//     until NUL, zero-fill up to destsize-1, force the terminator).
//   - Com_Printf / Com_PrintError / Sys_Milliseconds are silent no-op sinks;
//     the codec uses them for tracing only.
//   - MyAssertHandler is fail-closed, as in the other wire-contract tests.

#pragma once

#include <qcommon/msg_mp.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------- stubs ----
// Declared here, defined once in msg_wire_contract_tests.cpp (non-inline:
// only the production codec references them, and inline definitions would
// emit no symbols in that case).

void MyAssertHandler(const char *filename, int line, int type, const char *fmt, ...);

void QDECL Com_Printf(int channel, const char *fmt, ...);

void Com_PrintError(int channel, const char *fmt, ...);

std::uint32_t KISAK_CDECL Sys_Milliseconds();

// Verbatim semantics of universal/q_shared.cpp:518.
uint8_t I_CleanChar(uint8_t character);

// Verbatim semantics of universal/q_shared.cpp:45 (see the header comment).
void I_strncpyz(char *dest, const char *src, int destsize);

// --------------------------------------------------------------- harness ---

inline bool g_failed = false;

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

inline const char *spanFor(const WireSpan *spans, std::size_t spanCount,
                           std::size_t byteIndex, std::size_t bitIndex)
{
    // Match in absolute bit coordinates so a multi-byte span (16/32-bit
    // short/long fields) attributes drift in ANY of its bytes, not only the
    // byte the span starts in.
    const std::size_t diffBit = byteIndex * 8 + bitIndex;
    for (std::size_t i = 0; i < spanCount; ++i)
    {
        const WireSpan &s = spans[i];
        const std::size_t spanStart = s.byte * 8 + s.bit;
        if (diffBit >= spanStart && diffBit < spanStart + s.bits)
            return s.name;
    }
    return "(unmapped)";
}

// Quiet comparison core: computes the first-difference description without
// printing or touching g_failed, so the reporter itself can be self-tested
// without poisoning the run (compareWireNegative).
inline WireDiff compareWireDiff(const msg_t &m, const std::uint8_t *expected,
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
inline WireDiff compareWire(const msg_t &m, const char *what,
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
inline WireDiff compareWireNegative(const msg_t &m, const std::uint8_t *expected,
                                    std::size_t expectedSize,
                                    const WireSpan *spans = nullptr, std::size_t spanCount = 0)
{
    return compareWireDiff(m, expected, expectedSize, spans, spanCount);
}

inline msg_t makeMsg(msg_t &m, std::uint8_t *storage, int capacity)
{
    MSG_Init(&m, storage, capacity);
    return m;
}

inline usercmd_s baseCmd()
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

// ------------------------------------------------------------ test list ----

// msg_wire_contract_tests.cpp
void test_bit_width_selection();
void test_bit_packing_order();
void test_bit_byte_interleave();
void test_scalar_framing();
void test_angle16_quantization();
void test_huffman_msg_block();
void test_compare_wire_reporter();
void test_string_span_attribution();

// msg_wire_delta_framing_tests.cpp
void test_delta_key_xor();
void test_usercmd_identical();
void test_usercmd_low_path();
void test_usercmd_high_path();
void test_usercmd_time_jump();
void test_usercmd_selected_location();
void test_string_framing();
void test_read_data_and_splits();

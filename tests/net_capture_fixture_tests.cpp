// Production-codec round-trip contracts for the wire-contract target.
//
// Issue #127 / ki-dyqxl: the target previously linked only its own test TU
// (layout static_asserts + snap rounding). It now links the PRODUCTION MP
// bit codec (msg_bits_*_mp.cpp + huffman.cpp), and this TU adds the
// encode->decode round-trip half of the "Production wire contracts" evidence
// row in docs/NETWORK_COMPATIBILITY.md:
//
//   * string framing round-trips over the real writers/readers,
//   * Angle16 quantization determinism through the real codec,
//   * Huffman block end-to-end over adversarial fixed payloads,
//   * usercmd delta fidelity across a fixed command sequence,
//   * key/delta-key boundaries across widths and keys.
//
// Byte-level ENCODING goldens (bit packing, delta classes, string spans)
// stay in kisakcod-msg-wire-contract-tests; this TU pins DECODE/ENCODE
// CONSISTENCY across the input domain instead of duplicating those goldens.
// Commercial-reference capture byte-comparison lives in
// net_capture_certification.cpp.
//
// The stubs below mirror msg_wire_contract_tests.cpp verbatim (the harness
// header documents their provenance); each test binary defines them once.

#include "msg_wire_test_harness.hpp"
#include "net_capture_fixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------- stubs ----
// Defined once per test binary (see the harness header for provenance).

std::uint32_t g_stubClockMs = 0;

void MyAssertHandler(const char *filename, int line, int type, const char *fmt, ...)
{
    std::fprintf(stderr, "MyAssertHandler: %s:%d type=%d\n", filename, line, type);
    (void)fmt;
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

// Verbatim semantics of universal/q_shared.cpp:45 (see harness header note).
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

// --------------------------------------------------------------- helpers ---

namespace
{

constexpr int kMsgCapacity = 0x20000;

// Heap-backed encode buffer: 128 KiB of inline storage per instance, with
// several live in one frame, overflowed the Windows x86 ILP32 default stack
// (review r4059217206). Only the vector handle and the msg_t stay on the
// stack; the capacity is allocated on first init().
struct EncodeBuffer
{
    std::vector<std::uint8_t> storage;
    msg_t msg{};

    void init()
    {
        storage.assign(kMsgCapacity, 0);
        MSG_Init(&msg, storage.data(), kMsgCapacity);
    }
};

// Deterministic xorshift32 -- fixed seed, no library RNG, cross-arch stable.
std::uint32_t xorshift32(std::uint32_t &state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

} // namespace

// ---------------------------------------------------------------- tests ----

namespace
{

// WriteString/ReadString must round-trip the CLEANED text exactly, and the
// >=1024 overlong branch must degrade to the empty string (retail behavior),
// not error or truncate silently to a prefix.
void test_string_roundtrip_sweep()
{
    struct Case
    {
        const char *text;
        const char *what;
    };
    const Case cases[] = {
        {"", "empty"},
        {"getinfo", "plain ascii"},
        {"\x92quote", "I_CleanChar 146 -> 39 path"},
        {"mapname\txy with spaces;vers=1.7", "infostring-shaped"},
        {"\x01\x02\x7f", "control bytes preserved through cleaning"},
    };

    for (const Case &c : cases)
    {
        EncodeBuffer out;
        out.init();
        MSG_WriteString(&out.msg, c.text);

        EncodeBuffer echo;
        echo.init();
        MSG_WriteString(&echo.msg, c.text);
        CHECK(echo.msg.cursize == out.msg.cursize);
        CHECK(std::memcmp(echo.msg.data, out.msg.data, static_cast<std::size_t>(out.msg.cursize)) == 0);

        MSG_BeginReading(&out.msg);
        const char *read = MSG_ReadString(&out.msg);
        // The reader cleans again; cleaning is idempotent, so the read text
        // equals the cleaned input (39 for 146).
        std::string expected = c.text;
        for (char &ch : expected)
            ch = static_cast<char>(I_CleanChar(static_cast<std::uint8_t>(ch)));
        CHECK(std::strcmp(read, expected.c_str()) == 0);
    }

    // Overlong input: retail writes a single empty-string byte run.
    std::string overlong(1200, 'a');
    EncodeBuffer out;
    out.init();
    MSG_WriteString(&out.msg, overlong.c_str());
    EncodeBuffer empty;
    empty.init();
    MSG_WriteString(&empty.msg, "");
    CHECK(out.msg.cursize == empty.msg.cursize);
    CHECK(std::memcmp(out.msg.data, empty.msg.data, static_cast<std::size_t>(empty.msg.cursize)) == 0);
}

// BigString adds the retail '%' -> '.' read-side substitution and the 8192
// clip; both are wire-visible behavior, so both are pinned through the
// production codec.
void test_bigstring_roundtrip_sweep()
{
    EncodeBuffer out;
    out.init();
    char withPercent[] = "challenge-%-value";
    MSG_WriteBigString(&out.msg, withPercent);
    MSG_BeginReading(&out.msg);
    const char *read = MSG_ReadBigString(&out.msg);
    CHECK(std::strcmp(read, "challenge-.-value") == 0);

    std::string nearLimit(8190, 'b');
    EncodeBuffer big;
    big.init();
    MSG_WriteBigString(&big.msg, nearLimit.data());
    MSG_BeginReading(&big.msg);
    const char *bigRead = MSG_ReadBigString(&big.msg);
    CHECK(std::strlen(bigRead) == 8190);

    std::string overlong(9000, 'c');
    EncodeBuffer over;
    over.init();
    MSG_WriteBigString(&over.msg, overlong.data());
    EncodeBuffer empty;
    empty.init();
    MSG_WriteBigString(&empty.msg, const_cast<char *>(""));
    CHECK(over.msg.cursize == empty.msg.cursize);
}

// Angle16: quantization (int)(f * 182.0444488525391) is a single-valued
// function of the float; a quantized angle must re-encode to the same
// 16-bit value after decoding. Swept over the snap grid and both extremes.
void test_angle16_quantization_roundtrip()
{
    const float grid[] = {
        0.0f,   -0.0f,  0.25f,  -0.25f, 45.0f,  -45.0f, 90.0f,   180.0f,
        -180.0f, 359.9f, -359.9f, 0.4999999f, 89.9999f, 179.9999f,
    };
    for (const float angle : grid)
    {
        EncodeBuffer first;
        first.init();
        MSG_WriteAngle16(&first.msg, angle);
        const std::int16_t quantized = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(first.msg.data[0] | (first.msg.data[1] << 8)));

        const double decoded = MSG_ReadAngle16(&first.msg);

        EncodeBuffer second;
        second.init();
        MSG_WriteAngle16(&second.msg, static_cast<float>(decoded));
        const std::int16_t requantized = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(second.msg.data[0] | (second.msg.data[1] << 8)));
        CHECK(quantized == requantized);
    }
}

// Huffman block codec end-to-end: WriteBitsCompress -> ReadBitsCompress must
// reproduce the input bytes exactly over adversarial fixed payloads (the
// codebook itself is pinned by kisakcod-huffman-wire-contract-tests).
void test_huffman_end_to_end_payloads()
{
    const std::uint32_t seed = 0x12345678u;
    std::uint32_t state = seed;

    std::vector<std::uint8_t> allZero(256, 0x00);
    std::vector<std::uint8_t> allOnes(256, 0xFF);
    std::vector<std::uint8_t> alternating;
    for (int i = 0; i < 256; ++i)
        alternating.push_back(static_cast<std::uint8_t>(i & 1u ? 0xAA : 0x55));
    std::vector<std::uint8_t> ascii;
    {
        const char *text = "getstatus\xa respond with infostring values";
        ascii.insert(ascii.end(), text, text + std::strlen(text));
    }
    std::vector<std::uint8_t> pseudoRandom;
    for (int i = 0; i < 512; ++i)
        pseudoRandom.push_back(static_cast<std::uint8_t>(xorshift32(state) >> 24));

    const std::vector<std::uint8_t> *payloads[] = {
        &allZero, &allOnes, &alternating, &ascii, &pseudoRandom,
    };

    for (const bool trainHuffman : {true, false})
    {
        for (const std::vector<std::uint8_t> *payload : payloads)
        {
            // Production maxsize contract (Huff_Compress): the call fails
            // unless the output buffer covers the exact codebook bit cost
            // of the payload; the retail tree is static, so the exact
            // capacity is computable and the compressor must consume all of
            // it (review r4059217217).
            const std::size_t compressedCapacity = netcapture::huffmanCompressedCapacityFor(*payload);
            std::vector<std::uint8_t> compressed(compressedCapacity, 0);
            const int compressedSize = MSG_WriteBitsCompress(trainHuffman, payload->data(),
                                                             static_cast<int>(payload->size()),
                                                             compressed.data(),
                                                             static_cast<int>(compressed.size()));
            CHECK(compressedSize > 0);
            CHECK(static_cast<std::size_t>(compressedSize) == compressedCapacity);

            // Decoder contract: every symbol consumes at least one input
            // bit, so 8*compressedSize is a rigorous bound on producible
            // bytes; retail consumers pass an oversized buffer for the same
            // reason and the compressor's trailing pad-bit junk (up to 7
            // extra bits of symbols) cannot overflow it.
            std::vector<std::uint8_t> decompressed(compressed.size() * 8, 0);
            const int decompressedSize = MSG_ReadBitsCompress(compressed.data(), compressedSize,
                                                              decompressed.data(),
                                                              static_cast<int>(decompressed.size()));
            // Retail contract (cl_parse_mp.cpp / sv_client_mp.cpp): the
            // consumer passes an oversized buffer and the returned size is
            // whatever the decoder produced. Decompression drains every
            // input bit, so the compressor's trailing pad bits (up to 7)
            // can decode into a junk tail of extra symbols. The contract we
            // pin is payload integrity with tail tolerance: every original
            // byte comes back in order, and any tail stays within the
            // decoder's slack.
            CHECK(decompressedSize >= static_cast<int>(payload->size()));
            CHECK(decompressedSize <= static_cast<int>(decompressed.size()));
            CHECK(std::memcmp(decompressed.data(), payload->data(), payload->size()) == 0);
        }
    }
}

// Usercmd delta fidelity: a scripted command sequence encoded against its
// predecessor must decode to the exact originating command, every field,
// every frame; and encoding the same sequence twice must produce identical
// bytes (determinism of the wire under fixed input).
//
// The script only uses WIRE-REPRESENTABLE commands, mirroring the retail
// contract this codec pins:
//   * forwardmove/rightmove are quantized on the wire to the
//     MSG_HorMoveTo/HorMoveFrom flag set {0, 127, -127};
//   * meleeChargeYaw/meleeChargeDist and selectedLocation are transmitted
//     only when buttons bits 0x4 / 0x10000 are set -- with those bits clear
//     the decoder keeps the `from` values, so the script must not change
//     them between commands;
//   * buttons bit 0 rides the small arm; buttons>>1 (0x80 toggle) forces
//     the full arm, so both arms are exercised.
// Buffers are single-cursor in this engine (MSG_ReadBits repositions
// msg->bit), so each buffer is either written or read, never interleaved.
void test_usercmd_delta_sequence_fidelity()
{
    const int key = 0x1F3AB2C;

    std::vector<usercmd_s> script;
    usercmd_s cmd = baseCmd();
    script.push_back(cmd);

    for (int frame = 1; frame < 64; ++frame)
    {
        usercmd_s next = script.back();
        // 125Hz fixed tick; every 16th command jumps past the 8-bit delta
        // window to exercise the absolute serverTime path.
        next.serverTime += (frame % 16 == 0) ? 300 : 8;
        next.angles[1] += 3;
        if (frame % 7 == 0)
            next.buttons ^= 0x80; // buttons>>1 change -> full delta arm
        else if (frame % 3 == 0)
            next.buttons ^= 1; // flag bit rides the small arm
        next.forwardmove = static_cast<char>((frame % 3) == 0 ? 127
                                           : (frame % 3) == 1 ? 0 : static_cast<int>(-127));
        next.rightmove = static_cast<char>((frame % 5) < 2 ? 0 : (frame % 2) ? 127 : static_cast<int>(-127));
        script.push_back(next);
    }

    // Determinism: two independent encode passes must be byte-identical.
    EncodeBuffer run1;
    run1.init();
    EncodeBuffer run2;
    run2.init();

    for (std::size_t i = 1; i < script.size(); ++i)
    {
        MSG_WriteDeltaUsercmdKey(&run1.msg, key, &script[i - 1], &script[i]);
        MSG_WriteDeltaUsercmdKey(&run2.msg, key, &script[i - 1], &script[i]);
    }

    CHECK(run1.msg.cursize == run2.msg.cursize);
    CHECK(std::memcmp(run1.msg.data, run2.msg.data, static_cast<std::size_t>(run1.msg.cursize)) == 0);
    CHECK(MSG_GetUsedBitCount(&run1.msg) == MSG_GetUsedBitCount(&run2.msg));

    // Reader discipline: encoding left msg->bit mid-byte, and MSG_ReadBits
    // only repositions the cursor when it lands on a byte boundary. Start
    // every decode pass from a fresh reader state.
    MSG_BeginReading(&run1.msg);

    // Fidelity: decoding the stream frame by frame reconstructs each exact
    // command state.
    for (std::size_t i = 1; i < script.size(); ++i)
    {
        usercmd_s decoded{};
        MSG_ReadDeltaUsercmdKey(&run1.msg, key, &script[i - 1], &decoded);
        CHECK(std::memcmp(&decoded, &script[i], sizeof(usercmd_s)) == 0);
    }
}

// Key and delta-key round-trips across the widths the engine uses, boundary
// values included (0, all-ones, sign boundaries, key-masked flips). The
// readers return the value masked to the written width (kbitmask[bits] &
// key) ^ wire, so the round-trip identity is value-truncated-to-width; the
// delta path returns oldV verbatim on the unchanged branch.
void test_key_delta_boundary_sweep()
{
    auto maskFor = [](std::uint32_t bits) -> std::uint32_t
    {
        return bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    };

    struct WidthCase
    {
        std::uint32_t bits;
        std::vector<int> values;
    };
    const WidthCase widths[] = {
        {8, {0, 1, 0x7F, static_cast<int>(0xFFFFFF80), static_cast<int>(0xFFFFFF88)}},
        {16, {0, 1, 0x7FFF, static_cast<int>(0xFFFF8000), static_cast<int>(0xFFFF8877)}},
        {32, {0, 1, 0x7FFFFFFF, static_cast<int>(0x80000000), -1, 0x12345678}},
    };
    const int keys[] = {0, 1, 0x7F, 0x80, 0xFF, 0x12345678, static_cast<int>(0x87654321)};

    for (const WidthCase &w : widths)
    {
        const std::uint32_t mask = maskFor(w.bits);
        for (const int key : keys)
        {
            for (const int value : w.values)
            {
                const std::uint32_t expected = static_cast<std::uint32_t>(value) & mask;

                EncodeBuffer out;
                out.init();
                MSG_WriteKey(&out.msg, key, value, w.bits);
                MSG_BeginReading(&out.msg);
                const std::uint32_t readKey = MSG_ReadKey(&out.msg, key, w.bits);
                CHECK(readKey == expected);

                for (const int oldValue : {0, 1, -1, 0x12345678})
                {
                    EncodeBuffer delta;
                    delta.init();
                    MSG_WriteDeltaKey(&delta.msg, key, oldValue, value, w.bits);
                    MSG_BeginReading(&delta.msg);
                    const std::uint32_t readDelta = MSG_ReadDeltaKey(&delta.msg, key, oldValue, w.bits);
                    const std::uint32_t expectedDelta = (oldValue == value)
                                                            ? static_cast<std::uint32_t>(oldValue)
                                                            : expected;
                    CHECK(readDelta == expectedDelta);
                }
            }
        }
    }
}

} // namespace

void run_net_capture_roundtrip_contracts()
{
    test_string_roundtrip_sweep();
    test_bigstring_roundtrip_sweep();
    test_angle16_quantization_roundtrip();
    test_huffman_end_to_end_payloads();
    test_usercmd_delta_sequence_fidelity();
    test_key_delta_boundary_sweep();
}

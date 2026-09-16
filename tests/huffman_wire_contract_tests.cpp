// Huffman wire-format contracts.
//
// The commercial MP protocol compresses message payloads with a FIXED Huffman
// code built from the 256 symbol weights in msg_hData (now shared from
// src/qcommon/msg_huffman_data.h). Wire fidelity therefore depends on three
// things staying put:
//
//   1. the weight table,
//   2. the tree the production builder derives from it, and
//   3. the exact code words and bit/byte output of Huff_Compress.
//
// This test links the ACTUAL production Huffman implementation
// (src/qcommon/huffman.cpp) and pins all three against fixed-input synthetic
// fixtures. These are sanitized early-work fixtures: they lock the retail-table
// code the fork emits and catch accidental drift. They are NOT a substitute for
// the commercial-binary byte-comparison certification gate in
// docs/NETWORK_COMPATIBILITY.md, which still requires authentic 1.7/Steam-1.8
// references (#122).
//
// Tie ordering. The production comparator orders candidate nodes by weight
// only, exactly like retail. msg_hData contains duplicate weights (symbols 228
// and 231 are both 4683), so weight alone is not a total order and the tree the
// builder derives from the equal-weight nodes is defined by the host qsort's
// implementation-defined handling of equal elements. glibc and the Windows/
// macOS C libraries pick opposite orders, which swaps the two equal-weight
// 9-bit code words and changes the compressed bytes of any input containing
// both symbols. This test therefore:
//
//   - pins the platform-INDEPENDENT contracts on every host (the weight table,
//     the code-length histogram, per-symbol emitted bit counts, the two
//     complementary equal-weight code words, round trips and decoder
//     boundaries), and
//   - pins byte-exact output only for fixtures whose bytes are tie-order
//     independent, plus the tie-dependent fixtures on a host whose qsort
//     produced the ordering the recorded goldens were captured with.
//
// Neither equal-weight ordering is certified as retail-correct. Selecting one
// requires the authentic reference evidence tracked by #122, so the test
// reports the ordering the host derived instead of silently normalizing it or
// changing the production comparator to force one codebook.

#include <qcommon/huffman.h>
#include <qcommon/msg_huffman_data.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// The production Huffman primitives call MyAssertHandler directly (see
// Huff_bitCount). Release builds have no engine assert handler to link, so the
// test supplies a fail-closed one: every fixture uses in-range symbols, so any
// invocation is a defect, and we abort rather than limp on.
void MyAssertHandler(const char *filename, int line, int type, const char *fmt, ...)
{
    (void)fmt;
    std::fprintf(stderr, "unexpected production assert at %s:%d (type %d)\n",
                 filename, line, type);
    std::abort();
}

namespace
{
bool g_failed = false;

// Which of the two weight-consistent code books this host's qsort derived.
// Both are valid Huffman codes for the retail weight table; the choice is
// implementation-defined by the comparator and is not certified as retail.
enum class TieOrder
{
    kUnknown,
    kSymbol228First,
    kSymbol231First,
};

TieOrder g_tieOrder = TieOrder::kUnknown;

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond))                                                              \
        {                                                                         \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            g_failed = true;                                                      \
        }                                                                         \
    } while (0)

std::uint64_t fnv1a64(const std::uint8_t *data, std::size_t size)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i)
    {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

huffman_t g_huff;

void buildTree()
{
    Huff_Init(&g_huff);
    Huff_BuildFromData(&g_huff.compressDecompress, msg_hData);
}

std::vector<std::uint8_t> compress(const std::uint8_t *data, std::size_t size)
{
    std::vector<std::uint8_t> out(size * 2 + 128);
    const int produced = Huff_Compress(&g_huff.compressDecompress, data,
                                       static_cast<int>(size), out.data(),
                                       static_cast<int>(out.size()));
    if (produced < 0)
        out.clear();
    else
        out.resize(static_cast<std::size_t>(produced));
    return out;
}

int decompress(const std::vector<std::uint8_t> &compressed,
               std::vector<std::uint8_t> &out)
{
    return Huff_Decompress(g_huff.compressDecompress.tree, compressed.data(),
                           static_cast<int>(compressed.size()), out.data(),
                           static_cast<int>(out.size()));
}

// Emit the exact bit sequence Huff_Compress would write for one symbol,
// trimmed to whole bytes (the trailing pad bits are not part of the code).
std::vector<std::uint8_t> emitCode(int symbol)
{
    std::vector<std::uint8_t> code(16, 0);
    int offset = 0;
    Huff_offsetTransmit(&g_huff.compressDecompress, symbol, code.data(), &offset);
    code.resize(static_cast<std::size_t>((offset + 7) / 8));
    return code;
}

// ---------------------------------------------------------------------------
// Fixed inputs. All are synthetic and redistributable; none is a commercial
// capture.
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> fixtureIdentity32()
{
    std::vector<std::uint8_t> v(32);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<std::uint8_t>(i);
    return v;
}

std::vector<std::uint8_t> fixtureStride64()
{
    std::vector<std::uint8_t> v(64);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<std::uint8_t>((i * 37) & 0xFF);
    return v;
}

std::vector<std::uint8_t> fixtureZeros4096()
{
    return std::vector<std::uint8_t>(4096, 0);
}

std::vector<std::uint8_t> fixtureFullAlphabet()
{
    std::vector<std::uint8_t> v(256);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = static_cast<std::uint8_t>(i);
    return v;
}

// F1 = 0x00..0x1F. Explicit golden bytes: a small, reviewable fixture that
// contains neither equal-weight symbol, so it is tie-order independent.
const std::uint8_t kF1Compressed[] = {
    0x29, 0xdb, 0xfa, 0x4d, 0x80, 0xab, 0x1b, 0x61, 0xa7, 0x43,
    0x4b, 0xdd, 0x5a, 0xa2, 0xc4, 0x54, 0xee, 0xef, 0x75, 0xa4,
    0xbc, 0xca, 0x83, 0x7b, 0x38, 0x4b, 0x34, 0x83, 0x31,
};

// F2 = (i * 37) & 0xFF for i in [0, 64). Contains both equal-weight symbols.
// Golden bytes recorded on a host that ordered symbol 228 before 231.
const std::uint8_t kF2Compressed[] = {
    0x69, 0x71, 0xfc, 0x49, 0x1b, 0x82, 0x8f, 0xd7, 0x63, 0xff,
    0xff, 0xc1, 0xbb, 0x56, 0x1b, 0x88, 0xc3, 0xf9, 0xa1, 0x06,
    0x5e, 0x1f, 0xe3, 0x32, 0xc8, 0xf8, 0xa8, 0x3d, 0xfa, 0x0e,
    0x31, 0xcc, 0x36, 0xb5, 0x75, 0x61, 0x48, 0xbb, 0xe3, 0x52,
    0xec, 0x2e, 0x6c, 0xf1, 0xa6, 0xb2, 0xad, 0x2a, 0xe1, 0x1f,
    0xd9, 0x7c, 0xfd, 0xa6, 0xae, 0x74, 0xef, 0x9a, 0x77, 0xab,
    0x3c, 0x3a, 0xda, 0x4b, 0x14, 0x9b, 0x85, 0x70,
};

// Size pins. Huffman code lengths do not depend on the tie order (the change
// swaps two code words of equal length), so these hold on every host.
const std::size_t kZerosCompressedSize = 1536;
const std::size_t kAlphabetCompressedSize = 273;

// Content pins recorded on the 228-first host. The zeros fixture uses only
// symbol 0, so its bytes are tie-order independent; the alphabet fixture
// contains both equal-weight symbols, so its exact bytes are not.
const std::uint64_t kZerosFnv = 2387247832005793061ULL;
const std::uint64_t kAlphabetFnv228First = 1399084440640432086ULL;

// Pinned code-length histogram over the 256 symbols (index = bit length).
// min length 3, max length 11; counts sum to 256.
const int kLengthHistogram[12] = {0, 0, 0, 1, 0, 2, 8, 15, 69, 147, 13, 1};
} // namespace

// Compare a compressed stream with a golden only after the sizes match, so a
// short production result can never drive a memcmp past the buffer end.
// CHECK records a failure and continues, which would otherwise make the
// following read unsafe.
void checkExactBytes(const char *label, const std::vector<std::uint8_t> &actual,
                     const std::uint8_t *expected, std::size_t expectedSize)
{
    if (actual.size() != expectedSize)
    {
        std::fprintf(stderr, "FAIL %s: compressed size %zu != golden %zu\n",
                     label, actual.size(), expectedSize);
        g_failed = true;
        return;
    }
    if (std::memcmp(actual.data(), expected, expectedSize) != 0)
    {
        std::fprintf(stderr, "FAIL %s: compressed bytes differ from golden\n",
                     label);
        g_failed = true;
    }
}

void checkPinnedSize(const char *label, const std::vector<std::uint8_t> &actual,
                     std::size_t expected)
{
    if (actual.size() != expected)
    {
        std::fprintf(stderr, "FAIL %s: compressed size %zu != %zu\n",
                     label, actual.size(), expected);
        g_failed = true;
    }
}

void checkRoundTrip(const std::vector<std::uint8_t> &input,
                    const std::vector<std::uint8_t> &compressed)
{
    std::vector<std::uint8_t> out(input.size() + 32);
    const int decoded = decompress(compressed, out);
    CHECK(decoded == static_cast<int>(input.size()));
    if (decoded == static_cast<int>(input.size()))
        CHECK(std::memcmp(out.data(), input.data(), input.size()) == 0);
}

// --- 1. Shared table integrity ---------------------------------------------
// msg_hData is the retail weight table. If any value changes, the code
// changes and interoperability breaks, so pin the exact bytes.
void checkTableIntegrity()
{
    std::uint64_t tableFnv = 14695981039346656037ULL;
    std::uint64_t tableSum = 0;
    for (int i = 0; i < 256; ++i)
    {
        const int w = msg_hData[i];
        CHECK(w > 0);
        tableSum += static_cast<std::uint64_t>(w);
        for (int shift = 0; shift < 32; shift += 8)
        {
            tableFnv ^= static_cast<std::uint8_t>((w >> shift) & 0xFF);
            tableFnv *= 1099511628211ULL;
        }
    }
    CHECK(tableSum == 2154226ULL);
    CHECK(tableFnv == 7312978016625600390ULL);
}

// --- 2. Equal-weight code book characterization ----------------------------
// msg_hData gives symbols 228 and 231 the same weight (4683). Any valid Huffman
// code from that table must assign each of them one of the two complementary
// 9-bit code words below; only which symbol gets which depends on the host
// qsort. Record the host's choice and fail if it is neither.
void detectTieOrder()
{
    static const std::vector<std::uint8_t> kFirst228 = {0x7d, 0x00};
    static const std::vector<std::uint8_t> kFirst231 = {0x7d, 0x01};

    CHECK(msg_hData[228] == msg_hData[231]);

    const std::vector<std::uint8_t> code228 = emitCode(228);
    const std::vector<std::uint8_t> code231 = emitCode(231);

    if (code228 == kFirst228 && code231 == kFirst231)
    {
        g_tieOrder = TieOrder::kSymbol228First;
    }
    else if (code228 == kFirst231 && code231 == kFirst228)
    {
        g_tieOrder = TieOrder::kSymbol231First;
    }
    else
    {
        g_tieOrder = TieOrder::kUnknown;
        std::fprintf(stderr,
                     "FAIL: equal-weight symbols 228/231 did not receive the "
                     "expected complementary 9-bit code words\n");
        g_failed = true;
    }
}

// --- 3. Derived code length distribution -----------------------------------
void checkLengthHistogram()
{
    int histogram[12] = {0};
    for (int i = 0; i < 256; ++i)
    {
        const int length = Huff_bitCount(&g_huff.compressDecompress, i);
        CHECK(length >= 1 && length <= 11);
        if (length >= 1 && length <= 11)
            ++histogram[length];
    }
    for (int length = 1; length <= 11; ++length)
        CHECK(histogram[length] == kLengthHistogram[length]);
}

// --- 4. Code words agree with the reported lengths -------------------------
// Huff_offsetTransmit is the exact bit emitter used by Huff_Compress, so a
// disagreement with Huff_bitCount would mean the compressor writes a
// different number of bits than the codebook advertises.
void checkEmittedBitsMatchLengths()
{
    for (int symbol = 0; symbol < 256; ++symbol)
    {
        std::uint8_t code[16];
        std::memset(code, 0, sizeof(code));
        int offset = 0;
        Huff_offsetTransmit(&g_huff.compressDecompress, symbol, code, &offset);
        CHECK(offset == Huff_bitCount(&g_huff.compressDecompress, symbol));
    }
}

// --- 5. Tie-order-independent fixed-input fixtures -------------------------
void checkInvariantFixtures(const std::vector<std::uint8_t> &cIdentity,
                            const std::vector<std::uint8_t> &cZeros)
{
    checkExactBytes("F1", cIdentity, kF1Compressed, sizeof(kF1Compressed));
    checkPinnedSize("zeros", cZeros, kZerosCompressedSize);
    if (cZeros.size() == kZerosCompressedSize)
        CHECK(fnv1a64(cZeros.data(), cZeros.size()) == kZerosFnv);
}

// --- 6. Tie-order-dependent fixed-input fixtures ---------------------------
void checkTieDependentFixtures(const std::vector<std::uint8_t> &cStride,
                               const std::vector<std::uint8_t> &cAlphabet)
{
    // Code lengths are tie-order independent, so the stream sizes still pin
    // exactly even though the byte content does not.
    checkPinnedSize("F2", cStride, sizeof(kF2Compressed));
    checkPinnedSize("alphabet", cAlphabet, kAlphabetCompressedSize);

    if (g_tieOrder == TieOrder::kSymbol228First)
    {
        // The goldens were recorded with symbol 228 before 231. Byte-exact
        // comparison is only meaningful for that ordering.
        if (cStride.size() == sizeof(kF2Compressed))
            CHECK(std::memcmp(cStride.data(), kF2Compressed,
                              sizeof(kF2Compressed)) == 0);
        if (cAlphabet.size() == kAlphabetCompressedSize)
            CHECK(fnv1a64(cAlphabet.data(), cAlphabet.size()) ==
                  kAlphabetFnv228First);
        return;
    }

    // This host ordered symbol 231 before 228, so the two equal-weight code
    // words are swapped and the tie-dependent byte streams legitimately differ.
    // Neither ordering is certified as retail-correct; byte-exact comparison
    // for these inputs waits on the #122 reference evidence. Report it rather
    // than normalize it or change production to force one code book.
    std::fprintf(stdout,
                 "note: host ordered symbol 231 before 228; the two equal-weight "
                 "9-bit code words are swapped, so byte-exact F2/alphabet "
                 "comparison is deferred to issue #122\n");
}

void checkFixedInputFixtures()
{
    const std::vector<std::uint8_t> identity = fixtureIdentity32();
    const std::vector<std::uint8_t> stride = fixtureStride64();
    const std::vector<std::uint8_t> zeros = fixtureZeros4096();
    const std::vector<std::uint8_t> alphabet = fixtureFullAlphabet();

    const std::vector<std::uint8_t> cIdentity =
        compress(identity.data(), identity.size());
    const std::vector<std::uint8_t> cStride =
        compress(stride.data(), stride.size());
    const std::vector<std::uint8_t> cZeros =
        compress(zeros.data(), zeros.size());
    const std::vector<std::uint8_t> cAlphabet =
        compress(alphabet.data(), alphabet.size());

    checkInvariantFixtures(cIdentity, cZeros);
    checkTieDependentFixtures(cStride, cAlphabet);

    // --- 7. Round trips through the production decoder ---------------------
    checkRoundTrip(identity, cIdentity);
    checkRoundTrip(stride, cStride);
    checkRoundTrip(zeros, cZeros);
    checkRoundTrip(alphabet, cAlphabet);
}

// --- 8. Decoder boundary behavior ------------------------------------------
void checkDecoderBoundaries()
{
    // A run of zero bits walks left until the code runs out partway through
    // a symbol; the production decoder stops there and reports the symbols
    // it completed. Pin the count so a tree/length change is visible.
    std::vector<std::uint8_t> zeroInput(16, 0);
    std::vector<std::uint8_t> zeroOutput(256);
    const int decoded = decompress(zeroInput, zeroOutput);
    CHECK(decoded == 25);

    // The 257th tree leaf is the dummy symbol 256. A valid peer never
    // emits it, so the decoder must reject it rather than write it out.
    std::uint8_t dummyCode[16];
    std::memset(dummyCode, 0, sizeof(dummyCode));
    int dummyBits = 0;
    Huff_offsetTransmit(&g_huff.compressDecompress, 256, dummyCode, &dummyBits);
    CHECK(dummyBits == 11);
    std::vector<std::uint8_t> dummyOutput(64);
    const int dummyDecoded = Huff_Decompress(
        g_huff.compressDecompress.tree, dummyCode,
        static_cast<int>((dummyBits + 7) / 8), dummyOutput.data(),
        static_cast<int>(dummyOutput.size()));
    CHECK(dummyDecoded == -1);
}

// --- 9. Primitive read/write agreement -------------------------------------
void checkPrimitiveReadWrite()
{
    std::uint8_t code[16];
    std::memset(code, 0, sizeof(code));
    int offset = 0;
    Huff_offsetTransmit(&g_huff.compressDecompress, 'A', code, &offset);

    int symbol = -1;
    int readOffset = 0;
    const bool read = Huff_offsetReceive(g_huff.compressDecompress.tree,
                                         &symbol, code, &readOffset, offset);
    CHECK(read);
    CHECK(symbol == 'A');
    CHECK(readOffset == offset);

    // One bit is shorter than the shortest code (length 3): refuse.
    int truncatedSymbol = -1;
    int truncatedOffset = 0;
    const bool truncated = Huff_offsetReceive(
        g_huff.compressDecompress.tree, &truncatedSymbol, code,
        &truncatedOffset, 1);
    CHECK(!truncated);
}

// --- 10. Argument and capacity guards --------------------------------------
void checkArgumentAndCapacityGuards()
{
    std::vector<std::uint8_t> input = fixtureStride64();
    std::uint8_t out[512];
    std::vector<std::uint8_t> compressed;
    int result = 0;

    CHECK(Huff_Compress(nullptr, input.data(), 64, out, 512) == -1);
    CHECK(Huff_Compress(&g_huff.compressDecompress, nullptr, 64, out, 512) == -1);
    CHECK(Huff_Compress(&g_huff.compressDecompress, input.data(), -1, out, 512) == -1);
    CHECK(Huff_Compress(&g_huff.compressDecompress, input.data(), 64, nullptr, 512) == -1);
    CHECK(Huff_Compress(&g_huff.compressDecompress, input.data(), 64, out, -1) == -1);
    // No room for the fixed code: refuse rather than emit a partial stream.
    CHECK(Huff_Compress(&g_huff.compressDecompress, input.data(), 64, out, 0) == -1);

    compressed = compress(input.data(), input.size());
    checkPinnedSize("guards F2", compressed, sizeof(kF2Compressed));

    const int size = static_cast<int>(compressed.size());
    CHECK(Huff_Decompress(nullptr, compressed.data(), size, out, 512) == -1);
    CHECK(Huff_Decompress(g_huff.compressDecompress.tree, nullptr, size, out, 512) == -1);
    CHECK(Huff_Decompress(g_huff.compressDecompress.tree, compressed.data(), -1, out, 512) == -1);
    CHECK(Huff_Decompress(g_huff.compressDecompress.tree, compressed.data(), size, nullptr, 512) == -1);
    CHECK(Huff_Decompress(g_huff.compressDecompress.tree, compressed.data(), size, out, -1) == -1);
    CHECK(Huff_Decompress(g_huff.compressDecompress.tree, compressed.data(),
                          INT_MAX / 8 + 1, out, 512) == -1);
    // Output buffer cannot hold even the first symbol.
    CHECK(Huff_Decompress(g_huff.compressDecompress.tree, compressed.data(), size, out, 0) == -1);

    // Empty input is a valid no-op with non-null buffers.
    std::uint8_t dummy = 0;
    const int outCapacity = static_cast<int>(sizeof(out));
    result = Huff_Compress(&g_huff.compressDecompress, &dummy, 0, out, outCapacity);
    CHECK(result == 0);
    result = Huff_Decompress(g_huff.compressDecompress.tree, &dummy, 0, out, outCapacity);
    CHECK(result == 0);
}

int main()
{
    checkTableIntegrity();

    buildTree();

    detectTieOrder();
    checkLengthHistogram();
    checkEmittedBitsMatchLengths();
    checkFixedInputFixtures();
    checkDecoderBoundaries();
    checkPrimitiveReadWrite();
    checkArgumentAndCapacityGuards();

    if (g_failed)
    {
        std::fprintf(stderr, "huffman wire contracts FAILED\n");
        return 1;
    }

    std::fprintf(stdout, "huffman wire contracts OK (tie order: %s)\n",
                 g_tieOrder == TieOrder::kSymbol228First ? "228-first"
                                                         : "231-first");
    return 0;
}

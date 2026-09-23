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
// the Steam 1.8 byte-comparison certification gate in
// docs/design/NET_STEAM18.md, which still requires authentic Steam 1.8
// captures.
//
// Tie ordering. The production comparator orders candidate nodes by weight
// only, exactly like retail. msg_hData contains two duplicate weights --
// 3889 at symbols 155/205 and 4683 at symbols 228/231 -- so weight alone is not
// a total order and the tree the builder derives from those equal-weight nodes
// is defined by the host qsort's implementation-defined handling of equal
// elements. Different C libraries choose different orders, which changes the
// compressed bytes of any input containing an affected symbol. This test
// therefore:
//
//   - pins the platform-INDEPENDENT contracts on every host (the weight table,
//     the code-length histogram, per-symbol emitted bit counts, the exact pair
//     of code words each equal-weight pair may take, round trips and decoder
//     boundaries), and
//   - pins byte-exact output only for fixtures whose bytes this host's
//     equal-weight ordering can produce, and reports the ordering it did derive
//     otherwise.
//
// Neither ordering of either pair is certified as retail-correct. Selecting one
// requires the authentic reference evidence tracked by #122, so the test
// reports what it found instead of silently normalizing it or changing the
// production comparator to force one code book.

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

// How a host's qsort ordered one equal-weight pair. kReference is the ordering
// the recorded goldens were captured with (lower symbol gets the lower code
// word); kAlternate is the mirror image. Both are valid Huffman codes for the
// retail table; neither is certified as retail without #122 evidence.
enum class PairOrder
{
    kUnknown,
    kReference,
    kAlternate,
};

PairOrder g_pair155 = PairOrder::kUnknown;
PairOrder g_pair228 = PairOrder::kUnknown;

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

// Reference code words for the two equal-weight pairs, recorded on a host whose
// qsort ordered the lower symbol first. Each pair must always take exactly
// these two code words; only the assignment may differ between hosts.
const std::uint8_t kCode155Reference[] = {0xe6, 0x01};
const std::uint8_t kCode205Reference[] = {0x16, 0x00};
const std::uint8_t kCode228Reference[] = {0x7d, 0x00};
const std::uint8_t kCode231Reference[] = {0x7d, 0x01};

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
// contains none of the equal-weight symbols, so its bytes are tie-order
// independent and identical on every host observed so far.
const std::uint8_t kF1Compressed[] = {
    0x29, 0xdb, 0xfa, 0x4d, 0x80, 0xab, 0x1b, 0x61, 0xa7, 0x43,
    0x4b, 0xdd, 0x5a, 0xa2, 0xc4, 0x54, 0xee, 0xef, 0x75, 0xa4,
    0xbc, 0xca, 0x83, 0x7b, 0x38, 0x4b, 0x34, 0x83, 0x31,
};

// F2 = (i * 37) & 0xFF for i in [0, 64). It contains the 228/231 pair but not
// the 155/205 pair, so its bytes depend only on the 228/231 ordering. Golden
// bytes recorded with the reference ordering.
const std::uint8_t kF2Compressed[] = {
    0x69, 0x71, 0xfc, 0x49, 0x1b, 0x82, 0x8f, 0xd7, 0x63, 0xff,
    0xff, 0xc1, 0xbb, 0x56, 0x1b, 0x88, 0xc3, 0xf9, 0xa1, 0x06,
    0x5e, 0x1f, 0xe3, 0x32, 0xc8, 0xf8, 0xa8, 0x3d, 0xfa, 0x0e,
    0x31, 0xcc, 0x36, 0xb5, 0x75, 0x61, 0x48, 0xbb, 0xe3, 0x52,
    0xec, 0x2e, 0x6c, 0xf1, 0xa6, 0xb2, 0xad, 0x2a, 0xe1, 0x1f,
    0xd9, 0x7c, 0xfd, 0xa6, 0xae, 0x74, 0xef, 0x9a, 0x77, 0xab,
    0x3c, 0x3a, 0xda, 0x4b, 0x14, 0x9b, 0x85, 0x70,
};

// Size pins. Huffman code lengths do not depend on the tie order (the choice
// only reassigns code words of equal length), so these hold on every host.
const std::size_t kZerosCompressedSize = 1536;
const std::size_t kAlphabetCompressedSize = 273;

// Content pins recorded with both pairs in the reference ordering. The zeros
// fixture uses only symbol 0 (tie-order independent); the alphabet fixture
// contains both equal-weight pairs, so its exact bytes depend on both.
const std::uint64_t kZerosFnv = 2387247832005793061ULL;
const std::uint64_t kAlphabetFnvReference = 1399084440640432086ULL;

// FNV-1a over the whole derived code book (for each symbol in order: its bit
// length, then its code bytes), recorded with both pairs in the reference
// ordering. The alphabet fixture's bytes are only comparable to the reference
// golden when this whole code book matches, so gate that assertion on it.
const std::uint64_t kReferenceCodebookFnv = 9186525495699572604ULL;

// Pinned code-length histogram over the 256 symbols (index = bit length).
// min length 3, max length 11; counts sum to 256.
const int kLengthHistogram[12] = {0, 0, 0, 1, 0, 2, 8, 15, 69, 147, 13, 1};

PairOrder classifyPair(int lowSymbol, int highSymbol,
                       const std::uint8_t *referenceLow,
                       const std::uint8_t *referenceHigh)
{
    const std::vector<std::uint8_t> codeLow = emitCode(lowSymbol);
    const std::vector<std::uint8_t> codeHigh = emitCode(highSymbol);
    const std::vector<std::uint8_t> refLow(referenceLow, referenceLow + 2);
    const std::vector<std::uint8_t> refHigh(referenceHigh, referenceHigh + 2);

    if (codeLow == refLow && codeHigh == refHigh)
        return PairOrder::kReference;
    if (codeLow == refHigh && codeHigh == refLow)
        return PairOrder::kAlternate;

    // Fail closed: a pair that takes anything other than its two known code
    // words is an equal-weight-ordering change this test does not characterize.
    std::fprintf(stderr,
                 "FAIL: equal-weight symbols %d/%d did not take the expected "
                 "complementary code words\n",
                 lowSymbol, highSymbol);
    g_failed = true;
    return PairOrder::kUnknown;
}

// Fingerprint every derived code word so a host whose whole code book matches
// the reference capture can be distinguished from one that only matches on the
// symbols a given fixture happens to use.
std::uint64_t codebookFingerprint()
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (int symbol = 0; symbol < 256; ++symbol)
    {
        const std::vector<std::uint8_t> code = emitCode(symbol);
        const std::uint8_t bits =
            static_cast<std::uint8_t>(Huff_bitCount(&g_huff.compressDecompress,
                                                    symbol));
        hash ^= bits;
        hash *= 1099511628211ULL;
        for (std::size_t i = 0; i < code.size(); ++i)
        {
            hash ^= code[i];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}
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
// msg_hData duplicates two weights: 3889 at symbols 155/205 and 4683 at
// symbols 228/231. Each pair must take the same two complementary code words
// on every host; only which symbol gets which is implementation-defined.
// Classify both pairs and fail if either does something uncharacterized.
void detectTieOrder()
{
    CHECK(msg_hData[155] == msg_hData[205]);
    CHECK(msg_hData[228] == msg_hData[231]);

    g_pair155 = classifyPair(155, 205, kCode155Reference, kCode205Reference);
    g_pair228 = classifyPair(228, 231, kCode228Reference, kCode231Reference);
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
// F2 depends on the 228/231 ordering only; the alphabet fixture depends on
// both pairs. Assert byte-exact content exactly when this host derived the
// ordering the goldens were recorded with; otherwise report the difference.
void checkTieDependentFixtures(const std::vector<std::uint8_t> &cStride,
                               const std::vector<std::uint8_t> &cAlphabet)
{
    checkPinnedSize("F2", cStride, sizeof(kF2Compressed));
    checkPinnedSize("alphabet", cAlphabet, kAlphabetCompressedSize);

    const bool codebookMatchesReference =
        codebookFingerprint() == kReferenceCodebookFnv;

    if (codebookMatchesReference)
    {
        if (cStride.size() == sizeof(kF2Compressed))
            CHECK(std::memcmp(cStride.data(), kF2Compressed,
                              sizeof(kF2Compressed)) == 0);
        if (cAlphabet.size() == kAlphabetCompressedSize)
            CHECK(fnv1a64(cAlphabet.data(), cAlphabet.size()) ==
                  kAlphabetFnvReference);
        return;
    }

    // This host derived a different weight-consistent code book from the
    // reference capture. Neither ordering is certified as retail-correct; the
    // authentic 1.7/Steam-1.8 references (#122) are required before either byte
    // stream can be called compatible. Report the ordering this host derived
    // instead of normalizing it or changing production to force one code book.
    std::fprintf(stdout,
                 "note: host derived a different weight-consistent code book "
                 "(155/205=%s, 228/231=%s); byte-exact F2/alphabet comparison "
                 "deferred to issue #122\n",
                 g_pair155 == PairOrder::kReference ? "reference" : "alternate",
                 g_pair228 == PairOrder::kReference ? "reference" : "alternate");
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

    std::fprintf(stdout,
                 "huffman wire contracts OK (155/205=%s, 228/231=%s)\n",
                 g_pair155 == PairOrder::kReference ? "reference" : "alternate",
                 g_pair228 == PairOrder::kReference ? "reference" : "alternate");
    return 0;
}

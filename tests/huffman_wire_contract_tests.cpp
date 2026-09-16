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
// references.
//
// Compatibility caveat this test makes observable: a Huffman codebook is only
// wire-compatible if every peer derives the SAME code. The production builder
// orders candidate nodes with qsort, whose tie-breaking is implementation
// defined, so the codebook is only safe to pin -- and to trust across libc
// implementations -- while ties cannot change it. The pinned bytes below fail
// loudly if that ever stops being true.

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

// F1 = 0x00..0x1F. Explicit golden bytes: a small, reviewable fixture.
const std::uint8_t kF1Compressed[] = {
    0x29, 0xdb, 0xfa, 0x4d, 0x80, 0xab, 0x1b, 0x61, 0xa7, 0x43,
    0x4b, 0xdd, 0x5a, 0xa2, 0xc4, 0x54, 0xee, 0xef, 0x75, 0xa4,
    0xbc, 0xca, 0x83, 0x7b, 0x38, 0x4b, 0x34, 0x83, 0x31,
};

// F2 = (i * 37) & 0xFF for i in [0, 64). Explicit golden bytes.
const std::uint8_t kF2Compressed[] = {
    0x69, 0x71, 0xfc, 0x49, 0x1b, 0x82, 0x8f, 0xd7, 0x63, 0xff,
    0xff, 0xc1, 0xbb, 0x56, 0x1b, 0x88, 0xc3, 0xf9, 0xa1, 0x06,
    0x5e, 0x1f, 0xe3, 0x32, 0xc8, 0xf8, 0xa8, 0x3d, 0xfa, 0x0e,
    0x31, 0xcc, 0x36, 0xb5, 0x75, 0x61, 0x48, 0xbb, 0xe3, 0x52,
    0xec, 0x2e, 0x6c, 0xf1, 0xa6, 0xb2, 0xad, 0x2a, 0xe1, 0x1f,
    0xd9, 0x7c, 0xfd, 0xa6, 0xae, 0x74, 0xef, 0x9a, 0x77, 0xab,
    0x3c, 0x3a, 0xda, 0x4b, 0x14, 0x9b, 0x85, 0x70,
};

// Pinned code-length histogram over the 256 symbols (index = bit length).
// min length 3, max length 11; counts sum to 256.
const int kLengthHistogram[12] = {0, 0, 0, 1, 0, 2, 8, 15, 69, 147, 13, 1};

void checkRoundTrip(const std::vector<std::uint8_t> &input,
                    const std::vector<std::uint8_t> &compressed)
{
    std::vector<std::uint8_t> out(input.size() + 32);
    const int decoded = decompress(compressed, out);
    CHECK(decoded == static_cast<int>(input.size()));
    if (decoded == static_cast<int>(input.size()))
        CHECK(std::memcmp(out.data(), input.data(), input.size()) == 0);
}
} // namespace

int main()
{
    // --- 1. Shared table integrity -----------------------------------------
    // msg_hData is the retail weight table. If any value changes, the code
    // changes and interoperability breaks, so pin the exact bytes.
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

    buildTree();

    // --- 2. Derived code length distribution -------------------------------
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

    // --- 3. Code words agree with the reported lengths ---------------------
    // Huff_offsetTransmit is the exact bit emitter used by Huff_Compress, so a
    // disagreement with Huff_bitCount would mean the compressor writes a
    // different number of bits than the codebook advertises.
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

    // --- 4. Fixed-input byte fixtures --------------------------------------
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

        CHECK(cIdentity.size() == sizeof(kF1Compressed));
        CHECK(std::memcmp(cIdentity.data(), kF1Compressed,
                          sizeof(kF1Compressed)) == 0);

        CHECK(cStride.size() == sizeof(kF2Compressed));
        CHECK(std::memcmp(cStride.data(), kF2Compressed,
                          sizeof(kF2Compressed)) == 0);

        CHECK(cZeros.size() == 1536);
        CHECK(fnv1a64(cZeros.data(), cZeros.size()) == 2387247832005793061ULL);

        CHECK(cAlphabet.size() == 273);
        CHECK(fnv1a64(cAlphabet.data(), cAlphabet.size()) == 1399084440640432086ULL);

        // --- 5. Round trips through the production decoder -----------------
        checkRoundTrip(identity, cIdentity);
        checkRoundTrip(stride, cStride);
        checkRoundTrip(zeros, cZeros);
        checkRoundTrip(alphabet, cAlphabet);
    }

    // --- 6. Decoder boundary behavior --------------------------------------
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

    // --- 7. Primitive read/write agreement ---------------------------------
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

    // --- 8. Argument and capacity guards -----------------------------------
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
        CHECK(compressed.size() == sizeof(kF2Compressed));

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

    if (g_failed)
    {
        std::fprintf(stderr, "huffman wire contracts FAILED\n");
        return 1;
    }

    std::fprintf(stdout, "huffman wire contracts OK\n");
    return 0;
}

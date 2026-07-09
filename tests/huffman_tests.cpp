#include <qcommon/huffman.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>

void MyAssertHandler(const char *, int, int, const char *, ...)
{
}

namespace
{
int fail(const char *message)
{
    std::fprintf(stderr, "%s\n", message);
    return 1;
}
}

int main()
{
    nodetype left{};
    nodetype right{};
    nodetype root{};
    left.symbol = 10;
    right.symbol = 20;
    root.symbol = 257;
    root.left = &left;
    root.right = &right;

    const uint8_t zeroBit[] = {0};
    const uint8_t oneBit[] = {1};
    int symbol = -1;
    int offset = 0;

    if (!Huff_offsetReceive(&root, &symbol, zeroBit, &offset, 1) || symbol != 10 || offset != 1)
        return fail("failed to decode the left branch");

    offset = 0;
    if (!Huff_offsetReceive(&root, &symbol, oneBit, &offset, 1) || symbol != 20 || offset != 1)
        return fail("failed to decode the right branch");

    offset = 0;
    if (Huff_offsetReceive(&root, &symbol, zeroBit, &offset, 0))
        return fail("decoder accepted a symbol beyond the input bit bound");

    uint8_t decoded[8]{};
    const uint8_t alternatingBits[] = {0b10101010};
    if (Huff_Decompress(&root, alternatingBits, 1, decoded, 8) != 8)
        return fail("bounded decompressor returned the wrong output size");
    for (int i = 0; i < 8; ++i)
    {
        const uint8_t expected = (i & 1) ? 20 : 10;
        if (decoded[i] != expected)
            return fail("bounded decompressor returned the wrong symbol");
    }
    if (Huff_Decompress(&root, alternatingBits, 1, decoded, 7) != -1)
        return fail("bounded decompressor accepted undersized output storage");

    nodetype low{};
    nodetype high{};
    low.weight = 1;
    high.weight = 2;
    nodetype *lowPtr = &low;
    nodetype *highPtr = &high;
    if (nodeCmp(&lowPtr, &highPtr) >= 0 || nodeCmp(&highPtr, &lowPtr) <= 0)
        return fail("node comparator is not pointer-width safe");

    int weights[256];
    for (int i = 0; i < 256; ++i)
        weights[i] = i + 1;
    huffman_t huffman{};
    Huff_Init(&huffman);
    Huff_BuildFromData(&huffman.compressDecompress, weights);
    if (!huffman.compressDecompress.tree)
        return fail("failed to build a Huffman tree");

    const uint8_t plain[] = {1, 2, 3, 4, 5};
    uint8_t compressed[64]{};
    uint8_t roundTrip[64]{};
    const int compressedSize = Huff_Compress(
        &huffman.compressDecompress,
        plain,
        sizeof(plain),
        compressed,
        sizeof(compressed));
    if (compressedSize <= 0)
        return fail("failed to compress a test vector");
    const int roundTripSize = Huff_Decompress(
        huffman.compressDecompress.tree,
        compressed,
        compressedSize,
        roundTrip,
        sizeof(roundTrip));
    if (roundTripSize < static_cast<int>(sizeof(plain)))
        return fail("compressed test vector decoded too few symbols");
    for (std::size_t i = 0; i < sizeof(plain); ++i)
    {
        if (roundTrip[i] != plain[i])
            return fail("compressed test vector did not round-trip");
    }
    if (Huff_Compress(&huffman.compressDecompress, plain, sizeof(plain), compressed, 0) != -1)
        return fail("compressor accepted undersized output storage");

    return 0;
}

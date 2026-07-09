#include "huffman.h"
#include <universal/assertive.h>
#include <climits>
#include <cstdlib>
#include <cstring>

int bloc;

int __cdecl get_bit(const uint8_t *fin)
{
    int t; // [esp+0h] [ebp-4h]

    t = ((int)fin[bloc >> 3] >> (bloc & 7)) & 1;
    ++bloc;
    return t;
}

bool __cdecl Huff_offsetReceive(nodetype *node, int *ch, const uint8_t *fin, int *offset, int maxoffset)
{
    int bit = *offset;

    while (node && node->symbol == 257)
    {
        if (bit >= maxoffset)
            return false;
        if ((fin[bit >> 3] >> (bit & 7)) & 1)
            node = node->right;
        else
            node = node->left;
        ++bit;
    }
    if (node)
    {
        *ch = node->symbol;
        *offset = bit;
        return true;
    }
    return false;
}

int __cdecl Huff_Decompress(nodetype *tree, const uint8_t *from, int fromSize, uint8_t *to, int toSize)
{
    if (!tree || !from || !to || fromSize < 0 || toSize < 0 || fromSize > INT_MAX / 8)
        return -1;

    const int maxoffset = 8 * fromSize;
    int offset = 0;
    int written = 0;
    while (offset < maxoffset)
    {
        int symbol;
        if (!Huff_offsetReceive(tree, &symbol, from, &offset, maxoffset))
            break;
        if (symbol < 0 || symbol > 255 || written == toSize)
            return -1;
        to[written++] = static_cast<uint8_t>(symbol);
    }
    return written;
}

int __cdecl Huff_Compress(huff_t *huff, const uint8_t *from, int fromSize, uint8_t *to, int toSize)
{
    if (!huff || !from || !to || fromSize < 0 || toSize < 0)
        return -1;

    uint64_t requiredBits = 0;
    for (int i = 0; i < fromSize; ++i)
    {
        requiredBits += Huff_bitCount(huff, from[i]);
        if (requiredBits > static_cast<uint64_t>(toSize) * 8)
            return -1;
    }

    int bit = 0;
    for (int i = 0; i < fromSize; ++i)
        Huff_offsetTransmit(huff, from[i], to, &bit);
    return (bit + 7) >> 3;
}

void __cdecl huffman_send(nodetype *node, nodetype *child, uint8_t *fout)
{
    if (node->parent)
        huffman_send(node->parent, node, fout);
    if (child)
    {
        if (node->right == child)
            add_bit(1, fout);
        else
            add_bit(0, fout);
    }
}

void __cdecl add_bit(char bit, uint8_t *fout)
{
    if ((bloc & 7) == 0)
        fout[bloc >> 3] = 0;
    fout[bloc >> 3] |= bit << (bloc & 7);
    ++bloc;
}

int __cdecl huffman_bitCountForNode(nodetype *node, nodetype *child)
{
    int bits; // [esp+0h] [ebp-4h]

    bits = 0;
    if (node->parent)
        bits = huffman_bitCountForNode(node->parent, node);
    if (child)
        ++bits;
    return bits;
}

int __cdecl Huff_bitCount(huff_t *huff, uint32_t ch)
{
    if (ch >= 0x100)
        MyAssertHandler(".\\qcommon\\huffman.cpp", 152, 0, "ch doesn't index 256\n\t%i not in [0, %i)", ch, 256);
    if (!huff->loc[ch])
        MyAssertHandler(".\\qcommon\\huffman.cpp", 153, 0, "%s", "huff->loc[ch] != NULL");
    return huffman_bitCountForNode(huff->loc[ch], 0);
}

void __cdecl Huff_offsetTransmit(huff_t *huff, int ch, uint8_t *fout, int *offset)
{
    bloc = *offset;
    huffman_send(huff->loc[ch], 0, fout);
    *offset = bloc;
}

void __cdecl Huff_Init(huffman_t *huff)
{
    std::memset(huff, 0, sizeof(*huff));
    huff->compressDecompress.loc[256] = &huff->compressDecompress.nodeList[huff->compressDecompress.blocNode++];
    huff->compressDecompress.tree = huff->compressDecompress.loc[256];
    huff->compressDecompress.tree->symbol = 256;
    huff->compressDecompress.tree->weight = 0;
    huff->compressDecompress.tree->parent = 0;
    huff->compressDecompress.tree->left = 0;
    huff->compressDecompress.tree->right = 0;
}

nodetype *__cdecl Huff_initNode(huff_t *huff, int ch, int weight)
{
    nodetype *tnode; // [esp+0h] [ebp-4h]

    tnode = &huff->nodeList[huff->blocNode++];
    tnode->symbol = ch;
    tnode->weight = weight;
    tnode->left = 0;
    tnode->right = 0;
    tnode->parent = 0;
    if (ch >= 0 && ch < static_cast<int>(sizeof(huff->loc) / sizeof(huff->loc[0])))
        huff->loc[ch] = tnode;
    return tnode;
}

int __cdecl nodeCmp(const void *left, const void *right)
{
    const nodetype *leftNode = *static_cast<nodetype *const *>(left);
    const nodetype *rightNode = *static_cast<nodetype *const *>(right);

    if (leftNode->weight < rightNode->weight)
        return -1;
    if (leftNode->weight > rightNode->weight)
        return 1;
    return 0;
}

void __cdecl Huff_BuildFromData(huff_t *huff, const int *msg_hData)
{
    nodetype *inited; // eax
    nodetype *v3; // eax
    nodetype *v4; // eax
    int numNodes; // [esp+0h] [ebp-410h]
    nodetype *heap[256]; // [esp+8h] [ebp-408h] BYREF
    int i; // [esp+408h] [ebp-8h]
    int heapHead; // [esp+40Ch] [ebp-4h]

    numNodes = 256;
    heapHead = 0;
    for (i = 0; i < 256; ++i)
    {
        inited = Huff_initNode(huff, i, msg_hData[i]);
        heap[i] = inited;
    }
    qsort(heap, 0x100u, sizeof(heap[0]), nodeCmp);
    v3 = Huff_initNode(huff, 257, 1);
    v3->left = huff->tree;
    v3->right = heap[0];
    v3->left->parent = v3;
    v3->right->parent = v3;
    v3->weight = v3->right->weight + v3->left->weight;
    heap[0] = v3;
    while (numNodes > 1)
    {
        qsort(&heap[heapHead], 256 - heapHead, sizeof(heap[0]), nodeCmp);
        v4 = Huff_initNode(huff, 257, 1);
        v4->left = heap[heapHead];
        v4->right = heap[heapHead + 1];
        v4->left->parent = v4;
        v4->right->parent = v4;
        v4->weight = v4->right->weight + v4->left->weight;
        heap[++heapHead] = v4;
        --numNodes;
    }
    huff->tree = heap[heapHead];
}

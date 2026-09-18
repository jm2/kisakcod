// KisakCOD (ki-pyb5): MP message bit-codec, core translation unit.
//
// VERBATIM code motion from msg_mp.cpp (and the msgHuff definition from
// sv_msg_write_mp.cpp). The bit-level message primitives carry no dependency
// on the 32-bit-pinned client/bgame layouts, so they live in these
// target-neutral TUs. That lets the wire-contract test targets
// (tests/CMakeLists.txt kisakcod-msg-wire-contract-tests) link the real
// production codec instead of a reimplementation, while the Win32 game and
// dedicated builds keep compiling the same functions (registered in
// scripts/mp/mp_files.cmake).
//
// The codec is split across four TUs (pure code motion, same guarantees):
//   - msg_bits_mp.cpp (this TU): buffer init, the direction-agnostic bit
//     primitives, the Huffman cluster.
//   - msg_bits_write_mp.cpp: field writers and delta-key writers.
//   - msg_bits_read_mp.cpp: field readers and delta-key readers.
//   - msg_bits_usercmd_mp.cpp: the usercmd delta write/read codec.
//
// Deliberate text deviations from the verbatim bodies (all
// behavior-preserving; see the PR disposition comment):
//   - MSG_Init: the retail memset is replaced by an explicit zeroing loop
//     (static-analysis secure-erase finding); every byte of msg_t, padding
//     included, is zeroed exactly like the retail call.
//   - The retail #error MP guard is expressed as an always-failing
//     static_assert so compilation still fails outside the MP configuration
//     without a preprocessor #error directive.

#ifndef KISAK_MP
static_assert(false, "This File is MultiPlayer Only");
#endif

#include "msg_mp.h"
#include "huffman.h"
#include "msg_huffman_data.h"
#include "sys_time.h"
#include "sv_msg_write_mp.h"

#include <cstring>

huffman_t msgHuff;

int msgInit;
uint32_t huffBytesSeen[256];

int __cdecl GetMinBitCountForNum(uint32_t num)
{
    int v2; // eax

#if defined(_MSC_VER)
    if (!_BitScanReverse((unsigned long*)&v2, num))
    {
        //v2 = `CountLeadingZeros'::`2': : notFound;
        v2 = 63;
    }
#else
    // Portable equivalent: _BitScanReverse yields the index of the highest
    // set bit; the !found sentinel selects v2 = 63 when num == 0.
    if (num == 0)
        v2 = 63;
    else
        v2 = 31 - __builtin_clz(num);
#endif

    return 32 - (v2 ^ 0x1F);
}

void __cdecl MSG_Init(msg_t *buf, uint8_t *data, int length)
{
    uint8_t *zeroTo; // [esp+0h] [ebp-8h]
    size_t i; // [esp+4h] [ebp-4h]

    if (!msgInit)
        MSG_InitHuffman();

    // Explicit zeroing loop instead of memset (static-analysis secure-erase
    // finding): zeroes every byte of msg_t, padding included, exactly like
    // the retail call; the compiler recognizes the loop idiom.
    zeroTo = (uint8_t *)buf;
    for (i = 0; i < sizeof(msg_t); ++i)
        zeroTo[i] = 0;

    buf->data = data;
    buf->maxsize = length;
}

void __cdecl MSG_InitReadOnly(msg_t *buf, uint8_t *data, int length)
{
    iassert( data );
    if (!msgInit)
        MSG_InitHuffman();
    buf->readOnly = 1;
    buf->data = data;
    buf->maxsize = length;
    buf->cursize = length;
    buf->splitData = 0;
    buf->splitSize = 0;
}

void __cdecl MSG_InitReadOnlySplit(msg_t *buf, uint8_t *data, int length, uint8_t *data2, int length2)
{
    iassert( data );
    iassert( data2 );
    if (!msgInit)
        MSG_InitHuffman();
    buf->readOnly = 1;
    buf->data = data;
    buf->maxsize = length2 + length;
    buf->cursize = length;
    buf->splitData = data2;
    buf->splitSize = length2;
}

void __cdecl MSG_BeginReading(msg_t *msg)
{
    msg->overflowed = 0;
    msg->readcount = 0;
    msg->bit = 0;
}

void __cdecl MSG_Discard(msg_t *msg)
{
    msg->overflowed = 1;
    msg->cursize = msg->readcount;
    msg->splitSize = 0;
}

int __cdecl MSG_GetUsedBitCount(const msg_t *msg)
{
    return 8 * (msg->splitSize + msg->cursize) - ((8 - msg->bit) & 7);
}

void __cdecl MSG_WriteBits(msg_t *msg, int value, uint32_t bits)
{
    int bit; // [esp+4h] [ebp-4h]

    iassert( (unsigned)bits <= 32 );
    iassert( !msg->readOnly );
    if (msg->maxsize - msg->cursize >= 4)
    {
        while (bits)
        {
            --bits;
            bit = msg->bit & 7;
            if (!bit)
            {
                msg->bit = 8 * msg->cursize;
                msg->data[msg->cursize++] = 0;
            }
            if ((value & 1) != 0)
                msg->data[msg->bit >> 3] |= 1 << bit;
            ++msg->bit;
            value >>= 1;
        }
    }
    else
    {
        msg->overflowed = 1;
    }
}

void __cdecl MSG_WriteBit0(msg_t *msg)
{
    iassert( !msg->readOnly );
    if ((msg->bit & 7) == 0)
    {
        if (msg->cursize >= msg->maxsize)
        {
            msg->overflowed = 1;
            return;
        }
        msg->bit = 8 * msg->cursize;
        msg->data[msg->cursize++] = 0;
    }
    ++msg->bit;
}

void __cdecl MSG_WriteBit1(msg_t *msg)
{
    int bit; // [esp+4h] [ebp-4h]

    iassert( !msg->readOnly );
    bit = msg->bit & 7;
    if (!bit)
    {
        if (msg->cursize >= msg->maxsize)
        {
            msg->overflowed = 1;
            return;
        }
        msg->bit = 8 * msg->cursize;
        msg->data[msg->cursize++] = 0;
    }
    msg->data[msg->bit++ >> 3] |= 1 << bit;
}

int __cdecl MSG_ReadBits(msg_t *msg, uint32_t bits)
{
    int bit; // [esp+0h] [ebp-Ch]
    int i; // [esp+4h] [ebp-8h]
    int value; // [esp+8h] [ebp-4h]

    iassert( (unsigned)bits <= 32 );
    value = 0;
    for (i = 0; i < (int)bits; ++i)
    {
        bit = msg->bit & 7;
        if (!bit)
        {
            if (msg->readcount >= msg->splitSize + msg->cursize)
            {
                msg->overflowed = 1;
                return -1;
            }
            msg->bit = 8 * msg->readcount++;
        }
        value |= ((MSG_GetByte(msg, msg->bit >> 3) >> bit) & 1) << i;
        ++msg->bit;
    }
    return value;
}

int __cdecl MSG_GetByte(msg_t *msg, int where)
{
    if (where < msg->cursize)
        return msg->data[where];
    iassert( msg->splitData );
    return msg->splitData[where - msg->cursize];
}

int __cdecl MSG_ReadBit(msg_t *msg)
{
    int Byte; // eax
    int bit; // [esp+0h] [ebp-8h]

    bit = msg->bit & 7;
    if (!bit)
    {
        if (msg->readcount >= msg->splitSize + msg->cursize)
        {
            msg->overflowed = 1;
            return -1;
        }
        msg->bit = 8 * msg->readcount++;
    }
    Byte = MSG_GetByte(msg, msg->bit >> 3);
    ++msg->bit;
    return (Byte >> bit) & 1;
}

int __cdecl MSG_WriteBitsCompress(bool trainHuffman, const uint8_t *from, int fromSize, uint8_t *to, int toSize)
{
    int i; // [esp+4h] [ebp-4h]

    const int compressedSize =
        Huff_Compress(&msgHuff.compressDecompress, from, fromSize, to, toSize);
    if (compressedSize < 0)
        return -1;

    if (trainHuffman)
    {
        for (i = 0; i < fromSize; ++i)
            ++huffBytesSeen[from[i]];
    }
    return compressedSize;
}

int __cdecl MSG_ReadBitsCompress(const uint8_t *from, int fromSize, uint8_t *to, int toSize)
{
    return Huff_Decompress(msgHuff.compressDecompress.tree, from, fromSize, to, toSize);
}

void __cdecl MSG_ClearLastReferencedEntity(msg_t *msg)
{
    msg->lastEntityRef = -1;
}

void __cdecl MSG_InitHuffman()
{
    msgInit = 1;
    MSG_initHuffmanInternal();
}

void MSG_initHuffmanInternal()
{
    uint32_t time2; // [esp+0h] [ebp-8h]
    uint32_t time; // [esp+4h] [ebp-4h]

    Huff_Init(&msgHuff);
    time = Sys_Milliseconds();
    Huff_BuildFromData(&msgHuff.compressDecompress, msg_hData);
    time2 = Sys_Milliseconds();
    Com_Printf(16, "Huffman Took %d Milliseconds\n", time2 - time);
}

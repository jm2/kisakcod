// KisakCOD (ki-pyb5): MP message bit-codec translation unit.
//
// VERBATIM code motion from msg_mp.cpp (and the msgHuff definition from
// sv_msg_write_mp.cpp). The bit-level message primitives carry no dependency
// on the 32-bit-pinned client/bgame layouts, so they live in this
// target-neutral TU. That lets the wire-contract test targets
// (tests/CMakeLists.txt kisakcod-msg-wire-contract-tests) link the real
// production codec instead of a reimplementation, while the Win32 game and
// dedicated builds keep compiling the same functions (registered in
// scripts/mp/mp_files.cmake). Function bodies are unchanged; behavior is
// identical.

#ifndef KISAK_MP
#error This File is MultiPlayer Only
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
    if (!msgInit)
        MSG_InitHuffman();

    memset(buf, 0, sizeof(msg_t));

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

void __cdecl MSG_WriteByte(msg_t *msg, uint8_t c)
{
    iassert( !msg->readOnly );
    if (msg->cursize >= msg->maxsize)
        msg->overflowed = 1;
    else
        msg->data[msg->cursize++] = c;
}

void __cdecl MSG_WriteData(msg_t *buf, uint8_t *data, uint32_t length)
{
    int newsize; // [esp+0h] [ebp-4h]

    iassert( !buf->readOnly );
    newsize = length + buf->cursize;
    if (newsize > buf->maxsize)
    {
        buf->overflowed = 1;
    }
    else
    {
        memcpy(&buf->data[buf->cursize], data, length);
        buf->cursize = newsize;
    }
}

void __cdecl MSG_WriteShort(msg_t *msg, __int16 c)
{
    int newsize; // [esp+0h] [ebp-4h]

    iassert( !msg->readOnly );
    newsize = msg->cursize + 2;
    if (newsize > msg->maxsize)
    {
        msg->overflowed = 1;
    }
    else
    {
        *(_WORD *)&msg->data[msg->cursize] = c;
        msg->cursize = newsize;
    }
}

void __cdecl MSG_WriteLong(msg_t *msg, int c)
{
    int newsize; // [esp+0h] [ebp-4h]

    iassert( !msg->readOnly );
    newsize = msg->cursize + 4;
    if (newsize > msg->maxsize)
    {
        msg->overflowed = 1;
    }
    else
    {
        *(uint32_t *)&msg->data[msg->cursize] = c;
        msg->cursize = newsize;
    }
}

void __cdecl MSG_WriteString(msg_t *sb, const char *s)
{
    uint8_t v2; // al
    int l; // [esp+10h] [ebp-40Ch]
    char string[1024]; // [esp+14h] [ebp-408h] BYREF
    int i; // [esp+418h] [ebp-4h]

    iassert( s );
    iassert( !sb->readOnly );
    l = strlen(s);
    if (l < 1024)
    {
        for (i = 0; i < l; ++i)
        {
            v2 = I_CleanChar(s[i]);
            string[i] = v2;
        }
        string[i] = 0;
        MSG_WriteData(sb, (uint8_t *)string, l + 1);
    }
    else
    {
        Com_Printf(16, "MSG_WriteString: MAX_STRING_CHARS");
        MSG_WriteData(sb, (uint8_t *)"", 1u);
    }
}

void __cdecl MSG_WriteBigString(msg_t *sb, char *s)
{
    uint8_t v2; // al
    int v3; // [esp+10h] [ebp-200Ch]
    char dest[8192]; // [esp+14h] [ebp-2008h] BYREF
    int i; // [esp+2018h] [ebp-4h]

    iassert( s );
    iassert( !sb->readOnly );
    v3 = strlen(s);
    if (v3 < 0x2000)
    {
        I_strncpyz(dest, s, 0x2000);
        for (i = 0; i < v3; ++i)
        {
            v2 = I_CleanChar(dest[i]);
            dest[i] = v2;
        }
        MSG_WriteData(sb, (uint8_t *)dest, v3 + 1);
    }
    else
    {
        Com_Printf(16, "MSG_WriteString: BIG_INFO_STRING");
        MSG_WriteData(sb, (uint8_t *)"", 1u);
    }
}

void __cdecl MSG_WriteAngle16(msg_t *sb, float f)
{
    iassert( !sb->readOnly );
    MSG_WriteShort(sb, (int)(f * 182.0444488525391));
}

int __cdecl MSG_ReadByte(msg_t *msg)
{
    int c; // [esp+0h] [ebp-4h]

    if (msg->readcount >= msg->splitSize + msg->cursize)
    {
        msg->overflowed = 1;
        return -1;
    }
    else
    {
        c = MSG_GetByte(msg, msg->readcount);

        iassert(c == static_cast<byte>(c));
        ++msg->readcount;
        return c;
    }
}

int __cdecl MSG_ReadShort(msg_t *msg)
{
    int where; // [esp+0h] [ebp-14h]
    int i; // [esp+4h] [ebp-10h]
    int c; // [esp+8h] [ebp-Ch]
    int newcount; // [esp+Ch] [ebp-8h]
    __int16 read[2]; // [esp+10h] [ebp-4h]

    newcount = msg->readcount + 2;
    if (newcount > msg->splitSize + msg->cursize)
    {
        msg->overflowed = 1;
        return -1;
    }
    else
    {
        where = msg->readcount;
        for (i = 0; i < 2; ++i)
            *((_BYTE *)read + i) = MSG_GetByte(msg, where++);
        c = read[0];
        msg->readcount = newcount;
        return c;
    }
}

int __cdecl MSG_ReadLong(msg_t *msg)
{
    int where; // [esp+0h] [ebp-10h]
    int i; // [esp+4h] [ebp-Ch]
    int c; // [esp+8h] [ebp-8h]
    int newcount; // [esp+Ch] [ebp-4h]

    newcount = msg->readcount + 4;
    if (newcount > msg->splitSize + msg->cursize)
    {
        msg->overflowed = 1;
        return -1;
    }
    else
    {
        where = msg->readcount;
        for (i = 0; i < 4; ++i)
            *((_BYTE *)&c + i) = MSG_GetByte(msg, where++);
        msg->readcount = newcount;
        return c;
    }
}

static char string[1024];
char *__cdecl MSG_ReadString(msg_t *msg)
{
    int c; // [esp+0h] [ebp-8h]
    uint32_t l; // [esp+4h] [ebp-4h]

    for (l = 0; ; ++l)
    {
        c = MSG_ReadByte(msg);
        if (c == -1)
            c = 0;
        if (l < 0x400)
            string[l] = I_CleanChar(c);
        if (!c)
            break;
    }
    string[1023] = 0;
    return string;
}

static char bigstring[8192];
char *__cdecl MSG_ReadBigString(msg_t *msg)
{
    int c; // [esp+0h] [ebp-8h]
    uint32_t l; // [esp+4h] [ebp-4h]

    for (l = 0; ; ++l)
    {
        c = MSG_ReadByte(msg);
        if (c == 37)
        {
            c = 46;
        }
        else if (c == -1)
        {
            c = 0;
        }
        if (l < 0x2000)
            bigstring[l] = I_CleanChar(c);
        if (!c)
            break;
    }
    bigstring[8191] = 0;
    return bigstring;
}

static char stringread[1024];
char *__cdecl MSG_ReadStringLine(msg_t *msg)
{
    int c; // [esp+0h] [ebp-8h]
    uint32_t l; // [esp+4h] [ebp-4h]

    for (l = 0; ; ++l)
    {
        c = MSG_ReadByte(msg);
        if (c == 37)
        {
            c = 46;
        }
        else if (c == 10 || c == -1)
        {
            c = 0;
        }
        if (l < 0x400)
            stringread[l] = I_CleanChar(c);
        if (!c)
            break;
    }
    stringread[1023] = 0;
    return stringread;
}

double __cdecl MSG_ReadAngle16(msg_t *msg)
{
    return (float)((double)MSG_ReadShort(msg) * 0.0054931640625);
}

void __cdecl MSG_ReadData(msg_t *msg, uint8_t *data, int len)
{
    int newcount; // [esp+0h] [ebp-8h]
    signed int cursize; // [esp+4h] [ebp-4h]

    if (len < 0) // KISAK (ki-gu2, upstream 321218cb): a negative length would wrap the memcpy/memset sizes
    {
        msg->overflowed = 1;
        return;
    }

    newcount = len + msg->readcount;
    if (newcount > msg->cursize)
    {
        if (newcount > msg->splitSize + msg->cursize)
        {
            msg->overflowed = 1;
            memset(data, 0xFFu, len);
        }
        else
        {
            cursize = msg->cursize - msg->readcount;
            if (cursize > 0)
            {
                memcpy(data, &msg->data[msg->readcount], cursize);
                len -= cursize;
                data += cursize;
            }
            if (len > 0)
                memcpy(data, &msg->splitData[msg->readcount - msg->cursize], len);
            msg->readcount = newcount;
        }
    }
    else
    {
        memcpy(data, &msg->data[msg->readcount], len);
        msg->readcount = newcount;
    }
}

void __cdecl MSG_WriteDeltaKey(msg_t *msg, int key, int oldV, int newV, uint32_t bits)
{
    iassert( !msg->readOnly );
    if (oldV == newV)
    {
        MSG_WriteBit0(msg);
    }
    else
    {
        MSG_WriteBit1(msg);
        MSG_WriteBits(msg, key ^ newV, bits);
    }
}

uint32_t __cdecl MSG_ReadDeltaKey(msg_t *msg, int key, int oldV, uint32_t bits)
{
    if (MSG_ReadBit(msg))
        return (kbitmask[bits] & key) ^ MSG_ReadBits(msg, bits);
    else
        return oldV;
}

void __cdecl MSG_WriteKey(msg_t *msg, int key, int newV, uint32_t bits)
{
    iassert( !msg->readOnly );
    MSG_WriteBits(msg, key ^ newV, bits);
}

uint32_t __cdecl MSG_ReadKey(msg_t *msg, int key, uint32_t bits)
{
    return (kbitmask[bits] & key) ^ MSG_ReadBits(msg, bits);
}

void __cdecl MSG_WriteDeltaKeyByte(msg_t *msg, char key, char oldV, char newV)
{
    iassert( !msg->readOnly );
    if (oldV == newV)
    {
        MSG_WriteBit0(msg);
    }
    else
    {
        MSG_WriteBit1(msg);
        MSG_WriteByte(msg, key ^ newV);
    }
}

int __cdecl MSG_ReadDeltaKeyByte(msg_t *msg, uint8_t key, int oldV)
{
    if (MSG_ReadBit(msg))
        return key ^ (uint8_t)MSG_ReadByte(msg);
    else
        return oldV;
}

void __cdecl MSG_WriteDeltaKeyShort(msg_t *msg, __int16 key, __int16 oldV, __int16 newV)
{
    iassert( !msg->readOnly );
    if (oldV == newV)
    {
        MSG_WriteBit0(msg);
    }
    else
    {
        MSG_WriteBit1(msg);
        MSG_WriteShort(msg, key ^ newV);
    }
}

int __cdecl MSG_ReadDeltaKeyShort(msg_t *msg, __int16 key, int oldV)
{
    if (MSG_ReadBit(msg))
        return key ^ (uint16_t)MSG_ReadShort(msg);
    else
        return oldV;
}

static int __cdecl MSG_HorMoveTo(int iForwardMove, int iRightMove)
{
    int iFlags; // [esp+0h] [ebp-4h]

    iFlags = 0;
    if (iForwardMove <= 10)
    {
        if (iForwardMove < -10)
            iFlags = 2;
    }
    else
    {
        iFlags = 1;
    }
    if (iRightMove > 10)
        return iFlags | 4;
    if (iRightMove < -10)
        return iFlags | 8;
    return iFlags;
}

void __cdecl MSG_HorMoveFrom(char iFlags, char *pForwardMove, char *pRightMove)
{
    if ((iFlags & 1) != 0)
    {
        *pForwardMove = 127;
    }
    else if ((iFlags & 2) != 0)
    {
        *pForwardMove = -127;
    }
    else
    {
        *pForwardMove = 0;
    }
    if ((iFlags & 4) != 0)
    {
        *pRightMove = 127;
    }
    else if ((iFlags & 8) != 0)
    {
        *pRightMove = -127;
    }
    else
    {
        *pRightMove = 0;
    }
}

void __cdecl MSG_WriteDeltaUsercmdKey(msg_t *msg, int key, const usercmd_s *from, const usercmd_s *to)
{
    int horToMove; // [esp+4h] [ebp-Ch]
    uint32_t delta; // [esp+8h] [ebp-8h]
    int horFromMove; // [esp+Ch] [ebp-4h]
    int keyb; // [esp+1Ch] [ebp+Ch]
    int keya; // [esp+1Ch] [ebp+Ch]

    iassert( !msg->readOnly );
    iassert( from->buttons < (1 << BUTTON_BIT_COUNT) );
    iassert( to->buttons < (1 << BUTTON_BIT_COUNT) );
    iassert( from->weapon < (1 << MAX_WEAPONS_BITS) );
    iassert( to->weapon < (1 << MAX_WEAPONS_BITS) );
    iassert( from->offHandIndex < (1 << MAX_WEAPONS_BITS) );
    iassert( to->offHandIndex < (1 << MAX_WEAPONS_BITS) );
    delta = to->serverTime - from->serverTime;
    if (delta >= 0x100)
    {
        MSG_WriteBit0(msg);
        MSG_WriteLong(msg, to->serverTime);
    }
    else
    {
        MSG_WriteBit1(msg);
        MSG_WriteByte(msg, delta);
    }
    horToMove = MSG_HorMoveTo(to->forwardmove, to->rightmove);
    horFromMove = MSG_HorMoveTo(from->forwardmove, from->rightmove);
    if (from->buttons >> 1 == to->buttons >> 1
        && from->weapon == to->weapon
        && from->offHandIndex == to->offHandIndex
        && from->angles[2] == to->angles[2]
        && to->meleeChargeYaw == from->meleeChargeYaw
        && from->meleeChargeDist == to->meleeChargeDist)
    {
        if (from->angles[0] == to->angles[0]
            && from->angles[1] == to->angles[1]
            && (from->buttons & 1) == (to->buttons & 1)
            && horFromMove == horToMove)
        {
            MSG_WriteKey(msg, key, 0, 1u);
        }
        else
        {
            MSG_WriteKey(msg, key, 1, 1u);
            MSG_WriteKey(msg, key, 0, 1u);
            keyb = to->serverTime ^ key;
            MSG_WriteKey(msg, keyb, to->buttons, 1u);
            MSG_WriteDeltaKeyShort(msg, keyb, from->angles[0], to->angles[0]);
            MSG_WriteDeltaKeyShort(msg, keyb, from->angles[1], to->angles[1]);
            MSG_WriteDeltaKey(msg, keyb, horFromMove, horToMove, 4u);
        }
    }
    else
    {
        MSG_WriteKey(msg, key, 1, 1u);
        MSG_WriteKey(msg, key, 1, 1u);
        MSG_WriteKey(msg, key, to->buttons, 1u);
        MSG_WriteDeltaKeyShort(msg, key, from->angles[0], to->angles[0]);
        MSG_WriteDeltaKeyShort(msg, key, from->angles[1], to->angles[1]);
        MSG_WriteDeltaKey(msg, key, horFromMove, horToMove, 4u);
        keya = to->serverTime ^ key;
        MSG_WriteDeltaKeyShort(msg, keya, from->angles[2], to->angles[2]);
        MSG_WriteDeltaKey(msg, keya, from->buttons >> 1, to->buttons >> 1, 0x14u);
        MSG_WriteDeltaKey(msg, keya, from->weapon, to->weapon, 7u);
        MSG_WriteDeltaKey(msg, keya, from->offHandIndex, to->offHandIndex, 7u);
        if ((to->buttons & 0x10000) != 0)
        {
            MSG_WriteDeltaKeyByte(msg, keya, from->selectedLocation[0], to->selectedLocation[0]);
            MSG_WriteDeltaKeyByte(msg, keya, from->selectedLocation[1], to->selectedLocation[1]);
        }
        if ((to->buttons & 4) != 0)
        {
            MSG_WriteDeltaKeyShort(
                msg,
                keya,
                (int)(from->meleeChargeYaw * 182.0444488525391),
                (int)(to->meleeChargeYaw * 182.0444488525391));
            MSG_WriteDeltaKey(msg, keya, from->meleeChargeDist, to->meleeChargeDist, 8u);
        }
    }
}

void __cdecl MSG_ReadDeltaUsercmdKey(msg_t *msg, int key, const usercmd_s *from, usercmd_s *to)
{
    char horToMove; // [esp+Ch] [ebp-8h]
    char horToMovea; // [esp+Ch] [ebp-8h]
    int horFromMove; // [esp+10h] [ebp-4h]
    int horFromMovea; // [esp+10h] [ebp-4h]
    int keyb; // [esp+20h] [ebp+Ch]
    int keya; // [esp+20h] [ebp+Ch]

    iassert( from->buttons < (1 << BUTTON_BIT_COUNT) );
    iassert( from->weapon < (1 << MAX_WEAPONS_BITS) );
    iassert( from->offHandIndex < (1 << MAX_WEAPONS_BITS) );
    memcpy(to, from, sizeof(usercmd_s));
    if (MSG_ReadBit(msg))
        to->serverTime = from->serverTime + MSG_ReadByte(msg);
    else
        to->serverTime = MSG_ReadLong(msg);
    if (MSG_ReadKey(msg, key, 1u))
    {
        to->buttons &= ~1u;
        if (MSG_ReadKey(msg, key, 1u))
        {
            to->buttons |= MSG_ReadKey(msg, key, 1u);
            to->angles[0] = (uint16_t)MSG_ReadDeltaKeyShort(msg, key, from->angles[0]);
            to->angles[1] = (uint16_t)MSG_ReadDeltaKeyShort(msg, key, from->angles[1]);
            horFromMovea = MSG_HorMoveTo(from->forwardmove, from->rightmove);
            horToMovea = MSG_ReadDeltaKey(msg, key, horFromMovea, 4u);
            MSG_HorMoveFrom(horToMovea, &to->forwardmove, &to->rightmove);
            keya = to->serverTime ^ key;
            to->angles[2] = (uint16_t)MSG_ReadDeltaKeyShort(msg, keya, from->angles[2]);
            to->buttons &= 1u;
            to->buttons |= 2 * MSG_ReadDeltaKey(msg, keya, from->buttons >> 1, 0x14u);
            to->weapon = MSG_ReadDeltaKey(msg, keya, from->weapon, 7u);
            to->offHandIndex = MSG_ReadDeltaKey(msg, keya, from->offHandIndex, 7u);
            if ((to->buttons & 0x10000) != 0)
            {
                to->selectedLocation[0] = MSG_ReadDeltaKeyByte(msg, keya, from->selectedLocation[0]);
                to->selectedLocation[1] = MSG_ReadDeltaKeyByte(msg, keya, from->selectedLocation[1]);
            }
            if ((to->buttons & 4) != 0)
            {
                to->meleeChargeYaw = (double)MSG_ReadDeltaKeyShort(
                    msg,
                    keya,
                    (uint16_t)(int)(from->meleeChargeYaw * 182.0444488525391))
                    * 0.0054931640625;
                to->meleeChargeDist = MSG_ReadDeltaKey(msg, keya, from->meleeChargeDist, 8u);
            }
            if (to->buttons >= 0x200000)
            {
                Com_PrintError(15, "client sent an invalid buttons value %i\n", to->buttons);
                to->buttons = from->buttons;
            }
            if (to->weapon >= 0x80u)
            {
                Com_PrintError(15, "client sent an invalid weapon number %i\n", to->weapon);
                to->weapon = from->weapon;
            }
            if (to->offHandIndex >= 0x80u)
            {
                Com_PrintError(15, "client sent an invalid offhand index %i\n", to->offHandIndex);
                to->offHandIndex = from->offHandIndex;
            }
        }
        else
        {
            keyb = to->serverTime ^ key;
            to->buttons |= MSG_ReadKey(msg, keyb, 1u);
            to->angles[0] = (uint16_t)MSG_ReadDeltaKeyShort(msg, keyb, from->angles[0]);
            to->angles[1] = (uint16_t)MSG_ReadDeltaKeyShort(msg, keyb, from->angles[1]);
            horFromMove = MSG_HorMoveTo(from->forwardmove, from->rightmove);
            horToMove = MSG_ReadDeltaKey(msg, keyb, horFromMove, 4u);
            MSG_HorMoveFrom(horToMove, &to->forwardmove, &to->rightmove);
        }
    }
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

// KisakCOD (ki-pyb5): MP message field-reader translation unit.
//
// VERBATIM code motion from msg_mp.cpp (see msg_bits_mp.cpp for the split
// rationale). This TU holds the field-level readers, the delta-key readers
// and their static string buffers; the direction-agnostic bit primitives,
// buffer init and the Huffman cluster live in msg_bits_mp.cpp, the writers
// in msg_bits_write_mp.cpp, and the usercmd delta codec in
// msg_bits_usercmd_mp.cpp.
//
// Deliberate text deviations from the verbatim bodies (all
// behavior-preserving; see the PR disposition comment):
//   - MSG_ReadData: the retail memcpy/memset calls are replaced by explicit
//     bounded byte loops (static-analysis buffer findings). The bytes
//     written are identical, including the 0xFF fill of the overflow arm
//     (pinned byte-for-byte by test_read_data_and_splits).
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
    int i; // [esp+8h] [ebp-Ch]

    if (len < 0) // KISAK (ki-gu2, upstream 321218cb): a negative length would wrap the copy/fill sizes
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
            // Retail memset(data, 0xFFu, len): the fill bytes are pinned by
            // test_read_data_and_splits.
            for (i = 0; i < len; ++i)
                data[i] = 0xFFu;
        }
        else
        {
            cursize = msg->cursize - msg->readcount;
            if (cursize > 0)
            {
                // Retail memcpy(data, &msg->data[msg->readcount], cursize).
                for (i = 0; i < cursize; ++i)
                    data[i] = msg->data[msg->readcount + i];
                len -= cursize;
                data += cursize;
            }
            if (len > 0)
                // Retail memcpy(data,
                //     &msg->splitData[msg->readcount - msg->cursize], len).
                for (i = 0; i < len; ++i)
                    data[i] = msg->splitData[msg->readcount - msg->cursize + i];
            msg->readcount = newcount;
        }
    }
    else
    {
        // Retail memcpy(data, &msg->data[msg->readcount], len).
        for (i = 0; i < len; ++i)
            data[i] = msg->data[msg->readcount + i];
        msg->readcount = newcount;
    }
}

uint32_t __cdecl MSG_ReadDeltaKey(msg_t *msg, int key, int oldV, uint32_t bits)
{
    if (MSG_ReadBit(msg))
        return (kbitmask[bits] & key) ^ MSG_ReadBits(msg, bits);
    else
        return oldV;
}

uint32_t __cdecl MSG_ReadKey(msg_t *msg, int key, uint32_t bits)
{
    return (kbitmask[bits] & key) ^ MSG_ReadBits(msg, bits);
}

int __cdecl MSG_ReadDeltaKeyByte(msg_t *msg, uint8_t key, int oldV)
{
    if (MSG_ReadBit(msg))
        return key ^ (uint8_t)MSG_ReadByte(msg);
    else
        return oldV;
}

int __cdecl MSG_ReadDeltaKeyShort(msg_t *msg, __int16 key, int oldV)
{
    if (MSG_ReadBit(msg))
        return key ^ (uint16_t)MSG_ReadShort(msg);
    else
        return oldV;
}

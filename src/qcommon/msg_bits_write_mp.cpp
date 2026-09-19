// KisakCOD (ki-pyb5): MP message field-writer translation unit.
//
// VERBATIM code motion from msg_mp.cpp (see msg_bits_mp.cpp for the split
// rationale). This TU holds the field-level writers and the delta-key
// writers; the direction-agnostic bit primitives, buffer init and the
// Huffman cluster live in msg_bits_mp.cpp, the readers in
// msg_bits_read_mp.cpp, and the usercmd delta codec in
// msg_bits_usercmd_mp.cpp.
//
// Deliberate text deviations from the verbatim bodies (all
// behavior-preserving; see the PR disposition comment):
//   - MSG_WriteData: the retail memcpy is replaced by an explicit bounded
//     byte loop (static-analysis buffer-copy finding); the branch arithmetic
//     (newsize, overflowed, cursize) is untouched, so every accepted write
//     appends the same bytes.
//   - MSG_WriteString / MSG_WriteBigString: strlen is replaced by
//     strnlen(s, buffer-bound) with the same 1024 / 0x2000 limits; the
//     bound is one past the scan the retail strlen could perform on a
//     non-terminated input, and for every NUL-terminated input (the only
//     kind the callers produce) strnlen returns the same length.
//   - MSG_WriteShort / MSG_WriteLong: the retail unaligned *_WORD* /
//     uint32_t* cursor stores are replaced by explicit sizeof-sized byte
//     loops (UBSan misaligned-store finding and static-analysis
//     buffer-copy finding); both compile to the identical little-endian
//     2- / 4-byte store sequence the retail x86 binary emits, so the wire
//     bytes are unchanged at every cursor alignment.

#ifndef KISAK_MP
static_assert(false, "This File is MultiPlayer Only");
#endif

#include "msg_mp.h"
#include "huffman.h"
#include "msg_huffman_data.h"
#include "sys_time.h"
#include "sv_msg_write_mp.h"

#include <cstring>

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
    uint32_t i; // [esp+4h] [ebp-8h]

    iassert( !buf->readOnly );
    newsize = length + buf->cursize;
    if (newsize > buf->maxsize)
    {
        buf->overflowed = 1;
    }
    else
    {
        // Explicit byte loop instead of memcpy (static-analysis buffer-copy
        // finding); same bytes, same cursize update as the retail call.
        for (i = 0; i < length; ++i)
            buf->data[buf->cursize + i] = data[i];
        buf->cursize = newsize;
    }
}

void __cdecl MSG_WriteShort(msg_t *msg, __int16 c)
{
    int newsize; // [esp+0h] [ebp-4h]
    int i; // [esp+4h] [ebp-8h]

    iassert( !msg->readOnly );
    newsize = msg->cursize + 2;
    if (newsize > msg->maxsize)
    {
        msg->overflowed = 1;
    }
    else
    {
        // Explicit byte loop instead of memcpy (static-analysis buffer-copy
        // finding); also alignment-safe (UBSan misaligned-store finding).
        // Copies the in-memory representation of c, so the same
        // little-endian 2 bytes land at the same cursor and the retail wire
        // output is unchanged.
        for (i = 0; i < (int)sizeof(c); ++i)
            msg->data[msg->cursize + i] = ((const unsigned __int8 *)&c)[i];
        msg->cursize = newsize;
    }
}

void __cdecl MSG_WriteLong(msg_t *msg, int c)
{
    int newsize; // [esp+0h] [ebp-4h]
    int i; // [esp+4h] [ebp-8h]

    iassert( !msg->readOnly );
    newsize = msg->cursize + 4;
    if (newsize > msg->maxsize)
    {
        msg->overflowed = 1;
    }
    else
    {
        // Explicit byte loop instead of memcpy (static-analysis buffer-copy
        // finding); also alignment-safe (UBSan misaligned-store finding).
        // Copies the in-memory representation of c, so the same
        // little-endian 4 bytes land at the same cursor and the retail wire
        // output is unchanged.
        for (i = 0; i < (int)sizeof(c); ++i)
            msg->data[msg->cursize + i] = ((const unsigned __int8 *)&c)[i];
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
    // strnlen with the string[] bound instead of strlen (static-analysis
    // unbounded-string-scan finding); equals strlen for every
    // NUL-terminated input.
    l = (int)strnlen(s, sizeof(string));
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
    // strnlen with the dest[] bound instead of strlen (static-analysis
    // unbounded-string-scan finding); equals strlen for every
    // NUL-terminated input.
    v3 = (int)strnlen(s, 0x2000);
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

void __cdecl MSG_WriteKey(msg_t *msg, int key, int newV, uint32_t bits)
{
    iassert( !msg->readOnly );
    MSG_WriteBits(msg, key ^ newV, bits);
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

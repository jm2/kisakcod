#include "database.h"
#include "db_disk32.h"
#include "db_validation.h"
#include <qcommon/qcommon.h>

#include <cstring>

namespace
{
bool DB_DecodeOffset(uint32_t value, uint32_t requiredBytes, disk32::DecodedOffset *decoded)
{
    if (!g_streamZoneMem)
        return false;

    uint32_t blockSizes[9];
    for (uint32_t i = 0; i < 9; ++i)
        blockSizes[i] = g_streamZoneMem->blocks[i].size;
    return disk32::DecodeOffset({value}, blockSizes, 9, requiredBytes, decoded);
}
}


void __cdecl Load_Stream(bool atStreamStart, uint8_t *ptr, int32_t size)
{
    if (size < 0 || atStreamStart != (ptr == DB_GetStreamPos()))
    {
        Com_Error(ERR_DROP, "Invalid fast-file stream request (start %d, size %d)", atStreamStart, size);
        return;
    }
    if (atStreamStart && size)
    {
        if (!DB_IsStreamRangeValid(ptr, static_cast<uint32_t>(size)))
        {
            Com_Error(ERR_DROP, "Fast-file read of %d bytes exceeds stream block %u", size, g_streamPosIndex);
            return;
        }
        if (g_streamPosIndex - 1 < 3)
        {
            if (g_streamPosIndex == 1)
            {
                memset(ptr, 0, size);
            }
            else
            {
                if (g_streamDelayIndex >= ARRAY_COUNT(g_streamDelayArray))
                {
                    Com_Error(ERR_DROP, "Fast-file delayed stream table overflow");
                    return;
                }
                g_streamDelayArray[g_streamDelayIndex].ptr = ptr;
                g_streamDelayArray[g_streamDelayIndex++].size = size;
            }
        }
        else
        {
            DB_LoadXFileData(ptr, size);
        }
        DB_IncStreamPos(size);
    }
}

void __cdecl Load_StreamArray(bool atStreamStart, uint8_t *ptr, int32_t count, uint32_t stride)
{
    int32_t byteCount = 0;
    if (!db::validation::CheckedArrayBytes(count, stride, &byteCount))
    {
        Com_Error(ERR_DROP, "Invalid fast-file array size (count %d, stride %u)", count, stride);
        return;
    }
    Load_Stream(atStreamStart, ptr, byteCount);
}

void __cdecl Load_DelayStream()
{
    uint32_t index; // [esp+4h] [ebp-8h]

    if (g_streamDelayIndex > ARRAY_COUNT(g_streamDelayArray))
    {
        Com_Error(ERR_DROP, "Invalid fast-file delayed stream count %u", g_streamDelayIndex);
        return;
    }

    for (index = 0; index < g_streamDelayIndex; ++index)
    {
        if (g_streamDelayArray[index].size < 0
            || !DB_IsZoneRangeValid(
                g_streamDelayArray[index].ptr,
                static_cast<uint32_t>(g_streamDelayArray[index].size)))
        {
            Com_Error(ERR_DROP, "Invalid fast-file delayed stream span %u", index);
            return;
        }
        DB_LoadXFileData((unsigned char*)g_streamDelayArray[index].ptr, g_streamDelayArray[index].size);
    }
}

void __cdecl DB_ConvertOffsetToAlias(uint32_t *data)
{
    disk32::DecodedOffset decoded{};
    if (!data || !g_streamZoneMem || !DB_DecodeOffset(*data, sizeof(uint32_t), &decoded))
    {
        Com_Error(ERR_DROP, "Invalid fast-file alias offset");
        return;
    }
    if (!g_streamZoneMem->blocks[decoded.block].data)
    {
        Com_Error(ERR_DROP, "Fast-file alias references an unloaded block");
        return;
    }
    std::memcpy(
        data,
        &g_streamZoneMem->blocks[decoded.block].data[decoded.offset],
        sizeof(*data));
}

void __cdecl DB_ConvertOffsetToPointer(uint32_t *data)
{
    disk32::DecodedOffset decoded{};
    if (!data || !g_streamZoneMem || !DB_DecodeOffset(*data, 1, &decoded))
    {
        Com_Error(ERR_DROP, "Invalid fast-file pointer offset");
        return;
    }
    if (!g_streamZoneMem->blocks[decoded.block].data)
    {
        Com_Error(ERR_DROP, "Fast-file pointer references an unloaded block");
        return;
    }

    const uintptr_t pointer = reinterpret_cast<uintptr_t>(
        &g_streamZoneMem->blocks[decoded.block].data[decoded.offset]);
    if (pointer > UINT32_MAX)
    {
        Com_Error(ERR_DROP, "Fast-file pointer does not fit the 32-bit runtime");
        return;
    }
    *data = static_cast<uint32_t>(pointer);
}

void __cdecl Load_XStringCustom(char **str)
{
    uint8_t *pos; // [esp+0h] [ebp-8h]
    int32_t length = 0;

    if (!str || !*str)
    {
        Com_Error(ERR_DROP, "Invalid fast-file string pointer");
        return;
    }

    for (pos = reinterpret_cast<uint8_t *>(*str); ; ++pos)
    {
        if (length == (std::numeric_limits<int32_t>::max)() || !DB_IsStreamRangeValid(pos, 1))
        {
            Com_Error(ERR_DROP, "Unterminated fast-file string exceeds stream block");
            return;
        }
        DB_LoadXFileData(pos, 1u);
        ++length;
        if (!*pos)
            break;
    }
    DB_IncStreamPos(length);
}

void __cdecl Load_TempStringCustom(char **str)
{
    const char * string; // [esp+0h] [ebp-4h]

    Load_XStringCustom(str);
    if (*str)
        string = (const char*)SL_GetString(*str, 4u); // KISAKTODO: this seems way wrong but it's what the decomp is showing
    else
        string= 0;
    *str = (char *)string;
}

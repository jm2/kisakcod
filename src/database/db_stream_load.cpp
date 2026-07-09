#include "database.h"
#include "db_disk32.h"
#include <qcommon/qcommon.h>

#include <cstring>

namespace
{
bool DB_DecodeOffset(uint32_t value, uint32_t requiredBytes, disk32::DecodedOffset *decoded)
{
    uint32_t blockSizes[9];
    for (uint32_t i = 0; i < 9; ++i)
        blockSizes[i] = g_streamZoneMem->blocks[i].size;
    return disk32::DecodeOffset({value}, blockSizes, 9, requiredBytes, decoded);
}
}


void __cdecl Load_Stream(bool atStreamStart, uint8_t *ptr, int32_t size)
{
    iassert(atStreamStart == (ptr == DB_GetStreamPos()));
    if (atStreamStart && size)
    {
        if (g_streamPosIndex - 1 < 3)
        {
            if (g_streamPosIndex == 1)
            {
                memset(ptr, 0, size);
            }
            else
            {
                if (g_streamDelayIndex >= 0x1000)
                    MyAssertHandler(
                        ".\\database\\db_stream_load.cpp",
                        33,
                        0,
                        "g_streamDelayIndex doesn't index ARRAY_COUNT( g_streamDelayArray )\n\t%i not in [0, %i)",
                        g_streamDelayIndex,
                        4096);
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

void __cdecl Load_DelayStream()
{
    uint32_t index; // [esp+4h] [ebp-8h]

    for (index = 0; index < g_streamDelayIndex; ++index)
        DB_LoadXFileData((unsigned char*)g_streamDelayArray[index].ptr, g_streamDelayArray[index].size);
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
    char *s; // [esp+4h] [ebp-4h]

    s = *str;
    for (pos = (uint8_t *)*str; ; ++pos)
    {
        DB_LoadXFileData(pos, 1u);
        if (!*pos)
            break;
    }
    DB_IncStreamPos(pos - (uint8_t *)s + 1);
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

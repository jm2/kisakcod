#pragma once

#include <universal/kisak_abi.h>

#include <cstdint>

struct XZoneMemory;

struct StreamDelayInfo
{
    const void *ptr;
    std::int32_t size;
};

struct StreamPosInfo
{
    std::uint8_t *pos;
    std::uint32_t index;
};

RUNTIME_SIZE(StreamDelayInfo, 0x8, 0x10);
RUNTIME_SIZE(StreamPosInfo, 0x8, 0x10);

extern std::uint32_t g_streamDelayIndex;
extern std::uint8_t *g_streamPosArray[9];
extern StreamDelayInfo g_streamDelayArray[4096];
extern std::uint32_t g_streamPosIndex;
extern StreamPosInfo g_streamPosStack[64];
extern XZoneMemory *g_streamZoneMem;
extern std::uint8_t *g_streamPos;
extern std::uint32_t g_streamPosStackIndex;

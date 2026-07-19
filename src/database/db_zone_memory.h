#pragma once

#include <universal/kisak_abi.h>

#include <cstdint>

struct XBlock
{
    std::uint8_t *data;
    std::uint32_t size;
};

struct XZoneMemory
{
    XBlock blocks[9];
    std::uint8_t *lockedVertexData;
    std::uint8_t *lockedIndexData;
    void *vertexBuffer;
    void *indexBuffer;
};

RUNTIME_SIZE(XBlock, 0x8, 0x10);
RUNTIME_SIZE(XZoneMemory, 0x58, 0xB0);

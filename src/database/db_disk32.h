#pragma once

#include <cstddef>
#include <cstdint>

namespace disk32
{
constexpr uint32_t kInline = UINT32_MAX;
constexpr uint32_t kSharedInline = UINT32_MAX - 1;
constexpr uint32_t kOffsetMask = 0x0FFFFFFF;

struct PointerToken
{
    uint32_t value;

    constexpr bool isNull() const { return value == 0; }
    constexpr bool isInline() const { return value == kInline; }
    constexpr bool isSharedInline() const { return value == kSharedInline; }
    constexpr bool isOffset() const { return !isNull() && !isInline() && !isSharedInline(); }
};
static_assert(sizeof(PointerToken) == 4);

struct DecodedOffset
{
    uint32_t block;
    uint32_t offset;
};

constexpr bool DecodeOffset(
    PointerToken token,
    const uint32_t *blockSizes,
    std::size_t blockCount,
    uint32_t requiredBytes,
    DecodedOffset *decoded)
{
    if (!token.isOffset() || !blockSizes || !decoded)
        return false;

    const uint32_t adjusted = token.value - 1;
    const uint32_t block = adjusted >> 28;
    const uint32_t offset = adjusted & kOffsetMask;
    if (block >= blockCount)
        return false;

    const uint32_t blockSize = blockSizes[block];
    if (offset > blockSize || requiredBytes > blockSize - offset)
        return false;

    decoded->block = block;
    decoded->offset = offset;
    return true;
}
}

#pragma once

#include <cstdint>
#include <limits>

namespace db::validation
{
// Script strings allocate byteCount + 4 bytes from a 65,536-node memory tree;
// MT_GetSize treats allocations of 65,536 bytes or more as fatal.
constexpr std::uint32_t kMaxInternedStringBytes = 65531;

constexpr bool CanInternString(std::uint32_t byteCount)
{
    return byteCount != 0 && byteCount <= kMaxInternedStringBytes;
}

constexpr bool PointerCountConsistent(bool hasPointer, std::int64_t count)
{
    // Present empty spans are legal in the disk32 format. The direct resolver
    // still requires their token to identify at least one materialized byte.
    return count >= 0 && (count == 0 || hasPointer);
}

constexpr bool CountInRange(
    std::int64_t count,
    std::int64_t minimum,
    std::int64_t maximum)
{
    return minimum >= 0 && minimum <= maximum
        && count >= minimum && count <= maximum;
}

constexpr bool AllU16Below(
    const std::uint16_t *values,
    std::uint32_t count,
    std::uint32_t limit)
{
    if (!values && count)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (values[index] >= limit)
            return false;
    }
    return true;
}

constexpr bool CheckedSpanBytes(
    std::uint64_t count,
    std::uint32_t stride,
    std::uint32_t *bytes)
{
    if (!bytes || !stride
        || count > (std::numeric_limits<std::uint32_t>::max)() / stride)
    {
        return false;
    }

    *bytes = static_cast<std::uint32_t>(count * stride);
    return true;
}

constexpr bool CheckedArrayBytes(std::int32_t count, std::uint32_t stride, std::int32_t *bytes)
{
    if (!bytes || count < 0 || !stride)
        return false;

    const std::uint64_t product = static_cast<std::uint64_t>(count) * stride;
    if (product > static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()))
        return false;

    *bytes = static_cast<std::int32_t>(product);
    return true;
}

constexpr bool CheckedCountSum(std::int64_t left, std::int64_t right, std::int32_t *result)
{
    if (!result || left < 0 || right < 0)
        return false;

    constexpr std::int64_t maximum = (std::numeric_limits<std::int32_t>::max)();
    if (left > maximum || right > maximum - left)
        return false;

    *result = static_cast<std::int32_t>(left + right);
    return true;
}

constexpr bool CheckedCountProduct(std::int64_t left, std::int64_t right, std::int32_t *result)
{
    if (!result || left < 0 || right < 0)
        return false;

    constexpr std::int64_t maximum = (std::numeric_limits<std::int32_t>::max)();
    if (left > maximum || right > maximum || (right && left > maximum / right))
        return false;

    *result = static_cast<std::int32_t>(left * right);
    return true;
}

constexpr bool CheckedCountDifference(std::int64_t total, std::int64_t removed, std::int32_t *result)
{
    constexpr std::int64_t maximum = (std::numeric_limits<std::int32_t>::max)();
    if (!result || total < 0 || removed < 0 || total > maximum || removed > maximum
        || removed > total)
    {
        return false;
    }

    *result = static_cast<std::int32_t>(total - removed);
    return true;
}

constexpr bool CheckedCountCeilDiv(std::int64_t value, std::int64_t divisor, std::int32_t *result)
{
    if (!result || value < 0 || divisor <= 0
        || value > (std::numeric_limits<std::int32_t>::max)())
        return false;

    const std::int64_t quotient = value / divisor + (value % divisor != 0);
    if (quotient > (std::numeric_limits<std::int32_t>::max)())
        return false;

    *result = static_cast<std::int32_t>(quotient);
    return true;
}

constexpr bool IsAlignmentMask(std::uintptr_t mask)
{
    return mask != (std::numeric_limits<std::uintptr_t>::max)()
        && (mask & (mask + 1)) == 0;
}

constexpr bool CanAppendBytes(
    std::uint32_t current,
    std::uint32_t appended,
    std::uint32_t capacity)
{
    return current <= capacity && appended <= capacity - current;
}

constexpr bool AlignUp(std::uintptr_t position, std::uintptr_t mask, std::uintptr_t *aligned)
{
    if (!aligned || !IsAlignmentMask(mask)
        || position > (std::numeric_limits<std::uintptr_t>::max)() - mask)
    {
        return false;
    }

    *aligned = (position + mask) & ~mask;
    return true;
}

constexpr bool SpanWithinBlock(
    std::uintptr_t blockBase,
    std::uint32_t blockSize,
    std::uintptr_t position,
    std::uint32_t spanSize)
{
    if (position < blockBase)
        return false;

    const std::uintptr_t offset = position - blockBase;
    return offset <= blockSize && spanSize <= blockSize - offset;
}

constexpr bool RemainingInBlock(
    std::uintptr_t blockBase,
    std::uint32_t blockSize,
    std::uintptr_t position,
    std::uint32_t *remaining)
{
    if (!remaining || position < blockBase)
        return false;

    const std::uintptr_t offset = position - blockBase;
    if (offset > blockSize)
        return false;

    *remaining = blockSize - static_cast<std::uint32_t>(offset);
    return true;
}
}

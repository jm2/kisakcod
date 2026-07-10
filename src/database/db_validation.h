#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "db_disk32.h"

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

constexpr bool MaterialTechniqueDiskBytes(
    std::uint32_t passCount,
    std::uint32_t *bytes)
{
    if (bytes)
        *bytes = 0;
    if (!bytes || !CountInRange(passCount, 1, 4))
        return false;

    *bytes = disk32::kMaterialTechniqueHeaderBytes
        + passCount * disk32::kMaterialPassBytes;
    return true;
}

constexpr bool WaterGridValid(std::int64_t width, std::int64_t height)
{
    return width == height
        && CountInRange(width, 4, 64)
        && (static_cast<std::uint64_t>(width)
            & (static_cast<std::uint64_t>(width) - 1)) == 0;
}

inline bool WaterParametersValid(
    float horizontalLength,
    float verticalLength,
    float gravity,
    float windVelocity,
    float windDirectionX,
    float windDirectionY,
    float amplitude)
{
    return std::isfinite(horizontalLength) && horizontalLength > 0.0f
        && std::isfinite(verticalLength) && verticalLength > 0.0f
        && std::isfinite(gravity) && gravity > 0.0f
        && std::isfinite(windVelocity) && windVelocity > 0.0f
        && std::isfinite(windDirectionX)
        && std::isfinite(windDirectionY)
        && (windDirectionX != 0.0f || windDirectionY != 0.0f)
        && std::isfinite(amplitude) && amplitude > 0.0f;
}

template <typename Complex>
inline bool FiniteComplexArray(const Complex *values, std::uint32_t count)
{
    if (!values && count)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (!std::isfinite(values[index].real)
            || !std::isfinite(values[index].imag))
        {
            return false;
        }
    }
    return true;
}

inline bool FiniteNonnegativeFloatArray(
    const float *values,
    std::uint32_t count)
{
    if (!values && count)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (!std::isfinite(values[index]) || values[index] < 0.0f)
            return false;
    }
    return true;
}

inline bool FiniteFloatArray(const float *values, std::uint32_t count)
{
    if (!values && count)
        return false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (!std::isfinite(values[index]))
            return false;
    }
    return true;
}

constexpr std::int32_t WaterPicmipDimension(
    std::int32_t dimension,
    std::int32_t picmipLevel)
{
    if (!CountInRange(picmipLevel, 0, 1)
        || !CountInRange(dimension, 4, 64)
        || (static_cast<std::uint32_t>(dimension)
            & (static_cast<std::uint32_t>(dimension) - 1)) != 0)
    {
        return 0;
    }

    const std::int32_t shifted = dimension >> picmipLevel;
    return shifted < 4 ? 4 : shifted;
}

template <typename Sample>
inline bool DownsampleWaterGridInPlace(
    Sample *values,
    std::int32_t sourceWidth,
    std::int32_t targetWidth)
{
    if (!values
        || !WaterGridValid(sourceWidth, sourceWidth)
        || !WaterGridValid(targetWidth, targetWidth)
        || targetWidth > sourceWidth
        || sourceWidth % targetWidth != 0)
    {
        return false;
    }

    const std::int32_t stride = sourceWidth / targetWidth;
    for (std::int32_t y = 0; y < targetWidth; ++y)
    {
        for (std::int32_t x = 0; x < targetWidth; ++x)
        {
            const std::int32_t sourceIndex =
                y * stride * sourceWidth + x * stride;
            const std::int32_t targetIndex = y * targetWidth + x;
            values[targetIndex] = values[sourceIndex];
        }
    }
    return true;
}

enum class D3D9ShaderStage : std::uint8_t
{
    Vertex,
    Pixel,
};

constexpr bool MaterialShaderLoadDefValid(
    bool hasProgram,
    std::uint32_t programDwordCount,
    std::uint32_t loadForRenderer)
{
    return hasProgram
        && programDwordCount >= 2
        && programDwordCount <= (std::numeric_limits<std::uint16_t>::max)()
        && loadForRenderer < 2;
}

inline bool D3D9ShaderBytecodeValid(
    const std::uint32_t *program,
    std::uint32_t dwordCount,
    D3D9ShaderStage expectedStage,
    std::uint32_t loadForRenderer)
{
    if (!program || dwordCount < 2
        || dwordCount > (std::numeric_limits<std::uint16_t>::max)()
        || loadForRenderer >= 2
        || (expectedStage != D3D9ShaderStage::Vertex
            && expectedStage != D3D9ShaderStage::Pixel))
    {
        return false;
    }

    constexpr std::uint32_t kVersionTypeMask = UINT32_C(0xFFFF0000);
    constexpr std::uint32_t kVertexVersionType = UINT32_C(0xFFFE0000);
    constexpr std::uint32_t kPixelVersionType = UINT32_C(0xFFFF0000);
    constexpr std::uint32_t kOpcodeMask = UINT32_C(0x0000FFFF);
    constexpr std::uint32_t kCommentOpcode = UINT32_C(0x0000FFFE);
    constexpr std::uint32_t kEndOpcode = UINT32_C(0x0000FFFF);
    constexpr std::uint32_t kEndToken = UINT32_C(0x0000FFFF);
    constexpr std::uint32_t kInstructionLengthMask = UINT32_C(0x0F000000);
    constexpr std::uint32_t kInstructionLengthShift = 24;
    constexpr std::uint32_t kInstructionReservedMask = UINT32_C(0xE0000000);
    constexpr std::uint32_t kCommentLengthMask = UINT32_C(0x7FFF0000);
    constexpr std::uint32_t kCommentLengthShift = 16;
    constexpr std::uint32_t kCommentReservedMask = UINT32_C(0x80000000);

    const std::uint32_t version = program[0];
    const std::uint32_t expectedVersionType =
        expectedStage == D3D9ShaderStage::Vertex
        ? kVertexVersionType
        : kPixelVersionType;
    const std::uint32_t major = (version >> 8) & UINT32_C(0xFF);
    const std::uint32_t minor = version & UINT32_C(0xFF);
    if ((version & kVersionTypeMask) != expectedVersionType
        || major != loadForRenderer + 2
        || minor != 0)
    {
        return false;
    }

    std::uint32_t cursor = 1;
    while (cursor < dwordCount)
    {
        const std::uint32_t token = program[cursor];
        if (token == kEndToken)
            return cursor + 1 == dwordCount;

        const std::uint32_t opcode = token & kOpcodeMask;
        if (opcode == kEndOpcode)
            return false;

        std::uint32_t payloadDwords = 0;
        if (opcode == kCommentOpcode)
        {
            if (token & kCommentReservedMask)
                return false;
            payloadDwords = (token & kCommentLengthMask) >> kCommentLengthShift;
        }
        else
        {
            const bool knownOpcode = opcode <= 48
                || (opcode >= 64 && opcode <= 96);
            if (!knownOpcode || (token & kInstructionReservedMask))
                return false;
            payloadDwords =
                (token & kInstructionLengthMask) >> kInstructionLengthShift;
        }
        if (payloadDwords > dwordCount - cursor - 1)
            return false;
        cursor += payloadDwords + 1;
    }
    return false;
}

constexpr bool OptionalMirroredCountInRange(
    std::int64_t count,
    std::int64_t mirroredCount,
    std::int64_t minimum,
    std::int64_t maximum)
{
    return count == mirroredCount
        && (count == 0 || CountInRange(count, minimum, maximum));
}

inline bool NormalizedGraphKnots(const float (*knots)[2], std::uint32_t count)
{
    if (!knots || count < 2 || knots[0][0] != 0.0f || knots[count - 1][0] != 1.0f)
        return false;

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const float x = knots[index][0];
        const float y = knots[index][1];
        // Positive comparisons reject NaN as well as infinities/out-of-range values.
        if (!(x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f))
            return false;
        if (index && !(knots[index - 1][0] < x))
            return false;
    }
    return true;
}

constexpr bool MaterialVertexRoutingValid(
    std::uint32_t source,
    std::uint32_t destination)
{
    return source < 9 && destination < 12;
}

constexpr bool MaterialVertexRoutingFollows(
    std::uint32_t previousSource,
    std::uint32_t previousDestination,
    std::uint32_t source,
    std::uint32_t destination)
{
    return previousSource < source
        || (previousSource == source && previousDestination < destination);
}

constexpr bool MaterialPassLayoutValid(
    std::uint32_t perPrimitiveCount,
    std::uint32_t perObjectCount,
    std::uint32_t stableCount,
    bool hasArguments,
    std::uint32_t customSamplerFlags)
{
    if (customSamplerFlags & ~UINT32_C(7))
        return false;
    if (perPrimitiveCount > 64 || perObjectCount > 64 - perPrimitiveCount)
        return false;
    const std::uint32_t firstTwo = perPrimitiveCount + perObjectCount;
    if (stableCount > 64 - firstTwo)
        return false;
    const std::uint32_t argumentCount = firstTwo + stableCount;
    const std::uint32_t customArgumentCount = (customSamplerFlags & 1)
        + ((customSamplerFlags >> 1) & 1)
        + ((customSamplerFlags >> 2) & 1);
    return argumentCount != 0 && hasArguments
        && customArgumentCount <= 64 - argumentCount;
}

enum class MaterialArgumentSegment : std::uint8_t
{
    PerPrimitive,
    PerObject,
    Stable,
};

constexpr bool MaterialArgumentTypeAllowedInSegment(
    std::uint32_t type,
    MaterialArgumentSegment segment)
{
    if (type >= 8)
        return false;
    switch (segment)
    {
    case MaterialArgumentSegment::PerPrimitive:
        return type == 3;
    case MaterialArgumentSegment::PerObject:
        return type == 3 || type == 4;
    case MaterialArgumentSegment::Stable:
        return true;
    }
    return false;
}

constexpr bool MaterialArgumentShapeValid(
    std::uint32_t type,
    std::uint32_t destination,
    std::uint32_t sourceIndex,
    std::uint32_t firstRow,
    std::uint32_t rowCount)
{
    switch (type)
    {
    case 0: // named vertex constant
    case 1: // literal vertex constant
        return destination < 32;
    case 2: // named pixel sampler
        return destination < 16;
    case 3: // code vertex constant/matrix
        if (destination >= 32 || sourceIndex >= 90 || rowCount == 0
            || rowCount > 32 - destination)
        {
            return false;
        }
        if (sourceIndex < 58)
            return firstRow == 0 && rowCount == 1;
        return firstRow < 4 && rowCount <= 4 - firstRow;
    case 4: // code pixel sampler
        return destination < 16 && sourceIndex < 27;
    case 5: // code pixel constant
        return destination < 256 && sourceIndex < 51
            && firstRow == 0 && rowCount == 1;
    case 6: // named pixel constant
    case 7: // literal pixel constant
        return destination < 256;
    default:
        return false;
    }
}

constexpr bool MaterialCodeConstantAllowedInSegment(
    std::uint32_t sourceIndex,
    MaterialArgumentSegment segment)
{
    if (sourceIndex >= 90)
        return false;
    switch (segment)
    {
    case MaterialArgumentSegment::Stable:
        return sourceIndex <= 50;
    case MaterialArgumentSegment::PerObject:
        return (sourceIndex >= 51 && sourceIndex <= 56)
            || (sourceIndex >= 62 && sourceIndex <= 69)
            || (sourceIndex >= 74 && sourceIndex <= 77)
            || (sourceIndex >= 82 && sourceIndex <= 85);
    case MaterialArgumentSegment::PerPrimitive:
        return (sourceIndex >= 57 && sourceIndex <= 61)
            || (sourceIndex >= 70 && sourceIndex <= 73)
            || (sourceIndex >= 78 && sourceIndex <= 81)
            || (sourceIndex >= 86 && sourceIndex <= 89);
    }
    return false;
}

constexpr bool MaterialCodeSamplerAllowedInSegment(
    std::uint32_t sourceIndex,
    MaterialArgumentSegment segment)
{
    if (sourceIndex >= 27 || segment == MaterialArgumentSegment::PerPrimitive)
        return false;
    if (segment == MaterialArgumentSegment::PerObject)
    {
        return sourceIndex == 9 || sourceIndex == 15 || sourceIndex == 16
            || (sourceIndex >= 21 && sourceIndex <= 25);
    }
    if (segment != MaterialArgumentSegment::Stable)
        return false;
    return sourceIndex != 4 && sourceIndex != 5 && sourceIndex != 9
        && sourceIndex != 15 && sourceIndex != 16
        && !(sourceIndex >= 21 && sourceIndex <= 26);
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

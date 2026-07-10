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

constexpr bool AssetOutputCapacityValid(std::int64_t capacity)
{
    return capacity >= 0
        && capacity <= (std::numeric_limits<std::int32_t>::max)();
}

constexpr bool AssetOutputCountCanIncrement(std::int64_t count)
{
    return count >= 0
        && count < (std::numeric_limits<std::int32_t>::max)();
}

constexpr bool AssetOutputWriteAllowed(
    bool hasOutput,
    std::int64_t count,
    std::int64_t capacity)
{
    return hasOutput
        && AssetOutputCapacityValid(capacity)
        && count >= 0
        && count < capacity;
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

constexpr bool MaterialTextureTableDiskBytes(
    std::uint32_t textureCount,
    std::uint32_t *bytes)
{
    if (bytes)
        *bytes = 0;
    if (!bytes || !CountInRange(textureCount, 1, 255))
        return false;

    *bytes = textureCount * disk32::kMaterialTextureDefBytes;
    return true;
}

constexpr bool MaterialTextureHeaderValid(
    std::uint32_t samplerState,
    std::uint32_t semantic,
    bool hasPayload,
    std::uint32_t nameStart,
    std::uint32_t nameEnd)
{
    const std::uint32_t filter = samplerState & UINT32_C(0x7);
    const std::uint32_t mipmap = samplerState & UINT32_C(0x18);
    return hasPayload
        && nameStart != 0
        && nameEnd != 0
        && filter != 0
        && mipmap != UINT32_C(0x18)
        && semantic <= 11;
}

template <typename Entry>
inline bool StrictlyIncreasingNameHashes(
    const Entry *entries,
    std::uint32_t count)
{
    if (!entries)
        return count == 0;
    for (std::uint32_t index = 1; index < count; ++index)
    {
        if (entries[index - 1].nameHash >= entries[index].nameHash)
            return false;
    }
    return true;
}

template <typename Entry>
inline const Entry *FindSortedNameHash(
    const Entry *entries,
    std::uint32_t count,
    std::uint32_t target)
{
    if (!entries)
        return nullptr;
    std::uint32_t first = 0;
    std::uint32_t remaining = count;
    while (remaining)
    {
        const std::uint32_t half = remaining / 2;
        const std::uint32_t middle = first + half;
        if (entries[middle].nameHash < target)
        {
            first = middle + 1;
            remaining -= half + 1;
        }
        else
        {
            remaining = half;
        }
    }
    return first < count && entries[first].nameHash == target
        ? &entries[first]
        : nullptr;
}

template <typename Entry>
inline bool SortedNameHashContains(
    const Entry *entries,
    std::uint32_t count,
    std::uint32_t target)
{
    return FindSortedNameHash(entries, count, target) != nullptr;
}

constexpr bool CheckedMaterialTableCountSum(
    std::uint32_t current,
    std::uint32_t added,
    std::uint32_t *result)
{
    if (!result
        || current > UINT32_C(255)
        || added > UINT32_C(255) - current)
    {
        return false;
    }
    *result = current + added;
    return true;
}

constexpr bool MaterialTechniqueStateSpanValid(
    bool hasTechnique,
    std::uint32_t passCount,
    std::uint32_t stateEntry,
    std::uint32_t stateCount)
{
    if (stateCount > 136)
        return false;
    if (!hasTechnique)
        return stateEntry == UINT32_C(255);
    return CountInRange(passCount, 1, 4)
        && stateEntry != UINT32_C(255)
        && stateEntry <= stateCount
        && passCount <= stateCount - stateEntry;
}

constexpr bool MaterialRemapSlotValid(
    bool originalPresent,
    std::uint32_t originalPassCount,
    bool candidatePresent,
    std::uint32_t candidatePassCount)
{
    return !candidatePresent
        || (originalPresent
            && CountInRange(originalPassCount, 1, 4)
            && CountInRange(candidatePassCount, 1, originalPassCount));
}

constexpr bool MaterialStateBitsDecodeSafe(std::uint32_t bits0)
{
    const std::uint32_t rgbOperation = (bits0 >> 8) & UINT32_C(0x7);
    const std::uint32_t alphaOperation = (bits0 >> 24) & UINT32_C(0x7);
    if (rgbOperation > 5 || alphaOperation > 5)
        return false;
    if (!rgbOperation)
        return alphaOperation == 0;
    if ((bits0 & UINT32_C(0xF)) > 10
        || ((bits0 >> 4) & UINT32_C(0xF)) > 10)
    {
        return false;
    }
    return !alphaOperation
        || (((bits0 >> 16) & UINT32_C(0xF)) <= 10
            && ((bits0 >> 20) & UINT32_C(0xF)) <= 10);
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

constexpr bool WorldAabbTreePresenceValid(
    bool hasTree,
    std::int64_t nodeCount)
{
    return nodeCount >= 0
        && nodeCount <= (std::numeric_limits<std::int32_t>::max)()
        && hasTree == (nodeCount != 0);
}

constexpr bool UnsignedSpanWithinPartition(
    std::uint64_t start,
    std::uint64_t count,
    std::uint64_t partitionStart,
    std::uint64_t partitionCount)
{
    if (start < partitionStart)
        return false;
    const std::uint64_t relativeStart = start - partitionStart;
    return relativeStart <= partitionCount
        && count <= partitionCount - relativeStart;
}

constexpr bool OptionalUnsignedSpanWithinPartition(
    std::uint64_t start,
    std::uint64_t count,
    std::uint64_t partitionStart,
    std::uint64_t partitionCount)
{
    return count == 0
        || UnsignedSpanWithinPartition(
            start,
            count,
            partitionStart,
            partitionCount);
}

constexpr std::uint32_t kMaxWorldAabbDepth = 64;
constexpr std::uint64_t kMaxWorldAabbSurfaceEntries =
    static_cast<std::uint64_t>(
        (std::numeric_limits<std::uint16_t>::max)()) + 1;
constexpr std::uint64_t kMaxWorldAabbStaticModels =
    static_cast<std::uint64_t>(
        (std::numeric_limits<std::uint16_t>::max)()) + 1;

constexpr bool WorldAabbSurfacePartitionsValid(
    std::uint64_t staticSurfaceCount,
    std::uint64_t staticSurfaceCountNoDecal,
    std::uint64_t worldSurfaceCount)
{
    return staticSurfaceCountNoDecal <= staticSurfaceCount
        && staticSurfaceCount <= worldSurfaceCount
        && staticSurfaceCount <= kMaxWorldAabbSurfaceEntries
        && (staticSurfaceCountNoDecal == 0
            || staticSurfaceCount
                <= (std::numeric_limits<std::uint16_t>::max)());
}

inline bool MarkUniqueCoverageSpan(
    std::uint8_t *coverage,
    std::uint64_t coverageCount,
    std::uint64_t start,
    std::uint64_t count)
{
    if (count == 0)
        return true;
    if ((!coverage && coverageCount)
        || !UnsignedSpanWithinPartition(start, count, 0, coverageCount))
    {
        return false;
    }
    for (std::uint64_t index = start; index < start + count; ++index)
    {
        if (coverage[index])
            return false;
        coverage[index] = 1;
    }
    return true;
}

inline bool CoverageComplete(
    const std::uint8_t *coverage,
    std::uint64_t coverageCount)
{
    if (!coverage && coverageCount)
        return false;
    for (std::uint64_t index = 0; index < coverageCount; ++index)
    {
        if (!coverage[index])
            return false;
    }
    return true;
}

enum class WorldAabbTopologyStatus : std::uint8_t
{
    Ok,
    InvalidTree,
    InvalidWorkBuffer,
    InvalidBounds,
    InvalidSurfaceRange,
    InvalidChildSpan,
    InvalidTopology,
    DepthLimitExceeded,
};

constexpr const char *WorldAabbTopologyStatusName(
    WorldAabbTopologyStatus status)
{
    switch (status)
    {
    case WorldAabbTopologyStatus::Ok:
        return "ok";
    case WorldAabbTopologyStatus::InvalidTree:
        return "invalid tree pointer/count";
    case WorldAabbTopologyStatus::InvalidWorkBuffer:
        return "invalid topology work buffer";
    case WorldAabbTopologyStatus::InvalidBounds:
        return "invalid node bounds";
    case WorldAabbTopologyStatus::InvalidSurfaceRange:
        return "invalid surface range";
    case WorldAabbTopologyStatus::InvalidChildSpan:
        return "invalid child span";
    case WorldAabbTopologyStatus::InvalidTopology:
        return "disconnected or multiply-owned node";
    case WorldAabbTopologyStatus::DepthLimitExceeded:
        return "tree depth limit exceeded";
    }
    return "unknown";
}

struct ImplicitWorldAabbTraversalFrame
{
    std::uint32_t firstChild;
    std::uint32_t nextChild;
    std::uint32_t childCount;
};

template <typename Node>
inline WorldAabbTopologyStatus ValidateImplicitWorldAabbForest(
    const Node *nodes,
    std::int64_t nodeCount,
    std::uint64_t staticSurfaceCount,
    std::uint8_t *rootFlags,
    std::uint64_t rootFlagCapacity)
{
    if (nodeCount < 0
        || nodeCount > (std::numeric_limits<std::int32_t>::max)()
        || (!nodes && nodeCount))
    {
        return WorldAabbTopologyStatus::InvalidTree;
    }
    if (nodeCount == 0)
    {
        return staticSurfaceCount == 0
            ? WorldAabbTopologyStatus::Ok
            : WorldAabbTopologyStatus::InvalidSurfaceRange;
    }
    if (staticSurfaceCount > kMaxWorldAabbSurfaceEntries)
        return WorldAabbTopologyStatus::InvalidSurfaceRange;
    if (!rootFlags
        || rootFlagCapacity < static_cast<std::uint64_t>(nodeCount))
    {
        return WorldAabbTopologyStatus::InvalidWorkBuffer;
    }

    const std::uint32_t count = static_cast<std::uint32_t>(nodeCount);
    for (std::uint32_t index = 0; index < count; ++index)
        rootFlags[index] = 0;

    ImplicitWorldAabbTraversalFrame stack[kMaxWorldAabbDepth] = {};
    std::uint64_t leafSurfaceCount = 0;
    std::uint64_t rootSurfaceEnd = 0;
    std::uint32_t rootIndex = 0;
    while (rootIndex < count)
    {
        rootFlags[rootIndex] = 1;
        std::uint32_t nextFreeNode = rootIndex + 1;
        std::uint32_t nodeIndex = rootIndex;
        std::uint32_t stackCount = 0;
        for (;;)
        {
            const Node &node = nodes[nodeIndex];
            if (node.surfaceCount > (std::numeric_limits<std::uint16_t>::max)()
                || (node.surfaceCount
                    && (node.firstSurface
                            > (std::numeric_limits<std::uint16_t>::max)()
                        || !UnsignedSpanWithinPartition(
                            node.firstSurface,
                            node.surfaceCount,
                            0,
                            staticSurfaceCount))))
            {
                return WorldAabbTopologyStatus::InvalidSurfaceRange;
            }
            if (node.childCount > (std::numeric_limits<std::uint16_t>::max)())
                return WorldAabbTopologyStatus::InvalidChildSpan;
            if (nodeIndex == rootIndex && node.surfaceCount)
            {
                if (node.firstSurface < rootSurfaceEnd)
                    return WorldAabbTopologyStatus::InvalidSurfaceRange;
                rootSurfaceEnd = static_cast<std::uint64_t>(node.firstSurface)
                    + node.surfaceCount;
            }

            if (node.childCount)
            {
                if (stackCount + 2 > kMaxWorldAabbDepth)
                    return WorldAabbTopologyStatus::DepthLimitExceeded;
                if (node.childCount > count - nextFreeNode)
                    return WorldAabbTopologyStatus::InvalidChildSpan;

                std::uint64_t expectedSurface = node.firstSurface;
                std::uint64_t childSurfaceCount = 0;
                for (std::uint32_t child = 0;
                    child < node.childCount;
                    ++child)
                {
                    const Node &childNode = nodes[nextFreeNode + child];
                    if (childNode.surfaceCount)
                    {
                        if (childNode.firstSurface != expectedSurface)
                            return WorldAabbTopologyStatus::InvalidSurfaceRange;
                        expectedSurface += childNode.surfaceCount;
                        childSurfaceCount += childNode.surfaceCount;
                    }
                }
                if (childSurfaceCount != node.surfaceCount)
                    return WorldAabbTopologyStatus::InvalidSurfaceRange;

                ImplicitWorldAabbTraversalFrame &frame = stack[stackCount++];
                frame.firstChild = nextFreeNode;
                frame.nextChild = 1;
                frame.childCount = node.childCount;
                nextFreeNode += node.childCount;
                nodeIndex = frame.firstChild;
                continue;
            }

            if (node.surfaceCount
                > staticSurfaceCount - leafSurfaceCount)
            {
                return WorldAabbTopologyStatus::InvalidSurfaceRange;
            }
            leafSurfaceCount += node.surfaceCount;

            bool advancedToSibling = false;
            while (stackCount)
            {
                ImplicitWorldAabbTraversalFrame &frame = stack[stackCount - 1];
                if (frame.nextChild < frame.childCount)
                {
                    nodeIndex = frame.firstChild + frame.nextChild++;
                    advancedToSibling = true;
                    break;
                }
                --stackCount;
            }
            if (advancedToSibling)
                continue;

            rootIndex = nextFreeNode;
            break;
        }
    }
    if (rootIndex != count)
        return WorldAabbTopologyStatus::InvalidTopology;
    return leafSurfaceCount == staticSurfaceCount
        ? WorldAabbTopologyStatus::Ok
        : WorldAabbTopologyStatus::InvalidSurfaceRange;
}

template <typename Node>
inline WorldAabbTopologyStatus ValidateWorldAabbTopology(
    const Node *nodes,
    std::int64_t nodeCount,
    std::uint64_t staticSurfaceCount,
    std::uint64_t staticSurfaceCountNoDecal,
    std::uint8_t *nodeDepths,
    std::uint64_t nodeDepthCapacity,
    std::uint32_t maximumDepth = kMaxWorldAabbDepth)
{
    if (!WorldAabbTreePresenceValid(nodes != nullptr, nodeCount))
        return WorldAabbTopologyStatus::InvalidTree;
    if (nodeCount == 0)
        return WorldAabbTopologyStatus::Ok;
    if (!nodeDepths
        || nodeDepthCapacity < static_cast<std::uint64_t>(nodeCount)
        || maximumDepth == 0
        || maximumDepth
            > (std::numeric_limits<std::uint8_t>::max)() / 2)
    {
        return WorldAabbTopologyStatus::InvalidWorkBuffer;
    }

    const std::uint32_t count = static_cast<std::uint32_t>(nodeCount);
    for (std::uint32_t index = 0; index < count; ++index)
        nodeDepths[index] = 0;
    nodeDepths[0] = 1;

    std::uint64_t edgeCount = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (nodeDepths[index] == 0)
            return WorldAabbTopologyStatus::InvalidTopology;

        const Node &node = nodes[index];
        for (std::uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(node.mins[axis])
                || !std::isfinite(node.maxs[axis]))
            {
                return WorldAabbTopologyStatus::InvalidBounds;
            }
        }

        if ((node.surfaceCount
                && !UnsignedSpanWithinPartition(
                    node.startSurfIndex,
                    node.surfaceCount,
                    0,
                    staticSurfaceCount))
            || node.surfaceCountNoDecal > node.surfaceCount
            || (node.surfaceCountNoDecal
                && !UnsignedSpanWithinPartition(
                    node.startSurfIndexNoDecal,
                    node.surfaceCountNoDecal,
                    staticSurfaceCount,
                    staticSurfaceCountNoDecal)))
        {
            return WorldAabbTopologyStatus::InvalidSurfaceRange;
        }

        const std::uint32_t childCount = node.childCount;
        if (!childCount)
            continue;
        if (node.childrenOffset <= 0)
            return WorldAabbTopologyStatus::InvalidChildSpan;

        const std::uint64_t childByteOffset =
            static_cast<std::uint64_t>(node.childrenOffset);
        if (childByteOffset % sizeof(Node) != 0)
            return WorldAabbTopologyStatus::InvalidChildSpan;
        const std::uint64_t childDelta = childByteOffset / sizeof(Node);
        if (childDelta == 0 || childDelta > count - index)
            return WorldAabbTopologyStatus::InvalidChildSpan;
        const std::uint64_t firstChild = index + childDelta;
        if (firstChild >= count || childCount > count - firstChild)
            return WorldAabbTopologyStatus::InvalidChildSpan;

        std::uint64_t expectedSurface = node.startSurfIndex;
        std::uint64_t expectedNoDecalSurface = node.startSurfIndexNoDecal;
        std::uint64_t childSurfaceCount = 0;
        std::uint64_t childNoDecalSurfaceCount = 0;
        for (std::uint32_t child = 0; child < childCount; ++child)
        {
            const Node &childNode = nodes[firstChild + child];
            if (childNode.surfaceCount)
            {
                if (childNode.startSurfIndex != expectedSurface)
                    return WorldAabbTopologyStatus::InvalidSurfaceRange;
                expectedSurface += childNode.surfaceCount;
                childSurfaceCount += childNode.surfaceCount;
            }
            if (childNode.surfaceCountNoDecal)
            {
                if (childNode.startSurfIndexNoDecal != expectedNoDecalSurface)
                    return WorldAabbTopologyStatus::InvalidSurfaceRange;
                expectedNoDecalSurface += childNode.surfaceCountNoDecal;
                childNoDecalSurfaceCount += childNode.surfaceCountNoDecal;
            }
        }
        if (childSurfaceCount != node.surfaceCount
            || childNoDecalSurfaceCount != node.surfaceCountNoDecal)
        {
            return WorldAabbTopologyStatus::InvalidSurfaceRange;
        }

        edgeCount += childCount;
        if (edgeCount >= count)
            return WorldAabbTopologyStatus::InvalidTopology;

        const std::uint32_t childDepth = nodeDepths[index] + 1;
        if (childDepth > maximumDepth)
            return WorldAabbTopologyStatus::DepthLimitExceeded;
        for (std::uint32_t child = 0; child < childCount; ++child)
        {
            const std::uint32_t childIndex =
                static_cast<std::uint32_t>(firstChild + child);
            if (nodeDepths[childIndex] != 0)
                return WorldAabbTopologyStatus::InvalidTopology;
            nodeDepths[childIndex] = static_cast<std::uint8_t>(childDepth);
        }
    }
    if (edgeCount != count - 1)
        return WorldAabbTopologyStatus::InvalidTopology;

    constexpr std::uint8_t kSubtreeHasSpatialContents = UINT8_C(0x80);
    for (std::uint32_t index = count; index-- > 0;)
    {
        const Node &node = nodes[index];
        bool hasSpatialContents =
            node.surfaceCount != 0 || node.smodelIndexCount != 0;
        if (node.childCount)
        {
            const std::uint32_t firstChild = index
                + static_cast<std::uint32_t>(node.childrenOffset / sizeof(Node));
            for (std::uint32_t child = 0; child < node.childCount; ++child)
            {
                hasSpatialContents = hasSpatialContents
                    || (nodeDepths[firstChild + child]
                        & kSubtreeHasSpatialContents) != 0;
            }
        }
        if (!hasSpatialContents)
            continue;
        for (std::uint32_t axis = 0; axis < 3; ++axis)
        {
            if (node.mins[axis] > node.maxs[axis])
                return WorldAabbTopologyStatus::InvalidBounds;
        }
        nodeDepths[index] |= kSubtreeHasSpatialContents;
    }
    return WorldAabbTopologyStatus::Ok;
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

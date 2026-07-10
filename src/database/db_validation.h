#pragma once

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <vector>

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

constexpr bool SoundFileHeaderValid(
    std::uint32_t type,
    std::uint32_t exists)
{
    return (type == 1 || type == 2) && exists <= 1;
}

constexpr std::uint32_t SpeakerMapExpectedSpeakerCount(
    std::uint32_t outputConfiguration)
{
    return outputConfiguration == 0 ? 2u
        : outputConfiguration == 1 ? 6u
        : 0u;
}

inline bool SpeakerMapEntryValid(
    std::uint32_t speakerIndex,
    std::int64_t speaker,
    std::int64_t levelCount,
    std::uint32_t expectedLevelCount,
    float level0,
    float level1)
{
    return speaker == static_cast<std::int64_t>(speakerIndex)
        && levelCount == static_cast<std::int64_t>(expectedLevelCount)
        && std::isfinite(level0)
        && std::isfinite(level1)
        && level0 >= 0.0f
        && level0 <= 1.0f
        && level1 >= 0.0f
        && level1 <= 1.0f;
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

inline bool XModelPieceRuntimeValid(
    bool hasModel,
    const float *offset)
{
    return hasModel
        && FiniteFloatArray(offset, 3);
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

constexpr bool UnsignedSpanWithinPartition(
    std::uint64_t start,
    std::uint64_t count,
    std::uint64_t partitionStart,
    std::uint64_t partitionCount);

inline bool CoverageComplete(
    const std::uint8_t *coverage,
    std::uint64_t coverageCount);

inline bool XSurfaceTriangleIndicesValid(
    const std::uint16_t *indices,
    std::uint32_t triangleCount,
    std::uint32_t vertexCount)
{
    if (triangleCount
        > (std::numeric_limits<std::uint32_t>::max)() / 3u)
    {
        return false;
    }
    return AllU16Below(indices, triangleCount * 3u, vertexCount);
}

inline bool XSurfaceTrailingPaddingTriangleValid(
    const std::uint16_t *indices,
    std::uint32_t coveredTriangleCount,
    std::uint32_t surfaceTriangleCount)
{
    if (!indices
        || surfaceTriangleCount < 2
        || (surfaceTriangleCount & 1u) != 0
        || coveredTriangleCount != surfaceTriangleCount - 1
        || surfaceTriangleCount
            > (std::numeric_limits<std::uint32_t>::max)() / 3u)
    {
        return false;
    }

    const std::uint32_t tail = 3u * (surfaceTriangleCount - 1);
    return indices[tail] == indices[tail - 1]
        && indices[tail + 1] == indices[tail]
        && indices[tail + 2] == indices[tail];
}

template <typename RigidVertList>
inline bool XSurfaceRigidPartitionValid(
    const RigidVertList *lists,
    std::size_t listCount,
    std::uint32_t surfaceVertexCount,
    std::uint32_t surfaceTriangleCount,
    const std::uint16_t *triangleIndices)
{
    if ((!lists && listCount)
        || !XSurfaceTriangleIndicesValid(
            triangleIndices,
            surfaceTriangleCount,
            surfaceVertexCount))
    {
        return false;
    }

    std::uint32_t coveredVertices = 0;
    std::uint32_t coveredTriangles = 0;
    for (std::size_t index = 0; index < listCount; ++index)
    {
        const RigidVertList &list = lists[index];
        const std::uint32_t vertexCount = list.vertCount;
        const std::uint32_t triangleOffset = list.triOffset;
        const std::uint32_t triangleCount = list.triCount;
        if (!list.collisionTree
            || vertexCount == 0
            || triangleCount == 0
            || triangleOffset != coveredTriangles
            || coveredVertices > surfaceVertexCount
            || vertexCount > surfaceVertexCount - coveredVertices
            || coveredTriangles > surfaceTriangleCount
            || triangleCount > surfaceTriangleCount - coveredTriangles)
        {
            return false;
        }
        coveredVertices += vertexCount;
        coveredTriangles += triangleCount;
    }

    return coveredVertices == surfaceVertexCount
        && (coveredTriangles == surfaceTriangleCount
            || XSurfaceTrailingPaddingTriangleValid(
                triangleIndices,
                coveredTriangles,
                surfaceTriangleCount));
}

// A 16-bit begin plus the 15-bit decoded count can address [0, 98,302).
constexpr std::uint32_t kMaxXSurfaceCollisionEntries = UINT32_C(98302);
constexpr std::uint32_t kXSurfaceCollisionNodeRangeQueueCapacity = 64;

enum class XSurfaceCollisionTopologyStatus : std::uint8_t
{
    Ok,
    InvalidTree,
    InvalidBounds,
    InvalidChildSpan,
    InvalidTopology,
    InvalidTriangleSpan,
    NodeRangeQueueCapacityExceeded,
    AllocationFailure,
};

constexpr const char *XSurfaceCollisionTopologyStatusName(
    XSurfaceCollisionTopologyStatus status)
{
    switch (status)
    {
    case XSurfaceCollisionTopologyStatus::Ok:
        return "ok";
    case XSurfaceCollisionTopologyStatus::InvalidTree:
        return "invalid tree pointer/count";
    case XSurfaceCollisionTopologyStatus::InvalidBounds:
        return "invalid node bounds";
    case XSurfaceCollisionTopologyStatus::InvalidChildSpan:
        return "invalid child span";
    case XSurfaceCollisionTopologyStatus::InvalidTopology:
        return "disconnected or multiply-owned tree entry";
    case XSurfaceCollisionTopologyStatus::InvalidTriangleSpan:
        return "leaf triangle outside its rigid partition";
    case XSurfaceCollisionTopologyStatus::NodeRangeQueueCapacityExceeded:
        return "node range queue capacity exceeded";
    case XSurfaceCollisionTopologyStatus::AllocationFailure:
        return "topology work allocation failed";
    }
    return "unknown";
}

struct XSurfaceCollisionNodeRange
{
    std::uint32_t begin;
    std::uint32_t count;
};

template <typename Node, typename Leaf>
inline XSurfaceCollisionTopologyStatus ValidateXSurfaceCollisionTopology(
    const Node *nodes,
    std::size_t nodeCount,
    const Leaf *leafs,
    std::size_t leafCount,
    std::uint32_t rigidTriangleOffset,
    std::uint32_t rigidTriangleCount,
    std::uint32_t surfaceTriangleCount)
{
    if (!nodes
        || !leafs
        || nodeCount == 0
        || leafCount == 0
        || nodeCount > static_cast<std::size_t>(kMaxXSurfaceCollisionEntries)
        || leafCount > static_cast<std::size_t>(kMaxXSurfaceCollisionEntries))
    {
        return XSurfaceCollisionTopologyStatus::InvalidTree;
    }
    if (!UnsignedSpanWithinPartition(
            rigidTriangleOffset,
            rigidTriangleCount,
            0,
            surfaceTriangleCount))
    {
        return XSurfaceCollisionTopologyStatus::InvalidTriangleSpan;
    }

    try
    {
        const std::uint32_t nodeEntryCount =
            static_cast<std::uint32_t>(nodeCount);
        const std::uint32_t leafEntryCount =
            static_cast<std::uint32_t>(leafCount);
        std::vector<std::uint8_t> nodeOwners(nodeCount, UINT8_C(0));
        std::vector<std::uint8_t> leafOwners(leafCount, UINT8_C(0));
        std::vector<XSurfaceCollisionNodeRange> ranges;
        ranges.reserve(nodeCount);
        ranges.push_back({0, 1});
        nodeOwners[0] = 1;

        std::size_t rangeIndex = 0;
        while (rangeIndex < ranges.size())
        {
            const XSurfaceCollisionNodeRange range = ranges[rangeIndex++];
            const std::uint32_t rangeEnd = range.begin + range.count;
            for (std::uint32_t nodeIndex = range.begin;
                nodeIndex < rangeEnd;
                ++nodeIndex)
            {
                const Node &node = nodes[nodeIndex];
                for (std::uint32_t axis = 0; axis < 3; ++axis)
                {
                    if (node.aabb.mins[axis] > node.aabb.maxs[axis])
                    {
                        return XSurfaceCollisionTopologyStatus::InvalidBounds;
                    }
                }

                const std::uint64_t rawChildCount =
                    static_cast<std::uint64_t>(node.childCount);
                const std::uint64_t rawChildBegin =
                    static_cast<std::uint64_t>(node.childBeginIndex);
                if (rawChildCount
                        > (std::numeric_limits<std::uint16_t>::max)()
                    || rawChildBegin
                        > (std::numeric_limits<std::uint16_t>::max)())
                {
                    return XSurfaceCollisionTopologyStatus::InvalidChildSpan;
                }

                const bool hasLeafChildren =
                    (rawChildCount & UINT64_C(0x8000)) != 0;
                const std::uint32_t childCount = static_cast<std::uint32_t>(
                    rawChildCount & UINT64_C(0x7FFF));
                const std::uint32_t childBegin =
                    static_cast<std::uint32_t>(rawChildBegin);
                const std::uint32_t childLimit =
                    hasLeafChildren ? leafEntryCount : nodeEntryCount;
                if (childCount == 0
                    || !UnsignedSpanWithinPartition(
                        childBegin,
                        childCount,
                        0,
                        childLimit))
                {
                    return XSurfaceCollisionTopologyStatus::InvalidChildSpan;
                }

                if (!hasLeafChildren)
                {
                    for (std::uint32_t child = 0;
                        child < childCount;
                        ++child)
                    {
                        const std::uint32_t childIndex = childBegin + child;
                        if (nodeOwners[childIndex])
                        {
                            return XSurfaceCollisionTopologyStatus::InvalidTopology;
                        }
                        nodeOwners[childIndex] = 1;
                    }

                    const std::size_t pendingRangeCount =
                        ranges.size() - rangeIndex;
                    if (pendingRangeCount
                        >= kXSurfaceCollisionNodeRangeQueueCapacity - 1u)
                    {
                        return XSurfaceCollisionTopologyStatus::
                            NodeRangeQueueCapacityExceeded;
                    }
                    ranges.push_back({childBegin, childCount});
                    continue;
                }

                for (std::uint32_t child = 0;
                    child < childCount;
                    ++child)
                {
                    const std::uint32_t leafIndex = childBegin + child;
                    if (leafOwners[leafIndex])
                    {
                        return XSurfaceCollisionTopologyStatus::InvalidTopology;
                    }
                    leafOwners[leafIndex] = 1;

                    const std::uint64_t encodedTriangle =
                        static_cast<std::uint64_t>(
                            leafs[leafIndex].triangleBeginIndex);
                    if (encodedTriangle
                        > (std::numeric_limits<std::uint16_t>::max)())
                    {
                        return XSurfaceCollisionTopologyStatus::
                            InvalidTriangleSpan;
                    }
                    const std::uint32_t triangleBegin =
                        static_cast<std::uint32_t>(
                            encodedTriangle & UINT64_C(0x7FFF));
                    const std::uint32_t triangleCount =
                        (encodedTriangle & UINT64_C(0x8000)) != 0 ? 2u : 1u;
                    if (!UnsignedSpanWithinPartition(
                            triangleBegin,
                            triangleCount,
                            rigidTriangleOffset,
                            rigidTriangleCount)
                        || !UnsignedSpanWithinPartition(
                            triangleBegin,
                            triangleCount,
                            0,
                            surfaceTriangleCount))
                    {
                        return XSurfaceCollisionTopologyStatus::
                            InvalidTriangleSpan;
                    }
                }
            }
        }

        if (!CoverageComplete(nodeOwners.data(), nodeCount)
            || !CoverageComplete(leafOwners.data(), leafCount))
        {
            return XSurfaceCollisionTopologyStatus::InvalidTopology;
        }
    }
    catch (const std::bad_alloc &)
    {
        return XSurfaceCollisionTopologyStatus::AllocationFailure;
    }

    // The trans/scale floats are intentionally outside this integer-topology
    // check for compatibility with legacy source assets.
    return XSurfaceCollisionTopologyStatus::Ok;
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
            > static_cast<std::uint32_t>(
                (std::numeric_limits<std::uint8_t>::max)()) / 2u)
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

constexpr std::uint32_t kMaxClipMapBrushNonaxialSides = 250;
constexpr std::uint32_t kClipMapMaterialDiskBytes = 72;

struct ClipMapBrushLayoutExtents
{
    std::int32_t planeBytes = 0;
    std::int32_t materialBytes = 0;
    std::int32_t brushSideBytes = 0;
    std::int32_t brushEdgeBytes = 0;
    std::int32_t brushBytes = 0;
};

constexpr bool ClipMapBrushLayoutValid(
    bool hasPlanes,
    std::int64_t planeCount,
    bool hasMaterials,
    std::uint64_t materialCount,
    bool hasBrushSides,
    std::uint64_t brushSideCount,
    bool hasBrushEdges,
    std::uint64_t brushEdgeCount,
    bool hasBrushes,
    std::uint64_t brushCount,
    ClipMapBrushLayoutExtents *extents)
{
    if (!extents)
        return false;
    *extents = {};

    constexpr std::uint64_t maximumCount =
        static_cast<std::uint64_t>(
            (std::numeric_limits<std::int32_t>::max)());
    if (planeCount < 1 || planeCount > 65536 || !hasPlanes
        || materialCount == 0 || materialCount > maximumCount
        || !hasMaterials
        || brushSideCount > maximumCount
        || (brushSideCount != 0 && !hasBrushSides)
        || brushEdgeCount > maximumCount
        || (brushEdgeCount != 0 && !hasBrushEdges)
        || brushCount > maximumCount
        || (brushCount != 0 && !hasBrushes))
    {
        return false;
    }

    return CheckedArrayBytes(
            static_cast<std::int32_t>(planeCount),
            disk32::kCPlaneBytes,
            &extents->planeBytes)
        && CheckedArrayBytes(
            static_cast<std::int32_t>(materialCount),
            kClipMapMaterialDiskBytes,
            &extents->materialBytes)
        && CheckedArrayBytes(
            static_cast<std::int32_t>(brushSideCount),
            disk32::kCBrushSideBytes,
            &extents->brushSideBytes)
        && CheckedArrayBytes(
            static_cast<std::int32_t>(brushEdgeCount),
            1,
            &extents->brushEdgeBytes)
        && CheckedArrayBytes(
            static_cast<std::int32_t>(brushCount),
            disk32::kCBrushBytes,
            &extents->brushBytes);
}

template <typename ClipMap>
inline bool ClipMapBrushLayoutValid(
    const ClipMap &map,
    ClipMapBrushLayoutExtents *extents)
{
    return ClipMapBrushLayoutValid(
        map.planes != nullptr,
        static_cast<std::int64_t>(map.planeCount),
        map.materials != nullptr,
        static_cast<std::uint64_t>(map.numMaterials),
        map.brushsides != nullptr,
        static_cast<std::uint64_t>(map.numBrushSides),
        map.brushEdges != nullptr,
        static_cast<std::uint64_t>(map.numBrushEdges),
        map.brushes != nullptr,
        static_cast<std::uint64_t>(map.numBrushes),
        extents);
}

template <typename Plane>
inline bool ClipMapPlaneValid(const Plane &plane)
{
    if (!FiniteFloatArray(plane.normal, 3) || !std::isfinite(plane.dist))
        return false;

    std::uint8_t expectedSignbits = 0;
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        if (plane.normal[axis] < 0.0f)
            expectedSignbits |= static_cast<std::uint8_t>(1u << axis);
    }
    const std::uint8_t expectedType = plane.normal[0] == 1.0f ? 0
        : plane.normal[1] == 1.0f ? 1
        : plane.normal[2] == 1.0f ? 2
        : 3;
    return static_cast<std::uint32_t>(plane.type) == expectedType
        && static_cast<std::uint32_t>(plane.signbits)
            == expectedSignbits;
}

template <typename Element>
inline bool ExactArrayElementIndex(
    const Element *base,
    std::uint64_t count,
    const Element *candidate,
    std::uint64_t *index)
{
    if (index)
        *index = 0;
    if (!index || !base || !candidate || count == 0)
        return false;

    const std::uintptr_t baseAddress =
        reinterpret_cast<std::uintptr_t>(base);
    const std::uintptr_t candidateAddress =
        reinterpret_cast<std::uintptr_t>(candidate);
    constexpr std::uintptr_t elementBytes = sizeof(Element);
    const std::uintptr_t maximumAddress =
        (std::numeric_limits<std::uintptr_t>::max)();
    if (count > (maximumAddress - baseAddress) / elementBytes)
        return false;
    const std::uintptr_t endAddress = baseAddress
        + static_cast<std::uintptr_t>(count) * elementBytes;
    if (candidateAddress < baseAddress || candidateAddress >= endAddress)
        return false;
    const std::uintptr_t relativeAddress = candidateAddress - baseAddress;
    if (relativeAddress % elementBytes != 0)
        return false;
    *index = relativeAddress / elementBytes;
    return true;
}

template <typename Element>
inline bool ExactArrayBoundaryPosition(
    const Element *base,
    std::uint64_t count,
    const Element *candidate,
    std::uint64_t position)
{
    if (position > count)
        return false;
    if (!base)
        return count == 0 && position == 0 && !candidate;
    if (!candidate)
        return false;

    const std::uintptr_t baseAddress =
        reinterpret_cast<std::uintptr_t>(base);
    constexpr std::uintptr_t elementBytes = sizeof(Element);
    const std::uintptr_t maximumAddress =
        (std::numeric_limits<std::uintptr_t>::max)();
    if (position > (maximumAddress - baseAddress) / elementBytes)
        return false;
    return reinterpret_cast<std::uintptr_t>(candidate)
        == baseAddress + static_cast<std::uintptr_t>(position) * elementBytes;
}

template <typename Element>
inline bool SerializedArrayElementIndex(
    const Element *base,
    std::uint64_t count,
    std::uint32_t serializedStride,
    const Element *candidate,
    std::uint64_t *index)
{
    if (index)
        *index = 0;
    if (!index || !base || !candidate || count == 0
        || serializedStride == 0)
    {
        return false;
    }

    const std::uintptr_t baseAddress =
        reinterpret_cast<std::uintptr_t>(base);
    const std::uintptr_t candidateAddress =
        reinterpret_cast<std::uintptr_t>(candidate);
    const std::uintptr_t elementBytes = serializedStride;
    const std::uintptr_t maximumAddress =
        (std::numeric_limits<std::uintptr_t>::max)();
    if (count > (maximumAddress - baseAddress) / elementBytes)
        return false;

    const std::uintptr_t endAddress = baseAddress
        + static_cast<std::uintptr_t>(count) * elementBytes;
    if (candidateAddress < baseAddress || candidateAddress >= endAddress)
        return false;

    const std::uintptr_t relativeAddress = candidateAddress - baseAddress;
    if (relativeAddress % elementBytes != 0)
        return false;

    *index = relativeAddress / elementBytes;
    return true;
}

constexpr bool ClipMapBrushAdjacencySlotValid(
    std::int64_t offset,
    std::uint32_t count,
    std::uint32_t *runningCount)
{
    if (!runningCount || offset < 0
        || static_cast<std::uint64_t>(offset) != *runningCount
        || count > (std::numeric_limits<std::uint32_t>::max)()
            - *runningCount)
    {
        return false;
    }
    *runningCount += count;
    return true;
}

template <typename Brush>
inline bool ClipMapBrushAdjacencyPrefixExtent(
    const Brush &brush,
    std::uint32_t *edgeCount)
{
    if (edgeCount)
        *edgeCount = 0;
    if (!edgeCount || brush.numsides > kMaxClipMapBrushNonaxialSides
        || (brush.numsides != 0) != (brush.sides != nullptr))
    {
        return false;
    }

    std::uint32_t runningCount = 0;
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            if (!ClipMapBrushAdjacencySlotValid(
                    brush.firstAdjacentSideOffsets[direction][axis],
                    brush.edgeCount[direction][axis],
                    &runningCount))
            {
                return false;
            }
        }
    }
    for (std::uint32_t sideIndex = 0;
        sideIndex < brush.numsides;
        ++sideIndex)
    {
        if (!ClipMapBrushAdjacencySlotValid(
                brush.sides[sideIndex].firstAdjacentSideOffset,
                brush.sides[sideIndex].edgeCount,
                &runningCount))
        {
            return false;
        }
    }
    *edgeCount = runningCount;
    return true;
}

template <typename Brush>
inline bool ClipMapBrushAdjacencyExtentValid(
    const Brush &brush,
    std::uint32_t *edgeCount)
{
    std::uint32_t derivedCount = 0;
    if (edgeCount)
        *edgeCount = 0;
    if (!edgeCount
        || !ClipMapBrushAdjacencyPrefixExtent(brush, &derivedCount)
        || (derivedCount != 0 && !brush.baseAdjacentSide))
    {
        return false;
    }

    const std::uint32_t sideIdentityLimit = brush.numsides + 6;
    for (std::uint32_t edgeIndex = 0; edgeIndex < derivedCount; ++edgeIndex)
    {
        if (static_cast<std::uint32_t>(
                brush.baseAdjacentSide[edgeIndex]) >= sideIdentityLimit)
        {
            return false;
        }
    }
    *edgeCount = derivedCount;
    return true;
}

template <typename Brush>
inline bool ClipMapOrdinaryBrushValid(
    const Brush &brush,
    std::uint32_t materialCount,
    std::uint32_t *edgeCount)
{
    if (edgeCount)
        *edgeCount = 0;
    if (!edgeCount || materialCount == 0
        || !FiniteFloatArray(brush.mins, 3)
        || !FiniteFloatArray(brush.maxs, 3)
        || !ClipMapBrushAdjacencyExtentValid(brush, edgeCount))
    {
        return false;
    }

    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        if (brush.mins[axis] > brush.maxs[axis])
            return false;
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            const std::int64_t material =
                brush.axialMaterialNum[direction][axis];
            if (material < 0
                || static_cast<std::uint64_t>(material) >= materialCount)
            {
                return false;
            }
        }
    }
    for (std::uint32_t sideIndex = 0;
        sideIndex < brush.numsides;
        ++sideIndex)
    {
        const auto &side = brush.sides[sideIndex];
        if (!side.plane || side.materialNum >= materialCount)
            return false;
    }
    return true;
}

template <typename Brush>
inline bool ClipMapBoxBrushValid(const Brush &brush)
{
    if (brush.numsides != 0 || brush.sides || brush.baseAdjacentSide
        || brush.contents != -1
        || !FiniteFloatArray(brush.mins, 3)
        || !FiniteFloatArray(brush.maxs, 3))
    {
        return false;
    }

    bool sourceSentinel = true;
    bool orderedBounds = true;
    const float maximumFloat = (std::numeric_limits<float>::max)();
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        sourceSentinel = sourceSentinel
            && brush.mins[axis] == maximumFloat
            && brush.maxs[axis] == -maximumFloat;
        orderedBounds = orderedBounds
            && brush.mins[axis] <= brush.maxs[axis];
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            if (brush.axialMaterialNum[direction][axis] != -1
                || brush.firstAdjacentSideOffsets[direction][axis] != 0
                || brush.edgeCount[direction][axis] != 0)
            {
                return false;
            }
        }
    }
    return sourceSentinel || orderedBounds;
}

template <typename ClipMap>
inline bool ClipMapBrushGraphValid(const ClipMap &map)
{
    ClipMapBrushLayoutExtents extents = {};
    if (!ClipMapBrushLayoutValid(map, &extents))
        return false;

    const std::uint32_t planeCount =
        static_cast<std::uint32_t>(map.planeCount);
    const std::uint32_t materialCount =
        static_cast<std::uint32_t>(map.numMaterials);
    const std::uint64_t brushSideCount = map.numBrushSides;
    const std::uint64_t brushEdgeCount = map.numBrushEdges;
    for (std::uint32_t planeIndex = 0;
        planeIndex < planeCount;
        ++planeIndex)
    {
        if (!ClipMapPlaneValid(map.planes[planeIndex]))
            return false;
    }

    std::uint64_t consumedSides = 0;
    std::uint64_t consumedEdges = 0;
    for (std::uint32_t brushIndex = 0;
        brushIndex < static_cast<std::uint32_t>(map.numBrushes);
        ++brushIndex)
    {
        const auto &brush = map.brushes[brushIndex];
        if (brush.numsides > kMaxClipMapBrushNonaxialSides)
            return false;
        if (brush.numsides == 0)
        {
            if (brush.sides)
                return false;
        }
        else
        {
            std::uint64_t sideIndex = 0;
            if (brush.numsides > brushSideCount - consumedSides
                || !ExactArrayElementIndex(
                    map.brushsides,
                    brushSideCount,
                    brush.sides,
                    &sideIndex)
                || sideIndex != consumedSides)
            {
                return false;
            }
        }

        std::uint32_t derivedEdgeCount = 0;
        if (!ClipMapBrushAdjacencyPrefixExtent(
                brush,
                &derivedEdgeCount)
            || derivedEdgeCount > brushEdgeCount - consumedEdges
            || !ExactArrayBoundaryPosition(
                map.brushEdges,
                brushEdgeCount,
                brush.baseAdjacentSide,
                consumedEdges))
        {
            return false;
        }

        std::uint32_t validatedEdgeCount = 0;
        if (!ClipMapOrdinaryBrushValid(
                brush,
                materialCount,
                &validatedEdgeCount)
            || validatedEdgeCount != derivedEdgeCount)
        {
            return false;
        }
        for (std::uint32_t sideOffset = 0;
            sideOffset < brush.numsides;
            ++sideOffset)
        {
            std::uint64_t planeIndex = 0;
            if (!ExactArrayElementIndex(
                    map.planes,
                    planeCount,
                    brush.sides[sideOffset].plane,
                    &planeIndex))
            {
                return false;
            }
        }
        consumedSides += brush.numsides;
        consumedEdges += derivedEdgeCount;
    }
    return consumedSides == brushSideCount
        && consumedEdges == brushEdgeCount;
}

constexpr std::uint32_t kMaxGfxWorldCells = 1024;
constexpr std::uint32_t kMinGfxPortalVertices = 3;
constexpr std::uint32_t kMaxGfxPortalVertices = 64;
constexpr std::uint32_t kMinGfxReflectionProbes = 1;
constexpr std::uint32_t kMaxGfxReflectionProbes = 254;

constexpr bool GfxReflectionProbeCountValid(std::uint64_t count)
{
    return count >= kMinGfxReflectionProbes
        && count <= kMaxGfxReflectionProbes;
}

constexpr bool GfxWorldCellBitsValid(
    std::int64_t cellCount,
    std::int64_t cellBitsBytes)
{
    return CountInRange(cellCount, 1, kMaxGfxWorldCells)
        && cellBitsBytes == 16 * ((cellCount + 127) / 128);
}

constexpr bool GfxWorldCellLayoutValid(
    bool hasCells,
    std::int64_t cellCount,
    std::int32_t *cellBytes)
{
    if (cellBytes)
        *cellBytes = 0;
    if (!cellBytes || !hasCells
        || !CountInRange(cellCount, 1, kMaxGfxWorldCells))
    {
        return false;
    }

    return CheckedArrayBytes(
        static_cast<std::int32_t>(cellCount),
        disk32::kGfxCellBytes,
        cellBytes);
}

struct GfxCellLayoutExtents
{
    std::int32_t aabbTreeBytes = 0;
    std::int32_t portalBytes = 0;
    std::int32_t cullGroupBytes = 0;
    std::int32_t reflectionProbeBytes = 0;
};

inline bool GfxCellLayoutValid(
    const float *mins,
    const float *maxs,
    bool hasAabbTree,
    std::int64_t aabbTreeCount,
    bool hasPortals,
    std::int64_t portalCount,
    bool hasCullGroups,
    std::int64_t cullGroupCount,
    bool hasReflectionProbes,
    std::uint64_t reflectionProbeCount,
    GfxCellLayoutExtents *extents)
{
    if (!extents)
        return false;
    *extents = {};

    const std::int64_t maximumCount =
        (std::numeric_limits<std::int32_t>::max)();
    if (!FiniteFloatArray(mins, 3)
        || !FiniteFloatArray(maxs, 3)
        || aabbTreeCount < 1 || aabbTreeCount > maximumCount
        || portalCount < 0 || portalCount > maximumCount
        || cullGroupCount < 0 || cullGroupCount > maximumCount
        || !GfxReflectionProbeCountValid(reflectionProbeCount)
        || hasAabbTree != (aabbTreeCount != 0)
        || hasPortals != (portalCount != 0)
        || hasCullGroups != (cullGroupCount != 0)
        || hasReflectionProbes != (reflectionProbeCount != 0))
    {
        return false;
    }

    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        if (mins[axis] > maxs[axis])
            return false;
    }

    return CheckedArrayBytes(
            static_cast<std::int32_t>(aabbTreeCount),
            disk32::kGfxAabbTreeBytes,
            &extents->aabbTreeBytes)
        && CheckedArrayBytes(
            static_cast<std::int32_t>(portalCount),
            disk32::kGfxPortalBytes,
            &extents->portalBytes)
        && CheckedArrayBytes(
            static_cast<std::int32_t>(cullGroupCount),
            4,
            &extents->cullGroupBytes)
        && CheckedArrayBytes(
            static_cast<std::int32_t>(reflectionProbeCount),
            1,
            &extents->reflectionProbeBytes);
}

template <typename Cell>
inline bool GfxCellLayoutValid(
    const Cell &cell,
    GfxCellLayoutExtents *extents)
{
    return GfxCellLayoutValid(
        cell.mins,
        cell.maxs,
        cell.aabbTree != nullptr,
        static_cast<std::int64_t>(cell.aabbTreeCount),
        cell.portals != nullptr,
        static_cast<std::int64_t>(cell.portalCount),
        cell.cullGroups != nullptr,
        static_cast<std::int64_t>(cell.cullGroupCount),
        cell.reflectionProbes != nullptr,
        static_cast<std::uint64_t>(cell.reflectionProbeCount),
        extents);
}

template <typename Portal>
inline bool GfxPortalRuntimeValid(const Portal &portal)
{
    const std::int64_t vertexCount = portal.vertexCount;
    if (!portal.cell || !portal.vertices
        || !CountInRange(
            vertexCount,
            kMinGfxPortalVertices,
            kMaxGfxPortalVertices)
        || !FiniteFloatArray(portal.plane.coeffs, 4)
        || (portal.plane.coeffs[0] == 0.0f
            && portal.plane.coeffs[1] == 0.0f
            && portal.plane.coeffs[2] == 0.0f)
        || !FiniteFloatArray(&portal.hullAxis[0][0], 6))
    {
        return false;
    }

    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        const std::uint32_t expectedSide = axis * 4
            + (portal.plane.coeffs[axis] > 0.0f ? 12 : 0);
        if (static_cast<std::uint32_t>(portal.plane.side[axis])
            != expectedSide)
        {
            return false;
        }
    }

    for (std::uint32_t vertexIndex = 0;
        vertexIndex < static_cast<std::uint32_t>(vertexCount);
        ++vertexIndex)
    {
        if (!FiniteFloatArray(portal.vertices[vertexIndex], 3))
            return false;
    }
    return true;
}

template <typename ReflectionProbe>
inline bool GfxReflectionProbeRuntimeValid(
    const ReflectionProbe &probe)
{
    return FiniteFloatArray(probe.origin, 3)
        && probe.reflectionImage != nullptr;
}

template <typename CullGroup>
inline bool GfxCullGroupRuntimeValid(
    const CullGroup &group,
    std::uint64_t sortedSurfaceCount)
{
    if (!FiniteFloatArray(group.mins, 3)
        || !FiniteFloatArray(group.maxs, 3))
    {
        return false;
    }
    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        if (group.mins[axis] > group.maxs[axis])
            return false;
    }

    const std::int64_t surfaceCount = group.surfaceCount;
    const std::int64_t startSurface = group.startSurfIndex;
    if (surfaceCount < 0)
        return false;
    if (surfaceCount == 0)
        return startSurface == -1;
    return startSurface >= 0
        && UnsignedSpanWithinPartition(
            static_cast<std::uint64_t>(startSurface),
            static_cast<std::uint64_t>(surfaceCount),
            0,
            sortedSurfaceCount);
}

template <typename World>
inline bool GfxWorldCellGraphValid(
    const World &world,
    std::uint32_t serializedCellStride = disk32::kGfxCellBytes)
{
    const std::int64_t cellCount = world.dpvsPlanes.cellCount;
    std::int32_t cellBytes = 0;
    if (!GfxWorldCellLayoutValid(
            world.cells != nullptr,
            cellCount,
            &cellBytes)
        || world.cullGroupCount < 0
        || (world.dpvs.cullGroups != nullptr)
            != (world.cullGroupCount != 0)
        || !GfxReflectionProbeCountValid(world.reflectionProbeCount)
        || !world.reflectionProbes
        || !world.reflectionProbeTextures)
    {
        return false;
    }

    const std::uint64_t staticSurfaceCount =
        static_cast<std::uint64_t>(world.dpvs.staticSurfaceCount);
    const std::uint64_t staticSurfaceCountNoDecal =
        static_cast<std::uint64_t>(world.dpvs.staticSurfaceCountNoDecal);
    const std::uint64_t sortedSurfaceCount = staticSurfaceCount
        + staticSurfaceCountNoDecal;
    const std::int64_t worldSurfaceCount =
        static_cast<std::int64_t>(world.surfaceCount);
    if (worldSurfaceCount < 0
        || !WorldAabbSurfacePartitionsValid(
            staticSurfaceCount,
            staticSurfaceCountNoDecal,
            static_cast<std::uint64_t>(worldSurfaceCount))
        || (world.dpvs.sortedSurfIndex != nullptr)
            != (sortedSurfaceCount != 0)
        || sortedSurfaceCount
            > (std::numeric_limits<std::uint32_t>::max)()
        || !AllU16Below(
            world.dpvs.sortedSurfIndex,
            static_cast<std::uint32_t>(sortedSurfaceCount),
            static_cast<std::uint32_t>(staticSurfaceCount)))
    {
        return false;
    }

    const std::uint32_t checkedCellCount =
        static_cast<std::uint32_t>(cellCount);
    std::uint64_t firstCellIndex = 0;
    if (!SerializedArrayElementIndex(
            world.cells,
            checkedCellCount,
            serializedCellStride,
            world.cells,
            &firstCellIndex)
        || firstCellIndex != 0)
    {
        return false;
    }

    const std::uint64_t globalCullGroupCount =
        static_cast<std::uint64_t>(world.cullGroupCount);
    const std::uint64_t globalReflectionProbeCount =
        static_cast<std::uint64_t>(world.reflectionProbeCount);
    for (std::uint64_t probeIndex = 0;
        probeIndex < globalReflectionProbeCount;
        ++probeIndex)
    {
        if (!GfxReflectionProbeRuntimeValid(
                world.reflectionProbes[probeIndex]))
        {
            return false;
        }
    }
    for (std::uint64_t cullGroupIndex = 0;
        cullGroupIndex < globalCullGroupCount;
        ++cullGroupIndex)
    {
        if (!GfxCullGroupRuntimeValid(
                world.dpvs.cullGroups[cullGroupIndex],
                sortedSurfaceCount))
        {
            return false;
        }
    }
    for (std::uint32_t cellIndex = 0;
        cellIndex < checkedCellCount;
        ++cellIndex)
    {
        const auto &cell = world.cells[cellIndex];
        GfxCellLayoutExtents extents = {};
        if (!GfxCellLayoutValid(cell, &extents))
            return false;

        for (std::int32_t cullIndex = 0;
            cullIndex < cell.cullGroupCount;
            ++cullIndex)
        {
            const std::int64_t groupIndex = cell.cullGroups[cullIndex];
            if (groupIndex < 0
                || static_cast<std::uint64_t>(groupIndex)
                    >= globalCullGroupCount)
            {
                return false;
            }
        }
        for (std::uint32_t probeIndex = 0;
            probeIndex
                < static_cast<std::uint32_t>(cell.reflectionProbeCount);
            ++probeIndex)
        {
            if (static_cast<std::uint64_t>(
                    cell.reflectionProbes[probeIndex])
                >= globalReflectionProbeCount)
            {
                return false;
            }
        }
        for (std::int32_t portalIndex = 0;
            portalIndex < cell.portalCount;
            ++portalIndex)
        {
            const auto &portal = cell.portals[portalIndex];
            std::uint64_t targetCellIndex = 0;
            if (!GfxPortalRuntimeValid(portal)
                || !SerializedArrayElementIndex(
                    world.cells,
                    checkedCellCount,
                    serializedCellStride,
                    portal.cell,
                    &targetCellIndex))
            {
                return false;
            }
        }
    }
    return true;
}

constexpr std::uint32_t kMaxPathNodes = 8192;
constexpr std::uint32_t kMaxPathTreeNodes = 16383;
constexpr std::uint32_t kMaxPathTreeDepth = 64;
constexpr std::uint32_t kMaxPathChainDepth = 256;
constexpr std::int32_t kMaxPathNodeType = 19;

constexpr bool PathNodeTypeValid(std::int64_t type)
{
    return type >= 0 && type <= kMaxPathNodeType;
}

constexpr bool PathNodeReferenceValid(
    std::int64_t nodeIndex,
    std::uint64_t nodeCount)
{
    return nodeIndex == -1
        || (nodeIndex >= 0
            && static_cast<std::uint64_t>(nodeIndex) < nodeCount);
}

template <typename Node>
inline bool PathNodesRuntimeValid(
    const Node *nodes,
    std::uint64_t nodeCount)
{
    if (nodeCount > kMaxPathNodes
        || (nodes != nullptr) != (nodeCount != 0))
    {
        return false;
    }

    try
    {
        std::vector<std::uint8_t> parentStates(
            static_cast<std::size_t>(nodeCount),
            UINT8_C(0));
        std::vector<std::uint32_t> parentDepths(
            static_cast<std::size_t>(nodeCount),
            UINT32_C(0));
        std::vector<std::uint32_t> parentTrail;
        parentTrail.reserve(static_cast<std::size_t>(nodeCount));

        for (std::uint64_t nodeIndex = 0;
            nodeIndex < nodeCount;
            ++nodeIndex)
        {
            const auto &constant = nodes[nodeIndex].constant;
            const std::int64_t overlap0 =
                static_cast<std::int64_t>(constant.wOverlapNode[0]);
            const std::int64_t overlap1 =
                static_cast<std::int64_t>(constant.wOverlapNode[1]);
            if (!PathNodeTypeValid(
                    static_cast<std::int64_t>(constant.type))
                || !PathNodeReferenceValid(
                    overlap0,
                    nodeCount)
                || !PathNodeReferenceValid(
                    overlap1,
                    nodeCount)
                || (overlap0 == -1 && overlap1 != -1)
                || overlap0 == static_cast<std::int64_t>(nodeIndex)
                || overlap1 == static_cast<std::int64_t>(nodeIndex)
                || (overlap0 >= 0 && overlap0 == overlap1)
                || !PathNodeReferenceValid(
                    static_cast<std::int64_t>(constant.wChainParent),
                    nodeCount)
                || constant.wChainParent
                    == static_cast<std::int64_t>(nodeIndex)
                || !std::isfinite(constant.vOrigin[0])
                || !std::isfinite(constant.vOrigin[1])
                || !std::isfinite(constant.vOrigin[2])
                || !std::isfinite(constant.fAngle)
                || !std::isfinite(constant.forward[0])
                || !std::isfinite(constant.forward[1])
                || !std::isfinite(constant.fRadius)
                || !std::isfinite(constant.minUseDistSq))
            {
                return false;
            }
        }

        for (std::uint32_t start = 0;
            start < nodeCount;
            ++start)
        {
            if (parentStates[start] == 2)
                continue;
            parentTrail.clear();
            std::int64_t current = start;
            std::uint32_t parentDepth = 0;
            while (current >= 0)
            {
                if (parentTrail.size() >= kMaxPathChainDepth)
                    return false;
                const std::uint32_t index =
                    static_cast<std::uint32_t>(current);
                if (parentStates[index] == 1)
                    return false;
                if (parentStates[index] == 2)
                {
                    parentDepth = parentDepths[index];
                    break;
                }
                parentStates[index] = 1;
                parentTrail.push_back(index);
                current = static_cast<std::int64_t>(
                    nodes[index].constant.wChainParent);
            }
            for (auto entry = parentTrail.rbegin();
                entry != parentTrail.rend();
                ++entry)
            {
                if (parentDepth >= kMaxPathChainDepth)
                    return false;
                ++parentDepth;
                parentDepths[*entry] = parentDepth;
                parentStates[*entry] = 2;
            }
        }
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}

inline bool PathChainMapsRuntimeValid(
    const std::uint16_t *chainNodeForNode,
    const std::uint16_t *nodeForChainNode,
    std::uint64_t nodeCount,
    std::uint64_t chainNodeCount)
{
    if (nodeCount > kMaxPathNodes
        || chainNodeCount > nodeCount
        || (chainNodeForNode != nullptr) != (nodeCount != 0)
        || (nodeForChainNode != nullptr) != (nodeCount != 0))
    {
        return false;
    }
    for (std::uint64_t position = 0;
        position < nodeCount;
        ++position)
    {
        const std::uint32_t nodeIndex = nodeForChainNode[position];
        if (nodeIndex >= nodeCount
            || chainNodeForNode[nodeIndex] != position)
        {
            return false;
        }
    }
    return true;
}

constexpr bool PathVisibilityBytes(
    std::uint64_t nodeCount,
    std::int32_t *bytes)
{
    if (bytes)
        *bytes = 0;
    if (!bytes || nodeCount > kMaxPathNodes)
        return false;

    const std::uint64_t bitCount = nodeCount
        * (nodeCount ? nodeCount - 1u : 0u);
    const std::uint64_t byteCount = (bitCount + 7u) / 8u;
    if (byteCount
        > static_cast<std::uint64_t>(
            (std::numeric_limits<std::int32_t>::max)()))
    {
        return false;
    }
    *bytes = static_cast<std::int32_t>(byteCount);
    return true;
}

constexpr bool PathTreeLayoutValid(
    bool hasTree,
    std::uint64_t pathNodeCount,
    std::int64_t treeNodeCount,
    std::int32_t *treeBytes)
{
    if (treeBytes)
        *treeBytes = 0;
    if (!treeBytes || pathNodeCount > kMaxPathNodes
        || treeNodeCount < 0
        || treeNodeCount > kMaxPathTreeNodes
        || hasTree != (treeNodeCount != 0))
    {
        return false;
    }
    if (pathNodeCount == 0)
        return treeNodeCount == 0;
    if (treeNodeCount
        > static_cast<std::int64_t>(pathNodeCount * 2u - 1u))
    {
        return false;
    }
    return CheckedArrayBytes(
        static_cast<std::int32_t>(treeNodeCount),
        disk32::kPathTreeBytes,
        treeBytes);
}

struct PathDataLayoutExtents
{
    std::int32_t nodeBytes = 0;
    std::int32_t baseNodeBytes = 0;
    std::int32_t chainMapBytes = 0;
    std::int32_t visibilityBytes = 0;
    std::int32_t treeBytes = 0;
};

inline bool PathDataLayoutValid(
    bool hasNodes,
    bool hasBaseNodes,
    std::uint64_t nodeCount,
    std::uint64_t chainNodeCount,
    bool hasChainNodeForNode,
    bool hasNodeForChainNode,
    bool hasVisibility,
    std::int64_t visibilityBytes,
    bool hasTree,
    std::int64_t treeNodeCount,
    PathDataLayoutExtents *extents)
{
    if (!extents)
        return false;
    *extents = {};

    std::int32_t expectedVisibilityBytes = 0;
    if (nodeCount > kMaxPathNodes
        || chainNodeCount > nodeCount
        || visibilityBytes < 0
        || hasNodes != (nodeCount != 0)
        || hasBaseNodes != (nodeCount != 0)
        || hasChainNodeForNode != (nodeCount != 0)
        || hasNodeForChainNode != (nodeCount != 0)
        || hasVisibility != (visibilityBytes != 0)
        || !PathVisibilityBytes(nodeCount, &expectedVisibilityBytes)
        || (hasVisibility
            && visibilityBytes != expectedVisibilityBytes)
        || !PathTreeLayoutValid(
            hasTree,
            nodeCount,
            treeNodeCount,
            &extents->treeBytes))
    {
        return false;
    }

    const std::int32_t checkedNodeCount =
        static_cast<std::int32_t>(nodeCount);
    if (!CheckedArrayBytes(
            checkedNodeCount,
            disk32::kPathNodeBytes,
            &extents->nodeBytes)
        || !CheckedArrayBytes(
            checkedNodeCount,
            disk32::kPathBaseNodeBytes,
            &extents->baseNodeBytes))
    {
        return false;
    }
    if (hasChainNodeForNode
        && !CheckedArrayBytes(
            checkedNodeCount,
            disk32::kPathNodeIndexBytes,
            &extents->chainMapBytes))
    {
        return false;
    }
    extents->visibilityBytes =
        static_cast<std::int32_t>(visibilityBytes);
    return true;
}

template <typename PathData>
inline bool PathDataLayoutValid(
    const PathData &path,
    PathDataLayoutExtents *extents)
{
    return PathDataLayoutValid(
        path.nodes != nullptr,
        path.basenodes != nullptr,
        static_cast<std::uint64_t>(path.nodeCount),
        static_cast<std::uint64_t>(path.chainNodeCount),
        path.chainNodeForNode != nullptr,
        path.nodeForChainNode != nullptr,
        path.pathVis != nullptr,
        static_cast<std::int64_t>(path.visBytes),
        path.nodeTree != nullptr,
        static_cast<std::int64_t>(path.nodeTreeCount),
        extents);
}

template <typename Link>
inline bool PathLinksRuntimeValid(
    const Link *links,
    std::uint64_t linkCount,
    std::uint64_t pathNodeCount)
{
    if (pathNodeCount > kMaxPathNodes
        || linkCount > pathNodeCount
        || (links != nullptr) != (linkCount != 0))
    {
        return false;
    }
    for (std::uint64_t linkIndex = 0;
        linkIndex < linkCount;
        ++linkIndex)
    {
        if (static_cast<std::uint64_t>(links[linkIndex].nodeNum)
                >= pathNodeCount
            || !std::isfinite(links[linkIndex].fDist)
            || links[linkIndex].fDist < 0.0f)
        {
            return false;
        }
    }
    return true;
}

struct PathTreeVisit
{
    std::uint32_t index;
    std::uint32_t depth;
    bool exiting;
};

template <typename Tree>
inline bool PathTreeGraphValid(
    const Tree *trees,
    std::uint64_t treeNodeCount,
    std::uint64_t pathNodeCount,
    std::uint32_t serializedTreeStride = disk32::kPathTreeBytes)
{
    std::int32_t treeBytes = 0;
    if (treeNodeCount
            > static_cast<std::uint64_t>(
                (std::numeric_limits<std::int64_t>::max)())
        || !PathTreeLayoutValid(
            trees != nullptr,
            pathNodeCount,
            static_cast<std::int64_t>(treeNodeCount),
            &treeBytes))
    {
        return false;
    }
    if (treeNodeCount == 0)
        return true;

    std::uint64_t rootIndex = 0;
    if (!SerializedArrayElementIndex(
            trees,
            treeNodeCount,
            serializedTreeStride,
            trees,
            &rootIndex)
        || rootIndex != 0)
    {
        return false;
    }

    try
    {
        std::vector<std::uint8_t> states(
            static_cast<std::size_t>(treeNodeCount),
            UINT8_C(0));
        std::vector<std::uint8_t> leafOwners(
            static_cast<std::size_t>(pathNodeCount),
            UINT8_C(0));
        std::vector<PathTreeVisit> pending;
        pending.reserve(static_cast<std::size_t>(treeNodeCount) * 2u);
        pending.push_back({0, 1, false});
        std::uint64_t totalLeafEntries = 0;

        while (!pending.empty())
        {
            const PathTreeVisit visit = pending.back();
            pending.pop_back();
            if (visit.exiting)
            {
                if (states[visit.index] != 1)
                    return false;
                states[visit.index] = 2;
                continue;
            }
            if (visit.depth > kMaxPathTreeDepth
                || states[visit.index] != 0)
            {
                return false;
            }
            states[visit.index] = 1;

            const Tree &tree = trees[visit.index];
            const std::int64_t axis =
                static_cast<std::int64_t>(tree.axis);
            if (axis >= 0)
            {
                if (axis > 2 || !std::isfinite(tree.dist)
                    || visit.depth >= kMaxPathTreeDepth)
                {
                    return false;
                }
                std::uint64_t childIndices[2] = {};
                for (std::uint32_t child = 0; child < 2; ++child)
                {
                    if (!SerializedArrayElementIndex(
                            trees,
                            treeNodeCount,
                            serializedTreeStride,
                            tree.u.child[child],
                            &childIndices[child]))
                    {
                        return false;
                    }
                }
                pending.push_back({visit.index, visit.depth, true});
                pending.push_back({
                    static_cast<std::uint32_t>(childIndices[1]),
                    visit.depth + 1u,
                    false});
                pending.push_back({
                    static_cast<std::uint32_t>(childIndices[0]),
                    visit.depth + 1u,
                    false});
                continue;
            }

            const std::int64_t leafCount =
                static_cast<std::int64_t>(tree.u.s.nodeCount);
            if (!CountInRange(
                    leafCount,
                    1,
                    static_cast<std::int64_t>(pathNodeCount))
                || !tree.u.s.nodes
                || static_cast<std::uint64_t>(leafCount)
                    > pathNodeCount - totalLeafEntries)
            {
                return false;
            }
            totalLeafEntries += static_cast<std::uint64_t>(leafCount);
            for (std::int64_t leafIndex = 0;
                leafIndex < leafCount;
                ++leafIndex)
            {
                const std::uint32_t pathNodeIndex =
                    tree.u.s.nodes[leafIndex];
                if (pathNodeIndex >= pathNodeCount
                    || leafOwners[pathNodeIndex])
                {
                    return false;
                }
                leafOwners[pathNodeIndex] = 1;
            }
            states[visit.index] = 2;
        }

        for (const std::uint8_t state : states)
        {
            if (state != 2)
                return false;
        }
        return true;
    }
    catch (const std::bad_alloc &)
    {
        return false;
    }
}

constexpr std::uint32_t kMaxBrushNonaxialSides = 26;
constexpr std::uint32_t kMaxBrushWindingEdges = 12;
constexpr std::int32_t kMaxBrushAdjacencyEntries = 32 * 12;

constexpr bool BrushWrapperLayoutValid(
    bool hasSides,
    bool hasPlanes,
    std::uint32_t sideCount,
    bool hasAdjacency,
    std::int64_t adjacencyCount,
    std::int32_t *sideBytes,
    std::int32_t *planeBytes,
    std::int32_t *adjacencyBytes)
{
    if (sideBytes)
        *sideBytes = 0;
    if (planeBytes)
        *planeBytes = 0;
    if (adjacencyBytes)
        *adjacencyBytes = 0;
    if (!sideBytes || !planeBytes || !adjacencyBytes
        || sideCount > kMaxBrushNonaxialSides
        || adjacencyCount < 0
        || adjacencyCount > kMaxBrushAdjacencyEntries
        || !PointerCountConsistent(hasSides, sideCount)
        || !PointerCountConsistent(hasPlanes, sideCount)
        || !PointerCountConsistent(hasAdjacency, adjacencyCount))
    {
        return false;
    }

    const std::int32_t signedSideCount =
        static_cast<std::int32_t>(sideCount);
    const std::int32_t signedAdjacencyCount =
        static_cast<std::int32_t>(adjacencyCount);
    return CheckedArrayBytes(
            signedSideCount,
            disk32::kCBrushSideBytes,
            sideBytes)
        && CheckedArrayBytes(
            signedSideCount,
            disk32::kCPlaneBytes,
            planeBytes)
        && CheckedArrayBytes(
            signedAdjacencyCount,
            1,
            adjacencyBytes);
}

constexpr bool BrushAdjacencySpanValid(
    std::int64_t offset,
    std::uint64_t count,
    std::int64_t totalCount)
{
    if (offset < 0 || totalCount < 0 || offset > totalCount)
        return false;
    return count <= static_cast<std::uint64_t>(totalCount - offset);
}

template <typename Brush>
inline bool BrushMaterialIndex(
    const Brush *brush,
    std::uint32_t brushSideIndex,
    std::uint32_t materialCount,
    std::uint32_t *materialIndex)
{
    if (materialIndex)
        *materialIndex = 0;
    if (!brush || !materialIndex || !materialCount)
        return false;

    if (brushSideIndex >= 6)
    {
        const std::uint32_t sideIndex = brushSideIndex - 6;
        if (!brush->sides || sideIndex >= brush->numsides)
            return false;
        if (brush->sides[sideIndex].materialNum >= materialCount)
            return false;
        *materialIndex = brush->sides[sideIndex].materialNum;
        return true;
    }

    const std::int64_t axialMaterial =
        brush->axialMaterialNum[brushSideIndex & 1][brushSideIndex >> 1];
    if (axialMaterial < 0
        || static_cast<std::uint64_t>(axialMaterial) >= materialCount)
    {
        return false;
    }
    *materialIndex = static_cast<std::uint32_t>(axialMaterial);
    return true;
}

template <typename Brush>
inline bool BrushWrapperRuntimeValid(const Brush &brush)
{
    std::int32_t sideBytes = 0;
    std::int32_t planeBytes = 0;
    std::int32_t adjacencyBytes = 0;
    if (!BrushWrapperLayoutValid(
            brush.sides != nullptr,
            brush.planes != nullptr,
            brush.numsides,
            brush.baseAdjacentSide != nullptr,
            brush.totalEdgeCount,
            &sideBytes,
            &planeBytes,
            &adjacencyBytes)
        || !FiniteFloatArray(brush.mins, 3)
        || !FiniteFloatArray(brush.maxs, 3))
    {
        return false;
    }

    for (std::uint32_t axis = 0; axis < 3; ++axis)
    {
        if (brush.mins[axis] > brush.maxs[axis])
            return false;
        for (std::uint32_t direction = 0; direction < 2; ++direction)
        {
            if (brush.axialMaterialNum[direction][axis] != 0
                || static_cast<std::uint32_t>(
                    brush.edgeCount[direction][axis])
                    > kMaxBrushWindingEdges
                || !BrushAdjacencySpanValid(
                    brush.firstAdjacentSideOffsets[direction][axis],
                    brush.edgeCount[direction][axis],
                    brush.totalEdgeCount))
            {
                return false;
            }
        }
    }

    for (std::uint32_t sideIndex = 0;
        sideIndex < brush.numsides;
        ++sideIndex)
    {
        const auto &side = brush.sides[sideIndex];
        const auto &plane = brush.planes[sideIndex];
        if (side.plane != &brush.planes[sideIndex]
            || side.materialNum != 0
            || static_cast<std::uint32_t>(side.edgeCount)
                > kMaxBrushWindingEdges
            || !BrushAdjacencySpanValid(
                side.firstAdjacentSideOffset,
                side.edgeCount,
                brush.totalEdgeCount)
            || !FiniteFloatArray(plane.normal, 3)
            || !std::isfinite(plane.dist))
        {
            return false;
        }
    }

    const std::uint32_t sideReferenceLimit = brush.numsides + 6;
    for (std::uint32_t edgeIndex = 0;
        edgeIndex < static_cast<std::uint32_t>(brush.totalEdgeCount);
        ++edgeIndex)
    {
        if (static_cast<std::uint32_t>(
                brush.baseAdjacentSide[edgeIndex]) >= sideReferenceLimit)
            return false;
    }
    return true;
}

constexpr bool PhysGeomListLayoutValid(
    bool hasGeoms,
    std::uint32_t count,
    std::int32_t *geomBytes)
{
    if (geomBytes)
        *geomBytes = 0;
    if (!geomBytes
        || count == 0
        || count
            > static_cast<std::uint32_t>(
                (std::numeric_limits<std::int32_t>::max)())
        || !PointerCountConsistent(hasGeoms, count))
    {
        return false;
    }
    return CheckedArrayBytes(
        static_cast<std::int32_t>(count),
        disk32::kPhysGeomInfoBytes,
        geomBytes);
}

template <typename Mass>
inline bool PhysMassFinite(const Mass &mass)
{
    return FiniteFloatArray(mass.centerOfMass, 3)
        && FiniteFloatArray(mass.momentsOfInertia, 3)
        && FiniteFloatArray(mass.productsOfInertia, 3);
}

template <typename Geom>
inline bool PhysGeomInfoRuntimeValid(const Geom &geom)
{
    if (!FiniteFloatArray(&geom.orientation[0][0], 9)
        || !FiniteFloatArray(geom.offset, 3)
        || !FiniteFloatArray(geom.halfLengths, 3))
    {
        return false;
    }

    switch (geom.type)
    {
    case 0:
        return geom.brush != nullptr
            && BrushWrapperRuntimeValid(*geom.brush);
    case 1:
        return geom.brush == nullptr
            && geom.halfLengths[0] > 0.0f
            && geom.halfLengths[1] > 0.0f
            && geom.halfLengths[2] > 0.0f;
    case 4:
        // The source parser stores half-height then radius in the first two
        // elements and leaves the third zero-initialized.
        return geom.brush == nullptr
            && geom.halfLengths[0] > 0.0f
            && geom.halfLengths[1] > 0.0f;
    default:
        return false;
    }
}

template <typename List>
inline bool PhysGeomListRuntimeValid(const List &list)
{
    std::int32_t geomBytes = 0;
    if (!PhysGeomListLayoutValid(
            list.geoms != nullptr,
            list.count,
            &geomBytes)
        || !PhysMassFinite(list.mass))
    {
        return false;
    }
    for (std::uint32_t index = 0; index < list.count; ++index)
    {
        if (!PhysGeomInfoRuntimeValid(list.geoms[index]))
            return false;
    }
    return true;
}

constexpr bool XModelPiecesLayoutValid(
    bool hasName,
    bool hasPieces,
    std::int32_t count,
    std::int32_t *bytes)
{
    if (bytes)
        *bytes = 0;
    return hasName
        && PointerCountConsistent(hasPieces, count)
        && count <= static_cast<std::int32_t>(UINT16_MAX)
        && CheckedArrayBytes(count, disk32::kXModelPieceBytes, bytes);
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

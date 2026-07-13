#pragma once
//
// Dependency-light validation for the separately encoded BModel records in
// the heterogeneous renderer surface arena.  BModels use the draw-surface
// type as their protocol tag, then publish one placement followed by a native
// array of two-pointer records.  Frontend consumers validate the complete
// sequence; backend draw-list consumers validate one tagged array member.

#include "r_model_surface_stream.h"

#include <cstddef>
#include <cstdint>

namespace gfx::bmodel_surface_stream
{
template <typename Element>
[[nodiscard]] inline bool IsCanonicalArrayElement(
    const Element *const first,
    const std::uint32_t count,
    const Element *const candidate) noexcept
{
    if (!first || !candidate || count == 0u)
        return false;

    const std::uintptr_t firstAddress =
        reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t candidateAddress =
        reinterpret_cast<std::uintptr_t>(candidate);
    if (candidateAddress < firstAddress)
        return false;

    const std::uintptr_t byteOffset = candidateAddress - firstAddress;
    return (byteOffset % sizeof(Element)) == 0u
        && byteOffset / sizeof(Element) < count;
}

template <typename Record, typename Placement, typename Surface>
[[nodiscard]] inline bool IsRecordValid(
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const Record *const record,
    const Surface *const worldSurfaces,
    const std::uint32_t worldSurfaceCount) noexcept
{
    if (!record)
        return false;

    const void *placementRange = nullptr;
    if (!model_surface_stream::TryResolveArenaRange(
            arenaBegin,
            capacity,
            publishedBytes,
            record->placement,
            static_cast<std::uint32_t>(sizeof(Placement)),
            static_cast<std::uint32_t>(alignof(Placement)),
            &placementRange))
    {
        return false;
    }

    const std::uintptr_t placementAddress =
        reinterpret_cast<std::uintptr_t>(placementRange);
    const std::uintptr_t recordAddress =
        reinterpret_cast<std::uintptr_t>(record);
    if (recordAddress < placementAddress)
        return false;

    const std::uintptr_t ownerOffset = recordAddress - placementAddress;
    return ownerOffset >= sizeof(Placement)
        && (ownerOffset - sizeof(Placement)) % sizeof(Record) == 0u
        && IsCanonicalArrayElement(
            worldSurfaces,
            worldSurfaceCount,
            record->surf);
}

template <typename Record, typename Placement, typename Surface>
[[nodiscard]] inline bool TryResolveTaggedRecord(
    const bool hasBModelTypeTag,
    const void *const wordOffsetBase,
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const std::uint32_t dwordOffset,
    const Surface *const worldSurfaces,
    const std::uint32_t worldSurfaceCount,
    const Record **const record) noexcept
{
    if (!hasBModelTypeTag || !record)
        return false;

    const Record *resolved = nullptr;
    if (!model_surface_stream::TryResolveTypedWordOffsetExtent(
            wordOffsetBase,
            arenaBegin,
            capacity,
            publishedBytes,
            dwordOffset,
            &resolved)
        || !IsRecordValid<Record, Placement, Surface>(
            arenaBegin,
            capacity,
            publishedBytes,
            resolved,
            worldSurfaces,
            worldSurfaceCount))
    {
        return false;
    }

    *record = resolved;
    return true;
}

template <typename Record, typename Placement, typename Surface>
[[nodiscard]] inline bool TryResolveSequence(
    const void *const wordOffsetBase,
    const void *const arenaBegin,
    const std::uint32_t capacity,
    const std::uint32_t publishedBytes,
    const std::uint32_t firstDwordOffset,
    const std::uint32_t recordCount,
    const Surface *const worldSurfaces,
    const std::uint32_t worldSurfaceCount,
    const Record **const records) noexcept
{
    if (!records || recordCount == 0u)
        return false;

    std::uint32_t sequenceBytes = 0u;
    if (!model_surface_stream::TryMultiply(
            recordCount,
            static_cast<std::uint32_t>(sizeof(Record)),
            &sequenceBytes))
    {
        return false;
    }

    const void *resolvedRange = nullptr;
    if (!model_surface_stream::TryResolveWordOffsetExtent(
            wordOffsetBase,
            arenaBegin,
            capacity,
            publishedBytes,
            firstDwordOffset,
            sequenceBytes,
            static_cast<std::uint32_t>(alignof(Record)),
            &resolvedRange))
    {
        return false;
    }

    const Record *const resolved = static_cast<const Record *>(resolvedRange);
    const Placement *const sequencePlacement = resolved[0].placement;
    const std::uintptr_t firstRecordAddress =
        reinterpret_cast<std::uintptr_t>(resolved);
    const std::uintptr_t placementAddress =
        reinterpret_cast<std::uintptr_t>(sequencePlacement);
    if (firstRecordAddress < placementAddress
        || firstRecordAddress - placementAddress != sizeof(Placement))
    {
        return false;
    }

    for (std::uint32_t index = 0u; index < recordCount; ++index)
    {
        if (resolved[index].placement != sequencePlacement
            || !IsRecordValid<Record, Placement, Surface>(
                arenaBegin,
                capacity,
                publishedBytes,
                &resolved[index],
                worldSurfaces,
                worldSurfaceCount))
        {
            return false;
        }
    }

    *records = resolved;
    return true;
}

// A malformed draw surf must still advance the outer renderer loop.  The
// invalid record is at processedCount, so consuming it is bounded by total.
[[nodiscard]] constexpr std::uint32_t InvalidRecordProgress(
    const std::uint32_t processedCount,
    const std::uint32_t totalCount) noexcept
{
    return processedCount < totalCount ? processedCount + 1u : totalCount;
}
} // namespace gfx::bmodel_surface_stream

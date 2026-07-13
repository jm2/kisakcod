#pragma once

#include "fx_runtime.h"
#include "fx_runtime_blob.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>

// Load-object effect names occupy a 64-byte editor field.  Keep the alias
// helper's boundary identical: at most 63 bytes plus the terminating NUL.
constexpr std::size_t FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY = 64;
constexpr std::uint32_t FX_MISSING_EFFECT_ALIAS_MAX_ELEM_DEFS = 256;

struct FxMissingEffectAliasPlan
{
    std::uint32_t totalSize;
    std::uint32_t nameOffset;
    std::uint32_t nameLength;
};

namespace fx_missing_effect_alias_detail
{
[[nodiscard]] inline bool SourceGraphIsValid(
    const FxEffectDef *const source) noexcept
{
    if (!source
        || source->elemDefCountLooping < 0
        || source->elemDefCountOneShot < 0
        || source->elemDefCountEmission < 0)
    {
        return false;
    }

    if (source->totalSize < static_cast<std::int32_t>(sizeof(FxEffectDef)))
        return false;
    const std::size_t sourceSize =
        static_cast<std::size_t>(source->totalSize);
    const std::uintptr_t sourceBegin =
        reinterpret_cast<std::uintptr_t>(source);
    if (sourceSize
        > (std::numeric_limits<std::uintptr_t>::max)() - sourceBegin)
    {
        return false;
    }
    const std::uintptr_t sourceEnd = sourceBegin + sourceSize;

    const std::uint64_t elemDefCount =
        static_cast<std::uint64_t>(source->elemDefCountLooping)
        + static_cast<std::uint64_t>(source->elemDefCountOneShot)
        + static_cast<std::uint64_t>(source->elemDefCountEmission);
    if (elemDefCount > FX_MISSING_EFFECT_ALIAS_MAX_ELEM_DEFS)
        return false;
    if (elemDefCount == 0)
        return true;
    if (!source->elemDefs)
        return false;

    const std::uintptr_t elemDefs =
        reinterpret_cast<std::uintptr_t>(source->elemDefs);
    const std::uint64_t elemDefBytes =
        elemDefCount * FX_ELEM_DEF_RUNTIME_SIZE;
    return elemDefs % FX_ELEM_DEF_RUNTIME_ALIGNMENT == 0
        && elemDefs >= sourceBegin + sizeof(FxEffectDef)
        && elemDefs <= sourceEnd
        && elemDefBytes <= sourceEnd - elemDefs;
}

[[nodiscard]] inline bool TryGetNameLength(
    const char *const name,
    std::size_t *const outLength) noexcept
{
    if (!name || !outLength || name[0] == '\0')
        return false;

    for (std::size_t length = 1;
         length < FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY;
         ++length)
    {
        if (name[length] == '\0')
        {
            *outLength = length;
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool ReserveLayout(
    FxRuntimeBlobCursor *const cursor,
    const std::size_t nameLength,
    FxEffectDef **const outEffect = nullptr,
    char **const outName = nullptr) noexcept
{
    if (!cursor
        || nameLength >= FX_MISSING_EFFECT_ALIAS_NAME_CAPACITY
        || nameLength == (std::numeric_limits<std::size_t>::max)())
    {
        return false;
    }

    FxEffectDef *effectStorage = nullptr;
    void *nameStorage = nullptr;
    if (!cursor->ReserveArray<FxEffectDef>(1, &effectStorage)
        || !cursor->ReserveBytes(
            nameLength + 1, alignof(char), &nameStorage))
    {
        return false;
    }

    if (outEffect)
        *outEffect = effectStorage;
    if (outName)
        *outName = static_cast<char *>(nameStorage);
    return true;
}

[[nodiscard]] inline bool TryRangeEnd(
    const std::uintptr_t begin,
    const std::size_t size,
    std::uintptr_t *const outEnd) noexcept
{
    if (!outEnd
        || size > (std::numeric_limits<std::uintptr_t>::max)() - begin)
    {
        return false;
    }
    *outEnd = begin + size;
    return true;
}

[[nodiscard]] inline bool RangesOverlapOrOverflow(
    const void *const first,
    const std::size_t firstSize,
    const void *const second,
    const std::size_t secondSize) noexcept
{
    const std::uintptr_t firstBegin =
        reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t secondBegin =
        reinterpret_cast<std::uintptr_t>(second);
    std::uintptr_t firstEnd = 0;
    std::uintptr_t secondEnd = 0;
    if (!TryRangeEnd(firstBegin, firstSize, &firstEnd)
        || !TryRangeEnd(secondBegin, secondSize, &secondEnd))
    {
        return true;
    }
    return firstBegin < secondEnd && secondBegin < firstEnd;
}
}

// Plans a compact native runtime alias.  The returned size covers exactly one
// FxEffectDef followed by an owned copy of requestedName.
[[nodiscard]] inline bool FX_TryPlanMissingEffectAlias(
    const FxEffectDef *const source,
    const char *const requestedName,
    FxMissingEffectAliasPlan *const outPlan) noexcept
{
    if (!outPlan
        || !fx_missing_effect_alias_detail::SourceGraphIsValid(source))
        return false;

    std::size_t nameLength = 0;
    if (!fx_missing_effect_alias_detail::TryGetNameLength(
            requestedName, &nameLength))
    {
        return false;
    }

    FxRuntimeBlobCursor planner;
    FxEffectDef *ignoredEffect = nullptr;
    char *ignoredName = nullptr;
    if (!fx_missing_effect_alias_detail::ReserveLayout(
            &planner, nameLength, &ignoredEffect, &ignoredName))
    {
        return false;
    }

    std::size_t nameOffset = 0;
    if (!FxRuntimeBlobTryAlignOffset(
            sizeof(FxEffectDef), alignof(char), &nameOffset)
        || nameOffset > (std::numeric_limits<std::uint32_t>::max)()
        || nameLength > (std::numeric_limits<std::uint32_t>::max)())
    {
        return false;
    }

    const FxMissingEffectAliasPlan plan{
        planner.Offset(),
        static_cast<std::uint32_t>(nameOffset),
        static_cast<std::uint32_t>(nameLength),
    };
    if (plan.totalSize > static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)()))
    {
        return false;
    }

    *outPlan = plan;
    return true;
}

// Builds the alias transactionally: every argument, bound, alignment, and
// overlap check completes before storage or outAlias is changed.  The alias
// owns only its requested name.  Its immutable element definitions remain
// shared with source rather than being copied or relocated.
[[nodiscard]] inline bool FX_TryBuildMissingEffectAlias(
    const FxEffectDef *const source,
    const char *const requestedName,
    void *const storage,
    const std::size_t capacity,
    FxEffectDef **const outAlias) noexcept
{
    static_assert(std::is_trivially_copy_constructible_v<FxEffectDef>);

    if (!storage || !outAlias
        || reinterpret_cast<std::uintptr_t>(storage) % alignof(FxEffectDef) != 0)
    {
        return false;
    }

    FxMissingEffectAliasPlan plan{};
    if (!FX_TryPlanMissingEffectAlias(source, requestedName, &plan)
        || capacity < plan.totalSize)
    {
        return false;
    }

    // Reject in-place source/name aliases.  This both preserves the immutable
    // source contract and prevents header publication from corrupting a name
    // that has not yet been copied.
    if (fx_missing_effect_alias_detail::RangesOverlapOrOverflow(
            storage,
            plan.totalSize,
            source,
            static_cast<std::size_t>(source->totalSize))
        || fx_missing_effect_alias_detail::RangesOverlapOrOverflow(
            storage,
            plan.totalSize,
            requestedName,
            static_cast<std::size_t>(plan.nameLength) + 1)
        || fx_missing_effect_alias_detail::RangesOverlapOrOverflow(
            storage, plan.totalSize, outAlias, sizeof(*outAlias))
        || fx_missing_effect_alias_detail::RangesOverlapOrOverflow(
            outAlias,
            sizeof(*outAlias),
            source,
            static_cast<std::size_t>(source->totalSize))
        || fx_missing_effect_alias_detail::RangesOverlapOrOverflow(
            outAlias,
            sizeof(*outAlias),
            requestedName,
            static_cast<std::size_t>(plan.nameLength) + 1))
    {
        return false;
    }

    FxRuntimeBlobCursor writer(
        static_cast<std::uint8_t *>(storage), plan.totalSize);
    FxEffectDef *effectStorage = nullptr;
    char *nameStorage = nullptr;
    if (!fx_missing_effect_alias_detail::ReserveLayout(
            &writer, plan.nameLength, &effectStorage, &nameStorage)
        || writer.Offset() != plan.totalSize
        || reinterpret_cast<std::uint8_t *>(nameStorage)
            != static_cast<std::uint8_t *>(storage) + plan.nameOffset)
    {
        return false;
    }

    FxEffectDef *const alias = ::new (effectStorage) FxEffectDef(*source);
    std::memcpy(
        nameStorage,
        requestedName,
        static_cast<std::size_t>(plan.nameLength) + 1);
    alias->name = nameStorage;
    alias->totalSize = static_cast<std::int32_t>(plan.totalSize);
    *outAlias = alias;
    return true;
}

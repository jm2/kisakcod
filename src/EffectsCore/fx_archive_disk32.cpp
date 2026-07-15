#include <EffectsCore/fx_archive_disk32.h>

#include <EffectsCore/fx_pool.h>

#include <cstddef>
#include <limits>

namespace fx::archive
{
namespace
{
constexpr std::size_t FX_EFFECT_DISK32_BYTES = 0x80;
constexpr std::size_t FX_EFFECT_HANDLE_SCALE = FxEffect::HANDLE_SCALE;
constexpr std::size_t FX_EFFECT_DISK32_HANDLE_STRIDE =
    FX_EFFECT_DISK32_BYTES / FX_EFFECT_HANDLE_SCALE;
constexpr std::size_t FX_EFFECT_NATIVE_HANDLE_STRIDE =
    FxHandleStride<FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>();

static_assert(FX_EFFECT_DISK32_BYTES % FX_EFFECT_HANDLE_SCALE == 0);
static_assert(FX_EFFECT_DISK32_HANDLE_STRIDE == 32);
static_assert(
    (MAX_EFFECTS - 1) * FX_EFFECT_DISK32_HANDLE_STRIDE
    < (std::numeric_limits<std::uint16_t>::max)());
static_assert(
    (MAX_EFFECTS - 1) * FX_EFFECT_NATIVE_HANDLE_STRIDE
    < (std::numeric_limits<std::uint16_t>::max)());

constexpr std::uint32_t FX_BOLT_DOBJ_HANDLE_MASK = 0x00000FFFu;
constexpr std::uint32_t FX_BOLT_TEMPORAL_BITS_MASK = 0x00001000u;
constexpr std::uint32_t FX_BOLT_BONE_INDEX_MASK = 0x00FFE000u;
constexpr std::uint32_t FX_BOLT_SORT_ORDER_MASK = 0xFF000000u;
constexpr unsigned FX_BOLT_TEMPORAL_BITS_SHIFT = 12;
constexpr unsigned FX_BOLT_BONE_INDEX_SHIFT = 13;
constexpr unsigned FX_BOLT_SORT_ORDER_SHIFT = 24;

std::uint32_t PackBoltAndSortOrder(
    const FxBoltAndSortOrder &source) noexcept
{
    return (static_cast<std::uint32_t>(source.dobjHandle)
            & FX_BOLT_DOBJ_HANDLE_MASK)
        | ((static_cast<std::uint32_t>(source.temporalBits)
            << FX_BOLT_TEMPORAL_BITS_SHIFT)
           & FX_BOLT_TEMPORAL_BITS_MASK)
        | ((static_cast<std::uint32_t>(source.boneIndex)
            << FX_BOLT_BONE_INDEX_SHIFT)
           & FX_BOLT_BONE_INDEX_MASK)
        | ((static_cast<std::uint32_t>(source.sortOrder)
            << FX_BOLT_SORT_ORDER_SHIFT)
           & FX_BOLT_SORT_ORDER_MASK);
}

void UnpackBoltAndSortOrder(
    const std::uint32_t source,
    FxBoltAndSortOrder *const destination) noexcept
{
    destination->dobjHandle = source & FX_BOLT_DOBJ_HANDLE_MASK;
    destination->temporalBits =
        (source & FX_BOLT_TEMPORAL_BITS_MASK)
        >> FX_BOLT_TEMPORAL_BITS_SHIFT;
    destination->boneIndex =
        (source & FX_BOLT_BONE_INDEX_MASK) >> FX_BOLT_BONE_INDEX_SHIFT;
    destination->sortOrder =
        (source & FX_BOLT_SORT_ORDER_MASK) >> FX_BOLT_SORT_ORDER_SHIFT;
}

void UnpackSpatialFrame(
    const FxSpatialFrameDisk32 &source,
    FxSpatialFrame *const destination) noexcept
{
    for (std::size_t component = 0; component < 4; ++component)
        destination->quat[component] = source.quat[component];
    for (std::size_t component = 0; component < 3; ++component)
        destination->origin[component] = source.origin[component];
}

void PackSpatialFrame(
    const FxSpatialFrame &source,
    FxSpatialFrameDisk32 *const destination) noexcept
{
    for (std::size_t component = 0; component < 4; ++component)
        destination->quat[component] = source.quat[component];
    for (std::size_t component = 0; component < 3; ++component)
        destination->origin[component] = source.origin[component];
}
} // namespace

bool TryDecodeFxEffectHandleDisk32(
    const std::uint16_t diskHandle,
    std::uint16_t *const outNativeHandle) noexcept
{
    if (!outNativeHandle
        || diskHandle == (std::numeric_limits<std::uint16_t>::max)()
        || diskHandle % FX_EFFECT_DISK32_HANDLE_STRIDE != 0)
    {
        return false;
    }

    const std::size_t effectIndex =
        diskHandle / FX_EFFECT_DISK32_HANDLE_STRIDE;
    if (effectIndex >= MAX_EFFECTS)
        return false;

    const std::size_t nativeHandle =
        effectIndex * FX_EFFECT_NATIVE_HANDLE_STRIDE;
    if (nativeHandle
        >= (std::numeric_limits<std::uint16_t>::max)())
    {
        return false;
    }

    *outNativeHandle = static_cast<std::uint16_t>(nativeHandle);
    return true;
}

bool TryEncodeFxEffectHandleDisk32(
    const std::uint16_t nativeHandle,
    std::uint16_t *const outDiskHandle) noexcept
{
    if (!outDiskHandle
        || nativeHandle == (std::numeric_limits<std::uint16_t>::max)()
        || nativeHandle % FX_EFFECT_NATIVE_HANDLE_STRIDE != 0)
    {
        return false;
    }

    const std::size_t effectIndex =
        nativeHandle / FX_EFFECT_NATIVE_HANDLE_STRIDE;
    if (effectIndex >= MAX_EFFECTS)
        return false;

    const std::size_t diskHandle =
        effectIndex * FX_EFFECT_DISK32_HANDLE_STRIDE;
    if (diskHandle
        >= (std::numeric_limits<std::uint16_t>::max)())
    {
        return false;
    }

    *outDiskHandle = static_cast<std::uint16_t>(diskHandle);
    return true;
}

bool TryUnpackFxEffectDisk32(
    const FxEffectDisk32 &source,
    const bool isActive,
    const FxEffectDef *const resolvedDefinition,
    FxEffect *const outEffect) noexcept
{
    if (!outEffect
        || (isActive
            && (!EffectDefinitionKeyIsValid(source.definitionKey)
                || !resolvedDefinition))
        || (!isActive && resolvedDefinition))
    {
        return false;
    }

    std::uint16_t owner = source.owner;
    if (isActive
        && !TryDecodeFxEffectHandleDisk32(source.owner, &owner))
    {
        return false;
    }

    FxEffect unpacked{};
    unpacked.def = isActive ? resolvedDefinition : nullptr;
    unpacked.status = source.status;
    for (std::size_t index = 0; index < 3; ++index)
        unpacked.firstElemHandle[index] = source.firstElemHandle[index];
    unpacked.firstSortedElemHandle = source.firstSortedElemHandle;
    unpacked.firstTrailHandle = source.firstTrailHandle;
    unpacked.randomSeed = source.randomSeed;
    unpacked.owner = owner;
    unpacked.packedLighting = source.packedLighting;
    UnpackBoltAndSortOrder(
        source.boltAndSortOrder, &unpacked.boltAndSortOrder);
    unpacked.frameCount = source.frameCount;
    unpacked.msecBegin = source.msecBegin;
    unpacked.msecLastUpdate = source.msecLastUpdate;
    UnpackSpatialFrame(source.frameAtSpawn, &unpacked.frameAtSpawn);
    UnpackSpatialFrame(source.frameNow, &unpacked.frameNow);
    UnpackSpatialFrame(source.framePrev, &unpacked.framePrev);
    unpacked.distanceTraveled = source.distanceTraveled;

    *outEffect = unpacked;
    return true;
}

bool TryPackFxEffectDisk32(
    const FxEffect &source,
    const bool isActive,
    const EffectDefinitionKey32 definitionKey,
    FxEffectDisk32 *const outEffect) noexcept
{
    if (!outEffect
        || (isActive
            && (!source.def
                || !EffectDefinitionKeyIsValid(definitionKey))))
    {
        return false;
    }

    std::uint16_t owner = source.owner;
    if (isActive
        && !TryEncodeFxEffectHandleDisk32(source.owner, &owner))
    {
        return false;
    }

    FxEffectDisk32 packed{};
    packed.definitionKey = definitionKey;
    packed.status = source.status;
    for (std::size_t index = 0; index < 3; ++index)
        packed.firstElemHandle[index] = source.firstElemHandle[index];
    packed.firstSortedElemHandle = source.firstSortedElemHandle;
    packed.firstTrailHandle = source.firstTrailHandle;
    packed.randomSeed = source.randomSeed;
    packed.owner = owner;
    packed.packedLighting = source.packedLighting;
    packed.boltAndSortOrder = PackBoltAndSortOrder(source.boltAndSortOrder);
    packed.frameCount = source.frameCount;
    packed.msecBegin = source.msecBegin;
    packed.msecLastUpdate = source.msecLastUpdate;
    PackSpatialFrame(source.frameAtSpawn, &packed.frameAtSpawn);
    PackSpatialFrame(source.frameNow, &packed.frameNow);
    PackSpatialFrame(source.framePrev, &packed.framePrev);
    packed.distanceTraveled = source.distanceTraveled;

    *outEffect = packed;
    return true;
}
} // namespace fx::archive

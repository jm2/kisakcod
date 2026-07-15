#include <EffectsCore/fx_archive_system_disk32.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fx::archive
{
namespace
{
constexpr std::uint32_t FX_BUFFER_ELEMS_OFFSET = 0x20000u;
constexpr std::uint32_t FX_BUFFER_TRAILS_OFFSET = 0x34000u;
constexpr std::uint32_t FX_BUFFER_TRAIL_ELEMS_OFFSET = 0x34400u;
constexpr std::uint32_t FX_BUFFER_VIS_STATE_OFFSET = 0x44400u;
constexpr std::uint32_t FX_BUFFER_DEFERRED_ELEMS_OFFSET = 0x46420u;
constexpr std::uint32_t FX_BUFFER_DISK32_SIZE = 0x47480u;
constexpr std::uint32_t FX_VIS_STATE_DISK32_SIZE = 0x1010u;
constexpr std::uint32_t FX_EFFECT_DISK32_HANDLE_STRIDE = 0x20u;
constexpr std::uint32_t FX_EFFECT_NATIVE_HANDLE_STRIDE =
    static_cast<std::uint32_t>(sizeof(FxEffect) / FxEffect::HANDLE_SCALE);
constexpr std::uint32_t FX_ELEM_DISK32_HANDLE_STRIDE = 0x0Au;
constexpr std::uint32_t FX_ELEM_DISK32_HANDLE_LIMIT =
    static_cast<std::uint32_t>(MAX_ELEMS)
    * FX_ELEM_DISK32_HANDLE_STRIDE;

constexpr std::int64_t FX_ARCHIVE_DURATION_LIMIT_MSEC =
    24ll * 60ll * 60ll * 1000ll;
constexpr std::int64_t FX_ARCHIVE_TIME_HEADROOM_MSEC =
    4ll * FX_ARCHIVE_DURATION_LIMIT_MSEC;
constexpr float FX_ARCHIVE_SPATIAL_COMPONENT_MAX = 1048576.0f;
constexpr float FX_ARCHIVE_DISTANCE_MAX = 16777216.0f;
constexpr double FX_ARCHIVE_UNIT_LENGTH_TOLERANCE = 0.025;
constexpr double FX_ARCHIVE_ORTHOGONAL_TOLERANCE = 0.025;

bool ArchiveFloatIsBounded(
    const float value,
    const float magnitudeLimit) noexcept
{
    return std::isfinite(value)
        && value >= -magnitudeLimit
        && value <= magnitudeLimit;
}

template <std::size_t COUNT>
bool ValidateArchiveVector(
    const float (&values)[COUNT],
    const float magnitudeLimit) noexcept
{
    for (const float value : values)
    {
        if (!ArchiveFloatIsBounded(value, magnitudeLimit))
            return false;
    }
    return true;
}

bool ValidateArchiveOrthonormalBasis(
    const float (&basis)[3][3]) noexcept
{
    for (const auto &row : basis)
    {
        if (!ValidateArchiveVector(row, 1.001f))
            return false;

        double lengthSquared = 0.0;
        for (const float value : row)
            lengthSquared += static_cast<double>(value) * value;
        if (lengthSquared < 1.0 - FX_ARCHIVE_UNIT_LENGTH_TOLERANCE
            || lengthSquared > 1.0 + FX_ARCHIVE_UNIT_LENGTH_TOLERANCE)
        {
            return false;
        }
    }

    for (std::size_t first = 0; first < 3; ++first)
    {
        for (std::size_t second = first + 1; second < 3; ++second)
        {
            double dot = 0.0;
            for (std::size_t component = 0; component < 3; ++component)
            {
                dot += static_cast<double>(basis[first][component])
                    * basis[second][component];
            }
            if (dot < -FX_ARCHIVE_ORTHOGONAL_TOLERANCE
                || dot > FX_ARCHIVE_ORTHOGONAL_TOLERANCE)
            {
                return false;
            }
        }
    }

    const double determinant =
        static_cast<double>(basis[0][0])
            * (static_cast<double>(basis[1][1]) * basis[2][2]
                - static_cast<double>(basis[1][2]) * basis[2][1])
        - static_cast<double>(basis[0][1])
            * (static_cast<double>(basis[1][0]) * basis[2][2]
                - static_cast<double>(basis[1][2]) * basis[2][0])
        + static_cast<double>(basis[0][2])
            * (static_cast<double>(basis[1][0]) * basis[2][1]
                - static_cast<double>(basis[1][1]) * basis[2][0]);
    return determinant >= 1.0 - 3.0 * FX_ARCHIVE_UNIT_LENGTH_TOLERANCE
        && determinant <= 1.0 + 3.0 * FX_ARCHIVE_UNIT_LENGTH_TOLERANCE;
}

bool IsCanonicalInvalidCamera(const FxCameraDisk32 &camera) noexcept
{
    if (camera.isValid != 0 || camera.frustumPlaneCount != 0)
        return false;
    for (const float value : camera.origin)
    {
        if (value != 0.0f)
            return false;
    }
    for (const auto &plane : camera.frustum)
    {
        for (const float value : plane)
        {
            if (value != 0.0f)
                return false;
        }
    }
    for (const auto &row : camera.axis)
    {
        for (const float value : row)
        {
            if (value != 0.0f)
                return false;
        }
    }
    for (const float value : camera.viewOffset)
    {
        if (value != 0.0f)
            return false;
    }
    for (const std::uint32_t value : camera.pad)
    {
        if (value != 0)
            return false;
    }
    return true;
}

bool ArchiveCamerasAreReady(
    const FxCameraDisk32 &camera,
    const FxCameraDisk32 &previousCamera,
    const std::int32_t msecDraw) noexcept
{
    if (msecDraw >= 0)
        return camera.isValid == 1 && previousCamera.isValid == 1;
    if (msecDraw != -1)
        return false;
    return (camera.isValid == 1 || IsCanonicalInvalidCamera(camera))
        && (previousCamera.isValid == 1
            || IsCanonicalInvalidCamera(previousCamera));
}

bool ValidateArchiveCamera(const FxCameraDisk32 &camera) noexcept
{
    if ((camera.isValid != 0 && camera.isValid != 1)
        || camera.frustumPlaneCount > 6
        || (camera.isValid == 0 && camera.frustumPlaneCount != 0)
        || !ValidateArchiveVector(
            camera.origin, FX_ARCHIVE_SPATIAL_COMPONENT_MAX))
    {
        return false;
    }

    for (std::size_t planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        const float (&plane)[4] = camera.frustum[planeIndex];
        if (!ArchiveFloatIsBounded(plane[0], 2.0f)
            || !ArchiveFloatIsBounded(plane[1], 2.0f)
            || !ArchiveFloatIsBounded(plane[2], 2.0f)
            || !ArchiveFloatIsBounded(
                plane[3], FX_ARCHIVE_DISTANCE_MAX))
        {
            return false;
        }
        if (planeIndex < camera.frustumPlaneCount)
        {
            const double normalLengthSquared =
                static_cast<double>(plane[0]) * plane[0]
                + static_cast<double>(plane[1]) * plane[1]
                + static_cast<double>(plane[2]) * plane[2];
            if (normalLengthSquared
                    < 1.0 - FX_ARCHIVE_UNIT_LENGTH_TOLERANCE
                || normalLengthSquared
                    > 1.0 + FX_ARCHIVE_UNIT_LENGTH_TOLERANCE)
            {
                return false;
            }
        }
    }

    if (camera.isValid != 0 && camera.frustumPlaneCount != 0)
    {
        if (!ValidateArchiveOrthonormalBasis(camera.axis))
            return false;
    }
    else
    {
        for (const auto &row : camera.axis)
        {
            if (!ValidateArchiveVector(row, 1.001f))
                return false;
        }
    }
    return ValidateArchiveVector(
        camera.viewOffset, FX_ARCHIVE_SPATIAL_COMPONENT_MAX);
}

bool ArchiveTimeDifferenceFits(
    const std::int32_t lhs,
    const std::int32_t rhs) noexcept
{
    const std::int64_t minimumTimestamp =
        static_cast<std::int64_t>(
            (std::numeric_limits<std::int32_t>::min)())
        + FX_ARCHIVE_TIME_HEADROOM_MSEC;
    const std::int64_t maximumTimestamp =
        static_cast<std::int64_t>(
            (std::numeric_limits<std::int32_t>::max)())
        - FX_ARCHIVE_TIME_HEADROOM_MSEC;
    if (lhs < minimumTimestamp || lhs > maximumTimestamp
        || rhs < minimumTimestamp || rhs > maximumTimestamp)
    {
        return false;
    }

    const std::int64_t difference =
        static_cast<std::int64_t>(lhs) - rhs;
    return difference
            >= static_cast<std::int64_t>(
                (std::numeric_limits<std::int32_t>::min)())
                + FX_ARCHIVE_TIME_HEADROOM_MSEC
        && difference
            <= static_cast<std::int64_t>(
                (std::numeric_limits<std::int32_t>::max)())
                - FX_ARCHIVE_TIME_HEADROOM_MSEC;
}

bool TryAddArchiveAddress(
    const ArchiveAddress32 base,
    const std::uint32_t offset,
    std::uint32_t *const outAddress) noexcept
{
    if (!outAddress
        || base.value
            > (std::numeric_limits<std::uint32_t>::max)() - offset)
    {
        return false;
    }
    *outAddress = base.value + offset;
    return true;
}

bool ValidateBufferTopologyAndDeriveVisibility(
    const FxSystemDisk32 &source,
    std::uint8_t *const outReadSelector,
    std::uint8_t *const outWriteSelector) noexcept
{
    if (!outReadSelector || !outWriteSelector
        || outReadSelector == outWriteSelector
        || source.effects.value == 0
        || (source.effects.value & 0x3u) != 0
        || source.effects.value
            > (std::numeric_limits<std::uint32_t>::max)()
                - (FX_BUFFER_DISK32_SIZE - 1u))
    {
        return false;
    }

    std::uint32_t expectedElems = 0;
    std::uint32_t expectedTrails = 0;
    std::uint32_t expectedTrailElems = 0;
    std::uint32_t expectedVisState = 0;
    std::uint32_t expectedDeferredElems = 0;
    std::uint32_t secondVisState = 0;
    if (!TryAddArchiveAddress(
            source.effects, FX_BUFFER_ELEMS_OFFSET, &expectedElems)
        || !TryAddArchiveAddress(
            source.effects, FX_BUFFER_TRAILS_OFFSET, &expectedTrails)
        || !TryAddArchiveAddress(
            source.effects,
            FX_BUFFER_TRAIL_ELEMS_OFFSET,
            &expectedTrailElems)
        || !TryAddArchiveAddress(
            source.effects,
            FX_BUFFER_VIS_STATE_OFFSET,
            &expectedVisState)
        || !TryAddArchiveAddress(
            source.effects,
            FX_BUFFER_DEFERRED_ELEMS_OFFSET,
            &expectedDeferredElems)
        || source.elems.value != expectedElems
        || source.trails.value != expectedTrails
        || source.trailElems.value != expectedTrailElems
        || source.visState.value != expectedVisState
        || source.deferredElems.value != expectedDeferredElems
        || !TryAddArchiveAddress(
            source.visState,
            FX_VIS_STATE_DISK32_SIZE,
            &secondVisState))
    {
        return false;
    }

    std::uint8_t readSelector = 0;
    if (source.visStateBufferRead.value == secondVisState)
        readSelector = 1;
    else if (source.visStateBufferRead.value != source.visState.value)
        return false;

    std::uint8_t writeSelector = 0;
    if (source.visStateBufferWrite.value == secondVisState)
        writeSelector = 1;
    else if (source.visStateBufferWrite.value != source.visState.value)
        return false;

    if (readSelector == writeSelector)
        return false;
    *outReadSelector = readSelector;
    *outWriteSelector = writeSelector;
    return true;
}

template <std::size_t LIMIT>
bool PoolCountAndFreeHeadAreValid(
    const std::int32_t firstFree,
    const std::int32_t activeCount) noexcept
{
    if (activeCount < 0
        || static_cast<std::uint32_t>(activeCount) > LIMIT
        || firstFree < -1
        || (firstFree != -1
            && static_cast<std::uint32_t>(firstFree) >= LIMIT))
    {
        return false;
    }

    const bool poolIsFull =
        static_cast<std::uint32_t>(activeCount) == LIMIT;
    return (firstFree == -1) == poolIsFull;
}

bool EffectRingIsValid(
    const FxSystemDisk32 &source,
    std::int32_t *const outActiveEffectCount) noexcept
{
    if (!outActiveEffectCount || source.firstActiveEffect < 0
        || source.firstNewEffect != source.firstFreeEffect)
    {
        return false;
    }

    const std::int64_t activeEffectCount =
        static_cast<std::int64_t>(source.firstFreeEffect)
        - source.firstActiveEffect;
    if (activeEffectCount < 0
        || activeEffectCount > static_cast<std::int64_t>(MAX_EFFECTS))
    {
        return false;
    }
    *outActiveEffectCount =
        static_cast<std::int32_t>(activeEffectCount);
    return true;
}

bool Disk32ElemHandleIsValid(const std::uint16_t handle) noexcept
{
    return handle != (std::numeric_limits<std::uint16_t>::max)()
        && handle < FX_ELEM_DISK32_HANDLE_LIMIT
        && handle % FX_ELEM_DISK32_HANDLE_STRIDE == 0;
}

void UnpackCamera(
    const FxCameraDisk32 &source,
    FxCamera *const destination) noexcept
{
    for (std::size_t component = 0; component < 3; ++component)
        destination->origin[component] = source.origin[component];
    destination->isValid = source.isValid;
    for (std::size_t plane = 0; plane < 6; ++plane)
    {
        for (std::size_t component = 0; component < 4; ++component)
        {
            destination->frustum[plane][component] =
                source.frustum[plane][component];
        }
    }
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t component = 0; component < 3; ++component)
            destination->axis[row][component] = source.axis[row][component];
    }
    destination->frustumPlaneCount = source.frustumPlaneCount;
    for (std::size_t component = 0; component < 3; ++component)
    {
        destination->viewOffset[component] = source.viewOffset[component];
        destination->pad[component] = source.pad[component];
    }
}
} // namespace

bool TryUnpackFxSystemDisk32(
    const FxSystemDisk32 &source,
    FxSystem *const outSystem,
    FxSystemDisk32Metadata *const outMetadata) noexcept
{
    if (!outSystem || !outMetadata
        || source.isInitialized != 1
        || source.needsGarbageCollection > 1
        || source.isArchiving != 1
        || source.localClientNum != 0
        || source.iteratorCount != 0
        || source.deferredElemCount != 0
        || source.sprite.indexCount != 0
        || source.msecNow < 0
        || source.msecDraw < -1
        || source.gfxCloudCount < 0
        || source.gfxCloudCount > 256
        || !PoolCountAndFreeHeadAreValid<MAX_ELEMS>(
            source.firstFreeElem, source.activeElemCount)
        || !PoolCountAndFreeHeadAreValid<MAX_TRAIL_ELEMS>(
            source.firstFreeTrailElem,
            source.activeTrailElemCount)
        || !PoolCountAndFreeHeadAreValid<MAX_TRAILS>(
            source.firstFreeTrail, source.activeTrailCount)
        || !ArchiveTimeDifferenceFits(source.msecNow, source.msecDraw)
        || !ValidateArchiveCamera(source.camera)
        || !ValidateArchiveCamera(source.cameraPrev)
        || !ArchiveCamerasAreReady(
            source.camera, source.cameraPrev, source.msecDraw))
    {
        return false;
    }

    FxSystemDisk32Metadata metadata{};
    if (!ValidateBufferTopologyAndDeriveVisibility(
            source,
            &metadata.readVisibilitySelector,
            &metadata.writeVisibilitySelector))
    {
        return false;
    }

    std::int32_t activeEffectCount = 0;
    if (!EffectRingIsValid(source, &activeEffectCount))
        return false;

    const std::int32_t normalizedFirstActive =
        source.firstActiveEffect
        & static_cast<std::int32_t>(MAX_EFFECTS - 1);

    // Use the metadata bytes as a temporary permutation set.  The complete
    // inventory is proven before the array is cleared and repurposed as the
    // active-slot bitmap promised to the caller.
    for (std::size_t handleIndex = 0;
         handleIndex < MAX_EFFECTS;
         ++handleIndex)
    {
        const std::uint16_t diskHandle =
            source.allEffectHandles[handleIndex];
        std::uint16_t nativeHandle = 0;
        if (!TryDecodeFxEffectHandleDisk32(
                diskHandle, &nativeHandle))
        {
            return false;
        }
        const std::size_t effectIndex =
            static_cast<std::size_t>(diskHandle)
            / FX_EFFECT_DISK32_HANDLE_STRIDE;
        if (metadata.activeEffectSlots[effectIndex] != 0)
            return false;
        metadata.activeEffectSlots[effectIndex] = 1;
    }
    for (std::uint8_t &active : metadata.activeEffectSlots)
        active = 0;

    for (std::int64_t activeIndex = source.firstActiveEffect;
         activeIndex < source.firstFreeEffect;
         ++activeIndex)
    {
        const std::size_t ringIndex =
            static_cast<std::size_t>(activeIndex)
            & (MAX_EFFECTS - 1);
        const std::size_t effectIndex =
            static_cast<std::size_t>(
                source.allEffectHandles[ringIndex])
            / FX_EFFECT_DISK32_HANDLE_STRIDE;
        if (metadata.activeEffectSlots[effectIndex] != 0)
            return false;
        metadata.activeEffectSlots[effectIndex] = 1;
    }

    if (source.activeSpotLightEffectCount < 0
        || source.activeSpotLightEffectCount > 1
        || source.activeSpotLightElemCount < 0
        || source.activeSpotLightElemCount > 1
        || (source.activeSpotLightEffectCount == 0
            && source.activeSpotLightElemCount != 0))
    {
        return false;
    }

    std::uint16_t nativeSpotLightEffectHandle =
        (std::numeric_limits<std::uint16_t>::max)();
    std::uint16_t nativeSpotLightElemHandle =
        (std::numeric_limits<std::uint16_t>::max)();
    std::int16_t nativeSpotLightBoltDobj = -1;
    if (source.activeSpotLightEffectCount == 1)
    {
        if (!TryDecodeFxEffectHandleDisk32(
                source.activeSpotLightEffectHandle,
                &nativeSpotLightEffectHandle))
        {
            return false;
        }
        const std::size_t spotLightEffectIndex =
            static_cast<std::size_t>(
                source.activeSpotLightEffectHandle)
            / FX_EFFECT_DISK32_HANDLE_STRIDE;
        if (metadata.activeEffectSlots[spotLightEffectIndex] == 0)
            return false;
        nativeSpotLightBoltDobj = source.activeSpotLightBoltDobj;
    }
    if (source.activeSpotLightElemCount == 1)
    {
        if (!Disk32ElemHandleIsValid(
                source.activeSpotLightElemHandle))
        {
            return false;
        }
        nativeSpotLightElemHandle = source.activeSpotLightElemHandle;
    }

    std::int32_t nativeFrameCount = source.frameCount;
    if (nativeFrameCount <= 0
        || nativeFrameCount
            >= (std::numeric_limits<std::int32_t>::max)() - 1)
    {
        nativeFrameCount = 1;
    }

    // No operation below can fail.  Commit directly to the caller only after
    // the complete validation and conversion pass so the helper does not need
    // a second full native FxSystem on its stack.
    UnpackCamera(source.camera, &outSystem->camera);
    UnpackCamera(source.cameraPrev, &outSystem->cameraPrev);
    outSystem->sprite.indices = nullptr;
    outSystem->sprite.indexCount = 0;
    outSystem->sprite.material = nullptr;
    outSystem->sprite.name = nullptr;
    outSystem->effects = nullptr;
    outSystem->elems = nullptr;
    outSystem->trails = nullptr;
    outSystem->trailElems = nullptr;
    outSystem->deferredElems = nullptr;
    outSystem->firstFreeElem = source.firstFreeElem;
    outSystem->firstFreeTrailElem = source.firstFreeTrailElem;
    outSystem->firstFreeTrail = source.firstFreeTrail;
    outSystem->deferredElemCount = source.deferredElemCount;
    outSystem->activeElemCount = source.activeElemCount;
    outSystem->activeTrailElemCount = source.activeTrailElemCount;
    outSystem->activeTrailCount = source.activeTrailCount;
    outSystem->gfxCloudCount = source.gfxCloudCount;
    outSystem->visState = nullptr;
    outSystem->visStateBufferRead = nullptr;
    outSystem->visStateBufferWrite = nullptr;
    outSystem->firstActiveEffect = normalizedFirstActive;
    outSystem->firstNewEffect = normalizedFirstActive + activeEffectCount;
    outSystem->firstFreeEffect = normalizedFirstActive + activeEffectCount;
    for (std::size_t handleIndex = 0;
         handleIndex < MAX_EFFECTS;
         ++handleIndex)
    {
        const std::uint32_t effectIndex =
            static_cast<std::uint32_t>(
                source.allEffectHandles[handleIndex])
            / FX_EFFECT_DISK32_HANDLE_STRIDE;
        outSystem->allEffectHandles[handleIndex] =
            static_cast<std::uint16_t>(
                effectIndex * FX_EFFECT_NATIVE_HANDLE_STRIDE);
    }
    outSystem->activeSpotLightEffectCount =
        source.activeSpotLightEffectCount;
    outSystem->activeSpotLightElemCount =
        source.activeSpotLightElemCount;
    outSystem->activeSpotLightEffectHandle =
        nativeSpotLightEffectHandle;
    outSystem->activeSpotLightElemHandle = nativeSpotLightElemHandle;
    outSystem->activeSpotLightBoltDobj = nativeSpotLightBoltDobj;
    outSystem->iteratorCount = source.iteratorCount;
    outSystem->msecNow = source.msecNow;
    outSystem->msecDraw = source.msecDraw;
    outSystem->frameCount = nativeFrameCount;
    outSystem->isInitialized = true;
    outSystem->needsGarbageCollection =
        source.needsGarbageCollection != 0;
    outSystem->isArchiving = true;
    outSystem->localClientNum = source.localClientNum;
    for (std::size_t index = 0; index < 32; ++index)
        outSystem->restartList[index] = source.restartList[index];

    *outMetadata = metadata;
    return true;
}
} // namespace fx::archive

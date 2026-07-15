#include <EffectsCore/fx_archive_semantics.h>

#include <EffectsCore/fx_physics_sidecar.h>
#include <EffectsCore/fx_pool.h>
#include <EffectsCore/fx_runtime.h>
#include <EffectsCore/fx_snapshot_publication.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// The legacy table currently has no platform-neutral declaration header.
// Keep this translation unit independent of fx_system.h: that header also
// exposes renderer state and consequently pulls Direct3D into portable tools.
extern const float fx_randomTable[507];

namespace fx::archive
{
namespace
{
constexpr std::uint16_t FX_ARCHIVE_INVALID_HANDLE =
    (std::numeric_limits<std::uint16_t>::max)();
constexpr std::uint32_t FX_ARCHIVE_PHYSICS_FLAG = 0x08000000u;
constexpr std::uint32_t FX_ARCHIVE_MAX_GENTITIES = 1024u;
constexpr std::uint32_t FX_ARCHIVE_CLIENT_DOBJ_HANDLE_MAX =
    FX_ARCHIVE_MAX_GENTITIES + 128u;
constexpr std::uint32_t FX_ARCHIVE_DOBJ_HANDLE_NONE = 4095u;
constexpr std::uint32_t FX_ARCHIVE_BONE_INDEX_NONE = 2047u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_SPRITE_BILLBOARD = 0u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_SPRITE_ORIENTED = 1u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_TAIL = 2u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_TRAIL = 3u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_CLOUD = 4u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_MODEL = 5u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_OMNI_LIGHT = 6u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_SPOT_LIGHT = 7u;
constexpr std::uint8_t FX_ARCHIVE_ELEM_TYPE_COUNT = 11u;
constexpr std::size_t FX_ARCHIVE_ELEM_REPRESENTATION_SIZE = 0x28u;
constexpr std::int64_t FX_ARCHIVE_DURATION_LIMIT_MSEC =
    24ll * 60ll * 60ll * 1000ll;
constexpr std::int64_t FX_ARCHIVE_TIME_HEADROOM_MSEC =
    4ll * FX_ARCHIVE_DURATION_LIMIT_MSEC;
constexpr std::int32_t FX_ARCHIVE_RANDOM_RANGE_AMPLITUDE_MAX = 32767;
constexpr float FX_ARCHIVE_SPATIAL_COMPONENT_MAX = 1048576.0f;
constexpr float FX_ARCHIVE_DISTANCE_MAX = 16777216.0f;
constexpr std::int32_t FX_ARCHIVE_DISTANCE_INTEGER_MAX = 16777216;
constexpr float FX_ARCHIVE_LINEAR_VELOCITY_MAX = 1048576.0f;
constexpr double FX_ARCHIVE_UNIT_LENGTH_TOLERANCE = 0.025;
constexpr double FX_ARCHIVE_ORTHOGONAL_TOLERANCE = 0.025;

static_assert(layout::ELEM_DEF_STRIDE == FX_ELEM_DEF_RUNTIME_SIZE);
static_assert(std::is_standard_layout_v<FxElem>);
static_assert(std::is_trivially_copyable_v<FxElem>);
static_assert(
    fx::physics::BODY_LIMIT
    <= static_cast<std::size_t>(
        (std::numeric_limits<std::uint32_t>::max)()));

struct ArchiveIntRange
{
    std::int32_t base;
    std::int32_t amplitude;
};

struct ArchiveTrailDefHeader
{
    std::int32_t scrollTimeMsec;
    std::int32_t repeatDist;
    std::int32_t splitDist;
};

template <typename VALUE_TYPE>
VALUE_TYPE ReadRepresentation(
    const void *const object,
    const std::size_t offset = 0) noexcept
{
    static_assert(std::is_trivially_copyable_v<VALUE_TYPE>);
    VALUE_TYPE value{};
    const auto *const bytes = static_cast<const std::uint8_t *>(object);
    std::memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

const FxElemDef *ArchiveElemDefAt(
    const FxEffectDef &effectDef,
    const std::size_t index) noexcept
{
    const auto *const bytes =
        reinterpret_cast<const std::uint8_t *>(effectDef.elemDefs);
    return reinterpret_cast<const FxElemDef *>(
        bytes + index * layout::ELEM_DEF_STRIDE);
}

std::int32_t ArchiveElemDefFlags(const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<std::int32_t>(
        elemDef, layout::ELEM_DEF_FLAGS_OFFSET);
}

ArchiveIntRange ArchiveElemDefSpawn(
    const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<ArchiveIntRange>(
        elemDef, layout::ELEM_DEF_SPAWN_OFFSET);
}

ArchiveIntRange ArchiveElemDefSpawnDelay(
    const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<ArchiveIntRange>(
        elemDef, layout::ELEM_DEF_SPAWN_DELAY_OFFSET);
}

ArchiveIntRange ArchiveElemDefLifeSpan(
    const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<ArchiveIntRange>(
        elemDef, layout::ELEM_DEF_LIFE_SPAN_OFFSET);
}

std::uint8_t ArchiveElemDefType(const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<std::uint8_t>(
        elemDef, layout::ELEM_DEF_ELEM_TYPE_OFFSET);
}

std::uint8_t ArchiveElemDefVisualCount(
    const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<std::uint8_t>(
        elemDef, layout::ELEM_DEF_VISUAL_COUNT_OFFSET);
}

const void *ArchiveElemDefVisuals(
    const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<const void *>(
        elemDef, layout::ELEM_DEF_VISUALS_OFFSET);
}

const void *ArchiveElemDefTrailDef(
    const FxElemDef *const elemDef) noexcept
{
    return ReadRepresentation<const void *>(
        elemDef, layout::ELEM_DEF_TRAIL_DEF_OFFSET);
}

void CopyArchiveElemOrigin(
    const FxElem &elem,
    float (&origin)[3]) noexcept
{
    const auto *const bytes =
        reinterpret_cast<const std::uint8_t *>(&elem);
    std::memcpy(origin, bytes + offsetof(FxElem, physObjId), sizeof(origin));
}

std::int32_t CopyArchiveElemPhysicsToken(const FxElem &elem) noexcept
{
    return ReadRepresentation<std::int32_t>(
        &elem, offsetof(FxElem, physObjId));
}

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

bool ValidateArchiveUnitQuaternion(const float (&quat)[4]) noexcept
{
    if (!ValidateArchiveVector(quat, 1.001f))
        return false;

    double lengthSquared = 0.0;
    for (const float value : quat)
        lengthSquared += static_cast<double>(value) * value;
    return lengthSquared >= 1.0 - FX_ARCHIVE_UNIT_LENGTH_TOLERANCE
        && lengthSquared <= 1.0 + FX_ARCHIVE_UNIT_LENGTH_TOLERANCE;
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

bool ValidateArchiveSpatialFrame(const FxSpatialFrame &frame) noexcept
{
    return ValidateArchiveUnitQuaternion(frame.quat)
        && ValidateArchiveVector(
            frame.origin, FX_ARCHIVE_SPATIAL_COMPONENT_MAX);
}

bool ValidateArchiveSampledLifespan(
    const FxEffect *const effect,
    const FxElemDef *const elemDef,
    const std::int32_t msecBegin,
    const std::uint8_t sequence) noexcept
{
    if (!effect || !elemDef || effect->randomSeed >= 0x1DFu)
        return false;

    const ArchiveIntRange lifeSpan = ArchiveElemDefLifeSpan(elemDef);
    if (lifeSpan.amplitude < 0
        || lifeSpan.amplitude > FX_ARCHIVE_RANDOM_RANGE_AMPLITUDE_MAX)
    {
        return false;
    }

    const std::uint32_t randomSeed =
        (296u * sequence
            + static_cast<std::uint32_t>(msecBegin)
            + effect->randomSeed)
        % 0x1DFu;
    std::uint32_t randomBits = 0;
    std::memcpy(
        &randomBits,
        &fx_randomTable[randomSeed + 17u],
        sizeof(randomBits));
    const std::int64_t sampledLifespan =
        static_cast<std::int64_t>(lifeSpan.base)
        + ((static_cast<std::int64_t>(
                lifeSpan.amplitude)
                + 1)
            * (randomBits & 0xFFFFu)
            >> 16);
    const std::int64_t msecEnd =
        static_cast<std::int64_t>(msecBegin) + sampledLifespan;
    return sampledLifespan > 0
        && sampledLifespan <= FX_ARCHIVE_DURATION_LIMIT_MSEC
        && msecEnd >= (std::numeric_limits<std::int32_t>::min)()
        && msecEnd <= (std::numeric_limits<std::int32_t>::max)();
}

bool ValidateArchiveEffectRuntime(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    if (!system || !effect
        || !ValidateArchiveSpatialFrame(effect->frameAtSpawn)
        || !ValidateArchiveSpatialFrame(effect->frameNow)
        || !ValidateArchiveSpatialFrame(effect->framePrev)
        || !ArchiveFloatIsBounded(
            effect->distanceTraveled, FX_ARCHIVE_DISTANCE_MAX)
        || effect->distanceTraveled < 0.0f
        || effect->randomSeed >= 0x1DFu
        || effect->msecLastUpdate < effect->msecBegin
        || !ArchiveTimeDifferenceFits(
            system->msecNow, effect->msecBegin)
        || !ArchiveTimeDifferenceFits(
            system->msecNow, effect->msecLastUpdate)
        || !ArchiveTimeDifferenceFits(
            effect->msecLastUpdate, effect->msecBegin)
        || !ArchiveTimeDifferenceFits(
            system->msecDraw, effect->msecBegin))
    {
        return false;
    }

    const std::uint32_t dobjHandle = effect->boltAndSortOrder.dobjHandle;
    const std::uint32_t boneIndex = effect->boltAndSortOrder.boneIndex;
    if (boneIndex == FX_ARCHIVE_BONE_INDEX_NONE)
    {
        return dobjHandle == FX_ARCHIVE_DOBJ_HANDLE_NONE
            || dobjHandle < FX_ARCHIVE_MAX_GENTITIES;
    }
    return dobjHandle < FX_ARCHIVE_CLIENT_DOBJ_HANDLE_MAX;
}

bool ElemStoresPhysicsBody(const FxElemDef *const elemDef) noexcept
{
    return elemDef
        && ArchiveElemDefType(elemDef) == FX_ARCHIVE_ELEM_TYPE_MODEL
        && (static_cast<std::uint32_t>(ArchiveElemDefFlags(elemDef))
            & FX_ARCHIVE_PHYSICS_FLAG)
            != 0;
}

FxArchiveElemPayloadKind GetElemPayloadKind(
    const FxElemDef *const elemDef) noexcept
{
    if (ArchiveElemDefType(elemDef) == FX_ARCHIVE_ELEM_TYPE_TRAIL)
        return FxArchiveElemPayloadKind::OriginTrailTexCoord;
    return ElemStoresPhysicsBody(elemDef)
        ? FxArchiveElemPayloadKind::PhysicsLighting
        : FxArchiveElemPayloadKind::OriginLighting;
}

bool ValidateArchiveElemRuntime(
    const FxSystem *const system,
    const FxEffect *const effect,
    const FxElem *const elem,
    const FxElemDef *const elemDef) noexcept
{
    if (!system || !effect || !elem || !elemDef
        || !ArchiveTimeDifferenceFits(
            system->msecNow, elem->msecBegin)
        || !ArchiveTimeDifferenceFits(
            system->msecDraw, elem->msecBegin)
        || !ValidateArchiveSampledLifespan(
            effect, elemDef, elem->msecBegin, elem->sequence)
        || !ValidateArchiveVector(
            elem->baseVel, FX_ARCHIVE_LINEAR_VELOCITY_MAX))
    {
        return false;
    }

    if (ElemStoresPhysicsBody(elemDef))
        return true;

    float origin[3]{};
    CopyArchiveElemOrigin(*elem, origin);
    return ValidateArchiveVector(
        origin, FX_ARCHIVE_SPATIAL_COMPONENT_MAX);
}

bool ArchiveEffectRingIsValid(const FxSystem *const system) noexcept
{
    if (!system)
        return false;

    const std::int64_t firstActiveEffect = system->firstActiveEffect;
    const std::int64_t firstNewEffect = system->firstNewEffect;
    const std::int64_t firstFreeEffect = system->firstFreeEffect;
    const std::int64_t allocatedEffectCount =
        firstFreeEffect - firstActiveEffect;
    return firstActiveEffect >= 0
        && firstNewEffect == firstFreeEffect
        && allocatedEffectCount >= 0
        && allocatedEffectCount
            <= static_cast<std::int64_t>(MAX_EFFECTS);
}

bool TryGetArchiveEffectDefCount(
    const FxEffect *const effect,
    std::size_t *const outCount) noexcept
{
    if (!effect || !effect->def || !outCount
        || effect->def->elemDefCountLooping < 0
        || effect->def->elemDefCountOneShot < 0
        || effect->def->elemDefCountEmission < 0)
    {
        return false;
    }

    const std::int64_t count =
        static_cast<std::int64_t>(effect->def->elemDefCountLooping)
        + effect->def->elemDefCountOneShot
        + effect->def->elemDefCountEmission;
    if (count < 0
        || count
            > static_cast<std::int64_t>(
                (std::numeric_limits<std::uint8_t>::max)()) + 1
        || (count != 0 && !effect->def->elemDefs))
    {
        return false;
    }

    *outCount = static_cast<std::size_t>(count);
    return true;
}

bool ValidateArchiveTimeRange(const ArchiveIntRange &range) noexcept
{
    if (range.amplitude < 0
        || range.amplitude > FX_ARCHIVE_RANDOM_RANGE_AMPLITUDE_MAX)
    {
        return false;
    }
    const std::int64_t minimum = range.base;
    const std::int64_t maximum =
        static_cast<std::int64_t>(range.base) + range.amplitude;
    return minimum >= -FX_ARCHIVE_DURATION_LIMIT_MSEC
        && minimum <= FX_ARCHIVE_DURATION_LIMIT_MSEC
        && maximum >= -FX_ARCHIVE_DURATION_LIMIT_MSEC
        && maximum <= FX_ARCHIVE_DURATION_LIMIT_MSEC;
}

bool ValidateArchiveOneShotCount(const ArchiveIntRange &range) noexcept
{
    if (range.base < 0 || range.amplitude < 0
        || range.amplitude > FX_ARCHIVE_RANDOM_RANGE_AMPLITUDE_MAX)
    {
        return false;
    }
    const std::int64_t maximum =
        static_cast<std::int64_t>(range.base) + range.amplitude;
    return maximum <= static_cast<std::int64_t>(MAX_ELEMS);
}

bool ValidateArchiveEffectDefTiming(
    const FxEffectDef *const def,
    const std::size_t elemDefCount) noexcept
{
    if (!def || elemDefCount > std::size_t{256}
        || (elemDefCount != 0 && !def->elemDefs))
    {
        return false;
    }

    std::int64_t maximumLoopingLife = 0;
    bool hasInfiniteLoop = false;
    for (std::size_t index = 0; index < elemDefCount; ++index)
    {
        const FxElemDef *const elemDef = ArchiveElemDefAt(*def, index);
        if (!ValidateArchiveTimeRange(
                ArchiveElemDefSpawnDelay(elemDef))
            || !ValidateArchiveTimeRange(
                ArchiveElemDefLifeSpan(elemDef)))
        {
            return false;
        }

        if (index < static_cast<std::size_t>(def->elemDefCountLooping))
        {
            const ArchiveIntRange spawn = ArchiveElemDefSpawn(elemDef);
            const std::int32_t interval = spawn.base;
            const std::int32_t count = spawn.amplitude;
            if (interval < 0
                || (interval == 0
                    && ArchiveElemDefType(elemDef)
                        != FX_ARCHIVE_ELEM_TYPE_TRAIL)
                || interval > FX_ARCHIVE_DURATION_LIMIT_MSEC
                || count < 0)
            {
                return false;
            }
            if (count == (std::numeric_limits<std::int32_t>::max)())
            {
                hasInfiniteLoop = true;
                continue;
            }

            const std::int64_t lastSpawn = count > 1
                ? static_cast<std::int64_t>(interval) * (count - 1)
                : 0;
            if (lastSpawn > FX_ARCHIVE_DURATION_LIMIT_MSEC)
                return false;
            if (maximumLoopingLife < lastSpawn)
                maximumLoopingLife = lastSpawn;
        }
        else if (!ValidateArchiveOneShotCount(
                     ArchiveElemDefSpawn(elemDef)))
        {
            return false;
        }
    }

    const std::int32_t expectedLoopingLife = hasInfiniteLoop
        ? (std::numeric_limits<std::int32_t>::max)()
        : static_cast<std::int32_t>(maximumLoopingLife);
    return def->msecLoopingLife == expectedLoopingLife;
}

bool ArchiveElemTypeMatchesClass(
    const std::uint8_t elemType,
    const std::size_t elemClass) noexcept
{
    switch (elemClass)
    {
    case 0:
        return elemType == FX_ARCHIVE_ELEM_TYPE_SPRITE_BILLBOARD
            || elemType == FX_ARCHIVE_ELEM_TYPE_SPRITE_ORIENTED
            || elemType == FX_ARCHIVE_ELEM_TYPE_TAIL;
    case 1:
        return elemType == FX_ARCHIVE_ELEM_TYPE_MODEL
            || elemType == FX_ARCHIVE_ELEM_TYPE_OMNI_LIGHT;
    case 2:
        return elemType == FX_ARCHIVE_ELEM_TYPE_CLOUD;
    default:
        return false;
    }
}

bool ValidateArchiveCamera(const FxCamera &camera) noexcept
{
    const std::int32_t isValid = Sys_AtomicLoad(&camera.isValid);
    if ((isValid != 0 && isValid != 1)
        || camera.frustumPlaneCount > 6
        || (isValid == 0 && camera.frustumPlaneCount != 0)
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

    if (isValid != 0 && camera.frustumPlaneCount != 0)
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

bool ValidateArchiveVisibilityStates(
    const FxVisState *const visStates) noexcept
{
    if (!visStates)
        return false;
    for (std::size_t stateIndex = 0; stateIndex < 2; ++stateIndex)
    {
        const FxVisState &state = visStates[stateIndex];
        const std::int32_t blockerCount =
            Sys_AtomicLoad(&state.blockerCount);
        if (blockerCount < 0 || blockerCount > 256)
            return false;
        for (std::int32_t blockerIndex = 0;
             blockerIndex < blockerCount;
             ++blockerIndex)
        {
            if (!ValidateArchiveVector(
                    state.blocker[blockerIndex].origin,
                    FX_ARCHIVE_SPATIAL_COMPONENT_MAX))
            {
                return false;
            }
        }
    }
    return true;
}

bool ValidateArchiveSystemState(const FxSystem *const system) noexcept
{
    if (!system)
        return false;
    return ArchiveEffectRingIsValid(system)
        && system->isInitialized && system->localClientNum == 0
        && system->msecNow >= 0 && system->msecDraw >= -1
        && FX_AreArchiveCamerasReady(
            system->camera, system->cameraPrev, system->msecDraw)
        && Sys_AtomicLoad(&system->deferredElemCount) == 0
        && system->sprite.indexCount == 0
        && ArchiveTimeDifferenceFits(
            system->msecNow, system->msecDraw)
        && ValidateArchiveCamera(system->camera)
        && ValidateArchiveCamera(system->cameraPrev)
        && ValidateArchiveVisibilityStates(system->visState);
}

bool PrepareElemPayload(
    const FxArchiveSemanticCallbacks &callbacks,
    FxSystem *const system,
    FxEffect *const effect,
    FxElem *const elem,
    const FxElemDef *const elemDef) noexcept
{
    if (!callbacks.prepareElemPayload)
        return true;
    if (!elem)
        return false;

    // FxElem has one fixed 0x28-byte runtime representation on every supported
    // architecture (pinned in fx_runtime.h).  Beginning a selected union-member
    // lifetime must not alter any byte that callback-free preflight validated.
    std::array<std::uint8_t, FX_ARCHIVE_ELEM_REPRESENTATION_SIZE> before{};
    std::memcpy(before.data(), elem, before.size());
    const bool accepted = callbacks.prepareElemPayload(
        callbacks.context,
        system,
        effect,
        elem,
        elemDef,
        GetElemPayloadKind(elemDef));
    return accepted
        && std::memcmp(before.data(), elem, before.size()) == 0;
}

bool TrySelectPhysicsModelNoReport(
    const FxEffect &effect,
    const FxElem &elem,
    const FxElemDef *const elemDef,
    const XModel **const outModel) noexcept
{
    const std::uint8_t visualCount =
        elemDef ? ArchiveElemDefVisualCount(elemDef) : 0;
    if (!elemDef || !outModel || visualCount == 0
        || effect.randomSeed >= 0x1DFu)
    {
        return false;
    }

    const std::uint32_t randomSeed =
        (296u * static_cast<std::uint32_t>(elem.sequence)
            + static_cast<std::uint32_t>(elem.msecBegin)
            + static_cast<std::uint32_t>(effect.randomSeed))
        % 0x1DFu;
    const XModel *model = nullptr;
    if (visualCount == 1)
    {
        model = ReadRepresentation<const XModel *>(
            elemDef, layout::ELEM_DEF_VISUALS_OFFSET);
    }
    else
    {
        const void *const visuals = ArchiveElemDefVisuals(elemDef);
        if (!visuals)
            return false;
        std::uint32_t randomBits = 0;
        std::memcpy(
            &randomBits,
            &fx_randomTable[randomSeed + 21u],
            sizeof(randomBits));
        const std::size_t visualIndex =
            (static_cast<std::uint32_t>(visualCount)
                * (randomBits & 0xFFFFu))
            >> 16;
        if (visualIndex >= visualCount)
            return false;
        const auto *const visualBytes =
            static_cast<const std::uint8_t *>(visuals)
            + visualIndex * sizeof(void *);
        model = ReadRepresentation<const XModel *>(visualBytes);
    }

    if (!model)
        return false;
    *outModel = model;
    return true;
}

bool AcceptPhysicsElem(
    FxSystem *const system,
    FxEffect *const effect,
    FxElem *const elem,
    const FxElemDef *const elemDef,
    const FxArchiveSemanticCallbacks &callbacks,
    std::size_t *const physicsBodyCount) noexcept
{
    if (!system || !effect || !elem || !elemDef || !physicsBodyCount)
        return false;
    if (!ElemStoresPhysicsBody(elemDef))
        return true;
    if (*physicsBodyCount >= fx::physics::BODY_LIMIT)
        return false;

    const XModel *model = nullptr;
    if (!TrySelectPhysicsModelNoReport(
            *effect, *elem, elemDef, &model))
    {
        return false;
    }

    std::int32_t ownerIndex = -1;
    const fx::physics::BodyToken token =
        fx::physics::TokenFromLegacyField(
            CopyArchiveElemPhysicsToken(*elem));
    if (!FxPoolItemIndex<FxElem, MAX_ELEMS>(
            system->elems, elem, &ownerIndex)
        || ownerIndex < 0
        || token == fx::physics::INVALID_BODY_TOKEN)
    {
        return false;
    }

    const FxArchiveSemanticPhysicsDescriptor descriptor{
        elem,
        model,
        static_cast<std::size_t>(ownerIndex),
        static_cast<std::uint32_t>(token)};
    if (callbacks.acceptPhysics
        && !callbacks.acceptPhysics(
            callbacks.context, descriptor, *physicsBodyCount))
    {
        return false;
    }
    ++*physicsBodyCount;
    return true;
}
} // namespace

static bool TraverseFxArchiveSemanticsNoReport(
    FxSystem *const system,
    const FxArchiveSemanticCallbacks &callbacks,
    FxArchiveSemanticResult *const outResult) noexcept
{
    if (!system || !outResult || !system->effects || !system->elems
        || !system->trails || !system->trailElems
        || !ValidateArchiveSystemState(system))
    {
        return false;
    }

    std::size_t physicsBodyCount = 0;
    for (std::int64_t activeIndex = system->firstActiveEffect;
         activeIndex < system->firstFreeEffect;
         ++activeIndex)
    {
        FxEffect *const effect = FxDecodeHandle<
            FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                system->effects,
                system->allEffectHandles[
                    static_cast<std::size_t>(activeIndex)
                    & (MAX_EFFECTS - 1)]);
        std::size_t elemDefCount = 0;
        if (!effect || !ValidateArchiveEffectRuntime(system, effect)
            || !TryGetArchiveEffectDefCount(effect, &elemDefCount)
            || !ValidateArchiveEffectDefTiming(
                effect->def, elemDefCount))
        {
            return false;
        }

        std::size_t spotLightDefCount = 0;
        for (std::size_t elemDefIndex = 0;
             elemDefIndex < elemDefCount;
             ++elemDefIndex)
        {
            const FxElemDef *const elemDef =
                ArchiveElemDefAt(*effect->def, elemDefIndex);
            const std::uint8_t elemType = ArchiveElemDefType(elemDef);
            if (elemType >= FX_ARCHIVE_ELEM_TYPE_COUNT)
                return false;
            if (elemType == FX_ARCHIVE_ELEM_TYPE_SPOT_LIGHT
                && ++spotLightDefCount > 1)
            {
                return false;
            }
        }

        for (std::size_t elemClass = 0; elemClass < 3; ++elemClass)
        {
            std::uint16_t elemHandle =
                effect->firstElemHandle[elemClass];
            std::size_t chainLength = 0;
            while (elemHandle != FX_ARCHIVE_INVALID_HANDLE)
            {
                if (chainLength++ == MAX_ELEMS)
                    return false;
                FxPool<FxElem> *const remoteElem = FxDecodeHandle<
                    FxPool<FxElem>, MAX_ELEMS, FxElem::HANDLE_SCALE>(
                        system->elems, elemHandle);
                if (!remoteElem
                    || remoteElem->item.defIndex >= elemDefCount)
                {
                    return false;
                }
                FxElem *const elem = &remoteElem->item;
                const FxElemDef *const elemDef =
                    ArchiveElemDefAt(*effect->def, elem->defIndex);
                if (!PrepareElemPayload(
                        callbacks, system, effect, elem, elemDef)
                    || !ValidateArchiveElemRuntime(
                        system, effect, elem, elemDef)
                    || !ArchiveElemTypeMatchesClass(
                        ArchiveElemDefType(elemDef), elemClass)
                    || !AcceptPhysicsElem(
                        system,
                        effect,
                        elem,
                        elemDef,
                        callbacks,
                        &physicsBodyCount))
                {
                    return false;
                }
                elemHandle = elem->nextElemHandleInEffect;
            }
        }

        std::uint16_t trailHandle = effect->firstTrailHandle;
        std::size_t trailCount = 0;
        while (trailHandle != FX_ARCHIVE_INVALID_HANDLE)
        {
            if (trailCount++ == MAX_TRAILS)
                return false;
            FxPool<FxTrail> *const remoteTrail = FxDecodeHandle<
                FxPool<FxTrail>, MAX_TRAILS, FxTrail::HANDLE_SCALE>(
                    system->trails, trailHandle);
            if (!remoteTrail
                || remoteTrail->item.defIndex >= elemDefCount)
            {
                return false;
            }

            const FxElemDef *const trailDef = ArchiveElemDefAt(
                *effect->def, remoteTrail->item.defIndex);
            const void *const trailDefStorage =
                ArchiveElemDefTrailDef(trailDef);
            ArchiveTrailDefHeader trailHeader{};
            if (trailDefStorage)
            {
                trailHeader = ReadRepresentation<ArchiveTrailDefHeader>(
                    trailDefStorage);
            }
            const std::int64_t firstOneShotDef =
                effect->def->elemDefCountLooping;
            const std::int64_t firstEmissionDef = firstOneShotDef
                + effect->def->elemDefCountOneShot;
            if (ArchiveElemDefType(trailDef)
                    != FX_ARCHIVE_ELEM_TYPE_TRAIL
                || !trailDefStorage
                || trailHeader.splitDist <= 0
                || trailHeader.repeatDist <= 0
                || trailHeader.splitDist
                    > FX_ARCHIVE_DISTANCE_INTEGER_MAX
                || trailHeader.repeatDist
                    > FX_ARCHIVE_DISTANCE_INTEGER_MAX
                || (remoteTrail->item.defIndex >= firstOneShotDef
                    && remoteTrail->item.defIndex < firstEmissionDef))
            {
                return false;
            }

            std::uint16_t trailElemHandle =
                remoteTrail->item.firstElemHandle;
            std::size_t trailElemCount = 0;
            while (trailElemHandle != FX_ARCHIVE_INVALID_HANDLE)
            {
                if (trailElemCount++ == MAX_TRAIL_ELEMS)
                    return false;
                FxPool<FxTrailElem> *const remoteTrailElem =
                    FxDecodeHandle<
                        FxPool<FxTrailElem>,
                        MAX_TRAIL_ELEMS,
                        FxTrailElem::HANDLE_SCALE>(
                            system->trailElems, trailElemHandle);
                if (!remoteTrailElem
                    || !ArchiveTimeDifferenceFits(
                        system->msecNow,
                        remoteTrailElem->item.msecBegin)
                    || !ArchiveTimeDifferenceFits(
                        system->msecDraw,
                        remoteTrailElem->item.msecBegin)
                    || !ValidateArchiveSampledLifespan(
                        effect,
                        trailDef,
                        remoteTrailElem->item.msecBegin,
                        remoteTrailElem->item.sequence)
                    || !ValidateArchiveVector(
                        remoteTrailElem->item.origin,
                        FX_ARCHIVE_SPATIAL_COMPONENT_MAX)
                    || !ArchiveFloatIsBounded(
                        remoteTrailElem->item.spawnDist,
                        FX_ARCHIVE_DISTANCE_MAX)
                    || remoteTrailElem->item.spawnDist < 0.0f)
                {
                    return false;
                }
                trailElemHandle =
                    remoteTrailElem->item.nextTrailElemHandle;
            }
            trailHandle = remoteTrail->item.nextTrailHandle;
        }
    }

    const std::int32_t spotEffectCount =
        Sys_AtomicLoad(&system->activeSpotLightEffectCount);
    const std::int32_t spotElemCount =
        Sys_AtomicLoad(&system->activeSpotLightElemCount);
    if (spotEffectCount < 0 || spotEffectCount > 1
        || spotElemCount < 0 || spotElemCount > 1
        || (spotElemCount != 0 && spotEffectCount != 1))
    {
        return false;
    }

    FxEffect *spotLightEffect = nullptr;
    std::size_t spotLightEffectDefCount = 0;
    if (spotEffectCount == 1)
    {
        spotLightEffect = FxDecodeHandle<
            FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                system->effects, system->activeSpotLightEffectHandle);
        if (!spotLightEffect
            || !TryGetArchiveEffectDefCount(
                spotLightEffect, &spotLightEffectDefCount)
            || !ValidateArchiveEffectRuntime(
                system, spotLightEffect)
            || !ValidateArchiveEffectDefTiming(
                spotLightEffect->def, spotLightEffectDefCount))
        {
            return false;
        }

        bool hasSpotLightDefinition = false;
        for (std::size_t elemDefIndex = 0;
             elemDefIndex < spotLightEffectDefCount;
             ++elemDefIndex)
        {
            if (ArchiveElemDefType(ArchiveElemDefAt(
                    *spotLightEffect->def, elemDefIndex))
                == FX_ARCHIVE_ELEM_TYPE_SPOT_LIGHT)
            {
                hasSpotLightDefinition = true;
                break;
            }
        }
        if (!hasSpotLightDefinition)
            return false;
    }

    if (spotElemCount == 1)
    {
        FxPool<FxElem> *const remoteElem = FxDecodeHandle<
            FxPool<FxElem>, MAX_ELEMS, FxElem::HANDLE_SCALE>(
                system->elems, system->activeSpotLightElemHandle);
        if (!spotLightEffect || !remoteElem
            || remoteElem->item.defIndex >= spotLightEffectDefCount)
        {
            return false;
        }

        FxElem *const elem = &remoteElem->item;
        const FxElemDef *const elemDef =
            ArchiveElemDefAt(*spotLightEffect->def, elem->defIndex);
        if (!PrepareElemPayload(
                callbacks,
                system,
                spotLightEffect,
                elem,
                elemDef)
            || ArchiveElemDefType(elemDef)
                != FX_ARCHIVE_ELEM_TYPE_SPOT_LIGHT
            || !ValidateArchiveElemRuntime(
                system, spotLightEffect, elem, elemDef)
            || elem->nextElemHandleInEffect != FX_ARCHIVE_INVALID_HANDLE
            || elem->prevElemHandleInEffect != FX_ARCHIVE_INVALID_HANDLE)
        {
            return false;
        }
    }

    const FxArchiveSemanticResult result{
        static_cast<std::uint32_t>(physicsBodyCount),
        spotLightEffect
                && spotLightEffect->boltAndSortOrder.boneIndex
                    != FX_ARCHIVE_BONE_INDEX_NONE
            ? static_cast<std::int16_t>(
                spotLightEffect->boltAndSortOrder.dobjHandle)
            : static_cast<std::int16_t>(-1)};
    *outResult = result;
    return true;
}

bool TryValidateFxArchiveSemanticsNoReport(
    FxSystem *const system,
    const FxArchiveSemanticCallbacks &callbacks,
    FxArchiveSemanticResult *const outResult) noexcept
{
    if (!outResult)
        return false;

    // The first pass interprets every definition-selected union strictly via
    // object-representation bytes.  No callback can activate staging storage
    // until the complete definition-dependent graph has passed validation.
    const FxArchiveSemanticCallbacks noCallbacks{};
    FxArchiveSemanticResult preflight{};
    if (!TraverseFxArchiveSemanticsNoReport(
            system, noCallbacks, &preflight))
    {
        return false;
    }

    // The graph/lifetime contract keeps links and definitions stable between
    // passes.  Rewalking avoids a large descriptor staging allocation while
    // giving each element exactly one activation callback and each physics
    // element exactly one sink callback.
    FxArchiveSemanticResult applied{};
    if (!TraverseFxArchiveSemanticsNoReport(
            system, callbacks, &applied)
        || applied.physicsBodyCount != preflight.physicsBodyCount
        || applied.spotLightBoltDobj != preflight.spotLightBoltDobj)
    {
        return false;
    }

    *outResult = applied;
    return true;
}
} // namespace fx::archive

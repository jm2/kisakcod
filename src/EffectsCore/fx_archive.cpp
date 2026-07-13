#include "fx_system.h"
#include "fx_pool_graph.h"

#include <database/database.h>

#include <physics/phys_local.h>
#include <physics/ode/odeext.h>

#include <universal/com_memory.h>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace
{
struct FxArchivePoolAllocationStates
{
    FxPoolAllocationState<MAX_ELEMS> elems;
    FxPoolAllocationState<MAX_TRAILS> trails;
    FxPoolAllocationState<MAX_TRAIL_ELEMS> trailElems;
};

struct FxArchivePhysicsEntry
{
    FxElem *elem;
    const XModel *model;
    BodyState state;
    dxBody *createdBody;
    dxBody *replacedBody;
    int originalPhysObjId;
};

constexpr int FX_ARCHIVE_SYSTEM_SIZE = 2656;
constexpr int FX_ARCHIVE_BUFFER_SIZE = 291968;
constexpr int FX_ARCHIVE_BODY_STATE_SIZE = 112;
constexpr std::uint16_t FX_ARCHIVE_INVALID_HANDLE = 0xFFFFu;
constexpr std::uint32_t FX_ARCHIVE_PHYSICS_FLAG = 0x08000000u;
// Restored timestamps feed legacy signed-int additions and differences. Bound
// each accepted delay/lifespan/loop horizon to one day, then reserve four such
// horizons around every absolute time: finite loop offset + spawn delay +
// lifespan + a full day of post-restore update runway.
constexpr std::int64_t FX_ARCHIVE_DURATION_LIMIT_MSEC =
    24ll * 60ll * 60ll * 1000ll;
constexpr std::int64_t FX_ARCHIVE_TIME_HEADROOM_MSEC =
    4ll * FX_ARCHIVE_DURATION_LIMIT_MSEC;
constexpr std::int32_t FX_ARCHIVE_RANDOM_RANGE_AMPLITUDE_MAX = 32767;

// CoD4 maps conventionally live inside +/-131072 units. The archive permits
// eight times that range for custom content, while rejecting finite values
// large enough to overflow common squared-distance and integration math.
constexpr float FX_ARCHIVE_SPATIAL_COMPONENT_MAX = 1048576.0f;
constexpr float FX_ARCHIVE_DISTANCE_MAX = 16777216.0f;
constexpr std::int32_t FX_ARCHIVE_DISTANCE_INTEGER_MAX = 16777216;
constexpr float FX_ARCHIVE_LINEAR_VELOCITY_MAX = 1048576.0f;
constexpr float FX_ARCHIVE_ANGULAR_VELOCITY_MAX = 65536.0f;
constexpr float FX_ARCHIVE_PHYSICS_MASS_MIN = 0.0001f;
constexpr float FX_ARCHIVE_PHYSICS_MASS_MAX = 1000000.0f;
constexpr float FX_ARCHIVE_PHYSICS_FRICTION_MAX = 10000.0f;
constexpr float FX_ARCHIVE_PHYSICS_BOUNCE_MAX = 1.0f;
constexpr double FX_ARCHIVE_UNIT_LENGTH_TOLERANCE = 0.025;
constexpr double FX_ARCHIVE_ORTHOGONAL_TOLERANCE = 0.025;

bool FX_ArchiveFloatIsBounded(
    const float value,
    const float magnitudeLimit) noexcept
{
    return std::isfinite(value)
        && value >= -magnitudeLimit
        && value <= magnitudeLimit;
}

template <std::size_t COUNT>
bool FX_ValidateArchiveVector(
    const float (&values)[COUNT],
    const float magnitudeLimit) noexcept
{
    for (const float value : values)
    {
        if (!FX_ArchiveFloatIsBounded(value, magnitudeLimit))
            return false;
    }
    return true;
}

bool FX_ValidateArchiveUnitQuaternion(const float (&quat)[4]) noexcept
{
    if (!FX_ValidateArchiveVector(quat, 1.001f))
        return false;

    double lengthSquared = 0.0;
    for (const float value : quat)
        lengthSquared += static_cast<double>(value) * value;
    return lengthSquared >= 1.0 - FX_ARCHIVE_UNIT_LENGTH_TOLERANCE
        && lengthSquared <= 1.0 + FX_ARCHIVE_UNIT_LENGTH_TOLERANCE;
}

bool FX_ValidateArchiveOrthonormalBasis(
    const float (&basis)[3][3]) noexcept
{
    for (const auto &row : basis)
    {
        if (!FX_ValidateArchiveVector(row, 1.001f))
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

bool FX_ArchiveEffectNameIsValid(const char *const name) noexcept
{
    if (!name || !name[0])
        return false;
    std::size_t componentStart = 0;
    for (std::size_t index = 0; index < 64; ++index)
    {
        const unsigned char value =
            static_cast<unsigned char>(name[index]);
        if (value == 0)
        {
            const std::size_t componentLength = index - componentStart;
            return componentLength != 0
                && !(componentLength == 1
                    && name[componentStart] == '.')
                && !(componentLength == 2
                    && name[componentStart] == '.'
                    && name[componentStart + 1] == '.');
        }
        if (value < 0x20u || value == 0x7Fu || value == ':')
            return false;
        if (value == '/' || value == '\\')
        {
            const std::size_t componentLength = index - componentStart;
            if (componentLength == 0
                || (componentLength == 1
                    && name[componentStart] == '.')
                || (componentLength == 2
                    && name[componentStart] == '.'
                    && name[componentStart + 1] == '.'))
            {
                return false;
            }
            componentStart = index + 1;
        }
    }
    return false;
}
RUNTIME_SIZE(BodyState, 0x70, 0x70);

bool FX_ArchiveByteIsBool(const std::uint8_t value) noexcept
{
    return value == 0 || value == 1;
}

bool FX_ValidateArchiveSystemBooleanBytes(
    const std::uint8_t *const bytes,
    const std::size_t byteCount) noexcept
{
    if (!bytes)
        return false;

    const std::size_t initializedOffset =
        offsetof(FxSystem, isInitialized);
    const std::size_t garbageCollectionOffset =
        offsetof(FxSystem, needsGarbageCollection);
    const std::size_t archivingOffset = offsetof(FxSystem, isArchiving);
    if (initializedOffset >= byteCount
        || garbageCollectionOffset >= byteCount
        || archivingOffset >= byteCount)
    {
        return false;
    }

    return bytes[initializedOffset] == 1
        && FX_ArchiveByteIsBool(bytes[garbageCollectionOffset])
        && bytes[archivingOffset] == 1;
}

bool FX_ArchiveTimeDifferenceFits(
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

bool FX_ValidateArchiveSpatialFrame(
    const FxSpatialFrame &frame) noexcept
{
    return FX_ValidateArchiveUnitQuaternion(frame.quat)
        && FX_ValidateArchiveVector(
            frame.origin, FX_ARCHIVE_SPATIAL_COMPONENT_MAX);
}

bool FX_ValidateArchiveSampledLifespan(
    const FxEffect *const effect,
    const FxElemDef *const elemDef,
    const std::int32_t msecBegin,
    const std::uint8_t sequence) noexcept
{
    if (!effect || !elemDef || effect->randomSeed >= 0x1DFu
        || elemDef->lifeSpanMsec.amplitude < 0
        || elemDef->lifeSpanMsec.amplitude
            > FX_ARCHIVE_RANDOM_RANGE_AMPLITUDE_MAX)
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
        static_cast<std::int64_t>(elemDef->lifeSpanMsec.base)
        + ((static_cast<std::int64_t>(
                elemDef->lifeSpanMsec.amplitude)
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

bool FX_ValidateArchiveEffectRuntime(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    if (!system || !effect
        || !FX_ValidateArchiveSpatialFrame(effect->frameAtSpawn)
        || !FX_ValidateArchiveSpatialFrame(effect->frameNow)
        || !FX_ValidateArchiveSpatialFrame(effect->framePrev)
        || !FX_ArchiveFloatIsBounded(
            effect->distanceTraveled, FX_ARCHIVE_DISTANCE_MAX)
        || effect->distanceTraveled < 0.0f
        || effect->randomSeed >= 0x1DFu
        || effect->msecLastUpdate < effect->msecBegin
        || !FX_ArchiveTimeDifferenceFits(
            system->msecNow, effect->msecBegin)
        || !FX_ArchiveTimeDifferenceFits(
            system->msecNow, effect->msecLastUpdate)
        || !FX_ArchiveTimeDifferenceFits(
            effect->msecLastUpdate, effect->msecBegin)
        || !FX_ArchiveTimeDifferenceFits(
            system->msecDraw, effect->msecBegin))
    {
        return false;
    }

    const std::uint32_t dobjHandle = effect->boltAndSortOrder.dobjHandle;
    const std::uint32_t boneIndex = effect->boltAndSortOrder.boneIndex;
    if (boneIndex == FX_BONE_INDEX_NONE)
    {
        // A no-bone effect is either world-oriented (the sentinel pair) or
        // carries a mark entity.  Mark consumers index the entity arrays.
        return dobjHandle == FX_DOBJ_HANDLE_NONE
            || dobjHandle < MAX_GENTITIES;
    }

    // Bolted consumers pass the handle to Com_GetClientDObj before checking
    // whether the referenced object still exists.
    return dobjHandle < CLIENT_DOBJ_HANDLE_MAX;
}

bool FX_ValidateArchiveElemRuntime(
    const FxSystem *const system,
    const FxEffect *const effect,
    const FxElem *const elem,
    const FxElemDef *const elemDef) noexcept
{
    if (!system || !effect || !elem || !elemDef
        || !FX_ArchiveTimeDifferenceFits(
            system->msecNow, elem->msecBegin)
        || !FX_ArchiveTimeDifferenceFits(
            system->msecDraw, elem->msecBegin)
        || !FX_ValidateArchiveSampledLifespan(
            effect, elemDef, elem->msecBegin, elem->sequence))
    {
        return false;
    }
    if (!FX_ValidateArchiveVector(
            elem->baseVel, FX_ARCHIVE_LINEAR_VELOCITY_MAX))
    {
        return false;
    }

    const bool storesPhysicsBody = elemDef->elemType == FX_ELEM_TYPE_MODEL
        && (static_cast<std::uint32_t>(elemDef->flags)
            & FX_ARCHIVE_PHYSICS_FLAG)
            != 0;
    if (!storesPhysicsBody)
    {
        if (!FX_ValidateArchiveVector(
                elem->origin, FX_ARCHIVE_SPATIAL_COMPONENT_MAX))
        {
            return false;
        }
    }
    return true;
}

template <typename POINTER_TYPE>
std::uint32_t FX_ArchivePointerBits(POINTER_TYPE *const pointer) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &pointer, sizeof(std::uint32_t));
    return bits;
}

bool FX_RebuildArchivePoolAllocationStates(
    FxSystem *const system,
    FxArchivePoolAllocationStates *const states)
{
    if (!system || !states || !system->elems || !system->trails
        || !system->trailElems)
    {
        return false;
    }

    FxArchivePoolAllocationStates rebuilt{};
    alignas(4) volatile std::int32_t elemCount = 0;
    alignas(4) volatile std::int32_t trailCount = 0;
    alignas(4) volatile std::int32_t trailElemCount = 0;
    if (FxPoolRebuildAllocationStateLocked<FxElem, MAX_ELEMS>(
            &system->firstFreeElem,
            system->elems,
            &elemCount,
            &rebuilt.elems)
            != FxPoolMutationStatus::Success
        || FxPoolRebuildAllocationStateLocked<FxTrail, MAX_TRAILS>(
            &system->firstFreeTrail,
            system->trails,
            &trailCount,
            &rebuilt.trails)
            != FxPoolMutationStatus::Success
        || FxPoolRebuildAllocationStateLocked<FxTrailElem, MAX_TRAIL_ELEMS>(
            &system->firstFreeTrailElem,
            system->trailElems,
            &trailElemCount,
            &rebuilt.trailElems)
            != FxPoolMutationStatus::Success
        || Sys_AtomicLoad(&system->activeElemCount)
            != Sys_AtomicLoad(&elemCount)
        || Sys_AtomicLoad(&system->activeTrailCount)
            != Sys_AtomicLoad(&trailCount)
        || Sys_AtomicLoad(&system->activeTrailElemCount)
            != Sys_AtomicLoad(&trailElemCount)
        || !FxValidatePoolAllocationGraph(
            system,
            rebuilt.elems,
            rebuilt.trails,
            rebuilt.trailElems))
    {
        return false;
    }

    *states = rebuilt;
    return true;
}

[[noreturn]] void FX_DropInvalidEffectHandle(const std::uint32_t handle)
{
    Com_Error(ERR_DROP, "Invalid FX effect handle %u", handle);
    std::abort();
}

bool FX_ArchiveEffectRingIsValid(const FxSystem *const system) noexcept
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
        && allocatedEffectCount <= FX_EFFECT_LIMIT;
}

bool FX_NormalizeArchiveEffectRing(FxSystem *const system) noexcept
{
    if (!FX_ArchiveEffectRingIsValid(system))
        return false;
    const std::int32_t oldFirstActive = system->firstActiveEffect;
    const std::int32_t activeEffectCount =
        system->firstFreeEffect - oldFirstActive;
    const std::int32_t normalizedFirstActive =
        oldFirstActive & (FX_EFFECT_LIMIT - 1);
    system->firstActiveEffect = normalizedFirstActive;
    system->firstNewEffect = normalizedFirstActive + activeEffectCount;
    system->firstFreeEffect = normalizedFirstActive + activeEffectCount;
    return true;
}

bool FX_ReadArchiveDataNoDrop(
    MemoryFile *const memFile,
    const int byteCount,
    void *const data)
{
    if (!memFile || byteCount < 0 || (byteCount != 0 && !data)
        || memFile->memoryOverflow)
    {
        return false;
    }

    const bool errorOnOverflow = memFile->errorOnOverflow;
    memFile->errorOnOverflow = false;
    MemFile_ReadData(
        memFile,
        byteCount,
        static_cast<std::uint8_t *>(data));
    memFile->errorOnOverflow = errorOnOverflow;
    return !memFile->memoryOverflow;
}

bool FX_WriteArchiveDataNoDrop(
    MemoryFile *const memFile,
    const int byteCount,
    const void *const data)
{
    if (!memFile || byteCount < 0 || (byteCount != 0 && !data)
        || memFile->memoryOverflow)
    {
        return false;
    }

    const bool errorOnOverflow = memFile->errorOnOverflow;
    memFile->errorOnOverflow = false;
    MemFile_WriteData(memFile, byteCount, data);
    memFile->errorOnOverflow = errorOnOverflow;
    return !memFile->memoryOverflow;
}

const FxEffectDef *FX_FindEffectDefInTableNoDrop(
    const FxEffectDefTable *const table,
    const std::uint32_t key) noexcept
{
    if (!table || table->count < 0 || table->count > 1024)
        return nullptr;

    for (std::int32_t index = 0; index < table->count; ++index)
    {
        if (table->entries[index].key == key)
            return table->entries[index].effectDef;
    }
    return nullptr;
}

bool FX_FixupEffectDefHandlesNoDrop(
    FxSystem *const system,
    const FxEffectDefTable *const table) noexcept
{
    if (!system || !table || !system->isArchiving
        || !FX_ArchiveEffectRingIsValid(system))
    {
        return false;
    }

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
        if (!effect)
            return false;

        const std::uint32_t key = FX_ArchivePointerBits(effect->def);
        const FxEffectDef *const effectDef =
            FX_FindEffectDefInTableNoDrop(table, key);
        if (!effectDef)
            return false;
        effect->def = effectDef;
    }
    return true;
}

bool FX_GetArchiveEffectDefCount(
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

bool FX_ValidateArchiveTimeRange(const FxIntRange &range) noexcept
{
    // Sampling uses (amplitude + 1) * uint16_random in signed int math.
    // The amplitude cap keeps that multiplication representable as well as
    // bounding the resulting delay/lifespan to the archive time horizon.
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

bool FX_ValidateArchiveOneShotCount(const FxIntRange &range) noexcept
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

bool FX_ValidateArchiveEffectDefTiming(
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
        const FxElemDef &elemDef = def->elemDefs[index];
        if (!FX_ValidateArchiveTimeRange(elemDef.spawnDelayMsec)
            || !FX_ValidateArchiveTimeRange(elemDef.lifeSpanMsec))
        {
            return false;
        }

        if (index < static_cast<std::size_t>(def->elemDefCountLooping))
        {
            const std::int32_t interval =
                elemDef.spawn.looping.intervalMsec;
            const std::int32_t count = elemDef.spawn.looping.count;
            if (interval < 0
                || (interval == 0
                    && elemDef.elemType != FX_ELEM_TYPE_TRAIL)
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
        else if (!FX_ValidateArchiveOneShotCount(
                     elemDef.spawn.oneShot.count))
        {
            return false;
        }
    }

    const std::int32_t expectedLoopingLife = hasInfiniteLoop
        ? (std::numeric_limits<std::int32_t>::max)()
        : static_cast<std::int32_t>(maximumLoopingLife);
    return def->msecLoopingLife == expectedLoopingLife;
}

bool FX_ArchiveElemTypeMatchesClass(
    const std::uint8_t elemType,
    const std::size_t elemClass) noexcept
{
    switch (elemClass)
    {
    case 0:
        return elemType == FX_ELEM_TYPE_SPRITE_BILLBOARD
            || elemType == FX_ELEM_TYPE_SPRITE_ORIENTED
            || elemType == FX_ELEM_TYPE_TAIL;
    case 1:
        return elemType == FX_ELEM_TYPE_MODEL
            || elemType == FX_ELEM_TYPE_OMNI_LIGHT;
    case 2:
        return elemType == FX_ELEM_TYPE_CLOUD;
    default:
        return false;
    }
}

bool FX_ValidateArchiveBodyState(const BodyState &state) noexcept
{
    std::uint32_t underwaterBits = 0;
    std::memcpy(
        &underwaterBits,
        &state.underwater,
        sizeof(std::uint32_t));
    const bool frictionValid =
        (FX_ArchiveFloatIsBounded(
             state.friction, FX_ARCHIVE_PHYSICS_FRICTION_MAX)
            && state.friction >= 0.0f)
        // The asset format intentionally uses FLT_MAX for infinite friction.
        || state.friction == (std::numeric_limits<float>::max)();
    if (!FX_ValidateArchiveVector(
            state.position, FX_ARCHIVE_SPATIAL_COMPONENT_MAX)
        || !FX_ValidateArchiveOrthonormalBasis(state.rotation)
        || !FX_ValidateArchiveVector(
            state.velocity, FX_ARCHIVE_LINEAR_VELOCITY_MAX)
        || !FX_ValidateArchiveVector(
            state.angVelocity, FX_ARCHIVE_ANGULAR_VELOCITY_MAX)
        || !FX_ValidateArchiveVector(
            state.centerOfMassOffset,
            FX_ARCHIVE_SPATIAL_COMPONENT_MAX)
        || !FX_ArchiveFloatIsBounded(
            state.mass, FX_ARCHIVE_PHYSICS_MASS_MAX)
        || state.mass < FX_ARCHIVE_PHYSICS_MASS_MIN
        || !frictionValid
        || !FX_ArchiveFloatIsBounded(
            state.bounce, FX_ARCHIVE_PHYSICS_BOUNCE_MAX)
        || state.bounce < 0.0f
        || state.state < PHYS_OBJ_STATE_POSSIBLY_STUCK
        || state.state > PHYS_OBJ_STATE_FREE
        || state.type < 0 || state.type >= 50
        || (underwaterBits & 0xFFu) > 1u)
    {
        return false;
    }
    return true;
}

void FX_NormalizeArchiveBodyState(
    BodyState *const state,
    const std::int32_t archiveTime) noexcept
{
    if (!state)
        return;
    std::uint32_t underwaterBits = 0;
    std::memcpy(
        &underwaterBits,
        &state->underwater,
        sizeof(std::uint32_t));
    state->underwater = static_cast<int>(underwaterBits & 0xFFu);
    // timeLastAsleep belongs to the live physics clock, not the serialized
    // body's object state.  Rebasing it prevents an untrusted absolute value
    // from overflowing timeNow - timeLastAsleep on the first physics tick.
    state->timeLastAsleep = archiveTime;
}

bool FX_ValidateArchiveCamera(const FxCamera &camera) noexcept
{
    const std::int32_t isValid = Sys_AtomicLoad(&camera.isValid);
    if ((isValid != 0 && isValid != 1)
        || camera.frustumPlaneCount > 6
        || (isValid == 0 && camera.frustumPlaneCount != 0))
        return false;
    if (!FX_ValidateArchiveVector(
            camera.origin, FX_ARCHIVE_SPATIAL_COMPONENT_MAX))
    {
        return false;
    }
    for (std::size_t planeIndex = 0; planeIndex < 6; ++planeIndex)
    {
        const float (&plane)[4] = camera.frustum[planeIndex];
        if (!FX_ArchiveFloatIsBounded(plane[0], 2.0f)
            || !FX_ArchiveFloatIsBounded(plane[1], 2.0f)
            || !FX_ArchiveFloatIsBounded(plane[2], 2.0f)
            || !FX_ArchiveFloatIsBounded(
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
        if (!FX_ValidateArchiveOrthonormalBasis(camera.axis))
            return false;
    }
    else
    {
        for (const auto &row : camera.axis)
        {
            if (!FX_ValidateArchiveVector(row, 1.001f))
                return false;
        }
    }
    return FX_ValidateArchiveVector(
        camera.viewOffset, FX_ARCHIVE_SPATIAL_COMPONENT_MAX);
}

bool FX_ValidateArchiveVisibility(const FxSystem *const system) noexcept
{
    if (!system || !system->visState)
        return false;
    for (std::size_t stateIndex = 0; stateIndex < 2; ++stateIndex)
    {
        const FxVisState &state = system->visState[stateIndex];
        const std::int32_t blockerCount =
            Sys_AtomicLoad(&state.blockerCount);
        if (blockerCount < 0 || blockerCount > 256)
            return false;
        for (std::int32_t blockerIndex = 0;
             blockerIndex < blockerCount;
             ++blockerIndex)
        {
            if (!FX_ValidateArchiveVector(
                    state.blocker[blockerIndex].origin,
                    FX_ARCHIVE_SPATIAL_COMPONENT_MAX))
            {
                return false;
            }
        }
    }
    return true;
}

dxBody *FX_DecodeArchivedPhysicsBody(const int physObjId) noexcept
{
    std::uint32_t addressBits = 0;
    std::memcpy(&addressBits, &physObjId, sizeof(std::uint32_t));
    return reinterpret_cast<dxBody *>(
        static_cast<std::uintptr_t>(addressBits));
}

bool FX_EncodeArchivedPhysicsBody(
    dxBody *const body,
    int *const outPhysObjId) noexcept
{
    if (!body || !outPhysObjId)
        return false;
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(body);
    if (address > (std::numeric_limits<std::uint32_t>::max)())
        return false;
    const std::uint32_t addressBits = static_cast<std::uint32_t>(address);
    std::memcpy(outPhysObjId, &addressBits, sizeof(std::uint32_t));
    return true;
}

bool FX_ArchivePhysicsBodyIsLiveLocked(dxBody *const expectedBody) noexcept
{
    dxWorld *const world = physGlob.world[PHYS_WORLD_FX];
    if (!expectedBody || !world)
        return false;

    std::size_t bodyCount = 0;
    for (dxBody *body = world->firstbody;
         body;
         body = static_cast<dxBody *>(body->next))
    {
        if (bodyCount++ >= 512)
            return false;
        if (body == expectedBody)
            return true;
    }
    return false;
}

bool FX_GetArchiveModelGeomCount(
    const XModel *const model,
    std::size_t *const outGeomCount) noexcept
{
    if (!model || !outGeomCount)
        return false;

    if (!model->physGeoms)
    {
        *outGeomCount = 1;
        return true;
    }

    const PhysGeomList &geomList = *model->physGeoms;
    if (geomList.count > ODE_GEOM_POOL_COUNT
        || (geomList.count != 0 && !geomList.geoms))
    {
        return false;
    }

    std::size_t geomCount = 0;
    for (std::uint32_t index = 0; index < geomList.count; ++index)
    {
        // Oriented primitive collision consumes a primitive plus its transform.
        // Brushes are already expressed in model space and consume one geom.
        const std::size_t required = geomList.geoms[index].brush ? 1 : 2;
        if (geomCount > ODE_GEOM_POOL_COUNT - required)
            return false;
        geomCount += required;
    }
    *outGeomCount = geomCount;
    return true;
}

bool FX_ArchivePhysicsCapacityAvailableLocked(
    const FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount) noexcept
{
    if ((entryCount != 0 && !entries) || entryCount > 512
        || !physGlob.world[PHYS_WORLD_FX]
        || !physGlob.space[PHYS_WORLD_FX])
    {
        return false;
    }

    std::size_t requiredGeomCount = 0;
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        std::size_t modelGeomCount = 0;
        if (!FX_GetArchiveModelGeomCount(
                entries[index].model, &modelGeomCount)
            || requiredGeomCount
                > ODE_GEOM_POOL_COUNT - modelGeomCount)
        {
            return false;
        }
        requiredGeomCount += modelGeomCount;
    }

    return Pool_FreeCount(&odeGlob.bodyPool) >= entryCount
        && Pool_FreeCount(&physGlob.userDataPool) >= entryCount
        && Pool_FreeCount(&odeGlob.geomPool) >= requiredGeomCount;
}

bool FX_AppendArchivePhysicsEntry(
    FxEffect *const effect,
    FxElem *const elem,
    const FxElemDef *const elemDef,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCapacity,
    std::size_t *const entryCount) noexcept
{
    if (!effect || !elem || !elemDef || !entryCount)
        return false;
    if (elemDef->elemType != FX_ELEM_TYPE_MODEL
        || (static_cast<std::uint32_t>(elemDef->flags)
            & FX_ARCHIVE_PHYSICS_FLAG)
            == 0)
    {
        return true;
    }
    if (elemDef->visualCount == 0
        || (elemDef->visualCount > 1 && !elemDef->visuals.array)
        || *entryCount >= MAX_ELEMS
        || (entries && *entryCount >= entryCapacity))
    {
        return false;
    }

    const std::uint32_t randomSeed =
        (296u * static_cast<std::uint32_t>(elem->sequence)
            + static_cast<std::uint32_t>(elem->msecBegin)
            + static_cast<std::uint32_t>(effect->randomSeed))
        % 0x1DFu;
    const XModel *const model =
        FX_GetElemVisuals(elemDef, static_cast<std::int32_t>(randomSeed)).model;
    if (!model)
        return false;

    if (entries)
    {
        for (std::size_t previous = 0; previous < *entryCount; ++previous)
        {
            if (entries[previous].originalPhysObjId == elem->physObjId)
                return false;
        }
        FxArchivePhysicsEntry &entry = entries[*entryCount];
        std::memset(&entry, 0, sizeof(entry));
        entry.elem = elem;
        entry.model = model;
        entry.originalPhysObjId = elem->physObjId;
    }
    ++*entryCount;
    return true;
}

bool FX_CollectArchivePhysicsEntries(
    FxSystem *const system,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCapacity,
    std::size_t *const outEntryCount,
    const bool captureStates,
    std::int16_t *const outSpotLightBoltDobj) noexcept
{
    if (!system || !outEntryCount || !FX_ArchiveEffectRingIsValid(system)
        || !system->effects || !system->elems || !system->trails
        || !system->trailElems)
    {
        return false;
    }
    if (!system->isInitialized || system->localClientNum != 0
        || system->msecNow < 0 || system->msecDraw < -1
        || Sys_AtomicLoad(&system->deferredElemCount) != 0
        || system->sprite.indexCount != 0
        || !FX_ArchiveTimeDifferenceFits(
            system->msecNow, system->msecDraw)
        || !FX_ValidateArchiveCamera(system->camera)
        || !FX_ValidateArchiveCamera(system->cameraPrev)
        || !FX_ValidateArchiveVisibility(system))
    {
        return false;
    }

    std::size_t entryCount = 0;
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
        if (!effect || !FX_ValidateArchiveEffectRuntime(system, effect)
            || !FX_GetArchiveEffectDefCount(effect, &elemDefCount)
            || !FX_ValidateArchiveEffectDefTiming(
                effect->def, elemDefCount))
            return false;
        std::size_t spotLightDefCount = 0;
        for (std::size_t elemDefIndex = 0;
             elemDefIndex < elemDefCount;
             ++elemDefIndex)
        {
            const std::uint8_t elemType =
                effect->def->elemDefs[elemDefIndex].elemType;
            if (elemType >= FX_ELEM_TYPE_COUNT)
                return false;
            if (elemType == FX_ELEM_TYPE_SPOT_LIGHT
                && ++spotLightDefCount > 1)
            {
                return false;
            }
        }

        for (std::size_t elemClass = 0; elemClass < 3; ++elemClass)
        {
            std::uint16_t elemHandle = effect->firstElemHandle[elemClass];
            std::size_t chainLength = 0;
            while (elemHandle != FX_ARCHIVE_INVALID_HANDLE)
            {
                if (chainLength++ == MAX_ELEMS)
                    return false;
                FxPool<FxElem> *const remoteElem = FxDecodeHandle<
                    FxPool<FxElem>, MAX_ELEMS, FxElem::HANDLE_SCALE>(
                        system->elems, elemHandle);
                if (!remoteElem || remoteElem->item.defIndex >= elemDefCount)
                    return false;
                FxElem *const elem = &remoteElem->item;
                const FxElemDef *const elemDef =
                    &effect->def->elemDefs[elem->defIndex];
                if (!FX_ValidateArchiveElemRuntime(
                        system, effect, elem, elemDef)
                    || !FX_ArchiveElemTypeMatchesClass(
                        elemDef->elemType, elemClass)
                    || !FX_AppendArchivePhysicsEntry(
                        effect,
                        elem,
                        elemDef,
                        entries,
                        entryCapacity,
                        &entryCount))
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
            const FxElemDef &trailDef =
                effect->def->elemDefs[remoteTrail->item.defIndex];
            if (trailDef.elemType != FX_ELEM_TYPE_TRAIL
                || !trailDef.trailDef
                || trailDef.trailDef->splitDist <= 0
                || trailDef.trailDef->repeatDist <= 0
                || trailDef.trailDef->splitDist
                    > FX_ARCHIVE_DISTANCE_INTEGER_MAX
                || trailDef.trailDef->repeatDist
                    > FX_ARCHIVE_DISTANCE_INTEGER_MAX
                || (remoteTrail->item.defIndex
                        >= effect->def->elemDefCountLooping
                    && remoteTrail->item.defIndex
                        < effect->def->elemDefCountLooping
                            + effect->def->elemDefCountOneShot))
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
                    || !FX_ArchiveTimeDifferenceFits(
                        system->msecNow,
                        remoteTrailElem->item.msecBegin)
                    || !FX_ArchiveTimeDifferenceFits(
                        system->msecDraw,
                        remoteTrailElem->item.msecBegin)
                    || !FX_ValidateArchiveSampledLifespan(
                        effect,
                        &trailDef,
                        remoteTrailElem->item.msecBegin,
                        remoteTrailElem->item.sequence))
                {
                    return false;
                }
                if (!FX_ValidateArchiveVector(
                        remoteTrailElem->item.origin,
                        FX_ARCHIVE_SPATIAL_COMPONENT_MAX)
                    || !FX_ArchiveFloatIsBounded(
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
            || !FX_GetArchiveEffectDefCount(
                spotLightEffect, &spotLightEffectDefCount)
            || !FX_ValidateArchiveEffectRuntime(
                system, spotLightEffect)
            || !FX_ValidateArchiveEffectDefTiming(
                spotLightEffect->def, spotLightEffectDefCount))
        {
            return false;
        }
        bool hasSpotLightDefinition = false;
        for (std::size_t elemDefIndex = 0;
             elemDefIndex < spotLightEffectDefCount;
             ++elemDefIndex)
        {
            if (spotLightEffect->def->elemDefs[elemDefIndex].elemType
                == FX_ELEM_TYPE_SPOT_LIGHT)
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
            || remoteElem->item.defIndex >= spotLightEffectDefCount
            || spotLightEffect->def
                    ->elemDefs[remoteElem->item.defIndex].elemType
                != FX_ELEM_TYPE_SPOT_LIGHT
            || !FX_ValidateArchiveElemRuntime(
                system,
                spotLightEffect,
                &remoteElem->item,
                &spotLightEffect->def
                    ->elemDefs[remoteElem->item.defIndex])
            || remoteElem->item.nextElemHandleInEffect
                != FX_ARCHIVE_INVALID_HANDLE
            || remoteElem->item.prevElemHandleInEffect
                != FX_ARCHIVE_INVALID_HANDLE)
        {
            return false;
        }
    }

    if (outSpotLightBoltDobj)
    {
        *outSpotLightBoltDobj = spotLightEffect
                && spotLightEffect->boltAndSortOrder.boneIndex
                    != FX_BONE_INDEX_NONE
            ? static_cast<std::int16_t>(
                spotLightEffect->boltAndSortOrder.dobjHandle)
            : static_cast<std::int16_t>(-1);
    }

    if (captureStates)
    {
        if (entryCount != 0 && !entries)
            return false;
        bool physicsStatesValid = true;
        Sys_EnterCriticalSection(CRITSECT_PHYSICS);
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            dxBody *const body = FX_DecodeArchivedPhysicsBody(
                entries[index].originalPhysObjId);
            if (!FX_ArchivePhysicsBodyIsLiveLocked(body))
            {
                physicsStatesValid = false;
                break;
            }
            Phys_GetStateFromBody(body, &entries[index].state);
        }
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
        if (!physicsStatesValid)
            return false;
        for (std::size_t index = 0; index < entryCount; ++index)
        {
            if (!FX_ValidateArchiveBodyState(entries[index].state))
                return false;
            FX_NormalizeArchiveBodyState(
                &entries[index].state, system->msecNow);
        }
    }

    *outEntryCount = entryCount;
    return true;
}

void FX_DestroyCreatedArchivePhysics(
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount) noexcept
{
    if (!entries)
        return;
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        if (entries[index].createdBody)
        {
            Phys_ObjDestroy(PHYS_WORLD_FX, entries[index].createdBody);
            entries[index].createdBody = nullptr;
        }
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
}

bool FX_CaptureReplacedArchivePhysics(
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount) noexcept
{
    if (entryCount != 0 && !entries)
        return false;
    bool valid = true;
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        entries[index].replacedBody =
            entries[index].elem
                ? FX_DecodeArchivedPhysicsBody(
                    entries[index].elem->physObjId)
                : nullptr;
        if (!FX_ArchivePhysicsBodyIsLiveLocked(
                entries[index].replacedBody))
        {
            valid = false;
            break;
        }
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (entries[previous].replacedBody
                == entries[index].replacedBody)
            {
                valid = false;
                break;
            }
        }
        if (!valid)
            break;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    return valid;
}

void FX_DestroyReplacedArchivePhysics(
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount) noexcept
{
    if (!entries)
        return;
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        if (entries[index].replacedBody)
        {
            Phys_ObjDestroy(PHYS_WORLD_FX, entries[index].replacedBody);
            entries[index].replacedBody = nullptr;
        }
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
}

bool FX_CreateArchivePhysics(
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    const std::int32_t archiveTime) noexcept
{
    if (entryCount != 0 && !entries)
        return false;
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        FxArchivePhysicsEntry &entry = entries[index];
        if (!entry.elem || !entry.model
            || !FX_ValidateArchiveBodyState(entry.state))
        {
            return false;
        }
        FX_NormalizeArchiveBodyState(&entry.state, archiveTime);
        entry.createdBody = nullptr;
    }

    bool created = true;
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const std::int32_t physicsTime =
        physGlob.worldData[PHYS_WORLD_FX].timeLastUpdate;
    if ((entryCount != 0
            && !FX_ArchiveTimeDifferenceFits(archiveTime, physicsTime))
        || !FX_ArchivePhysicsCapacityAvailableLocked(entries, entryCount))
    {
        created = false;
    }
    for (std::size_t index = 0; index < entryCount && created; ++index)
    {
        FxArchivePhysicsEntry &entry = entries[index];
        FX_NormalizeArchiveBodyState(&entry.state, physicsTime);
        const PhysBodyModelCreateStatus bodyStatus =
            Phys_TryCreateBodyFromStateAndXModel(
                PHYS_WORLD_FX,
                &entry.state,
                entry.model,
                &entry.createdBody);
        int encodedBody = 0;
        const bool bodyEncoded =
            bodyStatus == PhysBodyModelCreateStatus::Success
            && FX_EncodeArchivedPhysicsBody(
                entry.createdBody, &encodedBody);
        if (!bodyEncoded)
            created = false;
        else
            entry.elem->physObjId = encodedBody;
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    return created;
}
}

void __cdecl FX_Restore(int32_t clientIndex, MemoryFile *memFile)
{
    if (sizeof(void *) != 4)
    {
        Com_Error(ERR_DROP, "FX archive restore requires Disk32 conversion on 64-bit targets");
        return;
    }
    if (sizeof(FxSystem) != FX_ARCHIVE_SYSTEM_SIZE
        || sizeof(FxSystemBuffers) != FX_ARCHIVE_BUFFER_SIZE)
    {
        Com_Error(ERR_DROP, "FX archive restore ABI does not match the legacy format");
        return;
    }
    if (!memFile || clientIndex != 0)
    {
        Com_Error(ERR_DROP, "Invalid FX archive restore request");
        return;
    }

    FxSystem *const system = FX_GetSystem(clientIndex);
    if (!system)
    {
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 220, 0, "%s", "system");
        Com_Error(ERR_DROP, "Missing FX system while restoring archive");
        return;
    }
    const std::uintptr_t currentSystemAddress =
        reinterpret_cast<std::uintptr_t>(system);
    if (currentSystemAddress
        > (std::numeric_limits<std::uint32_t>::max)())
    {
        Com_Error(ERR_DROP, "FX archive restore requires Disk32 conversion on this target");
        return;
    }
    FxSystemBuffers *const systemBuffers = FX_GetSystemBuffers(clientIndex);
    if (!systemBuffers)
    {
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 223, 0, "%s", "systemBuffers");
        Com_Error(ERR_DROP, "Missing FX system buffers while restoring archive");
        return;
    }

    // Effect registration may report a malformed table. Keep it ahead of all
    // heap staging so an engine-level ERR_DROP cannot leak staged resources.
    FxEffectDefTable table{};
    FX_RestoreEffectDefTable(memFile, &table);
    if (memFile->memoryOverflow)
    {
        Com_Error(ERR_DROP, "Invalid FX effect-definition table in archive");
        return;
    }

    FxSystemBuffers *const restoredBuffers =
        static_cast<FxSystemBuffers *>(Z_Malloc(
            static_cast<int>(sizeof(FxSystemBuffers)),
            "FX_Restore system buffers",
            10));
    if (!restoredBuffers)
    {
        Com_Error(ERR_DROP, "Unable to allocate FX restore staging buffers");
        return;
    }
    memset(restoredBuffers, 0, sizeof(*restoredBuffers));

    alignas(FxSystem) std::uint8_t
        restoredSystemBytes[FX_ARCHIVE_SYSTEM_SIZE]{};
    if (!FX_ReadArchiveDataNoDrop(
            memFile,
            FX_ARCHIVE_SYSTEM_SIZE,
            restoredSystemBytes)
        || !FX_ValidateArchiveSystemBooleanBytes(
            restoredSystemBytes, sizeof(restoredSystemBytes)))
    {
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid save file");
        return;
    }

    // Do not form a typed FxSystem until every serialized bool has a valid
    // object representation.  Reading an arbitrary byte through bool is UB.
    FxSystem restoredSystem{};
    std::memcpy(
        &restoredSystem,
        restoredSystemBytes,
        sizeof(restoredSystemBytes));
    if (!restoredSystem.isInitialized || !restoredSystem.isArchiving
        || restoredSystem.localClientNum != 0
        || Sys_AtomicLoad(&restoredSystem.iteratorCount) != 0)
    {
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid save file");
        return;
    }
    if (restoredSystem.frameCount <= 0
        || restoredSystem.frameCount
            >= (std::numeric_limits<std::int32_t>::max)() - 1)
    {
        // FX_SetNextUpdateTime pre-increments this signed counter.
        restoredSystem.frameCount = 1;
    }
    if (!FX_NormalizeArchiveEffectRing(&restoredSystem))
    {
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid FX effect ring in archive");
        return;
    }
    FX_LinkSystemBuffers(&restoredSystem, restoredBuffers);
    if (!FX_ReadArchiveDataNoDrop(
            memFile,
            FX_ARCHIVE_BUFFER_SIZE,
            restoredBuffers))
    {
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Truncated FX pool data in archive");
        return;
    }

    FxArchivePoolAllocationStates restoredStates{};
    if (!FX_RebuildArchivePoolAllocationStates(
            &restoredSystem, &restoredStates))
    {
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid FX pool state in archive");
        return;
    }
    if (!FX_FixupEffectDefHandlesNoDrop(&restoredSystem, &table))
    {
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid FX effect definition in archive");
        return;
    }
    for (std::int64_t activeIndex = restoredSystem.firstActiveEffect;
         activeIndex < restoredSystem.firstFreeEffect;
         ++activeIndex)
    {
        FxEffect *const effect = FxDecodeHandle<
            FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                restoredSystem.effects,
                restoredSystem.allEffectHandles[
                    static_cast<std::size_t>(activeIndex)
                    & (MAX_EFFECTS - 1)]);
        if (!effect)
        {
            Z_Free(restoredBuffers, 10);
            Com_Error(ERR_DROP, "Invalid FX effect frame state in archive");
            return;
        }
        Sys_AtomicStore(&effect->frameCount, 0);
    }

    std::size_t physicsEntryCount = 0;
    std::int16_t restoredSpotLightBoltDobj = -1;
    if (!FX_CollectArchivePhysicsEntries(
            &restoredSystem,
            nullptr,
            0,
            &physicsEntryCount,
            false,
            &restoredSpotLightBoltDobj))
    {
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid FX element semantics in archive");
        return;
    }
    restoredSystem.activeSpotLightBoltDobj =
        restoredSpotLightBoltDobj;
    FxArchivePhysicsEntry *physicsEntries = nullptr;
    if (physicsEntryCount != 0)
    {
        physicsEntries = static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
            static_cast<int>(
                physicsEntryCount * sizeof(FxArchivePhysicsEntry)),
            "FX_Restore physics staging",
            10));
        if (!physicsEntries)
        {
            Z_Free(restoredBuffers, 10);
            Com_Error(ERR_DROP, "Unable to allocate FX physics restore staging");
            return;
        }
        std::memset(
            physicsEntries,
            0,
            physicsEntryCount * sizeof(FxArchivePhysicsEntry));
        std::size_t populatedEntryCount = 0;
        std::int16_t populatedSpotLightBoltDobj = -1;
        if (!FX_CollectArchivePhysicsEntries(
                &restoredSystem,
                physicsEntries,
                physicsEntryCount,
                &populatedEntryCount,
                false,
                &populatedSpotLightBoltDobj)
            || populatedEntryCount != physicsEntryCount
            || populatedSpotLightBoltDobj
                != restoredSpotLightBoltDobj)
        {
            Z_Free(physicsEntries, 10);
            Z_Free(restoredBuffers, 10);
            Com_Error(ERR_DROP, "Unstable FX physics state in archive");
            return;
        }
    }

    std::uint32_t archivedSystemAddress = 0;
    if (!FX_ReadArchiveDataNoDrop(
            memFile,
            static_cast<int>(sizeof(archivedSystemAddress)),
            &archivedSystemAddress)
        || archivedSystemAddress == 0)
    {
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid FX relocation record in archive");
        return;
    }
    const std::uint32_t relocationBits =
        static_cast<std::uint32_t>(currentSystemAddress)
        - archivedSystemAddress;
    const FxVisState *const firstLiveVisState = &systemBuffers->visState[0];
    const FxVisState *const secondLiveVisState = &systemBuffers->visState[1];
    const std::uint32_t archivedReadAddress = FX_ArchivePointerBits(
        restoredSystem.visStateBufferRead);
    const std::uint32_t archivedWriteAddress = FX_ArchivePointerBits(
        restoredSystem.visStateBufferWrite);
    const std::uint32_t relocatedReadAddress =
        archivedReadAddress + relocationBits;
    const std::uint32_t relocatedWriteAddress =
        archivedWriteAddress + relocationBits;
    const std::uint32_t firstLiveVisAddress =
        FX_ArchivePointerBits(firstLiveVisState);
    const std::uint32_t secondLiveVisAddress =
        FX_ArchivePointerBits(secondLiveVisState);
    const bool readVisStateValid = relocatedReadAddress
            == firstLiveVisAddress
        || relocatedReadAddress == secondLiveVisAddress;
    const bool writeVisStateValid = relocatedWriteAddress
            == firstLiveVisAddress
        || relocatedWriteAddress == secondLiveVisAddress;
    if (!readVisStateValid || !writeVisStateValid
        || relocatedReadAddress == relocatedWriteAddress)
    {
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid FX visibility buffers in archive");
        return;
    }
    restoredSystem.visStateBufferRead =
        reinterpret_cast<const FxVisState *>(
            static_cast<std::uintptr_t>(relocatedReadAddress));
    restoredSystem.visStateBufferWrite =
        reinterpret_cast<FxVisState *>(
            static_cast<std::uintptr_t>(relocatedWriteAddress));

    bool physicsDataValid = true;
    for (std::size_t index = 0;
         index < physicsEntryCount;
         ++index)
    {
        if (!FX_ReadArchiveDataNoDrop(
                memFile,
                FX_ARCHIVE_BODY_STATE_SIZE,
                &physicsEntries[index].state)
            || !FX_ValidateArchiveBodyState(
                physicsEntries[index].state))
        {
            physicsDataValid = false;
            break;
        }
    }
    if (!physicsDataValid)
    {
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid FX physics state in archive");
        return;
    }

    FxArchivePhysicsEntry *const replacedPhysicsEntries =
        static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
            static_cast<int>(
                MAX_ELEMS * sizeof(FxArchivePhysicsEntry)),
            "FX_Restore replaced physics staging",
            10));
    if (!replacedPhysicsEntries)
    {
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Unable to allocate current FX physics staging");
        return;
    }
    std::memset(
        replacedPhysicsEntries,
        0,
        MAX_ELEMS * sizeof(FxArchivePhysicsEntry));

    FxSystemBuffers *const rollbackBuffers =
        static_cast<FxSystemBuffers *>(Z_Malloc(
            static_cast<int>(sizeof(FxSystemBuffers)),
            "FX_Restore rollback buffers",
            10));
    if (!rollbackBuffers)
    {
        Z_Free(replacedPhysicsEntries, 10);
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Unable to allocate FX restore rollback state");
        return;
    }

    if (!FX_BeginArchive(system))
    {
        Z_Free(rollbackBuffers, 10);
        Z_Free(replacedPhysicsEntries, 10);
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "FX archive restore could not acquire exclusive ownership");
        return;
    }

    // Nothing below this point performs archive I/O or reports ERR_DROP. New
    // physics bodies remain private to restoredBuffers until every body is
    // ready. The old bodies are retained until the validated state is copied.
    FxSystem rollbackSystem{};
    Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    std::memcpy(&rollbackSystem, system, sizeof(rollbackSystem));
    std::memcpy(rollbackBuffers, systemBuffers, sizeof(*rollbackBuffers));
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    // Both publication paths restore exclusive ownership through the checked
    // iterator helper after copying their staged image.  The live snapshot was
    // taken while this thread owned -1, so normalize its staged word first;
    // otherwise a rollback would try to acquire an already-negative copy and
    // falsely report that the old state could not be restored.
    Sys_AtomicStore(&rollbackSystem.iteratorCount, 0);

    std::size_t replacedPhysicsEntryCount = 0;
    bool restoreReady = FX_ArchiveEffectRingIsValid(system)
        && FX_ValidatePoolAllocationGraphState(system)
        && FX_CollectArchivePhysicsEntries(
            system,
            replacedPhysicsEntries,
            MAX_ELEMS,
            &replacedPhysicsEntryCount,
            false,
            nullptr)
        && FX_CaptureReplacedArchivePhysics(
            replacedPhysicsEntries,
            replacedPhysicsEntryCount)
        && FX_CreateArchivePhysics(
            physicsEntries,
            physicsEntryCount,
            restoredSystem.msecNow);
    bool statePublished = false;
    bool validCommittedState = false;
    bool rollbackValid = true;
    if (restoreReady)
    {
        restoredSystem.isArchiving = false;
        Sys_AtomicStore(&restoredSystem.iteratorCount, 0);

        Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
        memcpy(systemBuffers, restoredBuffers, sizeof(*systemBuffers));
        memcpy(system, &restoredSystem, sizeof(*system));
        FX_LinkSystemBuffers(system, systemBuffers);
        const bool restoredExclusiveState =
            FX_RestoreArchiveExclusiveState(system);
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        statePublished = true;

        validCommittedState = restoredExclusiveState
            && FX_RebuildPoolAllocationStates(system)
            && FX_ValidatePoolAllocationGraphState(system);
        if (validCommittedState)
        {
            // Publication transferred ownership of the new bodies to a fully
            // validated live graph. Only now are the old bodies unreachable.
            FX_DestroyReplacedArchivePhysics(
                replacedPhysicsEntries,
                replacedPhysicsEntryCount);
        }
        else
        {
            // Rebuild/graph validation is expected to be deterministic after
            // staging, but retain the complete old image so publication still
            // has a real rollback boundary if an invariant changes.
            Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
            std::memcpy(systemBuffers, rollbackBuffers, sizeof(*systemBuffers));
            std::memcpy(system, &rollbackSystem, sizeof(*system));
            FX_LinkSystemBuffers(system, systemBuffers);
            const bool restoredRollbackExclusiveState =
                FX_RestoreArchiveExclusiveState(system);
            Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
            rollbackValid = restoredRollbackExclusiveState
                && FX_RebuildPoolAllocationStates(system)
                && FX_ValidatePoolAllocationGraphState(system);
            FX_DestroyCreatedArchivePhysics(
                physicsEntries, physicsEntryCount);
        }
    }
    if (!statePublished)
        FX_DestroyCreatedArchivePhysics(physicsEntries, physicsEntryCount);
    system->isArchiving = false;
    const bool releasedArchive = FX_EndArchive(system);
    Z_Free(rollbackBuffers, 10);
    Z_Free(replacedPhysicsEntries, 10);
    if (physicsEntries)
        Z_Free(physicsEntries, 10);
    Z_Free(restoredBuffers, 10);
    if (!restoreReady || !validCommittedState || !rollbackValid
        || !releasedArchive)
    {
        Com_Error(ERR_DROP, "Unable to publish restored FX archive state");
        return;
    }
}

void __cdecl FX_RestoreEffectDefTable(MemoryFile *memFile, FxEffectDefTable *table)
{
    uint32_t p; // [esp+0h] [ebp-10h] BYREF
    const FxEffectDef *effectDef; // [esp+4h] [ebp-Ch]
    uint32_t key; // [esp+8h] [ebp-8h]
    const char *effectDefName; // [esp+Ch] [ebp-4h]

    table->count = 0;
    while (1)
    {
        effectDefName = MemFile_ReadCString(memFile);
        if (!*effectDefName)
            break;
        if (!FX_ArchiveEffectNameIsValid(effectDefName))
        {
            Com_Error(ERR_DROP, "Invalid FX effect name in archive");
            return;
        }
        if (table->count >= 1024)
        {
            Com_Error(ERR_DROP, "FX effect-definition table exceeds capacity");
            return;
        }
        MemFile_ReadData(memFile, 4, (uint8_t *)&p);
        key = p;
        effectDef = FX_Register((char *)effectDefName);
        FX_AddEffectDefTableEntry(table, key, effectDef);
    }
}

void __cdecl FX_AddEffectDefTableEntry(FxEffectDefTable *table, uint32_t key, const FxEffectDef *effectDef)
{
    if (!table)
    {
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 47, 0, "%s", "table");
        Com_Error(ERR_DROP, "Missing FX effect-definition table");
        return;
    }
    if (table->count < 0 || table->count >= 1024)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_archive.cpp",
            48,
            0,
            "table->count doesn't index ARRAY_COUNT( table->entries )\n\t%i not in [0, %i)",
            table->count,
            1024);
        Com_Error(ERR_DROP, "FX effect-definition table exceeds capacity");
        return;
    }
    if (!effectDef)
    {
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 49, 0, "%s", "effectDef");
        Com_Error(ERR_DROP, "Missing FX effect definition while restoring");
        return;
    }
    table->entries[table->count].key = key;
    table->entries[table->count++].effectDef = effectDef;
}

bool __cdecl FX_FixupEffectDefHandles(FxSystem *system, FxEffectDefTable *table)
{
    if (!system || !table)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_archive.cpp",
            131,
            0,
            "%s",
            "system && table");
        Com_Error(ERR_DROP, "Missing FX archive fixup state");
        return false;
    }
    if (!system->isArchiving)
    {
        MyAssertHandler(".\\EffectsCore\\fx_archive.cpp", 132, 0, "%s", "system->isArchiving");
        Com_Error(ERR_DROP, "FX archive fixup requires archive mode");
        return false;
    }
    if (!FX_FixupEffectDefHandlesNoDrop(system, table))
    {
        Com_Error(ERR_DROP, "Invalid FX effect definition fixup state");
        return false;
    }
    return true;
}

FxEffect *__cdecl FX_EffectFromHandle(FxSystem *system, uint16_t handle)
{
    if (!system)
    {
        MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 256, 0, "%s", "system");
        FX_DropInvalidEffectHandle(handle);
    }

    FxEffect *const effect =
        FxDecodeHandle<FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, handle);
    if (!effect)
    {
        const char *const context = va("%p %i", system->effects, handle);
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            257,
            0,
            "%s\n\t%s",
            "handle < FX_EFFECT_LIMIT * sizeof( FxEffect ) / FxEffect::HANDLE_SCALE && handle % (sizeof( FxEffect ) / FxEffect:"
            ":HANDLE_SCALE) == 0",
            context);
        FX_DropInvalidEffectHandle(handle);
    }
    return effect;
}

const FxEffectDef *__cdecl FX_FindEffectDefInTable(const FxEffectDefTable *table, uint32_t key)
{
    if (!table || table->count < 0 || table->count > 1024)
    {
        Com_Error(ERR_DROP, "Invalid FX effect-definition table while restoring");
        return nullptr;
    }
    return FX_FindEffectDefInTableNoDrop(table, key);
}

FxElemVisuals __cdecl FX_GetElemVisuals(const FxElemDef *elemDef, int32_t randomSeed)
{
    if (!elemDef->visualCount)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_draw.h",
            79,
            0,
            "%s\n\t(elemDef->visualCount) = %i",
            "(elemDef->visualCount > 0)",
            elemDef->visualCount);
    if (elemDef->visualCount == 1)
        return elemDef->visuals.instance;
    else
        return (FxElemVisuals)elemDef->visuals.markArray->materials[(elemDef->visualCount
            * LOWORD(fx_randomTable[randomSeed + 21])) >> 16];
}

void __cdecl FX_Save(int32_t clientIndex, MemoryFile *memFile)
{
    if (sizeof(void *) != 4)
    {
        Com_Error(ERR_DROP, "FX archive save requires Disk32 conversion on 64-bit targets");
        return;
    }
    if (sizeof(FxSystem) != FX_ARCHIVE_SYSTEM_SIZE
        || sizeof(FxSystemBuffers) != FX_ARCHIVE_BUFFER_SIZE)
    {
        Com_Error(ERR_DROP, "FX archive save ABI does not match the legacy format");
        return;
    }
    if (!memFile || clientIndex != 0)
    {
        Com_Error(ERR_DROP, "Invalid FX archive save request");
        return;
    }
    FxSystem *const system = FX_GetSystem(clientIndex);
    FxSystemBuffers *const systemBuffers = FX_GetSystemBuffers(clientIndex);
    if (!system || !systemBuffers)
    {
        Com_Error(ERR_DROP, "Missing FX state while saving archive");
        return;
    }
    const std::uintptr_t systemAddress =
        reinterpret_cast<std::uintptr_t>(system);
    if (systemAddress > (std::numeric_limits<std::uint32_t>::max)())
    {
        Com_Error(ERR_DROP, "FX archive save requires Disk32 conversion on this target");
        return;
    }

    FX_SaveEffectDefTable(system, memFile);
    if (memFile->memoryOverflow)
    {
        Com_Error(ERR_DROP, "FX effect-definition archive ran out of memory");
        return;
    }

    FxSystemBuffers *const bufferSnapshot =
        static_cast<FxSystemBuffers *>(Z_Malloc(
            static_cast<int>(sizeof(FxSystemBuffers)),
            "FX_Save system buffers",
            10));
    if (!bufferSnapshot)
    {
        Com_Error(ERR_DROP, "Unable to allocate FX save staging buffers");
        return;
    }

    FxArchivePhysicsEntry *const physicsEntries =
        static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
            static_cast<int>(
                MAX_ELEMS * sizeof(FxArchivePhysicsEntry)),
            "FX_Save physics staging",
            10));
    if (!physicsEntries)
    {
        Z_Free(bufferSnapshot, 10);
        Com_Error(ERR_DROP, "Unable to allocate FX physics save staging");
        return;
    }
    std::memset(
        physicsEntries,
        0,
        MAX_ELEMS * sizeof(FxArchivePhysicsEntry));

    if (!FX_BeginArchive(system))
    {
        Z_Free(physicsEntries, 10);
        Z_Free(bufferSnapshot, 10);
        Com_Error(ERR_DROP, "FX archive save could not acquire exclusive ownership");
        return;
    }
    system->isArchiving = 1;

    std::size_t physicsEntryCount = 0;
    std::int16_t snapshotSpotLightBoltDobj = -1;
    const bool snapshotValid = FX_ArchiveEffectRingIsValid(system)
        && FX_ValidatePoolAllocationGraphState(system)
        && FX_CollectArchivePhysicsEntries(
            system,
            physicsEntries,
            MAX_ELEMS,
            &physicsEntryCount,
            true,
            &snapshotSpotLightBoltDobj);
    if (!snapshotValid)
    {
        system->isArchiving = 0;
        const bool releasedArchive = FX_EndArchive(system);
        Z_Free(physicsEntries, 10);
        Z_Free(bufferSnapshot, 10);
        Com_Error(
            ERR_DROP,
            releasedArchive
                ? "Invalid FX state while saving archive"
                : "Unable to release invalid FX archive snapshot");
        return;
    }

    FxSystem systemSnapshot{};
    Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    memcpy(&systemSnapshot, system, sizeof(systemSnapshot));
    memcpy(bufferSnapshot, systemBuffers, sizeof(*bufferSnapshot));
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (!FX_NormalizeArchiveEffectRing(&systemSnapshot))
    {
        system->isArchiving = 0;
        const bool releasedArchive = FX_EndArchive(system);
        Z_Free(physicsEntries, 10);
        Z_Free(bufferSnapshot, 10);
        Com_Error(
            ERR_DROP,
            releasedArchive
                ? "Invalid FX effect ring during archive save"
                : "Unable to release invalid FX effect ring snapshot");
        return;
    }
    Sys_AtomicStore(&systemSnapshot.iteratorCount, 0);
    systemSnapshot.activeSpotLightBoltDobj =
        snapshotSpotLightBoltDobj;
    if (systemSnapshot.frameCount <= 0
        || systemSnapshot.frameCount
            >= (std::numeric_limits<std::int32_t>::max)() - 1)
    {
        systemSnapshot.frameCount = 1;
    }
    for (std::size_t effectIndex = 0;
         effectIndex < MAX_EFFECTS;
         ++effectIndex)
    {
        Sys_AtomicStore(
            &bufferSnapshot->effects[effectIndex].frameCount, 0);
    }

    system->isArchiving = 0;
    const bool releasedArchive = FX_EndArchive(system);
    if (!releasedArchive)
    {
        Z_Free(physicsEntries, 10);
        Z_Free(bufferSnapshot, 10);
        Com_Error(ERR_DROP, "Unable to release FX archive snapshot");
        return;
    }

    // The live graph and iterator gate are released before any potentially
    // failing archive write. Overflow reporting is deferred until all heap
    // staging has been freed.
    bool archiveWritten = FX_WriteArchiveDataNoDrop(
        memFile, FX_ARCHIVE_SYSTEM_SIZE, &systemSnapshot);
    archiveWritten = FX_WriteArchiveDataNoDrop(
        memFile, FX_ARCHIVE_BUFFER_SIZE, bufferSnapshot)
        && archiveWritten;
    const std::uint32_t archivedSystemAddress =
        static_cast<std::uint32_t>(systemAddress);
    archiveWritten = FX_WriteArchiveDataNoDrop(
        memFile,
        static_cast<int>(sizeof(archivedSystemAddress)),
        &archivedSystemAddress)
        && archiveWritten;
    for (std::size_t index = 0;
         index < physicsEntryCount && archiveWritten;
         ++index)
    {
        archiveWritten = FX_WriteArchiveDataNoDrop(
            memFile,
            FX_ARCHIVE_BODY_STATE_SIZE,
            &physicsEntries[index].state);
    }

    Z_Free(physicsEntries, 10);
    Z_Free(bufferSnapshot, 10);
    if (!archiveWritten)
    {
        Com_Error(ERR_DROP, "FX archive snapshot ran out of memory");
        return;
    }
}

void __cdecl FX_SaveEffectDefTable(FxSystem *system, MemoryFile *memFile)
{
    if (IsFastFileLoad())
        FX_SaveEffectDefTable_FastFile(memFile);
    else
        FX_SaveEffectDefTable_LoadObj(memFile);
    MemFile_WriteCString(memFile, "");
}

void __cdecl FX_SaveEffectDefTableEntry_FileLoadObj(
    const FxEffectDef *effectDef,
    void *data)
{
    const FxEffectDef* p; // [esp+0h] [ebp-4h] BYREF
    MemoryFile *memFile = static_cast<MemoryFile *>(data);

    if (!effectDef || !memFile)
        return;
    MemFile_WriteCString(memFile, (char*)effectDef->name);
    p = effectDef;
    MemFile_WriteData(memFile, 4, &p);
}

void __cdecl FX_SaveEffectDefTableEntry_FastFile(
    XAssetHeader header,
    void *data)
{
    FX_SaveEffectDefTableEntry_FileLoadObj(header.fx, data);
}

void __cdecl FX_SaveEffectDefTable_LoadObj(MemoryFile* memFile)
{
    FX_ForEachEffectDef(FX_SaveEffectDefTableEntry_FileLoadObj, memFile);
}

void __cdecl FX_SaveEffectDefTable_FastFile(MemoryFile *memFile)
{
    if (!memFile)
        return;
    const bool errorOnOverflow = memFile->errorOnOverflow;
    memFile->errorOnOverflow = false;
    DB_EnumXAssets(
        ASSET_TYPE_FX,
        FX_SaveEffectDefTableEntry_FastFile,
        memFile,
        0);
    memFile->errorOnOverflow = errorOnOverflow;
    if (errorOnOverflow && memFile->memoryOverflow)
        Com_Error(ERR_DROP, "FX effect table archive ran out of memory");
}

void __cdecl FX_Archive(int32_t clientIndex, MemoryFile *memFile)
{
    if (!memFile)
    {
        Com_Error(ERR_DROP, "Missing memory file while archiving FX state");
        return;
    }
    if (MemFile_IsWriting(memFile))
        FX_Save(clientIndex, memFile);
    else
        FX_Restore(clientIndex, memFile);
}

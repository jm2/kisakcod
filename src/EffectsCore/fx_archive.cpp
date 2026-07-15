#include "fx_system.h"
#include "fx_archive_capacity.h"
#include "fx_archive_physics_batch_control.h"
#include "fx_archive_restore_control.h"
#include "fx_archive_restore_workspace.h"
#include "fx_effect_table_save.h"
#include "fx_effect_table_restore.h"
#include "fx_physics_sidecar.h"
#include "fx_pool.h"
#include "fx_pool_graph.h"

#include <database/database.h>

#include <physics/phys_local.h>
#include <physics/ode/odeext.h>

#include <universal/com_memory.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>

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
    PhysBodyRollbackRecipe rollbackRecipe;
    std::size_t ownerIndex;
    fx::physics::BodyToken token;
    fx::physics::BodyToken reconstructedToken;
    bool retired;
};

struct FxArchivePhysicsOwnershipScratch
{
    std::array<fx::physics::BodyToken, MAX_ELEMS> expectedTokens{};
    fx::physics::OwnershipSnapshot drainOwnership{};
    fx::physics::BodySidecarSnapshotScratch sidecar{};
};

struct FxArchiveRestorePhysicsScratch
{
    FxArchivePhysicsOwnershipScratch ownership{};
    std::array<fx::archive::PhysicsRetirementCandidate,
               fx::physics::BODY_LIMIT>
        retirementCandidates{};
    fx::archive::PhysicsRetirementPlanScratch retirementPlanner{};
    FxPoolAllocationGraphScratch poolGraph{};
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

bool FX_TryGetArchivePhysicsEntryByteCount(
    const std::size_t entryCount,
    std::size_t *const outByteCount,
    int *const outAllocationSize) noexcept
{
    if (!outByteCount || !outAllocationSize
        || entryCount > fx::physics::BODY_LIMIT
        || entryCount
            > (std::numeric_limits<std::size_t>::max)()
                / sizeof(FxArchivePhysicsEntry))
    {
        return false;
    }
    const std::size_t byteCount =
        entryCount * sizeof(FxArchivePhysicsEntry);
    if (byteCount
        > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return false;
    }
    *outByteCount = byteCount;
    *outAllocationSize = static_cast<int>(byteCount);
    return true;
}

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
    FxArchivePoolAllocationStates *const states,
    FxPoolAllocationGraphScratch *const scratch)
{
    if (!system || !states || !scratch || !system->elems || !system->trails
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
        || !FxValidatePoolAllocationGraphWithScratch(
            system,
            rebuilt.elems,
            rebuilt.trails,
            rebuilt.trailElems,
            scratch))
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
    void *const data) noexcept
{
    if (!memFile || memFile->memoryOverflow)
        return false;
    return MemFile_TryReadDataNoReport(
               memFile,
               byteCount,
               static_cast<std::uint8_t *>(data))
        == MemFileReadStatus::Success;
}

bool FX_WriteArchiveDataNoDrop(
    MemoryFile *const memFile,
    const int byteCount,
    const void *const data) noexcept
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

struct FxEffectTableSaveCapture
{
    fx::archive::EffectTableSaveSnapshot *snapshot;
    fx::archive::EffectTableSaveStatus status;
};

enum class FxEffectTableSaveOutcome : std::uint8_t
{
    Success,
    AllocationFailed,
    InvalidTable,
    WriteFailed,
};

void __cdecl FX_CaptureEffectTableEntry_LoadObj(
    const FxEffectDef *const effectDef,
    void *const data)
{
    auto *const capture =
        static_cast<FxEffectTableSaveCapture *>(data);
    if (!capture
        || capture->status
            != fx::archive::EffectTableSaveStatus::Success)
    {
        return;
    }

    const char *const name = effectDef ? effectDef->name : nullptr;
    const std::uintptr_t key =
        reinterpret_cast<std::uintptr_t>(effectDef);
    capture->status = fx::archive::AppendEffectTableSaveEntryNoReport(
        capture->snapshot,
        name,
        key);
}

void __cdecl FX_CaptureEffectTableEntry_FastFile(
    const XAssetHeader header,
    void *const data)
{
    FX_CaptureEffectTableEntry_LoadObj(header.fx, data);
}

fx::archive::EffectTableSaveStatus FX_CaptureEffectTableNoReport(
    fx::archive::EffectTableSaveSnapshot *const snapshot) noexcept
{
    if (!snapshot)
        return fx::archive::EffectTableSaveStatus::InvalidArgument;

    FxEffectTableSaveCapture capture{
        snapshot,
        fx::archive::EffectTableSaveStatus::Success,
    };
    if (IsFastFileLoad())
    {
        DB_EnumXAssets(
            ASSET_TYPE_FX,
            FX_CaptureEffectTableEntry_FastFile,
            &capture,
            false);
    }
    else
    {
        FX_ForEachEffectDef(
            FX_CaptureEffectTableEntry_LoadObj,
            &capture);
    }

    if (capture.status
        != fx::archive::EffectTableSaveStatus::Success)
    {
        return capture.status;
    }
    return fx::archive::ValidateEffectTableSaveSnapshotNoReport(
        snapshot);
}

bool FX_WriteEffectTableSaveBytes(
    void *const context,
    const void *const data,
    const std::size_t byteCount) noexcept
{
    if (byteCount
        > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return false;
    }
    return FX_WriteArchiveDataNoDrop(
        static_cast<MemoryFile *>(context),
        static_cast<int>(byteCount),
        data);
}

FxEffectTableSaveOutcome FX_SaveEffectTableNoDrop(
    MemoryFile *const memFile)
{
    const std::size_t workspaceSize =
        fx::archive::EffectTableSaveSnapshotSize();
    if (!memFile || workspaceSize == 0
        || workspaceSize
            > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    {
        return FxEffectTableSaveOutcome::InvalidTable;
    }

    void *const storage = Z_Malloc(
        static_cast<int>(workspaceSize),
        "FX_Save effect table snapshot",
        10);
    if (!storage)
        return FxEffectTableSaveOutcome::AllocationFailed;

    fx::archive::EffectTableSaveSnapshot *const snapshot =
        fx::archive::ConstructEffectTableSaveSnapshot(
            storage,
            workspaceSize);
    if (!snapshot)
    {
        Z_Free(storage, 10);
        return FxEffectTableSaveOutcome::InvalidTable;
    }

    fx::archive::EffectTableSaveStatus status =
        FX_CaptureEffectTableNoReport(snapshot);
    if (status == fx::archive::EffectTableSaveStatus::Success)
    {
        const fx::archive::EffectTableSaveCallbacks callbacks{
            memFile,
            FX_WriteEffectTableSaveBytes,
        };
        status = fx::archive::WriteEffectTableSaveSnapshotNoReport(
            snapshot,
            callbacks);
    }

    const bool destroyed =
        fx::archive::DestroyEffectTableSaveSnapshot(snapshot);
    Z_Free(storage, 10);
    if (!destroyed)
        return FxEffectTableSaveOutcome::InvalidTable;
    if (status == fx::archive::EffectTableSaveStatus::Success)
        return FxEffectTableSaveOutcome::Success;
    if (status == fx::archive::EffectTableSaveStatus::WriterFailed)
        return FxEffectTableSaveOutcome::WriteFailed;
    return FxEffectTableSaveOutcome::InvalidTable;
}

bool FX_ValidateEffectTableRestoreLifecycle(
    void *const context,
    const void *const identity,
    const std::uint32_t lifecycleGeneration) noexcept
{
    const auto *const system = static_cast<const FxSystem *>(context);
    return system && identity == system
        && FX_EffectTableRestoreLifecycleIsCurrent(
            system, lifecycleGeneration);
}

const void *FX_RegisterEffectTableRestoreDefinition(
    void *,
    const char *const name) noexcept
{
    return FX_Register(name);
}

bool FX_FixupEffectDefHandlesNoDrop(
    FxSystem *const system,
    const fx::archive::EffectTableRestoreLease &lease) noexcept
{
    if (!system || !lease.ownerCookie || !system->isArchiving
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
        const auto *const effectDef = static_cast<const FxEffectDef *>(
            fx::archive::EffectTableRestoreFind(lease, key));
        if (!effectDef)
            return false;
        effect->def = effectDef;
    }
    return true;
}

bool FX_EffectTableRestoreReleaseIsSafe(
    const fx::archive::EffectTableRestoreStatus status) noexcept
{
    return status == fx::archive::EffectTableRestoreStatus::Success
        || status
            == fx::archive::EffectTableRestoreStatus::LifecycleChanged;
}

[[noreturn]] void FX_ReportEffectTableRestoreFailure(
    const fx::archive::EffectTableRestoreLease *const lease,
    FxSystemBuffers *const restoredBuffers,
    const char *const message)
{
    fx::archive::EffectTableRestoreStatus releaseStatus =
        fx::archive::EffectTableRestoreStatus::Success;
    if (lease && lease->ownerCookie)
    {
        releaseStatus =
            fx::archive::ReleaseEffectTableRestore(*lease);
    }
    if (restoredBuffers)
        Z_Free(restoredBuffers, 10);
    if (!FX_EffectTableRestoreReleaseIsSafe(releaseStatus))
    {
        Sys_Error(
            "Unable to release FX effect-definition restore ownership safely");
        std::abort();
    }
    Com_Error(ERR_DROP, "%s", message);
    std::abort();
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

bool FX_BuildArchiveExpectedTokens(
    const FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    std::array<fx::physics::BodyToken, MAX_ELEMS> *const outTokens) noexcept
{
    if (!outTokens || (entryCount != 0 && !entries)
        || entryCount > fx::physics::BODY_LIMIT)
    {
        return false;
    }

    outTokens->fill(fx::physics::INVALID_BODY_TOKEN);
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        const FxArchivePhysicsEntry &entry = entries[index];
        if (entry.ownerIndex >= MAX_ELEMS
            || (*outTokens)[entry.ownerIndex]
                != fx::physics::INVALID_BODY_TOKEN
            || entry.token == fx::physics::INVALID_BODY_TOKEN)
        {
            return false;
        }
        (*outTokens)[entry.ownerIndex] = entry.token;
    }
    // The first pass uses the token array itself as the uniqueness map. Clear
    // retired owners only after every duplicate has therefore been rejected.
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        const FxArchivePhysicsEntry &entry = entries[index];
        if (entry.retired)
        {
            (*outTokens)[entry.ownerIndex] =
                fx::physics::INVALID_BODY_TOKEN;
        }
    }
    return true;
}

bool FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
    const fx::physics::BodySidecar *const sidecar,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    const bool captureStates,
    std::array<fx::physics::BodyToken, MAX_ELEMS> *const expectedTokens,
    fx::physics::BodySidecarValidationScratch *const sidecarScratch) noexcept
{
    if ((entryCount != 0 && !entries) || !sidecar || !expectedTokens
        || !sidecarScratch)
        return false;

    if (!FX_BuildArchiveExpectedTokens(
            entries, entryCount, expectedTokens)
        || fx::physics::ValidateSemanticOwnershipWithScratch(
            sidecar, *expectedTokens, sidecarScratch)
            != fx::physics::SidecarStatus::Success)
    {
        return false;
    }

    for (std::size_t index = 0; index < entryCount; ++index)
    {
        FxArchivePhysicsEntry &entry = entries[index];
        if (entry.retired)
            continue;
        const fx::physics::BodyResult resolved = fx::physics::Resolve(
            sidecar, entry.ownerIndex, entry.token);
        BodyState captured{};
        if (!resolved
            || Phys_TryCaptureBodyStateLocked(
                   PHYS_WORLD_FX, resolved.body, &captured)
                != PhysBodyRollbackStatus::Success)
        {
            return false;
        }
        if (captureStates)
            entry.state = captured;
    }
    return true;
}

bool FX_ValidateArchivePhysicsOwnershipLocked(
    const fx::physics::BodySidecar *const sidecar,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    const bool captureStates) noexcept
{
    std::array<fx::physics::BodyToken, MAX_ELEMS> expectedTokens{};
    fx::physics::BodySidecarValidationScratch sidecarScratch{};
    return FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
        sidecar,
        entries,
        entryCount,
        captureStates,
        &expectedTokens,
        &sidecarScratch);
}

bool FX_CaptureArchivePhysicsRollbackRecipesLockedWithScratch(
    const fx::physics::BodySidecar *const sidecar,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    std::array<fx::physics::BodyToken, MAX_ELEMS> *const expectedTokens,
    fx::physics::BodySidecarValidationScratch *const sidecarScratch) noexcept
{
    if (!FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
            sidecar,
            entries,
            entryCount,
            false,
            expectedTokens,
            sidecarScratch))
    {
        return false;
    }

    for (std::size_t index = 0; index < entryCount; ++index)
    {
        FxArchivePhysicsEntry &entry = entries[index];
        if (entry.retired || !entry.model)
            return false;
        const fx::physics::BodyResult resolved = fx::physics::Resolve(
            sidecar, entry.ownerIndex, entry.token);
        PhysBodyRollbackRecipe recipe{};
        if (!resolved
            || Phys_TryBuildBodyRollbackRecipeLocked(
                   PHYS_WORLD_FX,
                   resolved.body,
                   entry.model,
                   &recipe)
                != PhysBodyRollbackStatus::Success
            || recipe.model != entry.model
            || recipe.demand.bodyCount != 1
            || recipe.demand.userDataCount != 1)
        {
            return false;
        }
        entry.rollbackRecipe = recipe;
        entry.state = recipe.state;
    }
    return true;
}

bool FX_PrepareArchivePhysicsSidecarForDrainInspectionLocked(
    fx::physics::BodySidecar *const sidecar,
    const bool allowVacantInitialization,
    fx::physics::BodySidecarSnapshotScratch *const scratch) noexcept
{
    if (!sidecar || !scratch)
        return false;
    if (!sidecar->IsInitialized())
    {
        if (!allowVacantInitialization)
            return false;
        return fx::physics::ResetEmptyWithScratch(sidecar, scratch)
            == fx::physics::SidecarStatus::Success;
    }
    return fx::physics::ValidateWithScratch(sidecar, scratch)
        == fx::physics::SidecarStatus::Success;
}

fx::archive::RestoreControlOperationStatus
FX_DrainArchivePhysicsSidecarsLocked(
    const std::array<fx::physics::BodySidecar *, 3> &sidecars,
    const std::array<bool, 3> &drainSidecar,
    FxArchivePhysicsOwnershipScratch *const scratch) noexcept
{
    using Status = fx::archive::RestoreControlOperationStatus;

    if (!scratch)
        return Status::RecoverableFailure;

    // Slot zero is always the persistent live registry. Treating an
    // uninitialized live registry as empty could abandon native bodies that
    // no longer have registrations. Slots one and two are fresh transaction
    // locals and may be initialized only after ResetEmpty proves them vacant.
    for (std::size_t first = 0; first < sidecars.size(); ++first)
    {
        if (!sidecars[first]
            || !FX_PrepareArchivePhysicsSidecarForDrainInspectionLocked(
                sidecars[first], first != 0, &scratch->sidecar))
        {
            return Status::RecoverableFailure;
        }
        for (std::size_t second = first + 1;
             second < sidecars.size();
             ++second)
        {
            if (sidecars[first] == sidecars[second])
                return Status::RecoverableFailure;
        }
    }

    // Protect retained as well as drained ownership. Otherwise a corrupt
    // staged/rollback alias could destroy a body that remains published by the
    // live sidecar even though the selected drain sets are mutually disjoint.
    for (std::size_t first = 0; first < sidecars.size(); ++first)
    {
        for (std::size_t second = first + 1;
             second < sidecars.size();
             ++second)
        {
            if (fx::physics::ValidateDisjointOwnershipWithScratch(
                    sidecars[first], sidecars[second], &scratch->sidecar)
                != fx::physics::SidecarStatus::Success)
            {
                return Status::RecoverableFailure;
            }
        }
    }

    // Reuse one bounded snapshot so stack cost does not scale with the three
    // sidecars. PHYSICS ownership prevents any registration/native mutation
    // between this all-body preflight and the deterministic drain pass.
    for (std::size_t index = 0; index < sidecars.size(); ++index)
    {
        if (!drainSidecar[index])
            continue;
        if (fx::physics::SnapshotOwnershipWithScratch(
                sidecars[index],
                &scratch->drainOwnership,
                &scratch->sidecar)
            != fx::physics::SidecarStatus::Success)
        {
            return Status::RecoverableFailure;
        }
        for (std::size_t bodyIndex = 0;
             bodyIndex < scratch->drainOwnership.count;
             ++bodyIndex)
        {
            if (Phys_TryValidateBodyDestroyLockedNoReport(
                    PHYS_WORLD_FX,
                    scratch->drainOwnership.records[bodyIndex].body)
                != PhysBodyRollbackStatus::Success)
            {
                return Status::RecoverableFailure;
            }
        }
    }

    for (std::size_t index = 0; index < sidecars.size(); ++index)
    {
        if (!drainSidecar[index])
            continue;
        fx::physics::BodySidecar *const sidecar = sidecars[index];
        while (sidecar->ActiveCount() != 0)
        {
            const fx::physics::IndexedBodyResult taken =
                fx::physics::TakeFirstWithScratch(
                    sidecar, &scratch->sidecar);
            if (!taken)
                return Status::RecoverableFailure;
            if (Phys_TryDestroyBodyLockedNoReport(
                    PHYS_WORLD_FX, taken.body)
                != PhysBodyRollbackStatus::Success)
            {
                return Status::UnsafeFailure;
            }
        }
        if (fx::physics::ResetEmptyWithScratch(
                sidecar, &scratch->sidecar)
            != fx::physics::SidecarStatus::Success)
        {
            return Status::RecoverableFailure;
        }
    }
    return Status::Success;
}

enum class FxArchivePhysicsCapacityStatus : std::uint8_t
{
    Sufficient,
    ValidInsufficient,
    Invalid,
};

bool FX_GetArchivePhysicsDemand(
    const FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    fx::archive::PhysicsResourceCount *const outDemand) noexcept
{
    if ((entryCount != 0 && !entries)
        || entryCount > fx::physics::BODY_LIMIT
        || !outDemand)
    {
        return false;
    }

    fx::archive::PhysicsResourceCount demand{};
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        PhysBodyResourceDemand bodyDemand{};
        if (entries[index].retired
            || Phys_TryGetBodyModelResourceDemand(
                   entries[index].model, &bodyDemand)
                != PhysBodyRollbackStatus::Success
            || bodyDemand.bodyCount != 1
            || bodyDemand.userDataCount != 1
            || !fx::archive::PhysicsResourceCountCanAdd(
                demand.bodies, bodyDemand.bodyCount)
            || !fx::archive::PhysicsResourceCountCanAdd(
                demand.userData, bodyDemand.userDataCount)
            || !fx::archive::PhysicsResourceCountCanAdd(
                demand.geoms, bodyDemand.geomCount))
        {
            return false;
        }
        demand.bodies += bodyDemand.bodyCount;
        demand.userData += bodyDemand.userDataCount;
        demand.geoms += bodyDemand.geomCount;
    }
    *outDemand = demand;
    return true;
}

bool FX_GetArchivePhysicsFreeCapacityLocked(
    fx::archive::PhysicsResourceCount *const outFreeCapacity) noexcept
{
    if (!outFreeCapacity)
        return false;
    PhysBodyResourceDemand available{};
    if (Phys_TryGetFreeResourceCapacityLockedNoReport(&available)
        != PhysBodyRollbackStatus::Success)
    {
        return false;
    }

    *outFreeCapacity = {
        available.bodyCount,
        available.userDataCount,
        available.geomCount,
    };
    return true;
}

FxArchivePhysicsCapacityStatus FX_ArchivePhysicsCapacityAvailableLocked(
    const FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount) noexcept
{
    fx::archive::PhysicsResourceCount required{};
    if (!FX_GetArchivePhysicsDemand(entries, entryCount, &required))
    {
        return FxArchivePhysicsCapacityStatus::Invalid;
    }
    if (required.bodies == 0 && required.userData == 0
        && required.geoms == 0)
    {
        return FxArchivePhysicsCapacityStatus::Sufficient;
    }

    fx::archive::PhysicsResourceCount available{};
    if (!FX_GetArchivePhysicsFreeCapacityLocked(&available))
        return FxArchivePhysicsCapacityStatus::Invalid;

    return available.bodies >= required.bodies
        && available.userData >= required.userData
        && available.geoms >= required.geoms
        ? FxArchivePhysicsCapacityStatus::Sufficient
        : FxArchivePhysicsCapacityStatus::ValidInsufficient;
}

bool FX_BuildArchivePhysicsRetirementPlanLocked(
    const FxArchivePhysicsEntry *const desiredEntries,
    const std::size_t desiredEntryCount,
    const FxArchivePhysicsEntry *const liveEntries,
    const std::size_t liveEntryCount,
    fx::archive::PhysicsRetirementPlan *const outPlan,
    FxArchiveRestorePhysicsScratch *const scratch) noexcept
{
    if ((liveEntryCount != 0 && !liveEntries) || !outPlan || !scratch
        || liveEntryCount > fx::physics::BODY_LIMIT)
    {
        return false;
    }

    fx::archive::PhysicsResourceCount desired{};
    if (!FX_GetArchivePhysicsDemand(
            desiredEntries, desiredEntryCount, &desired))
    {
        return false;
    }

    if (desired.bodies == 0 && desired.userData == 0
        && desired.geoms == 0)
    {
        *outPlan = {};
        return true;
    }

    fx::archive::PhysicsResourceCount freeCapacity{};
    if (!FX_GetArchivePhysicsFreeCapacityLocked(&freeCapacity))
        return false;

    for (std::size_t index = 0; index < liveEntryCount; ++index)
    {
        const FxArchivePhysicsEntry &entry = liveEntries[index];
        if (entry.retired || !entry.rollbackRecipe.model
            || entry.rollbackRecipe.model != entry.model
            || entry.rollbackRecipe.demand.bodyCount != 1
            || entry.rollbackRecipe.demand.userDataCount != 1)
        {
            return false;
        }
        scratch->retirementCandidates[index] = {
            index,
            entry.ownerIndex,
            entry.rollbackRecipe.demand.geomCount,
        };
    }

    return fx::archive::BuildPhysicsRetirementPlanWithScratch(
               freeCapacity,
               desired,
               scratch->retirementCandidates.data(),
               liveEntryCount,
               &scratch->retirementPlanner,
               outPlan)
        == fx::archive::PhysicsRetirementPlanStatus::Success;
}

bool FX_ArchivePhysicsBatchSelectionIsValidForWrapper(
    const std::size_t *const entryIndices,
    const std::size_t selectedCount,
    const std::size_t entryCount) noexcept
{
    if (selectedCount > entryCount
        || (selectedCount != 0 && !entryIndices))
    {
        return false;
    }
    for (std::size_t index = 0; index < selectedCount; ++index)
    {
        if (entryIndices[index] >= entryCount)
            return false;
        for (std::size_t prior = 0; prior < index; ++prior)
        {
            if (entryIndices[prior] == entryIndices[index])
                return false;
        }
    }
    return true;
}

struct FxArchivePhysicsRetirementBatchContext
{
    fx::physics::BodySidecar *liveSidecar;
    FxArchivePhysicsEntry *liveEntries;
    std::size_t liveEntryCount;
    fx::physics::BodySidecarSnapshotScratch *scratch;
};

fx::archive::RestoreControlOperationStatus
FX_PerformArchivePhysicsRetirementBatchOperation(
    void *const opaqueContext,
    const fx::archive::ArchivePhysicsBatchOperation operation,
    const std::size_t entryIndex) noexcept
{
    using Operation = fx::archive::ArchivePhysicsBatchOperation;
    using Status = fx::archive::RestoreControlOperationStatus;

    if (!opaqueContext)
        return Status::UnsafeFailure;
    auto &context = *static_cast<
        FxArchivePhysicsRetirementBatchContext *>(opaqueContext);
    if (!context.liveSidecar || !context.liveEntries || !context.scratch
        || entryIndex >= context.liveEntryCount)
    {
        return Status::UnsafeFailure;
    }

    FxArchivePhysicsEntry &entry = context.liveEntries[entryIndex];
    switch (operation)
    {
    case Operation::ValidateRetirement:
    {
        const fx::physics::BodyResult resolved = fx::physics::Resolve(
            context.liveSidecar, entry.ownerIndex, entry.token);
        return !entry.retired && entry.rollbackRecipe.model
                && entry.rollbackRecipe.model == entry.model && resolved
                && Phys_TryValidateBodyDestroyLockedNoReport(
                       PHYS_WORLD_FX, resolved.body)
                    == PhysBodyRollbackStatus::Success
            ? Status::Success
            : Status::RecoverableFailure;
    }

    case Operation::Retire:
    {
        if (entry.retired || !entry.rollbackRecipe.model
            || entry.rollbackRecipe.model != entry.model)
        {
            return Status::RecoverableFailure;
        }
        const fx::physics::BodyResult taken =
            fx::physics::TakeWithScratch(
                context.liveSidecar,
                entry.ownerIndex,
                entry.token,
                context.scratch);
        if (!taken)
            return Status::RecoverableFailure;
        entry.reconstructedToken = fx::physics::INVALID_BODY_TOKEN;
        entry.retired = true;
        return Phys_TryDestroyBodyLockedNoReport(
                   PHYS_WORLD_FX, taken.body)
                == PhysBodyRollbackStatus::Success
            ? Status::Success
            : Status::UnsafeFailure;
    }

    default:
        return Status::UnsafeFailure;
    }
}

fx::archive::RestoreControlOperationStatus
FX_RetireArchivePhysicsLocked(
    fx::physics::BodySidecar *const liveSidecar,
    FxArchivePhysicsEntry *const liveEntries,
    const std::size_t liveEntryCount,
    const fx::archive::PhysicsRetirementPlan &plan,
    std::size_t *const outRetiredCount,
    fx::physics::BodySidecarSnapshotScratch *const scratch) noexcept
{
    using Status = fx::archive::RestoreControlOperationStatus;

    if (!liveSidecar || (liveEntryCount != 0 && !liveEntries)
        || liveEntryCount > fx::physics::BODY_LIMIT
        || plan.count > liveEntryCount || !outRetiredCount || !scratch)
    {
        return Status::RecoverableFailure;
    }
    *outRetiredCount = 0;
    if (!FX_ArchivePhysicsBatchSelectionIsValidForWrapper(
            plan.entryIndices.data(), plan.count, liveEntryCount))
    {
        return Status::RecoverableFailure;
    }

    FxArchivePhysicsRetirementBatchContext context{
        liveSidecar,
        liveEntries,
        liveEntryCount,
        scratch,
    };
    const fx::archive::ArchivePhysicsBatchCallbacks callbacks{
        &context,
        FX_PerformArchivePhysicsRetirementBatchOperation,
    };
    return fx::archive::RunArchivePhysicsRetirementBatch(
        callbacks,
        plan.entryIndices.data(),
        plan.count,
        liveEntryCount,
        outRetiredCount);
}

struct FxArchivePhysicsReconstructionBatchContext
{
    fx::physics::BodySidecar *liveSidecar;
    FxArchivePhysicsEntry *liveEntries;
    std::size_t liveEntryCount;
    fx::physics::BodySidecarSnapshotScratch *scratch;
};

fx::archive::RestoreControlOperationStatus
FX_PerformArchivePhysicsReconstructionBatchOperation(
    void *const opaqueContext,
    const fx::archive::ArchivePhysicsBatchOperation operation,
    const std::size_t entryIndex) noexcept
{
    using Operation = fx::archive::ArchivePhysicsBatchOperation;
    using Status = fx::archive::RestoreControlOperationStatus;

    if (!opaqueContext)
        return Status::UnsafeFailure;
    auto &context = *static_cast<
        FxArchivePhysicsReconstructionBatchContext *>(opaqueContext);
    if (!context.liveSidecar || !context.liveEntries || !context.scratch
        || entryIndex >= context.liveEntryCount)
    {
        return Status::UnsafeFailure;
    }

    FxArchivePhysicsEntry &entry = context.liveEntries[entryIndex];
    const PhysBodyRollbackRecipe &recipe = entry.rollbackRecipe;
    switch (operation)
    {
    case Operation::ValidateReconstruction:
        return entry.retired && entry.ownerIndex < MAX_ELEMS
                && recipe.model && recipe.model == entry.model
                && entry.token != fx::physics::INVALID_BODY_TOKEN
                && entry.reconstructedToken
                    == fx::physics::INVALID_BODY_TOKEN
                && recipe.demand.bodyCount == 1
                && recipe.demand.userDataCount == 1
                && fx::physics::ValidateVacantOwner(
                       context.liveSidecar, entry.ownerIndex)
                    == fx::physics::SidecarStatus::Success
            ? Status::Success
            : Status::RecoverableFailure;

    case Operation::Reconstruct:
    {
        dxBody *createdBody = nullptr;
        const PhysBodyModelCreateStatus bodyStatus =
            Phys_TryCreateBodyFromStateAndXModelLockedNoReport(
                PHYS_WORLD_FX,
                &recipe.state,
                recipe.model,
                &createdBody);
        if (bodyStatus != PhysBodyModelCreateStatus::Success
            || !createdBody)
        {
            if (bodyStatus == PhysBodyModelCreateStatus::CleanupFailed)
                return Status::UnsafeFailure;
            return createdBody
                ? Status::UnsafeFailure
                : Status::RecoverableFailure;
        }

        const fx::physics::TokenResult bound =
            fx::physics::BindWithScratch(
                context.liveSidecar,
                entry.ownerIndex,
                createdBody,
                context.scratch);
        if (!bound)
        {
            // A newly created address that is already registered has
            // ambiguous native ownership. Destroying it could invalidate the
            // existing registration, while retaining it cannot be recovered.
            if (bound.status == fx::physics::SidecarStatus::DuplicateBody)
                return Status::UnsafeFailure;
            if (Phys_TryDestroyBodyLockedNoReport(
                    PHYS_WORLD_FX, createdBody)
                != PhysBodyRollbackStatus::Success)
            {
                return Status::UnsafeFailure;
            }
            return Status::RecoverableFailure;
        }
        entry.reconstructedToken = bound.token;
        return Status::Success;
    }

    default:
        return Status::UnsafeFailure;
    }
}

fx::archive::RestoreControlOperationStatus
FX_ReconstructRetiredArchivePhysicsLocked(
    fx::physics::BodySidecar *const liveSidecar,
    FxArchivePhysicsEntry *const liveEntries,
    const std::size_t liveEntryCount,
    const fx::archive::PhysicsRetirementPlan &plan,
    const std::size_t retiredCount,
    fx::physics::BodySidecarSnapshotScratch *const scratch) noexcept
{
    using Status = fx::archive::RestoreControlOperationStatus;

    if (!liveSidecar || (liveEntryCount != 0 && !liveEntries)
        || liveEntryCount > fx::physics::BODY_LIMIT
        || retiredCount > plan.count || plan.count > liveEntryCount
        || !scratch)
    {
        return Status::RecoverableFailure;
    }
    if (!FX_ArchivePhysicsBatchSelectionIsValidForWrapper(
            plan.entryIndices.data(), retiredCount, liveEntryCount))
    {
        return Status::RecoverableFailure;
    }

    FxArchivePhysicsReconstructionBatchContext context{
        liveSidecar,
        liveEntries,
        liveEntryCount,
        scratch,
    };
    const fx::archive::ArchivePhysicsBatchCallbacks callbacks{
        &context,
        FX_PerformArchivePhysicsReconstructionBatchOperation,
    };
    std::size_t reconstructedCount = 0;
    return fx::archive::RunArchivePhysicsReconstructionBatch(
        callbacks,
        plan.entryIndices.data(),
        retiredCount,
        liveEntryCount,
        &reconstructedCount);
}

bool FX_ArchiveRetiredTokenTargetsMatch(
    const FxPool<FxElem> *const targetElems,
    const FxArchivePhysicsEntry *const liveEntries,
    const std::size_t liveEntryCount,
    const fx::archive::PhysicsRetirementPlan &plan,
    const std::size_t retiredCount,
    const bool requireReconstructedTokens) noexcept
{
    if (!targetElems || (liveEntryCount != 0 && !liveEntries)
        || liveEntryCount > fx::physics::BODY_LIMIT
        || retiredCount > plan.count || plan.count > liveEntryCount)
    {
        return false;
    }

    for (std::size_t index = 0; index < retiredCount; ++index)
    {
        const std::size_t entryIndex = plan.entryIndices[index];
        if (entryIndex >= liveEntryCount)
            return false;
        const FxArchivePhysicsEntry &entry = liveEntries[entryIndex];
        if (!entry.retired || entry.ownerIndex >= MAX_ELEMS
            || entry.token == fx::physics::INVALID_BODY_TOKEN
            || (requireReconstructedTokens
                && entry.reconstructedToken
                    == fx::physics::INVALID_BODY_TOKEN)
            || fx::physics::TokenFromLegacyField(
                   targetElems[entry.ownerIndex].item.physObjId)
                != entry.token)
        {
            return false;
        }
    }
    return true;
}

bool FX_PatchReconstructedArchivePhysicsTokens(
    FxPool<FxElem> *const targetElems,
    FxArchivePhysicsEntry *const liveEntries,
    const std::size_t liveEntryCount,
    const fx::archive::PhysicsRetirementPlan &plan,
    const std::size_t retiredCount) noexcept
{
    if (!FX_ArchiveRetiredTokenTargetsMatch(
            targetElems,
            liveEntries,
            liveEntryCount,
            plan,
            retiredCount,
            true))
    {
        return false;
    }

    // All checks precede this no-fail publication pass. A failed body rebuild
    // therefore never leaves a partially patched old graph.
    for (std::size_t index = 0; index < retiredCount; ++index)
    {
        FxArchivePhysicsEntry &entry =
            liveEntries[plan.entryIndices[index]];
        targetElems[entry.ownerIndex].item.physObjId =
            fx::physics::TokenToLegacyField(entry.reconstructedToken);
        entry.token = entry.reconstructedToken;
        entry.reconstructedToken = fx::physics::INVALID_BODY_TOKEN;
        entry.retired = false;
    }

    return true;
}

fx::archive::RestoreControlOperationStatus
FX_PublishArchivePhysicsSafeEmptyLocked(
    FxSystem *const system,
    fx::physics::BodySidecar *const liveSidecar,
    fx::physics::BodySidecar *const stagedSidecar,
    fx::physics::BodySidecar *const rollbackSidecar,
    FxArchiveRestorePhysicsScratch *const scratch) noexcept
{
    using Status = fx::archive::RestoreControlOperationStatus;

    if (!system || !liveSidecar || !stagedSidecar || !rollbackSidecar
        || !scratch)
        return Status::RecoverableFailure;
    if (!FX_ValidateArchiveExclusiveState(system)
        || !FX_CanPublishArchiveSafeEmptyStateLocked(system))
    {
        return Status::RecoverableFailure;
    }

    const Status drainStatus =
        FX_DrainArchivePhysicsSidecarsLocked(
            {liveSidecar, stagedSidecar, rollbackSidecar},
            {true, true, true},
            &scratch->ownership);
    if (drainStatus != Status::Success)
        return drainStatus;
    return FX_PublishArchiveSafeEmptyStateLockedWithScratch(
        system,
        &scratch->ownership.sidecar,
        &scratch->poolGraph)
        ? Status::Success
        : Status::RecoverableFailure;
}

bool FX_AppendArchivePhysicsEntry(
    FxSystem *const system,
    FxEffect *const effect,
    FxElem *const elem,
    const FxElemDef *const elemDef,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCapacity,
    std::size_t *const entryCount) noexcept
{
    if (!system || !effect || !elem || !elemDef || !entryCount)
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
        || *entryCount >= fx::physics::BODY_LIMIT
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

    std::int32_t ownerIndex = -1;
    const fx::physics::BodyToken token =
        fx::physics::TokenFromLegacyField(elem->physObjId);
    if (!FxPoolItemIndex<FxElem, MAX_ELEMS>(
            system->elems, elem, &ownerIndex)
        || ownerIndex < 0
        || token == fx::physics::INVALID_BODY_TOKEN)
    {
        return false;
    }

    if (entries)
    {
        FxArchivePhysicsEntry &entry = entries[*entryCount];
        std::memset(&entry, 0, sizeof(entry));
        entry.elem = elem;
        entry.model = model;
        entry.ownerIndex = static_cast<std::size_t>(ownerIndex);
        entry.token = token;
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
                        system,
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
        const fx::physics::BodySidecar *const sidecar =
            FX_GetPhysicsBodySidecar(system);
        Sys_EnterCriticalSection(CRITSECT_PHYSICS);
        const bool physicsStatesValid =
            FX_ValidateArchivePhysicsOwnershipLocked(
                sidecar, entries, entryCount, true);
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

// The archive restore transaction owns CRITSECT_PHYSICS continuously while
// calling the helpers below.  Keeping lock ownership at the transaction level
// prevents staged bodies, which ODE links into its world at creation time,
// from becoming observable between construction, FX graph publication, and
// either commit or rollback.
fx::archive::RestoreControlOperationStatus
FX_CreateArchivePhysicsLocked(
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    const std::int32_t archiveTime,
    const fx::physics::BodySidecar *const liveSidecar,
    fx::physics::BodySidecar *const stagedSidecar,
    fx::physics::BodySidecarSnapshotScratch *const scratch) noexcept
{
    using Status = fx::archive::RestoreControlOperationStatus;

    if ((entryCount != 0 && !entries) || !liveSidecar
        || !stagedSidecar || !scratch
        || fx::physics::ValidateDisjointOwnershipWithScratch(
            liveSidecar, stagedSidecar, scratch)
            != fx::physics::SidecarStatus::Success)
    {
        return Status::RecoverableFailure;
    }
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        FxArchivePhysicsEntry &entry = entries[index];
        if (!entry.elem || !entry.model || entry.retired
            || !FX_ValidateArchiveBodyState(entry.state))
        {
            return Status::RecoverableFailure;
        }
        FX_NormalizeArchiveBodyState(&entry.state, archiveTime);
    }

    const std::int32_t physicsTime =
        physGlob.worldData[PHYS_WORLD_FX].timeLastUpdate;
    if ((entryCount != 0
            && !FX_ArchiveTimeDifferenceFits(archiveTime, physicsTime))
        || FX_ArchivePhysicsCapacityAvailableLocked(entries, entryCount)
            != FxArchivePhysicsCapacityStatus::Sufficient)
    {
        return Status::RecoverableFailure;
    }
    for (std::size_t index = 0; index < entryCount; ++index)
    {
        FxArchivePhysicsEntry &entry = entries[index];
        FX_NormalizeArchiveBodyState(&entry.state, physicsTime);
        dxBody *createdBody = nullptr;
        const PhysBodyModelCreateStatus bodyStatus =
            Phys_TryCreateBodyFromStateAndXModelLockedNoReport(
                PHYS_WORLD_FX,
                &entry.state,
                entry.model,
                &createdBody);
        if (bodyStatus != PhysBodyModelCreateStatus::Success
            || !createdBody)
        {
            if (bodyStatus == PhysBodyModelCreateStatus::CleanupFailed)
                return Status::UnsafeFailure;
            return createdBody
                ? Status::UnsafeFailure
                : Status::RecoverableFailure;
        }

        const fx::physics::TokenResult bound =
            fx::physics::BindWithScratch(
                stagedSidecar,
                entry.ownerIndex,
                createdBody,
                scratch);
        if (!bound)
        {
            // A validated staged sidecar can report DuplicateBody here only
            // when the allocator returned an already-registered address.
            // Its existing registration owns the sole eventual destruction.
            if (bound.status != fx::physics::SidecarStatus::DuplicateBody)
            {
                if (Phys_TryDestroyBodyLockedNoReport(
                        PHYS_WORLD_FX, createdBody)
                    != PhysBodyRollbackStatus::Success)
                {
                    return Status::UnsafeFailure;
                }
            }
            return Status::RecoverableFailure;
        }
        const fx::physics::SidecarStatus disjointStatus =
            fx::physics::ValidateDisjointOwnershipWithScratch(
                liveSidecar, stagedSidecar, scratch);
        if (disjointStatus != fx::physics::SidecarStatus::Success)
        {
            const fx::physics::BodyResult detached =
                fx::physics::TakeWithScratch(
                    stagedSidecar,
                    entry.ownerIndex,
                    bound.token,
                    scratch);
            if (detached
                && disjointStatus
                    != fx::physics::SidecarStatus::DuplicateBody)
            {
                if (Phys_TryDestroyBodyLockedNoReport(
                        PHYS_WORLD_FX, detached.body)
                    != PhysBodyRollbackStatus::Success)
                {
                    return Status::UnsafeFailure;
                }
            }
            return Status::RecoverableFailure;
        }
        entry.token = bound.token;
        entry.elem->physObjId =
            fx::physics::TokenToLegacyField(bound.token);
    }
    return Status::Success;
}

struct FxArchiveRestoreControlContext
{
    FxSystem *system;
    FxSystemBuffers *systemBuffers;
    FxSystem *desiredSystem;
    FxSystemBuffers *desiredBuffers;
    FxSystem *originalSystem;
    FxSystemBuffers *originalBuffers;
    FxArchivePhysicsEntry *desiredPhysicsEntries;
    std::size_t desiredPhysicsEntryCount;
    FxArchivePhysicsEntry *originalPhysicsEntries;
    std::size_t originalPhysicsEntryCount;
    fx::physics::BodySidecar *livePhysicsSidecar;
    fx::physics::BodySidecar *stagedPhysicsSidecar;
    fx::physics::BodySidecar *rollbackPhysicsSidecar;
    FxArchiveRestorePhysicsScratch *physicsScratch;
    bool originalSnapshotExclusive;
    bool originalGraphPublished;
    fx::archive::PhysicsRetirementPlan retirementPlan;
    std::size_t retiredPhysicsEntryCount;
};

// The sidecars are last so normal reverse destruction releases their checked
// transaction lifetimes before the scratch/context they reference. Unsafe
// restore outcomes deliberately never destroy this object: native ownership
// may be indeterminate and the process must terminate with admission closed.
struct FxArchiveRestoreTransactionWorkspace final
{
    FxSystem rollbackSystem{};
    FxArchiveRestoreControlContext control{};
    FxArchiveRestorePhysicsScratch physicsScratch{};
    fx::physics::BodySidecar stagedPhysicsSidecar{};
    fx::physics::BodySidecar rollbackPhysicsSidecar{};

    FxArchiveRestoreTransactionWorkspace() noexcept = default;
    ~FxArchiveRestoreTransactionWorkspace() noexcept = default;
    FxArchiveRestoreTransactionWorkspace(
        const FxArchiveRestoreTransactionWorkspace &) = delete;
    FxArchiveRestoreTransactionWorkspace &operator=(
        const FxArchiveRestoreTransactionWorkspace &) = delete;
};

static_assert(std::is_nothrow_default_constructible_v<
    FxArchiveRestoreTransactionWorkspace>);
static_assert(std::is_nothrow_destructible_v<
    FxArchiveRestoreTransactionWorkspace>);
static_assert(
    alignof(FxArchiveRestoreTransactionWorkspace)
    <= alignof(std::max_align_t));

void *FX_AllocateArchiveRestoreWorkspaceMemory(
    void *,
    const int byteCount) noexcept
{
    return Z_Malloc(
        byteCount,
        "FX_Restore checked workspace",
        10);
}

void FX_FreeArchiveRestoreWorkspaceMemory(
    void *,
    void *const storage) noexcept
{
    if (storage)
        Z_Free(storage, 10);
}

constexpr fx::archive::ArchiveRestoreWorkspaceMemoryCallbacks
    FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY{
        nullptr,
        FX_AllocateArchiveRestoreWorkspaceMemory,
        FX_FreeArchiveRestoreWorkspaceMemory,
    };

[[nodiscard]] FxArchiveRestoreTransactionWorkspace *
FX_AllocateArchiveRestoreTransactionWorkspace() noexcept
{
    return fx::archive::AllocateArchiveRestoreWorkspace<
        FxArchiveRestoreTransactionWorkspace>(
        FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
}

[[nodiscard]] bool FX_DestroyArchiveRestoreTransactionWorkspace(
    FxArchiveRestoreTransactionWorkspace *const workspace) noexcept
{
    return fx::archive::DestroyArchiveRestoreWorkspace(
        workspace,
        FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
}

fx::archive::RestoreControlOperationStatus
FX_ArchiveRestoreControlStatus(const bool succeeded) noexcept
{
    return succeeded
        ? fx::archive::RestoreControlOperationStatus::Success
        : fx::archive::RestoreControlOperationStatus::RecoverableFailure;
}

fx::archive::RestoreControlOperationStatus
FX_PerformArchiveRestoreControlOperation(
    void *const opaqueContext,
    const fx::archive::RestoreControlOperation operation) noexcept
{
    using Operation = fx::archive::RestoreControlOperation;
    using Status = fx::archive::RestoreControlOperationStatus;

    if (!opaqueContext)
        return Status::UnsafeFailure;
    auto &context = *static_cast<FxArchiveRestoreControlContext *>(
        opaqueContext);
    if (!context.physicsScratch)
        return Status::UnsafeFailure;

    switch (operation)
    {
    case Operation::CaptureOriginal:
        return FX_ArchiveRestoreControlStatus(
            context.originalSnapshotExclusive
            && FX_ArchiveEffectRingIsValid(context.system)
            && FX_ValidatePoolAllocationGraphStateWithScratch(
                context.system,
                &context.physicsScratch->poolGraph)
            && FX_CollectArchivePhysicsEntries(
                context.system,
                context.originalPhysicsEntries,
                fx::physics::BODY_LIMIT,
                &context.originalPhysicsEntryCount,
                false,
                nullptr)
            && FX_CaptureArchivePhysicsRollbackRecipesLockedWithScratch(
                context.livePhysicsSidecar,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                &context.physicsScratch->ownership.expectedTokens,
                &context.physicsScratch->ownership.sidecar));

    case Operation::PlanRetirement:
        return FX_ArchiveRestoreControlStatus(
            FX_BuildArchivePhysicsRetirementPlanLocked(
                context.desiredPhysicsEntries,
                context.desiredPhysicsEntryCount,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                &context.retirementPlan,
                context.physicsScratch));

    case Operation::RetireOriginal:
        return FX_RetireArchivePhysicsLocked(
            context.livePhysicsSidecar,
            context.originalPhysicsEntries,
            context.originalPhysicsEntryCount,
            context.retirementPlan,
            &context.retiredPhysicsEntryCount,
            &context.physicsScratch->ownership.sidecar);

    case Operation::PreparePhysicsReplacement:
        return FX_ArchiveRestoreControlStatus(
            fx::physics::PrepareReplacementWithScratch(
                context.livePhysicsSidecar,
                context.stagedPhysicsSidecar,
                &context.physicsScratch->ownership.sidecar)
                == fx::physics::SidecarStatus::Success);

    case Operation::CreateDesiredPhysics:
        return FX_CreateArchivePhysicsLocked(
            context.desiredPhysicsEntries,
            context.desiredPhysicsEntryCount,
            context.desiredSystem->msecNow,
            context.livePhysicsSidecar,
            context.stagedPhysicsSidecar,
            &context.physicsScratch->ownership.sidecar);

    case Operation::ValidateDesiredPhysics:
        return FX_ArchiveRestoreControlStatus(
            FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
                context.stagedPhysicsSidecar,
                context.desiredPhysicsEntries,
                context.desiredPhysicsEntryCount,
                false,
                &context.physicsScratch->ownership.expectedTokens,
                &context.physicsScratch->ownership.sidecar));

    case Operation::PublishPhysicsReplacement:
        return FX_ArchiveRestoreControlStatus(
            fx::physics::PublishReplacementWithScratch(
                context.livePhysicsSidecar,
                context.stagedPhysicsSidecar,
                context.rollbackPhysicsSidecar,
                &context.physicsScratch->ownership.sidecar)
                == fx::physics::SidecarStatus::Success);

    case Operation::PublishDesiredGraph:
    {
        context.desiredSystem->isArchiving = false;
        Sys_AtomicStore(&context.desiredSystem->iteratorCount, -1);

        Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
        bool desiredExclusiveState =
            FX_ValidateArchiveExclusiveState(context.system);
        if (desiredExclusiveState)
        {
            std::memcpy(
                context.systemBuffers,
                context.desiredBuffers,
                sizeof(*context.systemBuffers));
            std::memcpy(
                context.system,
                context.desiredSystem,
                sizeof(*context.system));
            FX_LinkSystemBuffers(context.system, context.systemBuffers);
            desiredExclusiveState =
                FX_ValidateArchiveExclusiveState(context.system);
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return FX_ArchiveRestoreControlStatus(desiredExclusiveState);
    }

    case Operation::ValidateDesiredState:
        return FX_ArchiveRestoreControlStatus(
            FX_RebuildPoolAllocationStatesNoReport(context.system)
            && FX_ValidatePoolAllocationGraphStateWithScratch(
                context.system,
                &context.physicsScratch->poolGraph)
            && FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
                context.livePhysicsSidecar,
                context.desiredPhysicsEntries,
                context.desiredPhysicsEntryCount,
                false,
                &context.physicsScratch->ownership.expectedTokens,
                &context.physicsScratch->ownership.sidecar));

    case Operation::ValidateDiscardedOriginalPhysics:
        return FX_ArchiveRestoreControlStatus(
            FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
                context.rollbackPhysicsSidecar,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                false,
                &context.physicsScratch->ownership.expectedTokens,
                &context.physicsScratch->ownership.sidecar));

    case Operation::DrainNonLivePhysics:
        return FX_DrainArchivePhysicsSidecarsLocked(
            {context.livePhysicsSidecar,
             context.stagedPhysicsSidecar,
             context.rollbackPhysicsSidecar},
            {false, true, true},
            &context.physicsScratch->ownership);

    case Operation::RollbackPhysicsReplacement:
        return FX_ArchiveRestoreControlStatus(
            fx::physics::RollbackReplacementWithScratch(
                context.livePhysicsSidecar,
                context.rollbackPhysicsSidecar,
                context.stagedPhysicsSidecar,
                &context.physicsScratch->ownership.sidecar)
                == fx::physics::SidecarStatus::Success);

    case Operation::ValidateOriginalTokensInSnapshot:
        return FX_ArchiveRestoreControlStatus(
            FX_ArchiveRetiredTokenTargetsMatch(
                context.originalBuffers->elems,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                context.retirementPlan,
                context.retiredPhysicsEntryCount,
                false));

    case Operation::ValidateOriginalTokensInLiveGraph:
        return FX_ArchiveRestoreControlStatus(
            FX_ArchiveRetiredTokenTargetsMatch(
                context.system->elems,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                context.retirementPlan,
                context.retiredPhysicsEntryCount,
                false));

    case Operation::ReconstructRetiredOriginalPhysics:
        return FX_ReconstructRetiredArchivePhysicsLocked(
            context.livePhysicsSidecar,
            context.originalPhysicsEntries,
            context.originalPhysicsEntryCount,
            context.retirementPlan,
            context.retiredPhysicsEntryCount,
            &context.physicsScratch->ownership.sidecar);

    case Operation::PatchOriginalTokensInSnapshot:
        return FX_ArchiveRestoreControlStatus(
            FX_PatchReconstructedArchivePhysicsTokens(
                context.originalBuffers->elems,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                context.retirementPlan,
                context.retiredPhysicsEntryCount));

    case Operation::PatchOriginalTokensInLiveGraph:
        return FX_ArchiveRestoreControlStatus(
            FX_PatchReconstructedArchivePhysicsTokens(
                context.system->elems,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                context.retirementPlan,
                context.retiredPhysicsEntryCount));

    case Operation::ValidateOriginalPhysics:
        return FX_ArchiveRestoreControlStatus(
            FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
                context.livePhysicsSidecar,
                context.originalPhysicsEntries,
                context.originalPhysicsEntryCount,
                false,
                &context.physicsScratch->ownership.expectedTokens,
                &context.physicsScratch->ownership.sidecar));

    case Operation::PublishOriginalGraph:
    {
        Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
        bool originalExclusiveState =
            FX_ValidateArchiveExclusiveState(context.system);
        if (originalExclusiveState)
        {
            std::memcpy(
                context.systemBuffers,
                context.originalBuffers,
                sizeof(*context.systemBuffers));
            std::memcpy(
                context.system,
                context.originalSystem,
                sizeof(*context.system));
            FX_LinkSystemBuffers(context.system, context.systemBuffers);
            context.originalGraphPublished = true;
            originalExclusiveState =
                FX_ValidateArchiveExclusiveState(context.system);
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return FX_ArchiveRestoreControlStatus(originalExclusiveState);
    }

    case Operation::ValidateOriginalGraph:
        // Live recovery never replaced the graph, so retain its validated
        // allocation sidecars. Snapshot recovery copied the saved graph and
        // must rebuild those sidecars before validating the restored image.
        return FX_ArchiveRestoreControlStatus(
            (!context.originalGraphPublished
                || FX_RebuildPoolAllocationStatesNoReport(context.system))
            && FX_ValidatePoolAllocationGraphStateWithScratch(
                context.system,
                &context.physicsScratch->poolGraph));

    case Operation::PublishSafeEmpty:
        return FX_PublishArchivePhysicsSafeEmptyLocked(
            context.system,
            context.livePhysicsSidecar,
            context.stagedPhysicsSidecar,
            context.rollbackPhysicsSidecar,
            context.physicsScratch);
    }

    return Status::UnsafeFailure;
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

    // Parse and validate the complete serialized table before the first asset
    // registration. Registration may ERR_DROP via longjmp, so keep it ahead of
    // all heap staging; FX_ErrorCleanup abandons this exact TLS-owned lease.
    const std::uint32_t restoreGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    const fx::archive::EffectTableRestoreCallbacks tableCallbacks{
        system,
        FX_ValidateEffectTableRestoreLifecycle,
        FX_RegisterEffectTableRestoreDefinition,
    };
    const fx::archive::EffectTableRestoreResult tableResult =
        fx::archive::RestoreEffectTableNoReport(
            memFile,
            system,
            restoreGeneration,
            tableCallbacks);
    if (tableResult.status
        != fx::archive::EffectTableRestoreStatus::Success)
    {
        const fx::archive::EffectTableRestoreLease *const retainedLease =
            tableResult.lease.ownerCookie ? &tableResult.lease : nullptr;
        FX_ReportEffectTableRestoreFailure(
            retainedLease,
            nullptr,
            "Invalid FX effect-definition table in archive");
    }

    FxSystemBuffers *const restoredBuffers =
        static_cast<FxSystemBuffers *>(Z_Malloc(
            static_cast<int>(sizeof(FxSystemBuffers)),
            "FX_Restore system buffers",
            10));
    if (!restoredBuffers)
    {
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease,
            nullptr,
            "Unable to allocate FX restore staging buffers");
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
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease, restoredBuffers, "Invalid save file");
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
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease, restoredBuffers, "Invalid save file");
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
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease,
            restoredBuffers,
            "Invalid FX effect ring in archive");
    }
    FX_LinkSystemBuffers(&restoredSystem, restoredBuffers);
    if (!FX_ReadArchiveDataNoDrop(
            memFile,
            FX_ARCHIVE_BUFFER_SIZE,
            restoredBuffers))
    {
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease,
            restoredBuffers,
            "Truncated FX pool data in archive");
    }

    FxPoolAllocationGraphScratch *const poolGraphScratch =
        fx::archive::AllocateArchiveRestoreWorkspace<
            FxPoolAllocationGraphScratch>(
            FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
    if (!poolGraphScratch)
    {
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease,
            restoredBuffers,
            "Unable to allocate FX pool validation workspace");
    }
    FxArchivePoolAllocationStates restoredStates{};
    const bool restoredPoolStateValid =
        FX_RebuildArchivePoolAllocationStates(
            &restoredSystem,
            &restoredStates,
            poolGraphScratch);
    const bool poolGraphScratchDestroyed =
        fx::archive::DestroyArchiveRestoreWorkspace(
            poolGraphScratch,
            FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
    if (!poolGraphScratchDestroyed)
    {
        const fx::archive::EffectTableRestoreStatus releaseStatus =
            fx::archive::ReleaseEffectTableRestore(tableResult.lease);
        Z_Free(restoredBuffers, 10);
        Sys_Error(
            "Unable to destroy FX pool validation workspace safely "
            "(effect-table release %u)",
            static_cast<unsigned>(releaseStatus));
        std::abort();
    }
    if (!restoredPoolStateValid)
    {
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease,
            restoredBuffers,
            "Invalid FX pool state in archive");
    }
    if (!FX_FixupEffectDefHandlesNoDrop(
            &restoredSystem, tableResult.lease))
    {
        FX_ReportEffectTableRestoreFailure(
            &tableResult.lease,
            restoredBuffers,
            "Invalid FX effect definition in archive");
    }

    // The pool graph is proven before any staged definition pointer is
    // rewritten. Once every handle is fixed up, release the singleton BSS
    // table immediately; only its captured lifecycle generation is needed by
    // the later archive admission.
    const fx::archive::EffectTableRestoreStatus tableReleaseStatus =
        fx::archive::ReleaseEffectTableRestore(tableResult.lease);
    if (tableReleaseStatus
        != fx::archive::EffectTableRestoreStatus::Success)
    {
        Z_Free(restoredBuffers, 10);
        if (tableReleaseStatus
            != fx::archive::EffectTableRestoreStatus::LifecycleChanged)
        {
            Sys_Error(
                "Unable to release FX effect-definition restore ownership safely");
            std::abort();
        }
        Com_Error(
            ERR_DROP,
            "FX lifecycle changed while restoring effect definitions");
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
    std::size_t physicsEntryByteCount = 0;
    int physicsEntryAllocationSize = 0;
    if (physicsEntryCount != 0)
    {
        if (!FX_TryGetArchivePhysicsEntryByteCount(
                physicsEntryCount,
                &physicsEntryByteCount,
                &physicsEntryAllocationSize))
        {
            Z_Free(restoredBuffers, 10);
            Com_Error(ERR_DROP, "Invalid FX physics staging size");
            return;
        }
        physicsEntries = static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
            physicsEntryAllocationSize,
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
            physicsEntryByteCount);
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

    std::size_t replacedPhysicsEntryByteCount = 0;
    int replacedPhysicsEntryAllocationSize = 0;
    if (!FX_TryGetArchivePhysicsEntryByteCount(
            fx::physics::BODY_LIMIT,
            &replacedPhysicsEntryByteCount,
            &replacedPhysicsEntryAllocationSize))
    {
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Invalid current FX physics staging size");
        return;
    }
    FxArchivePhysicsEntry *const replacedPhysicsEntries =
        static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
            replacedPhysicsEntryAllocationSize,
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
        replacedPhysicsEntryByteCount);

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

    FxArchiveRestoreTransactionWorkspace *const restoreWorkspace =
        FX_AllocateArchiveRestoreTransactionWorkspace();
    if (!restoreWorkspace)
    {
        Z_Free(rollbackBuffers, 10);
        Z_Free(replacedPhysicsEntries, 10);
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "Unable to allocate FX restore transaction workspace");
        return;
    }

    if (!FX_BeginArchive(system, restoreGeneration))
    {
        const bool workspaceDestroyed =
            FX_DestroyArchiveRestoreTransactionWorkspace(
                restoreWorkspace);
        if (!workspaceDestroyed)
            std::abort();
        Z_Free(rollbackBuffers, 10);
        Z_Free(replacedPhysicsEntries, 10);
        if (physicsEntries)
            Z_Free(physicsEntries, 10);
        Z_Free(restoredBuffers, 10);
        Com_Error(ERR_DROP, "FX archive restore could not acquire exclusive ownership");
        return;
    }

    // Nothing below this point performs archive I/O or reports ERR_DROP. Every
    // old body is captured as a validated reconstruction recipe before any
    // selected body is detached and destroyed to make replacement capacity.
    FxSystem &rollbackSystem = restoreWorkspace->rollbackSystem;
    Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    std::memcpy(&rollbackSystem, system, sizeof(rollbackSystem));
    std::memcpy(rollbackBuffers, systemBuffers, sizeof(*rollbackBuffers));
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    // The archive owner keeps iterator exclusivity throughout the transaction.
    // Both staged graph images retain -1 so publication never creates a window
    // that must be repaired by reacquiring the iterator after the copy.
    const bool rollbackSnapshotExclusive =
        FX_ValidateArchiveExclusiveState(system)
        && Sys_AtomicLoad(&rollbackSystem.iteratorCount) == -1;

    // Lock order for archive restore is archive gate/exclusive iterator, a
    // completed FX_ALLOC snapshot interval, then PHYSICS. The snapshot lock is
    // also a drain barrier for an allocator entrant that passed while the gate
    // was transitioning; after it is released, gate state 2 prevents any new
    // archive-aware entrant. This makes the archive owner the only thread that
    // can briefly acquire FX_ALLOC beneath PHYSICS for publication or rollback.
    // Normal paths do not nest these locks. Do not release PHYSICS until either
    // the replacement bodies and graph are both committed or every staged body
    // is destroyed: ODE links a body into its global world during construction.
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    fx::physics::BodySidecar *const livePhysicsSidecar =
        FX_GetPhysicsBodySidecar(system);
    FxArchiveRestoreControlContext &restoreContext =
        restoreWorkspace->control;
    restoreContext.system = system;
    restoreContext.systemBuffers = systemBuffers;
    restoreContext.desiredSystem = &restoredSystem;
    restoreContext.desiredBuffers = restoredBuffers;
    restoreContext.originalSystem = &rollbackSystem;
    restoreContext.originalBuffers = rollbackBuffers;
    restoreContext.desiredPhysicsEntries = physicsEntries;
    restoreContext.desiredPhysicsEntryCount = physicsEntryCount;
    restoreContext.originalPhysicsEntries = replacedPhysicsEntries;
    restoreContext.livePhysicsSidecar = livePhysicsSidecar;
    restoreContext.stagedPhysicsSidecar =
        &restoreWorkspace->stagedPhysicsSidecar;
    restoreContext.rollbackPhysicsSidecar =
        &restoreWorkspace->rollbackPhysicsSidecar;
    restoreContext.physicsScratch = &restoreWorkspace->physicsScratch;
    restoreContext.originalSnapshotExclusive = rollbackSnapshotExclusive;

    const fx::archive::RestoreControlCallbacks restoreCallbacks{
        &restoreContext,
        FX_PerformArchiveRestoreControlOperation,
    };
    const fx::archive::RestoreControlOutcome restoreOutcome =
        fx::archive::RunRestoreControl(restoreCallbacks);
    if (restoreOutcome == fx::archive::RestoreControlOutcome::UnsafeFailure)
    {
        // No graph/sidecar image is safe to admit. Com_Error is not suitable:
        // its longjmp cleanup deliberately releases archive ownership. Keep
        // both archive and native-physics exclusion closed and terminate the
        // process before scratch-sidecar ownership can be lost.
        Sys_Error("FX archive restore could not recover a safe runtime state");
        std::abort();
    }

    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    system->isArchiving = false;
    const bool releasedArchive = FX_EndArchive(system);
    const bool workspaceDestroyed =
        FX_DestroyArchiveRestoreTransactionWorkspace(restoreWorkspace);
    Z_Free(rollbackBuffers, 10);
    Z_Free(replacedPhysicsEntries, 10);
    if (physicsEntries)
        Z_Free(physicsEntries, 10);
    Z_Free(restoredBuffers, 10);
    if (restoreOutcome
            != fx::archive::RestoreControlOutcome::DesiredPublished
        || !releasedArchive
        || !workspaceDestroyed)
    {
        Com_Error(
            ERR_DROP,
            "Unable to publish restored FX archive state");
        return;
    }
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

    const FxEffectTableSaveOutcome effectTableOutcome =
        FX_SaveEffectTableNoDrop(memFile);
    if (effectTableOutcome != FxEffectTableSaveOutcome::Success)
    {
        if (effectTableOutcome
            == FxEffectTableSaveOutcome::AllocationFailed)
        {
            Com_Error(
                ERR_DROP,
                "Unable to allocate FX effect-definition save snapshot");
        }
        else if (effectTableOutcome
                 == FxEffectTableSaveOutcome::WriteFailed)
        {
            Com_Error(
                ERR_DROP,
                "FX effect-definition archive ran out of memory");
        }
        else
        {
            Com_Error(
                ERR_DROP,
                "Invalid FX effect-definition table while saving archive");
        }
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

    std::size_t physicsEntryByteCount = 0;
    int physicsEntryAllocationSize = 0;
    if (!FX_TryGetArchivePhysicsEntryByteCount(
            fx::physics::BODY_LIMIT,
            &physicsEntryByteCount,
            &physicsEntryAllocationSize))
    {
        Z_Free(bufferSnapshot, 10);
        Com_Error(ERR_DROP, "Invalid FX physics save staging size");
        return;
    }
    FxArchivePhysicsEntry *const physicsEntries =
        static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
            physicsEntryAllocationSize,
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
        physicsEntryByteCount);

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
            fx::physics::BODY_LIMIT,
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
    bool snapshotTokensCanonical = true;
    for (std::size_t index = 0; index < physicsEntryCount; ++index)
    {
        const std::size_t ownerIndex = physicsEntries[index].ownerIndex;
        if (ownerIndex >= MAX_ELEMS)
        {
            snapshotTokensCanonical = false;
            break;
        }
        // Serialized tokens carry no identity across restore. A stable,
        // nonzero per-owner marker keeps the legacy x86 image structurally
        // valid (including for older readers) without leaking pointer bits.
        const fx::physics::BodyToken marker =
            static_cast<fx::physics::BodyToken>(ownerIndex + 1u);
        bufferSnapshot->elems[ownerIndex].item.physObjId =
            fx::physics::TokenToLegacyField(marker);
    }
    if (!snapshotTokensCanonical
        || !FX_NormalizeArchiveEffectRing(&systemSnapshot))
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

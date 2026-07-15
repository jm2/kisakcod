#include "fx_system.h"
#include "fx_archive_capacity.h"
#include "fx_archive_physics_batch_control.h"
#include "fx_archive_reader_disk32.h"
#include "fx_archive_restore_candidate_disk32.h"
#include "fx_archive_restore_control.h"
#include "fx_archive_restore_workspace.h"
#include "fx_archive_semantics.h"
#include "fx_effect_table_save.h"
#include "fx_effect_table_restore.h"
#include "fx_physics_sidecar.h"
#include "fx_pool.h"
#include "fx_pool_graph.h"
#include "fx_snapshot_publication.h"

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
static_assert(
    FX_ELEM_DEF_RUNTIME_SIZE
    == fx::archive::layout::ELEM_DEF_STRIDE);
static_assert(
    alignof(FxElemDef) == FX_ELEM_DEF_RUNTIME_ALIGNMENT);
static_assert(
    offsetof(FxElemDef, flags)
    == fx::archive::layout::ELEM_DEF_FLAGS_OFFSET);
static_assert(
    offsetof(FxElemDef, spawn)
    == fx::archive::layout::ELEM_DEF_SPAWN_OFFSET);
static_assert(
    offsetof(FxElemDef, spawnDelayMsec)
    == fx::archive::layout::ELEM_DEF_SPAWN_DELAY_OFFSET);
static_assert(
    offsetof(FxElemDef, lifeSpanMsec)
    == fx::archive::layout::ELEM_DEF_LIFE_SPAN_OFFSET);
static_assert(
    offsetof(FxElemDef, elemType)
    == fx::archive::layout::ELEM_DEF_ELEM_TYPE_OFFSET);
static_assert(
    offsetof(FxElemDef, visualCount)
    == fx::archive::layout::ELEM_DEF_VISUAL_COUNT_OFFSET);
static_assert(
    offsetof(FxElemDef, visuals)
    == fx::archive::layout::ELEM_DEF_VISUALS_OFFSET);
static_assert(
    offsetof(FxElemDef, trailDef)
    == fx::archive::layout::ELEM_DEF_TRAIL_DEF_OFFSET);
static_assert(FX_ELEM_TYPE_SPRITE_BILLBOARD == 0);
static_assert(FX_ELEM_TYPE_SPRITE_ORIENTED == 1);
static_assert(FX_ELEM_TYPE_TAIL == 2);
static_assert(FX_ELEM_TYPE_TRAIL == 3);
static_assert(FX_ELEM_TYPE_CLOUD == 4);
static_assert(FX_ELEM_TYPE_MODEL == 5);
static_assert(FX_ELEM_TYPE_OMNI_LIGHT == 6);
static_assert(FX_ELEM_TYPE_SPOT_LIGHT == 7);
static_assert(FX_ELEM_TYPE_COUNT == 11);
static_assert(MAX_GENTITIES == 1024);
static_assert(CLIENT_DOBJ_HANDLE_MAX == MAX_GENTITIES + 128);
static_assert(FX_DOBJ_HANDLE_NONE == 4095);
static_assert(FX_BONE_INDEX_NONE == 2047);
static_assert(
    fx::physics::BODY_LIMIT
    == fx::archive::FX_ARCHIVE_PHYSICS_BODY_LIMIT);
static_assert(
    fx::physics::INVALID_BODY_TOKEN
    == fx::archive::FX_ARCHIVE_INVALID_PHYSICS_TOKEN);

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
RUNTIME_SIZE(FxArchivePhysicsOwnershipScratch, 0x5808, 0x7010);

struct FxArchiveSaveSnapshotWorkspace
{
    FxSystem serializedSystem{};
    FxSystem validationSystem{};
    FxSystemBuffers buffers{};
    FxArchivePoolAllocationStates allocationStates{};
    FxPoolAllocationGraphScratch poolGraph{};
    FxArchivePhysicsOwnershipScratch physics{};
    std::uint8_t readVisibilitySelector = 0;
    std::uint8_t writeVisibilitySelector = 0;
};
static_assert(
    fx::archive::IsSupportedArchiveRestoreWorkspace<
        FxArchiveSaveSnapshotWorkspace>);

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
// Restored timestamps feed legacy signed-int additions and differences. Bound
// each accepted delay/lifespan/loop horizon to one day, then reserve four such
// horizons around every absolute time: finite loop offset + spawn delay +
// lifespan + a full day of post-restore update runway.
constexpr std::int64_t FX_ARCHIVE_DURATION_LIMIT_MSEC =
    24ll * 60ll * 60ll * 1000ll;
constexpr std::int64_t FX_ARCHIVE_TIME_HEADROOM_MSEC =
    4ll * FX_ARCHIVE_DURATION_LIMIT_MSEC;

// CoD4 maps conventionally live inside +/-131072 units. The archive permits
// eight times that range for custom content, while rejecting finite values
// large enough to overflow common squared-distance and integration math.
constexpr float FX_ARCHIVE_SPATIAL_COMPONENT_MAX = 1048576.0f;
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

struct FxEffectTableSaveStaging
{
    void *storage = nullptr;
    fx::archive::EffectTableSaveSnapshot *snapshot = nullptr;
};

void FX_DestroyEffectTableSaveStaging(
    FxEffectTableSaveStaging *const staging) noexcept
{
    if (!staging)
        return;
    if (staging->snapshot
        && !fx::archive::DestroyEffectTableSaveSnapshot(
            staging->snapshot))
    {
        std::abort();
    }
    if (staging->storage)
        Z_Free(staging->storage, 10);
    staging->storage = nullptr;
    staging->snapshot = nullptr;
}

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
    const std::uintptr_t nativeIdentity =
        reinterpret_cast<std::uintptr_t>(effectDef);
    capture->status =
        fx::archive::AppendEffectTableSaveDefinitionNoReport(
            capture->snapshot,
            name,
            nativeIdentity);
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

FxEffectTableSaveOutcome FX_StageEffectTableNoDrop(
    FxEffectTableSaveStaging *const staging)
{
    const std::size_t workspaceSize =
        fx::archive::EffectTableSaveSnapshotSize();
    if (!staging || staging->storage || staging->snapshot
        || workspaceSize == 0
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
            workspaceSize,
            sizeof(void *) == sizeof(std::uint32_t)
                ? fx::archive::EffectTableSaveKeyPolicy::LegacyPointerBits
                : fx::archive::EffectTableSaveKeyPolicy::OpaqueSequential);
    if (!snapshot)
    {
        Z_Free(storage, 10);
        return FxEffectTableSaveOutcome::InvalidTable;
    }

    staging->storage = storage;
    staging->snapshot = snapshot;
    const fx::archive::EffectTableSaveStatus status =
        FX_CaptureEffectTableNoReport(snapshot);
    if (status == fx::archive::EffectTableSaveStatus::Success)
        return FxEffectTableSaveOutcome::Success;
    FX_DestroyEffectTableSaveStaging(staging);
    return FxEffectTableSaveOutcome::InvalidTable;
}

FxEffectTableSaveOutcome FX_WriteStagedEffectTableNoDrop(
    FxEffectTableSaveStaging *const staging,
    MemoryFile *const memFile) noexcept
{
    if (!staging || !staging->storage || !staging->snapshot || !memFile)
        return FxEffectTableSaveOutcome::InvalidTable;

    const fx::archive::EffectTableSaveCallbacks callbacks{
        memFile,
        FX_WriteEffectTableSaveBytes,
    };
    const fx::archive::EffectTableSaveStatus status =
        fx::archive::WriteEffectTableSaveSnapshotNoReport(
            staging->snapshot,
            callbacks);
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

bool FX_EffectTableRestoreReleaseIsSafe(
    const fx::archive::EffectTableRestoreStatus status) noexcept
{
    return status == fx::archive::EffectTableRestoreStatus::Success
        || status
            == fx::archive::EffectTableRestoreStatus::LifecycleChanged;
}

[[noreturn]] void FX_ReportEffectTableRestoreFailure(
    const fx::archive::EffectTableRestoreLease *const lease,
    const char *const message)
{
    fx::archive::EffectTableRestoreStatus releaseStatus =
        fx::archive::EffectTableRestoreStatus::Success;
    if (lease && lease->ownerCookie)
    {
        releaseStatus =
            fx::archive::ReleaseEffectTableRestore(*lease);
    }
    if (!FX_EffectTableRestoreReleaseIsSafe(releaseStatus))
    {
        Sys_Error(
            "Unable to release FX effect-definition restore ownership safely");
        std::abort();
    }
    Com_Error(ERR_DROP, "%s", message);
    std::abort();
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

struct FxArchivePhysicsEntrySink
{
    FxArchivePhysicsEntry *entries;
    std::size_t entryCapacity;
};

bool FX_AppendArchivePhysicsEntry(
    void *const opaqueContext,
    const fx::archive::FxArchiveSemanticPhysicsDescriptor &descriptor,
    const std::size_t physicsIndex) noexcept
{
    if (!opaqueContext)
        return false;

    auto &context =
        *static_cast<FxArchivePhysicsEntrySink *>(opaqueContext);
    if (!context.entries)
        return true;
    if (physicsIndex >= context.entryCapacity)
        return false;

    FxArchivePhysicsEntry &entry = context.entries[physicsIndex];
    std::memset(&entry, 0, sizeof(entry));
    entry.elem = descriptor.elem;
    entry.model = descriptor.model;
    entry.ownerIndex = descriptor.ownerIndex;
    entry.token = static_cast<fx::physics::BodyToken>(descriptor.token);
    return true;
}

bool FX_CaptureArchivePhysicsStates(
    const FxSystem *const liveOwner,
    const std::int32_t archiveTime,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCount,
    FxArchivePhysicsOwnershipScratch *const ownershipScratch) noexcept
{
    if (!liveOwner || (entryCount != 0 && !entries) || !ownershipScratch)
        return false;

    const fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(liveOwner);
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const bool physicsStatesValid =
        FX_ValidateArchivePhysicsOwnershipLockedWithScratch(
            sidecar,
            entries,
            entryCount,
            true,
            &ownershipScratch->expectedTokens,
            &ownershipScratch->sidecar);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    if (!physicsStatesValid)
        return false;

    for (std::size_t index = 0; index < entryCount; ++index)
    {
        if (!FX_ValidateArchiveBodyState(entries[index].state))
            return false;
        FX_NormalizeArchiveBodyState(
            &entries[index].state, archiveTime);
    }
    return true;
}

bool FX_CollectArchivePhysicsEntries(
    FxSystem *const system,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCapacity,
    std::size_t *const outEntryCount,
    const bool captureStates,
    FxArchivePhysicsOwnershipScratch *const ownershipScratch,
    std::int16_t *const outSpotLightBoltDobj) noexcept
{
    if (!outEntryCount)
        return false;

    FxArchivePhysicsEntrySink sink{entries, entryCapacity};
    const fx::archive::FxArchiveSemanticCallbacks callbacks{
        &sink, nullptr, FX_AppendArchivePhysicsEntry};
    fx::archive::FxArchiveSemanticResult result{};
    if (!fx::archive::TryValidateFxArchiveSemanticsNoReport(
            system, callbacks, &result))
    {
        return false;
    }

    const std::size_t entryCount =
        static_cast<std::size_t>(result.physicsBodyCount);
    if (captureStates
        && !FX_CaptureArchivePhysicsStates(
            system,
            system->msecNow,
            entries,
            entryCount,
            ownershipScratch))
    {
        return false;
    }

    if (outSpotLightBoltDobj)
        *outSpotLightBoltDobj = result.spotLightBoltDobj;
    *outEntryCount = entryCount;
    return true;
}

bool FX_ValidateArchiveEffectDefinitionReferences(
    const FxSystem *const system,
    const fx::archive::EffectTableSaveSnapshot *const effectTableSnapshot)
    noexcept
{
    if (!system || !system->effects || !effectTableSnapshot
        || !FX_ArchiveEffectRingIsValid(system))
    {
        return false;
    }

    const auto definitionIsStaged =
        [effectTableSnapshot](const FxEffect *const effect) noexcept {
            if (!effect)
                return false;
            fx::archive::EffectDefinitionKey32 diskKey{};
            return fx::archive::FindEffectTableSaveDefinitionKey(
                effectTableSnapshot,
                reinterpret_cast<std::uintptr_t>(effect->def),
                &diskKey);
        };
    for (std::int64_t activeIndex = system->firstActiveEffect;
         activeIndex < system->firstFreeEffect;
         ++activeIndex)
    {
        const FxEffect *const effect = FxDecodeHandle<
            FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                system->effects,
                system->allEffectHandles[
                    static_cast<std::size_t>(activeIndex)
                    & (MAX_EFFECTS - 1)]);
        if (!definitionIsStaged(effect))
            return false;
    }

    const std::int32_t spotEffectCount =
        Sys_AtomicLoad(&system->activeSpotLightEffectCount);
    if (spotEffectCount < 0 || spotEffectCount > 1)
        return false;
    if (spotEffectCount == 1)
    {
        const FxEffect *const spotLightEffect = FxDecodeHandle<
            FxEffect, MAX_EFFECTS, FxEffect::HANDLE_SCALE>(
                system->effects, system->activeSpotLightEffectHandle);
        if (!definitionIsStaged(spotLightEffect))
            return false;
    }
    return true;
}

bool FX_ValidateArchiveCopiedSnapshot(
    const FxSystem *const serializedSystem,
    FxSystemBuffers *const bufferSnapshot,
    const FxSystemBuffers *const liveBuffers,
    const fx::archive::EffectTableSaveSnapshot *const effectTableSnapshot,
    FxArchiveSaveSnapshotWorkspace *const workspace,
    FxArchivePhysicsEntry *const physicsEntries,
    const std::size_t physicsEntryCapacity,
    std::size_t *const outPhysicsEntryCount,
    std::int16_t *const outSpotLightBoltDobj) noexcept
{
    if (!serializedSystem || !bufferSnapshot || !liveBuffers
        || !effectTableSnapshot || !workspace
        || !outPhysicsEntryCount || !outSpotLightBoltDobj
        || serializedSystem->effects != liveBuffers->effects
        || serializedSystem->elems != liveBuffers->elems
        || serializedSystem->trails != liveBuffers->trails
        || serializedSystem->trailElems != liveBuffers->trailElems
        || serializedSystem->visState != liveBuffers->visState
        || serializedSystem->deferredElems != liveBuffers->deferredElems
        || !serializedSystem->isArchiving
        || Sys_AtomicLoad(&serializedSystem->iteratorCount) != -1)
    {
        return false;
    }

    // Preserve the serialized legacy pointer image untouched. Resolve its two
    // visibility pointers to bounded selectors, then relink only the separate
    // validation view to the copied buffers. Disk32 can consume the same 0/1
    // selector semantics without treating process addresses as pointers.
    std::uint8_t readSelector = 0;
    std::uint8_t writeSelector = 0;
    if (!FX_TryDeriveVisibilitySelectors(
            &liveBuffers->visState[0],
            &liveBuffers->visState[1],
            serializedSystem->visStateBufferRead,
            serializedSystem->visStateBufferWrite,
            &readSelector,
            &writeSelector))
    {
        return false;
    }

    FxSystem *const validationSystem = &workspace->validationSystem;
    std::memcpy(
        validationSystem, serializedSystem, sizeof(*validationSystem));
    FX_LinkSystemBuffers(validationSystem, bufferSnapshot);
    validationSystem->visStateBufferRead =
        &bufferSnapshot->visState[readSelector];
    validationSystem->visStateBufferWrite =
        &bufferSnapshot->visState[writeSelector];

    // Registered definitions follow the same lifetime contract as ordinary
    // lock-free FX update/draw readers: they remain live while the FX system
    // is initialized, and archive ownership excludes FX lifecycle teardown
    // (including LoadObj unregistration).  The copied graph can nevertheless
    // contain arbitrary pointer bits when runtime state is corrupt.  Prove
    // every live definition's provenance against the already validated table
    // using pointer values alone before any validation below reads definition
    // fields or follows elemDefs.
    if (!FX_ValidateArchiveEffectDefinitionReferences(
            validationSystem, effectTableSnapshot))
    {
        return false;
    }

    std::size_t physicsEntryCount = 0;
    std::int16_t spotLightBoltDobj = -1;
    if (!FX_RebuildArchivePoolAllocationStates(
            validationSystem,
            &workspace->allocationStates,
            &workspace->poolGraph)
        || !FX_CollectArchivePhysicsEntries(
            validationSystem,
            physicsEntries,
            physicsEntryCapacity,
            &physicsEntryCount,
            false,
            nullptr,
            &spotLightBoltDobj))
    {
        return false;
    }

    workspace->readVisibilitySelector = readSelector;
    workspace->writeVisibilitySelector = writeSelector;
    *outPhysicsEntryCount = physicsEntryCount;
    *outSpotLightBoltDobj = spotLightBoltDobj;
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

bool FX_ArchiveVisibilitySelectorsMatch(
    const FxSystem *const system,
    const FxSystemBuffers *const buffers,
    const FxVisibilityBufferSelectors &expectedSelectors) noexcept
{
    return system && buffers
        && system->visState == buffers->visState
        && FX_VisibilitySelectorsRoundTrip(
            &buffers->visState[0],
            &buffers->visState[1],
            system->visStateBufferRead,
            system->visStateBufferWrite,
            expectedSelectors);
}

struct FxArchiveRestoreControlContext
{
    FxSystem *system;
    FxSystemBuffers *systemBuffers;
    FxSystem *desiredSystem;
    FxSystemBuffers *desiredBuffers;
    FxVisibilityBufferSelectors desiredVisibilitySelectors{};
    FxSystem *originalSystem;
    FxSystemBuffers *originalBuffers;
    FxVisibilityBufferSelectors originalVisibilitySelectors{};
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
        "FX archive checked workspace",
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

[[nodiscard]] fx::archive::FxArchiveDisk32ReaderWorkspace *
FX_AllocateArchiveDisk32ReaderWorkspace() noexcept
{
    return fx::archive::AllocateArchiveRestoreWorkspace<
        fx::archive::FxArchiveDisk32ReaderWorkspace>(
            FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
}

[[nodiscard]] bool FX_DestroyArchiveDisk32ReaderWorkspace(
    fx::archive::FxArchiveDisk32ReaderWorkspace *const workspace) noexcept
{
    return fx::archive::DestroyArchiveRestoreWorkspace(
        workspace,
        FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
}

[[nodiscard]] fx::archive::FxArchiveRestoreCandidateDisk32Workspace *
FX_AllocateArchiveRestoreCandidateDisk32Workspace() noexcept
{
    return fx::archive::AllocateArchiveRestoreWorkspace<
        fx::archive::FxArchiveRestoreCandidateDisk32Workspace>(
            FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
}

[[nodiscard]] bool FX_DestroyArchiveRestoreCandidateDisk32Workspace(
    fx::archive::FxArchiveRestoreCandidateDisk32Workspace *const workspace)
    noexcept
{
    return fx::archive::DestroyArchiveRestoreWorkspace(
        workspace,
        FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
}

[[nodiscard]] FxArchiveSaveSnapshotWorkspace *
FX_AllocateArchiveSaveSnapshotWorkspace() noexcept
{
    return fx::archive::AllocateArchiveRestoreWorkspace<
        FxArchiveSaveSnapshotWorkspace>(
        FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY);
}

void FX_DestroyArchiveSaveSnapshotWorkspace(
    FxArchiveSaveSnapshotWorkspace *const workspace) noexcept
{
    if (!fx::archive::DestroyArchiveRestoreWorkspace(
            workspace,
            FX_ARCHIVE_RESTORE_WORKSPACE_MEMORY))
    {
        std::abort();
    }
}

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

struct FxArchiveDisk32RestoreStaging final
{
    fx::archive::FxArchiveDisk32ReaderWorkspace *reader = nullptr;
    fx::archive::FxArchiveRestoreCandidateDisk32Workspace *candidate =
        nullptr;
    FxArchivePhysicsEntry *desiredPhysicsEntries = nullptr;
    FxArchivePhysicsEntry *replacedPhysicsEntries = nullptr;
    FxSystemBuffers *rollbackBuffers = nullptr;
    FxArchiveRestoreTransactionWorkspace *transaction = nullptr;
};

[[nodiscard]] bool FX_DestroyArchiveDisk32RestoreStaging(
    FxArchiveDisk32RestoreStaging *const staging) noexcept
{
    if (!staging)
        return false;

    bool destroyed = true;
    if (!FX_DestroyArchiveRestoreTransactionWorkspace(
            staging->transaction))
    {
        destroyed = false;
    }
    else
    {
        staging->transaction = nullptr;
    }
    if (!FX_DestroyArchiveDisk32ReaderWorkspace(staging->reader))
    {
        destroyed = false;
    }
    else
    {
        staging->reader = nullptr;
    }
    if (!FX_DestroyArchiveRestoreCandidateDisk32Workspace(
            staging->candidate))
    {
        destroyed = false;
    }
    else
    {
        staging->candidate = nullptr;
    }

    if (staging->rollbackBuffers)
    {
        Z_Free(staging->rollbackBuffers, 10);
        staging->rollbackBuffers = nullptr;
    }
    if (staging->replacedPhysicsEntries)
    {
        Z_Free(staging->replacedPhysicsEntries, 10);
        staging->replacedPhysicsEntries = nullptr;
    }
    if (staging->desiredPhysicsEntries)
    {
        Z_Free(staging->desiredPhysicsEntries, 10);
        staging->desiredPhysicsEntries = nullptr;
    }
    return destroyed;
}

[[nodiscard]] bool FX_PrepareArchiveDisk32PhysicsEntries(
    const fx::archive::FxArchiveRestoreCandidateDisk32ReadyView &view,
    FxArchivePhysicsEntry *const entries,
    const std::size_t entryCapacity,
    FxVisibilityBufferSelectors *const outSelectors) noexcept
{
    if (!view.system || !view.buffers || !outSelectors
        || view.archivedSystemAddress.value == 0
        || view.physicsBodyCount > fx::physics::BODY_LIMIT
        || (view.physicsBodyCount != 0 && !view.physicsBodies)
        || (view.physicsBodyCount == 0 && view.physicsBodies)
        || view.physicsBodyCount > entryCapacity
        || (view.physicsBodyCount != 0 && !entries))
    {
        return false;
    }

    FxVisibilityBufferSelectors selectors{};
    if (!FX_TryDeriveVisibilitySelectorPair(
            &view.buffers->visState[0],
            &view.buffers->visState[1],
            view.system->visStateBufferRead,
            view.system->visStateBufferWrite,
            &selectors)
        || !FX_ArchiveVisibilitySelectorsMatch(
            view.system,
            view.buffers,
            selectors))
    {
        return false;
    }

    for (std::size_t index = 0;
         index < view.physicsBodyCount;
         ++index)
    {
        const auto &body = view.physicsBodies[index];
        if (body.ownerIndex >= MAX_ELEMS
            || body.elem
                != &view.buffers->elems[body.ownerIndex].item
            || !body.model
            || body.token == fx::archive::FX_ARCHIVE_INVALID_PHYSICS_TOKEN
            || !FX_ValidateArchiveBodyState(body.state))
        {
            return false;
        }
    }

    for (std::size_t index = 0;
         index < view.physicsBodyCount;
         ++index)
    {
        const auto &body = view.physicsBodies[index];
        FxArchivePhysicsEntry &entry = entries[index];
        std::memset(&entry, 0, sizeof(entry));
        entry.elem = body.elem;
        entry.model = body.model;
        entry.state = body.state;
        entry.ownerIndex = body.ownerIndex;
        entry.token = static_cast<fx::physics::BodyToken>(body.token);
    }

    *outSelectors = selectors;
    return true;
}

[[noreturn]] void FX_ReportArchiveDisk32RestoreFailure(
    const fx::archive::EffectTableRestoreLease *const lease,
    FxArchiveDisk32RestoreStaging *const staging,
    const char *const message)
{
    if (!staging
        || !FX_DestroyArchiveDisk32ReaderWorkspace(staging->reader))
    {
        Sys_Error(
            "Unable to destroy FX Disk32 reader staging safely");
        std::abort();
    }
    staging->reader = nullptr;

    fx::archive::EffectTableRestoreStatus releaseStatus =
        fx::archive::EffectTableRestoreStatus::Success;
    if (lease && lease->ownerCookie)
        releaseStatus = fx::archive::ReleaseEffectTableRestore(*lease);
    if (!FX_EffectTableRestoreReleaseIsSafe(releaseStatus))
    {
        Sys_Error(
            "Unable to release FX effect-definition restore ownership safely");
        std::abort();
    }
    if (!FX_DestroyArchiveDisk32RestoreStaging(staging))
    {
        Sys_Error(
            "Unable to destroy FX Disk32 restore staging safely");
        std::abort();
    }
    Com_Error(ERR_DROP, "%s", message);
    std::abort();
}

[[nodiscard]] const char *FX_ArchiveDisk32ReaderFailureMessage(
    const fx::archive::FxArchiveDisk32ReaderStatus status) noexcept
{
    using Status = fx::archive::FxArchiveDisk32ReaderStatus;
    switch (status)
    {
    case Status::Success:
        return "Unexpected successful FX archive reader status";
    case Status::Busy:
        return "FX archive reader is already active";
    case Status::InvalidArgument:
        return "Invalid FX archive reader request";
    case Status::InvalidLease:
        return "FX lifecycle changed while reading the archive";
    case Status::InvalidMemoryFile:
        return "Invalid FX archive stream state";
    case Status::TruncatedInput:
        return "Truncated FX archive tail";
    case Status::InvalidStructuralImage:
        return "Invalid FX archive graph";
    case Status::InvalidSemanticImage:
        return "Invalid FX archive semantics";
    case Status::InvalidRelocation:
        return "Invalid FX archive relocation record";
    case Status::InvalidBodyState:
        return "Invalid FX archive physics state";
    }
    return "Unknown FX archive reader failure";
}

[[nodiscard]] const char *FX_ArchiveDisk32CandidateFailureMessage(
    const fx::archive::FxArchiveRestoreCandidateDisk32Status status) noexcept
{
    using Status =
        fx::archive::FxArchiveRestoreCandidateDisk32Status;
    switch (status)
    {
    case Status::Success:
        return "Unexpected successful FX restore candidate status";
    case Status::Busy:
        return "FX restore candidate is already active";
    case Status::InvalidArgument:
        return "Invalid FX restore candidate request";
    case Status::InvalidLease:
        return "FX lifecycle changed while materializing the restore";
    case Status::InvalidGraph:
        return "Invalid FX restore candidate graph";
    case Status::InvalidPhysics:
        return "Invalid FX restore candidate physics";
    }
    return "Unknown FX restore candidate failure";
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
            && FX_ArchiveVisibilitySelectorsMatch(
                context.desiredSystem,
                context.desiredBuffers,
                context.desiredVisibilitySelectors)
            && FX_ArchiveVisibilitySelectorsMatch(
                context.originalSystem,
                context.originalBuffers,
                context.originalVisibilitySelectors)
            && FX_ArchiveVisibilitySelectorsMatch(
                context.system,
                context.systemBuffers,
                context.originalVisibilitySelectors)
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
                nullptr,
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
        if (!context.system || !context.systemBuffers)
            return Status::RecoverableFailure;
        const bool stagedSelectorsValid =
            FX_ArchiveVisibilitySelectorsMatch(
                context.desiredSystem,
                context.desiredBuffers,
                context.desiredVisibilitySelectors);
        const FxVisState *liveReadState = nullptr;
        FxVisState *liveWriteState = nullptr;
        const bool liveSelectorsResolved =
            FX_TryResolveVisibilitySelectors(
                &context.systemBuffers->visState[0],
                &context.systemBuffers->visState[1],
                context.desiredVisibilitySelectors,
                &liveReadState,
                &liveWriteState);
        if (!stagedSelectorsValid || !liveSelectorsResolved)
            return Status::RecoverableFailure;

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
            context.system->visStateBufferRead = liveReadState;
            context.system->visStateBufferWrite = liveWriteState;
            desiredExclusiveState =
                FX_ArchiveVisibilitySelectorsMatch(
                    context.system,
                    context.systemBuffers,
                    context.desiredVisibilitySelectors)
                && FX_ValidateArchiveExclusiveState(context.system);
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return FX_ArchiveRestoreControlStatus(desiredExclusiveState);
    }

    case Operation::ValidateDesiredState:
        return FX_ArchiveRestoreControlStatus(
            FX_ArchiveVisibilitySelectorsMatch(
                context.system,
                context.systemBuffers,
                context.desiredVisibilitySelectors)
            && FX_RebuildPoolAllocationStatesNoReport(context.system)
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
        if (!context.system || !context.systemBuffers)
            return Status::RecoverableFailure;
        const bool stagedSelectorsValid =
            FX_ArchiveVisibilitySelectorsMatch(
                context.originalSystem,
                context.originalBuffers,
                context.originalVisibilitySelectors);
        const FxVisState *liveReadState = nullptr;
        FxVisState *liveWriteState = nullptr;
        const bool liveSelectorsResolved =
            FX_TryResolveVisibilitySelectors(
                &context.systemBuffers->visState[0],
                &context.systemBuffers->visState[1],
                context.originalVisibilitySelectors,
                &liveReadState,
                &liveWriteState);
        if (!stagedSelectorsValid || !liveSelectorsResolved)
            return Status::RecoverableFailure;

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
            context.system->visStateBufferRead = liveReadState;
            context.system->visStateBufferWrite = liveWriteState;
            context.originalGraphPublished = true;
            originalExclusiveState =
                FX_ArchiveVisibilitySelectorsMatch(
                    context.system,
                    context.systemBuffers,
                    context.originalVisibilitySelectors)
                && FX_ValidateArchiveExclusiveState(context.system);
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return FX_ArchiveRestoreControlStatus(originalExclusiveState);
    }

    case Operation::ValidateOriginalGraph:
        // Live recovery never replaced the graph, so retain its validated
        // allocation sidecars. Snapshot recovery copied the saved graph and
        // must rebuild those sidecars before validating the restored image.
        return FX_ArchiveRestoreControlStatus(
            FX_ArchiveVisibilitySelectorsMatch(
                context.system,
                context.systemBuffers,
                context.originalVisibilitySelectors)
            && (!context.originalGraphPublished
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
            "Invalid FX effect-definition table in archive");
    }

    FxArchiveDisk32RestoreStaging staging{};
    staging.reader = FX_AllocateArchiveDisk32ReaderWorkspace();
    if (!staging.reader)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Unable to allocate FX Disk32 reader workspace");
    }
    const fx::archive::FxArchiveDisk32ReaderStatus readerStatus =
        fx::archive::TryReadFxArchiveDisk32NoReport(
            memFile,
            tableResult.lease,
            staging.reader);
    if (readerStatus
        != fx::archive::FxArchiveDisk32ReaderStatus::Success)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            FX_ArchiveDisk32ReaderFailureMessage(readerStatus));
    }

    staging.candidate =
        FX_AllocateArchiveRestoreCandidateDisk32Workspace();
    if (!staging.candidate)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Unable to allocate FX restore candidate workspace");
    }
    const fx::archive::FxArchiveRestoreCandidateDisk32Status
        candidateStatus =
            fx::archive::TryBuildFxArchiveRestoreCandidateDisk32(
                staging.reader,
                tableResult.lease,
                staging.candidate);
    if (candidateStatus
        != fx::archive::FxArchiveRestoreCandidateDisk32Status::Success)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            FX_ArchiveDisk32CandidateFailureMessage(candidateStatus));
    }

    fx::archive::FxArchiveRestoreCandidateDisk32ReadyView candidateView{};
    if (!fx::archive::TryGetFxArchiveRestoreCandidateDisk32ReadyView(
            staging.candidate,
            tableResult.lease,
            &candidateView))
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Invalid FX restore candidate view");
    }

    FxSystem *const restoredSystem = candidateView.system;
    FxSystemBuffers *const restoredBuffers = candidateView.buffers;
    const std::size_t physicsEntryCount =
        static_cast<std::size_t>(candidateView.physicsBodyCount);
    std::size_t physicsEntryByteCount = 0;
    int physicsEntryAllocationSize = 0;
    if (physicsEntryCount != 0)
    {
        if (!FX_TryGetArchivePhysicsEntryByteCount(
                physicsEntryCount,
                &physicsEntryByteCount,
                &physicsEntryAllocationSize))
        {
            FX_ReportArchiveDisk32RestoreFailure(
                &tableResult.lease,
                &staging,
                "Invalid FX physics staging size");
        }
        staging.desiredPhysicsEntries =
            static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
                physicsEntryAllocationSize,
                "FX_Restore physics staging",
                10));
        if (!staging.desiredPhysicsEntries)
        {
            FX_ReportArchiveDisk32RestoreFailure(
                &tableResult.lease,
                &staging,
                "Unable to allocate FX physics restore staging");
        }
    }

    FxVisibilityBufferSelectors desiredVisibilitySelectors{};
    if (!FX_PrepareArchiveDisk32PhysicsEntries(
            candidateView,
            staging.desiredPhysicsEntries,
            physicsEntryCount,
            &desiredVisibilitySelectors))
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Invalid FX restore physics candidate");
    }
    FxArchivePhysicsEntry *const physicsEntries =
        staging.desiredPhysicsEntries;

    std::size_t replacedPhysicsEntryByteCount = 0;
    int replacedPhysicsEntryAllocationSize = 0;
    if (!FX_TryGetArchivePhysicsEntryByteCount(
            fx::physics::BODY_LIMIT,
            &replacedPhysicsEntryByteCount,
            &replacedPhysicsEntryAllocationSize))
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Invalid current FX physics staging size");
    }
    staging.replacedPhysicsEntries =
        static_cast<FxArchivePhysicsEntry *>(Z_Malloc(
            replacedPhysicsEntryAllocationSize,
            "FX_Restore replaced physics staging",
            10));
    if (!staging.replacedPhysicsEntries)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Unable to allocate current FX physics staging");
    }
    std::memset(
        staging.replacedPhysicsEntries,
        0,
        replacedPhysicsEntryByteCount);
    FxArchivePhysicsEntry *const replacedPhysicsEntries =
        staging.replacedPhysicsEntries;

    staging.rollbackBuffers =
        static_cast<FxSystemBuffers *>(Z_Malloc(
            static_cast<int>(sizeof(FxSystemBuffers)),
            "FX_Restore rollback buffers",
            10));
    if (!staging.rollbackBuffers)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Unable to allocate FX restore rollback state");
    }
    FxSystemBuffers *const rollbackBuffers = staging.rollbackBuffers;

    staging.transaction =
        FX_AllocateArchiveRestoreTransactionWorkspace();
    if (!staging.transaction)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "Unable to allocate FX restore transaction workspace");
    }
    FxArchiveRestoreTransactionWorkspace *const restoreWorkspace =
        staging.transaction;

    if (fx::archive::ValidateEffectTableRestoreLease(tableResult.lease)
        != fx::archive::EffectTableRestoreStatus::Success)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "FX lifecycle changed while preparing restore staging");
    }
    if (!FX_DestroyArchiveDisk32ReaderWorkspace(staging.reader))
    {
        Sys_Error("Unable to destroy FX Disk32 reader workspace safely");
        std::abort();
    }
    staging.reader = nullptr;
    if (fx::archive::ValidateEffectTableRestoreLease(tableResult.lease)
        != fx::archive::EffectTableRestoreStatus::Success)
    {
        FX_ReportArchiveDisk32RestoreFailure(
            &tableResult.lease,
            &staging,
            "FX lifecycle changed before archive admission");
    }

    const fx::archive::EffectTableRestoreStatus tableReleaseStatus =
        fx::archive::ReleaseEffectTableRestore(tableResult.lease);
    if (tableReleaseStatus
        != fx::archive::EffectTableRestoreStatus::Success)
    {
        const bool stagingDestroyed =
            FX_DestroyArchiveDisk32RestoreStaging(&staging);
        if (!stagingDestroyed
            || tableReleaseStatus
                != fx::archive::EffectTableRestoreStatus::LifecycleChanged)
        {
            Sys_Error(
                "Unable to close FX restore staging safely "
                "(effect-table release %u)",
                static_cast<unsigned>(tableReleaseStatus));
            std::abort();
        }
        Com_Error(
            ERR_DROP,
            "FX lifecycle changed while restoring effect definitions");
        return;
    }

    if (!FX_BeginArchive(system, restoreGeneration))
    {
        if (!FX_DestroyArchiveDisk32RestoreStaging(&staging))
        {
            Sys_Error(
                "Unable to destroy rejected FX restore staging safely");
            std::abort();
        }
        Com_Error(ERR_DROP, "FX archive restore could not acquire exclusive ownership");
        return;
    }

    // Nothing below this point performs archive I/O or reports ERR_DROP. Every
    // old body is captured as a validated reconstruction recipe before any
    // selected body is detached and destroyed to make replacement capacity.
    FxSystem &rollbackSystem = restoreWorkspace->rollbackSystem;
    FxVisibilityBufferSelectors originalVisibilitySelectors{};
    bool rollbackSnapshotExclusive = false;
    Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    rollbackSnapshotExclusive =
        FX_ValidateArchiveExclusiveState(system)
        && FX_TryDeriveVisibilitySelectorPair(
            &systemBuffers->visState[0],
            &systemBuffers->visState[1],
            system->visStateBufferRead,
            system->visStateBufferWrite,
            &originalVisibilitySelectors);
    if (rollbackSnapshotExclusive)
    {
        std::memcpy(&rollbackSystem, system, sizeof(rollbackSystem));
        std::memcpy(
            rollbackBuffers,
            systemBuffers,
            sizeof(*rollbackBuffers));
        FX_LinkSystemBuffers(&rollbackSystem, rollbackBuffers);

        rollbackSnapshotExclusive =
            FX_TryResolveVisibilitySelectors(
                &rollbackBuffers->visState[0],
                &rollbackBuffers->visState[1],
                originalVisibilitySelectors,
                &rollbackSystem.visStateBufferRead,
                &rollbackSystem.visStateBufferWrite);
        if (rollbackSnapshotExclusive)
        {
            rollbackSnapshotExclusive =
                FX_ArchiveVisibilitySelectorsMatch(
                    &rollbackSystem,
                    rollbackBuffers,
                    originalVisibilitySelectors)
                && Sys_AtomicLoad(&rollbackSystem.iteratorCount) == -1
                && FX_ValidateArchiveExclusiveState(system);
        }
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    // The archive owner keeps iterator exclusivity throughout the transaction.
    // Both staged graph images retain -1 and bind visibility selectors only
    // to their own buffers, so publication never exposes staged pointers or
    // creates a window that must reacquire the iterator after the copy.

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
    restoreContext.desiredSystem = restoredSystem;
    restoreContext.desiredBuffers = restoredBuffers;
    restoreContext.desiredVisibilitySelectors =
        desiredVisibilitySelectors;
    restoreContext.originalSystem = &rollbackSystem;
    restoreContext.originalBuffers = rollbackBuffers;
    restoreContext.originalVisibilitySelectors =
        originalVisibilitySelectors;
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
    const bool stagingDestroyed =
        FX_DestroyArchiveDisk32RestoreStaging(&staging);
    if (restoreOutcome
            != fx::archive::RestoreControlOutcome::DesiredPublished
        || !releasedArchive
        || !stagingDestroyed)
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

    FxEffectTableSaveStaging effectTableStaging{};
    const FxEffectTableSaveOutcome effectTableStageOutcome =
        FX_StageEffectTableNoDrop(&effectTableStaging);
    if (effectTableStageOutcome != FxEffectTableSaveOutcome::Success)
    {
        if (effectTableStageOutcome
            == FxEffectTableSaveOutcome::AllocationFailed)
        {
            Com_Error(
                ERR_DROP,
                "Unable to allocate FX effect-definition save snapshot");
        }
        else
        {
            Com_Error(
                ERR_DROP,
                "Invalid FX effect-definition table while saving archive");
        }
        return;
    }

    FxArchiveSaveSnapshotWorkspace *const snapshotWorkspace =
        FX_AllocateArchiveSaveSnapshotWorkspace();
    if (!snapshotWorkspace)
    {
        FX_DestroyEffectTableSaveStaging(&effectTableStaging);
        Com_Error(ERR_DROP, "Unable to allocate FX save snapshot workspace");
        return;
    }
    FxSystem &systemSnapshot = snapshotWorkspace->serializedSystem;
    FxSystemBuffers *const bufferSnapshot = &snapshotWorkspace->buffers;
    FxArchivePhysicsOwnershipScratch *const physicsOwnershipScratch =
        &snapshotWorkspace->physics;

    std::size_t physicsEntryByteCount = 0;
    int physicsEntryAllocationSize = 0;
    if (!FX_TryGetArchivePhysicsEntryByteCount(
            fx::physics::BODY_LIMIT,
            &physicsEntryByteCount,
            &physicsEntryAllocationSize))
    {
        FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);
        FX_DestroyEffectTableSaveStaging(&effectTableStaging);
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
        FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);
        FX_DestroyEffectTableSaveStaging(&effectTableStaging);
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
        FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);
        FX_DestroyEffectTableSaveStaging(&effectTableStaging);
        Com_Error(ERR_DROP, "FX archive save could not acquire exclusive ownership");
        return;
    }
    system->isArchiving = 1;

    std::size_t physicsEntryCount = 0;
    std::int16_t snapshotSpotLightBoltDobj = -1;
    bool snapshotCapturedExclusive = false;
    Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    snapshotCapturedExclusive = FX_ValidateArchiveExclusiveState(system);
    if (snapshotCapturedExclusive)
    {
        memcpy(&systemSnapshot, system, sizeof(systemSnapshot));
        memcpy(bufferSnapshot, systemBuffers, sizeof(*bufferSnapshot));
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    const bool copiedSnapshotValid = snapshotCapturedExclusive
        && FX_ValidateArchiveExclusiveState(system)
        && FX_ValidateArchiveCopiedSnapshot(
            &systemSnapshot,
            bufferSnapshot,
            systemBuffers,
            effectTableStaging.snapshot,
            snapshotWorkspace,
            physicsEntries,
            fx::physics::BODY_LIMIT,
            &physicsEntryCount,
            &snapshotSpotLightBoltDobj)
        && FX_CaptureArchivePhysicsStates(
            system,
            systemSnapshot.msecNow,
            physicsEntries,
            physicsEntryCount,
            physicsOwnershipScratch);
    if (!copiedSnapshotValid)
    {
        system->isArchiving = 0;
        const bool releasedArchive = FX_EndArchive(system);
        Z_Free(physicsEntries, 10);
        FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);
        FX_DestroyEffectTableSaveStaging(&effectTableStaging);
        Com_Error(
            ERR_DROP,
            releasedArchive
                ? "Invalid copied FX state while saving archive"
                : "Unable to release invalid copied FX archive snapshot");
        return;
    }
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
        FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);
        FX_DestroyEffectTableSaveStaging(&effectTableStaging);
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
        FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);
        FX_DestroyEffectTableSaveStaging(&effectTableStaging);
        Com_Error(ERR_DROP, "Unable to release FX archive snapshot");
        return;
    }

    // The live graph and iterator gate are released before any potentially
    // failing archive write. Overflow reporting is deferred until all heap
    // staging has been freed.
    const FxEffectTableSaveOutcome effectTableWriteOutcome =
        FX_WriteStagedEffectTableNoDrop(&effectTableStaging, memFile);
    bool archiveWritten =
        effectTableWriteOutcome == FxEffectTableSaveOutcome::Success;
    if (archiveWritten)
    {
        archiveWritten = FX_WriteArchiveDataNoDrop(
            memFile, FX_ARCHIVE_SYSTEM_SIZE, &systemSnapshot);
    }
    if (archiveWritten)
    {
        archiveWritten = FX_WriteArchiveDataNoDrop(
            memFile, FX_ARCHIVE_BUFFER_SIZE, bufferSnapshot);
    }
    const std::uint32_t archivedSystemAddress =
        static_cast<std::uint32_t>(systemAddress);
    if (archiveWritten)
    {
        archiveWritten = FX_WriteArchiveDataNoDrop(
            memFile,
            static_cast<int>(sizeof(archivedSystemAddress)),
            &archivedSystemAddress);
    }
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
    FX_DestroyArchiveSaveSnapshotWorkspace(snapshotWorkspace);
    FX_DestroyEffectTableSaveStaging(&effectTableStaging);
    if (effectTableWriteOutcome
        != FxEffectTableSaveOutcome::Success)
    {
        Com_Error(
            ERR_DROP,
            effectTableWriteOutcome == FxEffectTableSaveOutcome::WriteFailed
                ? "FX effect-definition archive ran out of memory"
                : "Invalid staged FX effect-definition table while saving archive");
        return;
    }
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

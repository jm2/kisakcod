#include "fx_system.h"
#include "fx_archive_gate_control.h"
#include "fx_iterator_atomic.h"
#include "fx_physics_sidecar.h"
#include "fx_pool.h"
#include "fx_pool_graph.h"
#include "fx_visibility_atomic.h"

#include <qcommon/mem_track.h>
#include <qcommon/sys_sync.h>
#include <qcommon/threads.h>

#include <physics/phys_local.h>

#include <gfx_d3d/rb_light.h>

#include <universal/com_sndalias.h>

#ifdef KISAK_MP
#include <cgame_mp/cg_local_mp.h>
#include <client_mp/client_mp.h>
#elif KISAK_SP
#include <cgame/cg_main.h>
#endif

#include <gfx_d3d/r_model.h>

#include <universal/profile.h>
#include <universal/sys_atomic.h>

#include <array>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <thread>

int32_t fx_maxLocalClients;
int32_t fx_serverVisClient;

FxSystem fx_systemPool[1];
FxSystemBuffers fx_systemBufferPool[1];
FxMarksSystem fx_marksSystemPool[1];
alignas(4) volatile std::int32_t fx_archiveGate[1]{};
alignas(4) volatile std::int32_t fx_cooperativeIteratorGeneration[1]{};
alignas(4) volatile std::int32_t fx_effectKillGate[1]{};
alignas(4) volatile std::int32_t
    fx_effectOwnerAdmissionBlocked[1][FX_EFFECT_LIMIT]{};

namespace
{
struct FxPoolAllocationStates
{
    FxPoolAllocationState<MAX_ELEMS> elems;
    FxPoolAllocationState<MAX_TRAILS> trails;
    FxPoolAllocationState<MAX_TRAIL_ELEMS> trailElems;
};

FxPoolAllocationStates fx_poolAllocationStates[1];
fx::physics::BodySidecar fx_physicsBodySidecars[
    std::size(fx_poolAllocationStates)];
// Every user holds CRITSECT_PHYSICS, so one bounded BSS workspace can validate
// live sidecar structure without adding 4/8 KiB to legacy call stacks.
fx::physics::BodySidecarValidationScratch
    fx_liveSidecarValidationScratch;

static_assert(
    std::size(fx_physicsBodySidecars) == std::size(fx_systemPool),
    "FX physics sidecars must match the system pool");

thread_local fx::archive::ArchiveGateOwnerState fx_archiveThreadState;

struct FxEffectLockThreadEntry
{
    FxSystem *system = nullptr;
    FxEffect *effect = nullptr;
    std::uint32_t generation = 0;
};

thread_local std::array<FxEffectLockThreadEntry, FX_EFFECT_LIMIT>
    fx_effectLockThreadEntries{};
thread_local std::size_t fx_effectLockThreadEntryCount = 0;

struct FxEffectReservationThreadState
{
    FxSystem *system = nullptr;
    FxEffect *effect = nullptr;
    FxEffect *ownerEffect = nullptr;
    std::int32_t allocIndex = 0;
    std::uint32_t generation = 0;
    bool ownerRelationshipAdded = false;
    bool spotLightRelationshipAdded = false;
};

thread_local FxEffectReservationThreadState
    fx_effectReservationThreadState{};

struct FxEffectKillThreadState
{
    FxSystem *system = nullptr;
    FxEffect *effect = nullptr;
    volatile std::int32_t *killGate = nullptr;
    std::array<volatile std::int32_t *, FX_EFFECT_LIMIT> admissionStates{};
    std::size_t admissionStateCount = 0;
    std::array<FxEffect *, FX_EFFECT_LIMIT> retainedEffects{};
    std::size_t retainedEffectCount = 0;
    std::uint32_t generation = 0;
    bool mutationStarted = false;
};

thread_local FxEffectKillThreadState fx_effectKillThreadState{};

struct FxEffectKillExclusiveThreadState
{
    FxSystem *system = nullptr;
    std::uint32_t generation = 0;
    bool exclusiveAcquired = false;
};

thread_local FxEffectKillExclusiveThreadState
    fx_effectKillExclusiveThreadState{};

struct FxEffectRestartRetainThreadState
{
    FxSystem *system = nullptr;
    std::array<FxEffect *, FX_EFFECT_LIMIT> effects{};
    std::size_t effectCount = 0;
    std::uint32_t generation = 0;
    bool ownsKillGate = false;
};

thread_local FxEffectRestartRetainThreadState
    fx_effectRestartRetainThreadState{};

enum class FxModelPhysicsSpawnOutcome : std::uint8_t
{
    Success,
    ResourceUnavailable,
    InvalidState,
    OwnershipRejected,
};

struct FxModelPhysicsSpawnResult
{
    FxModelPhysicsSpawnOutcome outcome =
        FxModelPhysicsSpawnOutcome::InvalidState;
    PhysBodyModelCreateStatus physicsStatus =
        PhysBodyModelCreateStatus::InvalidArgument;
    PhysBodyCreateResourceFailure resourceFailure =
        PhysBodyCreateResourceFailure::None;
    fx::physics::SidecarStatus sidecarStatus =
        fx::physics::SidecarStatus::InvalidArgument;
};

FxModelPhysicsSpawnResult FX_TrySpawnModelPhysics(
    FxSystem *system,
    FxEffect *effect,
    const FxElemDef *elemDef,
    int32_t randomSeed,
    FxElem *elem) noexcept;

volatile std::int32_t *FX_GetArchiveGate(
    const FxSystem *const system) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++index)
    {
        if (system == &fx_systemPool[index])
            return &fx_archiveGate[index];
    }
    return nullptr;
}

struct FxArchiveGateControlContext
{
    FxSystem *system = nullptr;
    volatile std::int32_t *gate = nullptr;
    std::uint32_t expectedGeneration = 0;
};

fx::archive::ArchiveGateControlStatus
FX_PerformArchiveGateControlOperation(
    void *const rawContext,
    const fx::archive::ArchiveGateControlOperation operation) noexcept
{
    using Operation = fx::archive::ArchiveGateControlOperation;
    using Status = fx::archive::ArchiveGateControlStatus;
    using Value = fx::archive::ArchiveGateValue;

    if (!rawContext)
        return Status::UnsafeFailure;
    auto &context = *static_cast<FxArchiveGateControlContext *>(
        rawContext);
    if (!context.system || !context.gate)
        return Status::UnsafeFailure;

    constexpr std::int32_t open =
        static_cast<std::int32_t>(Value::Open);
    constexpr std::int32_t pending =
        static_cast<std::int32_t>(Value::Pending);
    constexpr std::int32_t exclusive =
        static_cast<std::int32_t>(Value::Exclusive);

    switch (operation)
    {
    case Operation::ClaimPending:
        return Sys_AtomicCompareExchange(
                   context.gate, pending, open)
                == open
            ? Status::Success
            : Status::Rejected;
    case Operation::TryAcquireIterator:
        return FxIteratorTryBeginExclusive(
                   &context.system->iteratorCount)
            ? Status::Success
            : Status::Retry;
    case Operation::WaitForIteratorProgress:
        if (!context.system->isInitialized
            || context.system->isArchiving
            || FX_GetCooperativeIteratorGeneration(context.system)
                != context.expectedGeneration)
        {
            return Status::Cancelled;
        }
        std::this_thread::yield();
        return Status::Retry;
    case Operation::PromoteExclusive:
        return Sys_AtomicCompareExchange(
                   context.gate, exclusive, pending)
                == pending
            ? Status::Success
            : Status::UnsafeFailure;
    case Operation::ValidateAdmission:
        if (Sys_AtomicLoad(context.gate) != exclusive
            || Sys_AtomicLoad(&context.system->iteratorCount) != -1)
        {
            return Status::UnsafeFailure;
        }
        return context.system->isInitialized
                && !context.system->isArchiving
                && FX_GetCooperativeIteratorGeneration(context.system)
                    == context.expectedGeneration
            ? Status::Success
            : Status::Cancelled;
    case Operation::ValidateExclusive:
        return Sys_AtomicLoad(context.gate) == exclusive
                && Sys_AtomicLoad(&context.system->iteratorCount) == -1
            ? Status::Success
            : Status::UnsafeFailure;
    case Operation::ReleaseIterator:
        return FxIteratorEndExclusive(
                   &context.system->iteratorCount)
            ? Status::Success
            : Status::UnsafeFailure;
    case Operation::ClearArchivingForError:
        context.system->isArchiving = false;
        return Status::Success;
    case Operation::ReopenPending:
        return Sys_AtomicCompareExchange(
                   context.gate, open, pending)
                == pending
            ? Status::Success
            : Status::UnsafeFailure;
    case Operation::ReopenExclusive:
        return Sys_AtomicCompareExchange(
                   context.gate, open, exclusive)
                == exclusive
            ? Status::Success
            : Status::UnsafeFailure;
    default:
        return Status::UnsafeFailure;
    }
}

volatile std::int32_t *FX_GetCooperativeIteratorGenerationState(
    const FxSystem *const system) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++index)
    {
        if (system == &fx_systemPool[index])
            return &fx_cooperativeIteratorGeneration[index];
    }
    return nullptr;
}

volatile std::int32_t *FX_GetEffectKillGate(
    const FxSystem *const system) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++index)
    {
        if (system == &fx_systemPool[index])
            return &fx_effectKillGate[index];
    }
    return nullptr;
}

bool FX_CurrentThreadOwnsArchive(const FxSystem *const system) noexcept
{
    if (!system || !fx_archiveThreadState.identity)
        return false;
    const auto *const ownerSystem = static_cast<const FxSystem *>(
        fx_archiveThreadState.identity);
    return fx::archive::ArchiveGateOwnerMatches(
        &fx_archiveThreadState,
        system,
        FX_GetCooperativeIteratorGeneration(ownerSystem));
}

bool FX_CurrentThreadOwnsEffectKillExclusive(
    const FxSystem *const system) noexcept
{
    if (!fx_effectKillExclusiveThreadState.system)
        return false;
    if (fx_effectKillExclusiveThreadState.generation
        != FX_GetCooperativeIteratorGeneration(
            fx_effectKillExclusiveThreadState.system))
    {
        fx_effectKillExclusiveThreadState = {};
        return false;
    }
    return fx_effectKillExclusiveThreadState.system == system;
}

bool FX_CurrentThreadOwnsEffectRestartGate(
    const FxSystem *const system) noexcept
{
    if (!fx_effectRestartRetainThreadState.system
        || !fx_effectRestartRetainThreadState.ownsKillGate)
    {
        return false;
    }
    if (fx_effectRestartRetainThreadState.generation
        != FX_GetCooperativeIteratorGeneration(
            fx_effectRestartRetainThreadState.system))
    {
        fx_effectRestartRetainThreadState = {};
        return false;
    }
    return fx_effectRestartRetainThreadState.system == system;
}

void FX_EnterArchiveAwarePoolCriticalSection()
{
    for (;;)
    {
        while (fx::archive::ArchiveGateBlocksAllocatorAdmission(
            static_cast<fx::archive::ArchiveGateValue>(
                Sys_AtomicLoad(&fx_archiveGate[0]))))
        {
            std::this_thread::yield();
        }
        Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
        if (!fx::archive::ArchiveGateBlocksAllocatorAdmission(
                static_cast<fx::archive::ArchiveGateValue>(
                    Sys_AtomicLoad(&fx_archiveGate[0]))))
        {
            return;
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    }
}

FxPoolAllocationStates *FX_GetPoolAllocationStates(
    const FxSystem *const system) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++index)
    {
        if (system == &fx_systemPool[index])
            return &fx_poolAllocationStates[index];
    }
    return nullptr;
}

const FxSystemBuffers *FX_GetOwnedSystemBuffers(
    const FxSystem *const system) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++index)
    {
        if (system == &fx_systemPool[index])
            return &fx_systemBufferPool[index];
    }
    return nullptr;
}

volatile std::int32_t *FX_GetEffectOwnerAdmissionState(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    if (!system || !effect)
        return nullptr;
    constexpr std::size_t effectHandleStride = FxHandleStride<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>();
    for (std::size_t systemIndex = 0;
         systemIndex < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++systemIndex)
    {
        if (system != &fx_systemPool[systemIndex])
            continue;
        const std::uint16_t effectHandle = FxEncodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects, effect);
        if (effectHandle == FX_INVALID_HANDLE)
            return nullptr;
        return &fx_effectOwnerAdmissionBlocked[systemIndex]
            [effectHandle / effectHandleStride];
    }
    return nullptr;
}

bool FX_ResetEffectOwnerAdmissionForReservation(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    volatile std::int32_t *const state =
        FX_GetEffectOwnerAdmissionState(system, effect);
    if (!state)
        return false;
    FX_EnterArchiveAwarePoolCriticalSection();
    Sys_AtomicStore(state, 0);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return true;
}

void FX_ReleaseAllEffectKillRetains() noexcept;
bool FX_TryAdjustEffectReferenceCount(
    FxEffect *effect,
    bool increment) noexcept;
bool FX_RecordEffectKillRetain(
    FxSystem *system,
    FxEffect *effect) noexcept;

bool FX_BeginEffectKillAdmission(
    FxSystem *const system,
    FxEffect *const effect,
    std::int32_t *const outPublicationBarrier) noexcept
{
    if (!system || !effect || !outPublicationBarrier
        || fx_effectKillThreadState.system
        || !FX_CurrentThreadOwnsEffectKillExclusive(system))
    {
        return false;
    }
    volatile std::int32_t *const admissionState =
        FX_GetEffectOwnerAdmissionState(system, effect);
    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (!admissionState || !killGate)
        return false;

    FX_EnterArchiveAwarePoolCriticalSection();
    if (Sys_AtomicLoad(killGate) != 2
        || Sys_AtomicLoad(admissionState) != 0)
    {
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return false;
    }
    const std::int32_t publicationBarrier =
        Sys_AtomicLoad(&system->firstFreeEffect);
    if (publicationBarrier < 0)
    {
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return false;
    }
    fx_effectKillThreadState.system = system;
    fx_effectKillThreadState.effect = effect;
    fx_effectKillThreadState.killGate = killGate;
    fx_effectKillThreadState.admissionStates[0] = admissionState;
    fx_effectKillThreadState.admissionStateCount = 1;
    fx_effectKillThreadState.generation =
        FX_GetCooperativeIteratorGeneration(system);
    const bool targetReferenceAdded =
        FX_TryAdjustEffectReferenceCount(effect, true);
    const bool targetRetainRecorded = targetReferenceAdded
        && FX_RecordEffectKillRetain(system, effect);
    if (!targetRetainRecorded)
    {
        if (targetReferenceAdded)
            (void)FX_TryAdjustEffectReferenceCount(effect, false);
        fx_effectKillThreadState = {};
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return false;
    }
    Sys_AtomicStore(admissionState, 1);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    *outPublicationBarrier = publicationBarrier;
    return true;
}

void FX_EndEffectKillAdmission(const bool keepBlocked) noexcept
{
    if (fx_effectKillThreadState.retainedEffectCount != 0)
        FX_ReleaseAllEffectKillRetains();
    FxSystem *const system = fx_effectKillThreadState.system;
    volatile std::int32_t *const killGate =
        fx_effectKillThreadState.killGate;
    const std::uint32_t generation =
        fx_effectKillThreadState.generation;
    if (!system || !killGate
        || generation != FX_GetCooperativeIteratorGeneration(system))
    {
        fx_effectKillThreadState = {};
        return;
    }
    FX_EnterArchiveAwarePoolCriticalSection();
    if (!keepBlocked)
    {
        while (fx_effectKillThreadState.admissionStateCount != 0)
        {
            volatile std::int32_t *const admissionState =
                fx_effectKillThreadState.admissionStates[
                    --fx_effectKillThreadState.admissionStateCount];
            if (admissionState)
            {
                (void)Sys_AtomicCompareExchange(
                    admissionState, 0, 1);
            }
        }
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    fx_effectKillThreadState = {};
}

void FX_MarkEffectKillMutationStarted() noexcept
{
    if (fx_effectKillThreadState.system)
        fx_effectKillThreadState.mutationStarted = true;
}

bool FX_ClaimEffectOwnerAdmissionForKillLocked(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    if (!system || !effect
        || fx_effectKillThreadState.system != system
        || fx_effectKillThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system))
    {
        return false;
    }
    volatile std::int32_t *const admissionState =
        FX_GetEffectOwnerAdmissionState(system, effect);
    if (!admissionState)
        return false;
    for (std::size_t stateIndex = 0;
         stateIndex < fx_effectKillThreadState.admissionStateCount;
         ++stateIndex)
    {
        if (fx_effectKillThreadState.admissionStates[stateIndex]
            == admissionState)
        {
            return true;
        }
    }
    if (Sys_AtomicLoad(admissionState) != 0)
    {
        return (static_cast<std::uint32_t>(
                    Sys_AtomicLoad(&effect->status))
                & FX_STATUS_OWNER_ADMISSION_BLOCKED) != 0;
    }
    if (fx_effectKillThreadState.admissionStateCount
        == fx_effectKillThreadState.admissionStates.size())
    {
        return false;
    }
    Sys_AtomicStore(admissionState, 1);
    fx_effectKillThreadState.admissionStates[
        fx_effectKillThreadState.admissionStateCount++] = admissionState;
    return true;
}

void FX_MarkEffectOwnerAdmissionPermanentlyBlocked(
    FxEffect *const effect) noexcept
{
    if (!effect)
        return;
    std::int32_t observed = Sys_AtomicLoad(&effect->status);
    for (;;)
    {
        const std::uint32_t desiredStatus =
            static_cast<std::uint32_t>(observed)
            | static_cast<std::uint32_t>(
                FX_STATUS_OWNER_ADMISSION_BLOCKED);
        const std::int32_t previous = Sys_AtomicCompareExchange(
            &effect->status,
            static_cast<std::int32_t>(desiredStatus),
            observed);
        if (previous == observed)
            return;
        observed = previous;
    }
}

bool FX_RecordEffectKillRetain(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    if (!system || !effect
        || fx_effectKillThreadState.system != system
        || fx_effectKillThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system)
        || fx_effectKillThreadState.retainedEffectCount
            == fx_effectKillThreadState.retainedEffects.size())
    {
        return false;
    }
    fx_effectKillThreadState.retainedEffects[
        fx_effectKillThreadState.retainedEffectCount++] = effect;
    return true;
}

bool FX_ForgetEffectKillRetain(
    FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    if (!system || !effect
        || fx_effectKillThreadState.system != system
        || fx_effectKillThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system)
        || fx_effectKillThreadState.retainedEffectCount == 0
        || fx_effectKillThreadState.retainedEffects[
            fx_effectKillThreadState.retainedEffectCount - 1] != effect)
    {
        return false;
    }
    --fx_effectKillThreadState.retainedEffectCount;
    fx_effectKillThreadState.retainedEffects[
        fx_effectKillThreadState.retainedEffectCount] = nullptr;
    return true;
}

bool FX_ReleaseEffectKillRetain(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    if (!FX_ForgetEffectKillRetain(system, effect))
        return false;
    FX_DelRefToEffect(system, effect);
    return true;
}

void FX_ReleaseAllEffectKillRetains() noexcept
{
    FxSystem *const system = fx_effectKillThreadState.system;
    if (!system
        || fx_effectKillThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system))
    {
        fx_effectKillThreadState.retainedEffectCount = 0;
        fx_effectKillThreadState.retainedEffects.fill(nullptr);
        return;
    }
    while (fx_effectKillThreadState.retainedEffectCount != 0)
    {
        FxEffect *const effect = fx_effectKillThreadState.retainedEffects[
            --fx_effectKillThreadState.retainedEffectCount];
        fx_effectKillThreadState.retainedEffects[
            fx_effectKillThreadState.retainedEffectCount] = nullptr;
        if (effect)
            FX_DelRefToEffect(system, effect);
    }
}

template <typename ITEM_TYPE, std::size_t LIMIT>
bool FX_IsPoolItemAllocatedLocked(
    const FxPool<ITEM_TYPE> *const pool,
    const ITEM_TYPE *const item,
    const FxPoolAllocationState<LIMIT> *const state) noexcept
{
    std::int32_t itemIndex = -1;
    return state && state->initialized
        && FxPoolItemIndex<ITEM_TYPE, LIMIT>(pool, item, &itemIndex)
        && FxPoolAllocationStateIsAllocated(
            *state, static_cast<std::size_t>(itemIndex));
}
}

std::uint32_t __cdecl FX_GetCooperativeIteratorGeneration(
    const FxSystem *const system)
{
    volatile std::int32_t *const generation =
        FX_GetCooperativeIteratorGenerationState(system);
    return generation
        ? static_cast<std::uint32_t>(Sys_AtomicLoad(generation))
        : 0;
}

namespace
{
[[noreturn]] void FX_DropInvalidEffectLock(const char *const reason)
{
    Com_Error(ERR_DROP, "Invalid FX effect lock state: %s", reason);
    std::abort();
}

bool FX_CurrentThreadOwnsEffectLock(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    if (!system || !effect)
        return false;
    const std::uint32_t generation =
        FX_GetCooperativeIteratorGeneration(system);
    for (std::size_t index = 0;
         index < fx_effectLockThreadEntryCount;
         ++index)
    {
        const FxEffectLockThreadEntry &entry =
            fx_effectLockThreadEntries[index];
        if (entry.system == system && entry.effect == effect
            && entry.generation == generation)
        {
            return true;
        }
    }
    return false;
}

bool FX_CurrentThreadCanMutateEffect(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    return FX_CurrentThreadOwnsEffectLock(system, effect)
        && (FX_CurrentThreadOwnsCooperativeIterator(system)
            || FX_CurrentThreadOwnsEffectKillExclusive(system));
}

bool FX_PhysicsOwnerIsVacantLocked(
    const fx::physics::BodySidecar *const sidecar,
    const std::size_t ownerIndex,
    fx::physics::SidecarStatus *const outStatus) noexcept
{
    if (!outStatus)
        return false;
    *outStatus = fx::physics::ValidateWithScratch(
        sidecar, &fx_liveSidecarValidationScratch);
    if (*outStatus == fx::physics::SidecarStatus::Success)
    {
        *outStatus =
            fx::physics::ValidateVacantOwner(sidecar, ownerIndex);
    }
    return *outStatus == fx::physics::SidecarStatus::Success;
}

bool FX_CurrentThreadOwnsAnyEffectLock() noexcept
{
    bool ownsLiveLock = false;
    for (std::size_t index = 0;
         index < fx_effectLockThreadEntryCount;
         ++index)
    {
        const FxEffectLockThreadEntry &entry =
            fx_effectLockThreadEntries[index];
        if (entry.system && entry.effect
            && entry.generation
                == FX_GetCooperativeIteratorGeneration(entry.system))
        {
            ownsLiveLock = true;
            break;
        }
    }
    if (!ownsLiveLock && fx_effectLockThreadEntryCount != 0)
    {
        fx_effectLockThreadEntries.fill({});
        fx_effectLockThreadEntryCount = 0;
    }
    return ownsLiveLock;
}

bool FX_TryClearEffectLockBit(FxEffect *const effect) noexcept
{
    if (!effect)
        return false;
    std::int32_t observed = Sys_AtomicLoad(&effect->status);
    for (;;)
    {
        const std::uint32_t status =
            static_cast<std::uint32_t>(observed);
        if ((status & FX_STATUS_IS_LOCKED) == 0)
            return false;
        const std::uint32_t desiredStatus =
            status & ~static_cast<std::uint32_t>(FX_STATUS_IS_LOCKED);
        const std::int32_t previous = Sys_AtomicCompareExchange(
            &effect->status,
            static_cast<std::int32_t>(desiredStatus),
            observed);
        if (previous == observed)
            return true;
        observed = previous;
    }
}

bool FX_EffectNoLongerReferencedBounded(
    FxSystem *system,
    FxEffect *effect) noexcept;

void FX_BeginEffectReservation(
    FxSystem *const system,
    const std::int32_t allocIndex)
{
    if (!system || fx_effectReservationThreadState.system)
        FX_DropInvalidEffectLock("nested or invalid effect reservation");
    fx_effectReservationThreadState.system = system;
    fx_effectReservationThreadState.allocIndex = allocIndex;
    fx_effectReservationThreadState.generation =
        FX_GetCooperativeIteratorGeneration(system);
}

void FX_PreflightEffectReservation(FxSystem *const system)
{
    if (!system)
        FX_DropInvalidEffectLock("missing effect reservation system");
    if (!fx_effectReservationThreadState.system)
        return;
    if (fx_effectReservationThreadState.generation
        != FX_GetCooperativeIteratorGeneration(
            fx_effectReservationThreadState.system))
    {
        fx_effectReservationThreadState = {};
        return;
    }
    FX_DropInvalidEffectLock("nested effect reservation");
}

void FX_SetReservedEffect(FxEffect *const effect)
{
    if (!fx_effectReservationThreadState.system || !effect)
        FX_DropInvalidEffectLock("missing reserved effect");
    fx_effectReservationThreadState.effect = effect;
}

void FX_RecordReservedEffectOwner(FxEffect *const ownerEffect)
{
    if (!fx_effectReservationThreadState.system || !ownerEffect)
        FX_DropInvalidEffectLock("missing reserved effect owner");
    fx_effectReservationThreadState.ownerEffect = ownerEffect;
    fx_effectReservationThreadState.ownerRelationshipAdded = true;
}

void FX_RecordReservedEffectSpotLight()
{
    if (!fx_effectReservationThreadState.system
        || !fx_effectReservationThreadState.effect)
        FX_DropInvalidEffectLock("missing reserved spotlight effect");
    fx_effectReservationThreadState.spotLightRelationshipAdded = true;
}

bool FX_PublishEffectReservation(const bool tombstone) noexcept
{
    const FxEffectReservationThreadState reservation =
        fx_effectReservationThreadState;
    if (!reservation.system)
        return false;
    if (reservation.generation
        != FX_GetCooperativeIteratorGeneration(reservation.system))
    {
        fx_effectReservationThreadState = {};
        return false;
    }

    const std::int32_t observedFirstNewEffect =
        Sys_AtomicLoad(&reservation.system->firstNewEffect);
    if (observedFirstNewEffect > reservation.allocIndex)
    {
        // The slot is already visible. Never convert a published, potentially
        // worker-owned effect into a tombstone from longjmp cleanup.
        fx_effectReservationThreadState = {};
        return false;
    }

    bool rollbackValid = true;
    if (tombstone)
    {
        const std::uint16_t effectHandle =
            reservation.system->allEffectHandles[
                static_cast<std::size_t>(reservation.allocIndex)
                & (FX_EFFECT_LIMIT - 1)];
        FxEffect *const effect = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                reservation.system->effects, effectHandle);
        const bool effectSlotIsValid = effect
            && (!reservation.effect || reservation.effect == effect);
        if (!effectSlotIsValid)
            rollbackValid = false;

        FX_EnterArchiveAwarePoolCriticalSection();
        if (reservation.ownerRelationshipAdded)
        {
            if (effectSlotIsValid)
            {
                FxEffect *const recordedOwner = FxDecodeHandle<
                    FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                        reservation.system->effects, effect->owner);
                const std::uint32_t heldLockBit =
                    static_cast<std::uint32_t>(
                        Sys_AtomicLoad(&effect->status))
                    & static_cast<std::uint32_t>(FX_STATUS_IS_LOCKED);
                // Model cancellation as the reserved child's final reference
                // release. The bounded release plan removes the owner's pair
                // and cascades through any owner that consequently retires.
                if (recordedOwner != reservation.ownerEffect)
                    rollbackValid = false;
                if (recordedOwner)
                {
                    Sys_AtomicStore(
                        &effect->status,
                        static_cast<std::int32_t>(heldLockBit | 1u));
                    if (!FX_EffectNoLongerReferencedBounded(
                            reservation.system, effect))
                    {
                        rollbackValid = false;
                    }
                }
                else
                {
                    rollbackValid = false;
                }
            }
            else
            {
                rollbackValid = false;
            }
        }
        if (reservation.spotLightRelationshipAdded)
        {
            if (Sys_AtomicLoad(
                    &reservation.system->activeSpotLightEffectCount) == 1
                && Sys_AtomicLoad(
                    &reservation.system->activeSpotLightElemCount) == 0
                && reservation.system->activeSpotLightEffectHandle
                    == effectHandle)
            {
                reservation.system->activeSpotLightEffectHandle =
                    FX_INVALID_HANDLE;
                reservation.system->activeSpotLightElemHandle =
                    FX_INVALID_HANDLE;
                reservation.system->activeSpotLightBoltDobj = -1;
                Sys_AtomicStore(
                    &reservation.system->activeSpotLightEffectCount, 0);
            }
            else
            {
                rollbackValid = false;
            }
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);

        // Ordinary elements are only created after publication. Empty trails
        // may already exist and are deliberately retained for the normal GC
        // trail cleanup path. Give the zero-reference tombstone a valid root
        // owner so full-graph validation can safely admit it.
        if (effectSlotIsValid)
        {
            const std::uint32_t heldLockBit =
                static_cast<std::uint32_t>(
                    Sys_AtomicLoad(&effect->status))
                & static_cast<std::uint32_t>(FX_STATUS_IS_LOCKED);
            effect->owner = effectHandle;
            effect->firstElemHandle[0] = FX_INVALID_HANDLE;
            effect->firstElemHandle[1] = FX_INVALID_HANDLE;
            effect->firstElemHandle[2] = FX_INVALID_HANDLE;
            effect->firstSortedElemHandle = FX_INVALID_HANDLE;
            Sys_AtomicStore(
                &effect->status,
                static_cast<std::int32_t>(
                    static_cast<std::uint32_t>(FX_STATUS_SELF_OWNED)
                    | heldLockBit));
        }
    }

    for (;;)
    {
        const std::int32_t firstNewEffect =
            Sys_AtomicLoad(&reservation.system->firstNewEffect);
        if (firstNewEffect == reservation.allocIndex)
        {
            if (Sys_AtomicCompareExchange(
                    &reservation.system->firstNewEffect,
                    reservation.allocIndex + 1,
                    reservation.allocIndex)
                == reservation.allocIndex)
            {
                if (tombstone)
                {
                    FxRequestGarbageCollection(
                        &reservation.system->needsGarbageCollection);
                }
                // Publication is the transaction boundary. Clearing the TLS
                // frame here permits synchronous runner effects during
                // FX_StartNewEffect; any later ERR_DROP leaves this live,
                // internally consistent effect for ordinary system cleanup.
                fx_effectReservationThreadState = {};
                return !tombstone || rollbackValid;
            }
            continue;
        }
        if (firstNewEffect > reservation.allocIndex)
        {
            fx_effectReservationThreadState = {};
            return false;
        }
        std::this_thread::yield();
    }
}
}

bool __cdecl FX_ThreadOwnsEffectLock(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    return FX_CurrentThreadOwnsEffectLock(system, effect);
}

bool __cdecl FX_LockEffect(FxSystem *const system, FxEffect *const effect)
{
    if (!system || !effect)
        FX_DropInvalidEffectLock("missing system or effect");
    if (fx_effectLockThreadEntryCount
        == fx_effectLockThreadEntries.size())
    {
        FX_DropInvalidEffectLock("thread lock stack exceeds capacity");
    }
    for (std::size_t index = 0;
         index < fx_effectLockThreadEntryCount;
         ++index)
    {
        if (fx_effectLockThreadEntries[index].system == system
            && fx_effectLockThreadEntries[index].effect == effect)
        {
            FX_DropInvalidEffectLock("recursive lock acquisition");
        }
    }

    std::int32_t observed = Sys_AtomicLoad(&effect->status);
    for (;;)
    {
        const std::uint32_t status =
            static_cast<std::uint32_t>(observed);
        if ((status & 0xC0000000u) != 0)
            FX_DropInvalidEffectLock("reserved lock bits are set");
        // A cooperative iterator can observe an effect immediately before a
        // different worker releases its final reference. Treat that ordinary
        // lifetime race as stale work, not as corrupt engine state.
        if ((status & FX_STATUS_REF_COUNT_MASK) == 0)
            return false;
        if ((status & FX_STATUS_IS_LOCKED) != 0)
        {
            std::this_thread::yield();
            observed = Sys_AtomicLoad(&effect->status);
            continue;
        }
        const std::uint32_t desiredStatus =
            status | static_cast<std::uint32_t>(FX_STATUS_IS_LOCKED);
        const std::int32_t previous = Sys_AtomicCompareExchange(
            &effect->status,
            static_cast<std::int32_t>(desiredStatus),
            observed);
        if (previous == observed)
            break;
        observed = previous;
    }

    fx_effectLockThreadEntries[fx_effectLockThreadEntryCount++] = {
        system,
        effect,
        FX_GetCooperativeIteratorGeneration(system)};
    return true;
}

void __cdecl FX_UnlockEffect(FxSystem *const system, FxEffect *const effect)
{
    if (fx_effectLockThreadEntryCount == 0)
        FX_DropInvalidEffectLock("unlock without thread ownership");
    const FxEffectLockThreadEntry entry =
        fx_effectLockThreadEntries[fx_effectLockThreadEntryCount - 1];
    if (entry.system != system || entry.effect != effect)
        FX_DropInvalidEffectLock("non-LIFO or cross-system unlock");
    --fx_effectLockThreadEntryCount;
    fx_effectLockThreadEntries[fx_effectLockThreadEntryCount] = {};
    if (entry.generation != FX_GetCooperativeIteratorGeneration(system))
        FX_DropInvalidEffectLock("stale reset generation");
    if (!FX_TryClearEffectLockBit(effect))
        FX_DropInvalidEffectLock("owned lock bit is clear");
}

bool __cdecl FX_IsEffectLifecycleBlocked(
    const FxEffect *const effect) noexcept
{
    return !effect
        || (static_cast<std::uint32_t>(
                Sys_AtomicLoad(&effect->status))
            & FX_STATUS_OWNER_ADMISSION_BLOCKED) != 0;
}

bool __cdecl FX_RearmEffectForRestart(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    if (!system || !effect
        || !FX_CurrentThreadOwnsEffectLock(system, effect)
        || (!FX_CurrentThreadOwnsCooperativeIterator(system)
            && !FX_CurrentThreadOwnsEffectKillExclusive(system))
        || fx_effectKillThreadState.system)
    {
        return false;
    }

    volatile std::int32_t *const admissionState =
        FX_GetEffectOwnerAdmissionState(system, effect);
    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (!admissionState || !killGate)
        return false;

    FX_EnterArchiveAwarePoolCriticalSection();
    FxPoolAllocationStates *const poolStates =
        FX_GetPoolAllocationStates(system);
    const std::uint16_t effectHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    const std::uint32_t status = static_cast<std::uint32_t>(
        Sys_AtomicLoad(&effect->status));
    const std::int32_t killGateState = Sys_AtomicLoad(killGate);
    const bool canRearm = effectHandle != FX_INVALID_HANDLE
        && effect->owner == effectHandle
        && (status & FX_STATUS_REF_COUNT_MASK) != 0
        && (status & FX_STATUS_IS_LOCKED) != 0
        && (status & FX_STATUS_SELF_OWNED) != 0
        && (status & FX_STATUS_HAS_PENDING_LOOP_ELEMS) == 0
        && (status & FX_STATUS_OWNER_ADMISSION_BLOCKED) != 0
        && effect->firstSortedElemHandle == FX_INVALID_HANDLE
        && effect->firstElemHandle[0] == FX_INVALID_HANDLE
        && effect->firstElemHandle[1] == FX_INVALID_HANDLE
        && effect->firstElemHandle[2] == FX_INVALID_HANDLE
        && Sys_AtomicLoad(admissionState) == 1
        && (killGateState == 0
            || (killGateState == 2
                && FX_CurrentThreadOwnsEffectRestartGate(system)));
    bool emptyTrailGraph = canRearm && poolStates;
    std::array<bool, MAX_TRAILS> visitedTrails{};
    std::uint16_t trailHandle = effect->firstTrailHandle;
    for (std::size_t trailCount = 0;
         emptyTrailGraph && trailHandle != FX_INVALID_HANDLE;
         ++trailCount)
    {
        if (trailCount == MAX_TRAILS)
        {
            emptyTrailGraph = false;
            break;
        }
        FxPool<FxTrail> *const trail = FxDecodeHandle<
            FxPool<FxTrail>, MAX_TRAILS, FxTrail::HANDLE_SCALE>(
                system->trails, trailHandle);
        if (!trail)
        {
            emptyTrailGraph = false;
            break;
        }
        const std::size_t trailIndex = static_cast<std::size_t>(
            trail - system->trails);
        if (visitedTrails[trailIndex]
            || !FX_IsPoolItemAllocatedLocked<FxTrail, MAX_TRAILS>(
                system->trails,
                &trail->item,
                &poolStates->trails)
            || trail->item.firstElemHandle != FX_INVALID_HANDLE
            || trail->item.lastElemHandle != FX_INVALID_HANDLE)
        {
            emptyTrailGraph = false;
            break;
        }
        visitedTrails[trailIndex] = true;
        trailHandle = trail->item.nextTrailHandle;
    }
    const std::int32_t spotLightEffectCount =
        Sys_AtomicLoad(&system->activeSpotLightEffectCount);
    const std::int32_t spotLightElemCount =
        Sys_AtomicLoad(&system->activeSpotLightElemCount);
    const bool emptySpotLightGraph = spotLightEffectCount >= 0
        && spotLightEffectCount <= 1
        && spotLightElemCount >= 0
        && spotLightElemCount <= spotLightEffectCount
        && !(spotLightEffectCount == 1
            && system->activeSpotLightEffectHandle == effectHandle
            && spotLightElemCount != 0);
    if (canRearm && emptyTrailGraph && emptySpotLightGraph)
    {
        Sys_AtomicStore(
            &effect->status,
            static_cast<std::int32_t>(
                status
                & ~static_cast<std::uint32_t>(
                    FX_STATUS_OWNER_ADMISSION_BLOCKED)));
        Sys_AtomicStore(admissionState, 0);
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return canRearm && emptyTrailGraph && emptySpotLightGraph;
}

bool __cdecl FX_RetainEffectForRestart(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    if (fx_effectRestartRetainThreadState.system
        && fx_effectRestartRetainThreadState.generation
            != FX_GetCooperativeIteratorGeneration(
                fx_effectRestartRetainThreadState.system))
    {
        fx_effectRestartRetainThreadState = {};
    }
    if (!system || !effect
        || !FX_CurrentThreadOwnsEffectKillExclusive(system)
        || (fx_effectRestartRetainThreadState.system
            && (fx_effectRestartRetainThreadState.system != system
                || fx_effectRestartRetainThreadState.generation
                    != FX_GetCooperativeIteratorGeneration(system)))
        || fx_effectRestartRetainThreadState.effectCount
            == fx_effectRestartRetainThreadState.effects.size())
    {
        return false;
    }

    FX_EnterArchiveAwarePoolCriticalSection();
    const std::uint32_t status = static_cast<std::uint32_t>(
        Sys_AtomicLoad(&effect->status));
    const std::uint32_t referenceCount =
        status & FX_STATUS_REF_COUNT_MASK;
    const bool retained = referenceCount != 0
        && referenceCount < FX_STATUS_REF_COUNT_MASK - 1u
        && (status & FX_STATUS_OWNER_ADMISSION_BLOCKED) == 0
        && FX_TryAdjustEffectReferenceCount(effect, true);
    if (retained)
    {
        if (!fx_effectRestartRetainThreadState.system)
        {
            fx_effectRestartRetainThreadState.system = system;
            fx_effectRestartRetainThreadState.generation =
                FX_GetCooperativeIteratorGeneration(system);
        }
        fx_effectRestartRetainThreadState.effects[
            fx_effectRestartRetainThreadState.effectCount++] = effect;
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return retained;
}

bool __cdecl FX_ConsumeEffectRestartRetain(
    FxSystem *const system,
    FxEffect *const effect,
    const bool releaseReference) noexcept
{
    if (!system || !effect
        || fx_effectRestartRetainThreadState.system != system
        || fx_effectRestartRetainThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system)
        || fx_effectRestartRetainThreadState.effectCount == 0)
    {
        return false;
    }

    std::size_t retainedIndex =
        fx_effectRestartRetainThreadState.effectCount;
    for (std::size_t index = 0;
         index < fx_effectRestartRetainThreadState.effectCount;
         ++index)
    {
        if (fx_effectRestartRetainThreadState.effects[index] == effect)
        {
            retainedIndex = index;
            break;
        }
    }
    if (retainedIndex == fx_effectRestartRetainThreadState.effectCount)
        return false;
    for (std::size_t index = retainedIndex + 1;
         index < fx_effectRestartRetainThreadState.effectCount;
         ++index)
    {
        fx_effectRestartRetainThreadState.effects[index - 1] =
            fx_effectRestartRetainThreadState.effects[index];
    }
    --fx_effectRestartRetainThreadState.effectCount;
    fx_effectRestartRetainThreadState.effects[
        fx_effectRestartRetainThreadState.effectCount] = nullptr;
    if (fx_effectRestartRetainThreadState.effectCount == 0
        && !fx_effectRestartRetainThreadState.ownsKillGate)
        fx_effectRestartRetainThreadState = {};
    if (releaseReference)
        FX_DelRefToEffect(system, effect);
    return true;
}

void __cdecl FX_AbandonCurrentThreadEffectRestartRetainsForError() noexcept
{
    FxSystem *const system = fx_effectRestartRetainThreadState.system;
    if (!system
        || fx_effectRestartRetainThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system))
    {
        fx_effectRestartRetainThreadState = {};
        return;
    }
    while (fx_effectRestartRetainThreadState.effectCount != 0)
    {
        FxEffect *const effect =
            fx_effectRestartRetainThreadState.effects[
                --fx_effectRestartRetainThreadState.effectCount];
        fx_effectRestartRetainThreadState.effects[
            fx_effectRestartRetainThreadState.effectCount] = nullptr;
        if (effect)
            FX_DelRefToEffect(system, effect);
    }
    if (!fx_effectRestartRetainThreadState.ownsKillGate)
        fx_effectRestartRetainThreadState = {};
}

void __cdecl FX_AbandonCurrentThreadEffectRestartGateForError() noexcept
{
    FxSystem *const system = fx_effectRestartRetainThreadState.system;
    const bool generationMatches = system
        && fx_effectRestartRetainThreadState.generation
            == FX_GetCooperativeIteratorGeneration(system);
    const bool ownsKillGate =
        fx_effectRestartRetainThreadState.ownsKillGate;
    fx_effectRestartRetainThreadState = {};
    if (!generationMatches || !ownsKillGate)
        return;
    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (killGate)
        (void)Sys_AtomicCompareExchange(killGate, 0, 2);
}

void __cdecl FX_AbandonCurrentThreadEffectLocksForError() noexcept
{
    while (fx_effectLockThreadEntryCount != 0)
    {
        const FxEffectLockThreadEntry entry =
            fx_effectLockThreadEntries[--fx_effectLockThreadEntryCount];
        fx_effectLockThreadEntries[fx_effectLockThreadEntryCount] = {};
        if (entry.system && entry.effect
            && entry.generation
                == FX_GetCooperativeIteratorGeneration(entry.system))
        {
            (void)FX_TryClearEffectLockBit(entry.effect);
        }
    }
}

void __cdecl FX_AbandonCurrentThreadEffectReservationForError() noexcept
{
    (void)FX_PublishEffectReservation(true);
}

void __cdecl FX_AbandonCurrentThreadEffectKillRetainsForError() noexcept
{
    FX_ReleaseAllEffectKillRetains();
}

void __cdecl FX_AbandonCurrentThreadEffectKillForError() noexcept
{
    const bool mutationStarted =
        fx_effectKillThreadState.mutationStarted;
    FX_EndEffectKillAdmission(mutationStarted);
}

bool __cdecl FX_IsElemAllocated(
    FxSystem *const system,
    const FxElem *const elem)
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states || !system || !system->elems || !elem)
        return false;
    FX_EnterArchiveAwarePoolCriticalSection();
    const bool allocated = FX_IsPoolItemAllocatedLocked<FxElem, MAX_ELEMS>(
        system->elems, elem, &states->elems);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return allocated;
}

bool __cdecl FX_IsTrailAllocated(
    FxSystem *const system,
    const FxTrail *const trail)
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states || !system || !system->trails || !trail)
        return false;
    FX_EnterArchiveAwarePoolCriticalSection();
    const bool allocated =
        FX_IsPoolItemAllocatedLocked<FxTrail, MAX_TRAILS>(
            system->trails, trail, &states->trails);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return allocated;
}

bool __cdecl FX_IsTrailElemAllocated(
    FxSystem *const system,
    const FxTrailElem *const trailElem)
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states || !system || !system->trailElems || !trailElem)
        return false;
    FX_EnterArchiveAwarePoolCriticalSection();
    const bool allocated =
        FX_IsPoolItemAllocatedLocked<FxTrailElem, MAX_TRAIL_ELEMS>(
            system->trailElems, trailElem, &states->trailElems);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return allocated;
}

bool __cdecl FX_GetSpotLightStateSnapshot(
    const FxSystem *const system,
    FxSpotLightStateSnapshot *const snapshot)
{
    if (!system || !snapshot)
        return false;
    FX_EnterArchiveAwarePoolCriticalSection();
    snapshot->effectCount =
        Sys_AtomicLoad(&system->activeSpotLightEffectCount);
    snapshot->elemCount =
        Sys_AtomicLoad(&system->activeSpotLightElemCount);
    snapshot->effectHandle = system->activeSpotLightEffectHandle;
    snapshot->elemHandle = system->activeSpotLightElemHandle;
    snapshot->boltDobj = system->activeSpotLightBoltDobj;
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return true;
}

bool __cdecl FX_SetSpotLightBoltDobj(
    FxSystem *const system,
    const std::int16_t boltDobj)
{
    if (!system)
        return false;
    FX_EnterArchiveAwarePoolCriticalSection();
    system->activeSpotLightBoltDobj = boltDobj;
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return true;
}

namespace
{
const FxElemDef *FX_GetValidatedRuntimeElemDef(
    const FxEffect *const effect,
    const std::uint32_t elemDefIndex)
{
    if (!effect || !effect->def || !effect->def->elemDefs
        || effect->def->elemDefCountLooping < 0
        || effect->def->elemDefCountOneShot < 0
        || effect->def->elemDefCountEmission < 0)
    {
        Com_Error(ERR_DROP, "Invalid FX element definition state");
        return nullptr;
    }

    const std::int64_t elemDefCount =
        static_cast<std::int64_t>(effect->def->elemDefCountLooping)
        + effect->def->elemDefCountOneShot
        + effect->def->elemDefCountEmission;
    if (elemDefCount > 256 || elemDefIndex >= elemDefCount)
    {
        Com_Error(ERR_DROP, "Invalid FX element definition index %u", elemDefIndex);
        return nullptr;
    }
    return &effect->def->elemDefs[elemDefIndex];
}

bool FX_ValidateSpotLightEffectDef(
    const FxEffectDef *const def,
    bool *const outHasSpotLight) noexcept
{
    if (!def || !outHasSpotLight || def->elemDefCountLooping < 0
        || def->elemDefCountOneShot < 0
        || def->elemDefCountEmission < 0)
    {
        return false;
    }
    const std::int64_t elemDefCount =
        static_cast<std::int64_t>(def->elemDefCountLooping)
        + def->elemDefCountOneShot
        + def->elemDefCountEmission;
    if (elemDefCount > 256 || (elemDefCount != 0 && !def->elemDefs))
        return false;

    std::size_t spotLightCount = 0;
    for (std::int64_t elemDefIndex = 0;
         elemDefIndex < elemDefCount;
         ++elemDefIndex)
    {
        if (def->elemDefs[elemDefIndex].elemType == FX_ELEM_TYPE_SPOT_LIGHT
            && ++spotLightCount > 1)
        {
            return false;
        }
    }
    *outHasSpotLight = spotLightCount == 1;
    return true;
}
}

bool __cdecl FX_ArchiveGateIsActive(const FxSystem *const system)
{
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    return gate
        && fx::archive::ArchiveGateBlocksIteratorAdmission(
            static_cast<fx::archive::ArchiveGateValue>(
                Sys_AtomicLoad(gate)));
}

bool __cdecl FX_EffectKillGateIsActive(
    const FxSystem *const system) noexcept
{
    volatile std::int32_t *const gate = FX_GetEffectKillGate(system);
    return gate && Sys_AtomicLoad(gate) != 0;
}

void __cdecl FX_WaitForArchiveGate(const FxSystem *const system)
{
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    if (!gate)
        return;
    while (fx::archive::ArchiveGateBlocksIteratorAdmission(
        static_cast<fx::archive::ArchiveGateValue>(
            Sys_AtomicLoad(gate))))
    {
        std::this_thread::yield();
    }
}

void __cdecl FX_WaitForEffectKillGate(
    const FxSystem *const system) noexcept
{
    volatile std::int32_t *const gate = FX_GetEffectKillGate(system);
    if (!gate)
        return;
    while (Sys_AtomicLoad(gate) != 0)
        std::this_thread::yield();
}

bool __cdecl FX_BeginEffectKillExclusive(FxSystem *const system) noexcept
{
    if (!system)
        return false;
    const std::uint32_t admissionGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    if (fx_effectRestartRetainThreadState.system
        && fx_effectRestartRetainThreadState.generation
            != FX_GetCooperativeIteratorGeneration(
                fx_effectRestartRetainThreadState.system))
    {
        fx_effectRestartRetainThreadState = {};
    }
    if (fx_effectKillExclusiveThreadState.system
        && fx_effectKillExclusiveThreadState.generation
            != FX_GetCooperativeIteratorGeneration(
                fx_effectKillExclusiveThreadState.system))
    {
        fx_effectKillExclusiveThreadState = {};
    }
    if (fx_effectKillExclusiveThreadState.system
        || fx_effectRestartRetainThreadState.system
        || FX_CurrentThreadOwnsCooperativeIterator(system)
        || FX_CurrentThreadOwnsArchive(system)
        || FX_CurrentThreadOwnsSortExclusive(system)
        || FX_CurrentThreadOwnsAnyEffectLock())
    {
        return false;
    }

    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (!killGate)
        return false;
    while (Sys_AtomicCompareExchange(killGate, 1, 0) != 0)
        std::this_thread::yield();

    fx_effectKillExclusiveThreadState.system = system;
    fx_effectKillExclusiveThreadState.generation = admissionGeneration;
    for (;;)
    {
        FX_WaitForArchiveGate(system);
        FxIteratorWaitBeginExclusive(&system->iteratorCount);
        volatile std::int32_t *const archiveGate =
            FX_GetArchiveGate(system);
        const fx::archive::ArchiveGateValue archiveGateState =
            archiveGate
            ? static_cast<fx::archive::ArchiveGateValue>(
                Sys_AtomicLoad(archiveGate))
            : static_cast<fx::archive::ArchiveGateValue>(-1);
        const std::uint32_t currentGeneration =
            FX_GetCooperativeIteratorGeneration(system);
        if (fx::archive::ArchiveGateAllowsPrecheckedExclusiveCompletion(
                archiveGateState)
            && system->isInitialized
            && currentGeneration == admissionGeneration)
        {
            fx_effectKillExclusiveThreadState.exclusiveAcquired = true;
            if (Sys_AtomicCompareExchange(killGate, 2, 1) == 1)
                return true;
            (void)FxIteratorEndExclusive(&system->iteratorCount);
            fx_effectKillExclusiveThreadState = {};
            (void)Sys_AtomicCompareExchange(killGate, 0, 1);
            return false;
        }
        if (currentGeneration != admissionGeneration
            || !system->isInitialized)
        {
            (void)FxIteratorEndExclusive(&system->iteratorCount);
            fx_effectKillExclusiveThreadState = {};
            (void)Sys_AtomicCompareExchange(killGate, 0, 1);
            return false;
        }
        if (!FxIteratorEndExclusive(&system->iteratorCount))
        {
            fx_effectKillExclusiveThreadState = {};
            (void)Sys_AtomicCompareExchange(killGate, 0, 1);
            return false;
        }
    }
}

bool __cdecl FX_EndEffectKillExclusive(FxSystem *const system) noexcept
{
    if (!system
        || !FX_CurrentThreadOwnsEffectKillExclusive(system)
        || !fx_effectKillExclusiveThreadState.exclusiveAcquired)
    {
        return false;
    }
    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (!killGate || Sys_AtomicLoad(killGate) != 2)
        return false;
    const bool released =
        FxIteratorEndExclusive(&system->iteratorCount);
    if (!released)
        return false;
    fx_effectKillExclusiveThreadState.exclusiveAcquired = false;
    if (Sys_AtomicCompareExchange(killGate, 0, 2) != 2)
        return false;
    fx_effectKillExclusiveThreadState = {};
    return true;
}

bool __cdecl FX_ThreadOwnsEffectKillExclusive(
    const FxSystem *const system) noexcept
{
    return FX_CurrentThreadOwnsEffectKillExclusive(system);
}

bool __cdecl FX_CompleteEffectKillExclusiveDowngrade(
    FxSystem *const system) noexcept
{
    if (!system
        || !FX_CurrentThreadOwnsEffectKillExclusive(system)
        || !fx_effectKillExclusiveThreadState.exclusiveAcquired
        || Sys_AtomicLoad(&system->iteratorCount) != 1)
    {
        return false;
    }
    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (!killGate || Sys_AtomicLoad(killGate) != 2)
        return false;
    if (fx_effectRestartRetainThreadState.system
        && (fx_effectRestartRetainThreadState.system != system
            || fx_effectRestartRetainThreadState.generation
                != FX_GetCooperativeIteratorGeneration(system)))
    {
        return false;
    }
    if (!fx_effectRestartRetainThreadState.system)
    {
        fx_effectRestartRetainThreadState.system = system;
        fx_effectRestartRetainThreadState.generation =
            FX_GetCooperativeIteratorGeneration(system);
    }
    fx_effectRestartRetainThreadState.ownsKillGate = true;
    fx_effectKillExclusiveThreadState = {};
    return true;
}

bool __cdecl FX_EndEffectRestartGate(FxSystem *const system) noexcept
{
    if (!system || !FX_CurrentThreadOwnsEffectRestartGate(system)
        || fx_effectRestartRetainThreadState.effectCount != 0
        || !FX_CurrentThreadOwnsCooperativeIterator(system))
    {
        return false;
    }
    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (!killGate
        || Sys_AtomicCompareExchange(killGate, 0, 2) != 2)
    {
        return false;
    }
    fx_effectRestartRetainThreadState = {};
    return true;
}

void __cdecl FX_AbandonCurrentThreadEffectKillExclusiveForError() noexcept
{
    const FxEffectKillExclusiveThreadState state =
        fx_effectKillExclusiveThreadState;
    fx_effectKillExclusiveThreadState = {};
    if (state.system
        && state.generation
            == FX_GetCooperativeIteratorGeneration(state.system))
    {
        if (state.exclusiveAcquired)
            (void)FxIteratorEndExclusive(&state.system->iteratorCount);
        volatile std::int32_t *const killGate =
            FX_GetEffectKillGate(state.system);
        if (killGate)
        {
            if (Sys_AtomicCompareExchange(killGate, 0, 2) != 2)
                (void)Sys_AtomicCompareExchange(killGate, 0, 1);
        }
    }
}

bool __cdecl FX_BeginArchive(FxSystem *const system)
{
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    if (!system || !gate)
        return false;
    const std::uint32_t admissionGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    if (FX_CurrentThreadOwnsCooperativeIterator(system)
        || FX_ThreadOwnsEffectKillExclusive(system)
        || FX_CurrentThreadOwnsArchive(system)
        || FX_CurrentThreadOwnsSortExclusive(system)
        || FX_CurrentThreadOwnsAnyEffectLock()
        || fx_archiveThreadState.phase
            != fx::archive::ArchiveGateOwnerPhase::Idle)
    {
        return false;
    }

    FxArchiveGateControlContext context{
        system,
        gate,
        admissionGeneration,
    };
    const fx::archive::ArchiveGateControlCallbacks callbacks{
        &context,
        FX_PerformArchiveGateControlOperation,
    };
    return fx::archive::AcquireArchiveGate(
               &fx_archiveThreadState,
               system,
               admissionGeneration,
               callbacks)
        == fx::archive::ArchiveGateControlStatus::Success;
}

bool __cdecl FX_ValidateArchiveExclusiveState(
    const FxSystem *const system) noexcept
{
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    return system && gate
        && fx_archiveThreadState.phase
            == fx::archive::ArchiveGateOwnerPhase::Acquired
        && Sys_AtomicLoad(gate)
            == static_cast<std::int32_t>(
                fx::archive::ArchiveGateValue::Exclusive)
        && FX_CurrentThreadOwnsArchive(system)
        && Sys_AtomicLoad(&system->iteratorCount) == -1;
}

bool __cdecl FX_EndArchive(FxSystem *const system)
{
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    if (!system || !gate)
        return false;

    const std::uint32_t currentGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    FxArchiveGateControlContext context{
        system,
        gate,
        currentGeneration,
    };
    const fx::archive::ArchiveGateControlCallbacks callbacks{
        &context,
        FX_PerformArchiveGateControlOperation,
    };
    return fx::archive::ReleaseArchiveGate(
               &fx_archiveThreadState,
               system,
               currentGeneration,
               callbacks)
        == fx::archive::ArchiveGateControlStatus::Success;
}

void __cdecl FX_AbandonCurrentThreadArchiveForError() noexcept
{
    auto *const system = const_cast<FxSystem *>(
        static_cast<const FxSystem *>(fx_archiveThreadState.identity));
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    const std::uint32_t currentGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    FxArchiveGateControlContext context{
        system,
        gate,
        currentGeneration,
    };
    const fx::archive::ArchiveGateControlCallbacks callbacks{
        &context,
        FX_PerformArchiveGateControlOperation,
    };
    if (fx::archive::AbandonArchiveGateForError(
            &fx_archiveThreadState,
            currentGeneration,
            callbacks)
        != fx::archive::ArchiveGateControlStatus::Success)
    {
        Sys_Error("Unable to abandon FX archive ownership safely");
        std::abort();
    }
}

[[noreturn]] void KISAK_CDECL FX_InvalidPoolHandle(
    const void *const pool,
    const std::uint32_t handle)
{
    Com_Error(
        ERR_DROP,
        "Invalid FX pool handle %u for pool %p",
        handle,
        pool);
    std::abort();
}

void __cdecl TRACK_fx_system()
{
    track_static_alloc_internal(
        fx_systemPool, static_cast<int>(sizeof(fx_systemPool)), "fx_systemPool", 8);
    track_static_alloc_internal(
        fx_systemBufferPool, static_cast<int>(sizeof(fx_systemBufferPool)), "fx_systemBufferPool", 8);
    track_static_alloc_internal(
        fx_marksSystemPool, static_cast<int>(sizeof(fx_marksSystemPool)), "fx_marksSystemPool", 8);
    track_static_alloc_internal(
        fx_poolAllocationStates,
        static_cast<int>(sizeof(fx_poolAllocationStates)),
        "fx_poolAllocationStates",
        8);
    track_static_alloc_internal(
        fx_physicsBodySidecars,
        static_cast<int>(sizeof(fx_physicsBodySidecars)),
        "fx_physicsBodySidecars",
        8);
    track_static_alloc_internal(
        const_cast<std::int32_t *>(fx_archiveGate),
        static_cast<int>(sizeof(fx_archiveGate)),
        "fx_archiveGate",
        4);
    track_static_alloc_internal(
        const_cast<std::int32_t *>(fx_cooperativeIteratorGeneration),
        static_cast<int>(sizeof(fx_cooperativeIteratorGeneration)),
        "fx_cooperativeIteratorGeneration",
        4);
    track_static_alloc_internal(
        const_cast<std::int32_t *>(fx_effectKillGate),
        static_cast<int>(sizeof(fx_effectKillGate)),
        "fx_effectKillGate",
        4);
}

XModel *__cdecl FX_RegisterModel(const char *modelName)
{
    return R_RegisterModel(modelName);
}

FxSystem *__cdecl FX_GetSystem(int32_t clientIndex)
{
    if (clientIndex)
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            140,
            0,
            "%s\n\t(clientIndex) = %i",
            "(clientIndex == 0)",
            clientIndex);
    return fx_systemPool;
}

fx::physics::BodySidecar *FX_GetPhysicsBodySidecar(
    FxSystem *const system) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++index)
    {
        if (system == &fx_systemPool[index])
            return &fx_physicsBodySidecars[index];
    }
    return nullptr;
}

const fx::physics::BodySidecar *FX_GetPhysicsBodySidecar(
    const FxSystem *const system) noexcept
{
    for (std::size_t index = 0;
         index < sizeof(fx_systemPool) / sizeof(fx_systemPool[0]);
         ++index)
    {
        if (system == &fx_systemPool[index])
            return &fx_physicsBodySidecars[index];
    }
    return nullptr;
}

FxSystemBuffers *__cdecl FX_GetSystemBuffers(int32_t clientIndex)
{
    if (clientIndex)
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            152,
            0,
            "%s\n\t(clientIndex) = %i",
            "(clientIndex == 0)",
            clientIndex);
    return fx_systemBufferPool;
}

void __cdecl FX_LinkSystemBuffers(FxSystem *system, FxSystemBuffers *systemBuffers)
{
    system->elems = systemBuffers->elems;
    system->effects = systemBuffers->effects;
    system->trails = systemBuffers->trails;
    system->trailElems = systemBuffers->trailElems;
    system->visState = systemBuffers->visState;
    system->deferredElems = systemBuffers->deferredElems;
}

namespace
{
bool FX_RebuildPoolAllocationStatesInternal(
    FxSystem *const system,
    const bool reportFailure) noexcept
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!system || !states || !system->effects || !system->elems
        || !system->trails
        || !system->trailElems)
    {
        if (reportFailure)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                180,
                0,
                "%s",
                "system and FX pool sidecars are linked");
        }
        return false;
    }

    FxPoolAllocationStates rebuilt{};
    alignas(4) volatile std::int32_t rebuiltElemCount = 0;
    alignas(4) volatile std::int32_t rebuiltTrailCount = 0;
    alignas(4) volatile std::int32_t rebuiltTrailElemCount = 0;

    volatile std::int32_t *const archiveGate =
        FX_GetArchiveGate(system);
    if (!archiveGate)
        return false;
    const fx::archive::ArchiveGateValue archiveGateState =
        static_cast<fx::archive::ArchiveGateValue>(
            Sys_AtomicLoad(archiveGate));
    const bool ownsArchive = FX_ValidateArchiveExclusiveState(system);
    if (fx::archive::ArchiveGateBlocksAllocatorAdmission(
            archiveGateState)
        && !ownsArchive)
    {
        return false;
    }
    if (ownsArchive)
        Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    else
        FX_EnterArchiveAwarePoolCriticalSection();
    const FxPoolMutationStatus elemStatus =
        FxPoolRebuildAllocationStateLocked<FxElem, MAX_ELEMS>(
            &system->firstFreeElem,
            system->elems,
            &rebuiltElemCount,
            &rebuilt.elems);
    const FxPoolMutationStatus trailStatus =
        FxPoolRebuildAllocationStateLocked<FxTrail, MAX_TRAILS>(
            &system->firstFreeTrail,
            system->trails,
            &rebuiltTrailCount,
            &rebuilt.trails);
    const FxPoolMutationStatus trailElemStatus =
        FxPoolRebuildAllocationStateLocked<FxTrailElem, MAX_TRAIL_ELEMS>(
            &system->firstFreeTrailElem,
            system->trailElems,
            &rebuiltTrailElemCount,
            &rebuilt.trailElems);

    const bool valid = elemStatus == FxPoolMutationStatus::Success
        && trailStatus == FxPoolMutationStatus::Success
        && trailElemStatus == FxPoolMutationStatus::Success
        && Sys_AtomicLoad(&system->activeElemCount)
            == Sys_AtomicLoad(&rebuiltElemCount)
        && Sys_AtomicLoad(&system->activeTrailCount)
            == Sys_AtomicLoad(&rebuiltTrailCount)
        && Sys_AtomicLoad(&system->activeTrailElemCount)
            == Sys_AtomicLoad(&rebuiltTrailElemCount);
    if (valid)
    {
        *states = rebuilt;
        for (std::size_t effectIndex = 0;
             effectIndex < FX_EFFECT_LIMIT;
             ++effectIndex)
        {
            volatile std::int32_t *const admissionState =
                FX_GetEffectOwnerAdmissionState(
                    system, &system->effects[effectIndex]);
            if (admissionState)
            {
                const std::uint32_t effectStatus =
                    static_cast<std::uint32_t>(Sys_AtomicLoad(
                        &system->effects[effectIndex].status));
                Sys_AtomicStore(
                    admissionState,
                    (effectStatus
                        & FX_STATUS_OWNER_ADMISSION_BLOCKED) != 0
                        ? 1
                        : 0);
            }
        }
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);

    if (!valid && reportFailure)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            220,
            0,
            "FX pool state rebuild failed (%u, %u, %u)",
            static_cast<unsigned>(elemStatus),
            static_cast<unsigned>(trailStatus),
            static_cast<unsigned>(trailElemStatus));
    }
    return valid;
}
} // namespace

bool __cdecl FX_RebuildPoolAllocationStates(FxSystem *const system)
{
    return FX_RebuildPoolAllocationStatesInternal(system, true);
}

bool __cdecl FX_RebuildPoolAllocationStatesNoReport(
    FxSystem *const system) noexcept
{
    return FX_RebuildPoolAllocationStatesInternal(system, false);
}

bool __cdecl FX_ValidatePoolAllocationGraphStateWithScratch(
    FxSystem *const system,
    FxPoolAllocationGraphScratch *const scratch) noexcept
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!system || !states || !scratch)
        return false;

    volatile std::int32_t *const archiveGate =
        FX_GetArchiveGate(system);
    if (!archiveGate)
        return false;
    const fx::archive::ArchiveGateValue archiveGateState =
        static_cast<fx::archive::ArchiveGateValue>(
            Sys_AtomicLoad(archiveGate));
    const bool ownsArchive = FX_ValidateArchiveExclusiveState(system);
    if (fx::archive::ArchiveGateBlocksAllocatorAdmission(
            archiveGateState)
        && !ownsArchive)
    {
        return false;
    }
    if (ownsArchive)
        Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    else
        FX_EnterArchiveAwarePoolCriticalSection();
    const bool valid = FxValidatePoolAllocationGraphWithScratch(
        system,
        states->elems,
        states->trails,
        states->trailElems,
        scratch);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return valid;
}

bool __cdecl FX_ValidatePoolAllocationGraphState(FxSystem *const system)
{
    FxPoolAllocationGraphScratch scratch{};
    return FX_ValidatePoolAllocationGraphStateWithScratch(
        system, &scratch);
}

namespace
{
enum class FxLifecycleClaimStatus : std::uint8_t
{
    Success,
    InvalidState,
    ArchiveBusy,
    KillBusy,
    IteratorsActive,
    GateChanged,
};

struct FxLifecycleClaim
{
    FxSystem *system = nullptr;
    volatile std::int32_t *archiveGate = nullptr;
    volatile std::int32_t *killGate = nullptr;
};

FxLifecycleClaimStatus FX_BeginLifecycleClaim(
    FxSystem *const system,
    FxLifecycleClaim *const claim) noexcept
{
    if (!system || !claim)
        return FxLifecycleClaimStatus::InvalidState;
    *claim = {};
    volatile std::int32_t *const archiveGate =
        FX_GetArchiveGate(system);
    volatile std::int32_t *const killGate =
        FX_GetEffectKillGate(system);
    if (!archiveGate || !killGate)
        return FxLifecycleClaimStatus::InvalidState;
    if (Sys_AtomicCompareExchange(archiveGate, 2, 0) != 0)
        return FxLifecycleClaimStatus::ArchiveBusy;
    claim->archiveGate = archiveGate;

    // Claim archive exclusion before the kill gate. If a kill already owns
    // its gate, release archive exclusion immediately so that operation can
    // finish instead of creating a gate-order deadlock.
    if (Sys_AtomicCompareExchange(killGate, 1, 0) != 0)
    {
        (void)Sys_AtomicCompareExchange(archiveGate, 0, 2);
        *claim = {};
        return FxLifecycleClaimStatus::KillBusy;
    }
    claim->killGate = killGate;

    // Drain an allocator entrant that observed the old archive gate before it
    // changed. Gate state 2 prevents every new archive-aware entrant.
    Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (Sys_AtomicLoad(archiveGate) != 2
        || Sys_AtomicLoad(killGate) != 1)
    {
        (void)Sys_AtomicCompareExchange(killGate, 0, 1);
        (void)Sys_AtomicCompareExchange(archiveGate, 0, 2);
        *claim = {};
        return FxLifecycleClaimStatus::GateChanged;
    }

    // A zero sample is not ownership: an entrant may have passed both gate
    // waits and not incremented the reader word yet. The exclusive CAS closes
    // that admission window; a stranded entrant then waits or rolls back while
    // both external gates remain closed.
    if (!FxIteratorTryBeginExclusive(&system->iteratorCount))
    {
        (void)Sys_AtomicCompareExchange(killGate, 0, 1);
        (void)Sys_AtomicCompareExchange(archiveGate, 0, 2);
        *claim = {};
        return FxLifecycleClaimStatus::IteratorsActive;
    }
    claim->system = system;
    if (Sys_AtomicLoad(archiveGate) != 2
        || Sys_AtomicLoad(killGate) != 1)
    {
        (void)FxIteratorEndExclusive(&system->iteratorCount);
        (void)Sys_AtomicCompareExchange(killGate, 0, 1);
        (void)Sys_AtomicCompareExchange(archiveGate, 0, 2);
        *claim = {};
        return FxLifecycleClaimStatus::GateChanged;
    }
    return FxLifecycleClaimStatus::Success;
}

bool FX_EndLifecycleClaim(FxLifecycleClaim *const claim) noexcept
{
    if (!claim || !claim->system || !claim->archiveGate
        || !claim->killGate)
        return false;
    const bool iteratorReleased =
        FxIteratorEndExclusive(&claim->system->iteratorCount);
    const bool killReleased =
        Sys_AtomicCompareExchange(claim->killGate, 0, 1) == 1;
    const bool archiveReleased =
        Sys_AtomicCompareExchange(claim->archiveGate, 0, 2) == 2;
    *claim = {};
    return iteratorReleased && killReleased && archiveReleased;
}

bool FX_ClearSystemForShutdownLocked(FxSystem *const system) noexcept
{
    if (!system || Sys_AtomicLoad(&system->iteratorCount) != -1)
        return false;

    // Keep the exclusive iterator word intact while clearing the surrounding
    // legacy image. A whole-object memset would publish zero early and race an
    // entrant that passed its gate checks immediately before this claim.
    std::uint8_t *const bytes = reinterpret_cast<std::uint8_t *>(system);
    std::uint8_t *const iteratorBytes =
        reinterpret_cast<std::uint8_t *>(
            const_cast<std::int32_t *>(&system->iteratorCount));
    const std::size_t prefixSize =
        static_cast<std::size_t>(iteratorBytes - bytes);
    if (prefixSize > sizeof(FxSystem) - sizeof(system->iteratorCount))
        return false;
    std::memset(bytes, 0, prefixSize);
    std::memset(
        iteratorBytes + sizeof(system->iteratorCount),
        0,
        sizeof(FxSystem) - prefixSize - sizeof(system->iteratorCount));
    return Sys_AtomicLoad(&system->iteratorCount) == -1;
}

fx::physics::SidecarStatus FX_DrainPhysicsBodySidecarLocked(
    FxSystem *const system) noexcept
{
    fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(system);
    if (!sidecar)
        return fx::physics::SidecarStatus::InvalidArgument;
    if (!sidecar->IsInitialized())
        return fx::physics::ResetEmpty(sidecar);

    fx::physics::SidecarStatus status = fx::physics::Validate(sidecar);
    if (status != fx::physics::SidecarStatus::Success)
        return status;
    std::size_t destroyedCount = 0;
    while (sidecar->ActiveCount() != 0
        && destroyedCount < fx::physics::BODY_LIMIT)
    {
        const fx::physics::IndexedBodyResult body =
            fx::physics::TakeFirst(sidecar);
        if (!body)
            return body.status;
        Phys_ObjDestroy(PHYS_WORLD_FX, body.body);
        ++destroyedCount;
    }
    if (sidecar->ActiveCount() != 0)
        return fx::physics::SidecarStatus::ActiveCountCorrupt;
    return fx::physics::ResetEmpty(sidecar);
}

bool FX_CanResetSystemGraphUnderExclusiveClaim(
    const FxSystem *const system) noexcept
{
    if (!system)
        return false;
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    const FxSystemBuffers *const buffers =
        FX_GetOwnedSystemBuffers(system);
    return states && buffers
        && system->effects == buffers->effects
        && system->elems == buffers->elems
        && system->trailElems == buffers->trailElems
        && system->trails == buffers->trails
        && system->visState == buffers->visState
        && system->deferredElems == buffers->deferredElems
        && Sys_AtomicLoad(&system->iteratorCount) == -1;
}

fx::physics::SidecarStatus FX_ResetSystemGraphUnderExclusiveClaim(
    FxSystem *const system) noexcept
{
    if (!FX_CanResetSystemGraphUnderExclusiveClaim(system))
        return fx::physics::SidecarStatus::InvalidArgument;
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);

    system->effects->def = nullptr;
    for (std::int32_t effectIndex = 0;
         effectIndex < FX_EFFECT_LIMIT;
         ++effectIndex)
    {
        system->allEffectHandles[effectIndex] = FxEncodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects, &system->effects[effectIndex]);
        volatile std::int32_t *const admissionState =
            FX_GetEffectOwnerAdmissionState(
                system, &system->effects[effectIndex]);
        if (admissionState)
            Sys_AtomicStore(admissionState, 0);
    }
    system->firstActiveEffect = 0;
    system->firstNewEffect = 0;
    system->firstFreeEffect = 0;
    volatile std::int32_t *const iteratorGeneration =
        FX_GetCooperativeIteratorGenerationState(system);
    if (iteratorGeneration)
        Sys_AtomicIncrement(iteratorGeneration);
    FxClearGarbageCollectionRequest(&system->needsGarbageCollection);
    system->deferredElemCount = 0;

    system->firstFreeElem = 0;
    for (std::size_t index = 0; index < MAX_ELEMS - 1; ++index)
    {
        system->elems[index].nextFree =
            static_cast<std::int32_t>(index + 1);
    }
    system->elems[MAX_ELEMS - 1].nextFree = -1;
    Sys_AtomicStore(&system->activeElemCount, 0);

    system->firstFreeTrailElem = 0;
    for (std::size_t index = 0; index < MAX_TRAIL_ELEMS - 1; ++index)
    {
        system->trailElems[index].nextFree =
            static_cast<std::int32_t>(index + 1);
    }
    system->trailElems[MAX_TRAIL_ELEMS - 1].nextFree = -1;
    Sys_AtomicStore(&system->activeTrailElemCount, 0);

    system->firstFreeTrail = 0;
    for (std::size_t index = 0; index < MAX_TRAILS - 1; ++index)
    {
        system->trails[index].nextFree =
            static_cast<std::int32_t>(index + 1);
    }
    system->trails[MAX_TRAILS - 1].nextFree = -1;
    Sys_AtomicStore(&system->activeTrailCount, 0);

    FxPoolResetAllocationState(&states->elems);
    FxPoolResetAllocationState(&states->trails);
    FxPoolResetAllocationState(&states->trailElems);
    Sys_AtomicStore(&system->activeSpotLightEffectCount, 0);
    Sys_AtomicStore(&system->activeSpotLightElemCount, 0);
    system->activeSpotLightEffectHandle = FX_INVALID_HANDLE;
    system->activeSpotLightElemHandle = FX_INVALID_HANDLE;
    system->activeSpotLightBoltDobj = -1;
    Sys_AtomicStore(&system->gfxCloudCount, 0);
    Sys_AtomicStore(&system->visState[0].blockerCount, 0);
    Sys_AtomicStore(&system->visState[1].blockerCount, 0);
    system->visStateBufferRead = system->visState;
    system->visStateBufferWrite = system->visState + 1;
    return fx::physics::SidecarStatus::Success;
}

fx::physics::SidecarStatus FX_ResetSystemUnderLifecycleClaim(
    FxSystem *const system) noexcept
{
    if (!FX_CanResetSystemGraphUnderExclusiveClaim(system))
        return fx::physics::SidecarStatus::InvalidArgument;

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const fx::physics::SidecarStatus physicsStatus =
        FX_DrainPhysicsBodySidecarLocked(system);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    if (physicsStatus != fx::physics::SidecarStatus::Success)
        return physicsStatus;
    return FX_ResetSystemGraphUnderExclusiveClaim(system);
}
} // namespace

bool __cdecl FX_CanPublishArchiveSafeEmptyStateLocked(
    const FxSystem *const system) noexcept
{
    return FX_ValidateArchiveExclusiveState(system)
        && FX_CanResetSystemGraphUnderExclusiveClaim(system);
}

bool __cdecl FX_PublishArchiveSafeEmptyStateLockedWithScratch(
    FxSystem *const system,
    fx::physics::BodySidecarValidationScratch *const sidecarScratch,
    FxPoolAllocationGraphScratch *const poolGraphScratch) noexcept
{
    if (!sidecarScratch || !poolGraphScratch
        || !FX_CanPublishArchiveSafeEmptyStateLocked(system))
    {
        return false;
    }
    fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(system);
    if (!sidecar
        || fx::physics::ValidateWithScratch(sidecar, sidecarScratch)
            != fx::physics::SidecarStatus::Success
        || fx::physics::ValidateVacantDestination(sidecar)
            != fx::physics::SidecarStatus::Success)
    {
        return false;
    }
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
        return false;

    Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
    const fx::physics::SidecarStatus resetStatus =
        FX_ResetSystemGraphUnderExclusiveClaim(system);
    bool published = false;
    if (resetStatus == fx::physics::SidecarStatus::Success
        && FxValidatePoolAllocationGraphWithScratch(
            system,
            states->elems,
            states->trails,
            states->trailElems,
            poolGraphScratch)
        && fx::archive::RefreshArchiveGateOwnerGeneration(
            &fx_archiveThreadState,
            system,
            FX_GetCooperativeIteratorGeneration(system)))
    {
        system->isInitialized = true;
        system->isArchiving = false;
        published = true;
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return published;
}

bool __cdecl FX_PublishArchiveSafeEmptyStateLocked(
    FxSystem *const system) noexcept
{
    fx::physics::BodySidecarValidationScratch sidecarScratch{};
    FxPoolAllocationGraphScratch poolGraphScratch{};
    return FX_PublishArchiveSafeEmptyStateLockedWithScratch(
        system, &sidecarScratch, &poolGraphScratch);
}

void __cdecl FX_InitSystem(int32_t localClientNum)
{
    FxSystem *const system = FX_GetSystem(localClientNum);
    FxSystemBuffers *const systemBuffers =
        FX_GetSystemBuffers(localClientNum);
    if (!system || !systemBuffers)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            463,
            0,
            "%s",
            "system && systemBuffers");
        return;
    }

    // Registration does not touch the FxSystem image and may allocate/report;
    // finish it before closing lifecycle admission.
    FX_RegisterDvars();
    KISAK_NULLSUB();

    FxLifecycleClaim lifecycleClaim{};
    const FxLifecycleClaimStatus claimStatus =
        FX_BeginLifecycleClaim(system, &lifecycleClaim);
    if (claimStatus != FxLifecycleClaimStatus::Success)
    {
        Com_Error(
            ERR_DROP,
            "FX init could not acquire quiescent ownership (%u)",
            static_cast<unsigned>(claimStatus));
        return;
    }

    if (system->isInitialized)
    {
        const bool released = FX_EndLifecycleClaim(&lifecycleClaim);
        Com_Error(
            ERR_DROP,
            "FX system must be shut down before it is initialized again (release %u)",
            static_cast<unsigned>(released));
        return;
    }

    const bool systemCleared =
        FX_ClearSystemForShutdownLocked(system);
    if (!systemCleared)
    {
        const bool released = FX_EndLifecycleClaim(&lifecycleClaim);
        Com_Error(
            ERR_DROP,
            "FX init could not clear lifecycle state (release %u)",
            static_cast<unsigned>(released));
        return;
    }
    std::memset(systemBuffers, 0, sizeof(*systemBuffers));
    FX_LinkSystemBuffers(system, systemBuffers);
    const fx::physics::SidecarStatus resetStatus =
        FX_ResetSystemUnderLifecycleClaim(system);
    if (resetStatus != fx::physics::SidecarStatus::Success)
    {
        const bool released = FX_EndLifecycleClaim(&lifecycleClaim);
        Com_Error(
            ERR_DROP,
            "FX init could not reset lifecycle state (%u, release %u)",
            static_cast<unsigned>(resetStatus),
            static_cast<unsigned>(released));
        return;
    }

    system->msecNow = 0;
    system->msecDraw = -1;
    system->cameraPrev.isValid = 1;
    system->cameraPrev.frustumPlaneCount = 0;
    system->frameCount = 1;
    if (system->firstActiveEffect)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 479, 1, "%s", "system->firstActiveEffect == 0");
    if (system->firstNewEffect)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 480, 1, "%s", "system->firstNewEffect == 0");
    if (system->firstFreeEffect)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 481, 1, "%s", "system->firstFreeEffect == 0");
    if (localClientNum)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_marks.h",
            139,
            0,
            "%s\n\t(clientIndex) = %i",
            "(clientIndex == 0)",
            localClientNum);
    FX_InitMarksSystem(fx_marksSystemPool);
    system->localClientNum = localClientNum;
    system->isInitialized = 1;
    fx_serverVisClient = localClientNum;
    if (!FX_EndLifecycleClaim(&lifecycleClaim))
    {
        Com_Error(ERR_DROP, "FX init could not release lifecycle ownership");
    }
}

void __cdecl FX_ResetSystem(FxSystem *system)
{
    FxLifecycleClaim lifecycleClaim{};
    const FxLifecycleClaimStatus claimStatus =
        FX_BeginLifecycleClaim(system, &lifecycleClaim);
    if (claimStatus != FxLifecycleClaimStatus::Success)
    {
        Com_Error(
            ERR_DROP,
            "FX reset could not acquire quiescent ownership (%u)",
            static_cast<unsigned>(claimStatus));
        return;
    }

    const fx::physics::SidecarStatus resetStatus =
        FX_ResetSystemUnderLifecycleClaim(system);
    const bool released = FX_EndLifecycleClaim(&lifecycleClaim);
    if (resetStatus != fx::physics::SidecarStatus::Success)
    {
        Com_Error(
            ERR_DROP,
            "FX reset failed under lifecycle ownership (%u, release %u)",
            static_cast<unsigned>(resetStatus),
            static_cast<unsigned>(released));
        return;
    }
    if (!released)
    {
        Com_Error(ERR_DROP, "FX reset could not release lifecycle ownership");
    }
}

int32_t __cdecl FX_EffectToHandle(FxSystem *system, FxEffect *effect)
{
    if (!system || !effect)
    {
        iassert(system);
        iassert(effect);
        return -1;
    }

    const std::uint16_t handle =
        FxEncodeHandle<FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    vassert(handle != FX_INVALID_HANDLE, "%p %p", system->effects, effect);
    return handle == FX_INVALID_HANDLE ? -1 : static_cast<std::int32_t>(handle);
}


void __cdecl FX_ShutdownSystem(int32_t localClientNum)
{
    FxSystem *system; // [esp+0h] [ebp-8h]
    FxSystemBuffers *systemBuffers; // [esp+4h] [ebp-4h]

    system = FX_GetSystem(localClientNum);
    systemBuffers = FX_GetSystemBuffers(localClientNum);
    if (!system)
    {
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 503, 0, "%s", "system");
        return;
    }
    if (!systemBuffers)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            505,
            0,
            "%s",
            "FX system buffers exist during shutdown");
        return;
    }

    volatile std::int32_t *const iteratorGeneration =
        FX_GetCooperativeIteratorGenerationState(system);
    FxLifecycleClaim lifecycleClaim{};
    const FxLifecycleClaimStatus claimStatus =
        FX_BeginLifecycleClaim(system, &lifecycleClaim);
    if (claimStatus != FxLifecycleClaimStatus::Success)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            503,
            0,
            "%s",
            "FX shutdown begins with a quiescent archive gate");
        Com_Error(
            ERR_DROP,
            "FX shutdown could not acquire quiescent ownership (%u)",
            static_cast<unsigned>(claimStatus));
        return;
    }

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const fx::physics::SidecarStatus physicsStatus =
        FX_DrainPhysicsBodySidecarLocked(system);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    if (physicsStatus != fx::physics::SidecarStatus::Success)
    {
        const bool released = FX_EndLifecycleClaim(&lifecycleClaim);
        Com_Error(
            ERR_DROP,
            "FX shutdown could not drain physics ownership (%u, release %u)",
            static_cast<unsigned>(physicsStatus),
            static_cast<unsigned>(released));
        return;
    }

    if (iteratorGeneration)
        Sys_AtomicIncrement(iteratorGeneration);
    if (fx_archiveThreadState.identity == system)
        fx_archiveThreadState = {};
    const bool systemCleared =
        FX_ClearSystemForShutdownLocked(system);
    if (!systemCleared)
    {
        const bool released = FX_EndLifecycleClaim(&lifecycleClaim);
        Com_Error(
            ERR_DROP,
            "FX shutdown could not clear lifecycle state (release %u)",
            static_cast<unsigned>(released));
        return;
    }
    memset((uint8_t *)systemBuffers, 0, sizeof(FxSystemBuffers));
    FX_UnregisterAll();
    if (!FX_EndLifecycleClaim(&lifecycleClaim))
    {
        Com_Error(ERR_DROP, "FX shutdown could not release lifecycle gates");
        return;
    }
}

void __cdecl FX_RelocateSystem(FxSystem *system, int32_t relocationDistance)
{
    if (relocationDistance)
    {
        system->visStateBufferRead = (const FxVisState *)((char *)system->visStateBufferRead + relocationDistance);
        system->visStateBufferWrite = (FxVisState *)((char *)system->visStateBufferWrite + relocationDistance);
    }
}

namespace
{
constexpr std::uint32_t FX_OWNED_EFFECT_COUNT_INCREMENT =
    1u << FX_STATUS_OWNED_EFFECTS_SHIFT;

struct FxEffectReferenceReleasePlan
{
    std::array<std::uint16_t, FX_EFFECT_LIMIT> referenceHandles{};
    std::array<std::uint16_t, FX_EFFECT_LIMIT> ownedCountHandles{};
    std::array<bool, FX_EFFECT_LIMIT + 1> requestGarbageCollection{};
    std::size_t referenceCount = 0;
    std::size_t ownedCount = 0;
};

bool FX_TryAdjustEffectReferenceCount(
    FxEffect *const effect,
    const bool increment) noexcept
{
    if (!effect)
        return false;
    std::int32_t observed = Sys_AtomicLoad(&effect->status);
    for (;;)
    {
        const std::uint32_t status =
            static_cast<std::uint32_t>(observed);
        const std::uint32_t referenceCount = status & FX_STATUS_REF_COUNT_MASK;
        if (referenceCount == 0
            || (increment
                && referenceCount == FX_STATUS_REF_COUNT_MASK))
        {
            return false;
        }
        const std::uint32_t desiredStatus = increment
            ? status + 1u
            : status - 1u;
        const std::int32_t previous = Sys_AtomicCompareExchange(
            &effect->status,
            static_cast<std::int32_t>(desiredStatus),
            observed);
        if (previous == observed)
            return true;
        observed = previous;
    }
}

bool FX_TryAdjustOwnedEffectCount(
    FxEffect *const effect,
    const bool increment) noexcept
{
    if (!effect)
        return false;
    std::int32_t observed = Sys_AtomicLoad(&effect->status);
    for (;;)
    {
        const std::uint32_t status =
            static_cast<std::uint32_t>(observed);
        const std::uint32_t ownedCount =
            status & FX_STATUS_OWNED_EFFECTS_MASK;
        if ((!increment && ownedCount < FX_OWNED_EFFECT_COUNT_INCREMENT)
            || (increment && ownedCount == FX_STATUS_OWNED_EFFECTS_MASK))
        {
            return false;
        }
        const std::uint32_t desiredStatus = increment
            ? status + FX_OWNED_EFFECT_COUNT_INCREMENT
            : status - FX_OWNED_EFFECT_COUNT_INCREMENT;
        const std::int32_t previous = Sys_AtomicCompareExchange(
            &effect->status,
            static_cast<std::int32_t>(desiredStatus),
            observed);
        if (previous == observed)
            return true;
        observed = previous;
    }
}

bool FX_TryAddOwnedEffectReference(FxEffect *const effect) noexcept
{
    if (!effect)
        return false;
    std::int32_t observed = Sys_AtomicLoad(&effect->status);
    for (;;)
    {
        const std::uint32_t status =
            static_cast<std::uint32_t>(observed);
        const std::uint32_t referenceCount =
            status & FX_STATUS_REF_COUNT_MASK;
        const std::uint32_t ownedCount =
            status & FX_STATUS_OWNED_EFFECTS_MASK;
        if (referenceCount == 0
            || referenceCount == FX_STATUS_REF_COUNT_MASK
            || ownedCount == FX_STATUS_OWNED_EFFECTS_MASK)
        {
            return false;
        }
        const std::uint32_t desiredStatus = status + 1u
            + FX_OWNED_EFFECT_COUNT_INCREMENT;
        const std::int32_t previous = Sys_AtomicCompareExchange(
            &effect->status,
            static_cast<std::int32_t>(desiredStatus),
            observed);
        if (previous == observed)
            return true;
        observed = previous;
    }
}

bool FX_TryAddEffectReferenceSerialized(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    if (!system || !effect)
        return false;
    FX_EnterArchiveAwarePoolCriticalSection();
    const bool adjusted =
        FX_TryAdjustEffectReferenceCount(effect, true);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return adjusted;
}

enum class FxOwnedEffectReferenceAddResult : std::uint8_t
{
    Success,
    Blocked,
    Invalid
};

FxOwnedEffectReferenceAddResult FX_TryAddOwnedEffectReferenceSerialized(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    if (!system || !effect)
        return FxOwnedEffectReferenceAddResult::Invalid;
    volatile std::int32_t *const admissionState =
        FX_GetEffectOwnerAdmissionState(system, effect);
    if (!admissionState)
        return FxOwnedEffectReferenceAddResult::Invalid;
    FX_EnterArchiveAwarePoolCriticalSection();
    FxOwnedEffectReferenceAddResult result =
        FxOwnedEffectReferenceAddResult::Invalid;
    const std::uint32_t status = static_cast<std::uint32_t>(
        Sys_AtomicLoad(&effect->status));
    if (Sys_AtomicLoad(admissionState) != 0
        || (status & FX_STATUS_OWNER_ADMISSION_BLOCKED) != 0)
    {
        result = FxOwnedEffectReferenceAddResult::Blocked;
    }
    else if (FX_TryAddOwnedEffectReference(effect))
    {
        result = FxOwnedEffectReferenceAddResult::Success;
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return result;
}

bool FX_BuildEffectReferenceReleasePlan(
    FxSystem *const system,
    FxEffect *const effect,
    FxEffectReferenceReleasePlan *const plan) noexcept
{
    if (!system || !system->effects || !effect || !plan)
        return false;

    const std::uint16_t initialHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    if (initialHandle == FX_INVALID_HANDLE)
        return false;

    const std::uint32_t initialStatus =
        static_cast<std::uint32_t>(Sys_AtomicLoad(&effect->status));
    if ((initialStatus & FX_STATUS_REF_COUNT_MASK) != 1
        || (initialStatus & FX_STATUS_OWNED_EFFECTS_MASK) != 0)
    {
        return false;
    }

    constexpr std::size_t effectHandleStride = FxHandleStride<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>();
    std::array<bool, FX_EFFECT_LIMIT> visitedEffects{};
    visitedEffects[initialHandle / effectHandleStride] = true;

    plan->requestGarbageCollection[0] = true;
    if ((initialStatus & FX_STATUS_SELF_OWNED) != 0)
        return effect->owner == initialHandle;

    FxEffect *child = effect;
    for (;;)
    {
        if (plan->ownedCount == plan->ownedCountHandles.size()
            || plan->referenceCount == plan->referenceHandles.size())
        {
            return false;
        }

        const std::uint16_t ownerHandle = child->owner;
        FxEffect *const owner = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects, ownerHandle);
        if (!owner)
            return false;
        const std::size_t ownerIndex = ownerHandle / effectHandleStride;
        if (visitedEffects[ownerIndex])
            return false;
        visitedEffects[ownerIndex] = true;

        const std::uint32_t ownerStatus =
            static_cast<std::uint32_t>(Sys_AtomicLoad(&owner->status));
        if ((ownerStatus & FX_STATUS_REF_COUNT_MASK) == 0
            || (ownerStatus & FX_STATUS_OWNED_EFFECTS_MASK)
                < FX_OWNED_EFFECT_COUNT_INCREMENT)
        {
            return false;
        }

        plan->ownedCountHandles[plan->ownedCount++] = ownerHandle;
        const std::size_t referenceIndex = plan->referenceCount++;
        plan->referenceHandles[referenceIndex] = ownerHandle;
        if ((ownerStatus & FX_STATUS_REF_COUNT_MASK) != 1)
            return true;
        // The relationship from child is still published while this plan is
        // validated. It is the owner's final owned-effect/reference pair.
        if ((ownerStatus & FX_STATUS_OWNED_EFFECTS_MASK)
            != FX_OWNED_EFFECT_COUNT_INCREMENT)
            return false;

        plan->requestGarbageCollection[referenceIndex + 1] = true;
        if ((ownerStatus & FX_STATUS_SELF_OWNED) != 0)
            return owner->owner == ownerHandle;
        child = owner;
    }
}

bool FX_ApplyEffectReferenceReleasePlan(
    FxSystem *const system,
    FxEffect *const initialEffect,
    const FxEffectReferenceReleasePlan &plan) noexcept
{
    std::size_t adjustedOwnedCount = 0;
    for (; adjustedOwnedCount < plan.ownedCount; ++adjustedOwnedCount)
    {
        FxEffect *const owner = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects,
                plan.ownedCountHandles[adjustedOwnedCount]);
        if (!FX_TryAdjustOwnedEffectCount(owner, false))
            break;
    }
    if (adjustedOwnedCount != plan.ownedCount)
    {
        while (adjustedOwnedCount != 0)
        {
            FxEffect *const owner = FxDecodeHandle<
                FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                    system->effects,
                    plan.ownedCountHandles[--adjustedOwnedCount]);
            FX_TryAdjustOwnedEffectCount(owner, true);
        }
        return false;
    }

    std::size_t nextReferenceIndex = plan.referenceCount;
    while (nextReferenceIndex != 0)
    {
        FxEffect *const referencedEffect = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects,
                plan.referenceHandles[nextReferenceIndex - 1]);
        if (!FX_TryAdjustEffectReferenceCount(referencedEffect, false))
            break;
        --nextReferenceIndex;
    }
    if (nextReferenceIndex != 0)
    {
        for (std::size_t referenceIndex = nextReferenceIndex;
             referenceIndex < plan.referenceCount;
             ++referenceIndex)
        {
            FxEffect *const referencedEffect = FxDecodeHandle<
                FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                    system->effects,
                    plan.referenceHandles[referenceIndex]);
            FX_TryAdjustEffectReferenceCount(referencedEffect, true);
        }
        while (adjustedOwnedCount != 0)
        {
            FxEffect *const owner = FxDecodeHandle<
                FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                    system->effects,
                    plan.ownedCountHandles[--adjustedOwnedCount]);
            FX_TryAdjustOwnedEffectCount(owner, true);
        }
        return false;
    }

    if (plan.requestGarbageCollection[0])
        FxRequestGarbageCollection(&system->needsGarbageCollection);
    for (std::size_t referenceIndex = 0;
         referenceIndex < plan.referenceCount;
         ++referenceIndex)
    {
        if (plan.requestGarbageCollection[referenceIndex + 1])
            FxRequestGarbageCollection(&system->needsGarbageCollection);
    }
    (void)initialEffect;
    return true;
}

bool FX_EffectNoLongerReferencedBounded(
    FxSystem *const system,
    FxEffect *const effect) noexcept
{
    FxEffectReferenceReleasePlan plan{};
    return FX_BuildEffectReferenceReleasePlan(system, effect, &plan)
        && FX_ApplyEffectReferenceReleasePlan(system, effect, plan);
}
}

void __cdecl FX_DelRefToEffect(FxSystem *system, FxEffect *effect)
{
    if (!system || !effect)
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            393,
            0,
            "%s",
            "system && effect");
        return;
    }

    FX_EnterArchiveAwarePoolCriticalSection();
    const std::uint32_t status =
        static_cast<std::uint32_t>(Sys_AtomicLoad(&effect->status));
    if ((status & FX_STATUS_REF_COUNT_MASK) == 0)
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            411,
            0,
            "%s",
            "FX effect reference count is nonzero");
        FxRequestGarbageCollection(&system->needsGarbageCollection);
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return;
    }
    if ((status & FX_STATUS_REF_COUNT_MASK) == 1
        && !FX_EffectNoLongerReferencedBounded(system, effect))
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            411,
            0,
            "%s",
            "FX owner-reference graph is bounded, acyclic, and consistent");
        FxRequestGarbageCollection(&system->needsGarbageCollection);
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        return;
    }
    if (!FX_TryAdjustEffectReferenceCount(effect, false))
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            411,
            0,
            "%s",
            "FX effect reference decrement succeeds");
        FxRequestGarbageCollection(&system->needsGarbageCollection);
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
}

static bool FX_ValidateSpotLightForGarbageCollection(
    FxSystem *system,
    std::uint16_t effectHandle) noexcept;

static bool FX_ValidateEffectForGarbageCollection(
    const FxEffect *const effect) noexcept
{
    if (!effect)
        return false;
    const std::uint32_t status = static_cast<std::uint32_t>(
        Sys_AtomicLoad(&effect->status));
    constexpr std::uint32_t allowedZeroReferenceStatus =
        FX_STATUS_OWNER_ADMISSION_BLOCKED | FX_STATUS_SELF_OWNED;
    if ((status & ~allowedZeroReferenceStatus) != 0
        || effect->firstSortedElemHandle != FX_INVALID_HANDLE)
    {
        return false;
    }
    for (std::size_t elemClass = 0; elemClass < 3; ++elemClass)
    {
        if (effect->firstElemHandle[elemClass] != FX_INVALID_HANDLE)
            return false;
    }
    return true;
}

void __cdecl FX_RunGarbageCollection(FxSystem *system)
{
    std::array<std::uint16_t, FX_EFFECT_LIMIT> freedHandles{};
    std::size_t freedCount = 0;
    bool invalidEffectRing = false;
    bool invalidEffectHandle = false;
    bool invalidPoolGraph = false;
    bool invalidElementGraph = false;
    bool invalidTrailGraph = false;
    bool invalidSpotLightGraph = false;

    if (!system)
    {
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 779, 0, "%s", "system");
        return;
    }
    const bool reuseKillExclusive =
        FX_ThreadOwnsEffectKillExclusive(system);
    if (FxGarbageCollectionRequested(&system->needsGarbageCollection)
        && (reuseKillExclusive
            || FX_BeginIteratingOverEffects_Exclusive(system)))
    {
        FxClearGarbageCollectionRequest(&system->needsGarbageCollection);
        const std::int32_t firstActiveEffect =
            Sys_AtomicLoad(&system->firstActiveEffect);
        const std::int32_t firstNewEffect =
            Sys_AtomicLoad(&system->firstNewEffect);
        const std::int32_t firstFreeEffect =
            Sys_AtomicLoad(&system->firstFreeEffect);
        const std::int64_t activeEffectCount64 =
            static_cast<std::int64_t>(firstNewEffect) - firstActiveEffect;
        const std::int64_t allocatedEffectCount64 =
            static_cast<std::int64_t>(firstFreeEffect) - firstActiveEffect;
        if (firstActiveEffect < 0
            || firstNewEffect < firstActiveEffect
            || firstFreeEffect < firstNewEffect
            || activeEffectCount64 > FX_EFFECT_LIMIT
            || allocatedEffectCount64 > FX_EFFECT_LIMIT)
        {
            invalidEffectRing = true;
            FxRequestGarbageCollection(&system->needsGarbageCollection);
        }
        if (!invalidEffectRing
            && !FX_ValidatePoolAllocationGraphState(system))
        {
            invalidPoolGraph = true;
            FxRequestGarbageCollection(&system->needsGarbageCollection);
        }

        std::int32_t activeIndex = firstNewEffect;
        if (!invalidEffectRing && !invalidPoolGraph)
        {
            const std::size_t activeEffectCount =
                static_cast<std::size_t>(activeEffectCount64);
            for (std::size_t scannedCount = 0;
                 scannedCount < activeEffectCount;
                 ++scannedCount)
            {
                --activeIndex;
                const std::uint16_t effectHandle =
                    system->allEffectHandles[
                        static_cast<std::size_t>(activeIndex)
                        & (FX_EFFECT_LIMIT - 1)];
                const auto retainEffectHandle = [&]() noexcept {
                    system->allEffectHandles[
                        (static_cast<std::size_t>(activeIndex) + freedCount)
                        & (FX_EFFECT_LIMIT - 1)] = effectHandle;
                };
                FxEffect *const effect = FxDecodeHandle<
                    FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                        system->effects, effectHandle);
                if (!effect)
                {
                    retainEffectHandle();
                    FxRequestGarbageCollection(&system->needsGarbageCollection);
                    invalidEffectHandle = true;
                }
                else if (static_cast<std::uint16_t>(
                             Sys_AtomicLoad(&effect->status)) != 0)
                {
                    retainEffectHandle();
                }
                else if (!FX_ValidateEffectForGarbageCollection(effect))
                {
                    retainEffectHandle();
                    FxRequestGarbageCollection(&system->needsGarbageCollection);
                    invalidElementGraph = true;
                }
                else if (!FX_ValidateSpotLightForGarbageCollection(
                             system, effectHandle))
                {
                    retainEffectHandle();
                    FxRequestGarbageCollection(&system->needsGarbageCollection);
                    invalidSpotLightGraph = true;
                }
                else if (!FX_RunGarbageCollection_FreeTrails(system, effect)
                         || effect->firstTrailHandle != FX_INVALID_HANDLE)
                {
                    retainEffectHandle();
                    FxRequestGarbageCollection(&system->needsGarbageCollection);
                    invalidTrailGraph = true;
                }
                else if (!FX_RunGarbageCollection_FreeSpotLight(
                             system, effectHandle))
                {
                    retainEffectHandle();
                    FxRequestGarbageCollection(&system->needsGarbageCollection);
                    invalidSpotLightGraph = true;
                }
                else if (freedCount == freedHandles.size())
                {
                    retainEffectHandle();
                    FxRequestGarbageCollection(&system->needsGarbageCollection);
                    invalidEffectRing = true;
                }
                else
                {
                    freedHandles[freedCount++] = effectHandle;
                }
            }
            while (freedCount != 0)
            {
                const std::uint16_t freedHandle =
                    freedHandles[--freedCount];
                system->allEffectHandles[
                    static_cast<std::size_t>(activeIndex++)
                    & (FX_EFFECT_LIMIT - 1)] = freedHandle;
                FxEffect *const effect = FxDecodeHandle<
                    FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                        system->effects, freedHandle);
                if (effect)
                    memset(static_cast<void *>(effect), 0, sizeof(*effect));
                else
                    invalidEffectHandle = true;
            }
            const std::int32_t remainingActiveEffectCount =
                firstNewEffect - activeIndex;
            const std::int32_t remainingAllocatedEffectCount =
                firstFreeEffect - activeIndex;
            const std::int32_t normalizedFirstActive =
                activeIndex & (FX_EFFECT_LIMIT - 1);
            Sys_AtomicStore(
                &system->firstActiveEffect, normalizedFirstActive);
            Sys_AtomicStore(
                &system->firstNewEffect,
                normalizedFirstActive + remainingActiveEffectCount);
            Sys_AtomicStore(
                &system->firstFreeEffect,
                normalizedFirstActive + remainingAllocatedEffectCount);
        }

        const bool releasedExclusive = reuseKillExclusive
            || FxIteratorEndExclusive(&system->iteratorCount);
        if (!releasedExclusive)
            MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 535, 0, "%s", "system->iteratorCount == -1");
        if (!releasedExclusive)
            Com_Error(ERR_DROP, "Invalid FX exclusive iterator state during garbage collection");
        else if (invalidEffectRing)
            Com_Error(ERR_DROP, "Invalid FX effect ring during garbage collection");
        else if (invalidEffectHandle)
            Com_Error(ERR_DROP, "Invalid FX effect handle during garbage collection");
        else if (invalidPoolGraph)
            Com_Error(ERR_DROP, "Invalid FX pool graph during garbage collection");
        else if (invalidElementGraph)
            Com_Error(ERR_DROP, "Invalid FX element graph during garbage collection");
        else if (invalidTrailGraph)
            Com_Error(ERR_DROP, "Invalid FX trail graph during garbage collection");
        else if (invalidSpotLightGraph)
            Com_Error(ERR_DROP, "Invalid FX spotlight graph during garbage collection");
    }
}

bool __cdecl FX_BeginIteratingOverEffects_Exclusive(FxSystem *system)
{
    if (!system)
        return false;
    const std::uint32_t admissionGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    if (FX_ThreadOwnsEffectKillExclusive(system)
        || FX_ArchiveGateIsActive(system)
        || !FxIteratorTryBeginExclusive(&system->iteratorCount))
    {
        return false;
    }
    const bool initialized = system->isInitialized != 0;
    const bool archiving = system->isArchiving != 0;
    const bool archiveActive = FX_ArchiveGateIsActive(system);
    const std::uint32_t currentGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    if (!archiveActive && initialized && !archiving
        && currentGeneration == admissionGeneration)
    {
        return true;
    }
    if (!FxIteratorEndExclusive(&system->iteratorCount))
    {
        Com_Error(ERR_DROP, "Invalid FX exclusive iterator state during archive race");
        return false;
    }
    return false;
}

static bool FX_ValidateSpotLightForGarbageCollection(
    FxSystem *const system,
    const std::uint16_t effectHandle) noexcept
{
    if (!system)
        return false;
    const std::int32_t effectCount =
        Sys_AtomicLoad(&system->activeSpotLightEffectCount);
    const std::int32_t elemCount =
        Sys_AtomicLoad(&system->activeSpotLightElemCount);
    if (effectCount < 0 || effectCount > 1
        || elemCount < 0 || elemCount > 1
        || elemCount > effectCount)
    {
        return false;
    }
    if (effectCount == 0)
        return elemCount == 0;

    FxEffect *const spotLightEffect = FxDecodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, system->activeSpotLightEffectHandle);
    bool hasSpotLight = false;
    if (!spotLightEffect
        || !FX_ValidateSpotLightEffectDef(
            spotLightEffect->def, &hasSpotLight)
        || !hasSpotLight)
    {
        return false;
    }
    if (system->activeSpotLightEffectHandle != effectHandle)
        return true;
    return elemCount == 0
        && (static_cast<std::uint32_t>(
                Sys_AtomicLoad(&spotLightEffect->status)) & 0xFFFFu)
            == 0;
}

bool __cdecl FX_RunGarbageCollection_FreeSpotLight(
    FxSystem *system,
    uint16_t effectHandle)
{
    if (!FX_ValidateSpotLightForGarbageCollection(system, effectHandle))
        return false;
    if (system->activeSpotLightEffectHandle == effectHandle)
    {
        system->activeSpotLightEffectHandle = FX_INVALID_HANDLE;
        system->activeSpotLightBoltDobj = -1;
        if (Sys_AtomicLoad(&system->activeSpotLightEffectCount) == 1)
            Sys_AtomicStore(&system->activeSpotLightEffectCount, 0);
    }
    return true;
}

template <typename BEFORE_PUBLISH>
bool __cdecl FX_FreePool_Generic_FxTrail_(
    FxTrail *item,
    volatile int32_t *firstFreeIndex,
    FxPool<FxTrail> *pool,
    volatile int32_t *activeCount,
    FxPoolAllocationState<MAX_TRAILS> *allocationState,
    BEFORE_PUBLISH &&beforePublish)
{
    FX_EnterArchiveAwarePoolCriticalSection();
    const FxPoolMutationStatus status =
        FxPoolFreeLocked<FxTrail, MAX_TRAILS>(
        item,
        firstFreeIndex,
        pool,
        activeCount,
        allocationState,
        std::forward<BEFORE_PUBLISH>(beforePublish));
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (status != FxPoolMutationStatus::Success
        && status != FxPoolMutationStatus::BeforePublishRejected)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            243,
            0,
            "FX trail pool free failed with status %u",
            static_cast<unsigned>(status));
    }
    return status == FxPoolMutationStatus::Success;
}

bool __cdecl FX_RunGarbageCollection_FreeTrails(FxSystem *system, FxEffect *effect)
{
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            361,
            0,
            "%s",
            "system && effect");
        return false;
    }
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
        return false;

    std::array<FxPool<FxTrail> *, MAX_TRAILS> trailsToFree{};
    std::array<bool, MAX_TRAILS> visitedTrailIndices{};
    std::size_t trailCount = 0;
    std::uint16_t trailHandle = effect->firstTrailHandle;
    while (trailHandle != FX_INVALID_HANDLE)
    {
        if (trailCount == MAX_TRAILS)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                397,
                0,
                "%s",
                "FX trail chain is bounded and acyclic");
            return false;
        }
        FxPool<FxTrail> *const trail =
            FxDecodeHandle<FxPool<FxTrail>, MAX_TRAILS, FxTrail::HANDLE_SCALE>(
                system->trails, trailHandle);
        if (!trail || !FX_IsTrailAllocated(system, &trail->item))
            return false;
        if (trail->item.firstElemHandle != FX_INVALID_HANDLE
            || trail->item.lastElemHandle != FX_INVALID_HANDLE)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                397,
                0,
                "%s",
                "garbage-collected FX trails have no live elements");
            return false;
        }
        const std::size_t trailIndex =
            static_cast<std::size_t>(trail - system->trails);
        if (visitedTrailIndices[trailIndex])
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                397,
                0,
                "%s",
                "FX trail chain is bounded and acyclic");
            return false;
        }
        visitedTrailIndices[trailIndex] = true;
        trailsToFree[trailCount++] = trail;
        trailHandle = trail->item.nextTrailHandle;
    }

    for (std::size_t index = 0; index < trailCount; ++index)
    {
        FxPool<FxTrail> *const trail = trailsToFree[index];
        const std::uint16_t firstTrailHandle =
            effect->firstTrailHandle;
        const std::uint16_t expectedTrailHandle =
            FX_PoolToHandle_Generic<FxTrail, MAX_TRAILS>(
                system->trails, &trail->item);
        if (firstTrailHandle != expectedTrailHandle)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                397,
                0,
                "%s",
                "effect trail head matches the validated free order");
            return false;
        }
        const std::uint16_t nextTrailHandle = trail->item.nextTrailHandle;
        if (!FX_FreePool_Generic_FxTrail_(
                &trail->item,
                &system->firstFreeTrail,
                system->trails,
                &system->activeTrailCount,
                &states->trails,
                [&]() noexcept {
                    effect->firstTrailHandle = nextTrailHandle;
                }))
        {
            return false;
        }
    }
    return true;
}

void __cdecl FX_SpawnEffect_AllocTrails(FxSystem *system, FxEffect *effect)
{
    const FxEffectDef *def; // [esp+4h] [ebp-1Ch]
    FxPool<FxTrail> *remoteTrail; // [esp+Ch] [ebp-14h]
    int32_t elemDefCount; // [esp+10h] [ebp-10h]
    int32_t elemDefIter; // [esp+14h] [ebp-Ch]
    FxTrail localTrail;

    if (!system || !effect || !effect->def)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            968,
            0,
            "%s",
            "system && effect && effect->def");
        return;
    }
    def = effect->def;
    const std::int64_t validatedElemDefCount =
        static_cast<std::int64_t>(def->elemDefCountOneShot)
        + def->elemDefCountLooping
        + def->elemDefCountEmission;
    if (def->elemDefCountOneShot < 0 || def->elemDefCountLooping < 0
        || def->elemDefCountEmission < 0 || validatedElemDefCount > 256
        || (validatedElemDefCount && !def->elemDefs))
    {
        Com_Error(ERR_DROP, "Invalid FX element definitions for trail allocation");
        return;
    }
    elemDefCount = static_cast<std::int32_t>(validatedElemDefCount);
    for (elemDefIter = 0; elemDefIter != elemDefCount; ++elemDefIter)
    {
        if (effect->def->elemDefs[elemDefIter].elemType == 3)
        {
            remoteTrail = FX_AllocTrail(system);
            if (!remoteTrail)
                return;

            localTrail.nextTrailHandle = effect->firstTrailHandle;
            localTrail.defIndex = static_cast<std::uint8_t>(elemDefIter);

            iassert(localTrail.defIndex == elemDefIter);

            localTrail.firstElemHandle = 0xFFFF;
            localTrail.lastElemHandle = 0xFFFF;

            localTrail.sequence = 0;

            const std::uint16_t trailHandle =
                FX_PoolToHandle_Generic<FxTrail, MAX_TRAILS>(
                    system->trails, &remoteTrail->item);
            remoteTrail->item = localTrail;
            effect->firstTrailHandle = trailHandle;
        }
    }
}

FxPool<FxTrail>* __cdecl FX_AllocPool_Generic_FxTrail_(
    volatile int32_t* firstFreeIndex,
    FxPool<FxTrail>* pool,
    volatile int32_t* activeCount,
    FxPoolAllocationState<MAX_TRAILS> *allocationState)
{
    FxPoolMutationStatus status;
    FX_EnterArchiveAwarePoolCriticalSection();
    FxPool<FxTrail> *const item =
        FxPoolAllocateLocked<FxTrail, MAX_TRAILS>(
        firstFreeIndex, pool, activeCount, allocationState, &status);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (status != FxPoolMutationStatus::Success
        && status != FxPoolMutationStatus::Empty)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            188,
            0,
            "FX trail pool allocation failed with status %u",
            static_cast<unsigned>(status));
        Com_Error(
            ERR_DROP,
            "FX trail pool allocation failed with status %u",
            static_cast<unsigned>(status));
    }
    return item;
}

FxPool<FxTrailElem>* __cdecl FX_AllocPool_Generic_FxTrailElem_(
    volatile int32_t * firstFreeIndex,
    FxPool<FxTrailElem>* pool,
    volatile int32_t * activeCount,
    FxPoolAllocationState<MAX_TRAIL_ELEMS> *allocationState)
{
    FxPoolMutationStatus status;
    FX_EnterArchiveAwarePoolCriticalSection();
    FxPool<FxTrailElem> *const item =
        FxPoolAllocateLocked<FxTrailElem, MAX_TRAIL_ELEMS>(
            firstFreeIndex, pool, activeCount, allocationState, &status);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (status != FxPoolMutationStatus::Success
        && status != FxPoolMutationStatus::Empty)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            188,
            0,
            "FX trail element pool allocation failed with status %u",
            static_cast<unsigned>(status));
        Com_Error(
            ERR_DROP,
            "FX trail element pool allocation failed with status %u",
            static_cast<unsigned>(status));
    }
    return item;
}

FxPool<FxElem>* __cdecl FX_AllocPool_Generic_FxElem_(
    volatile int32_t* firstFreeIndex,
    FxPool<FxElem>* pool,
    volatile int32_t * activeCount,
    FxPoolAllocationState<MAX_ELEMS> *allocationState)
{
    FxPoolMutationStatus status;
    FX_EnterArchiveAwarePoolCriticalSection();
    FxPool<FxElem> *const item = FxPoolAllocateLocked<FxElem, MAX_ELEMS>(
        firstFreeIndex, pool, activeCount, allocationState, &status);
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (status != FxPoolMutationStatus::Success
        && status != FxPoolMutationStatus::Empty)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            188,
            0,
            "FX element pool allocation failed with status %u",
            static_cast<unsigned>(status));
        Com_Error(
            ERR_DROP,
            "FX element pool allocation failed with status %u",
            static_cast<unsigned>(status));
    }
    return item;
}

template <typename BEFORE_PUBLISH>
FxPoolMutationStatus FX_FreePool_Generic_FxElem_Status_(
    FxElem *item,
    volatile int32_t *firstFreeIndex,
    FxPool<FxElem> *pool,
    volatile int32_t *activeCount,
    FxPoolAllocationState<MAX_ELEMS> *allocationState,
    BEFORE_PUBLISH &&beforePublish) noexcept
{
    FX_EnterArchiveAwarePoolCriticalSection();
    const FxPoolMutationStatus status =
        FxPoolFreeLocked<FxElem, MAX_ELEMS>(
            item,
            firstFreeIndex,
            pool,
            activeCount,
            allocationState,
            std::forward<BEFORE_PUBLISH>(beforePublish));
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return status;
}

template <typename BEFORE_PUBLISH>
bool __cdecl FX_FreePool_Generic_FxElem_(
    FxElem *item,
    volatile int32_t *firstFreeIndex,
    FxPool<FxElem> *pool,
    volatile int32_t *activeCount,
    FxPoolAllocationState<MAX_ELEMS> *allocationState,
    BEFORE_PUBLISH &&beforePublish)
{
    FX_EnterArchiveAwarePoolCriticalSection();
    const FxPoolMutationStatus status =
        FxPoolFreeLocked<FxElem, MAX_ELEMS>(
        item,
        firstFreeIndex,
        pool,
        activeCount,
        allocationState,
        std::forward<BEFORE_PUBLISH>(beforePublish));
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (status != FxPoolMutationStatus::Success
        && status != FxPoolMutationStatus::BeforePublishRejected)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            243,
            0,
            "FX element pool free failed with status %u",
            static_cast<unsigned>(status));
        Com_Error(
            ERR_DROP,
            "FX element pool free failed with status %u",
            static_cast<unsigned>(status));
    }
    return status == FxPoolMutationStatus::Success;
}

template <typename BEFORE_PUBLISH>
bool __cdecl FX_FreePool_Generic_FxTrailElem_(
    FxTrailElem* item,
    volatile int32_t* firstFreeIndex,
    FxPool<FxTrailElem>* pool,
    volatile int32_t *activeCount,
    FxPoolAllocationState<MAX_TRAIL_ELEMS> *allocationState,
    BEFORE_PUBLISH &&beforePublish)
{
    FX_EnterArchiveAwarePoolCriticalSection();
    const FxPoolMutationStatus status =
        FxPoolFreeLocked<FxTrailElem, MAX_TRAIL_ELEMS>(
        item,
        firstFreeIndex,
        pool,
        activeCount,
        allocationState,
        std::forward<BEFORE_PUBLISH>(beforePublish));
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (status != FxPoolMutationStatus::Success
        && status != FxPoolMutationStatus::BeforePublishRejected)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            243,
            0,
            "FX trail element pool free failed with status %u",
            static_cast<unsigned>(status));
        Com_Error(
            ERR_DROP,
            "FX trail element pool free failed with status %u",
            static_cast<unsigned>(status));
    }
    return status == FxPoolMutationStatus::Success;
}


FxPool<FxTrail> *__cdecl FX_AllocTrail(FxSystem *system)
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
    {
        Com_Error(ERR_DROP, "Missing FX trail pool allocation sidecar");
        return nullptr;
    }
    return FX_AllocPool_Generic_FxTrail_(
        &system->firstFreeTrail,
        system->trails,
        &system->activeTrailCount,
        &states->trails);
}

uint16_t __cdecl FX_CalculatePackedLighting(const float *origin)
{
    uint8_t color[4]; // [esp+4h] [ebp-4h] BYREF

    R_GetAverageLightingAtPoint(origin, color);
    return ((color[2] & 0xF8) << 8) | (8 * (color[1] & 0xF8)) | ((color[0] & 0xF8) >> 3);
}
static FxEffect* FX_SpawnEffect_Internal(
    FxSystem* system,
    const FxEffectDef* remoteDef,
    int32_t msecBegin,
    const float* origin,
    const float (*axis)[3],
    int32_t dobjHandle,
    int32_t boneIndex,
    int32_t runnerSortOrder,
    uint16_t owner,
    uint32_t markEntnum)
{
    uint16_t effectHandle; // [esp+1Ch] [ebp-24h]
    int32_t allocIndex = -1; // [esp+20h] [ebp-20h]
    FxEffect* ownerEffect; // [esp+28h] [ebp-18h]
    FxEffect* remoteEffect; // [esp+2Ch] [ebp-14h]
    bool isSpotLightEffect = false; // [esp+3Bh] [ebp-5h]
    bool effectClaimBlocked = false;
    uint32_t elemClass; // [esp+3Ch] [ebp-4h]

    iassert(system);
    iassert(!system->isArchiving);
    iassert(remoteDef);
    iassert(origin);
    iassert(axis);

    if (!FX_ValidateSpotLightEffectDef(remoteDef, &isSpotLightEffect))
    {
        Com_Error(ERR_DROP, "Invalid FX effect definition during spawn");
        return nullptr;
    }
    if (isSpotLightEffect)
    {
        FxSpotLightStateSnapshot spotLightSnapshot{};
        if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot))
        {
            Com_Error(ERR_DROP, "Missing FX spotlight state during spawn");
            return nullptr;
        }
        const std::int32_t spotLightEffectCount =
            spotLightSnapshot.effectCount;
        const std::int32_t spotLightElemCount =
            spotLightSnapshot.elemCount;
        if (spotLightEffectCount < 0 || spotLightEffectCount > 1
            || spotLightElemCount < 0 || spotLightElemCount > 1
            || spotLightElemCount > spotLightEffectCount)
        {
            Com_Error(ERR_DROP, "Invalid FX spotlight counts during spawn");
            return nullptr;
        }
    }
    if (fx_cull_effect_spawn->current.enabled && FX_CullEffectForSpawn(&system->cameraPrev, remoteDef, origin))
        return 0;

    if (!isSpotLightEffect || FX_CanAllocSpotLightEffect(system))
    {
        // No fallible reservation-state transition may occur after the ring
        // slot is claimed; otherwise ordered firstNew publication can stall
        // permanently behind an untracked index.
        FX_PreflightEffectReservation(system);
        // Serialize claims with owner admission and kill preflight. This gives
        // a killer a real quiescent point at firstNew == firstFree while it
        // holds the allocator lock; publication itself remains ordered and
        // lock-free so a claimant can always finish outside this section.
        FX_EnterArchiveAwarePoolCriticalSection();
        for (;;)
        {
            volatile std::int32_t *const killGate =
                FX_GetEffectKillGate(system);
            const std::int32_t killGateState = killGate
                ? Sys_AtomicLoad(killGate)
                : -1;
            if (killGateState == 2
                && !FX_CurrentThreadOwnsEffectRestartGate(system))
            {
                effectClaimBlocked = true;
                break;
            }
            if (killGateState < 0 || killGateState > 2)
                break;
            const std::int32_t firstActiveEffect =
                Sys_AtomicLoad(&system->firstActiveEffect);
            const std::int32_t firstNewEffect =
                Sys_AtomicLoad(&system->firstNewEffect);
            const std::int32_t firstFreeEffect =
                Sys_AtomicLoad(&system->firstFreeEffect);
            const std::int64_t activeEffectCount =
                static_cast<std::int64_t>(firstNewEffect)
                - firstActiveEffect;
            const std::int64_t allocatedEffectCount =
                static_cast<std::int64_t>(firstFreeEffect)
                - firstActiveEffect;
            if (firstActiveEffect < 0
                || firstNewEffect < firstActiveEffect
                || firstFreeEffect < firstNewEffect
                || activeEffectCount > FX_EFFECT_LIMIT
                || allocatedEffectCount > FX_EFFECT_LIMIT)
            {
                break;
            }
            if (allocatedEffectCount == FX_EFFECT_LIMIT)
                break;
            if (firstFreeEffect
                == (std::numeric_limits<std::int32_t>::max)())
                break;
            if (Sys_AtomicCompareExchange(
                    &system->firstFreeEffect,
                    firstFreeEffect + 1,
                    firstFreeEffect)
                == firstFreeEffect)
            {
                allocIndex = firstFreeEffect;
                break;
            }
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
        if (effectClaimBlocked)
            return nullptr;
        if (allocIndex < 0)
        {
            FxRequestGarbageCollection(
                &system->needsGarbageCollection);
            return nullptr;
        }
        FX_BeginEffectReservation(system, allocIndex);
        effectHandle = system->allEffectHandles[allocIndex & 0x3FF];
        remoteEffect = FX_EffectFromHandle(system, effectHandle);
        FX_SetReservedEffect(remoteEffect);
        if (!FX_ResetEffectOwnerAdmissionForReservation(
                system, remoteEffect))
        {
            Com_Error(ERR_DROP, "Missing FX effect owner-admission state");
            return nullptr;
        }
        remoteEffect->owner = effectHandle;
        remoteEffect->firstElemHandle[0] = FX_INVALID_HANDLE;
        remoteEffect->firstElemHandle[1] = FX_INVALID_HANDLE;
        remoteEffect->firstElemHandle[2] = FX_INVALID_HANDLE;
        remoteEffect->firstSortedElemHandle = FX_INVALID_HANDLE;
        remoteEffect->firstTrailHandle = FX_INVALID_HANDLE;
        remoteEffect->def = nullptr;
        Sys_AtomicStore(&remoteEffect->status, FX_STATUS_SELF_OWNED);
        remoteEffect->def = remoteDef;
        Sys_AtomicStore(
            &remoteEffect->status,
            remoteDef->msecLoopingLife != 0 ? 0x00010002 : 0x00000001);
        if (!FX_LockEffect(system, remoteEffect))
        {
            Com_Error(ERR_DROP, "Reserved FX effect lost its initial reference");
            return nullptr;
        }

        if ((remoteDef->flags & 1) != 0)
            remoteEffect->packedLighting = FX_CalculatePackedLighting(origin);
        else
            remoteEffect->packedLighting = 255;

        remoteEffect->msecBegin = msecBegin;
        remoteEffect->msecLastUpdate = remoteEffect->msecBegin;
        remoteEffect->distanceTraveled = 0.0;
        FX_SetEffectRandomSeed(remoteEffect, remoteDef);
        if (isSpotLightEffect
            && !FX_SpawnEffect_AllocSpotLightEffect(system, remoteEffect))
        {
            FX_UnlockEffect(system, remoteEffect);
            Sys_AtomicStore(&remoteEffect->status, 0);
            (void)FX_PublishEffectReservation(true);
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            return nullptr;
        }
        if (isSpotLightEffect)
            FX_RecordReservedEffectSpotLight();
        FX_SpawnEffect_AllocTrails(system, remoteEffect);

        iassert((static_cast<std::uint32_t>(
            Sys_AtomicLoad(&remoteEffect->status))
            & FX_STATUS_OWNED_EFFECTS_MASK) == 0);

        if (owner == 0xFFFF)
        {
            remoteEffect->owner = effectHandle;
            Sys_AtomicStore(
                &remoteEffect->status,
                Sys_AtomicLoad(&remoteEffect->status)
                    | FX_STATUS_SELF_OWNED);
        }
        else
        {
            remoteEffect->owner = owner;
            ownerEffect = FX_EffectFromHandle(system, owner);
            const FxOwnedEffectReferenceAddResult ownerAddResult =
                FX_TryAddOwnedEffectReferenceSerialized(
                    system, ownerEffect);
            if (ownerAddResult
                == FxOwnedEffectReferenceAddResult::Blocked)
            {
                const bool cancelled =
                    FX_PublishEffectReservation(true);
                FX_UnlockEffect(system, remoteEffect);
                if (!cancelled)
                {
                    Com_Error(
                        ERR_DROP,
                        "Invalid FX reservation cancellation state");
                }
                return nullptr;
            }
            if (ownerAddResult
                != FxOwnedEffectReferenceAddResult::Success)
            {
                Com_Error(
                    ERR_DROP,
                    "Invalid FX owner reference state during spawn");
                return nullptr;
            }
            FX_RecordReservedEffectOwner(ownerEffect);
        }
        if (dobjHandle < 0)
            MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1253, 0, "%s", "dobjHandle >= 0");

        remoteEffect->boltAndSortOrder.boneIndex = boneIndex;
        iassert(remoteEffect->boltAndSortOrder.boneIndex == static_cast<uint>(boneIndex));

        remoteEffect->boltAndSortOrder.temporalBits = 0;

        if (markEntnum == ENTITYNUM_NONE)
        {
            iassert((!(boneIndex == ((1 << 11) - 1) && dobjHandle != ((1 << 12) - 1))));
            iassert(boneIndex >= 0);
            remoteEffect->boltAndSortOrder.dobjHandle = dobjHandle;
            iassert(remoteEffect->boltAndSortOrder.dobjHandle == static_cast<uint>(dobjHandle));
            remoteEffect->boltAndSortOrder.temporalBits = FX_GetBoltTemporalBits(system->localClientNum, dobjHandle);
        }
        else
        {
            iassert(boneIndex == FX_BONE_INDEX_NONE);
            iassert(dobjHandle == FX_DOBJ_HANDLE_NONE);
            iassert(markEntnum >= 0 && markEntnum < FX_DOBJ_HANDLE_NONE);
            remoteEffect->boltAndSortOrder.dobjHandle = markEntnum;
            iassert(remoteEffect->boltAndSortOrder.dobjHandle == markEntnum);
        }

        remoteEffect->boltAndSortOrder.sortOrder = runnerSortOrder;
        iassert(remoteEffect->boltAndSortOrder.sortOrder == static_cast<uint>(runnerSortOrder));
        
        remoteEffect->frameAtSpawn.origin[0] = *origin;
        remoteEffect->frameAtSpawn.origin[1] = origin[1];
        remoteEffect->frameAtSpawn.origin[2] = origin[2];
        AxisToQuat(axis, remoteEffect->frameAtSpawn.quat);
        memcpy(&remoteEffect->framePrev, &remoteEffect->frameAtSpawn, sizeof(remoteEffect->framePrev));
        memcpy(&remoteEffect->frameNow, &remoteEffect->frameAtSpawn, sizeof(remoteEffect->frameNow));
        if (!FX_PublishEffectReservation(false))
        {
            Com_Error(ERR_DROP, "Invalid FX effect publication state");
            return nullptr;
        }
        FX_StartNewEffect(system, remoteEffect);
        FX_UnlockEffect(system, remoteEffect);
        return remoteEffect;
    }
    else
    {
        R_WarnOncePerFrame(R_WARN_SPOT_LIGHT_LIMIT);
        return 0;
    }
}

FxEffect* __cdecl FX_SpawnEffect(
    FxSystem* system,
    const FxEffectDef* remoteDef,
    int32_t msecBegin,
    const float* origin,
    const float (*axis)[3],
    int32_t dobjHandle,
    int32_t boneIndex,
    int32_t runnerSortOrder,
    uint16_t owner,
    uint32_t markEntnum)
{
    if (!system)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1255,
            0,
            "%s",
            "system");
        return nullptr;
    }

    // Com_Error performs an engine-level nonlocal unwind, so a C++ scope guard
    // cannot make iterator release reliable. Engine recovery must quiesce and
    // reset or shut down FX; those paths invalidate stale thread ownership.
    FX_BeginIteratingOverEffects_Cooperative(system);
    FxEffect *const effect = FX_SpawnEffect_Internal(
        system,
        remoteDef,
        msecBegin,
        origin,
        axis,
        dobjHandle,
        boneIndex,
        runnerSortOrder,
        owner,
        markEntnum);
    FX_EndIteratingOverEffects_Cooperative(system);
    return effect;
}

void __cdecl FX_AddRefToEffect(FxSystem *system, FxEffect *effect)
{
    if (!FX_TryAddEffectReferenceSerialized(system, effect))
    {
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_system.h",
            372,
            0,
            "%s\n\t(effect->status & FX_STATUS_REF_COUNT_MASK) = %i",
            "FX effect reference count is nonzero and has headroom",
            effect
                ? static_cast<int>(
                    static_cast<std::uint32_t>(
                        Sys_AtomicLoad(&effect->status))
                    & FX_STATUS_REF_COUNT_MASK)
                : 0);
        Com_Error(ERR_DROP, "Invalid FX effect reference increment");
    }
}

char __cdecl FX_CullEffectForSpawn(const FxCamera *camera, const FxEffectDef *effectDef, const float *origin)
{
    const FxElemDef *localDefs; // [esp+18h] [ebp-Ch]
    int32_t elemDefCount; // [esp+1Ch] [ebp-8h]
    int32_t elemDefIndex; // [esp+20h] [ebp-4h]

    elemDefCount = effectDef->elemDefCountOneShot + effectDef->elemDefCountLooping;
    localDefs = effectDef->elemDefs;
    for (elemDefIndex = 0; elemDefIndex < elemDefCount; ++elemDefIndex)
    {
        if (!FX_CullElemForSpawn(camera, &localDefs[elemDefIndex], origin))
            return 0;
    }
    return 1;
}

bool __cdecl FX_CullElemForSpawn(const FxCamera *camera, const FxElemDef *elemDef, const float *origin)
{
    float v4; // [esp+4h] [ebp-18h]
    float diff[3]; // [esp+Ch] [ebp-10h] BYREF
    float dist; // [esp+18h] [ebp-4h]

    if (elemDef->spawnRange.amplitude != 0.0)
    {
        Vec3Sub(camera->origin, origin, diff);
        v4 = Vec3Length(diff);
        dist = v4 - elemDef->spawnRange.base;
        if (dist < 0.0 || elemDef->spawnRange.amplitude < (double)dist)
            return 1;
    }
    return (elemDef->flags & 4) != 0
        && FX_CullSphere(camera, camera->frustumPlaneCount, origin, elemDef->spawnFrustumCullRadius);
}

void __cdecl FX_SetEffectRandomSeed(FxEffect *effect, const FxEffectDef *remoteDef)
{
    if (FX_EffectAffectsGameplay(remoteDef))
        effect->randomSeed = (479 * ((uint32_t)(214013 * effect->msecBegin + 2531011) >> 17)) >> 15; // has to be unsigned
    else
        effect->randomSeed = 479 * rand() / 0x8000;

    // LWSS ADD - bounds check
    iassert(effect->randomSeed < ARRAY_COUNT(fx_randomTable));
    //if (effect->randomSeed >= ARRAY_COUNT(fx_randomTable))
    //{
    //    effect->randomSeed = rand() % ARRAY_COUNT(fx_randomTable);
    //}
    // LWSS END
}

char __cdecl FX_EffectAffectsGameplay(const FxEffectDef *remoteEffectDef)
{
    bool result; // [esp+7h] [ebp-19h]
    const FxElemDef *elemDef; // [esp+8h] [ebp-18h]
    uint32_t elemDefCount; // [esp+Ch] [ebp-14h]
    FxElemVisuals *visArray; // [esp+10h] [ebp-10h]
    uint32_t visIndex; // [esp+18h] [ebp-8h]
    uint32_t elemDefIndex; // [esp+1Ch] [ebp-4h]

    if (!remoteEffectDef)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 867, 0, "%s", "remoteEffectDef");
    elemDefCount = remoteEffectDef->elemDefCountEmission
        + remoteEffectDef->elemDefCountOneShot
        + remoteEffectDef->elemDefCountLooping;
    result = 0;
    for (elemDefIndex = 0; elemDefIndex < elemDefCount; ++elemDefIndex)
    {
        elemDef = &remoteEffectDef->elemDefs[elemDefIndex];
        if ((elemDef->flags & 0x1000) != 0)
            return 1;
        if (elemDef->effectOnDeath.handle && FX_EffectAffectsGameplay(elemDef->effectOnDeath.handle))
            return 1;
        if (elemDef->effectOnImpact.handle && FX_EffectAffectsGameplay(elemDef->effectOnImpact.handle))
            return 1;
        if (elemDef->effectEmitted.handle && FX_EffectAffectsGameplay(elemDef->effectEmitted.handle))
            return 1;
        if (elemDef->elemType == 10)
        {
            if (elemDef->visualCount == 1)
            {
                if (FX_EffectAffectsGameplay(elemDef->visuals.instance.effectDef.handle))
                    return 1;
            }
            else
            {
                visArray = elemDef->visuals.array;
                for (visIndex = 0; visIndex < elemDef->visualCount; ++visIndex)
                {
                    if (FX_EffectAffectsGameplay(visArray[visIndex].effectDef.handle))
                    {
                        result = 1;
                        break;
                    }
                }
            }
        }
    }
    return result;
}

char __cdecl FX_IsSpotLightEffect(FxSystem *system, const FxEffectDef *def)
{
    bool hasSpotLight = false;
    if (!system || !FX_ValidateSpotLightEffectDef(def, &hasSpotLight))
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1020,
            0,
            "%s",
            "system && valid single-spotlight effect definition");
        Com_Error(ERR_DROP, "Invalid FX spotlight effect definition");
        return 0;
    }
    return hasSpotLight;
}

bool __cdecl FX_CanAllocSpotLightEffect(const FxSystem *system)
{
    FxSpotLightStateSnapshot snapshot{};
    return FX_GetSpotLightStateSnapshot(system, &snapshot)
        && snapshot.effectCount == 0 && snapshot.elemCount == 0;
}

char __cdecl FX_SpawnEffect_AllocSpotLightEffect(FxSystem *system, FxEffect *effect)
{
    bool hasSpotLight = false;
    if (!system || !effect
        || !FX_ValidateSpotLightEffectDef(effect->def, &hasSpotLight)
        || !hasSpotLight)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight allocation state");
        return 0;
    }

    const std::uint16_t effectHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    if (effectHandle == FX_INVALID_HANDLE)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight owner pointer");
        return 0;
    }

    bool published = false;
    FX_EnterArchiveAwarePoolCriticalSection();
    if (Sys_AtomicLoad(&system->activeSpotLightEffectCount) == 0
        && Sys_AtomicLoad(&system->activeSpotLightElemCount) == 0)
    {
        system->activeSpotLightEffectHandle = effectHandle;
        system->activeSpotLightElemHandle = FX_INVALID_HANDLE;
        system->activeSpotLightBoltDobj = -1;
        Sys_AtomicStore(&system->activeSpotLightEffectCount, 1);
        published = true;
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    if (!published)
        return 0;
    return 1;
}

FxEffect *__cdecl FX_SpawnOrientedEffect(
    int32_t localClientNum,
    const FxEffectDef *def,
    int32_t msecBegin,
    const float *origin,
    const float (*axis)[3],
    uint32_t markEntnum)
{
    FxSystem *system; // [esp+0h] [ebp-4h]

    if (!fx_enable->current.enabled)
        return 0;
    system = FX_GetSystem(localClientNum);
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1323, 0, "%s", "system");
    return FX_SpawnEffect(system, def, msecBegin, origin, axis, 4095, 2047, 255, 0xFFFFu, markEntnum);
}

void __cdecl FX_AssertAllocatedEffect(int32_t localClientNum, FxEffect *effect)
{
    FxSystem *system; // [esp+0h] [ebp-4h]

    system = FX_GetSystem(localClientNum);
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1335, 0, "%s", "system");
    FX_EffectToHandle(system, effect);
    if ((static_cast<std::uint32_t>(Sys_AtomicLoad(&effect->status))
            & FX_STATUS_REF_COUNT_MASK) == 0)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1337, 0, "%s", "(effect->status & FX_STATUS_REF_COUNT_MASK) != 0");
}

void __cdecl FX_PlayOrientedEffectWithMarkEntity(
    int32_t localClientNum,
    const FxEffectDef *def,
    int32_t startMsec,
    const float *origin,
    const float (*axis)[3],
    uint32_t markEntnum)
{
    FxEffect *effect; // [esp+4h] [ebp-8h]
    FxSystem *system; // [esp+8h] [ebp-4h]

    system = FX_GetSystem(localClientNum);
    effect = FX_SpawnOrientedEffect(localClientNum, def, startMsec, origin, axis, markEntnum);
    if (effect)
        FX_DelRefToEffect(system, effect);
}

void __cdecl FX_PlayOrientedEffect(
    int32_t localClientNum,
    const FxEffectDef *def,
    int32_t startMsec,
    const float *origin,
    const float (*axis)[3])
{
    FxEffect *effect; // [esp+4h] [ebp-8h]
    FxSystem *system; // [esp+8h] [ebp-4h]

    system = FX_GetSystem(localClientNum);
    effect = FX_SpawnOrientedEffect(localClientNum, def, startMsec, origin, axis, 0x3FFu);
    if (effect)
        FX_DelRefToEffect(system, effect);
}

FxEffect *__cdecl FX_SpawnBoltedEffect(
    int32_t localClientNum,
    const FxEffectDef *def,
    int32_t msecBegin,
    uint32_t dobjHandle,
    uint32_t boneIndex)
{
    orientation_t orient; // [esp+0h] [ebp-34h] BYREF
    FxSystem *system; // [esp+30h] [ebp-4h]

    if (!fx_enable->current.enabled)
        return 0;
    if (!FX_GetBoneOrientation(localClientNum, dobjHandle, boneIndex, &orient))
        return 0;
    if (FX_NeedsBoltUpdate(def))
    {
        bcassert(dobjHandle, FX_DOBJ_HANDLE_NONE);
        bcassert(boneIndex, FX_BONE_INDEX_NONE);
    }
    else
    {
        dobjHandle = 4095;
        boneIndex = 2047;
    }
    system = FX_GetSystem(localClientNum);
    return FX_SpawnEffect(system, def, msecBegin, orient.origin, orient.axis, dobjHandle, boneIndex, 255, -1, ENTITYNUM_NONE);
}

char __cdecl FX_NeedsBoltUpdate(const FxEffectDef *def)
{
    int32_t elemDefIndex; // [esp+4h] [ebp-4h]

    if (!def)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1372, 0, "%s", "def");
    for (elemDefIndex = 0; elemDefIndex < def->elemDefCountOneShot + def->elemDefCountLooping; ++elemDefIndex)
    {
        if (def->elemDefs[elemDefIndex].elemType == 3)
            return 1;
        if ((def->elemDefs[elemDefIndex].flags & 0xC0) == 0x80)
            return 1;
    }
    return 0;
}

void __cdecl FX_PlayBoltedEffect(
    int32_t localClientNum,
    const FxEffectDef *def,
    int32_t startMsec,
    uint32_t dobjHandle,
    uint32_t boneIndex)
{
    FxEffect *effect; // [esp+4h] [ebp-8h]
    FxSystem *system; // [esp+8h] [ebp-4h]

    system = FX_GetSystem(localClientNum);
    effect = FX_SpawnBoltedEffect(localClientNum, def, startMsec, dobjHandle, boneIndex);
    if (effect)
        FX_DelRefToEffect(system, effect);
}
static void FX_StopEffect_Internal(FxSystem *system, FxEffect *effect);
static void FX_StopEffectNonRecursive_Internal(
    FxSystem *system,
    FxEffect *effect);
static void FX_KillEffect_Internal(FxSystem *system, FxEffect *effect);
static void FX_RemoveAllEffectElems_Internal(
    FxSystem *system,
    FxEffect *effect);

static void FX_RetriggerEffect_Internal(
    int32_t localClientNum,
    FxEffect* effect,
    int32_t msecBegin)
{
    volatile int32_t* Destination; // [esp+1Ch] [ebp-54h]
    volatile int32_t Comperand; // [esp+20h] [ebp-50h]
    uint16_t lastOldTrailElemHandle[MAX_TRAILS];
    uint16_t lastElemHandle[5]; // [esp+48h] [ebp-28h] BYREF
    bool catchUpNewElems; // [esp+53h] [ebp-1Dh]
    uint16_t firstOldElemHandle[4]; // [esp+54h] [ebp-1Ch] BYREF
    FxSystem* system; // [esp+64h] [ebp-Ch]
    bool hasPendingLoopElems; // [esp+6Bh] [ebp-5h]
    uint32_t elemClass; // [esp+6Ch] [ebp-4h]

    system = FX_GetSystem(localClientNum);
    if (!system || !effect)
    {
        Com_Error(ERR_DROP, "Missing FX retrigger state");
        return;
    }
    if ((static_cast<std::uint32_t>(Sys_AtomicLoad(&effect->status))
            & FX_STATUS_REF_COUNT_MASK) == 0)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1461, 0, "%s", "(effect->status & FX_STATUS_REF_COUNT_MASK) != 0");
    if (!FX_LockEffect(system, effect))
        return;
    if (FX_IsEffectLifecycleBlocked(effect))
    {
        FX_UnlockEffect(system, effect);
        return;
    }
    FX_AddRefToEffect(system, effect);
    if ((static_cast<std::uint32_t>(Sys_AtomicLoad(&effect->status))
            & FX_STATUS_HAS_PENDING_LOOP_ELEMS) != 0)
    {
        FX_SpawnAllFutureLooping(
            system,
            effect,
            0,
            effect->def->elemDefCountLooping,
            &effect->framePrev,
            &effect->frameNow,
            effect->msecBegin,
            effect->msecLastUpdate);
        FX_StopEffect_Internal(system, effect);
    }
    for (elemClass = 0; elemClass < 3; ++elemClass)
        firstOldElemHandle[elemClass] = effect->firstElemHandle[elemClass];
    if (!FX_GetTrailHandleList_Last(
            system,
            effect,
            lastOldTrailElemHandle,
            sizeof(lastOldTrailElemHandle) / sizeof(lastOldTrailElemHandle[0])))
    {
        FX_DelRefToEffect(system, effect);
        FX_UnlockEffect(system, effect);
        Com_Error(ERR_DROP, "Invalid FX trail chain during retrigger");
        return;
    }
    catchUpNewElems = msecBegin < effect->msecLastUpdate;
    if (msecBegin > effect->msecLastUpdate)
    {
        for (elemClass = 0; elemClass < 3; ++elemClass)
            lastElemHandle[elemClass] = -1;
        FX_UpdateEffectPartial(
            system,
            effect,
            effect->msecLastUpdate,
            msecBegin,
            0.0,
            0.0,
            firstOldElemHandle,
            lastElemHandle,
            0,
            lastOldTrailElemHandle);
    }
    effect->msecBegin = msecBegin;
    effect->distanceTraveled = 0.0;
    FX_BeginLooping(
        system,
        effect,
        0,
        effect->def->elemDefCountLooping,
        &effect->frameNow,
        &effect->frameNow,
        msecBegin,
        msecBegin);
    FX_TriggerOneShot(
        system,
        effect,
        effect->def->elemDefCountLooping,
        effect->def->elemDefCountOneShot,
        &effect->frameNow,
        msecBegin);
    hasPendingLoopElems = effect->def->msecLoopingLife != 0;
    if (hasPendingLoopElems)
    {
        Destination = &effect->status;
        do
            Comperand = *Destination;
        while (Sys_AtomicCompareExchange(Destination, Comperand | 0x10000, Comperand) != Comperand);
    }
    if (catchUpNewElems)
        FX_UpdateEffectPartial(
            system,
            effect,
            effect->msecBegin,
            effect->msecLastUpdate,
            0.0,
            0.0,
            effect->firstElemHandle,
            firstOldElemHandle,
            lastOldTrailElemHandle,
            0);
    FX_SortNewElemsInEffect(system, effect);
    if (!hasPendingLoopElems)
        FX_DelRefToEffect(system, effect);
    FX_UnlockEffect(system, effect);
}

void __cdecl FX_RetriggerEffect(
    int32_t localClientNum,
    FxEffect* effect,
    int32_t msecBegin)
{
    FxSystem *const system = FX_GetSystem(localClientNum);
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1474,
            0,
            "%s",
            "system && effect");
        return;
    }
    FX_BeginIteratingOverEffects_Cooperative(system);
    FX_RetriggerEffect_Internal(localClientNum, effect, msecBegin);
    FX_EndIteratingOverEffects_Cooperative(system);
}

bool __cdecl FX_GetTrailHandleList_Last(
    FxSystem *system,
    FxEffect *effect,
    uint16_t *outHandleList,
    const std::size_t outHandleCapacity)
{
    if (!system || !effect || !outHandleList
        || outHandleCapacity == 0 || outHandleCapacity > MAX_TRAILS)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1442,
            0,
            "%s",
            "system && effect && outHandleList && valid output capacity");
        return false;
    }

    std::array<bool, MAX_TRAILS> visitedTrailIndices{};
    std::array<bool, MAX_TRAIL_ELEMS> visitedTrailElemIndices{};
    std::size_t trailIndex = 0;
    std::uint16_t trailHandle = effect->firstTrailHandle;
    while (trailHandle != FX_INVALID_HANDLE)
    {
        FxPool<FxTrail> *const trail =
            FxDecodeHandle<FxPool<FxTrail>, MAX_TRAILS, FxTrail::HANDLE_SCALE>(
                system->trails, trailHandle);
        if (!trail || !FX_IsTrailAllocated(system, &trail->item))
            return false;
        const std::size_t poolIndex =
            static_cast<std::size_t>(trail - system->trails);
        if (visitedTrailIndices[poolIndex] || trailIndex == outHandleCapacity)
            return false;
        visitedTrailIndices[poolIndex] = true;

        if ((trail->item.firstElemHandle == FX_INVALID_HANDLE)
            != (trail->item.lastElemHandle == FX_INVALID_HANDLE))
        {
            return false;
        }
        std::uint16_t terminalTrailElemHandle = FX_INVALID_HANDLE;
        std::uint16_t trailElemHandle = trail->item.firstElemHandle;
        std::size_t traversedTrailElemCount = 0;
        while (trailElemHandle != FX_INVALID_HANDLE)
        {
            if (traversedTrailElemCount++ == MAX_TRAIL_ELEMS)
                return false;
            FxPool<FxTrailElem> *const trailElem = FxDecodeHandle<
                FxPool<FxTrailElem>, MAX_TRAIL_ELEMS,
                FxTrailElem::HANDLE_SCALE>(
                    system->trailElems, trailElemHandle);
            if (!trailElem
                || !FX_IsTrailElemAllocated(system, &trailElem->item))
            {
                return false;
            }
            const std::size_t trailElemIndex =
                static_cast<std::size_t>(trailElem - system->trailElems);
            if (visitedTrailElemIndices[trailElemIndex])
                return false;
            visitedTrailElemIndices[trailElemIndex] = true;
            terminalTrailElemHandle = trailElemHandle;
            trailElemHandle = trailElem->item.nextTrailElemHandle;
        }
        if (terminalTrailElemHandle != trail->item.lastElemHandle)
            return false;

        outHandleList[trailIndex++] = terminalTrailElemHandle;
        trailHandle = trail->item.nextTrailHandle;
    }
    return true;
}

void __cdecl FX_ThroughWithEffect(
    const int32_t localClientNum,
    FxEffect *const effect)
{
    FxSystem *const system = FX_GetSystem(localClientNum);
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1517,
            0,
            "%s",
            "system && effect");
        return;
    }
    if (!system->isInitialized)
        return;
    if ((static_cast<std::uint32_t>(Sys_AtomicLoad(&effect->status))
            & FX_STATUS_REF_COUNT_MASK) == 0)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1521,
            0,
            "%s",
            "(effect->status & FX_STATUS_REF_COUNT_MASK) != 0");
        return;
    }
    FX_KillEffect(system, effect);
    FX_DelRefToEffect(system, effect);
}

static bool FX_GetBoundedActiveEffectRange(
    const FxSystem *const system,
    std::uint32_t *const outFirstActiveEffect,
    std::size_t *const outActiveEffectCount) noexcept
{
    if (!system || !outFirstActiveEffect || !outActiveEffectCount)
        return false;
    const std::int32_t firstActiveEffect =
        Sys_AtomicLoad(&system->firstActiveEffect);
    const std::int32_t firstNewEffect =
        Sys_AtomicLoad(&system->firstNewEffect);
    const std::int32_t firstFreeEffect =
        Sys_AtomicLoad(&system->firstFreeEffect);
    const std::int64_t activeEffectCount =
        static_cast<std::int64_t>(firstNewEffect) - firstActiveEffect;
    const std::int64_t allocatedEffectCount =
        static_cast<std::int64_t>(firstFreeEffect) - firstActiveEffect;
    if (firstActiveEffect < 0 || firstNewEffect < firstActiveEffect
        || firstFreeEffect < firstNewEffect
        || activeEffectCount > allocatedEffectCount
        || allocatedEffectCount > FX_EFFECT_LIMIT)
    {
        return false;
    }
    *outFirstActiveEffect = static_cast<std::uint32_t>(firstActiveEffect);
    *outActiveEffectCount =
        static_cast<std::size_t>(activeEffectCount);
    return true;
}

static bool FX_ValidateEffectAdmissionMarkersExclusive(
    FxSystem *const system) noexcept
{
    if (!system || !FX_CurrentThreadOwnsEffectKillExclusive(system))
        return false;

    bool valid = true;
    FX_EnterArchiveAwarePoolCriticalSection();
    const std::int32_t firstActiveEffect =
        Sys_AtomicLoad(&system->firstActiveEffect);
    const std::int32_t firstNewEffect =
        Sys_AtomicLoad(&system->firstNewEffect);
    const std::int32_t firstFreeEffect =
        Sys_AtomicLoad(&system->firstFreeEffect);
    if (firstActiveEffect < 0
        || firstNewEffect < firstActiveEffect
        || firstNewEffect != firstFreeEffect
        || static_cast<std::int64_t>(firstFreeEffect)
                - firstActiveEffect
            > FX_EFFECT_LIMIT)
    {
        valid = false;
    }
    for (std::int32_t activeIndex = firstActiveEffect;
         valid && activeIndex < firstFreeEffect;
         ++activeIndex)
    {
        FxEffect *const effect = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects,
                system->allEffectHandles[
                    static_cast<std::size_t>(activeIndex)
                    & (FX_EFFECT_LIMIT - 1)]);
        volatile std::int32_t *const admissionState =
            FX_GetEffectOwnerAdmissionState(system, effect);
        if (!effect || !admissionState)
        {
            valid = false;
            break;
        }
        const std::int32_t blocked =
            Sys_AtomicLoad(admissionState);
        const bool markerBlocked =
            FX_IsEffectLifecycleBlocked(effect);
        if ((blocked != 0 && blocked != 1)
            || (blocked == 1) != markerBlocked)
        {
            valid = false;
        }
    }
    Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    return valid;
}

bool __cdecl FX_ValidateEffectKillExclusiveState(
    FxSystem *const system) noexcept
{
    return system
        && FX_CurrentThreadOwnsEffectKillExclusive(system)
        && FX_ValidatePoolAllocationGraphState(system)
        && FX_ValidateEffectAdmissionMarkersExclusive(system);
}

static void FX_StopEffect_Internal(FxSystem *system, FxEffect *effect)
{
    if (!system || !system->effects || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1569,
            0,
            "%s",
            "system && system->effects && effect");
        return;
    }

    const std::uint16_t stoppedEffectHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    std::uint32_t firstActiveEffect = 0;
    std::size_t activeEffectCount = 0;
    if (stoppedEffectHandle == FX_INVALID_HANDLE
        || !FX_GetBoundedActiveEffectRange(
            system, &firstActiveEffect, &activeEffectCount))
    {
        FxRequestGarbageCollection(&system->needsGarbageCollection);
        return;
    }

    constexpr std::size_t effectHandleStride = FxHandleStride<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>();
    std::array<std::uint16_t, FX_EFFECT_LIMIT> activeEffectHandles{};
    std::array<bool, FX_EFFECT_LIMIT> activeEffectSlots{};
    for (std::size_t activeOffset = 0;
         activeOffset < activeEffectCount;
         ++activeOffset)
    {
        const std::uint16_t activeEffectHandle =
            system->allEffectHandles[
                (static_cast<std::size_t>(firstActiveEffect) + activeOffset)
                & (FX_EFFECT_LIMIT - 1)];
        FxEffect *const activeEffect = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects, activeEffectHandle);
        if (!activeEffect)
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            return;
        }
        const std::size_t activeEffectIndex =
            activeEffectHandle / effectHandleStride;
        if (activeEffectSlots[activeEffectIndex])
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            return;
        }
        activeEffectSlots[activeEffectIndex] = true;
        activeEffectHandles[activeOffset] = activeEffectHandle;
    }

    PROF_SCOPED("FX_StopEffect");
    std::array<std::uint16_t, FX_EFFECT_LIMIT> pendingEffectHandles{};
    std::array<bool, FX_EFFECT_LIMIT> discoveredEffects{};
    std::size_t pendingEffectCount = 0;
    pendingEffectHandles[pendingEffectCount++] = stoppedEffectHandle;
    discoveredEffects[stoppedEffectHandle / effectHandleStride] = true;
    while (pendingEffectCount != 0)
    {
        const std::uint16_t currentEffectHandle =
            pendingEffectHandles[--pendingEffectCount];
        FxEffect *const currentEffect = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects, currentEffectHandle);
        if (!currentEffect)
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            continue;
        }
        const std::uint32_t currentStatus = static_cast<std::uint32_t>(
            Sys_AtomicLoad(&currentEffect->status));
        if ((currentStatus & FX_STATUS_REF_COUNT_MASK) == 0)
        {
            if ((currentStatus & FX_STATUS_HAS_PENDING_LOOP_ELEMS) != 0)
                FxRequestGarbageCollection(&system->needsGarbageCollection);
            continue;
        }
        if (!FX_TryAddEffectReferenceSerialized(system, currentEffect))
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            continue;
        }
        FX_StopEffectNonRecursive_Internal(system, currentEffect);

        for (std::size_t activeOffset = 0;
             activeOffset < activeEffectCount;
             ++activeOffset)
        {
            const std::uint16_t childEffectHandle =
                activeEffectHandles[activeOffset];
            if (childEffectHandle == currentEffectHandle)
                continue;
            FxEffect *const childEffect = FxDecodeHandle<
                FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                    system->effects, childEffectHandle);
            if (!childEffect || childEffect->owner != currentEffectHandle)
                continue;
            const std::size_t childEffectIndex =
                childEffectHandle / effectHandleStride;
            if (!discoveredEffects[childEffectIndex])
            {
                if (pendingEffectCount == pendingEffectHandles.size())
                {
                    FxRequestGarbageCollection(
                        &system->needsGarbageCollection);
                    break;
                }
                discoveredEffects[childEffectIndex] = true;
                pendingEffectHandles[pendingEffectCount++] =
                    childEffectHandle;
            }
        }
        FX_DelRefToEffect(system, currentEffect);
    }
}

void __cdecl FX_StopEffect(FxSystem *system, FxEffect *effect)
{
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1569,
            0,
            "%s",
            "system && effect");
        return;
    }
    FX_BeginIteratingOverEffects_Cooperative(system);
    FX_StopEffect_Internal(system, effect);
    FX_EndIteratingOverEffects_Cooperative(system);
}

static void FX_StopEffectNonRecursive_Internal(
    FxSystem *system,
    FxEffect *effect)
{
    volatile int32_t status; // [esp+4h] [ebp-4h]

    if (!effect)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1541, 0, "%s", "effect");
    while (1)
    {
        status = Sys_AtomicLoad(&effect->status);
        if ((status & 0x10000) == 0)
            break;
        if (Sys_AtomicCompareExchange(&effect->status, status & -65537, status) == status)
        {
            FX_DelRefToEffect(system, effect);
            return;
        }
    }
}

void __cdecl FX_StopEffectNonRecursive(
    FxSystem *system,
    FxEffect *effect)
{
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1541,
            0,
            "%s",
            "system && effect");
        return;
    }
    FX_BeginIteratingOverEffects_Cooperative(system);
    FX_StopEffectNonRecursive_Internal(system, effect);
    FX_EndIteratingOverEffects_Cooperative(system);
}

static bool FX_ValidateEffectOwnerForestSnapshot(
    FxSystem *const system,
    const std::array<std::uint16_t, FX_EFFECT_LIMIT> &activeHandles,
    const std::size_t activeEffectCount,
    const std::uint16_t killedEffectHandle,
    std::array<bool, FX_EFFECT_LIMIT> *const outKilledSubtreeSlots,
    std::size_t *const outKilledSubtreeCount) noexcept
{
    if (!system || !outKilledSubtreeSlots || !outKilledSubtreeCount
        || activeEffectCount > FX_EFFECT_LIMIT)
    {
        return false;
    }
    constexpr std::size_t effectHandleStride = FxHandleStride<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>();
    std::array<bool, FX_EFFECT_LIMIT> activeSlots{};
    std::array<std::size_t, FX_EFFECT_LIMIT> inboundLiveChildren{};
    std::array<FxEffect *, FX_EFFECT_LIMIT> activeEffects{};
    outKilledSubtreeSlots->fill(false);
    *outKilledSubtreeCount = 0;

    for (std::size_t offset = 0; offset < activeEffectCount; ++offset)
    {
        const std::uint16_t effectHandle = activeHandles[offset];
        FxEffect *const activeEffect = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects, effectHandle);
        if (!activeEffect)
            return false;
        const std::size_t effectIndex =
            effectHandle / effectHandleStride;
        if (activeSlots[effectIndex])
            return false;
        activeSlots[effectIndex] = true;
        activeEffects[offset] = activeEffect;
    }

    FxEffect *const killedEffect = FxDecodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, killedEffectHandle);
    if (!killedEffect)
        return false;
    const std::size_t killedIndex = static_cast<std::size_t>(
        killedEffect - system->effects);
    if (!activeSlots[killedIndex])
        return false;

    for (std::size_t offset = 0; offset < activeEffectCount; ++offset)
    {
        FxEffect *const activeEffect = activeEffects[offset];
        const std::uint16_t effectHandle = activeHandles[offset];
        const std::uint32_t status = static_cast<std::uint32_t>(
            Sys_AtomicLoad(&activeEffect->status));
        if ((status & 0xC0000000u) != 0)
            return false;
        FxEffect *const owner = FxDecodeHandle<
            FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                system->effects, activeEffect->owner);
        if (!owner)
            return false;
        const std::size_t ownerIndex = static_cast<std::size_t>(
            owner - system->effects);
        if (!activeSlots[ownerIndex])
            return false;
        const bool selfOwned =
            (status & FX_STATUS_SELF_OWNED) != 0;
        if (selfOwned != (activeEffect->owner == effectHandle))
            return false;
        if (!selfOwned && (status & FX_STATUS_REF_COUNT_MASK) != 0)
        {
            if (inboundLiveChildren[ownerIndex]
                == FX_EFFECT_LIMIT - 1)
            {
                return false;
            }
            ++inboundLiveChildren[ownerIndex];
        }
    }

    for (std::size_t offset = 0; offset < activeEffectCount; ++offset)
    {
        FxEffect *const activeEffect = activeEffects[offset];
        const std::size_t effectIndex = static_cast<std::size_t>(
            activeEffect - system->effects);
        const std::uint32_t status = static_cast<std::uint32_t>(
            Sys_AtomicLoad(&activeEffect->status));
        const std::size_t encodedOwnedCount =
            (status & FX_STATUS_OWNED_EFFECTS_MASK)
            >> FX_STATUS_OWNED_EFFECTS_SHIFT;
        if (encodedOwnedCount != inboundLiveChildren[effectIndex])
            return false;

        FxEffect *ownerCursor = activeEffect;
        bool reachedRoot = false;
        bool belongsToKilledSubtree = false;
        for (std::size_t depth = 0; depth < FX_EFFECT_LIMIT; ++depth)
        {
            belongsToKilledSubtree = belongsToKilledSubtree
                || ownerCursor == killedEffect;
            const std::uint32_t ownerStatus =
                static_cast<std::uint32_t>(
                    Sys_AtomicLoad(&ownerCursor->status));
            if ((ownerStatus & FX_STATUS_SELF_OWNED) != 0)
            {
                reachedRoot = true;
                break;
            }
            ownerCursor = FxDecodeHandle<
                FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                    system->effects, ownerCursor->owner);
            if (!ownerCursor
                || !activeSlots[static_cast<std::size_t>(
                    ownerCursor - system->effects)])
            {
                return false;
            }
        }
        if (!reachedRoot)
            return false;
        if (belongsToKilledSubtree
            && (status & FX_STATUS_REF_COUNT_MASK) != 0)
        {
            (*outKilledSubtreeSlots)[effectIndex] = true;
            ++*outKilledSubtreeCount;
        }
    }
    return (*outKilledSubtreeSlots)[killedIndex]
        && *outKilledSubtreeCount != 0;
}

struct FxEffectRemovalValidationState
{
    std::array<bool, MAX_ELEMS> visitedElems{};
    std::array<bool, MAX_TRAILS> visitedTrails{};
    std::array<bool, MAX_TRAIL_ELEMS> visitedTrailElems{};
};

static bool FX_ValidateEffectRemovalGraph(
    FxSystem *const system,
    const FxEffect *const effect,
    FxEffectRemovalValidationState *const validationState) noexcept
{
    if (!system || !effect || !effect->def || !system->elems
        || !system->trails || !system->trailElems || !validationState)
    {
        return false;
    }

    const FxEffectDef *const effectDef = effect->def;
    const std::int64_t elemDefCount64 =
        static_cast<std::int64_t>(effectDef->elemDefCountLooping)
        + effectDef->elemDefCountOneShot
        + effectDef->elemDefCountEmission;
    if (effectDef->elemDefCountLooping < 0
        || effectDef->elemDefCountOneShot < 0
        || effectDef->elemDefCountEmission < 0
        || elemDefCount64 > 256
        || (elemDefCount64 != 0 && !effectDef->elemDefs))
    {
        return false;
    }
    const std::size_t elemDefCount =
        static_cast<std::size_t>(elemDefCount64);

    std::size_t linkedReferenceCount = 0;
    bool foundSortedHead =
        effect->firstSortedElemHandle == FX_INVALID_HANDLE;

    for (std::size_t elemClass = 0; elemClass < 3; ++elemClass)
    {
        std::uint16_t expectedPreviousHandle = FX_INVALID_HANDLE;
        std::uint16_t elemHandle = effect->firstElemHandle[elemClass];
        std::size_t elemCount = 0;
        while (elemHandle != FX_INVALID_HANDLE)
        {
            if (elemCount++ == MAX_ELEMS)
                return false;
            FxPool<FxElem> *const elem = FxDecodeHandle<
                FxPool<FxElem>, MAX_ELEMS, FxElem::HANDLE_SCALE>(
                    system->elems, elemHandle);
            if (!elem || !FX_IsElemAllocated(system, &elem->item))
                return false;
            const std::size_t elemIndex =
                static_cast<std::size_t>(elem - system->elems);
            if (validationState->visitedElems[elemIndex]
                || elem->item.prevElemHandleInEffect
                    != expectedPreviousHandle
                || elem->item.defIndex >= elemDefCount)
            {
                return false;
            }
            validationState->visitedElems[elemIndex] = true;
            ++linkedReferenceCount;
            if (elemClass == 0
                && elemHandle == effect->firstSortedElemHandle)
            {
                foundSortedHead = true;
            }
            expectedPreviousHandle = elemHandle;
            elemHandle = elem->item.nextElemHandleInEffect;
        }
    }
    if (!foundSortedHead)
        return false;

    std::uint16_t trailHandle = effect->firstTrailHandle;
    std::size_t trailCount = 0;
    while (trailHandle != FX_INVALID_HANDLE)
    {
        if (trailCount++ == MAX_TRAILS)
            return false;
        FxPool<FxTrail> *const trail = FxDecodeHandle<
            FxPool<FxTrail>, MAX_TRAILS, FxTrail::HANDLE_SCALE>(
                system->trails, trailHandle);
        if (!trail || !FX_IsTrailAllocated(system, &trail->item))
            return false;
        const std::size_t trailIndex =
            static_cast<std::size_t>(trail - system->trails);
        if (validationState->visitedTrails[trailIndex]
            || trail->item.defIndex >= elemDefCount
            || effectDef->elemDefs[trail->item.defIndex].elemType
                != FX_ELEM_TYPE_TRAIL)
        {
            return false;
        }
        validationState->visitedTrails[trailIndex] = true;

        const bool hasFirst =
            trail->item.firstElemHandle != FX_INVALID_HANDLE;
        const bool hasLast =
            trail->item.lastElemHandle != FX_INVALID_HANDLE;
        if (hasFirst != hasLast)
            return false;
        std::uint16_t trailElemHandle = trail->item.firstElemHandle;
        std::uint16_t terminalHandle = FX_INVALID_HANDLE;
        std::size_t trailElemCount = 0;
        while (trailElemHandle != FX_INVALID_HANDLE)
        {
            if (trailElemCount++ == MAX_TRAIL_ELEMS)
                return false;
            FxPool<FxTrailElem> *const trailElem = FxDecodeHandle<
                FxPool<FxTrailElem>, MAX_TRAIL_ELEMS,
                FxTrailElem::HANDLE_SCALE>(
                    system->trailElems, trailElemHandle);
            if (!trailElem
                || !FX_IsTrailElemAllocated(system, &trailElem->item))
            {
                return false;
            }
            const std::size_t trailElemIndex =
                static_cast<std::size_t>(trailElem - system->trailElems);
            if (validationState->visitedTrailElems[trailElemIndex])
                return false;
            validationState->visitedTrailElems[trailElemIndex] = true;
            ++linkedReferenceCount;
            terminalHandle = trailElemHandle;
            trailElemHandle = trailElem->item.nextTrailElemHandle;
        }
        if (terminalHandle != trail->item.lastElemHandle)
            return false;
        trailHandle = trail->item.nextTrailHandle;
    }

    FxSpotLightStateSnapshot spotLightSnapshot{};
    if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot)
        || spotLightSnapshot.effectCount < 0
        || spotLightSnapshot.effectCount > 1
        || spotLightSnapshot.elemCount < 0
        || spotLightSnapshot.elemCount > spotLightSnapshot.effectCount)
    {
        return false;
    }
    const std::uint16_t effectHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    if (effectHandle == FX_INVALID_HANDLE)
        return false;
    if (spotLightSnapshot.effectCount == 1
        && spotLightSnapshot.effectHandle == effectHandle)
    {
        bool hasSpotLight = false;
        if (!FX_ValidateSpotLightEffectDef(
                effectDef, &hasSpotLight)
            || !hasSpotLight)
        {
            return false;
        }
    }
    if (spotLightSnapshot.elemCount == 1
        && spotLightSnapshot.effectHandle == effectHandle)
    {
        FxPool<FxElem> *const spotLightElem = FxDecodeHandle<
            FxPool<FxElem>, MAX_ELEMS, FxElem::HANDLE_SCALE>(
                system->elems, spotLightSnapshot.elemHandle);
        if (!spotLightElem
            || !FX_IsElemAllocated(system, &spotLightElem->item))
        {
            return false;
        }
        const std::size_t elemIndex =
            static_cast<std::size_t>(spotLightElem - system->elems);
        if (validationState->visitedElems[elemIndex]
            || spotLightElem->item.defIndex >= elemDefCount
            || effectDef->elemDefs[spotLightElem->item.defIndex].elemType
                != FX_ELEM_TYPE_SPOT_LIGHT
            || spotLightElem->item.nextElemHandleInEffect
                != FX_INVALID_HANDLE
            || spotLightElem->item.prevElemHandleInEffect
                != FX_INVALID_HANDLE)
        {
            return false;
        }
        validationState->visitedElems[elemIndex] = true;
        ++linkedReferenceCount;
    }

    const std::uint32_t status = static_cast<std::uint32_t>(
        Sys_AtomicLoad(&effect->status));
    const std::size_t referenceCount =
        status & FX_STATUS_REF_COUNT_MASK;
    const std::size_t ownedEffectCount =
        (status & FX_STATUS_OWNED_EFFECTS_MASK)
        >> FX_STATUS_OWNED_EFFECTS_SHIFT;
    const std::size_t pendingLoopReference =
        (status & FX_STATUS_HAS_PENDING_LOOP_ELEMS) != 0 ? 1u : 0u;
    // Each caller has already retained the effect for preflight. The original
    // graph must still fund every linked element, live owned child, and
    // pending-loop reference without borrowing that temporary retain.
    const std::size_t requiredReferenceCount = linkedReferenceCount
        + ownedEffectCount + pendingLoopReference + 1u;
    return requiredReferenceCount <= FX_STATUS_REF_COUNT_MASK
        && referenceCount >= requiredReferenceCount;
}

static bool FX_WaitForEffectPublicationBarrier(
    const FxSystem *const system,
    const std::int32_t publicationBarrier) noexcept
{
    if (!system || publicationBarrier < 0)
        return false;

    constexpr std::size_t maxPublicationWaits = 1u << 20;
    for (std::size_t waitCount = 0;
         waitCount < maxPublicationWaits;
         ++waitCount)
    {
        const std::int32_t firstActiveEffect =
            Sys_AtomicLoad(&system->firstActiveEffect);
        const std::int32_t firstNewEffect =
            Sys_AtomicLoad(&system->firstNewEffect);
        const std::int32_t firstFreeEffect =
            Sys_AtomicLoad(&system->firstFreeEffect);
        if (firstActiveEffect < 0
            || firstNewEffect < firstActiveEffect
            || firstFreeEffect < firstNewEffect
            || publicationBarrier > firstFreeEffect
            || static_cast<std::int64_t>(firstFreeEffect)
                    - firstActiveEffect
                > FX_EFFECT_LIMIT)
        {
            return false;
        }
        if (firstNewEffect >= publicationBarrier)
            return true;
        std::this_thread::yield();
    }
    return false;
}

static void FX_KillEffect_Internal(FxSystem *system, FxEffect *effect)
{
    if (!system || !system->effects || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1653,
            0,
            "%s",
            "system && system->effects && effect");
        return;
    }
    const std::uint32_t initialStatus = static_cast<std::uint32_t>(
        Sys_AtomicLoad(&effect->status));
    if ((initialStatus & FX_STATUS_REF_COUNT_MASK) == 0
        || (initialStatus & FX_STATUS_IS_LOCKED) == 0
        || !FX_CurrentThreadOwnsEffectLock(system, effect)
        || !FX_CurrentThreadOwnsEffectKillExclusive(system))
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1654,
            0,
            "%s",
            "killed FX effect is referenced and owned by this kill iterator/locker");
        FxRequestGarbageCollection(&system->needsGarbageCollection);
        return;
    }

    const std::uint16_t killedEffectHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    std::int32_t publicationBarrier = 0;
    if (killedEffectHandle == FX_INVALID_HANDLE)
    {
        Com_Error(ERR_DROP, "Invalid FX effect handle during kill");
        return;
    }
    if (!FX_BeginEffectKillAdmission(
            system, effect, &publicationBarrier))
    {
        volatile std::int32_t *const admissionState =
            FX_GetEffectOwnerAdmissionState(system, effect);
        if (admissionState
            && Sys_AtomicLoad(admissionState) == 1
            && FX_IsEffectLifecycleBlocked(effect))
        {
            return;
        }
        Com_Error(ERR_DROP, "Invalid FX kill admission state");
        return;
    }

    std::array<std::uint16_t, FX_EFFECT_LIMIT> activeEffectHandles{};
    std::array<bool, FX_EFFECT_LIMIT> killedSubtreeSlots{};
    std::array<FxEffect *, FX_EFFECT_LIMIT> retainedChildEffects{};
    std::size_t activeEffectCount = 0;
    std::size_t retainedChildCount = 0;
    std::size_t lockedChildCount = 0;
    bool targetRetained = true;

    const auto abandonPreflight = [&]() noexcept
    {
        while (retainedChildCount != 0)
        {
            const std::size_t childIndex = --retainedChildCount;
            FxEffect *const childEffect =
                retainedChildEffects[childIndex];
            (void)FX_ReleaseEffectKillRetain(system, childEffect);
            if (childIndex < lockedChildCount)
                FX_UnlockEffect(system, childEffect);
        }
        lockedChildCount = 0;
        if (targetRetained)
            (void)FX_ReleaseEffectKillRetain(system, effect);
        FX_EndEffectKillAdmission(false);
        FxRequestGarbageCollection(&system->needsGarbageCollection);
    };

    bool snapshotReady = FX_WaitForEffectPublicationBarrier(
        system, publicationBarrier);
    if (snapshotReady)
    {
        FX_EnterArchiveAwarePoolCriticalSection();
        const std::int32_t firstActiveEffect =
            Sys_AtomicLoad(&system->firstActiveEffect);
        const std::int32_t firstNewEffect =
            Sys_AtomicLoad(&system->firstNewEffect);
        const std::int32_t firstFreeEffect =
            Sys_AtomicLoad(&system->firstFreeEffect);
        const std::int64_t activeEffectCount64 =
            static_cast<std::int64_t>(firstNewEffect)
            - firstActiveEffect;
        if (firstActiveEffect < 0
            || firstNewEffect < firstActiveEffect
            || firstFreeEffect < firstNewEffect
            || firstNewEffect != firstFreeEffect
            || activeEffectCount64 > FX_EFFECT_LIMIT)
        {
            snapshotReady = false;
        }
        else
        {
            activeEffectCount =
                static_cast<std::size_t>(activeEffectCount64);
            for (std::size_t activeOffset = 0;
                 activeOffset < activeEffectCount;
                 ++activeOffset)
            {
                activeEffectHandles[activeOffset] =
                    system->allEffectHandles[
                        (static_cast<std::size_t>(firstActiveEffect)
                            + activeOffset)
                        & (FX_EFFECT_LIMIT - 1)];
            }

            std::size_t killedSubtreeCount = 0;
            snapshotReady = FX_ValidateEffectOwnerForestSnapshot(
                system,
                activeEffectHandles,
                activeEffectCount,
                killedEffectHandle,
                &killedSubtreeSlots,
                &killedSubtreeCount);
            constexpr std::size_t effectHandleStride = FxHandleStride<
                FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>();
            for (std::size_t activeOffset = 0;
                 snapshotReady && activeOffset < activeEffectCount;
                 ++activeOffset)
            {
                const std::uint16_t subtreeHandle =
                    activeEffectHandles[activeOffset];
                const std::size_t subtreeIndex =
                    subtreeHandle / effectHandleStride;
                if (!killedSubtreeSlots[subtreeIndex])
                    continue;
                FxEffect *const subtreeEffect = FxDecodeHandle<
                    FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                        system->effects, subtreeHandle);
                snapshotReady = subtreeEffect
                    && FX_ClaimEffectOwnerAdmissionForKillLocked(
                        system, subtreeEffect);
            }

            for (std::size_t activeOffset = 0;
                 snapshotReady && activeOffset < activeEffectCount;
                 ++activeOffset)
            {
                const std::uint16_t subtreeHandle =
                    activeEffectHandles[activeOffset];
                const std::size_t subtreeIndex =
                    subtreeHandle / effectHandleStride;
                if (subtreeHandle == killedEffectHandle
                    || !killedSubtreeSlots[subtreeIndex])
                {
                    continue;
                }
                FxEffect *const subtreeEffect = FxDecodeHandle<
                    FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
                        system->effects, subtreeHandle);
                const bool referenceAdded = subtreeEffect
                    && retainedChildCount
                        < retainedChildEffects.size()
                    && FX_TryAdjustEffectReferenceCount(
                        subtreeEffect, true);
                if (!referenceAdded
                    || !FX_RecordEffectKillRetain(
                        system, subtreeEffect))
                {
                    if (referenceAdded)
                    {
                        (void)FX_TryAdjustEffectReferenceCount(
                            subtreeEffect, false);
                    }
                    snapshotReady = false;
                    break;
                }
                retainedChildEffects[retainedChildCount++] =
                    subtreeEffect;
            }
            snapshotReady = snapshotReady
                && retainedChildCount + 1 == killedSubtreeCount;
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    }

    if (!snapshotReady)
    {
        abandonPreflight();
        Com_Error(
            ERR_DROP,
            "FX kill could not obtain a valid quiescent ownership snapshot");
        return;
    }

    for (; lockedChildCount < retainedChildCount; ++lockedChildCount)
    {
        if (!FX_LockEffect(
                system, retainedChildEffects[lockedChildCount]))
        {
            abandonPreflight();
            Com_Error(
                ERR_DROP,
                "FX kill could not lock its retained ownership subtree");
            return;
        }
    }

    FxEffectRemovalValidationState removalValidation{};
    if (!FX_ValidateEffectRemovalGraph(
            system, effect, &removalValidation))
    {
        abandonPreflight();
        Com_Error(ERR_DROP, "Invalid FX removal graph during kill");
        return;
    }
    for (std::size_t childIndex = 0;
         childIndex < retainedChildCount;
         ++childIndex)
    {
        if (!FX_ValidateEffectRemovalGraph(
                system,
                retainedChildEffects[childIndex],
                &removalValidation))
        {
            abandonPreflight();
            Com_Error(ERR_DROP, "Invalid owned FX removal graph during kill");
            return;
        }
    }

    // The entire live ownership subtree is retained, admission-blocked,
    // locked, and locally validated before the first destructive unlink.
    FX_MarkEffectOwnerAdmissionPermanentlyBlocked(effect);
    for (std::size_t childIndex = 0;
         childIndex < retainedChildCount;
         ++childIndex)
    {
        FX_MarkEffectOwnerAdmissionPermanentlyBlocked(
            retainedChildEffects[childIndex]);
    }
    FX_MarkEffectKillMutationStarted();
    FX_RemoveAllEffectElems_Internal(system, effect);
    for (std::size_t childIndex = 0;
         childIndex < retainedChildCount;
         ++childIndex)
    {
        FxEffect *const childEffect = retainedChildEffects[childIndex];
        FX_RemoveAllEffectElems_Internal(system, childEffect);
    }
    while (lockedChildCount != 0)
    {
        FxEffect *const lockedChild =
            retainedChildEffects[--lockedChildCount];
        (void)FX_ReleaseEffectKillRetain(system, lockedChild);
        FX_UnlockEffect(system, lockedChild);
    }
    retainedChildCount = 0;
    (void)FX_ReleaseEffectKillRetain(system, effect);
    targetRetained = false;
    FX_EndEffectKillAdmission(true);
}

void __cdecl FX_KillEffect(FxSystem *system, FxEffect *effect)
{
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1653,
            0,
            "%s",
            "system && effect");
        return;
    }
    const bool alreadyExclusive =
        FX_CurrentThreadOwnsEffectKillExclusive(system);
    if (!alreadyExclusive && !FX_BeginEffectKillExclusive(system))
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1653,
            0,
            "%s",
            "FX kill begins without cooperative or nested exclusive ownership");
        FxRequestGarbageCollection(&system->needsGarbageCollection);
        return;
    }
    if (!FX_ValidatePoolAllocationGraphState(system)
        || !FX_ValidateEffectAdmissionMarkersExclusive(system))
    {
        Com_Error(ERR_DROP, "Invalid FX pool graph before effect kill");
        return;
    }
    if (!FX_IsEffectLifecycleBlocked(effect)
        && FX_LockEffect(system, effect))
    {
        FX_KillEffect_Internal(system, effect);
        FX_UnlockEffect(system, effect);
    }
    if (!FX_ValidatePoolAllocationGraphState(system)
        || !FX_ValidateEffectAdmissionMarkersExclusive(system))
    {
        Com_Error(ERR_DROP, "Invalid FX pool graph after effect kill");
        return;
    }
    if (!alreadyExclusive && !FX_EndEffectKillExclusive(system))
    {
        Com_Error(ERR_DROP, "Invalid FX exclusive iterator after effect kill");
        return;
    }
    if (!alreadyExclusive
        && FxGarbageCollectionRequested(
            &system->needsGarbageCollection))
        FX_RunGarbageCollection(system);
}

static void FX_RemoveAllEffectElems_Internal(
    FxSystem *system,
    FxEffect *effect)
{
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1618,
            0,
            "%s",
            "system && effect");
        return;
    }

    FxSpotLightStateSnapshot spotLightSnapshot{};
    if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot))
    {
        Com_Error(ERR_DROP, "Missing FX spotlight cleanup state");
        return;
    }
    const std::int32_t spotLightEffectCount =
        spotLightSnapshot.effectCount;
    const std::int32_t spotLightElemCount =
        spotLightSnapshot.elemCount;
    if (spotLightEffectCount < 0 || spotLightEffectCount > 1
        || spotLightElemCount < 0 || spotLightElemCount > 1
        || spotLightElemCount > spotLightEffectCount)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight counts during effect cleanup");
        return;
    }

    // Kill preflight already retained and locked the complete ownership
    // subtree, so the non-recursive stop consumes only this effect's pending
    // loop reference without introducing another fallible temporary retain.
    FX_StopEffectNonRecursive_Internal(system, effect);
    std::array<bool, MAX_ELEMS> releasedElemIndices{};
    std::array<bool, MAX_TRAIL_ELEMS> releasedTrailElemIndices{};
    std::array<bool, MAX_TRAILS> traversedTrailIndices{};
    for (std::uint32_t elemClass = 0; elemClass < 3; ++elemClass)
    {
        while (effect->firstElemHandle[elemClass] != FX_INVALID_HANDLE)
        {
            const std::uint16_t previousHandle =
                effect->firstElemHandle[elemClass];
            FxPool<FxElem> *const elem =
                FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                    system->elems, previousHandle);
            if (!elem || !FX_IsElemAllocated(system, &elem->item))
            {
                FxRequestGarbageCollection(&system->needsGarbageCollection);
                break;
            }
            const std::size_t elemIndex =
                static_cast<std::size_t>(elem - system->elems);
            if (releasedElemIndices[elemIndex])
            {
                FxRequestGarbageCollection(&system->needsGarbageCollection);
                break;
            }
            releasedElemIndices[elemIndex] = true;
            FX_FreeElem(system, previousHandle, effect, elemClass);
            if (effect->firstElemHandle[elemClass] == previousHandle)
            {
                FxRequestGarbageCollection(&system->needsGarbageCollection);
                break;
            }
        }
    }

    std::uint16_t trailHandle = effect->firstTrailHandle;
    std::size_t traversedTrailCount = 0;
    while (trailHandle != FX_INVALID_HANDLE)
    {
        if (traversedTrailCount++ >= MAX_TRAILS)
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            break;
        }
        FxPool<FxTrail> *const trail =
            FX_PoolFromHandle_Generic<FxTrail, MAX_TRAILS>(
                system->trails, trailHandle);
        if (!trail || !FX_IsTrailAllocated(system, &trail->item))
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            break;
        }
        const std::size_t trailIndex =
            static_cast<std::size_t>(trail - system->trails);
        if (traversedTrailIndices[trailIndex])
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            break;
        }
        traversedTrailIndices[trailIndex] = true;
        const std::uint16_t nextTrailHandle = trail->item.nextTrailHandle;
        bool trailComplete = true;
        while (trail->item.firstElemHandle != FX_INVALID_HANDLE)
        {
            const std::uint16_t previousHandle =
                trail->item.firstElemHandle;
            FxPool<FxTrailElem> *const trailElem =
                FX_PoolFromHandle_Generic<FxTrailElem, MAX_TRAIL_ELEMS>(
                    system->trailElems, previousHandle);
            if (!trailElem
                || !FX_IsTrailElemAllocated(system, &trailElem->item))
            {
                FxRequestGarbageCollection(&system->needsGarbageCollection);
                trailComplete = false;
                break;
            }
            const std::size_t trailElemIndex =
                static_cast<std::size_t>(trailElem - system->trailElems);
            if (releasedTrailElemIndices[trailElemIndex])
            {
                FxRequestGarbageCollection(&system->needsGarbageCollection);
                trailComplete = false;
                break;
            }
            releasedTrailElemIndices[trailElemIndex] = true;
            FX_FreeTrailElem(
                system,
                previousHandle,
                effect,
                &trail->item,
                &trail->item);
            if (trail->item.firstElemHandle == previousHandle)
            {
                FxRequestGarbageCollection(&system->needsGarbageCollection);
                trailComplete = false;
                break;
            }
        }
        if (!trailComplete || nextTrailHandle == trailHandle)
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            break;
        }
        trailHandle = nextTrailHandle;
    }

    const int effectHandle = FX_EffectToHandle(system, effect);
    if (effectHandle >= 0
        && spotLightSnapshot.elemCount > 0
        && spotLightSnapshot.effectHandle
            == static_cast<std::uint16_t>(effectHandle))
    {
        FX_FreeSpotLightElem(system, spotLightSnapshot.elemHandle, effect);
    }
}

void __cdecl FX_KillEffectDef(int32_t localClientNum, const FxEffectDef *def)
{
    FxSystem *const system = FX_GetSystem(localClientNum);
    if (!system || !def)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1710,
            0,
            "%s",
            "system && def");
        return;
    }
    if (!FX_BeginEffectKillExclusive(system))
    {
        FxRequestGarbageCollection(&system->needsGarbageCollection);
        return;
    }
    if (!FX_ValidatePoolAllocationGraphState(system)
        || !FX_ValidateEffectAdmissionMarkersExclusive(system))
    {
        Com_Error(ERR_DROP, "Invalid FX pool graph before definition kill");
        return;
    }
    std::uint32_t firstActiveEffect = 0;
    std::size_t activeEffectCount = 0;
    if (!FX_GetBoundedActiveEffectRange(
            system, &firstActiveEffect, &activeEffectCount))
    {
        Com_Error(ERR_DROP, "Invalid FX effect ring during definition kill");
        return;
    }
    for (std::size_t activeOffset = 0;
         activeOffset < activeEffectCount;
         ++activeOffset)
    {
        const std::uint32_t activeIndex =
            firstActiveEffect + static_cast<std::uint32_t>(activeOffset);
        FxEffect *const effect = FX_EffectFromHandle(
            system,
            system->allEffectHandles[
                activeIndex & (FX_EFFECT_LIMIT - 1)]);
        if (effect->def == def
            && (static_cast<std::uint32_t>(
                    Sys_AtomicLoad(&effect->status))
                & FX_STATUS_REF_COUNT_MASK) != 0
            && !FX_IsEffectLifecycleBlocked(effect))
        {
            if (FX_LockEffect(system, effect))
            {
                FX_KillEffect_Internal(system, effect);
                FX_UnlockEffect(system, effect);
            }
        }
    }
    if (!FX_ValidatePoolAllocationGraphState(system)
        || !FX_ValidateEffectAdmissionMarkersExclusive(system))
    {
        Com_Error(
            ERR_DROP,
            "Invalid FX pool graph after definition kill");
        return;
    }
    if (!FX_EndEffectKillExclusive(system))
    {
        Com_Error(
            ERR_DROP,
            "Invalid FX exclusive iterator after definition kill");
        return;
    }
    if (FxGarbageCollectionRequested(&system->needsGarbageCollection))
        FX_RunGarbageCollection(system);
}

void __cdecl FX_KillAllEffects(int32_t localClientNum)
{
    FxSystem *const system = FX_GetSystem(localClientNum);
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 1732, 0, "%s", "system");
    if (system && system->isInitialized)
    {
        if (!FX_BeginEffectKillExclusive(system))
        {
            FxRequestGarbageCollection(&system->needsGarbageCollection);
            return;
        }
        if (!FX_ValidatePoolAllocationGraphState(system)
            || !FX_ValidateEffectAdmissionMarkersExclusive(system))
        {
            Com_Error(ERR_DROP, "Invalid FX pool graph before kill-all");
            return;
        }
        std::uint32_t firstActiveEffect = 0;
        std::size_t activeEffectCount = 0;
        if (!FX_GetBoundedActiveEffectRange(
                system, &firstActiveEffect, &activeEffectCount))
        {
            Com_Error(ERR_DROP, "Invalid FX effect ring during kill-all");
            return;
        }
        for (std::size_t activeOffset = 0;
             activeOffset < activeEffectCount;
             ++activeOffset)
        {
            const std::uint32_t activeIndex =
                firstActiveEffect
                + static_cast<std::uint32_t>(activeOffset);
            FxEffect *const effect = FX_EffectFromHandle(
                system,
                system->allEffectHandles[
                    activeIndex & (FX_EFFECT_LIMIT - 1)]);
            if ((static_cast<std::uint32_t>(
                    Sys_AtomicLoad(&effect->status))
                    & FX_STATUS_REF_COUNT_MASK) != 0
                && !FX_IsEffectLifecycleBlocked(effect))
            {
                if (FX_LockEffect(system, effect))
                {
                    FX_KillEffect_Internal(system, effect);
                    FX_UnlockEffect(system, effect);
                }
            }
        }
        if (!FX_ValidatePoolAllocationGraphState(system)
            || !FX_ValidateEffectAdmissionMarkersExclusive(system))
        {
            Com_Error(ERR_DROP, "Invalid FX pool graph after kill-all");
            return;
        }
        if (!FX_EndEffectKillExclusive(system))
        {
            Com_Error(
                ERR_DROP,
                "Invalid FX exclusive iterator after kill-all");
            return;
        }
        if (FxGarbageCollectionRequested(
                &system->needsGarbageCollection))
        {
            FX_RunGarbageCollection(system);
        }
    }
}

void __cdecl FX_SpawnTrailElem_NoCull(
    FxSystem *system,
    FxEffect *effect,
    FxTrail *trail,
    const FxSpatialFrame *effectFrameWhenPlayed,
    int32_t msecWhenPlayed,
    float distanceWhenPlayed)
{
    uint16_t lastElemHandle; // [esp+12h] [ebp-4Ah]
    bool v7; // [esp+1Bh] [ebp-41h]
    int32_t msecBegin; // [esp+20h] [ebp-3Ch]
    const FxElemDef *elemDef; // [esp+2Ch] [ebp-30h]
    uint32_t randomSeed; // [esp+30h] [ebp-2Ch]
    FxPool<FxTrailElem> *remoteTrailElem; // [esp+38h] [ebp-24h]
    float basis[2][3]; // [esp+3Ch] [ebp-20h] BYREF
    uint16_t trailElemHandle; // [esp+54h] [ebp-8h]
    FxTrailElem *lastTrailElemInEffect; // [esp+58h] [ebp-4h]

    if (!system || !effect || !effect->def || !trail
        || !effectFrameWhenPlayed)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1916,
            0,
            "%s",
            "system && effect && effect->def && trail && effectFrameWhenPlayed");
        return;
    }
    if (FX_IsEffectLifecycleBlocked(effect))
        return;
    if (!FX_IsTrailAllocated(system, trail))
    {
        Com_Error(ERR_DROP, "FX trail spawn owner is not allocated");
        return;
    }
    elemDef = FX_GetValidatedRuntimeElemDef(effect, trail->defIndex);
    if (!elemDef)
        return;
    if (elemDef->elemType != 3)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1923,
            0,
            "%s\n\t(elemDef->elemType) = %i",
            "(elemDef->elemType == FX_ELEM_TYPE_TRAIL)",
            elemDef->elemType);
        return;
    }
    msecBegin = elemDef->spawnDelayMsec.base + msecWhenPlayed;
    if (elemDef->spawnDelayMsec.amplitude)
        msecBegin += ((elemDef->spawnDelayMsec.amplitude + 1)
            * LOWORD(fx_randomTable[(msecBegin + (uint32_t)effect->randomSeed + 296 * trail->sequence) % 0x1DF
                + 18])) >> 16;
    randomSeed = (296 * trail->sequence + msecBegin + (uint32_t)effect->randomSeed) % 0x1DF;
    if (elemDef->effectOnImpact.handle)
    {
        v7 = 1;
    }
    else if (elemDef->effectOnDeath.handle)
    {
        v7 = 1;
    }
    else
    {
        v7 = elemDef->effectEmitted.handle != 0;
    }
    if (v7
        || msecBegin
        + (((elemDef->lifeSpanMsec.amplitude + 1) * LOWORD(fx_randomTable[randomSeed + 17])) >> 16)
        + elemDef->lifeSpanMsec.base > system->msecNow)
    {
        lastTrailElemInEffect = nullptr;
        if ((trail->firstElemHandle == FX_INVALID_HANDLE)
            != (trail->lastElemHandle == FX_INVALID_HANDLE))
        {
            Com_Error(ERR_DROP, "Invalid FX trail endpoint handles");
            return;
        }
        if (trail->lastElemHandle != FX_INVALID_HANDLE)
        {
            lastElemHandle = trail->lastElemHandle;
            FxPool<FxTrailElem> *const lastTrailElem =
                FX_PoolFromHandle_Generic<FxTrailElem, MAX_TRAIL_ELEMS>(
                    system->trailElems, lastElemHandle);
            if (!FX_IsTrailElemAllocated(system, &lastTrailElem->item)
                || lastTrailElem->item.nextTrailElemHandle != FX_INVALID_HANDLE)
            {
                Com_Error(ERR_DROP, "Invalid FX trail tail link");
                return;
            }
            lastTrailElemInEffect = &lastTrailElem->item;
        }

        remoteTrailElem = FX_AllocTrailElem(system);
        if (remoteTrailElem)
        {
            FX_AddRefToEffect(system, effect);
            FX_GetOriginForTrailElem(
                effect,
                elemDef,
                effectFrameWhenPlayed,
                randomSeed,
                remoteTrailElem->item.origin,
                basis[0],
                basis[1]);
            trailElemHandle = FX_PoolToHandle_Generic<
                FxTrailElem, MAX_TRAIL_ELEMS>(
                    system->trailElems, &remoteTrailElem->item);
            remoteTrailElem->item.nextTrailElemHandle = FX_INVALID_HANDLE;
            remoteTrailElem->item.msecBegin = msecBegin;
            remoteTrailElem->item.spawnDist = distanceWhenPlayed;
            remoteTrailElem->item.baseVelZ = 0;
            remoteTrailElem->item.sequence = trail->sequence++;
            FX_TrailElem_CompressBasis(basis, remoteTrailElem->item.basis);

            if (lastTrailElemInEffect)
                lastTrailElemInEffect->nextTrailElemHandle = trailElemHandle;
            else
                trail->firstElemHandle = trailElemHandle;
            trail->lastElemHandle = trailElemHandle;
        }
    }
}

FxPool<FxTrailElem> *__cdecl FX_AllocTrailElem(FxSystem *system)
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
    {
        Com_Error(ERR_DROP, "Missing FX trail-element allocation sidecar");
        return nullptr;
    }
    return FX_AllocPool_Generic_FxTrailElem_(
        &system->firstFreeTrailElem,
        system->trailElems,
        &system->activeTrailElemCount,
        &states->trailElems);
}

void __cdecl FX_SpawnTrailElem_Cull(
    FxSystem *system,
    FxEffect *effect,
    FxTrail *trail,
    const FxSpatialFrame *effectFrameWhenPlayed,
    int32_t msecWhenPlayed,
    float distanceWhenPlayed)
{
    const FxElemDef *elemDef; // [esp+28h] [ebp-4h]

    if (!system || !effect || !effect->def || !trail
        || !effectFrameWhenPlayed)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1981,
            0,
            "%s",
            "system && effect && effect->def && trail && effectFrameWhenPlayed");
        return;
    }
    if (FX_IsEffectLifecycleBlocked(effect))
        return;
    if (!FX_IsTrailAllocated(system, trail))
    {
        Com_Error(ERR_DROP, "FX culled-trail spawn owner is not allocated");
        return;
    }
    elemDef = FX_GetValidatedRuntimeElemDef(effect, trail->defIndex);
    if (!elemDef)
        return;
    if (elemDef->elemType != 3)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1988,
            0,
            "%s\n\t(elemDef->elemType) = %i",
            "(elemDef->elemType == FX_ELEM_TYPE_TRAIL)",
            elemDef->elemType);
        return;
    }
    if (FX_CullTrailElem(&system->cameraPrev, elemDef, effectFrameWhenPlayed->origin, trail->sequence))
        ++trail->sequence;
    else
        FX_SpawnTrailElem_NoCull(system, effect, trail, effectFrameWhenPlayed, msecWhenPlayed, distanceWhenPlayed);
}

bool __cdecl FX_CullTrailElem(
    const FxCamera *camera,
    const FxElemDef *elemDef,
    const float *origin,
    uint8_t sequence)
{
    float diff[3]; // [esp+0h] [ebp-1Ch] BYREF
    float cutoffMultiple; // [esp+Ch] [ebp-10h]
    float cutoffDist; // [esp+10h] [ebp-Ch]
    float distSq; // [esp+14h] [ebp-8h]
    float baseCutoffDist; // [esp+18h] [ebp-4h]

    baseCutoffDist = elemDef->spawnRange.base + elemDef->spawnRange.amplitude;
    if (baseCutoffDist == 0.0)
        return 0;
    if (!sequence)
        return 0;
    cutoffMultiple = 1.0;
    while ((sequence & 1) == 0)
    {
        cutoffMultiple = cutoffMultiple + 1.0;
        sequence >>= 1;
    }
    cutoffDist = baseCutoffDist * cutoffMultiple;
    Vec3Sub(camera->origin, origin, diff);
    distSq = Vec3LengthSq(diff);
    return distSq > cutoffDist * cutoffDist;
}

void __cdecl FX_SpawnSpotLightElem(FxSystem *system, FxElem *elem)
{
    if (!system || !elem)
    {
        Com_Error(ERR_DROP, "Missing FX spotlight spawn state");
        return;
    }
    if (!FX_IsElemAllocated(system, elem))
    {
        Com_Error(ERR_DROP, "FX spotlight element is not allocated");
        return;
    }
    FxSpotLightStateSnapshot spotLightSnapshot{};
    if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot))
    {
        Com_Error(ERR_DROP, "Missing FX spotlight publication state");
        return;
    }
    if (spotLightSnapshot.effectCount != 1
        || spotLightSnapshot.elemCount != 0)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            2038,
            0,
            "%s",
            "system->activeSpotLightEffectCount == 1 && system->activeSpotLightElemCount == 0");
        Com_Error(ERR_DROP, "Invalid FX spotlight publication counts");
        return;
    }

    FxEffect *const effect = FxDecodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, spotLightSnapshot.effectHandle);
    if (!effect
        || (static_cast<std::uint32_t>(Sys_AtomicLoad(&effect->status))
                & FX_STATUS_REF_COUNT_MASK) == 0
        || FX_IsEffectLifecycleBlocked(effect))
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight effect owner");
        return;
    }
    bool hasSpotLight = false;
    if (!FX_ValidateSpotLightEffectDef(effect->def, &hasSpotLight)
        || !hasSpotLight)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight owner definition");
        return;
    }
    const FxElemDef *const elemDef =
        FX_GetValidatedRuntimeElemDef(effect, elem->defIndex);
    if (!elemDef || elemDef->elemType != FX_ELEM_TYPE_SPOT_LIGHT
        || elem->prevElemHandleInEffect != FX_INVALID_HANDLE)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight element ownership");
        return;
    }

    const std::uint16_t elemHandle =
        FxEncodeHandle<FxPool<FxElem>, MAX_ELEMS, FxElem::HANDLE_SCALE>(
            system->elems, elem);
    if (elemHandle == FX_INVALID_HANDLE)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight element pointer");
        return;
    }
    if (!FX_CurrentThreadCanMutateEffect(system, effect))
    {
        Com_Error(
            ERR_DROP,
            "FX spotlight publication requires effect and iterator ownership");
        return;
    }

    fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(system);
    std::int32_t ownerIndex = -1;
    if (!sidecar
        || !FxPoolItemIndex<FxElem, MAX_ELEMS>(
            system->elems, elem, &ownerIndex))
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight physics owner");
        return;
    }
    fx::physics::SidecarStatus sidecarStatus =
        fx::physics::SidecarStatus::InvalidArgument;

    bool published = false;
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const bool ownerVacant = FX_PhysicsOwnerIsVacantLocked(
        sidecar,
        static_cast<std::size_t>(ownerIndex),
        &sidecarStatus);
    if (ownerVacant)
    {
        FX_EnterArchiveAwarePoolCriticalSection();
        if (states
            && FX_IsPoolItemAllocatedLocked<FxElem, MAX_ELEMS>(
                system->elems, elem, &states->elems)
            && Sys_AtomicLoad(&system->activeSpotLightEffectCount) == 1
            && Sys_AtomicLoad(&system->activeSpotLightElemCount) == 0
            && system->activeSpotLightEffectHandle
                == FxEncodeHandle<FxEffect, FX_EFFECT_LIMIT,
                    FxEffect::HANDLE_SCALE>(system->effects, effect))
        {
            elem->nextElemHandleInEffect = FX_INVALID_HANDLE;
            system->activeSpotLightElemHandle = elemHandle;
            Sys_AtomicStore(&system->activeSpotLightElemCount, 1);
            published = true;
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    if (!ownerVacant)
    {
        Com_Error(
            ERR_DROP,
            "FX spotlight owner slot has invalid physics ownership (%u)",
            static_cast<unsigned>(sidecarStatus));
        return;
    }
    if (!published)
    {
        Com_Error(ERR_DROP, "FX spotlight state changed during element spawn");
        return;
    }
}

bool FX_RollbackUnpublishedElem(
    FxSystem *const system,
    FxPool<FxElem> *const elem,
    FxEffect *const effect,
    FxPoolMutationStatus *const outPoolStatus,
    fx::physics::SidecarStatus *const outSidecarStatus) noexcept
{
    if (!outPoolStatus || !outSidecarStatus)
        return false;
    *outPoolStatus = FxPoolMutationStatus::InvalidArgument;
    *outSidecarStatus = fx::physics::SidecarStatus::InvalidArgument;
    if (!system || !elem || !effect
        || !FX_CurrentThreadCanMutateEffect(system, effect))
    {
        return false;
    }

    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(system);
    std::int32_t ownerIndex = -1;
    if (!states || !sidecar
        || !FxPoolItemIndex<FxElem, MAX_ELEMS>(
            system->elems, &elem->item, &ownerIndex))
    {
        return false;
    }

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    *outPoolStatus = FX_FreePool_Generic_FxElem_Status_(
        &elem->item,
        &system->firstFreeElem,
        system->elems,
        &system->activeElemCount,
        &states->elems,
        [&]() noexcept {
            return FX_PhysicsOwnerIsVacantLocked(
                sidecar,
                static_cast<std::size_t>(ownerIndex),
                outSidecarStatus);
        });
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);

    if (*outPoolStatus != FxPoolMutationStatus::Success)
        return false;
    FX_DelRefToEffect(system, effect);
    return true;
}

void __cdecl FX_SpawnElem(
    FxSystem *system,
    FxEffect *effect,
    int32_t elemDefIndex,
    const FxSpatialFrame *effectFrameWhenPlayed,
    int32_t msecWhenPlayed,
    float distanceWhenPlayed,
    int32_t sequence)
{
    uint16_t nextElemHandleInEffect; // [esp+0h] [ebp-80h]
    uint8_t elemType; // [esp+3h] [ebp-7Dh]
    bool v10; // [esp+47h] [ebp-39h]
    int32_t msecBegin; // [esp+64h] [ebp-1Ch]
    const FxElemDef *elemDef; // [esp+6Ch] [ebp-14h]
    uint32_t randomSeed; // [esp+74h] [ebp-Ch]
    FxPool<FxElem> *elem; // [esp+78h] [ebp-8h]
    FxPool<FxElem> *nextElemInEffect;
    uint32_t elemClass; // [esp+7Ch] [ebp-4h]

    if (!system || !effect || !effect->def || !effectFrameWhenPlayed
        || elemDefIndex < 0)
    {
        iassert(system);
        iassert(effect);
        iassert(effect && effect->def);
        iassert(effectFrameWhenPlayed);
        return;
    }
    if (FX_IsEffectLifecycleBlocked(effect))
        return;

    elemDef = FX_GetValidatedRuntimeElemDef(
        effect, static_cast<std::uint32_t>(elemDefIndex));
    if (!elemDef)
        return;

    if (elemDef->elemType == FX_ELEM_TYPE_TRAIL)
    {
        iassert(elemDef->elemType != FX_ELEM_TYPE_TRAIL);
        return;
    }

    if (!fx_cull_elem_spawn->current.enabled || !FX_CullElemForSpawn(&system->cameraPrev, elemDef, effectFrameWhenPlayed->origin))
    {
        msecBegin = elemDef->spawnDelayMsec.base + msecWhenPlayed;
        if (elemDef->spawnDelayMsec.amplitude)
            msecBegin += ((elemDef->spawnDelayMsec.amplitude + 1)
                * LOWORD(fx_randomTable[(296 * sequence + msecBegin + (uint32_t)effect->randomSeed) % 0x1DF + 18])) >> 16;
        randomSeed = (msecBegin + effect->randomSeed + 296 * (uint32_t)(uint8_t)sequence) % 0x1DF;
        switch (elemDef->elemType)
        {
        case 0xAu:
            FX_SpawnRunner(system, effect, elemDef, effectFrameWhenPlayed, randomSeed, msecBegin);
            break;
        case 9u:
            if (effect->boltAndSortOrder.boneIndex != 0x7FF || effect->boltAndSortOrder.dobjHandle == 0xFFF)
            {
                FX_CreateImpactMark(system->localClientNum, elemDef, effectFrameWhenPlayed, randomSeed, ENTITYNUM_NONE);
            }
            else
            {
                FX_CreateImpactMark(
                    system->localClientNum,
                    elemDef,
                    effectFrameWhenPlayed,
                    randomSeed,
                    effect->boltAndSortOrder.dobjHandle);
            }
            break;
        case 8u:
            FX_SpawnSound(system->localClientNum, effect, elemDef, effectFrameWhenPlayed, randomSeed);
            break;
        default:
            if (elemDef->effectOnImpact.handle)
            {
                v10 = 1;
            }
            else if (elemDef->effectOnDeath.handle)
            {
                v10 = 1;
            }
            else
            {
                v10 = elemDef->effectEmitted.handle != 0;
            }
            if (v10
                || msecBegin
                + (((elemDef->lifeSpanMsec.amplitude + 1) * LOWORD(fx_randomTable[randomSeed + 17])) >> 16)
                + elemDef->lifeSpanMsec.base > system->msecNow)
            {
                nextElemInEffect = nullptr;
                nextElemHandleInEffect = FX_INVALID_HANDLE;
                elemType = elemDef->elemType;
                if (elemType != FX_ELEM_TYPE_SPOT_LIGHT)
                {
                    if (elemType > FX_ELEM_TYPE_TRAIL)
                    {
                        elemClass = elemType == FX_ELEM_TYPE_CLOUD ? 2 : 1;
                    }
                    else
                    {
                        elemClass = 0;
                    }
                    nextElemHandleInEffect =
                        effect->firstElemHandle[elemClass];
                    if (nextElemHandleInEffect != FX_INVALID_HANDLE)
                    {
                        nextElemInEffect =
                            FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                                system->elems, nextElemHandleInEffect);
                        if (!FX_IsElemAllocated(system, &nextElemInEffect->item)
                            || nextElemInEffect->item.prevElemHandleInEffect
                                != FX_INVALID_HANDLE)
                        {
                            Com_Error(ERR_DROP, "Invalid FX element head link");
                            return;
                        }
                    }
                }
                else
                {
                    bool hasSpotLight = false;
                    const std::uint16_t effectHandle = FxEncodeHandle<
                        FxEffect, FX_EFFECT_LIMIT,
                        FxEffect::HANDLE_SCALE>(system->effects, effect);
                    if (effectHandle == FX_INVALID_HANDLE
                        || !FX_ValidateSpotLightEffectDef(
                            effect->def, &hasSpotLight)
                        || !hasSpotLight
                        || Sys_AtomicLoad(
                            &system->activeSpotLightEffectCount) != 1
                        || Sys_AtomicLoad(
                            &system->activeSpotLightElemCount) != 0
                        || system->activeSpotLightEffectHandle
                            != effectHandle)
                    {
                        Com_Error(
                            ERR_DROP,
                            "Invalid FX spotlight state before element allocation");
                        return;
                    }
                }

                elem = FX_AllocElem(system);
                if (elem)
                {
                    FX_AddRefToEffect(system, effect);
                    elem->item.defIndex = static_cast<std::uint8_t>(elemDefIndex);
                    elem->item.sequence = static_cast<std::uint8_t>(sequence);
                    elem->item.atRestFraction =
                        (std::numeric_limits<std::uint8_t>::max)();
                    elem->item.emitResidual = 0;
                    elem->item.msecBegin = msecBegin;
                    if (randomSeed != (296 * elem->item.sequence + elem->item.msecBegin + (uint32_t)effect->randomSeed)
                        % 0x1DF)
                        MyAssertHandler(
                            ".\\EffectsCore\\fx_system.cpp",
                            2147,
                            0,
                            "%s",
                            "randomSeed == FX_ElemRandomSeed( effect->randomSeed, elem->msecBegin, elem->sequence )");
                    FX_GetOriginForElem(effect, elemDef, effectFrameWhenPlayed, randomSeed, elem->item.origin);
                    elem->item.baseVel[0] = 0.0;
                    elem->item.baseVel[1] = 0.0;
                    elem->item.baseVel[2] = 0.0;
                    if (elemDef->elemType == 3)
                        elem->item.u.trailTexCoord = distanceWhenPlayed / (double)elemDef->trailDef->repeatDist;
                    elem->item.prevElemHandleInEffect = -1;
                    if (elemDef->elemType == 7)
                    {
                        FX_SpawnSpotLightElem(system, (FxElem *)elem);
                    }
                    else
                    {
                        elem->item.nextElemHandleInEffect =
                            nextElemHandleInEffect;
                        const std::uint16_t elemHandle =
                            FX_PoolToHandle_Generic<FxElem, MAX_ELEMS>(
                                system->elems, &elem->item);
                        bool publishElem = true;
                        if (elemDef->elemType == 5)
                        {
                            elem->item.u.lightingHandle = 0;
                            if ((elemDef->flags & 0x8000000) != 0)
                            {
                                const FxModelPhysicsSpawnResult physicsResult =
                                    FX_TrySpawnModelPhysics(
                                        system,
                                        effect,
                                        elemDef,
                                        randomSeed,
                                        &elem->item);
                                publishElem = physicsResult.outcome
                                    == FxModelPhysicsSpawnOutcome::Success;
                                if (!publishElem)
                                {
                                    FxPoolMutationStatus poolStatus{};
                                    fx::physics::SidecarStatus sidecarStatus{};
                                    const bool rolledBack =
                                        FX_RollbackUnpublishedElem(
                                            system,
                                            elem,
                                            effect,
                                            &poolStatus,
                                            &sidecarStatus);
                                    if (!rolledBack)
                                    {
                                        Com_Error(
                                            ERR_DROP,
                                            "FX unpublished physics element rollback failed (%u, %u)",
                                            static_cast<unsigned>(poolStatus),
                                            static_cast<unsigned>(sidecarStatus));
                                        return;
                                    }
                                    if (physicsResult.outcome
                                            != FxModelPhysicsSpawnOutcome::ResourceUnavailable)
                                    {
                                        Com_Error(
                                            ERR_DROP,
                                            "FX model physics spawn failed (%u, %u, %u)",
                                            static_cast<unsigned>(physicsResult.outcome),
                                            static_cast<unsigned>(physicsResult.physicsStatus),
                                            static_cast<unsigned>(physicsResult.sidecarStatus));
                                        return;
                                    }
                                }
                            }
                        }
                        if (publishElem)
                        {
                            if (nextElemInEffect)
                            {
                                nextElemInEffect->item.prevElemHandleInEffect =
                                    elemHandle;
                            }
                            effect->firstElemHandle[elemClass] = elemHandle;
                        }
                    }
                }
                else
                {
                    R_WarnOncePerFrame(R_WARN_FX_ELEM_LIMIT);
                }
            }
            break;
        }
    }
}

FxPool<FxElem> *__cdecl FX_AllocElem(FxSystem *system)
{
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
    {
        Com_Error(ERR_DROP, "Missing FX element allocation sidecar");
        return nullptr;
    }
    return FX_AllocPool_Generic_FxElem_(
        &system->firstFreeElem,
        system->elems,
        &system->activeElemCount,
        &states->elems);
}

void __cdecl FX_SpawnRunner(
    FxSystem *system,
    FxEffect *effect,
    const FxElemDef *remoteElemDef,
    const FxSpatialFrame *effectFrameWhenPlayed,
    int32_t randomSeed,
    int32_t msecWhenPlayed)
{
    int32_t v6; // [esp+0h] [ebp-88h]
    int32_t sortOrder; // [esp+Ch] [ebp-7Ch]
    const FxEffectDef *effectDef; // [esp+20h] [ebp-68h]
    FxEffect *spawnedEffect; // [esp+28h] [ebp-60h]
    float *usedAxis; // [esp+30h] [ebp-58h]
    float rotatedAxis[3][3]; // [esp+34h] [ebp-54h] BYREF
    float spawnOrigin[3]; // [esp+58h] [ebp-30h] BYREF
    float axis[3][3]; // [esp+64h] [ebp-24h] BYREF

    FX_GetSpawnOrigin(effectFrameWhenPlayed, remoteElemDef, randomSeed, spawnOrigin);
    FX_OffsetSpawnOrigin(effectFrameWhenPlayed, remoteElemDef, randomSeed, spawnOrigin);
    effectDef = FX_GetElemVisuals(remoteElemDef, randomSeed).effectDef.handle;
    UnitQuatToAxis(effectFrameWhenPlayed->quat, axis);
    if ((remoteElemDef->flags & 8) != 0)
    {
        FX_RandomlyRotateAxis(axis, randomSeed, rotatedAxis);
        usedAxis = rotatedAxis[0];
    }
    else
    {
        usedAxis = axis[0];
    }
    if (remoteElemDef->sortOrder == 255)
        sortOrder = 255;
    else
        sortOrder = remoteElemDef->sortOrder;
    if (sortOrder > 0)
        v6 = sortOrder;
    else
        v6 = 0;
    if (effect->boltAndSortOrder.boneIndex == 0x7FF)
    {
        if (effect->boltAndSortOrder.dobjHandle == 0xFFF)
            spawnedEffect = FX_SpawnEffect(
                system,
                effectDef,
                msecWhenPlayed,
                spawnOrigin,
                (const float (*)[3])usedAxis,
                4095,
                2047,
                v6,
                effect->owner,
                ENTITYNUM_NONE);
        else
            spawnedEffect = FX_SpawnEffect(
                system,
                effectDef,
                msecWhenPlayed,
                spawnOrigin,
                (const float (*)[3])usedAxis,
                4095,
                2047,
                v6,
                effect->owner,
                effect->boltAndSortOrder.dobjHandle);
    }
    else
    {
        spawnedEffect = FX_SpawnEffect(
            system,
            effectDef,
            msecWhenPlayed,
            spawnOrigin,
            (const float (*)[3])usedAxis,
            effect->boltAndSortOrder.dobjHandle,
            effect->boltAndSortOrder.boneIndex,
            v6,
            effect->owner,
            ENTITYNUM_NONE);
    }
    if (spawnedEffect)
        FX_DelRefToEffect(system, spawnedEffect);
}

namespace
{
FxModelPhysicsSpawnResult FX_TrySpawnModelPhysics(
    FxSystem *const system,
    FxEffect *const effect,
    const FxElemDef *const elemDef,
    const int32_t randomSeed,
    FxElem *const elem) noexcept
{
    FxModelPhysicsSpawnResult result{};
    if (!system || !effect || !elemDef || !elem
        || elemDef->elemType != FX_ELEM_TYPE_MODEL
        || (static_cast<std::uint32_t>(elemDef->flags) & 0x08000000u)
            == 0
        || !FX_CurrentThreadCanMutateEffect(system, effect))
    {
        return result;
    }

    fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(system);
    std::int32_t ownerIndex = -1;
    if (!sidecar || !system->elems
        || !FxPoolItemIndex<FxElem, MAX_ELEMS>(
            system->elems, elem, &ownerIndex)
        || !FX_IsElemAllocated(system, elem))
    {
        return result;
    }

    float velocity[3]; // [esp+4Ch] [ebp-90h] BYREF
    float angularVelocity[3]; // [esp+58h] [ebp-84h] BYREF
    FxElemVisuals visuals; // [esp+64h] [ebp-78h]
    float msecLifeSpan; // [esp+68h] [ebp-74h]
    float quat[4]; // [esp+6Ch] [ebp-70h] BYREF
    orientation_t orient; // [esp+7Ch] [ebp-60h] BYREF
    float worldOrigin[3]; // [esp+ACh] [ebp-30h] BYREF
    float axis[3][3]; // [esp+B8h] [ebp-24h] BYREF

    FX_GetOrientation(elemDef, &effect->frameAtSpawn, &effect->frameNow, randomSeed, &orient);
    FX_OrientationPosToWorldPos(&orient, elem->origin, worldOrigin);
    FX_GetElemAxis(elemDef, randomSeed, &orient, 0.0, axis);
    AxisToQuat(axis, quat);
    msecLifeSpan = (float)((((elemDef->lifeSpanMsec.amplitude + 1) * LOWORD(fx_randomTable[randomSeed + 17])) >> 16)
        + elemDef->lifeSpanMsec.base);
    FX_GetVelocityAtTime(elemDef, randomSeed, msecLifeSpan, 0.0, &orient, elem->baseVel, velocity);
    angularVelocity[0] =
        (elemDef->angularVelocity[0].amplitude
             * fx_randomTable[randomSeed + 3]
         + elemDef->angularVelocity[0].base)
        * 1000.0f;
    angularVelocity[1] =
        (elemDef->angularVelocity[1].amplitude
             * fx_randomTable[randomSeed + 4]
         + elemDef->angularVelocity[1].base)
        * 1000.0f;
    angularVelocity[2] =
        (elemDef->angularVelocity[2].amplitude
             * fx_randomTable[randomSeed + 5]
         + elemDef->angularVelocity[2].base)
        * 1000.0f;
    constexpr float MAX_FX_PHYSICS_ANGULAR_VELOCITY = 65536.0f;
    for (const float component : angularVelocity)
    {
        if (!std::isfinite(component)
            || component < -MAX_FX_PHYSICS_ANGULAR_VELOCITY
            || component > MAX_FX_PHYSICS_ANGULAR_VELOCITY)
        {
            return result;
        }
    }
    visuals.anonymous = FX_GetElemVisuals(elemDef, randomSeed).anonymous;
    if (!visuals.model || !visuals.model->physPreset)
        return result;

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    if (!FX_PhysicsOwnerIsVacantLocked(
            sidecar,
            static_cast<std::size_t>(ownerIndex),
            &result.sidecarStatus))
    {
        result.outcome = FxModelPhysicsSpawnOutcome::OwnershipRejected;
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
        return result;
    }
    if (sidecar->ActiveCount() == fx::physics::BODY_LIMIT)
    {
        result.outcome = FxModelPhysicsSpawnOutcome::ResourceUnavailable;
        result.sidecarStatus = fx::physics::SidecarStatus::CapacityExceeded;
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
        return result;
    }

    dxBody *body = nullptr;
    result.physicsStatus = Phys_TryCreateBodyFromPresetAndXModelLockedNoReport(
        PHYS_WORLD_FX,
        worldOrigin,
        quat,
        velocity,
        visuals.model->physPreset,
        visuals.model,
        &body,
        &result.resourceFailure);
    if (result.physicsStatus != PhysBodyModelCreateStatus::Success)
    {
        const bool cleanupFailed = result.physicsStatus
            == PhysBodyModelCreateStatus::CleanupFailed;
        result.outcome = result.physicsStatus
                == PhysBodyModelCreateStatus::InvalidArgument
            ? FxModelPhysicsSpawnOutcome::InvalidState
            : FxModelPhysicsSpawnOutcome::ResourceUnavailable;
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
        if (cleanupFailed)
            std::abort();
        if (result.physicsStatus != PhysBodyModelCreateStatus::InvalidArgument)
            Phys_ReportBodyModelCreateFailure(
                result.physicsStatus, result.resourceFailure);
        return result;
    }

    if (!Phys_TryObjSetAngularVelocityLockedNoReport(
            body, angularVelocity))
    {
        result.outcome = FxModelPhysicsSpawnOutcome::InvalidState;
        const PhysBodyRollbackStatus cleanupStatus =
            Phys_TryDestroyBodyLockedNoReport(PHYS_WORLD_FX, body);
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
        if (cleanupStatus != PhysBodyRollbackStatus::Success)
            std::abort();
        return result;
    }

    const fx::physics::TokenResult binding = fx::physics::BindWithScratch(
        sidecar,
        static_cast<std::size_t>(ownerIndex),
        body,
        &fx_liveSidecarValidationScratch);
    result.sidecarStatus = binding.status;
    if (!binding)
    {
        // The complete sidecar was validated immediately before allocation
        // under this same lock. DuplicateBody can therefore identify only the
        // address returned by the allocator, whose existing registration keeps
        // the sole right to destroy it. Every other failure leaves this fresh
        // body caller-owned.
        const bool ownershipAmbiguous =
            binding.status == fx::physics::SidecarStatus::DuplicateBody;
        PhysBodyRollbackStatus cleanupStatus =
            PhysBodyRollbackStatus::Success;
        if (!ownershipAmbiguous)
        {
            cleanupStatus = Phys_TryDestroyBodyLockedNoReport(
                PHYS_WORLD_FX, body);
        }
        result.outcome = binding.status
                == fx::physics::SidecarStatus::CapacityExceeded
            ? FxModelPhysicsSpawnOutcome::ResourceUnavailable
            : FxModelPhysicsSpawnOutcome::OwnershipRejected;
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
        if (ownershipAmbiguous
            || cleanupStatus != PhysBodyRollbackStatus::Success)
        {
            std::abort();
        }
        return result;
    }

    elem->physObjId = fx::physics::TokenToLegacyField(binding.token);
    result.outcome = FxModelPhysicsSpawnOutcome::Success;
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    return result;
}
} // namespace

bool __cdecl FX_SpawnModelPhysics(
    FxSystem *const system,
    FxEffect *const effect,
    const FxElemDef *const elemDef,
    const int32_t randomSeed,
    FxElem *const elem)
{
    return FX_TrySpawnModelPhysics(
               system, effect, elemDef, randomSeed, elem)
            .outcome
        == FxModelPhysicsSpawnOutcome::Success;
}

void __cdecl FX_GetOriginForElem(
    FxEffect *effect,
    const FxElemDef *elemDef,
    const FxSpatialFrame *effectFrameWhenPlayed,
    int32_t randomSeed,
    float *outOrigin)
{
    const FxSpatialFrame *p_frameAtSpawn; // [esp+0h] [ebp-3Ch]
    float effectFrameAxis[3][3]; // [esp+4h] [ebp-38h] BYREF
    const FxSpatialFrame *effectFrame; // [esp+28h] [ebp-14h]
    float delta[3]; // [esp+2Ch] [ebp-10h] BYREF
    int32_t runFlags; // [esp+38h] [ebp-4h]

    runFlags = elemDef->flags & 0xC0;
    if (runFlags == 64)
        p_frameAtSpawn = &effect->frameAtSpawn;
    else
        p_frameAtSpawn = effectFrameWhenPlayed;
    effectFrame = p_frameAtSpawn;
    if (runFlags == 192)
    {
        *outOrigin = 0.0;
        outOrigin[1] = 0.0;
        outOrigin[2] = 0.0;
    }
    else
    {
        UnitQuatToAxis(effectFrame->quat, effectFrameAxis);
        FX_GetSpawnOrigin(effectFrame, elemDef, randomSeed, outOrigin);
        FX_OffsetSpawnOrigin(effectFrame, elemDef, randomSeed, outOrigin);
        if (runFlags == 128 || runFlags == 64)
        {
            Vec3Sub(outOrigin, effectFrame->origin, delta);
            *outOrigin = Vec3Dot(delta, effectFrameAxis[0]);
            outOrigin[1] = Vec3Dot(delta, effectFrameAxis[1]);
            outOrigin[2] = Vec3Dot(delta, effectFrameAxis[2]);
        }
    }
}

void __cdecl FX_SpawnSound(
    int32_t localClientNumber,
    FxEffect *effect,
    const FxElemDef *elemDef,
    const FxSpatialFrame *effectFrameWhenPlayed,
    int32_t randomSeed)
{
    FxElemVisuals visuals; // [esp+Ch] [ebp-14h]
    snd_alias_list_t *alias_list; // [esp+10h] [ebp-10h]
    float spawnOrigin[3]; // [esp+14h] [ebp-Ch] BYREF

    if (elemDef->elemType != 8)
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            2007,
            0,
            "%s\n\t(elemDef->elemType) = %i",
            "(elemDef->elemType == FX_ELEM_TYPE_SOUND)",
            elemDef->elemType);
    FX_GetSpawnOrigin(effectFrameWhenPlayed, elemDef, randomSeed, spawnOrigin);
    FX_OffsetSpawnOrigin(effectFrameWhenPlayed, elemDef, randomSeed, spawnOrigin);
    visuals.anonymous = FX_GetElemVisuals(elemDef, randomSeed).anonymous;
    alias_list = Com_FindSoundAlias(visuals.effectDef.name);
    if (alias_list)
    {
        if (SND_AnyActiveListeners())
        {
            if (Sys_IsMainThread())
                CG_PlaySoundAlias(localClientNumber, ENTITYNUM_WORLD, spawnOrigin, alias_list);
            else
                CG_AddFXSoundAlias(localClientNumber, spawnOrigin, alias_list);
        }
    }
    else
    {
        Com_PrintWarning(21, "Failed to find sound alias '%s'\n", visuals.effectDef.name);
    }
}

void __cdecl FX_FreeElem(FxSystem* system, uint16_t elemHandle, FxEffect* effect, uint32_t elemClass)
{
    if (!system || !effect || !effect->def || elemClass >= 3)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1868,
            0,
            "%s",
            "system && effect && effect->def && elemClass < 3");
        return;
    }
    if (!FX_CurrentThreadCanMutateEffect(system, effect))
    {
        Com_Error(
            ERR_DROP,
            "FX element free requires effect and iterator ownership");
        return;
    }
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
    {
        Com_Error(ERR_DROP, "Missing FX element allocation sidecar during free");
        return;
    }

    FxPool<FxElem> *const elem =
        FX_PoolFromHandle_Generic<FxElem, 2048>(system->elems, elemHandle);
    if (!elem || !FX_IsElemAllocated(system, &elem->item))
    {
        Com_Error(ERR_DROP, "FX element being freed is not allocated");
        return;
    }

    const FxElem releasedElem = elem->item;
    FxPool<FxElem> *nextElem = nullptr;
    if (releasedElem.nextElemHandleInEffect != FX_INVALID_HANDLE)
    {
        nextElem = FX_PoolFromHandle_Generic<FxElem, 2048>(
            system->elems, releasedElem.nextElemHandleInEffect);
        if (!nextElem || nextElem == elem
            || !FX_IsElemAllocated(system, &nextElem->item))
        {
            Com_Error(ERR_DROP, "Invalid next FX element ownership during free");
            return;
        }
        if (nextElem->item.prevElemHandleInEffect != elemHandle)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                1912,
                0,
                "%s",
                "nextElem->item.prevElemHandleInEffect == elemHandle");
            Com_Error(ERR_DROP, "Invalid next FX element backlink during free");
            return;
        }
    }

    FxPool<FxElem> *prevElem = nullptr;
    if (releasedElem.prevElemHandleInEffect == FX_INVALID_HANDLE)
    {
        if (effect->firstElemHandle[elemClass] != elemHandle)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                2207,
                0,
                "%s",
                "effect->firstElemHandle[elemClass] == elemHandle");
            Com_Error(ERR_DROP, "Invalid FX element list head during free");
            return;
        }
    }
    else
    {
        prevElem = FX_PoolFromHandle_Generic<FxElem, 2048>(
            system->elems, releasedElem.prevElemHandleInEffect);
        if (!prevElem || prevElem == elem
            || !FX_IsElemAllocated(system, &prevElem->item))
        {
            Com_Error(ERR_DROP, "Invalid previous FX element ownership during free");
            return;
        }
        if (prevElem->item.nextElemHandleInEffect != elemHandle)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                1938,
                0,
                "%s",
                "prevElem->item.nextElemHandleInEffect == elemHandle");
            Com_Error(ERR_DROP, "Invalid previous FX element forward link during free");
            return;
        }
    }

    const FxEffectDef *const effectDef = effect->def;
    const std::int64_t elemDefCount =
        static_cast<std::int64_t>(effectDef->elemDefCountLooping)
        + effectDef->elemDefCountOneShot
        + effectDef->elemDefCountEmission;
    if (!effectDef->elemDefs || effectDef->elemDefCountLooping < 0
        || effectDef->elemDefCountOneShot < 0
        || effectDef->elemDefCountEmission < 0
        || releasedElem.defIndex >= elemDefCount)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1954,
            0,
            "%s",
            "releasedElem.defIndex indexes effectDef->elemDefs");
        Com_Error(ERR_DROP, "Invalid FX element definition during free");
        return;
    }
    const FxElemDef *const elemDef =
        &effectDef->elemDefs[releasedElem.defIndex];
    fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(system);
    const std::size_t ownerIndex =
        static_cast<std::size_t>(elem - system->elems);
    const bool ownsPhysicsBody = elemDef->elemType == FX_ELEM_TYPE_MODEL
        && (static_cast<std::uint32_t>(elemDef->flags) & 0x08000000u)
            != 0;
    const fx::physics::BodyToken bodyToken = ownsPhysicsBody
        ? fx::physics::TokenFromLegacyField(releasedElem.physObjId)
        : fx::physics::INVALID_BODY_TOKEN;
    fx::physics::SidecarStatus sidecarStatus =
        fx::physics::SidecarStatus::InvalidArgument;
    dxBody *releasedBody = nullptr;

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const FxPoolMutationStatus poolStatus =
        FX_FreePool_Generic_FxElem_Status_(
        &elem->item,
        &system->firstFreeElem,
        system->elems,
        &system->activeElemCount,
        &states->elems,
        [&]() noexcept -> bool {
            if (ownsPhysicsBody)
            {
                const fx::physics::BodyResult body = fx::physics::Take(
                    sidecar, ownerIndex, bodyToken);
                sidecarStatus = body.status;
                if (!body)
                    return false;
                releasedBody = body.body;
            }
            else if (!FX_PhysicsOwnerIsVacantLocked(
                         sidecar, ownerIndex, &sidecarStatus))
            {
                return false;
            }

            if (!elemClass && effect->firstSortedElemHandle == elemHandle)
                effect->firstSortedElemHandle =
                    releasedElem.nextElemHandleInEffect;
            if (nextElem)
            {
                nextElem->item.prevElemHandleInEffect =
                    releasedElem.prevElemHandleInEffect;
            }
            if (prevElem)
            {
                prevElem->item.nextElemHandleInEffect =
                    releasedElem.nextElemHandleInEffect;
            }
            else
            {
                effect->firstElemHandle[elemClass] =
                    releasedElem.nextElemHandleInEffect;
            }
            return true;
        });
    if (poolStatus == FxPoolMutationStatus::Success && releasedBody)
        Phys_ObjDestroy(PHYS_WORLD_FX, releasedBody);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);

    if (poolStatus != FxPoolMutationStatus::Success)
    {
        if (poolStatus == FxPoolMutationStatus::BeforePublishRejected)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                __LINE__,
                0,
                "FX element physics ownership transfer failed with status %u",
                static_cast<unsigned>(sidecarStatus));
            Com_Error(
                ERR_DROP,
                "FX element physics ownership transfer failed with status %u",
                static_cast<unsigned>(sidecarStatus));
        }
        else
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                __LINE__,
                0,
                "FX element pool free failed with status %u",
                static_cast<unsigned>(poolStatus));
            Com_Error(
                ERR_DROP,
                "FX element pool free failed with status %u",
                static_cast<unsigned>(poolStatus));
        }
        return;
    }
    FX_DelRefToEffect(system, effect);
}

void __cdecl FX_FreeTrailElem(
    FxSystem *system,
    uint16_t trailElemHandle,
    FxEffect *effect,
    FxTrail *trail,
    FxTrail *trailOwner)
{
    if (!system || !effect || !trail || !trailOwner)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1932,
            0,
            "%s",
            "system && effect && trail && trailOwner");
        return;
    }
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
    {
        Com_Error(ERR_DROP, "Missing FX trail-element allocation sidecar during free");
        return;
    }
    if (!FX_IsTrailAllocated(system, trailOwner))
    {
        Com_Error(ERR_DROP, "FX trail-element owner is not allocated");
        return;
    }
    if (trail != trailOwner
        && (trail->nextTrailHandle != trailOwner->nextTrailHandle
            || trail->firstElemHandle != trailOwner->firstElemHandle
            || trail->lastElemHandle != trailOwner->lastElemHandle
            || trail->defIndex != trailOwner->defIndex
            || trail->sequence != trailOwner->sequence))
    {
        Com_Error(ERR_DROP, "FX staged trail state does not match its owner");
        return;
    }

    FxPool<FxTrailElem> *const trailElem =
        FX_PoolFromHandle_Generic<FxTrailElem, 2048>(
            system->trailElems, trailElemHandle);
    if (!trailElem
        || !FX_IsTrailElemAllocated(system, &trailElem->item))
    {
        Com_Error(ERR_DROP, "FX trail element being freed is not allocated");
        return;
    }
    if (trail->firstElemHandle != trailElemHandle)
    {
        MyAssertHandler(".\\EffectsCore\\fx_system.cpp", 2256, 0, "%s", "trail->firstElemHandle == trailElemHandle");
        Com_Error(ERR_DROP, "Invalid FX trail-element list head during free");
        return;
    }

    if (trail->lastElemHandle == FX_INVALID_HANDLE)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1949,
            0,
            "%s",
            "nonempty trail has a valid tail handle");
        Com_Error(ERR_DROP, "Invalid FX trail endpoint state during free");
        return;
    }
    FxPool<FxTrailElem> *const lastTrailElem =
        FxDecodeHandle<FxPool<FxTrailElem>, MAX_TRAIL_ELEMS,
            FxTrailElem::HANDLE_SCALE>(
            system->trailElems, trail->lastElemHandle);
    if (!lastTrailElem
        || !FX_IsTrailElemAllocated(system, &lastTrailElem->item)
        || lastTrailElem->item.nextTrailElemHandle != FX_INVALID_HANDLE)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1949,
            0,
            "%s",
            "trail tail handle identifies a terminal element");
        Com_Error(ERR_DROP, "Invalid FX trail tail ownership during free");
        return;
    }

    const std::uint16_t nextTrailElemHandle =
        trailElem->item.nextTrailElemHandle;
    FxPool<FxTrailElem> *nextTrailElem = nullptr;
    if (nextTrailElemHandle != FX_INVALID_HANDLE)
    {
        nextTrailElem = FxDecodeHandle<
            FxPool<FxTrailElem>, MAX_TRAIL_ELEMS,
            FxTrailElem::HANDLE_SCALE>(
            system->trailElems, nextTrailElemHandle);
    }
    if (nextTrailElemHandle == trailElemHandle
        || (nextTrailElemHandle != FX_INVALID_HANDLE
            && (!nextTrailElem
                || !FX_IsTrailElemAllocated(
                    system, &nextTrailElem->item)))
        || ((trail->lastElemHandle == trailElemHandle)
            != (nextTrailElemHandle == FX_INVALID_HANDLE)))
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1949,
            0,
            "%s",
            "trail element links are consistent");
        Com_Error(ERR_DROP, "Invalid FX trail-element links during free");
        return;
    }
    const bool releasesTail =
        trail->lastElemHandle == trailElemHandle;
    if (!FX_FreePool_Generic_FxTrailElem_(
            &trailElem->item,
            &system->firstFreeTrailElem,
            system->trailElems,
            &system->activeTrailElemCount,
            &states->trailElems,
            [&]() noexcept {
                if (releasesTail)
                    trail->lastElemHandle = FX_INVALID_HANDLE;
                trail->firstElemHandle = nextTrailElemHandle;
                if (trailOwner != trail)
                {
                    if (releasesTail)
                        trailOwner->lastElemHandle = FX_INVALID_HANDLE;
                    trailOwner->firstElemHandle = nextTrailElemHandle;
                }
            }))
    {
        return;
    }
    FX_DelRefToEffect(system, effect);
}

void __cdecl FX_FreeSpotLightElem(FxSystem *system, uint16_t elemHandle, FxEffect *effect)
{
    if (!system || !effect)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1976,
            0,
            "%s",
            "system && effect");
        return;
    }
    if (!FX_CurrentThreadCanMutateEffect(system, effect))
    {
        Com_Error(
            ERR_DROP,
            "FX spotlight free requires effect and iterator ownership");
        return;
    }
    FxPoolAllocationStates *const states =
        FX_GetPoolAllocationStates(system);
    if (!states)
    {
        Com_Error(ERR_DROP, "Missing FX element allocation sidecar during spotlight free");
        return;
    }

    const std::uint16_t effectHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    if (effectHandle == FX_INVALID_HANDLE)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight effect pointer during free");
        return;
    }
    FxSpotLightStateSnapshot spotLightSnapshot{};
    if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot))
    {
        Com_Error(ERR_DROP, "Missing FX spotlight state during free");
        return;
    }
    if (spotLightSnapshot.effectCount != 1
        || spotLightSnapshot.elemCount != 1)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            2288,
            0,
            "%s",
            "system->activeSpotLightEffectCount == 1 && system->activeSpotLightElemCount == 1");
        Com_Error(ERR_DROP, "Invalid FX spotlight counts during free");
        return;
    }
    if (spotLightSnapshot.effectHandle != effectHandle)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1994,
            0,
            "%s",
            "system->activeSpotLightEffectHandle == effectHandle");
        Com_Error(ERR_DROP, "Invalid FX spotlight effect owner during free");
        return;
    }
    if (spotLightSnapshot.elemHandle != elemHandle)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            1994,
            0,
            "%s",
            "system->activeSpotLightElemHandle == elemHandle");
        Com_Error(ERR_DROP, "Invalid FX spotlight element handle during free");
        return;
    }

    FxPool<FxElem> *const elem = FX_PoolFromHandle_Generic<FxElem, 2048>(
        system->elems, elemHandle);
    if (!elem || !FX_IsElemAllocated(system, &elem->item))
    {
        Com_Error(ERR_DROP, "FX spotlight element being freed is not allocated");
        return;
    }
    bool hasSpotLight = false;
    if (!FX_ValidateSpotLightEffectDef(effect->def, &hasSpotLight)
        || !hasSpotLight)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight owner definition during free");
        return;
    }
    const FxElemDef *const elemDef =
        FX_GetValidatedRuntimeElemDef(effect, elem->item.defIndex);
    if (!elemDef || elemDef->elemType != FX_ELEM_TYPE_SPOT_LIGHT
        || elem->item.nextElemHandleInEffect != FX_INVALID_HANDLE
        || elem->item.prevElemHandleInEffect != FX_INVALID_HANDLE)
    {
        Com_Error(ERR_DROP, "Invalid FX spotlight element ownership during free");
        return;
    }
    fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(system);
    const std::size_t ownerIndex =
        static_cast<std::size_t>(elem - system->elems);
    fx::physics::SidecarStatus sidecarStatus =
        fx::physics::SidecarStatus::InvalidArgument;
    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const FxPoolMutationStatus poolStatus =
        FX_FreePool_Generic_FxElem_Status_(
        &elem->item,
        &system->firstFreeElem,
        system->elems,
        &system->activeElemCount,
        &states->elems,
        [&]() noexcept -> bool {
            if (!FX_PhysicsOwnerIsVacantLocked(
                    sidecar, ownerIndex, &sidecarStatus))
            {
                return false;
            }
            system->activeSpotLightElemHandle = FX_INVALID_HANDLE;
            system->activeSpotLightBoltDobj = -1;
            Sys_AtomicStore(&system->activeSpotLightElemCount, 0);
            return true;
        });
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    if (poolStatus != FxPoolMutationStatus::Success)
    {
        if (poolStatus == FxPoolMutationStatus::BeforePublishRejected)
        {
            Com_Error(
                ERR_DROP,
                "FX spotlight physics owner is not vacant (%u)",
                static_cast<unsigned>(sidecarStatus));
        }
        else
        {
            Com_Error(
                ERR_DROP,
                "FX spotlight pool free failed with status %u",
                static_cast<unsigned>(poolStatus));
        }
        return;
    }
    FX_DelRefToEffect(system, effect);
}

double __cdecl FX_GetClientVisibility(int32_t localClientNum, const float *start, const float *end)
{
    float v4; // [esp+14h] [ebp-9Ch]
    float v5; // [esp+18h] [ebp-98h]
    float diff[11]; // [esp+38h] [ebp-78h] BYREF
    const FxVisBlocker *visBlocker; // [esp+64h] [ebp-4Ch]
    float totalVis; // [esp+68h] [ebp-48h]
    const FxVisState *visState; // [esp+6Ch] [ebp-44h]
    float dir[3]; // [esp+70h] [ebp-40h] BYREF
    float halfLen; // [esp+7Ch] [ebp-34h]
    std::uint32_t blockerIndex;
    std::uint32_t blockerCount;
    std::int32_t rawBlockerCount;
    float len; // [esp+84h] [ebp-2Ch]
    FxSystem *system; // [esp+88h] [ebp-28h]
    float projDir[3]; // [esp+8Ch] [ebp-24h] BYREF
    float projPt[3]; // [esp+98h] [ebp-18h] BYREF
    float dot; // [esp+A4h] [ebp-Ch]
    float distSq; // [esp+A8h] [ebp-8h]
    float blockerRadius; // [esp+ACh] [ebp-4h]

    system = FX_GetSystem(localClientNum);
    visState = system->visStateBufferRead;
    if (!visState)
        return 1.0;
    rawBlockerCount = Sys_AtomicLoad(&visState->blockerCount);
    if (rawBlockerCount <= 0)
    {
        if (rawBlockerCount < 0)
        {
            MyAssertHandler(
                ".\\EffectsCore\\fx_system.cpp",
                2355,
                0,
                "%s",
                "visState->blockerCount >= 0");
        }
        return 1.0;
    }
    blockerCount = static_cast<std::uint32_t>(rawBlockerCount);
    if (blockerCount > fx::visibility::kBlockerCapacity)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_system.cpp",
            2355,
            0,
            "visState->blockerCount <= FX_VIS_BLOCKER_LIMIT\n\t%u, %u",
            blockerCount,
            fx::visibility::kBlockerCapacity);
        blockerCount = fx::visibility::kBlockerCapacity;
    }

    PROF_SCOPED("FX_GetVisibility");

    Vec3Sub(end, start, dir);
    len = Vec3Normalize(dir);
    if (fx_visMinTraceDist->current.value <= (double)len)
    {
        halfLen = len * 0.5;
        totalVis = 1.0;
        for (blockerIndex = 0; blockerIndex < blockerCount; ++blockerIndex)
        {
            visBlocker = &visState->blocker[blockerIndex];
            Vec3Sub(visBlocker->origin, start, projDir);
            dot = Vec3Dot(projDir, dir);
            v5 = dot - halfLen;
            v4 = I_fabs(v5);
            if (halfLen >= (double)v4)
            {
                Vec3Mad(start, dot, dir, projPt);
                Vec3Sub(projPt, visBlocker->origin, diff);
                distSq = Vec3LengthSq(diff);
                blockerRadius = (double)visBlocker->radius * 0.0625;
                if (distSq < blockerRadius * blockerRadius)
                    totalVis = (double)visBlocker->visibility * 0.0000152587890625 * totalVis;
            }
        }
        return totalVis;
    }
    else
    {
        return 1.0;
    }
}

double FX_GetServerVisibility(const float *start, const float *end)
{
    return FX_GetClientVisibility(fx_serverVisClient, start, end);
}

FxEffect *FX_GetClientEffectByIndex(int clientIndex, uint32_t index)
{
    iassert(clientIndex == 0);
    iassert(index >= 0 && index < FX_EFFECT_LIMIT);

    return &fx_systemPool[0].effects[index];
}

int FX_GetClientEffectIndex(int clientIndex, FxEffect *effect)
{
    FxEffect *effects; // r11

    iassert(clientIndex == 0);
    iassert(effect);

    effects = fx_systemPool[0].effects;

    iassert(effect >= &fx_systemPool[0].effects[0] && effect < &fx_systemPool[0].effects[FX_EFFECT_LIMIT]);

    return effect - effects;
}

// fx_restore_entry_stubs: engine services the enrolled production FX
// archive pipeline references but a unit harness cannot host. Every
// stub carries its PRODUCTION signature and reproduces the
// single-slot fx_system.cpp runtime semantics verbatim (the
// production loops walk fx_systemPool; this harness hosts exactly one
// slot, so each pool-indexed lookup reduces to one comparison against
// the harness system with identical outcomes for every input).
//
// Service lookups (FX_GetSystem / FX_GetSystemBuffers /
// FX_GetArchiveGate / FX_GetCooperativeIteratorGeneration[State] /
// FX_GetEffectKillGate / FX_GetPoolAllocationStates /
// FX_GetOwnedSystemBuffers / FX_GetPhysicsBodySidecar /
// FX_GetEffectOwnerAdmissionState) return harness-owned cells.
//
// Gate machinery (FX_BeginArchive / FX_EndArchive /
// FX_ValidateArchiveExclusiveState / FX_ValidateExclusiveState,
// archive gate publication and reset helpers) reproduces the
// production single-threaded outcomes; the cooperative iterator and
// pool-graph validation logic is the production algorithm (header-
// inlined FxPoolRebuildAllocationStateLocked /
// FxValidatePoolAllocationGraphWithScratch / fx::physics sidecar
// primitives) driven against the harness slot.
//
// Native physics (Phys_* family) is FAIL-CLOSED: the harness hosts no
// ODE world, so every transaction is refused, counted in
// HarnessState.physicsRejectionCalls and returns its failure enum.
// A pristine 0-effect archive never reaches these paths; a call means
// the test created live bodies through a path it must not.
//
// FX_ForEachEffectDef is only reachable on the LoadObj capture path,
// which fast-file mode never takes - the stub records the call so a
// routing regression is observable. FX_ErrorCleanup mirrors the
// production lease-abandonment cleanup as a counted no-op.

#include "fx_restore_entry_harness.hpp"

#include <EffectsCore/fx_archive_gate_control.h>
#include <EffectsCore/fx_physics_sidecar.h>
#include <EffectsCore/fx_pool.h>
#include <EffectsCore/fx_pool_graph.h>
#include <EffectsCore/fx_iterator_atomic.h>

#include <physics/phys_local.h>
#include <qcommon/sys_sync.h>

#include <thread>

using namespace fx_restore_entry_harness;

namespace
{
// The production thread-local archive gate owner state
// (fx_system.cpp: fx_archiveThreadState). Owned by the harness
// because the enrolled fx::archive::RefreshArchiveGateOwnerGeneration
// takes the owner cell by address.
thread_local fx::archive::ArchiveGateOwnerState fx_archiveThreadState{};
} // namespace

FxSystem *__cdecl FX_GetSystem(int32_t clientIndex)
{
    if (clientIndex != 0)
        return nullptr;
    return &State().system;
}

FxSystemBuffers *__cdecl FX_GetSystemBuffers(int32_t clientIndex)
{
    if (clientIndex != 0)
        return nullptr;
    return &State().buffers;
}

volatile std::int32_t *FX_GetCooperativeIteratorGenerationState(
    const FxSystem *const system) noexcept
{
    if (system != &State().system)
        return nullptr;
    return &State().iteratorGeneration;
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

volatile std::int32_t *FX_GetEffectKillGate(
    const FxSystem *const system) noexcept
{
    if (system != &State().system)
        return nullptr;
    return &State().effectKillGate;
}

FxPoolAllocationStates *FX_GetPoolAllocationStates(
    const FxSystem *const system) noexcept
{
    if (system != &State().system)
        return nullptr;
    return &State().poolStates;
}

const FxSystemBuffers *FX_GetOwnedSystemBuffers(
    const FxSystem *const system) noexcept
{
    if (system != &State().system)
        return nullptr;
    return &State().buffers;
}

volatile std::int32_t *FX_GetEffectOwnerAdmissionState(
    const FxSystem *const system,
    const FxEffect *const effect) noexcept
{
    if (!system || !effect || system != &State().system)
        return nullptr;
    constexpr std::size_t effectHandleStride = FxHandleStride<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>();
    const std::uint16_t effectHandle = FxEncodeHandle<
        FxEffect, FX_EFFECT_LIMIT, FxEffect::HANDLE_SCALE>(
            system->effects, effect);
    if (effectHandle == FX_INVALID_HANDLE)
        return nullptr;
    return &State().effectOwnerAdmissionBlocked
        [effectHandle / effectHandleStride];
}

void __cdecl FX_LinkSystemBuffers(FxSystem *system,
                                  FxSystemBuffers *systemBuffers)
{
    system->elems = systemBuffers->elems;
    system->effects = systemBuffers->effects;
    system->trails = systemBuffers->trails;
    system->trailElems = systemBuffers->trailElems;
    system->visState = systemBuffers->visState;
    system->deferredElems = systemBuffers->deferredElems;
}

volatile std::int32_t *FX_GetArchiveGate(
    const FxSystem *const system) noexcept
{
    if (system != &State().system)
        return nullptr;
    return &State().archiveGate;
}

bool __cdecl FX_ValidateArchiveExclusiveState(
    const FxSystem *const system) noexcept
{
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    return system && gate
        && *gate
            == static_cast<std::int32_t>(
                fx::archive::ArchiveGateValue::Exclusive)
        && Sys_AtomicLoad(&system->iteratorCount) == -1;
}

bool __cdecl FX_BeginArchive(FxSystem *const system,
                             const std::uint32_t expectedGeneration)
{
    HarnessState &state = State();
    ++state.beginArchiveCalls;
    if (!system || system != &state.system)
        return false;
    if (FX_GetCooperativeIteratorGeneration(system)
            != expectedGeneration
        || state.failNextBeginArchive)
    {
        return false;
    }
    state.archiveGate = static_cast<std::int32_t>(
        fx::archive::ArchiveGateValue::Exclusive);
    ++state.beginArchiveSuccesses;
    return true;
}

bool __cdecl FX_BeginArchive(FxSystem *const system)
{
    return system
        && FX_BeginArchive(
            system, FX_GetCooperativeIteratorGeneration(system));
}

bool __cdecl FX_EndArchive(FxSystem *const system)
{
    HarnessState &state = State();
    ++state.endArchiveCalls;
    if (!system || system != &state.system)
        return false;
    state.archiveGate = static_cast<std::int32_t>(
        fx::archive::ArchiveGateValue::Open);
    return true;
}

fx::physics::BodySidecar *FX_GetPhysicsBodySidecar(
    FxSystem *const system)
{
    HarnessState &state = State();
    ++state.physicsSidecarCalls;
    if (system != &state.system)
        return nullptr;
    return &state.physicsSidecar;
}

const fx::physics::BodySidecar *FX_GetPhysicsBodySidecar(
    const FxSystem *const system)
{
    HarnessState &state = State();
    ++state.physicsSidecarCalls;
    if (system != &state.system)
        return nullptr;
    return &state.physicsSidecar;
}

#ifdef KISAK_FX_RESTORE_RIG
// Production FX_ForEachEffectDef (fx_load_obj.cpp) is enrolled in this
// rig and walks the real fx_load.effectDefs table.
#else
void __cdecl FX_ForEachEffectDef(
    void(__cdecl *callback)(const FxEffectDef *, void *),
    void *data)
{
    HarnessState &state = State();
    ++state.forEachEffectDefCalls;
    (void)callback;
    (void)data;
    // Fast-file capture never routes here; a recorded call is the
    // observable routing regression the test can assert on.
}
#endif

void FX_ErrorCleanup()
{
    ++State().errorCleanupCalls;
}

#ifdef KISAK_FX_RESTORE_RIG
// --- production fx_load_obj.cpp LoadObj-path fail-closed stubs -----
// The production FX_Register (fx_load_obj.cpp) is enrolled and takes
// its FastFile branch (useFastFile=true in this rig). The LoadObj
// branch and its transitive asset-conversion dependencies are
// unreachable in this configuration, but the linker still needs their
// symbols. Each stub aborts loudly through the production assert
// channel: reaching one means the fast-file routing invariant broke.
void __cdecl FS_FreeFile(char *buffer)
{
    (void)buffer;
    MyAssertHandler(".\\tests\\fx_restore_entry_stubs.cpp", 0, 0,
                    "%s", "LoadObj-path FS_FreeFile reached in fast-file rig");
}

int __cdecl FS_ReadFile(const char *qpath, void **buffer)
{
    (void)qpath;
    (void)buffer;
    MyAssertHandler(".\\tests\\fx_restore_entry_stubs.cpp", 0, 0,
                    "%s", "LoadObj-path FS_ReadFile reached in fast-file rig");
    return 0;
}

const FxEffectDef *__cdecl FX_Convert(const FxEditorEffectDef *editorEffect,
                                      void *(*Alloc)(unsigned int))
{
    (void)editorEffect;
    (void)Alloc;
    MyAssertHandler(".\\tests\\fx_restore_entry_stubs.cpp", 0, 0,
                    "%s", "LoadObj-path FX_Convert reached in fast-file rig");
    return nullptr;
}

XModel *__cdecl FX_RegisterModel(const char *modelName)
{
    (void)modelName;
    MyAssertHandler(".\\tests\\fx_restore_entry_stubs.cpp", 0, 0,
                    "%s", "LoadObj-path FX_RegisterModel reached in fast-file rig");
    return nullptr;
}

const FxCurve *__cdecl FxCurve_AllocAndCreateWithKeys(float *keyArray,
                                                      int dimensionCount,
                                                      int keyCount)
{
    (void)keyArray;
    (void)dimensionCount;
    (void)keyCount;
    MyAssertHandler(".\\tests\\fx_restore_entry_stubs.cpp", 0, 0,
                    "%s", "LoadObj-path FxCurve_AllocAndCreateWithKeys reached in fast-file rig");
    return nullptr;
}

PhysPreset *__cdecl PhysPresetPrecache(const char *name, void *(*Alloc)(int))
{
    (void)name;
    (void)Alloc;
    MyAssertHandler(".\\tests\\fx_restore_entry_stubs.cpp", 0, 0,
                    "%s", "LoadObj-path PhysPresetPrecache reached in fast-file rig");
    return nullptr;
}
#endif

// --- production fx_system.cpp gate-aware critical section ----------
// Verbatim semantics (fx_system.cpp FX_EnterArchiveAwarePool-
// CriticalSection) against the single harness gate cell.

void FX_EnterArchiveAwarePoolCriticalSection()
{
    for (;;)
    {
        while (fx::archive::ArchiveGateBlocksAllocatorAdmission(
            static_cast<fx::archive::ArchiveGateValue>(
                Sys_AtomicLoad(&State().archiveGate))))
        {
            std::this_thread::yield();
        }
        Sys_EnterCriticalSection(CRITSECT_FX_ALLOC);
        if (!fx::archive::ArchiveGateBlocksAllocatorAdmission(
                static_cast<fx::archive::ArchiveGateValue>(
                    Sys_AtomicLoad(&State().archiveGate))))
        {
            return;
        }
        Sys_LeaveCriticalSection(CRITSECT_FX_ALLOC);
    }
}

// --- production fx_system.cpp reset / publication helpers ----------

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

bool __cdecl FX_EffectTableRestoreLifecycleIsCurrent(
    const FxSystem *const system,
    const std::uint32_t expectedGeneration) noexcept
{
    volatile std::int32_t *const gate = FX_GetArchiveGate(system);
    // This predicate is also the restore lease's pre-CAS check. Do not sample
    // mutable non-atomic FxSystem fields until the post-CAS handshake has
    // excluded lifecycle mutation; later archive admission validates them.
    return system && gate
        && Sys_AtomicLoad(gate)
            == static_cast<std::int32_t>(
                fx::archive::ArchiveGateValue::Open)
        && FX_GetCooperativeIteratorGeneration(system)
            == expectedGeneration;
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

// --- production fx_system.cpp physics sidecar drain -----------------
// Production drains a live body one at a time through Phys_ObjDestroy
// (an ODE world transaction). The harness hosts no ODE world, so
// Phys_ObjDestroy is the counted fail-closed stub below and the drain
// loop keeps production shape: an uninitialized sidecar resets to the
// initialized-empty state; a validate failure is terminal.

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

// --- production fx_system.cpp pool-state rebuild --------------------

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

// --- production physics globals + fail-closed Phys_* boundary -------
// The enrolled fx_archive.cpp reads physGlob.worldData[PHYS_WORLD_FX]
// .timeLastUpdate for token staleness math; the harness world has no
// physics time, so the zero-initialized world data reads as "stale"
// (the honest state for a world with no live simulation).

PhysGlob physGlob;

namespace
{
void HarnessRefusePhysicsTransaction()
{
    ++State().physicsRejectionCalls;
}
} // namespace

PhysBodyModelCreateStatus __cdecl
Phys_TryCreateBodyFromStateAndXModelLockedNoReport(
    PhysWorld worldIndex,
    const BodyState *state,
    const XModel *model,
    dxBody **outBody) noexcept
{
    (void)worldIndex;
    (void)state;
    (void)model;
    if (outBody)
        *outBody = nullptr;
    HarnessRefusePhysicsTransaction();
    return PhysBodyModelCreateStatus::InvalidArgument;
}

PhysBodyRollbackStatus __cdecl Phys_TryGetBodyModelResourceDemand(
    const XModel *model,
    PhysBodyResourceDemand *outDemand) noexcept
{
    (void)model;
    (void)outDemand;
    HarnessRefusePhysicsTransaction();
    return PhysBodyRollbackStatus::InvalidArgument;
}

PhysBodyRollbackStatus __cdecl
Phys_TryGetFreeResourceCapacityLockedNoReport(
    PhysBodyResourceDemand *outCapacity) noexcept
{
    (void)outCapacity;
    HarnessRefusePhysicsTransaction();
    return PhysBodyRollbackStatus::InvalidArgument;
}

PhysBodyRollbackStatus __cdecl Phys_TryCaptureBodyStateLocked(
    PhysWorld worldIndex,
    dxBody *body,
    BodyState *outState) noexcept
{
    (void)worldIndex;
    (void)body;
    if (outState)
        memset(outState, 0, sizeof(*outState));
    HarnessRefusePhysicsTransaction();
    return PhysBodyRollbackStatus::InvalidArgument;
}

PhysBodyRollbackStatus __cdecl Phys_TryBuildBodyRollbackRecipeLocked(
    PhysWorld worldIndex,
    dxBody *body,
    const XModel *model,
    PhysBodyRollbackRecipe *outRecipe) noexcept
{
    (void)worldIndex;
    (void)body;
    (void)model;
    (void)outRecipe;
    HarnessRefusePhysicsTransaction();
    return PhysBodyRollbackStatus::InvalidArgument;
}

PhysBodyRollbackStatus __cdecl
Phys_TryValidateBodyDestroyLockedNoReport(
    PhysWorld worldIndex,
    dxBody *body) noexcept
{
    (void)worldIndex;
    (void)body;
    HarnessRefusePhysicsTransaction();
    return PhysBodyRollbackStatus::InvalidArgument;
}

PhysBodyRollbackStatus __cdecl Phys_TryDestroyBodyLockedNoReport(
    PhysWorld worldIndex,
    dxBody *body) noexcept
{
    (void)worldIndex;
    (void)body;
    HarnessRefusePhysicsTransaction();
    return PhysBodyRollbackStatus::InvalidArgument;
}

void __cdecl Phys_ObjDestroy(PhysWorld worldIndex, dxBody *id)
{
    (void)worldIndex;
    (void)id;
    HarnessRefusePhysicsTransaction();
}

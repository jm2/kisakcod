// fx_restore_entry_graph_stubs: the pool-graph rebuild / validation
// machinery and the fail-closed Phys_* boundary for the production FX
// archive save / restore enrollment (ki-458h / #125).
//
// Behavior-preserving file split from fx_restore_entry_stubs.cpp
// (mirroring the fuzz-fastfile gate split): every definition here
// keeps its production signature, linkage and body semantics - the
// pool-indexed lookups still reduce to the single harness slot, and
// the rebuild / validation algorithms remain the production
// header-inlined FxPoolRebuildAllocationStateLocked /
// FxValidatePoolAllocationGraphWithScratch driven against the harness
// slot.
//
// Shared gate discipline: both the rebuild and the validation enter
// the production fx_system.cpp gate-aware critical section
// (FX_EnterArchiveAwarePoolCriticalSection, below) under the same
// admission contract, so the shared EnterGateAwarePoolSection helper
// spells the refusal + entry block once for both callers.
//
// Native physics (Phys_* family) is FAIL-CLOSED: the harness hosts no
// ODE world, so every transaction is refused, counted in
// HarnessState.physicsRejectionCalls and returns its failure enum.
// A pristine 0-effect archive never reaches these paths; a call means
// the test created live bodies through a path it must not.

#include "fx_restore_entry_harness.hpp"

#include <EffectsCore/fx_archive_gate_control.h>
#include <EffectsCore/fx_pool.h>
#include <EffectsCore/fx_pool_graph.h>

#include <physics/phys_local.h>
#include <qcommon/sys_sync.h>

#include <thread>

using namespace fx_restore_entry_harness;

#ifdef KISAK_FX_RESTORE_RIG
// --- production fx_load_obj.cpp LoadObj-path fail-closed stubs -----
// The production FX_Register (fx_load_obj.cpp) is enrolled in this
// rig and takes its FastFile branch (useFastFile=true in this rig).
// The LoadObj branch and its transitive asset-conversion dependencies
// are unreachable in this configuration, but the linker still needs
// their symbols. Each stub aborts loudly through the production assert
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

namespace
{
// The shared gate-aware section entry for the pool-graph rebuild and
// validation: refuse when the gate blocks allocator admission without
// archive ownership, otherwise take the exclusive critical section
// under archive ownership or the archive-aware admission loop. This
// is the identical block both callers spelled inline before the
// extraction; false is returned exactly where the inline versions
// returned false before touching the pools.
bool EnterGateAwarePoolSection(FxSystem *const system)
{
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
    return true;
}

// The three live pool counts must equal the rebuilt counts for the
// rebuild to publish; identical comparison, identical operand order.
bool PoolRebuildCountsMatchLive(
    FxSystem *const system,
    const volatile std::int32_t *const rebuiltElemCount,
    const volatile std::int32_t *const rebuiltTrailCount,
    const volatile std::int32_t *const rebuiltTrailElemCount)
{
    return Sys_AtomicLoad(&system->activeElemCount)
            == Sys_AtomicLoad(rebuiltElemCount)
        && Sys_AtomicLoad(&system->activeTrailCount)
            == Sys_AtomicLoad(rebuiltTrailCount)
        && Sys_AtomicLoad(&system->activeTrailElemCount)
            == Sys_AtomicLoad(rebuiltTrailElemCount);
}

// Every effect's owner-admission word is re-derived from its live
// status word (the blocked bit), exactly as the publish path spelled
// before the extraction.
void RefreshEffectOwnerAdmissionWords(FxSystem *const system)
{
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
                (effectStatus & FX_STATUS_OWNER_ADMISSION_BLOCKED) != 0
                    ? 1
                    : 0);
        }
    }
}

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

    if (!EnterGateAwarePoolSection(system))
        return false;
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
        && PoolRebuildCountsMatchLive(system,
                                      &rebuiltElemCount,
                                      &rebuiltTrailCount,
                                      &rebuiltTrailElemCount);
    if (valid)
    {
        *states = rebuilt;
        RefreshEffectOwnerAdmissionWords(system);
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

    if (!EnterGateAwarePoolSection(system))
        return false;
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

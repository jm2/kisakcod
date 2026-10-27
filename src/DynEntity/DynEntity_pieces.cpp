#include "DynEntity_client.h"
#include <qcommon/sys_local.h>
#include <gfx_d3d/r_dpvs.h>
#include <universal/profile.h>
#include <universal/phys_obj_id.h>

#include <cmath>
#include <cstdlib>

const dvar_t *dynEntPieces_velocity;
const dvar_t *dynEntPieces_angularVelocity;
const dvar_t *dynEntPieces_impactForce;

int32_t numPieces;
BreakablePiece g_breakablePieces[100];

namespace
{
int32_t g_pieceRecycleCursor;

struct DynEntPiecesPhysSpawnResult
{
    dxBody *body;
    PhysBodyModelCreateStatus status;
    PhysBodyCreateResourceFailure resourceFailure;
    bool cleanupFailed;
};

// The caller owns the outer CRITSECT_PHYSICS.  Every fallible pool mutation
// is silent, and a geom failure retires the fresh body before returning.
DynEntPiecesPhysSpawnResult DynEntPieces_TrySpawnPhysObjLockedNoReport(
    const float *const mins,
    const float *const maxs,
    const float *const position,
    const float *const quat,
    const float *const velocity,
    const float *const angularVelocity,
    const PhysPreset *const physPreset) noexcept
{
    DynEntPiecesPhysSpawnResult result{
        nullptr,
        PhysBodyModelCreateStatus::InvalidArgument,
        PhysBodyCreateResourceFailure::None,
        false};
    if (!physPreset)
        return result;

    result.status = Phys_TryObjCreateLockedNoReport(
        PHYS_WORLD_FX,
        position,
        quat,
        velocity,
        physPreset,
        &result.body,
        &result.resourceFailure);
    if (result.status != PhysBodyModelCreateStatus::Success)
    {
        result.cleanupFailed =
            result.status == PhysBodyModelCreateStatus::CleanupFailed;
        return result;
    }

    result.status = Phys_TryObjAddGeomBoxLockedNoReport(
        PHYS_WORLD_FX, result.body, mins, maxs);
    if (result.status != PhysBodyModelCreateStatus::Success)
    {
        const bool geomCleanupFailed =
            result.status == PhysBodyModelCreateStatus::CleanupFailed;
        const bool bodyCleanupFailed = Phys_TryDestroyBodyLockedNoReport(
            PHYS_WORLD_FX, result.body) != PhysBodyRollbackStatus::Success;
        result.cleanupFailed = geomCleanupFailed || bodyCleanupFailed;
        result.body = nullptr;
        return result;
    }

    if (!Phys_TryObjSetAngularVelocityLockedNoReport(
            result.body, angularVelocity))
    {
        result.status = PhysBodyModelCreateStatus::InvalidArgument;
        result.cleanupFailed = Phys_TryDestroyBodyLockedNoReport(
            PHYS_WORLD_FX, result.body) != PhysBodyRollbackStatus::Success;
        result.body = nullptr;
    }
    return result;
}

// The caller owns CRITSECT_PHYSICS.
dxBody *DynEntPieces_ReleaseSlotLocked(int32_t index)
{
    BreakablePiece &piece = g_breakablePieces[index];
    dxBody *retiredBody = nullptr;
    const phys_obj_id::BodyResult released =
        g_breakablePieceBodySidecar.ReleaseByOwner(
            static_cast<phys_obj_id::OwnerIndex>(index));
    if (released)
        retiredBody = static_cast<dxBody *>(released.body);
    piece.active = false;
    piece.physObjId = phys_obj_id::INVALID_BODY_TOKEN;
    piece.model = nullptr;
    piece.lightingHandle = 0;
    return retiredBody;
}
} // namespace

DynEntPieceSlotCommit __cdecl DynEntPieces_CommitSlotLocked(
    dxBody *body,
    const XModel *model)
{
    DynEntPieceSlotCommit commit{false, nullptr};

    int32_t index;
    if (numPieces < 100)
    {
        index = numPieces;
    }
    else
    {
        // The pool is full: recycle the next slot round-robin so a session
        // that breaks more than 100 pieces keeps producing them. Releasing
        // the old binding rolls the generation forward, so any token still
        // held for that slot resolves as stale rather than as the new body.
        index = g_pieceRecycleCursor;
        g_pieceRecycleCursor = (g_pieceRecycleCursor + 1) % 100;
        commit.retiredBody = DynEntPieces_ReleaseSlotLocked(index);
    }

    const phys_obj_id::TokenResult bind =
        g_breakablePieceBodySidecar.Bind(
            static_cast<phys_obj_id::OwnerIndex>(index), body);
    if (!bind)
        return commit;

    BreakablePiece &piece = g_breakablePieces[index];
    piece.physObjId = bind.token;
    piece.model = model;
    piece.lightingHandle = 0;
    piece.active = true;
    if (index == numPieces)
        ++numPieces;
    commit.committed = true;
    return commit;
}

void __cdecl DynEntPieces_Shutdown()
{
    dxBody *retiredBodies[100];
    int32_t retiredCount = 0;

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    for (int32_t index = 0; index < 100; ++index)
    {
        dxBody *const retiredBody = DynEntPieces_ReleaseSlotLocked(index);
        if (retiredBody)
            retiredBodies[retiredCount++] = retiredBody;
    }
    numPieces = 0;
    g_pieceRecycleCursor = 0;
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);

    // Phys_ObjDestroy takes CRITSECT_PHYSICS itself, so the bodies leave the
    // sidecar under the lock but are destroyed after it is released.
    for (int32_t index = 0; index < retiredCount; ++index)
        Phys_ObjDestroy(PHYS_WORLD_FX, retiredBodies[index]);
}

void __cdecl DynEntPieces_RegisterDvars()
{
    DvarLimits min; // [esp+Ch] [ebp-10h]
    DvarLimits mina; // [esp+Ch] [ebp-10h]
    DvarLimits minb; // [esp+Ch] [ebp-10h]

    min.value.max = 1000.0;
    min.value.min = -1000.0;
    dynEntPieces_velocity = Dvar_RegisterVec3(
        "dynEntPieces_velocity",
        0.0,
        0.0,
        0.0,
        min,
        DVAR_CHEAT,
        "Initial breakable pieces velocity");
    mina.value.max = 180.0;
    mina.value.min = -180.0;
    dynEntPieces_angularVelocity = Dvar_RegisterVec3(
        "dynEntPieces_angularVelocity",
        0.0,
        0.0,
        0.0,
        mina,
        DVAR_CHEAT,
        "Initial breakable pieces angular velocity");
    minb.value.max = 1000000.0;
    minb.value.min = 0.0;
    dynEntPieces_impactForce = Dvar_RegisterFloat(
        "dynEntPieces_impactForce",
        1000.0,
        minb,
        DVAR_CHEAT,
        "Force applied when breakable is destroyed");
}

void __cdecl DynEntPieces_AddDrawSurfs()
{
    GfxScaledPlacement placement; // [esp+30h] [ebp-24h] BYREF
    int32_t i; // [esp+50h] [ebp-4h]

    PROF_SCOPED("DynEntCl_AddBreakableDrawSurfs");
    for (i = 0; i < numPieces; ++i)
    {
        if (g_breakablePieces[i].active)
        {
            // The sidecar contract requires Resolve to run under
            // CRITSECT_PHYSICS: an unlocked ReadResolve can race a
            // Bind/Release on this slot and publish a dangling body.
            Sys_EnterCriticalSection(CRITSECT_PHYSICS);
            dxBody *const physObjIdBody = phys_obj_id::ReadResolve<dxBody>(
                g_breakablePieceBodySidecar,
                g_breakablePieces[i].physObjId);
            if (physObjIdBody)
            {
                Phys_ObjGetInterpolatedState(
                    PHYS_WORLD_FX,
                    physObjIdBody,
                    placement.base.origin,
                    placement.base.quat);
                placement.scale = 1.0;
                Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
                R_FilterXModelIntoScene(g_breakablePieces[i].model, &placement, 0, &g_breakablePieces[i].lightingHandle);
            }
            else
            {
                Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
            }
        }
    }
}

void __cdecl DynEntPieces_SpawnPieces(
    int32_t localClientNum,
    const XModelPieces *pieces,
    const float *origin,
    const float (*axis)[3],
    const float *hitPos,
    const float *hitDir)
{
    int32_t pieceIndex; // [esp+0h] [ebp-4h]

    if (!pieces)
        MyAssertHandler(".\\DynEntity\\DynEntity_pieces.cpp", 184, 0, "%s", "pieces");
    for (pieceIndex = 0; pieceIndex < pieces->numpieces; ++pieceIndex)
        DynEntPieces_SpawnPhysicsModel(
            localClientNum,
            pieces->pieces[pieceIndex].model,
            pieces->pieces[pieceIndex].offset,
            origin,
            axis,
            hitPos,
            hitDir);
}

bool __cdecl DynEntPieces_SpawnPhysicsModel(
    int32_t localClientNum,
    const XModel *model,
    const float *offset,
    const float *origin,
    const float (*axis)[3],
    const float *hitPos,
    const float *hitDir)
{
    bool result; // al
    float forceDir[3]; // [esp+10h] [ebp-5Ch] BYREF
    float velocity[3]; // [esp+1Ch] [ebp-50h] BYREF
    float angularVelocity[3]; // [esp+28h] [ebp-44h] BYREF
    dxBody *physObjId; // [esp+34h] [ebp-38h]
    float mins[3]; // [esp+38h] [ebp-34h] BYREF
    float quat[4]; // [esp+44h] [ebp-28h] BYREF
    float maxs[3]; // [esp+54h] [ebp-18h] BYREF
    float worldOffset[3]; // [esp+60h] [ebp-Ch] BYREF
    float impactPosition[3];
    float impactDirection[3];

    if (!model)
    {
        MyAssertHandler(".\\DynEntity\\DynEntity_pieces.cpp", 131, 0, "%s", "model");
        return false;
    }
    XModelGetBounds(model, mins, maxs);
    if (maxs[0] == mins[0] || maxs[1] == mins[1] || maxs[2] == mins[2])
    {
        Com_PrintWarning(1, "Failed to spawn pieces model '%s'.  No bounds.\n", model->name);
        return 0;
    }
    else
    {
        if (!model->physPreset)
        {
            Com_PrintWarning(
                1,
                "Failed to spawn pieces model '%s'.  It is missing a physics preset.\n",
                model->name ? model->name : "<unnamed>");
            return false;
        }
        const dvar_t *const impactForceDvar = dynEntPieces_impactForce;
        if (!hitPos || !hitDir || !impactForceDvar
            || impactForceDvar->type != DVAR_TYPE_FLOAT)
        {
            Com_PrintWarning(
                1,
                "Failed to create physics object for '%s': invalid impact inputs.\n",
                model->name ? model->name : "<unnamed>");
            return false;
        }
        for (std::size_t component = 0; component < 3; ++component)
        {
            impactPosition[component] = hitPos[component];
            impactDirection[component] = hitDir[component];
            if (!std::isfinite(impactPosition[component])
                || !std::isfinite(impactDirection[component]))
            {
                Com_PrintWarning(
                    1,
                    "Failed to create physics object for '%s': invalid impact inputs.\n",
                    model->name ? model->name : "<unnamed>");
                return false;
            }
        }
        const float impactForce = impactForceDvar->current.value;
        const float spreadFraction =
            model->physPreset->piecesSpreadFraction;
        const float bulletForceScale =
            model->physPreset->bulletForceScale;
        if (!std::isfinite(impactForce)
            || !std::isfinite(spreadFraction)
            || !std::isfinite(bulletForceScale))
        {
            Com_PrintWarning(
                1,
                "Failed to create physics object for '%s': invalid impact inputs.\n",
                model->name ? model->name : "<unnamed>");
            return false;
        }
        MatrixTransformVector(offset, *(const mat3x3*)axis, worldOffset);
        Vec3Add(worldOffset, origin, worldOffset);
        AxisToQuat(axis, quat);
        velocity[0] = dynEntPieces_velocity->current.value;
        velocity[1] = dynEntPieces_velocity->current.vector[1];
        velocity[2] = dynEntPieces_velocity->current.vector[2];
        angularVelocity[0] = dynEntPieces_angularVelocity->current.value;
        angularVelocity[1] = dynEntPieces_angularVelocity->current.vector[1];
        angularVelocity[2] = dynEntPieces_angularVelocity->current.vector[2];
        velocity[2] = velocity[2] + model->physPreset->piecesUpwardVelocity;
        Sys_EnterCriticalSection(CRITSECT_PHYSICS);
        DynEntPiecesPhysSpawnResult spawnResult =
            DynEntPieces_TrySpawnPhysObjLockedNoReport(
                mins,
                maxs,
                worldOffset,
                quat,
                velocity,
                angularVelocity,
                model->physPreset);
        if (spawnResult.status == PhysBodyModelCreateStatus::Success)
        {
            DynEntPieces_CalcForceDir(
                impactDirection,
                spreadFraction,
                forceDir);
            if (!Phys_TryObjBulletImpactLockedNoReport(
                    PHYS_WORLD_DYNENT,
                    spawnResult.body,
                    impactPosition,
                    forceDir,
                    impactForce,
                    bulletForceScale))
            {
                spawnResult.status =
                    PhysBodyModelCreateStatus::InvalidArgument;
                spawnResult.cleanupFailed =
                    Phys_TryDestroyBodyLockedNoReport(
                        PHYS_WORLD_FX,
                        spawnResult.body)
                    != PhysBodyRollbackStatus::Success;
                spawnResult.body = nullptr;
            }
        }
        // Binding and publishing run inside the physics lock span per the
        // sidecar contract (same span as creation and the bullet-impact
        // rollback). On a full pool CommitSlotLocked evicts the oldest slot
        // and returns its body; that body is destroyed after the lock.
        DynEntPieceSlotCommit pieceCommit{false, nullptr};
        if (spawnResult.status == PhysBodyModelCreateStatus::Success)
        {
            pieceCommit =
                DynEntPieces_CommitSlotLocked(spawnResult.body, model);
            if (!pieceCommit.committed)
            {
                spawnResult.status =
                    PhysBodyModelCreateStatus::InvalidArgument;
                spawnResult.cleanupFailed =
                    Phys_TryDestroyBodyLockedNoReport(
                        PHYS_WORLD_FX,
                        spawnResult.body)
                    != PhysBodyRollbackStatus::Success;
                spawnResult.body = nullptr;
            }
        }
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);

        if (pieceCommit.retiredBody)
            Phys_ObjDestroy(PHYS_WORLD_FX, pieceCommit.retiredBody);
        if (spawnResult.cleanupFailed)
            std::abort();
        physObjId = spawnResult.body;
        if (spawnResult.status == PhysBodyModelCreateStatus::Success)
        {
            result = 1;
        }
        else
        {
            Phys_ReportBodyModelCreateFailure(
                spawnResult.status, spawnResult.resourceFailure);
            Com_PrintWarning(
                1,
                "Failed to create physics object for '%s'.\n",
                model->name ? model->name : "<unnamed>");
            return false;
        }
    }
    return result;
}

dxBody *__cdecl DynEntPieces_SpawnPhysObj(
    const char *modelName,
    const float *mins,
    const float *maxs,
    float *position,
    float *quat,
    float *velocity,
    float *angularVelocity,
    const PhysPreset *physPreset)
{
    const char *const safeModelName = modelName ? modelName : "<unnamed>";
    if (numPieces >= 100 || !physPreset)
    {
        Com_PrintWarning(
            1,
            "Failed to spawn pieces model '%s'.  It is missing physics preset.\n",
            safeModelName);
        return nullptr;
    }

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    const DynEntPiecesPhysSpawnResult spawnResult =
        DynEntPieces_TrySpawnPhysObjLockedNoReport(
            mins,
            maxs,
            position,
            quat,
            velocity,
            angularVelocity,
            physPreset);
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);

    if (spawnResult.cleanupFailed)
        std::abort();
    if (spawnResult.status != PhysBodyModelCreateStatus::Success)
    {
        Phys_ReportBodyModelCreateFailure(
            spawnResult.status, spawnResult.resourceFailure);
        Com_PrintWarning(
            1,
            "Failed to create physics object for '%s'.\n",
            safeModelName);
        return nullptr;
    }

    return spawnResult.body;
}

void __cdecl DynEntPieces_CalcForceDir(const float *hitDir, float spreadFraction, float *forceDir)
{
    int32_t v3; // [esp+8h] [ebp-18h]
    int32_t v4; // [esp+Ch] [ebp-14h]
    int32_t v5; // [esp+10h] [ebp-10h]
    float outDir[3]; // [esp+14h] [ebp-Ch] BYREF

    v5 = Kisak_rand();
    outDir[0] = (double)v5 / 32767.0 + (double)v5 / 32767.0 - 1.0;
    v4 = Kisak_rand();
    outDir[1] = (double)v4 / 32767.0 + (double)v4 / 32767.0 - 1.0;
    v3 = Kisak_rand();
    outDir[2] = (double)v3 / 32767.0 + (double)v3 / 32767.0 - 1.0;
    Vec3Lerp(hitDir, outDir, spreadFraction, forceDir);
}

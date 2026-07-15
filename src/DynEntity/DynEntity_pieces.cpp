#include "DynEntity_client.h"
#include <win32/win_local.h>
#include <gfx_d3d/r_dpvs.h>
#include <universal/profile.h>

#include <cmath>
#include <cstdlib>

const dvar_t *dynEntPieces_velocity;
const dvar_t *dynEntPieces_angularVelocity;
const dvar_t *dynEntPieces_impactForce;

int32_t numPieces;
BreakablePiece g_breakablePieces[100];

namespace
{
struct DynEntPiecesPhysSpawnResult
{
    dxBody *body;
    PhysBodyModelCreateStatus status;
    PhysBodyCreateResourceFailure resourceFailure;
    bool capacityExceeded;
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
        false,
        false};
    if (numPieces >= 100)
    {
        result.capacityExceeded = true;
        return result;
    }
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
} // namespace

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
            Sys_EnterCriticalSection(CRITSECT_PHYSICS);
            Phys_ObjGetInterpolatedState(
                PHYS_WORLD_FX,
                (dxBody *)g_breakablePieces[i].physObjId,
                placement.base.origin,
                placement.base.quat);
            placement.scale = 1.0;
            Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
            R_FilterXModelIntoScene(g_breakablePieces[i].model, &placement, 0, &g_breakablePieces[i].lightingHandle);
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
        Sys_LeaveCriticalSection(CRITSECT_PHYSICS);

        if (spawnResult.cleanupFailed)
            std::abort();
        if (spawnResult.capacityExceeded)
        {
            Com_PrintWarning(
                1,
                "Failed to create physics object for '%s': piece capacity exhausted.\n",
                model->name ? model->name : "<unnamed>");
            return false;
        }
        physObjId = spawnResult.body;
        if (spawnResult.status == PhysBodyModelCreateStatus::Success)
        {
            g_breakablePieces[numPieces].physObjId = (int32_t)(uintptr_t)physObjId;
            g_breakablePieces[numPieces].model = model;
            result = 1;
            g_breakablePieces[numPieces].lightingHandle = 0;
            g_breakablePieces[numPieces++].active = 1;
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
    if (spawnResult.capacityExceeded)
    {
        Com_PrintWarning(
            1,
            "Failed to create physics object for '%s': piece capacity exhausted.\n",
            safeModelName);
        return nullptr;
    }
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

    v5 = rand();
    outDir[0] = (double)v5 / 32767.0 + (double)v5 / 32767.0 - 1.0;
    v4 = rand();
    outDir[1] = (double)v4 / 32767.0 + (double)v4 / 32767.0 - 1.0;
    v3 = rand();
    outDir[2] = (double)v3 / 32767.0 + (double)v3 / 32767.0 - 1.0;
    Vec3Lerp(hitDir, outDir, spreadFraction, forceDir);
}

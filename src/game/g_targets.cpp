#ifndef KISAK_SP 
#error This file is for SinglePlayer only 
#endif

#include "g_local.h"
#include <server/sv_public.h>
#include "g_main.h"
#include <script/scr_vm.h>
#include <server/sv_game.h>
#include <script/scr_const.h>

#include <cmath>
#include <limits>

TargetGlob targGlob;

namespace
{
namespace target_protocol = bg::target_protocol;
constexpr int kMaxTargets = target_protocol::kMaxTargets;

void ResetTargetEntry(target_t *const target)
{
    if (target->ent)
        target->ent->flags &= ~FL_TARGET;

    target->ent = nullptr;
    target->offset[0] = 0.0f;
    target->offset[1] = 0.0f;
    target->offset[2] = 0.0f;
    target->materialIndex = target_protocol::kNoMaterial;
    target->offscreenMaterialIndex = target_protocol::kNoMaterial;
    target->flags = 0;
}

void InitializeTargetEntry(
    target_t *const target,
    gentity_s *const ent)
{
    ResetTargetEntry(target);
    target->ent = ent;
    ent->flags |= FL_TARGET;
}

bool CanEncodeLegacyOffset(const float (&offset)[3])
{
    for (const float component : offset)
    {
        if (!std::isfinite(component)
            || static_cast<double>(component)
                < static_cast<double>((std::numeric_limits<int>::min)())
            || static_cast<double>(component)
                > static_cast<double>((std::numeric_limits<int>::max)()))
        {
            return false;
        }
    }
    return true;
}
} // namespace

void __cdecl G_InitTargets()
{
    targGlob.targetCount = 0;

    for (int i = 0; i < kMaxTargets; ++i)
    {
        ResetTargetEntry(&targGlob.targets[i]);
        SV_SetConfigstring(CS_TARGETS + i, "");
    }
}

void __cdecl G_LoadTargets()
{
    struct StagedTarget
    {
        bool active;
        target_protocol::ParsedConfig config;
    };

    StagedTarget staged[kMaxTargets]{};
    unsigned int stagedCount = 0;
    char configString[MAX_INFO_STRING];

    // Validate the complete table before changing either the live entries or
    // their FL_TARGET bits. A corrupt save therefore cannot publish a partial
    // table even if Com_Error is intercepted by a test or recovery boundary.
    for (int targetIndex = 0; targetIndex < kMaxTargets; ++targetIndex)
    {
        SV_GetConfigstring(
            CS_TARGETS + targetIndex,
            configString,
            MAX_INFO_STRING);
        if (!configString[0])
            continue;

        const target_protocol::ConfigParseError error =
            target_protocol::ParseConfig(
                configString,
                MAX_GENTITIES,
                &staged[targetIndex].config);
        if (error != target_protocol::ConfigParseError::None)
        {
            Com_Error(
                ERR_DROP,
                "G_LoadTargets: target configstring %i is invalid (%s)",
                targetIndex,
                target_protocol::ConfigParseErrorName(error));
            return;
        }

        const int entityNumber = staged[targetIndex].config.entityNumber;
        if (!level.gentities[entityNumber].r.inuse)
        {
            Com_Error(
                ERR_DROP,
                "G_LoadTargets: target configstring %i references unused "
                "entity %i",
                targetIndex,
                entityNumber);
            return;
        }

        for (int previous = 0; previous < targetIndex; ++previous)
        {
            if (staged[previous].active
                && staged[previous].config.entityNumber == entityNumber)
            {
                Com_Error(
                    ERR_DROP,
                    "G_LoadTargets: target configstrings %i and %i both "
                    "reference entity %i",
                    previous,
                    targetIndex,
                    entityNumber);
                return;
            }
        }

        staged[targetIndex].active = true;
        ++stagedCount;
    }

    for (target_t &target : targGlob.targets)
        ResetTargetEntry(&target);

    for (int targetIndex = 0; targetIndex < kMaxTargets; ++targetIndex)
    {
        if (!staged[targetIndex].active)
            continue;

        const target_protocol::ParsedConfig &source =
            staged[targetIndex].config;
        target_t &target = targGlob.targets[targetIndex];
        InitializeTargetEntry(
            &target, &level.gentities[source.entityNumber]);
        target.offset[0] = source.offset[0];
        target.offset[1] = source.offset[1];
        target.offset[2] = source.offset[2];
        target.materialIndex = source.materialIndex;
        target.offscreenMaterialIndex = source.offscreenMaterialIndex;
        target.flags = source.flags;
    }
    targGlob.targetCount = stagedCount;
}

void __cdecl Scr_Target_SetShader()
{
    if (Scr_GetNumParam() < 2)
    {
        Scr_Error("Too few arguments\n");
        return;
    }

    gentity_s *const ent = Scr_GetEntity(0);
    const int targetIndex = GetTargetIdx(ent);
    if (targetIndex == kMaxTargets)
    {
        Scr_Error(va("Entity %i is not a target", ent->s.number));
        return;
    }

    const char *const materialName = Scr_GetString(1);
    target_t &target = targGlob.targets[targetIndex];
    target.materialIndex = *materialName
        ? G_MaterialIndex(materialName)
        : target_protocol::kNoMaterial;

    char configString[MAX_INFO_STRING];
    SV_GetConfigstring(
        CS_TARGETS + targetIndex,
        configString,
        MAX_INFO_STRING);
    Info_SetValueForKey(
        configString, "mat", va("%i", target.materialIndex));
    SV_SetConfigstring(CS_TARGETS + targetIndex, configString);
}

void __cdecl Scr_Target_SetOffscreenShader()
{
    if (Scr_GetNumParam() < 2)
    {
        Scr_Error("Too few arguments\n");
        return;
    }

    gentity_s *const ent = Scr_GetEntity(0);
    const int targetIndex = GetTargetIdx(ent);
    if (targetIndex == kMaxTargets)
    {
        Scr_Error(va("Entity %i is not a target", ent->s.number));
        return;
    }

    const char *const materialName = Scr_GetString(1);
    target_t &target = targGlob.targets[targetIndex];
    target.offscreenMaterialIndex = *materialName
        ? G_MaterialIndex(materialName)
        : target_protocol::kNoMaterial;

    char configString[MAX_INFO_STRING];
    SV_GetConfigstring(
        CS_TARGETS + targetIndex,
        configString,
        MAX_INFO_STRING);
    Info_SetValueForKey(
        configString,
        "offmat",
        va("%i", target.offscreenMaterialIndex));
    SV_SetConfigstring(CS_TARGETS + targetIndex, configString);
}

void __cdecl Scr_Target_GetArray()
{
    Scr_MakeArray();

    for (int targIdx = 0; targIdx < kMaxTargets; targIdx++)
    {
        if (targGlob.targets[targIdx].ent)
        {
            if (targGlob.targets[targIdx].ent->r.inuse)
            {
                Scr_AddEntity(targGlob.targets[targIdx].ent);
                Scr_AddArray();
            }
        }
    }
}

int __cdecl TargetIndex(gentity_s *ent)
{
    return GetTargetIdx(ent);
}

void __cdecl Scr_Target_IsTarget()
{
    gentity_s *ent; // [esp+4h] [ebp-4h]

    if (!Scr_GetNumParam())
    {
        Scr_Error("Too few arguments\n");
        return;
    }
    ent = Scr_GetEntity(0);
    if (TargetIndex(ent) == kMaxTargets)
        Scr_AddBool(0);
    else
        Scr_AddBool(1);
}

void __cdecl Scr_Target_Set()
{
    if (!Scr_GetNumParam())
    {
        Scr_Error("Too few arguments\n");
        return;
    }

    gentity_s *const ent = Scr_GetEntity(0);
    float requestedOffset[3]{};
    if (Scr_GetNumParam() > 1)
        Scr_GetVector(1u, requestedOffset);
    if (!CanEncodeLegacyOffset(requestedOffset))
    {
        Scr_ParamError(
            1u,
            "Target offset components must be finite signed 32-bit values");
        return;
    }

    int targetIndex = GetTargetIdx(ent);
    if (targetIndex == kMaxTargets)
    {
        if (targGlob.targetCount >= kMaxTargets)
        {
            Scr_Error("Maximum number of targets exceeded");
            return;
        }

        for (int candidate = 0; candidate < kMaxTargets; ++candidate)
        {
            if (!targGlob.targets[candidate].ent)
            {
                targetIndex = candidate;
                break;
            }
        }
        if (targetIndex == kMaxTargets)
        {
            Scr_Error("Target table count is inconsistent");
            return;
        }

        InitializeTargetEntry(&targGlob.targets[targetIndex], ent);
        ++targGlob.targetCount;
    }

    target_t &target = targGlob.targets[targetIndex];
    target.ent->flags |= FL_TARGET;
    target.offset[0] = requestedOffset[0];
    target.offset[1] = requestedOffset[1];
    target.offset[2] = requestedOffset[2];

    char configString[MAX_INFO_STRING]{};
    Info_SetValueForKey(configString, "ent", va("%i", ent->s.number));
    // Keep the retail x86 configstring/save representation: offsets are
    // truncated to signed decimal integers even though the live table is float.
    Info_SetValueForKey(
        configString,
        "offs",
        va(
            "%i %i %i",
            static_cast<int>(target.offset[0]),
            static_cast<int>(target.offset[1]),
            static_cast<int>(target.offset[2])));
    Info_SetValueForKey(
        configString, "mat", va("%i", target.materialIndex));
    Info_SetValueForKey(
        configString,
        "offmat",
        va("%i", target.offscreenMaterialIndex));
    Info_SetValueForKey(
        configString, "flags", va("%i", target.flags));
    SV_SetConfigstring(CS_TARGETS + targetIndex, configString);
}

bool Targ_Remove(gentity_s *ent)
{
    const int targetIndex = GetTargetIdx(ent);
    if (targetIndex == kMaxTargets)
        return false;

    ResetTargetEntry(&targGlob.targets[targetIndex]);
    if (targGlob.targetCount > 0)
        --targGlob.targetCount;
    else
        targGlob.targetCount = 0;

    SV_SetConfigstring(CS_TARGETS + targetIndex, "");
    return true;
}

void __cdecl Targ_RemoveAll()
{
    for (int targetIndex = 0; targetIndex < kMaxTargets; ++targetIndex)
    {
        ResetTargetEntry(&targGlob.targets[targetIndex]);
        SV_SetConfigstring(CS_TARGETS + targetIndex, "");
    }
    targGlob.targetCount = 0;
}

void __cdecl Scr_Target_Remove()
{
    unsigned int v0; // r4
    gentity_s *Entity; // r31
    const char *v2; // r3

    if (!Scr_GetNumParam())
    {
        Scr_Error("Too few arguments\n");
        return;
    }
    Entity = Scr_GetEntity(0);
    if (!(unsigned __int8)Targ_Remove(Entity))
    {
        v2 = va("Entity %i is not a target", Entity->s.number);
        Scr_Error(v2);
        return;
    }
}

int __cdecl G_WorldDirToScreenPos(
    const gentity_s *player,
    double fov_x,
    const float *worldDir,
    float *outScreenPos)
{
    long double v9; // fp2
    int result; // r3
    double v11; // fp28
    double v12; // fp27
    long double v13; // fp2
    double v14; // fp31
    double v15; // fp30
    float v16[4]; // [sp+50h] [-90h] BYREF
    float v17[6][3]; // [sp+60h] [-80h] BYREF

    if (fov_x <= 0.0)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\game\\g_targets.cpp", 360, 0, "%s", "fov_x > 0");
    AnglesToAxis(player->s.lerp.apos.trBase, v17);
    MatrixTransposeTransformVector(worldDir, (const mat3x3&)v17, v16);
    if (v16[0] <= 0.0)
        return 0;
    *(double *)&v9 = (float)((float)((float)fov_x * (float)0.017453292) * (float)0.5);
    v11 = (float)((float)((float)1.0 / v16[0]) * v16[1]);
    v12 = (float)((float)((float)1.0 / v16[0]) * v16[2]);
    v13 = tan(v9);
    v14 = (float)*(double *)&v13;
    v15 = (float)((float)*(double *)&v13 * (float)0.75);
    if (v14 <= 0.0)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\game\\g_targets.cpp", 373, 1, "%s", "tanHalfFovX > 0");
    if (v15 <= 0.0)
        MyAssertHandler("c:\\trees\\cod3\\cod3src\\src\\game\\g_targets.cpp", 374, 1, "%s", "tanHalfFovY > 0");
    result = 1;
    outScreenPos[0] = (float)((float)v11 / (float)v14) * (float)-320.0;
    outScreenPos[1] = (float)((float)v12 / (float)v15) * (float)-240.0;
    return result;
}

int __cdecl ScrGetTargetScreenPos(float *screenPos)
{
    float worldDir[3]; // [sp+50h] [-50h] BYREF

    if (Scr_GetNumParam() < 2)
    {
        Scr_Error("Too few arguments\n");
        return 0;
    }

    gentity_s *const targetEntity = Scr_GetEntity(0);
    const gentity_s *const player = Scr_GetEntity(1);
    if (!player->client)
    {
        Scr_ObjectError(va("entity %i is not a player", player->s.number));
        return 0;
    }

    const double fov_x = Scr_GetFloat(2);
    if (!(fov_x > 0.0) || !std::isfinite(fov_x))
    {
        Scr_ParamError(2u, "FOV must be positive");
        return 0;
    }

    const int targetIndex = GetTargetIdx(targetEntity);
    if (targetIndex == kMaxTargets)
    {
        Scr_Error(va(
            "Entity %i is not a target", targetEntity->s.number));
        return 0;
    }

    const target_t &target = targGlob.targets[targetIndex];
    worldDir[0] = target.ent->r.currentOrigin[0] + target.offset[0];
    worldDir[1] = target.ent->r.currentOrigin[1] + target.offset[1];
    worldDir[2] = target.ent->r.currentOrigin[2] + target.offset[2];

    worldDir[0] -= player->r.currentOrigin[0];
    worldDir[1] -= player->r.currentOrigin[1];
    worldDir[2] -= player->r.currentOrigin[2];
    worldDir[2] -= player->client->ps.viewHeightCurrent;
    return G_WorldDirToScreenPos(player, fov_x, worldDir, screenPos);
}

void __cdecl Scr_Target_IsInCircle()
{
    double Float; // fp31
    int v1; // r3
    float screenPos[2]; // [sp+50h] [-20h] BYREF
    //float v3; // [sp+54h] [-1Ch]

    Float = Scr_GetFloat(3);
    if (!(unsigned __int8)ScrGetTargetScreenPos(screenPos)
        || (v1 = 1, (float)((float)(screenPos[0] * screenPos[0]) + (float)(screenPos[1] * screenPos[1])) >= (double)(float)((float)Float * (float)Float)))
    {
        v1 = 0;
    }
    Scr_AddBool(v1);
}

void __cdecl Scr_Target_IsInRect()
{
    double Float; // fp31
    double v1; // fp30
    int v2; // r3
    float v3[2]; // [sp+50h] [-20h] BYREF

    Float = Scr_GetFloat(3);
    v1 = Scr_GetFloat(4);
    if (!(unsigned __int8)ScrGetTargetScreenPos(v3) || I_fabs(v3[0]) >= Float || (v2 = 1, I_fabs(v3[1]) >= v1))
        v2 = 0;
    Scr_AddBool(v2);
}

void __cdecl Scr_Target_StartLockOn()
{
    gentity_s *Entity; // r31
    double Float; // fp1
    int number; // r4
    const char *v5; // r3
    int v6; // [sp+50h] [-20h]

    Entity = Scr_GetEntity(0);
    Float = Scr_GetFloat(1);
    number = Entity->s.number;
    v6 = (int)(float)((float)Float * (float)1000.0);
    v5 = va("ret_lock_on %i %i", number, v6);
    SV_GameSendServerCommand(-1, v5);
}

void __cdecl Scr_Target_ClearLockOn()
{
    const char *v0; // r3

    v0 = va("ret_lock_on %i %i", ENTITYNUM_NONE, 0);
    SV_GameSendServerCommand(-1, v0);
}

int __cdecl GetTargetIdx(const gentity_s *ent)
{
    if (!ent)
        return kMaxTargets;

    for (int targetIndex = 0;
         targetIndex < kMaxTargets;
         ++targetIndex)
    {
        if (targGlob.targets[targetIndex].ent == ent)
            return targetIndex;
    }
    return kMaxTargets;
}

int __cdecl G_TargetGetOffset(const gentity_s *targ, float *result)
{
    if (!result)
        return 0;

    const int targetIndex = GetTargetIdx(targ);
    if (targetIndex == kMaxTargets)
    {
        result[0] = 0.0f;
        result[1] = 0.0f;
        result[2] = 0.0f;
        return 0;
    }
    else
    {
        *result = targGlob.targets[targetIndex].offset[0];
        result[1] = targGlob.targets[targetIndex].offset[1];
        result[2] = targGlob.targets[targetIndex].offset[2];
        return 1;
    }
}

int __cdecl G_TargetAttackProfileTop(const gentity_s *ent)
{
    const int targetIndex = GetTargetIdx(ent);
    if (targetIndex == kMaxTargets)
        return 0;
    return targGlob.targets[targetIndex].flags
        & target_protocol::kAttackProfileTop;
}

void __cdecl Scr_Target_SetAttackMode()
{
    if (Scr_GetNumParam() < 2)
    {
        Scr_Error("Too few arguments\n");
        return;
    }

    gentity_s *const ent = Scr_GetEntity(0);
    const int targetIndex = GetTargetIdx(ent);
    if (targetIndex == kMaxTargets)
    {
        Scr_Error(va("Entity %i is not a target", ent->s.number));
        return;
    }

    const unsigned int mode = Scr_GetConstString(1);
    if (mode == scr_const.top)
    {
        targGlob.targets[targetIndex].flags |=
            target_protocol::kAttackProfileTop;
    }
    else if (mode == scr_const.direct)
    {
        targGlob.targets[targetIndex].flags &=
            ~target_protocol::kAttackProfileTop;
    }
    else
    {
        Scr_Error("Incorrect mode name passed to target_setAttackMode().\n");
        return;
    }

    char configString[MAX_INFO_STRING];
    SV_GetConfigstring(
        CS_TARGETS + targetIndex,
        configString,
        MAX_INFO_STRING);
    Info_SetValueForKey(
        configString,
        "flags",
        va("%i", targGlob.targets[targetIndex].flags));
    SV_SetConfigstring(CS_TARGETS + targetIndex, configString);
}

void __cdecl Scr_Target_SetJavelinOnly()
{
    if ((unsigned int)Scr_GetNumParam() < 2)
    {
        Scr_Error("Too few arguments\n");
        return;
    }

    gentity_s *const ent = Scr_GetEntity(0);
    const int targetIndex = GetTargetIdx(ent);
    if (targetIndex == kMaxTargets)
    {
        Scr_Error(va("Entity %i is not a target", ent->s.number));
        return;
    }

    if (Scr_GetInt(1))
        targGlob.targets[targetIndex].flags |= target_protocol::kJavelinOnly;
    else
        targGlob.targets[targetIndex].flags &= ~target_protocol::kJavelinOnly;

    char configString[MAX_INFO_STRING];
    SV_GetConfigstring(
        CS_TARGETS + targetIndex,
        configString,
        MAX_INFO_STRING);
    Info_SetValueForKey(
        configString,
        "flags",
        va("%i", targGlob.targets[targetIndex].flags));
    SV_SetConfigstring(CS_TARGETS + targetIndex, configString);
}

#include "fx_system.h"
#include "fx_effect_table_restore.h"
#include "fx_iterator_atomic.h"
#include "fx_physics_sidecar.h"
#include "fx_pool.h"

#include <gfx_d3d/r_drawsurf.h>
#include <gfx_d3d/r_scene.h>
#include <gfx_d3d/r_dpvs.h>

#include <aim_assist/aim_assist.h>

#include <physics/phys_local.h>

#include <qcommon/sys_sync.h>
#include <universal/profile.h>
#include <universal/sys_atomic.h>

#include <cstring>
#include <cstdlib>
#include <limits>

namespace
{
struct FxCooperativeIteratorThreadState
{
    const FxSystem *system = nullptr;
    std::uint32_t generation = 0;
    std::uint32_t depth = 0;
};

thread_local FxCooperativeIteratorThreadState
    fx_cooperativeIteratorThreadState{};

// FxSystem is a frozen archive/binary layout, so camera publication ownership
// lives beside it. The runtime currently owns one FxSystem; keeping the gate
// external also makes the publication protocol independent of pointer width.
alignas(4) volatile std::int32_t fx_cameraPublicationIteratorCount = 0;

enum class FxCameraPublicationThreadMode : std::uint8_t
{
    None,
    Read,
    Write,
};

struct FxCameraPublicationThreadState
{
    const FxSystem *system = nullptr;
    std::uint32_t generation = 0;
    std::uint32_t depth = 0;
    FxCameraPublicationThreadMode mode =
        FxCameraPublicationThreadMode::None;
};

thread_local FxCameraPublicationThreadState
    fx_cameraPublicationThreadState{};

[[noreturn]] void FX_DropCameraPublicationGate(const char *const reason)
{
    Com_Error(ERR_DROP, "Invalid FX camera publication ownership: %s", reason);
    std::abort();
}

bool FX_CurrentThreadOwnsExactCooperativeIterator(
    const FxSystem *const system) noexcept
{
    return system
        && fx_cooperativeIteratorThreadState.depth != 0
        && fx_cooperativeIteratorThreadState.system == system
        && fx_cooperativeIteratorThreadState.generation
            == FX_GetCooperativeIteratorGeneration(system);
}

void FX_AbandonCurrentThreadCooperativeIteratorsForError() noexcept
{
    FxSystem *const system = const_cast<FxSystem *>(
        fx_cooperativeIteratorThreadState.system);
    if (system
        && fx_cooperativeIteratorThreadState.generation
            == FX_GetCooperativeIteratorGeneration(system))
    {
        while (fx_cooperativeIteratorThreadState.depth != 0)
        {
            std::int32_t remaining = -1;
            if (!FxIteratorEndCooperative(
                    &system->iteratorCount, &remaining))
            {
                break;
            }
            --fx_cooperativeIteratorThreadState.depth;
        }
    }
    fx_cooperativeIteratorThreadState = {};
}

[[noreturn]] void FX_DropCorruptDrawPool(
    const char *const poolName,
    const std::uint16_t handle)
{
    Com_Error(
        ERR_DROP,
        "Corrupt FX draw %s handle %u is invalid or refers to a free pool slot",
        poolName,
        static_cast<unsigned int>(handle));
    std::abort();
}

[[noreturn]] void FX_DropCorruptDrawList(const char *const reason)
{
    Com_Error(ERR_DROP, "Corrupt FX draw list: %s", reason);
    std::abort();
}

std::size_t FX_GetValidatedDrawElemDefCount(const FxEffect *const effect)
{
    if (!effect || !effect->def
        || effect->def->elemDefCountLooping < 0
        || effect->def->elemDefCountOneShot < 0
        || effect->def->elemDefCountEmission < 0)
    {
        FX_DropCorruptDrawList("invalid effect definition state");
    }
    const std::int64_t elemDefCount =
        static_cast<std::int64_t>(effect->def->elemDefCountLooping)
        + effect->def->elemDefCountOneShot
        + effect->def->elemDefCountEmission;
    if (elemDefCount > 256
        || (elemDefCount > 0 && !effect->def->elemDefs))
    {
        FX_DropCorruptDrawList("invalid effect definition count");
    }
    return static_cast<std::size_t>(elemDefCount);
}

const FxElemDef *FX_GetValidatedDrawElemDef(
    const FxEffect *const effect,
    const std::uint32_t elemDefIndex)
{
    if (elemDefIndex >= FX_GetValidatedDrawElemDefCount(effect))
        FX_DropCorruptDrawList("element definition index is out of range");
    return &effect->def->elemDefs[elemDefIndex];
}

void FX_ValidateDrawTrailDef(const FxElemDef *const elemDef)
{
    if (!elemDef || elemDef->elemType != FX_ELEM_TYPE_TRAIL
        || !elemDef->trailDef || elemDef->trailDef->repeatDist <= 0
        || elemDef->trailDef->vertCount <= 0
        || elemDef->trailDef->vertCount
            > (std::numeric_limits<std::uint16_t>::max)()
        || elemDef->trailDef->indCount <= 0
        || (elemDef->trailDef->indCount & 1) != 0
        || elemDef->trailDef->indCount
            > (std::numeric_limits<std::uint16_t>::max)() / 3
        || !elemDef->trailDef->verts
        || !elemDef->trailDef->inds)
    {
        FX_DropCorruptDrawList("invalid trail definition");
    }
    for (std::int32_t index = 0;
         index < elemDef->trailDef->indCount;
         ++index)
    {
        if (elemDef->trailDef->inds[index]
            >= elemDef->trailDef->vertCount)
        {
            FX_DropCorruptDrawList("trail index exceeds its vertex set");
        }
    }
}

FxElem *FX_GetAllocatedDrawElem(
    FxSystem *const system,
    const std::uint16_t handle)
{
    if (!system || !system->elems)
        FX_DropCorruptDrawPool("element", handle);
    FxPool<FxElem> *const slot =
        FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(system->elems, handle);
    if (!FX_IsElemAllocated(system, &slot->item))
        FX_DropCorruptDrawPool("element", handle);
    return &slot->item;
}

FxTrail *FX_GetAllocatedDrawTrail(
    FxSystem *const system,
    const std::uint16_t handle)
{
    if (!system || !system->trails)
        FX_DropCorruptDrawPool("trail", handle);
    FxPool<FxTrail> *const slot =
        FX_PoolFromHandle_Generic<FxTrail, MAX_TRAILS>(system->trails, handle);
    if (!FX_IsTrailAllocated(system, &slot->item))
        FX_DropCorruptDrawPool("trail", handle);
    return &slot->item;
}

const FxTrailElem *FX_GetAllocatedDrawTrailElem(
    FxSystem *const system,
    const std::uint16_t handle)
{
    if (!system || !system->trailElems)
        FX_DropCorruptDrawPool("trail element", handle);
    FxPool<FxTrailElem> *const slot =
        FX_PoolFromHandle_Generic<FxTrailElem, MAX_TRAIL_ELEMS>(
            system->trailElems, handle);
    if (!FX_IsTrailElemAllocated(system, &slot->item))
        FX_DropCorruptDrawPool("trail element", handle);
    return &slot->item;
}
}

void __cdecl FX_ErrorCleanup() noexcept
{
    // Com_Error uses longjmp, so C++ destructors cannot release iterator
    // ownership. This hook runs at the very start of Com_ErrorCleanup, before
    // thread synchronization can wait behind a gate abandoned by this thread.
    // Effect-table staging never overlaps acquired archive ownership. Release
    // it first so lifecycle/archive cleanup cannot remain blocked behind this
    // thread after a registration-time ERR_DROP.
    fx::archive::AbandonCurrentThreadEffectTableRestoreForError();

    // A pre-publication spotlight can already be visible through the
    // spotlight side channel. Withdraw/tombstone it while its effect lock is
    // still held, then release the recorded locks so stale waiters observe a
    // zero-reference effect and skip it.
    FX_AbandonCurrentThreadEffectReservationForError();
    FX_AbandonCurrentThreadEffectKillRetainsForError();
    FX_AbandonCurrentThreadEffectRestartRetainsForError();
    FX_AbandonCurrentThreadEffectLocksForError();
    FX_AbandonCurrentThreadEffectKillForError();
    FX_AbandonCurrentThreadEffectRestartGateForError();
    FX_AbandonCurrentThreadEffectKillExclusiveForError();
    // Camera ownership nests inside the main cooperative iterator. Release it
    // first so a publication waiter cannot remain blocked while cleanup drops
    // the outer lifetime/admission ownership.
    FX_AbandonCurrentThreadCameraPublicationForError();
    FX_AbandonCurrentThreadCooperativeIteratorsForError();
    FX_AbandonCurrentThreadSortExclusiveForError();
    FX_AbandonCurrentThreadArchiveForError();
}

bool __cdecl FX_CurrentThreadOwnsCooperativeIterator(
    const FxSystem *const system)
{
    if (fx_cooperativeIteratorThreadState.depth == 0)
        return false;

    const FxSystem *const ownedSystem =
        fx_cooperativeIteratorThreadState.system;
    if (!ownedSystem
        || fx_cooperativeIteratorThreadState.generation
            != FX_GetCooperativeIteratorGeneration(ownedSystem))
    {
        fx_cooperativeIteratorThreadState = {};
        return false;
    }

    // Any live cooperative ownership on this thread must prevent an archive
    // wait. Waiting for exclusivity while this thread owns a reader would
    // deadlock even if a future multi-client build supplied another system.
    (void)system;
    return true;
}

void __cdecl FX_BeginReadingCameraPublication(FxSystem *const system)
{
    if (!FX_CurrentThreadOwnsExactCooperativeIterator(system))
    {
        FX_DropCameraPublicationGate(
            "camera reader requires exact cooperative iterator ownership");
    }

    if (fx_cameraPublicationThreadState.depth != 0)
    {
        if (fx_cameraPublicationThreadState.system != system
            || fx_cameraPublicationThreadState.generation
                != FX_GetCooperativeIteratorGeneration(system))
        {
            FX_DropCameraPublicationGate(
                "cross-system or stale camera reader nesting");
        }
        if (fx_cameraPublicationThreadState.mode
            != FxCameraPublicationThreadMode::Read)
        {
            FX_DropCameraPublicationGate(
                "camera writer cannot be downgraded to a nested reader");
        }
        if (fx_cameraPublicationThreadState.depth
            == (std::numeric_limits<std::uint32_t>::max)())
        {
            FX_DropCameraPublicationGate("camera reader nesting overflow");
        }
        ++fx_cameraPublicationThreadState.depth;
        return;
    }

    FxIteratorBeginCooperative(&fx_cameraPublicationIteratorCount);
    fx_cameraPublicationThreadState.system = system;
    fx_cameraPublicationThreadState.generation =
        FX_GetCooperativeIteratorGeneration(system);
    fx_cameraPublicationThreadState.depth = 1;
    fx_cameraPublicationThreadState.mode =
        FxCameraPublicationThreadMode::Read;
}

void __cdecl FX_EndReadingCameraPublication(FxSystem *const system)
{
    if (!FX_CurrentThreadOwnsExactCooperativeIterator(system)
        || fx_cameraPublicationThreadState.depth == 0
        || fx_cameraPublicationThreadState.system != system
        || fx_cameraPublicationThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system)
        || fx_cameraPublicationThreadState.mode
            != FxCameraPublicationThreadMode::Read)
    {
        FX_DropCameraPublicationGate(
            "camera reader ended without matching ownership");
    }

    if (fx_cameraPublicationThreadState.depth > 1)
    {
        --fx_cameraPublicationThreadState.depth;
        return;
    }

    std::int32_t remaining = -1;
    if (!FxIteratorEndCooperative(
            &fx_cameraPublicationIteratorCount, &remaining))
    {
        // Preserve TLS ownership for FX_ErrorCleanup. Its abandonment path
        // will terminate if the fixed-width gate cannot be released either.
        FX_DropCameraPublicationGate("camera reader gate release failed");
    }
    fx_cameraPublicationThreadState = {};
}

void __cdecl FX_BeginWritingCameraPublication(FxSystem *const system)
{
    if (!FX_CurrentThreadOwnsExactCooperativeIterator(system))
    {
        FX_DropCameraPublicationGate(
            "camera writer requires exact cooperative iterator ownership");
    }
    if (fx_cameraPublicationThreadState.depth != 0)
    {
        FX_DropCameraPublicationGate(
            "camera publication mode upgrades and writer nesting are unsafe");
    }

    FxIteratorWaitBeginExclusive(&fx_cameraPublicationIteratorCount);
    fx_cameraPublicationThreadState.system = system;
    fx_cameraPublicationThreadState.generation =
        FX_GetCooperativeIteratorGeneration(system);
    fx_cameraPublicationThreadState.depth = 1;
    fx_cameraPublicationThreadState.mode =
        FxCameraPublicationThreadMode::Write;
}

void __cdecl FX_EndWritingCameraPublication(FxSystem *const system)
{
    if (!FX_CurrentThreadOwnsExactCooperativeIterator(system)
        || fx_cameraPublicationThreadState.depth != 1
        || fx_cameraPublicationThreadState.system != system
        || fx_cameraPublicationThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system)
        || fx_cameraPublicationThreadState.mode
            != FxCameraPublicationThreadMode::Write)
    {
        FX_DropCameraPublicationGate(
            "camera writer ended without matching ownership");
    }

    if (!FxIteratorEndExclusive(&fx_cameraPublicationIteratorCount))
    {
        FX_DropCameraPublicationGate("camera writer gate release failed");
    }
    fx_cameraPublicationThreadState = {};
}

void __cdecl FX_AbandonCurrentThreadCameraPublicationForError() noexcept
{
    if (fx_cameraPublicationThreadState.depth == 0)
    {
        fx_cameraPublicationThreadState = {};
        return;
    }

    bool released = false;
    if (fx_cameraPublicationThreadState.mode
        == FxCameraPublicationThreadMode::Read)
    {
        std::int32_t remaining = -1;
        released = FxIteratorEndCooperative(
            &fx_cameraPublicationIteratorCount, &remaining);
    }
    else if (fx_cameraPublicationThreadState.mode
        == FxCameraPublicationThreadMode::Write)
    {
        released =
            FxIteratorEndExclusive(&fx_cameraPublicationIteratorCount);
    }

    fx_cameraPublicationThreadState = {};
    if (!released)
    {
        // Continuing after losing track of an external writer/read count can
        // expose torn camera payloads or leave future jobs blocked forever.
        std::abort();
    }
}

#ifdef KISAK_MP
#include <cgame_mp/cg_local_mp.h>
#elif KISAK_SP
#include <cgame/cg_main.h>
#endif

void __cdecl FX_EvaluateVisAlpha(FxElemPreVisualState *preVisState, FxElemVisualState *visState)
{
    float valueLerpInv; // [esp+30h] [ebp-8h]
    float valueLerp; // [esp+34h] [ebp-4h]

    valueLerp = fx_randomTable[preVisState->randomSeed + 23];
    valueLerpInv = 1.0 - valueLerp;
    visState->color[3] = FX_InterpolateColor(
        preVisState->refState,
        valueLerp,
        valueLerpInv,
        preVisState->sampleLerp,
        preVisState->sampleLerpInv,
        3);
}

uint8_t __cdecl FX_InterpolateColor(
    const FxElemVisStateSample *refState,
    float valueLerp,
    float valueLerpInv,
    float sampleLerp,
    float sampleLerpInv,
    int32_t channel)
{
    float valueFrom; // [esp+24h] [ebp-8h]
    float valueTo; // [esp+28h] [ebp-4h]

    valueFrom = (double)refState->base.color[channel] * valueLerpInv
        + (double)refState->amplitude.color[channel] * valueLerp;
    valueTo = (double)refState[1].base.color[channel] * valueLerpInv
        + (double)refState[1].amplitude.color[channel] * valueLerp;

    return SnapFloatToInt(sampleLerp * valueTo + sampleLerpInv * valueFrom);
}

void __cdecl FX_SetupVisualState(
    const FxElemDef *elemDef,
    const FxEffect *effect,
    int32_t randomSeed,
    float normTimeUpdateEnd,
    FxElemPreVisualState *preVisState)
{
    float v5; // [esp+8h] [ebp-10h]
    float samplePoint; // [esp+10h] [ebp-8h]

    samplePoint = (double)elemDef->visStateIntervalCount * normTimeUpdateEnd;
    v5 = floor(samplePoint);
    preVisState->sampleLerp = samplePoint - (double)(int)v5;
    preVisState->sampleLerpInv = 1.0 - preVisState->sampleLerp;
    preVisState->elemDef = elemDef;
    preVisState->effect = effect;
    preVisState->refState = &elemDef->visSamples[(int)v5];
    preVisState->randomSeed = randomSeed;
    preVisState->distanceFade = 255;
}

void __cdecl FX_EvaluateSize(FxElemPreVisualState *preVisState, FxElemVisualState *visState)
{
    visState->size[0] = FX_InterpolateSize(
        preVisState->refState,
        preVisState->randomSeed,
        FXRAND_SIZE_0,
        preVisState->sampleLerp,
        preVisState->sampleLerpInv,
        0);
    if ((preVisState->elemDef->flags & 0x10000000) != 0)
        visState->size[1] = FX_InterpolateSize(
            preVisState->refState,
            preVisState->randomSeed,
            FXRAND_SIZE_1,
            preVisState->sampleLerp,
            preVisState->sampleLerpInv,
            1);
    else
        visState->size[1] = visState->size[0];
}

double __cdecl FX_InterpolateSize(
    const FxElemVisStateSample *refState,
    int32_t randomSeed,
    FxRandKey randomKey,
    float sampleLerp,
    float sampleLerpInv,
    int32_t channel)
{
    float valueFrom; // [esp+4h] [ebp-Ch]
    float valueTo; // [esp+8h] [ebp-8h]
    float valueLerp; // [esp+Ch] [ebp-4h]

    valueLerp = fx_randomTable[randomKey + randomSeed];
    valueFrom = valueLerp * refState->amplitude.size[channel] + refState->base.size[channel];
    valueTo = valueLerp * refState[1].amplitude.size[channel] + refState[1].base.size[channel];
    return (float)(valueFrom * sampleLerpInv + valueTo * sampleLerp);
}

void __cdecl FX_EvaluateVisualState(FxElemPreVisualState *preVisState, float msecLifeSpan, FxElemVisualState *visState)
{
    float valueLerpInv; // [esp+B4h] [ebp-20h]
    const FxElemDef *elemDef; // [esp+B8h] [ebp-1Ch]
    int32_t randomSeed; // [esp+BCh] [ebp-18h]
    float valueLerp; // [esp+C0h] [ebp-14h]
    float sampleLerpInv; // [esp+C4h] [ebp-10h]
    const FxElemVisStateSample *refState; // [esp+C8h] [ebp-Ch]
    float sampleLerp; // [esp+CCh] [ebp-8h]

    elemDef = preVisState->elemDef;
    if (!elemDef)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 182, 0, "%s", "elemDef");
    refState = preVisState->refState;
    randomSeed = preVisState->randomSeed;
    valueLerp = fx_randomTable[randomSeed + 23];
    valueLerpInv = 1.0 - valueLerp;
    sampleLerp = preVisState->sampleLerp;
    sampleLerpInv = preVisState->sampleLerpInv;
    visState->color[0] = FX_InterpolateColor(refState, valueLerp, valueLerpInv, sampleLerp, sampleLerpInv, 0);
    visState->color[1] = FX_InterpolateColor(refState, valueLerp, valueLerpInv, sampleLerp, sampleLerpInv, 1);
    visState->color[2] = FX_InterpolateColor(refState, valueLerp, valueLerpInv, sampleLerp, sampleLerpInv, 2);
    visState->color[3] = FX_InterpolateColor(refState, valueLerp, valueLerpInv, sampleLerp, sampleLerpInv, 3);
    if (elemDef->lightingFrac)
        FX_EvaluateVisualState_DoLighting(preVisState, visState, elemDef);
    visState->rotationTotal = elemDef->initialRotation.amplitude * fx_randomTable[randomSeed + 23] + elemDef->initialRotation.base;
    visState->rotationTotal = FX_IntegrateRotationFromZero(
        refState,
        randomSeed,
        FXRAND_ROTATION_DELTA,
        sampleLerp,
        msecLifeSpan)
        + visState->rotationTotal;
    visState->color[3] = (uint16_t)(LOWORD(preVisState->distanceFade) * visState->color[3]) >> 8;
}

double __cdecl FX_IntegrateRotationFromZero(
    const FxElemVisStateSample *refState,
    int32_t randomSeed,
    FxRandKey randomKey,
    float sampleLerp,
    float msecLifeSpan)
{
    float valueLerp; // [esp+4h] [ebp-10h]
    float rotationTotal; // [esp+8h] [ebp-Ch]
    float rotationTotala; // [esp+8h] [ebp-Ch]
    float rotationTotalb; // [esp+8h] [ebp-Ch]
    float weight; // [esp+Ch] [ebp-8h]
    float weight_4; // [esp+10h] [ebp-4h]

    valueLerp = fx_randomTable[randomKey + randomSeed];
    weight_4 = sampleLerp * sampleLerp * 0.5;
    weight = sampleLerp - weight_4;
    rotationTotal = valueLerp * refState->amplitude.rotationTotal + refState->base.rotationTotal;
    rotationTotala = (valueLerp * refState->amplitude.rotationDelta + refState->base.rotationDelta) * weight
        + rotationTotal;
    rotationTotalb = (valueLerp * refState[1].amplitude.rotationDelta + refState[1].base.rotationDelta) * weight_4
        + rotationTotala;
    return (float)(rotationTotalb * msecLifeSpan);
}

static int32_t mapping[] = { 2, 1, 0 }; // idb

void __cdecl FX_EvaluateVisualState_DoLighting(
    FxElemPreVisualState *preVisState,
    FxElemVisualState *visState,
    const FxElemDef *elemDef)
{
    uint8_t v3; // [esp+0h] [ebp-1Ch]
    int32_t v4; // [esp+4h] [ebp-18h]
    uint32_t lightFactor; // [esp+Ch] [ebp-10h]
    uint32_t colorIndex; // [esp+10h] [ebp-Ch]
    uint32_t outColorIndex; // [esp+14h] [ebp-8h]
    uint8_t lightColor[4]; // [esp+18h] [ebp-4h] BYREF

    if (!preVisState->effect)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 156, 0, "%s", "preVisState->effect");
    FX_UnpackColor565(preVisState->effect->packedLighting, lightColor, &lightColor[1], &lightColor[2]);
    for (colorIndex = 0; colorIndex != 3; ++colorIndex)
    {
        outColorIndex = mapping[colorIndex];
        lightFactor = (2 * lightColor[colorIndex] - 255) * elemDef->lightingFrac / 255 + 255;
        if (lightFactor > 0x1FE)
            MyAssertHandler(
                ".\\EffectsCore\\fx_draw.cpp",
                163,
                0,
                "%s\n\t(lightFactor) = %i",
                "(lightFactor >= 0 && lightFactor <= 510)",
                lightFactor);
        if ((int)(lightFactor * visState->color[outColorIndex]) / 255 < 255)
            v4 = (int)(lightFactor * visState->color[outColorIndex]) / 255;
        else
            v4 = 255;
        if (v4 > 0)
            v3 = v4;
        else
            v3 = 0;
        visState->color[outColorIndex] = v3;
    }
}

void __cdecl FX_UnpackColor565(
    uint16_t packed,
    uint8_t *outR,
    uint8_t *outG,
    uint8_t *outB)
{
    *outR = 8 * packed;
    *outR |= (int)*outR >> 5;
    *outG = ((int)packed >> 3) & 0xF8;
    *outG |= (int)*outG >> 5;
    *outB = HIBYTE(packed) & 0xF8;
    *outB |= (int)*outB >> 5;
}

void __cdecl FX_DrawElem_BillboardSprite(FxDrawState *draw)
{
    float *origin; // ecx
    float *v2; // [esp+4h] [ebp-2Ch]
    float *v3; // [esp+8h] [ebp-28h]
    float normal[3]; // [esp+Ch] [ebp-24h] BYREF
    float tangent[3]; // [esp+18h] [ebp-18h] BYREF
    float binormal[3]; // [esp+24h] [ebp-Ch] BYREF

    if (!FX_CullElementForDraw_Sprite(draw))
    {
        v3 = draw->camera->axis[0];
        normal[0] = -v3[0];
        normal[1] = -v3[1];
        normal[2] = -v3[2];
        v2 = draw->camera->axis[1];
        tangent[0] = -v2[0];
        tangent[1] = -v2[1];
        tangent[2] = -v2[2];
        origin = draw->camera->axis[2];
        binormal[0] = origin[0];
        binormal[1] = origin[1];
        binormal[2] = origin[2];
        FX_GenSpriteVerts(draw, tangent, binormal, normal);
    }
}

void __cdecl FX_GenSpriteVerts(FxDrawState *draw, const float *tangent, const float *binormal, const float *normal)
{
    float scale1; // [esp+8h] [ebp-274h]
    __int16 v5; // [esp+14h] [ebp-268h]
    __int16 v6; // [esp+18h] [ebp-264h]
    __int16 v7; // [esp+1Ch] [ebp-260h]
    __int16 v8; // [esp+20h] [ebp-25Ch]
    __int16 v9; // [esp+24h] [ebp-258h]
    __int16 v10; // [esp+28h] [ebp-254h]
    __int16 v11; // [esp+2Ch] [ebp-250h]
    __int16 v12; // [esp+30h] [ebp-24Ch]
    float v13; // [esp+7Ch] [ebp-200h]
    int32_t v14; // [esp+80h] [ebp-1FCh]
    float v15; // [esp+90h] [ebp-1ECh]
    int32_t v16; // [esp+94h] [ebp-1E8h]
    int32_t v17; // [esp+B4h] [ebp-1C8h]
    float v18; // [esp+C4h] [ebp-1B8h]
    int32_t v19; // [esp+C8h] [ebp-1B4h]
    int32_t v20; // [esp+E8h] [ebp-194h]
    int32_t v21; // [esp+FCh] [ebp-180h]
    float v22; // [esp+118h] [ebp-164h]
    int32_t v23; // [esp+11Ch] [ebp-160h]
    int32_t v24; // [esp+130h] [ebp-14Ch]
    PackedUnitVec v25; // [esp+14Ch] [ebp-130h]
    PackedUnitVec v26; // [esp+16Ch] [ebp-110h]
    float rotationTotal; // [esp+1A8h] [ebp-D4h]
    int32_t t0; // [esp+1E4h] [ebp-98h] BYREF
    r_double_index_t *baseIndices; // [esp+1E8h] [ebp-94h] BYREF
    float dt; // [esp+1ECh] [ebp-90h] BYREF
    FxElemVisuals visuals; // [esp+1F0h] [ebp-8Ch]
    float leftSide[3]; // [esp+1F4h] [ebp-88h] BYREF
    PackedUnitVec packedNormal; // [esp+200h] [ebp-7Ch]
    PackedUnitVec packedTangent; // [esp+204h] [ebp-78h]
    float rightSide[3]; // [esp+208h] [ebp-74h] BYREF
    FxSpriteInfo *sprite; // [esp+214h] [ebp-68h]
    float testBinormal[3]; // [esp+218h] [ebp-64h] BYREF
    float cosRot; // [esp+224h] [ebp-58h]
    float left[3]; // [esp+228h] [ebp-54h] BYREF
    float v40; // [esp+234h] [ebp-48h] BYREF
    GfxPackedVertex *verts; // [esp+238h] [ebp-44h]
    float sinRot; // [esp+23Ch] [ebp-40h]
    r_double_index_t *indices; // [esp+240h] [ebp-3Ch]
    r_double_index_t index; // [esp+244h] [ebp-38h]
    float rotatedTangent[3]; // [esp+248h] [ebp-34h] BYREF
    float up[3]; // [esp+254h] [ebp-28h] BYREF
    FxSystem *system; // [esp+260h] [ebp-1Ch]
    int32_t s0; // [esp+264h] [ebp-18h] BYREF
    uint16_t baseVertex; // [esp+268h] [ebp-14h] BYREF
    float rotatedBinormal[3]; // [esp+26Ch] [ebp-10h] BYREF
    GfxPackedVertex *baseVerts; // [esp+278h] [ebp-4h]

    system = draw->system;
    sprite = &system->sprite;
    visuals.anonymous = FX_GetElemVisuals(draw->elemDef, draw->randomSeed).anonymous;
    if (!visuals.anonymous)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 298, 0, "%s", "visuals.material");
    if (sprite->material != visuals.anonymous && sprite->indexCount)
    {
        if (!sprite->name)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 304, 0, "%s", "sprite->name");
        if (!sprite->material)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 305, 0, "%s", "sprite->material");
        if (!sprite->indices)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 306, 0, "%s", "sprite->indices");
        R_AddCodeMeshDrawSurf(sprite->material, sprite->indices, sprite->indexCount, 0, 0, sprite->name);
        sprite->indexCount = 0;
    }
    if (R_ReserveCodeMeshVerts(4, &baseVertex) && R_ReserveCodeMeshIndices(6, &baseIndices))
    {
        if (sprite->material != visuals.anonymous)
        {
            sprite->name = draw->effect->def->name;
            sprite->material = visuals.material;
            sprite->indices = baseIndices;
        }
        sprite->indexCount += 6;
        {
            PROF_SCOPED("FX_EvalVisState");
            FX_EvaluateVisualState(&draw->preVisState, draw->msecLifeSpan, &draw->visState);
        }
        rotationTotal = draw->visState.rotationTotal;
        cosRot = cos(rotationTotal);
        sinRot = sin(rotationTotal);
        Vec3ScaleMad(cosRot, tangent, sinRot, binormal, rotatedTangent);
        scale1 = -cosRot;
        Vec3ScaleMad(sinRot, tangent, scale1, binormal, rotatedBinormal);
        Vec3Scale(rotatedTangent, draw->visState.size[0], left);
        Vec3Scale(rotatedBinormal, draw->visState.size[1], up);
        Vec3Sub(draw->posWorld, left, leftSide);
        Vec3Add(draw->posWorld, left, rightSide);
        FX_GetSpriteTexCoords(draw, (float *)&s0, &v40, (float *)&t0, &dt);
        v26.array[0] = (int)(*normal * 127.0 + 127.5);
        v26.array[1] = (int)(normal[1] * 127.0 + 127.5);
        v26.array[2] = (int)(normal[2] * 127.0 + 127.5);
        v26.array[3] = 63;
        packedNormal.packed = v26.packed;
        v25.array[0] = (int)(rotatedTangent[0] * 127.0 + 127.5);
        v25.array[1] = (int)(rotatedTangent[1] * 127.0 + 127.5);
        v25.array[2] = (int)(rotatedTangent[2] * 127.0 + 127.5);
        v25.array[3] = 63;
        packedTangent.packed = v25.packed;
        indices = baseIndices;
        index.value[0] = baseVertex;
        index.value[1] = baseVertex + 1;
        *baseIndices = index;
        ++indices;
        index.value[0] = baseVertex + 2;
        index.value[1] = baseVertex + 2;
        *indices++ = index;
        index.value[0] = baseVertex + 3;
        index.value[1] = baseVertex;
        *indices++ = index;
        Vec3Cross(normal, rotatedTangent, testBinormal);
        if (Vec3Dot(testBinormal, rotatedBinormal) > 0.0)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 361, 0, "%s", "Vec3Dot( testBinormal, rotatedBinormal ) <= 0.0f");
        baseVerts = R_GetCodeMeshVerts(baseVertex);
        verts = baseVerts;
        Vec3Add(leftSide, up, baseVerts->xyz);
        verts->binormalSign = -1.0;
        verts->normal = packedNormal;
        verts->color.packed = *(uint32_t *)draw->visState.color;
        if ((int)((2 * s0) ^ 0x80000000) >> 14 < 0x3FFF)
            v24 = (int)((2 * s0) ^ 0x80000000) >> 14;
        else
            v24 = 0x3FFF;
        if (v24 > -16384)
            v12 = v24;
        else
            v12 = -16384;
        v22 = dt + *(float *)&t0;
        if ((int)((2 * LODWORD(v22)) ^ 0x80000000) >> 14 < 0x3FFF)
            v23 = (int)((2 * LODWORD(v22)) ^ 0x80000000) >> 14;
        else
            v23 = 0x3FFF;
        if (v23 > -16384)
            v11 = v23;
        else
            v11 = -16384;
        verts->texCoord.packed = (v11 & 0x3FFF | ((int)LODWORD(v22) >> 16) & 0xC000)
            + ((v12 & 0x3FFF | (s0 >> 16) & 0xC000) << 16);
        verts->tangent = packedTangent;
        ++verts;
        Vec3Sub(leftSide, up, verts->xyz);
        verts->binormalSign = -1.0;
        verts->normal = packedNormal;
        verts->color.packed = *(uint32_t *)draw->visState.color;
        if ((int)((2 * s0) ^ 0x80000000) >> 14 < 0x3FFF)
            v21 = (int)((2 * s0) ^ 0x80000000) >> 14;
        else
            v21 = 0x3FFF;
        if (v21 > -16384)
            v10 = v21;
        else
            v10 = -16384;
        if ((int)((2 * t0) ^ 0x80000000) >> 14 < 0x3FFF)
            v20 = (int)((2 * t0) ^ 0x80000000) >> 14;
        else
            v20 = 0x3FFF;
        if (v20 > -16384)
            v9 = v20;
        else
            v9 = -16384;
        verts->texCoord.packed = (v9 & 0x3FFF | (t0 >> 16) & 0xC000) + ((v10 & 0x3FFF | (s0 >> 16) & 0xC000) << 16);
        verts->tangent = packedTangent;
        ++verts;
        Vec3Sub(rightSide, up, verts->xyz);
        verts->binormalSign = -1.0;
        verts->normal = packedNormal;
        verts->color.packed = *(uint32_t *)draw->visState.color;
        v18 = v40 + *(float *)&s0;
        if ((int)((2 * LODWORD(v18)) ^ 0x80000000) >> 14 < 0x3FFF)
            v19 = (int)((2 * LODWORD(v18)) ^ 0x80000000) >> 14;
        else
            v19 = 0x3FFF;
        if (v19 > -16384)
            v8 = v19;
        else
            v8 = -16384;
        if ((int)((2 * t0) ^ 0x80000000) >> 14 < 0x3FFF)
            v17 = (int)((2 * t0) ^ 0x80000000) >> 14;
        else
            v17 = 0x3FFF;
        if (v17 > -16384)
            v7 = v17;
        else
            v7 = -16384;
        verts->texCoord.packed = (v7 & 0x3FFF | (t0 >> 16) & 0xC000)
            + ((v8 & 0x3FFF | ((int)LODWORD(v18) >> 16) & 0xC000) << 16);
        verts->tangent = packedTangent;
        ++verts;
        Vec3Add(rightSide, up, verts->xyz);
        verts->binormalSign = -1.0;
        verts->normal = packedNormal;
        verts->color.packed = *(uint32_t *)draw->visState.color;
        v15 = v40 + *(float *)&s0;
        if ((int)((2 * LODWORD(v15)) ^ 0x80000000) >> 14 < 0x3FFF)
            v16 = (int)((2 * LODWORD(v15)) ^ 0x80000000) >> 14;
        else
            v16 = 0x3FFF;
        if (v16 > -16384)
            v6 = v16;
        else
            v6 = -16384;
        v13 = dt + *(float *)&t0;
        if ((int)((2 * LODWORD(v13)) ^ 0x80000000) >> 14 < 0x3FFF)
            v14 = (int)((2 * LODWORD(v13)) ^ 0x80000000) >> 14;
        else
            v14 = 0x3FFF;
        if (v14 > -16384)
            v5 = v14;
        else
            v5 = -16384;
        verts->texCoord.packed = (v5 & 0x3FFF | ((int)LODWORD(v13) >> 16) & 0xC000)
            + ((v6 & 0x3FFF | ((int)LODWORD(v15) >> 16) & 0xC000) << 16);
        verts->tangent = packedTangent;
    }
}

void __cdecl FX_GetSpriteTexCoords(const FxDrawState *draw, float *s0, float *ds, float *t0, float *dt)
{
    uint8_t colBits; // [esp+14h] [ebp-14h]
    int32_t atlasIndex; // [esp+18h] [ebp-10h]
    int32_t atlasIndexa; // [esp+18h] [ebp-10h]
    const FxElemDef *elemDef; // [esp+1Ch] [ebp-Ch]
    int32_t atlasCount; // [esp+20h] [ebp-8h]
    uint8_t rowBits; // [esp+24h] [ebp-4h]

    if (!draw)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 215, 0, "%s", "draw");
    elemDef = draw->elemDef;
    if (!elemDef)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 217, 0, "%s", "elemDef");
    atlasCount = elemDef->atlas.entryCount;
    if (atlasCount == 1)
    {
        *s0 = 0.0;
        *ds = 1.0;
        *t0 = 0.0;
        *dt = 1.0;
    }
    else
    {
        if (atlasCount <= 1 || atlasCount > 256 || (atlasCount & (atlasCount - 1)) != 0)
            MyAssertHandler(
                ".\\EffectsCore\\fx_draw.cpp",
                228,
                0,
                "%s\n\t(atlasCount) = %i",
                "(atlasCount > 1 && atlasCount <= 256 && (((atlasCount) & ((atlasCount) - 1)) == 0))",
                atlasCount);
        if ((elemDef->atlas.behavior & 3) != 0)
        {
            if ((elemDef->atlas.behavior & 3) == 1)
            {
                atlasIndex = (atlasCount * LOWORD(fx_randomTable[draw->randomSeed + 22])) >> 16;
            }
            else
            {
                if ((elemDef->atlas.behavior & 3) != 2)
                    MyAssertHandler(
                        ".\\EffectsCore\\fx_draw.cpp",
                        241,
                        1,
                        "elemDef->atlas.behavior & FX_ATLAS_START_MASK == FX_ATLAS_START_INDEXED\n\t%i, %i",
                        elemDef->atlas.behavior & 3,
                        2);
                atlasIndex = (atlasCount - 1) & draw->elem->sequence;
            }
        }
        else
        {
            atlasIndex = elemDef->atlas.index;
        }
        if ((elemDef->atlas.behavior & 4) != 0)
        {
            atlasIndex += (int)((double)atlasCount * draw->normTimeUpdateEnd);
        }
        else if (elemDef->atlas.fps)
        {
            atlasIndex += elemDef->atlas.fps * (int)draw->msecElapsed / 1000;
        }
        if ((elemDef->atlas.behavior & 8) != 0 && atlasIndex >= atlasCount * elemDef->atlas.loopCount)
            atlasIndex = atlasCount - 1;
        atlasIndexa = atlasIndex & (atlasCount - 1);
        rowBits = elemDef->atlas.rowIndexBits;
        colBits = elemDef->atlas.colIndexBits;
        *ds = 1.0 / (double)(1 << colBits);
        *s0 = (double)(atlasIndexa & ((1 << colBits) - 1)) * *ds;
        *dt = 1.0 / (double)(1 << rowBits);
        *t0 = (double)(atlasIndexa >> colBits) * *dt;
    }
}

bool __cdecl FX_CullElementForDraw_Sprite(const FxDrawState *draw)
{
    bool result; // al
    float v2; // [esp+4h] [ebp-18h]
    float v3; // [esp+8h] [ebp-14h]
    float v4; // [esp+Ch] [ebp-10h]
    float v5; // [esp+10h] [ebp-Ch]
    uint32_t frustumPlaneCount; // [esp+18h] [ebp-4h]

    result = 0;
    if (fx_cull_elem_draw->current.enabled)
    {
        frustumPlaneCount = FX_CullElementForDraw_FrustumPlaneCount(draw);
        v4 = draw->visState.size[0];
        v5 = draw->visState.size[1];
        v3 = v4 - v5;
        v2 = v3 < 0.0 ? v5 : v4;
        if (FX_CullSphere(draw->camera, frustumPlaneCount, draw->posWorld, v2))
            return 1;
    }
    return result;
}

uint32_t __cdecl FX_CullElementForDraw_FrustumPlaneCount(const FxDrawState *draw)
{
    if (!draw || !draw->camera || draw->camera->frustumPlaneCount < 5)
        MyAssertHandler(
            ".\\EffectsCore\\fx_draw.cpp",
            537,
            0,
            "%s",
            "draw && draw->camera && draw->camera->frustumPlaneCount >= 5");
    if ((draw->elemDef->flags & 0x400) != 0)
        return 5;
    else
        return draw->camera->frustumPlaneCount;
}

void __cdecl FX_DrawElem_OrientedSprite(FxDrawState *draw)
{
    if (!FX_CullElementForDraw_Sprite(draw))
        FX_GenSpriteVerts(draw, draw->orient.axis[1], draw->orient.axis[2], draw->orient.axis[0]);
}

void __cdecl FX_DrawElem_Tail(FxDrawState *draw)
{
    float scale; // [esp+8h] [ebp-48h]
    float normal[3]; // [esp+2Ch] [ebp-24h] BYREF
    float deltaCamera[3]; // [esp+38h] [ebp-18h] BYREF
    float tangent[3]; // [esp+44h] [ebp-Ch] BYREF

    FX_GetVelocityAtTime(
        draw->elemDef,
        draw->randomSeed,
        draw->msecLifeSpan,
        draw->msecElapsed,
        &draw->orient,
        draw->elem->baseVel,
        draw->velDirWorld);
    Vec3Normalize(draw->velDirWorld);
    if (!FX_CullElementForDraw_Tail(draw))
    {
        scale = -draw->visState.size[1];
        Vec3Mad(draw->posWorld, scale, draw->velDirWorld, draw->posWorld);
        Vec3Sub(draw->camera->origin, draw->posWorld, deltaCamera);
        Vec3Cross(draw->velDirWorld, deltaCamera, tangent);
        Vec3Normalize(tangent);
        Vec3Cross(tangent, draw->velDirWorld, normal);
        FX_GenSpriteVerts(draw, tangent, draw->velDirWorld, normal);
    }
}

bool __cdecl FX_CullElementForDraw_Tail(const FxDrawState *draw)
{
    bool result; // al
    float scale; // [esp+Ch] [ebp-14h]
    float endpoint[3]; // [esp+10h] [ebp-10h] BYREF
    uint32_t frustumPlaneCount; // [esp+1Ch] [ebp-4h]

    result = 0;
    if (fx_cull_elem_draw->current.enabled)
    {
        frustumPlaneCount = FX_CullElementForDraw_FrustumPlaneCount(draw);
        scale = draw->visState.size[1] * -2.0;
        Vec3Mad(draw->posWorld, scale, draw->velDirWorld, endpoint);
        if (FX_CullCylinder(draw->camera, frustumPlaneCount, draw->posWorld, endpoint, draw->visState.size[0]))
            return 1;
    }
    return result;
}

char __cdecl FX_CullCylinder(
    const FxCamera *camera,
    uint32_t frustumPlaneCount,
    const float *posWorld0,
    const float *posWorld1,
    float radius)
{
    const char *v5; // eax
    const char *v6; // eax
    double v8; // [esp+18h] [ebp-18h]
    float pointToPlaneDist; // [esp+28h] [ebp-8h]
    float pointToPlaneDista; // [esp+28h] [ebp-8h]
    uint32_t planeIndex; // [esp+2Ch] [ebp-4h]

    if (!Sys_AtomicLoad(&camera->isValid))
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 620, 0, "%s", "camera->isValid");
    if (frustumPlaneCount != camera->frustumPlaneCount && frustumPlaneCount != 5)
    {
        v5 = va("%i, %i", frustumPlaneCount, camera->frustumPlaneCount);
        MyAssertHandler(
            ".\\EffectsCore\\fx_draw.cpp",
            621,
            0,
            "%s\n\t%s",
            "frustumPlaneCount == camera->frustumPlaneCount || frustumPlaneCount == 5",
            v5);
    }
    for (planeIndex = 0; planeIndex < frustumPlaneCount; ++planeIndex)
    {
        if (!Vec3IsNormalized(camera->frustum[planeIndex]))
        {
            v8 = Vec3Length(camera->frustum[planeIndex]);
            v6 = va(
                "(%g %g %g) len %g",
                camera->frustum[planeIndex][0],
                camera->frustum[planeIndex][1],
                camera->frustum[planeIndex][2],
                v8);
            MyAssertHandler(
                ".\\EffectsCore\\fx_draw.cpp",
                625,
                0,
                "%s\n\t%s",
                "Vec3IsNormalized( camera->frustum[planeIndex] )",
                v6);
        }
        pointToPlaneDist = Vec3Dot(camera->frustum[planeIndex], posWorld0) - camera->frustum[planeIndex][3];
        if (pointToPlaneDist <= -radius)
        {
            pointToPlaneDista = Vec3Dot(camera->frustum[planeIndex], posWorld1) - camera->frustum[planeIndex][3];
            if (pointToPlaneDista <= -radius)
                return 1;
        }
    }
    return 0;
}

void __cdecl FX_DrawElem_Cloud(FxDrawState *draw)
{
    GfxParticleCloud *cloud; // [esp+50h] [ebp-Ch]
    FxElemVisuals visuals; // [esp+54h] [ebp-8h]

    if (fx_drawClouds->current.enabled)
    {
        FX_GetVelocityAtTime(
            draw->elemDef,
            draw->randomSeed,
            draw->msecLifeSpan,
            draw->msecElapsed,
            &draw->orient,
            draw->elem->baseVel,
            draw->velDirWorld);
        Vec3Normalize(draw->velDirWorld);
        draw->visState.scale = FX_InterpolateScale(
            draw->preVisState.refState,
            draw->preVisState.randomSeed,
            FXRAND_SCALE,
            draw->preVisState.sampleLerp,
            draw->preVisState.sampleLerpInv);
        if (draw->visState.scale != 0.0 && !FX_CullElementForDraw_Cloud(draw))
        {
            Sys_AtomicIncrement(&draw->system->gfxCloudCount);
            visuals.anonymous = FX_GetElemVisuals(draw->elemDef, draw->randomSeed).anonymous;
            if (!visuals.anonymous)
                MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 792, 0, "%s", "visuals.material");
            cloud = R_AddParticleCloudToScene(visuals.material);
            if (cloud)
            {
                {
                    PROF_SCOPED("FX_EvalVisState");
                    FX_EvaluateVisualState(&draw->preVisState, draw->msecLifeSpan, &draw->visState);
                }
                FX_SetPlacement(draw, &cloud->placement);
                cloud->color.packed = *(uint32_t *)draw->visState.color;
                *(double *)cloud->radius = *(double *)draw->visState.size;
                Vec3Sub(draw->posWorld, draw->velDirWorld, cloud->endpos);
            }
        }
    }
}

void __cdecl FX_SetPlacement(const FxDrawState *draw, GfxScaledPlacement *placement)
{
    float axis[3][3]; // [esp+14h] [ebp-28h] BYREF
    float msecForAxis; // [esp+38h] [ebp-4h]

    msecForAxis = FX_GetMsecForSamplingAxis(draw->msecElapsed, draw->msecLifeSpan, draw->elem->atRestFraction);
    FX_GetElemAxis(draw->elemDef, draw->randomSeed, &draw->orient, msecForAxis, axis);
    AxisToQuat(axis, placement->base.quat);
    placement->base.origin[0] = draw->posWorld[0];
    placement->base.origin[1] = draw->posWorld[1];
    placement->base.origin[2] = draw->posWorld[2];
    placement->scale = draw->visState.scale;
}

double __cdecl FX_GetMsecForSamplingAxis(float msecElapsed, float msecLifeSpan, int32_t atRestFraction)
{
    float msecAtRest; // [esp+8h] [ebp-8h]
    float msecSinceAtRest; // [esp+Ch] [ebp-4h]

    msecAtRest = (double)atRestFraction * msecLifeSpan * 0.003921568859368563;
    msecSinceAtRest = msecElapsed - msecAtRest;
    if (msecSinceAtRest <= 0.0)
        return msecElapsed;
    if (msecSinceAtRest < 300.0)
        return (float)(msecElapsed - msecSinceAtRest * msecSinceAtRest * 0.001666666707023978);
    else
        return (float)(msecAtRest + 150.0);
}

double __cdecl FX_InterpolateScale(
    const FxElemVisStateSample *refState,
    int32_t randomSeed,
    FxRandKey randomKey,
    float sampleLerp,
    float sampleLerpInv)
{
    float valueFrom; // [esp+4h] [ebp-Ch]
    float valueTo; // [esp+8h] [ebp-8h]
    float valueLerp; // [esp+Ch] [ebp-4h]

    valueLerp = fx_randomTable[randomKey + randomSeed];
    valueFrom = valueLerp * refState->amplitude.scale + refState->base.scale;
    valueTo = valueLerp * refState[1].amplitude.scale + refState[1].base.scale;
    return (float)(valueFrom * sampleLerpInv + valueTo * sampleLerp);
}

bool __cdecl FX_CullElementForDraw_Cloud(const FxDrawState *draw)
{
    bool result; // al
    float v2; // [esp+4h] [ebp-18h]
    float v3; // [esp+8h] [ebp-14h]
    float v4; // [esp+Ch] [ebp-10h]
    float v5; // [esp+10h] [ebp-Ch]
    float radius; // [esp+14h] [ebp-8h]
    uint32_t frustumPlaneCount; // [esp+18h] [ebp-4h]

    result = 0;
    if (fx_cull_elem_draw->current.enabled)
    {
        frustumPlaneCount = FX_CullElementForDraw_FrustumPlaneCount(draw);
        v4 = draw->visState.size[0];
        v5 = draw->visState.size[1];
        v3 = v4 - v5;
        v2 = v3 < 0.0 ? v5 : v4;
        radius = draw->visState.scale + v2;
        if (FX_CullSphere(draw->camera, frustumPlaneCount, draw->posWorld, radius))
            return 1;
    }
    return result;
}

void __cdecl FX_DrawElem_Model(FxDrawState *draw)
{
    FxElemVisuals visuals; // [esp+20h] [ebp-24h]
    GfxScaledPlacement placement; // [esp+24h] [ebp-20h] BYREF

    draw->visState.scale = FX_InterpolateScale(
        draw->preVisState.refState,
        draw->preVisState.randomSeed,
        FXRAND_SCALE,
        draw->preVisState.sampleLerp,
        draw->preVisState.sampleLerpInv);
    if (draw->visState.scale != 0.0)
    {
        FX_SetPlacement(draw, &placement);
        if (draw->elemDef->elemType != 5)
            MyAssertHandler(
                ".\\EffectsCore\\fx_draw.cpp",
                844,
                0,
                "%s\n\t(draw->elemDef->elemType) = %i",
                "(draw->elemDef->elemType == FX_ELEM_TYPE_MODEL)",
                draw->elemDef->elemType);
        if ((draw->elemDef->flags & 0x8000000) != 0)
            FX_SetPlacementFromPhysics(draw, &placement.base);
        visuals.anonymous = FX_GetElemVisuals(draw->elemDef, draw->randomSeed).anonymous;
        if (!visuals.anonymous)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 851, 0, "%s", "visuals.model");
        R_FilterXModelIntoScene(visuals.model, &placement, 0, (uint16_t *)&draw->elem->u);
    }
}

void __cdecl FX_SetPlacementFromPhysics(const FxDrawState *draw, GfxPlacement *placement)
{
    if (!draw || !draw->system || !draw->effect || !draw->elem
        || !placement)
    {
        Com_Error(ERR_DROP, "Missing FX physics draw state");
        return;
    }
    if (!FX_CurrentThreadOwnsCooperativeIterator(draw->system)
        || !FX_ThreadOwnsEffectLock(draw->system, draw->effect))
    {
        Com_Error(
            ERR_DROP,
            "FX physics draw requires cooperative and effect ownership");
        return;
    }
    const fx::physics::BodySidecar *const sidecar =
        FX_GetPhysicsBodySidecar(draw->system);
    std::int32_t ownerIndex = -1;
    if (!sidecar
        || !FxPoolItemIndex<FxElem, MAX_ELEMS>(
            draw->system->elems, draw->elem, &ownerIndex))
    {
        Com_Error(ERR_DROP, "Invalid FX physics draw owner");
        return;
    }

    Sys_EnterCriticalSection(CRITSECT_PHYSICS);
    // The owning effect lock keeps this pool-slot incarnation live. FX free
    // then clears the slot under PHYSICS -> FX_ALLOC, so sampling the token
    // under PHYSICS also keeps Resolve atomic with sidecar transfer.
    const fx::physics::BodyToken token =
        fx::physics::TokenFromLegacyField(draw->elem->physObjId);
    const fx::physics::BodyResult body = fx::physics::Resolve(
        sidecar, static_cast<std::size_t>(ownerIndex), token);
    if (body)
    {
        Phys_ObjGetInterpolatedState(
            PHYS_WORLD_FX,
            body.body,
            placement->origin,
            placement->quat);
    }
    Sys_LeaveCriticalSection(CRITSECT_PHYSICS);
    if (!body)
    {
        Com_Error(
            ERR_DROP,
            "Invalid FX physics draw binding (%u)",
            static_cast<unsigned>(body.status));
    }
}

void __cdecl FX_DrawElem_Light(FxDrawState *draw)
{
    float green; // [esp+4Ch] [ebp-Ch]
    float blue; // [esp+50h] [ebp-8h]
    float red; // [esp+54h] [ebp-4h]

    if (!FX_CullElementForDraw_Light(draw))
    {
        {
            PROF_SCOPED("FX_EvalVisState");
            FX_EvaluateVisualState(&draw->preVisState, draw->msecLifeSpan, &draw->visState);
        }
        red = (double)draw->visState.color[2] * 0.003921568859368563;
        green = (double)draw->visState.color[1] * 0.003921568859368563;
        blue = (double)draw->visState.color[0] * 0.003921568859368563;
        R_AddOmniLightToScene(draw->posWorld, draw->visState.size[0], red, green, blue);
    }
}

bool __cdecl FX_CullElementForDraw_Light(const FxDrawState *draw)
{
    bool result; // al
    uint32_t frustumPlaneCount; // [esp+4h] [ebp-4h]

    result = 0;
    if (fx_cull_elem_draw->current.enabled)
    {
        frustumPlaneCount = FX_CullElementForDraw_FrustumPlaneCount(draw);
        if (FX_CullSphere(draw->camera, frustumPlaneCount, draw->posWorld, draw->visState.size[0]))
            return 1;
    }
    return result;
}

void __cdecl FX_DrawElem_SpotLight(FxDrawState *draw)
{
    float green; // [esp+4Ch] [ebp-Ch]
    float blue; // [esp+50h] [ebp-8h]
    float red; // [esp+54h] [ebp-4h]

    if (!FX_CullElementForDraw_Light(draw))
    {
        {
            PROF_SCOPED("FX_EvalVisState");
            FX_EvaluateVisualState(&draw->preVisState, draw->msecLifeSpan, &draw->visState);
        }
        red = (double)draw->visState.color[2] * 0.003921568859368563;
        green = (double)draw->visState.color[1] * 0.003921568859368563;
        blue = (double)draw->visState.color[0] * 0.003921568859368563;
        R_AddSpotLightToScene(draw->posWorld, draw->orient.axis[0], draw->visState.size[0], red, green, blue);
    }
}

void __cdecl FX_DrawNonSpriteElems(FxSystem *system)
{
    FxEffect *effect; // [esp+3Ch] [ebp-8h]
    std::uint32_t activeIndex; // [esp+40h] [ebp-4h]

    PROF_SCOPED("FX_DrawElems");
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1370, 0, "%s", "system");
    FX_BeginIteratingOverEffects_Cooperative(system);
    FX_BeginReadingCameraPublication(system);
    if (!Sys_AtomicLoad(&system->camera.isValid))
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1371, 0, "%s", "system->camera.isValid");
    const std::int32_t firstActiveEffect =
        Sys_AtomicLoad(&system->firstActiveEffect);
    const std::int32_t firstNewEffect =
        Sys_AtomicLoad(&system->firstNewEffect);
    const std::int64_t activeEffectCount64 =
        static_cast<std::int64_t>(firstNewEffect) - firstActiveEffect;
    if (firstActiveEffect < 0 || firstNewEffect < firstActiveEffect
        || activeEffectCount64 > FX_EFFECT_LIMIT)
        FX_DropCorruptDrawList("active effect ring exceeds capacity");
    const std::uint32_t activeEffectCount =
        static_cast<std::uint32_t>(activeEffectCount64);
    for (std::uint32_t activeOffset = 0;
         activeOffset < activeEffectCount;
         ++activeOffset)
    {
        activeIndex =
            static_cast<std::uint32_t>(firstActiveEffect) + activeOffset;
        effect = FX_EffectFromHandle(system, system->allEffectHandles[activeIndex & 0x3FF]);
        if (FX_LockEffect(system, effect))
        {
            FX_DrawNonSpriteEffect(system, effect, 1u, system->msecDraw);
            FX_UnlockEffect(system, effect);
        }
    }
    FX_EndReadingCameraPublication(system);
    FX_EndIteratingOverEffects_Cooperative(system);
}

void __cdecl FX_BeginIteratingOverEffects_Cooperative(FxSystem *system)
{
    if (!system)
        FX_DropCorruptDrawList("missing cooperative iterator system");
    const std::uint32_t admissionGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    if (fx_cooperativeIteratorThreadState.depth != 0
        && fx_cooperativeIteratorThreadState.system != system)
    {
        // Refreshing the ownership query discards a generation made stale by
        // FX_ResetSystem; live cross-system nesting is unsupported.
        if (FX_CurrentThreadOwnsCooperativeIterator(system))
            FX_DropCorruptDrawList("cross-system cooperative iterator nesting");
    }
    if (FX_CurrentThreadOwnsCooperativeIterator(system))
    {
        FxIteratorBeginCooperative(&system->iteratorCount);
        ++fx_cooperativeIteratorThreadState.depth;
        return;
    }
    for (;;)
    {
        FX_WaitForArchiveGate(system);
        FX_WaitForEffectKillGate(system);
        FxIteratorBeginCooperative(&system->iteratorCount);
        const std::uint32_t currentGeneration =
            FX_GetCooperativeIteratorGeneration(system);
        const bool initialized = system->isInitialized != 0;
        const bool archiveActive = FX_ArchiveGateIsActive(system);
        const bool killActive = FX_EffectKillGateIsActive(system);
        if (!archiveActive && !killActive && initialized
            && currentGeneration == admissionGeneration)
        {
            fx_cooperativeIteratorThreadState.system = system;
            fx_cooperativeIteratorThreadState.generation = currentGeneration;
            fx_cooperativeIteratorThreadState.depth = 1;
            return;
        }
        std::int32_t remaining = -1;
        if (!FxIteratorEndCooperative(&system->iteratorCount, &remaining))
        {
            FX_DropCorruptDrawList(
                "failed to roll back cooperative iterator admission");
        }
        if (currentGeneration != admissionGeneration)
        {
            FX_DropCorruptDrawList(
                "cooperative iterator crossed an FX lifecycle boundary");
        }
        if (!initialized)
        {
            FX_DropCorruptDrawList(
                "cooperative iterator entered an uninitialized FX system");
        }
    }
}

bool __cdecl FX_TryBeginIteratingOverEffects_Cooperative(
    FxSystem *const system) noexcept
{
    if (!system)
        return false;

    const std::uint32_t admissionGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    if (fx_cooperativeIteratorThreadState.depth != 0
        && fx_cooperativeIteratorThreadState.system != system)
    {
        const FxSystem *const ownedSystem =
            fx_cooperativeIteratorThreadState.system;
        if (!ownedSystem
            || fx_cooperativeIteratorThreadState.generation
                != FX_GetCooperativeIteratorGeneration(ownedSystem))
        {
            fx_cooperativeIteratorThreadState = {};
        }
        else
        {
            return false;
        }
    }

    if (FX_CurrentThreadOwnsCooperativeIterator(system))
    {
        if (fx_cooperativeIteratorThreadState.system != system
            || !FxIteratorTryBeginCooperative(&system->iteratorCount))
        {
            return false;
        }
        ++fx_cooperativeIteratorThreadState.depth;
        return true;
    }

    // This report-free path is used by queries whose legacy result is neutral
    // outside an active FX lifetime. Do not inspect any mutable FxSystem field
    // until cooperative admission excludes archive/restore/reset mutation.
    if (FX_ArchiveGateIsActive(system)
        || FX_EffectKillGateIsActive(system)
        || !FxIteratorTryBeginCooperative(&system->iteratorCount))
    {
        return false;
    }

    const std::uint32_t currentGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    const bool initialized = system->isInitialized != 0;
    const bool archiveActive = FX_ArchiveGateIsActive(system);
    const bool killActive = FX_EffectKillGateIsActive(system);
    if (initialized && !archiveActive && !killActive
        && currentGeneration == admissionGeneration)
    {
        fx_cooperativeIteratorThreadState.system = system;
        fx_cooperativeIteratorThreadState.generation = currentGeneration;
        fx_cooperativeIteratorThreadState.depth = 1;
        return true;
    }

    std::int32_t remaining = -1;
    if (!FxIteratorEndCooperative(&system->iteratorCount, &remaining))
    {
        FX_DropCorruptDrawList(
            "failed to roll back report-free cooperative iterator admission");
    }
    return false;
}

bool __cdecl FX_DowngradeEffectKillExclusiveToCooperative(
    FxSystem *const system) noexcept
{
    if (!system || fx_cooperativeIteratorThreadState.depth != 0
        || !FX_ThreadOwnsEffectKillExclusive(system)
        || !FxIteratorDowngradeExclusiveToCooperative(
            &system->iteratorCount))
    {
        return false;
    }

    fx_cooperativeIteratorThreadState.system = system;
    fx_cooperativeIteratorThreadState.generation =
        FX_GetCooperativeIteratorGeneration(system);
    fx_cooperativeIteratorThreadState.depth = 1;
    if (FX_CompleteEffectKillExclusiveDowngrade(system))
        return true;

    fx_cooperativeIteratorThreadState = {};
    (void)Sys_AtomicCompareExchange(
        &system->iteratorCount, -1, 1);
    return false;
}

void __cdecl FX_EndIteratingOverEffects_Cooperative(FxSystem *system)
{
    std::int32_t remaining = -1;
    if (fx_cooperativeIteratorThreadState.depth == 0
        || fx_cooperativeIteratorThreadState.system != system
        || fx_cooperativeIteratorThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system))
    {
        fx_cooperativeIteratorThreadState = {};
        FX_DropCorruptDrawList("cooperative iterator ended without thread ownership");
    }
    if (!FxIteratorEndCooperative(&system->iteratorCount, &remaining))
    {
        fx_cooperativeIteratorThreadState = {};
        FX_DropCorruptDrawList("failed to release cooperative iterator ownership");
    }
    --fx_cooperativeIteratorThreadState.depth;
    if (fx_cooperativeIteratorThreadState.depth == 0)
        fx_cooperativeIteratorThreadState = {};

    if (remaining == 0
        && FxGarbageCollectionRequested(&system->needsGarbageCollection))
        FX_RunGarbageCollection(system);
}

void __cdecl FX_DrawNonSpriteEffect(FxSystem *system, FxEffect *effect, uint32_t elemClass, int32_t drawTime)
{
    uint16_t elemHandle; // [esp+0h] [ebp-BCh]
    FxDrawState drawState; // [esp+4h] [ebp-B8h] BYREF
    const FxElemDef *elemDef; // [esp+B0h] [ebp-Ch]
    FxElem *elem; // [esp+B8h] [ebp-4h]

    if (!system || !effect)
        FX_DropCorruptDrawList("missing non-sprite draw state");
    if (elemClass == 0 || elemClass >= 3)
        FX_DropCorruptDrawList("invalid non-sprite element class");
    if (!FX_CurrentThreadOwnsCooperativeIterator(system)
        || !FX_ThreadOwnsEffectLock(system, effect))
    {
        FX_DropCorruptDrawList(
            "non-sprite traversal requires cooperative and effect ownership");
    }
    drawState.effect = effect;
    drawState.msecDraw = drawTime;
    elemHandle = effect->firstElemHandle[elemClass];
    if (elemHandle != 0xFFFF)
    {
        drawState.system = system;
        std::size_t elemCount = 0;
        while (elemHandle != 0xFFFF)
        {
            if (elemCount++ >= MAX_ELEMS)
                FX_DropCorruptDrawList("non-sprite element chain exceeds pool capacity");
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
            elem = FX_GetAllocatedDrawElem(system, elemHandle);
            elemDef = FX_GetValidatedDrawElemDef(effect, elem->defIndex);
            const bool classMatches =
                (elemClass == 1
                    && (elemDef->elemType == FX_ELEM_TYPE_MODEL
                        || elemDef->elemType == FX_ELEM_TYPE_OMNI_LIGHT))
                || (elemClass == 2
                    && elemDef->elemType == FX_ELEM_TYPE_CLOUD);
            if (!classMatches)
            {
                FX_DropCorruptDrawList("element type does not match its non-sprite class");
            }
            FX_DrawElement(system, elemDef, elem, &drawState);
            elemHandle = elem->nextElemHandleInEffect;
        }
    }
}

void __cdecl FX_DrawElement_Setup_1_(
    FxSystem* system,
    FxDrawState* draw,
    int32_t elemMsecBegin,
    int32_t elemSequence,
    const float* elemOrigin,
    float* outRealNormTime)
{
    float opacity; // [esp+8h] [ebp-84h]
    int32_t msecElapsed; // [esp+7Ch] [ebp-10h]
    float normTime; // [esp+80h] [ebp-Ch]
    float msecElapsedFloat; // [esp+84h] [ebp-8h]

    draw->randomSeed = (elemMsecBegin + (uint32_t)draw->effect->randomSeed + 296 * elemSequence) % 0x1DF;
    draw->msecLifeSpan = (float)((((draw->elemDef->lifeSpanMsec.amplitude + 1)
        * LOWORD(fx_randomTable[draw->randomSeed + 17])) >> 16)
        + draw->elemDef->lifeSpanMsec.base);
    msecElapsed = draw->msecDraw - elemMsecBegin;
    msecElapsedFloat = (float)msecElapsed;
    normTime = msecElapsedFloat / draw->msecLifeSpan;
    if (outRealNormTime)
        *outRealNormTime = normTime;
    if (msecElapsed < (int)draw->msecLifeSpan)
    {
        if (msecElapsed > (int)draw->msecLifeSpan)
            MyAssertHandler(
                ".\\EffectsCore\\fx_draw.cpp",
                972,
                0,
                "msecElapsed <= static_cast< int32_t >( draw->msecLifeSpan )\n\t%i, %i",
                msecElapsed,
                (int)draw->msecLifeSpan);
        draw->msecElapsed = msecElapsedFloat;
        draw->normTimeUpdateEnd = normTime;
    }
    else
    {
        draw->msecElapsed = draw->msecLifeSpan;
        draw->normTimeUpdateEnd = 1.0;
    }
    FX_GetOrientation(
        draw->elemDef,
        &draw->effect->frameAtSpawn,
        &draw->effect->frameNow,
        draw->randomSeed,
        &draw->orient);
    FX_OrientationPosToWorldPos(&draw->orient, elemOrigin, draw->posWorld);
    {
        PROF_SCOPED("FX_EvalVisState");
        FX_SetupVisualState(draw->elemDef, draw->effect, draw->randomSeed, draw->normTimeUpdateEnd, &draw->preVisState);
        FX_EvaluateSize(&draw->preVisState, &draw->visState);
    }
    if ((draw->elemDef->flags & 0x1000) != 0)
        FX_EvaluateVisAlpha(&draw->preVisState, &draw->visState);
    draw->camera = &draw->system->camera;
    FX_EvaluateDistanceFade(draw);
    if ((draw->elemDef->flags & 0x1000) != 0)
    {
        opacity = (double)((draw->preVisState.distanceFade * draw->visState.color[3]) >> 8) * 0.003921568859368563;
        FX_AddVisBlocker(system, draw->posWorld, draw->visState.size[0], opacity);
    }
}

using ElementHandlerFn = void(*)(FxDrawState*);

static ElementHandlerFn s_drawElemHandler[8] =
{
    FX_DrawElem_BillboardSprite,
    FX_DrawElem_OrientedSprite,
    FX_DrawElem_Tail,
    NULL,
    FX_DrawElem_Cloud,
    FX_DrawElem_Model,
    FX_DrawElem_Light,
    FX_DrawElem_SpotLight
};

void __cdecl FX_DrawElement(FxSystem *system, const FxElemDef *elemDef, const FxElem *elem, FxDrawState *draw)
{
    if (!elemDef || !elem || !draw)
        FX_DropCorruptDrawList("missing element draw state");
    if (elemDef->elemType >= 8u)
        FX_DropCorruptDrawList("element type has no draw handler");
    if (elemDef->visualCount && elem->msecBegin <= draw->msecDraw)
    {
        draw->elem = elem;
        draw->elemDef = elemDef;
        FX_DrawElement_Setup_1_(system, draw, elem->msecBegin, elem->sequence, elem->origin, 0);
        if (!s_drawElemHandler[elemDef->elemType])
            FX_DropCorruptDrawList("element type maps to a null draw handler");
        s_drawElemHandler[elemDef->elemType](draw);
    }
}

void __cdecl FX_DrawSpotLight(FxSystem *system)
{
    FxEffect *v1; // eax
    volatile int32_t msecDraw; // [esp-4h] [ebp-Ch]

    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1389, 0, "%s", "system");
    FX_BeginIteratingOverEffects_Cooperative(system);
    FX_BeginReadingCameraPublication(system);
    if (!Sys_AtomicLoad(&system->camera.isValid))
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1390, 0, "%s", "system->camera.isValid");
    FxSpotLightStateSnapshot spotLightSnapshot{};
    if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot))
        FX_DropCorruptDrawList("missing spotlight state");
    const std::int32_t activeSpotLightEffectCount =
        spotLightSnapshot.effectCount;
    const std::int32_t activeSpotLightElemCount =
        spotLightSnapshot.elemCount;
    if (activeSpotLightEffectCount < 0
        || activeSpotLightEffectCount > 1
        || activeSpotLightElemCount < 0
        || activeSpotLightElemCount > activeSpotLightEffectCount)
    {
        FX_EndReadingCameraPublication(system);
        FX_EndIteratingOverEffects_Cooperative(system);
        FX_DropCorruptDrawList("invalid spotlight count pair");
    }
    if (activeSpotLightElemCount == 1)
    {
        msecDraw = system->msecDraw;
        v1 = FX_EffectFromHandle(system, spotLightSnapshot.effectHandle);
        const bool effectLocked = FX_LockEffect(system, v1);
        FxSpotLightStateSnapshot lockedSnapshot{};
        if (effectLocked
            && !FX_GetSpotLightStateSnapshot(system, &lockedSnapshot))
            FX_DropCorruptDrawList("missing locked spotlight state");
        if (effectLocked && lockedSnapshot.effectCount == 1
            && lockedSnapshot.elemCount == 1
            && lockedSnapshot.effectHandle == spotLightSnapshot.effectHandle
            && lockedSnapshot.elemHandle == spotLightSnapshot.elemHandle)
        {
            FX_DrawSpotLightEffect(system, v1, msecDraw);
        }
        if (effectLocked)
            FX_UnlockEffect(system, v1);
    }
    FX_EndReadingCameraPublication(system);
    FX_EndIteratingOverEffects_Cooperative(system);
}

void __cdecl FX_DrawSpotLightEffect(FxSystem *system, FxEffect *effect, int32_t drawTime)
{
    uint16_t activeSpotLightElemHandle; // [esp+2h] [ebp-BAh]
    FxDrawState drawState; // [esp+4h] [ebp-B8h] BYREF
    const FxElemDef *elemDef; // [esp+B0h] [ebp-Ch]
    FxElem *elem; // [esp+B8h] [ebp-4h]

    if (!system || !effect)
        FX_DropCorruptDrawList("missing spotlight draw state");
    if (!FX_CurrentThreadOwnsCooperativeIterator(system)
        || !FX_ThreadOwnsEffectLock(system, effect))
    {
        FX_DropCorruptDrawList(
            "spotlight traversal requires cooperative and effect ownership");
    }
    FxSpotLightStateSnapshot spotLightSnapshot{};
    if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot))
        FX_DropCorruptDrawList("missing spotlight draw snapshot");
    if (spotLightSnapshot.effectCount != 1
        || spotLightSnapshot.elemCount != 1)
        FX_DropCorruptDrawList("spotlight draw requested without an exact count pair");
    if (effect
        != FX_EffectFromHandle(system, spotLightSnapshot.effectHandle))
    {
        FX_DropCorruptDrawList("spotlight effect handle does not match the draw effect");
    }
    drawState.effect = effect;
    drawState.system = system;
    drawState.msecDraw = drawTime;
    activeSpotLightElemHandle = spotLightSnapshot.elemHandle;
    if (!system)
        MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
    elem = FX_GetAllocatedDrawElem(system, activeSpotLightElemHandle);
    elemDef = FX_GetValidatedDrawElemDef(effect, elem->defIndex);
    if (elemDef->elemType != 7)
        FX_DropCorruptDrawList("spotlight element has a non-spotlight definition");
    FX_DrawElement(system, elemDef, elem, &drawState);
}

void __cdecl FX_DrawSpriteElems(FxSystem *system, int32_t drawTime)
{
    int32_t numTrailEffects; // [esp+38h] [ebp-820h]
    uint16_t effectHandle; // [esp+3Ch] [ebp-81Ch]
    FxEffect *effect; // [esp+40h] [ebp-818h]
    FxEffect *effecta; // [esp+40h] [ebp-818h]
    FxSpriteInfo *sprite; // [esp+44h] [ebp-814h]
    uint16_t trailEffects[1026]; // [esp+48h] [ebp-810h]
    int32_t i; // [esp+850h] [ebp-8h]
    std::uint32_t activeIndex; // [esp+854h] [ebp-4h]

    PROF_SCOPED("FX_DrawElems");
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1510, 0, "%s", "system");
    FX_BeginIteratingOverEffects_Cooperative(system);
    FX_BeginReadingCameraPublication(system);
    if (!Sys_AtomicLoad(&system->camera.isValid))
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1511, 0, "%s", "system->camera.isValid");
    Sys_AtomicStore(&system->gfxCloudCount, 0);
    sprite = &system->sprite;
    system->sprite.indices = 0;
    system->sprite.indexCount = 0;
    system->sprite.name = 0;
    system->sprite.material = 0;
    numTrailEffects = 0;
    const std::int32_t firstActiveEffect =
        Sys_AtomicLoad(&system->firstActiveEffect);
    const std::int32_t firstNewEffect =
        Sys_AtomicLoad(&system->firstNewEffect);
    const std::int64_t activeEffectCount64 =
        static_cast<std::int64_t>(firstNewEffect) - firstActiveEffect;
    if (firstActiveEffect < 0 || firstNewEffect < firstActiveEffect
        || activeEffectCount64 > FX_EFFECT_LIMIT)
        FX_DropCorruptDrawList("sprite effect ring exceeds capacity");
    const std::uint32_t activeEffectCount =
        static_cast<std::uint32_t>(activeEffectCount64);
    for (std::uint32_t activeOffset = 0;
         activeOffset < activeEffectCount;
         ++activeOffset)
    {
        activeIndex =
            static_cast<std::uint32_t>(firstActiveEffect) + activeOffset;
        effectHandle = system->allEffectHandles[activeIndex & 0x3FF];
        effect = FX_EffectFromHandle(system, effectHandle);
        if (!FX_LockEffect(system, effect))
            continue;
        FX_DrawSpriteEffect(system, effect, drawTime);
        FX_DrawNonSpriteEffect(system, effect, 2u, drawTime);
        if (effect->firstTrailHandle != 0xFFFF)
        {
            if (numTrailEffects >= FX_EFFECT_LIMIT)
                FX_DropCorruptDrawList("trail effect list exceeds capacity");
            trailEffects[numTrailEffects++] = effectHandle;
        }
        FX_UnlockEffect(system, effect);
    }
    if (numTrailEffects > 0)
    {
        for (i = 0; i < numTrailEffects; ++i)
        {
            effecta = FX_EffectFromHandle(system, trailEffects[i]);
            if (FX_LockEffect(system, effecta))
            {
                FX_DrawTrailsForEffect(system, effecta, drawTime);
                FX_UnlockEffect(system, effecta);
            }
        }
    }
    if (system->sprite.indexCount)
    {
        if (!system->sprite.name)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1555, 0, "%s", "sprite->name");
        if (!system->sprite.material)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1556, 0, "%s", "sprite->material");
        if (!sprite->indices)
            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1557, 0, "%s", "sprite->indices");
        R_AddCodeMeshDrawSurf(
            system->sprite.material,
            system->sprite.indices,
            system->sprite.indexCount,
            0,
            0,
            system->sprite.name);
        system->sprite.indexCount = 0;
        sprite->indices = 0;
    }
    FX_EndReadingCameraPublication(system);
    FX_EndIteratingOverEffects_Cooperative(system);
}

void __cdecl FX_DrawTrailsForEffect(FxSystem *system, FxEffect *effect, int32_t drawTime)
{
    FxDrawState drawState; // [esp+0h] [ebp-B8h] BYREF
    uint16_t trailHandle; // [esp+B0h] [ebp-8h]
    FxTrail *trail; // [esp+B4h] [ebp-4h]

    if (!system || !effect)
        FX_DropCorruptDrawList("missing trail draw state");
    if (!FX_CurrentThreadOwnsCooperativeIterator(system)
        || !FX_ThreadOwnsEffectLock(system, effect))
    {
        FX_DropCorruptDrawList(
            "trail traversal requires cooperative and effect ownership");
    }
    drawState.system = system;
    drawState.effect = effect;
    drawState.msecDraw = drawTime;
    std::size_t trailCount = 0;
    for (trailHandle = effect->firstTrailHandle; trailHandle != 0xFFFF; trailHandle = trail->nextTrailHandle)
    {
        if (trailCount++ >= MAX_TRAILS)
            FX_DropCorruptDrawList("trail chain exceeds pool capacity");
        if (!system)
            MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 362, 0, "%s", "system");
        trail = FX_GetAllocatedDrawTrail(system, trailHandle);
        FX_DrawTrail(system, &drawState, trail);
    }
}

void __cdecl FX_DrawTrail(FxSystem *system, FxDrawState *draw, FxTrail *trail)
{
    double v3; // st7
    const char *v4; // eax
    float v5; // [esp+24h] [ebp-194h]
    float v6; // [esp+98h] [ebp-120h]
    FxTrailSegmentDrawState tailSegmentDrawState; // [esp+A0h] [ebp-118h] BYREF
    float alpha; // [esp+DCh] [ebp-DCh]
    r_double_index_t *reservedIndices; // [esp+E0h] [ebp-D8h] BYREF
    int32_t trailDefIndCount; // [esp+E4h] [ebp-D4h]
    FxTrailSegmentDrawState lastSegmentDrawState; // [esp+E8h] [ebp-D0h] BYREF
    FxElemVisuals visuals; // [esp+124h] [ebp-94h]
    uint16_t vertsPerSegment; // [esp+128h] [ebp-90h]
    int32_t upperBoundSegmentCount; // [esp+12Ch] [ebp-8Ch]
    FxTrailSegmentDrawState segmentDrawState; // [esp+130h] [ebp-88h] BYREF
    int32_t exactSegmentCount; // [esp+170h] [ebp-48h]
    FxSpriteInfo *sprite; // [esp+174h] [ebp-44h]
    uint16_t reservedBaseVertex; // [esp+178h] [ebp-40h] BYREF
    GfxPackedVertex *reservedVerts; // [esp+17Ch] [ebp-3Ch]
    int32_t indicesToReserve; // [esp+180h] [ebp-38h]
    const FxTrailElem *trailElem; // [esp+184h] [ebp-34h]
    float uCoordOffset; // [esp+188h] [ebp-30h]
    float basis[2][3]; // [esp+18Ch] [ebp-2Ch] BYREF
    float lastSegmentNormTime; // [esp+1A4h] [ebp-14h]
    bool haveLastSegmentDrawState;
    int32_t trailDefVertCount; // [esp+1A8h] [ebp-10h]
    int32_t curSegment; // [esp+1ACh] [ebp-Ch]
    uint16_t trailElemHandle; // [esp+1B0h] [ebp-8h]
    float segmentNormTime; // [esp+1B4h] [ebp-4h] BYREF

    if (!system || !draw || !draw->effect || !trail)
        FX_DropCorruptDrawList("missing trail draw state");
    sprite = &system->sprite;
    draw->elemDef =
        FX_GetValidatedDrawElemDef(draw->effect, trail->defIndex);
    FX_ValidateDrawTrailDef(draw->elemDef);
    if (draw->elemDef->visualCount)
    {
        trailElemHandle = trail->firstElemHandle;
        if (trailElemHandle != 0xFFFF)
        {
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 348, 0, "%s", "system");
            trailElem = FX_GetAllocatedDrawTrailElem(system, trailElemHandle);
            v6 = trailElem->spawnDist / (double)draw->elemDef->trailDef->repeatDist;
            v5 = floor(v6);
            uCoordOffset = -v5;
            if (draw->elemDef->trailDef->scrollTimeMsec)
            {
                const std::int64_t scrollDivisor =
                    draw->elemDef->trailDef->scrollTimeMsec <= 0
                    ? static_cast<std::int64_t>(
                        draw->elemDef->trailDef->scrollTimeMsec)
                    : -static_cast<std::int64_t>(
                        draw->elemDef->trailDef->scrollTimeMsec);
                const double scrollFraction =
                    static_cast<double>(
                        static_cast<std::int64_t>(draw->msecDraw)
                        % scrollDivisor)
                    / static_cast<double>(scrollDivisor);
                if (draw->elemDef->trailDef->scrollTimeMsec <= 0)
                    v3 = 1.0
                    - scrollFraction
                    + uCoordOffset;
                else
                    v3 = scrollFraction + uCoordOffset;
                uCoordOffset = v3;
            }
            upperBoundSegmentCount = 0;
            std::size_t traversedTrailElemCount = 0;
            for (trailElemHandle = trail->firstElemHandle;
                trailElemHandle != 0xFFFF;
                trailElemHandle = trailElem->nextTrailElemHandle)
            {
                if (traversedTrailElemCount++ >= MAX_TRAIL_ELEMS)
                    FX_DropCorruptDrawList("trail-element count chain exceeds pool capacity");
                if (!system)
                    MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 348, 0, "%s", "system");
                trailElem =
                    FX_GetAllocatedDrawTrailElem(system, trailElemHandle);
                if (trailElem->msecBegin <= draw->msecDraw)
                    ++upperBoundSegmentCount;
            }
            trailDefVertCount = draw->elemDef->trailDef->vertCount;
            trailDefIndCount = draw->elemDef->trailDef->indCount;
            if (upperBoundSegmentCount > 0
                && trailDefVertCount
                    > (std::numeric_limits<std::int32_t>::max)()
                        / upperBoundSegmentCount)
            {
                FX_DropCorruptDrawList("trail vertex reservation overflows");
            }
            const std::int32_t verticesToReserve =
                trailDefVertCount * upperBoundSegmentCount;
            if (verticesToReserve
                > (std::numeric_limits<std::uint16_t>::max)())
            {
                FX_DropCorruptDrawList("trail vertex reservation exceeds the mesh index space");
            }
            if (R_ReserveCodeMeshVerts(verticesToReserve, &reservedBaseVertex))
            {
                reservedVerts = R_GetCodeMeshVerts(reservedBaseVertex);
                exactSegmentCount = 0;
                lastSegmentNormTime = 1.0;
                haveLastSegmentDrawState = false;
                memset((uint8_t *)&lastSegmentDrawState, 0, sizeof(lastSegmentDrawState));
                traversedTrailElemCount = 0;
                for (trailElemHandle = trail->firstElemHandle;
                    trailElemHandle != 0xFFFF;
                    trailElemHandle = trailElem->nextTrailElemHandle)
                {
                    if (traversedTrailElemCount++ >= MAX_TRAIL_ELEMS)
                        FX_DropCorruptDrawList("trail-element draw chain exceeds pool capacity");
                    if (!system)
                        MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 348, 0, "%s", "system");
                    trailElem =
                        FX_GetAllocatedDrawTrailElem(system, trailElemHandle);
                    if (trailElem->msecBegin <= draw->msecDraw)
                    {
                        FX_DrawElement_Setup_1_(
                            system,
                            draw,
                            trailElem->msecBegin,
                            trailElem->sequence,
                            trailElem->origin,
                            &segmentNormTime);
                        FX_TrailElem_UncompressBasis(trailElem->basis, basis);
                        {
                            PROF_SCOPED("FX_EvalVisState");
                            FX_EvaluateVisualState(&draw->preVisState, draw->msecLifeSpan, &draw->visState);
                        }
                        Fx_GenTrail_PopulateSegmentDrawState(draw, trailElem->spawnDist, uCoordOffset, basis, &segmentDrawState);
                        if (segmentNormTime < 1.0)
                        {
                            if (trailElemHandle == trail->firstElemHandle)
                            {
                                if (!trailElem->sequence)
                                    segmentDrawState.color[3] = 0;
                            }
                            else if (haveLastSegmentDrawState
                                && lastSegmentNormTime >= 1.0)
                            {
                                if (exactSegmentCount >= upperBoundSegmentCount)
                                    FX_DropCorruptDrawList("trail tail segment exceeds its vertex reservation");
                                memcpy(&tailSegmentDrawState, &lastSegmentDrawState, sizeof(tailSegmentDrawState));
                                alpha = (1.0 - lastSegmentNormTime) / (segmentNormTime - lastSegmentNormTime);
                                tailSegmentDrawState.uCoord = (segmentDrawState.uCoord - lastSegmentDrawState.uCoord) * alpha
                                    + lastSegmentDrawState.uCoord;
                                Vec3Lerp(lastSegmentDrawState.posWorld, segmentDrawState.posWorld, alpha, tailSegmentDrawState.posWorld);
                                Vec3Lerp(lastSegmentDrawState.basis[0], segmentDrawState.basis[0], alpha, tailSegmentDrawState.basis[0]);
                                Vec3Lerp(lastSegmentDrawState.basis[1], segmentDrawState.basis[1], alpha, tailSegmentDrawState.basis[1]);
                                FX_GenTrail_VertsForSegment(
                                    &tailSegmentDrawState,
                                    &reservedVerts[trailDefVertCount * exactSegmentCount++]);
                            }
                            if (exactSegmentCount >= upperBoundSegmentCount)
                                FX_DropCorruptDrawList("trail segment exceeds its vertex reservation");
                            FX_GenTrail_VertsForSegment(&segmentDrawState, &reservedVerts[trailDefVertCount * exactSegmentCount++]);
                        }
                        lastSegmentNormTime = segmentNormTime;
                        memcpy(&lastSegmentDrawState, &segmentDrawState, sizeof(lastSegmentDrawState));
                        haveLastSegmentDrawState = true;
                    }
                }
                if (exactSegmentCount > upperBoundSegmentCount)
                {
                    v4 = va(
                        "Too optimistic: exactSegmentCount = %i, upperBoundSegmentCount = %i",
                        exactSegmentCount,
                        upperBoundSegmentCount);
                    MyAssertHandler(
                        ".\\EffectsCore\\fx_draw.cpp",
                        1157,
                        0,
                        "%s\n\t%s",
                        "exactSegmentCount <= upperBoundSegmentCount",
                        v4);
                }
                if (exactSegmentCount > 1)
                {
                    indicesToReserve = 3 * trailDefIndCount;
                    vertsPerSegment = draw->elemDef->trailDef->vertCount;
                    visuals.anonymous = FX_GetElemVisuals(draw->elemDef, draw->randomSeed).anonymous;
                    if (sprite->material != visuals.anonymous && sprite->indexCount)
                    {
                        if (!sprite->name)
                            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1169, 0, "%s", "sprite->name");
                        if (!sprite->material)
                            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1170, 0, "%s", "sprite->material");
                        if (!sprite->indices)
                            MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 1171, 0, "%s", "sprite->indices");
                        R_AddCodeMeshDrawSurf(sprite->material, sprite->indices, sprite->indexCount, 0, 0, sprite->name);
                        sprite->indexCount = 0;
                    }
                    curSegment = 0;
                    while (curSegment < exactSegmentCount - 1 && R_ReserveCodeMeshIndices(indicesToReserve, &reservedIndices))
                    {
                        if (sprite->material != visuals.anonymous)
                        {
                            sprite->name = draw->effect->def->name;
                            sprite->material = visuals.material;
                            sprite->indices = reservedIndices;
                        }
                        sprite->indexCount += indicesToReserve;
                        FX_GenTrail_IndsForSegment(draw, reservedBaseVertex, reservedIndices);
                        ++curSegment;
                        reservedBaseVertex += vertsPerSegment;
                    }
                }
            }
        }
    }
}

void __cdecl FX_TrailElem_UncompressBasis(
    const std::int8_t (*inBasis)[3],
    float (*basis)[3])
{
    int32_t basisVecIter; // [esp+4h] [ebp-8h]
    int32_t dimIter; // [esp+8h] [ebp-4h]

    for (basisVecIter = 0; basisVecIter != 2; ++basisVecIter)
    {
        for (dimIter = 0; dimIter != 3; ++dimIter)
        {
            basis[basisVecIter][dimIter] =
                static_cast<double>(inBasis[basisVecIter][dimIter])
                * 0.007874015718698502;
        }
    }
}

void __cdecl FX_GenTrail_IndsForSegment(
    FxDrawState *draw,
    uint16_t reservedBaseVertex,
    r_double_index_t *outIndices)
{
    uint16_t quadInds_2; // [esp+2h] [ebp-2Eh]
    r_double_index_t quadInds_4; // [esp+4h] [ebp-2Ch]
    r_double_index_t index; // [esp+Ch] [ebp-24h]
    int32_t indCount; // [esp+10h] [ebp-20h]
    FxTrailDef *trailDef; // [esp+18h] [ebp-18h]
    uint16_t farBase; // [esp+20h] [ebp-10h]
    uint16_t *inds; // [esp+24h] [ebp-Ch]
    int32_t indPairIter; // [esp+28h] [ebp-8h]
    r_double_index_t *outIndicesa; // [esp+40h] [ebp+10h]

    trailDef = draw->elemDef->trailDef;
    if (!trailDef)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 414, 0, "%s", "trailDef");
    inds = trailDef->inds;
    indCount = trailDef->indCount;
    if (2 * (indCount / 2) != indCount)
        MyAssertHandler(
            ".\\EffectsCore\\fx_draw.cpp",
            419,
            0,
            "%s\n\t(trailDef->indCount) = %i",
            "(indPairCount * 2 == indCount)",
            trailDef->indCount);
    farBase = LOWORD(trailDef->vertCount) + reservedBaseVertex;
    for (indPairIter = 0; indPairIter != indCount / 2; ++indPairIter)
    {
        quadInds_2 = inds[2 * indPairIter + 1] + reservedBaseVertex;
        quadInds_4.value[0] = inds[2 * indPairIter] + farBase;
        quadInds_4.value[1] = inds[2 * indPairIter + 1] + farBase;
        index.value[0] = inds[2 * indPairIter] + reservedBaseVertex;
        index.value[1] = quadInds_2;
        *outIndices = index;
        outIndicesa = outIndices + 1;
        *outIndicesa = quadInds_4;
        index.value[0] = quadInds_4.value[0];
        index.value[1] = quadInds_2;
        outIndicesa[1] = index;
        outIndices = outIndicesa + 2;
    }
}

void __cdecl Fx_GenTrail_PopulateSegmentDrawState(
    FxDrawState *draw,
    float spawnDist,
    float uCoordOffset,
    const float (*basis)[3],
    FxTrailSegmentDrawState *outState)
{
    outState->trailDef = draw->elemDef->trailDef;
    outState->posWorld[0] = draw->posWorld[0];
    outState->posWorld[1] = draw->posWorld[1];
    outState->posWorld[2] = draw->posWorld[2];
    outState->basis[0][0] = basis[0][0];
    outState->basis[0][1] = basis[0][1];
    outState->basis[0][2] = basis[0][2];
    outState->basis[1][0] = basis[1][0];
    outState->basis[1][1] = basis[1][1];
    outState->basis[1][2] = basis[1][2];
    outState->rotation = draw->visState.rotationTotal;
    std::memcpy(outState->size, draw->visState.size, sizeof(outState->size));
    std::memcpy(outState->color, draw->visState.color, sizeof(outState->color));
    outState->uCoord = spawnDist / (double)draw->elemDef->trailDef->repeatDist + uCoordOffset;
}

void __cdecl FX_GenTrail_VertsForSegment(const FxTrailSegmentDrawState *segmentDrawState, GfxPackedVertex *remoteVerts)
{
    float scale1; // [esp+8h] [ebp-10Ch]
    __int16 v3; // [esp+38h] [ebp-DCh]
    __int16 v4; // [esp+3Ch] [ebp-D8h]
    float scale0; // [esp+40h] [ebp-D4h]
    float v6; // [esp+44h] [ebp-D0h]
    PackedUnitVec v7; // [esp+48h] [ebp-CCh]
    float v8; // [esp+50h] [ebp-C4h]
    float v9; // [esp+54h] [ebp-C0h]
    int32_t v10; // [esp+5Ch] [ebp-B8h]
    float texCoord; // [esp+68h] [ebp-ACh]
    int32_t v12; // [esp+70h] [ebp-A4h]
    float rotation; // [esp+94h] [ebp-80h]
    float normal; // [esp+9Ch] [ebp-78h]
    float normal_4; // [esp+A0h] [ebp-74h]
    float normal_8; // [esp+A4h] [ebp-70h]
    float temp; // [esp+ACh] [ebp-68h]
    float temp_4; // [esp+B0h] [ebp-64h]
    float temp_8; // [esp+B4h] [ebp-60h]
    float leftFloat4; // [esp+BCh] [ebp-58h]
    float leftFloat4_4; // [esp+C0h] [ebp-54h]
    float leftFloat4_8; // [esp+C4h] [ebp-50h]
    FxTrailDef *trailDef; // [esp+CCh] [ebp-48h]
    float cosRot; // [esp+D4h] [ebp-40h]
    float left[3]; // [esp+D8h] [ebp-3Ch] BYREF
    float sinRot; // [esp+E4h] [ebp-30h]
    float4 upFloat4; // [esp+E8h] [ebp-2Ch]
    int32_t uCoord; // [esp+FCh] [ebp-18h]
    float up[3]; // [esp+100h] [ebp-14h] BYREF
    int32_t vertCount; // [esp+10Ch] [ebp-8h]
    int32_t vertIter; // [esp+110h] [ebp-4h]

    trailDef = segmentDrawState->trailDef;
    if (!segmentDrawState->trailDef)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 499, 0, "%s", "trailDef");
    if (trailDef->vertCount <= 0)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 500, 0, "%s", "trailDef->vertCount > 0");
    if (trailDef->indCount <= 0)
        MyAssertHandler(".\\EffectsCore\\fx_draw.cpp", 501, 0, "%s", "trailDef->indCount > 0");
    rotation = segmentDrawState->rotation;
    cosRot = cos(rotation);
    sinRot = sin(rotation);
    Vec3ScaleMad(cosRot, segmentDrawState->basis[0], sinRot, segmentDrawState->basis[1], left);
    scale1 = -cosRot;
    Vec3ScaleMad(sinRot, segmentDrawState->basis[0], scale1, segmentDrawState->basis[1], up);
    leftFloat4 = left[0];
    leftFloat4_4 = left[1];
    leftFloat4_8 = left[2];
    upFloat4.v[0] = up[0];
    upFloat4.v[1] = up[1];
    upFloat4.v[2] = up[2];
    upFloat4.v[3] = 0.0;
    uCoord = LODWORD(segmentDrawState->uCoord);
    vertCount = trailDef->vertCount;
    for (vertIter = 0; vertIter != vertCount; ++vertIter)
    {
        v6 = trailDef->verts[vertIter].pos[1] * segmentDrawState->size[1];
        scale0 = trailDef->verts[vertIter].pos[0] * segmentDrawState->size[0];
        Vec3MadMad(segmentDrawState->posWorld, scale0, left, v6, up, remoteVerts->xyz);
        remoteVerts->color.packed = *(uint32_t *)segmentDrawState->color;
        if ((int)((2 * uCoord) ^ 0x80000000) >> 14 < 0x3FFF)
            v12 = (int)((2 * uCoord) ^ 0x80000000) >> 14;
        else
            v12 = 0x3FFF;
        if (v12 > -16384)
            v4 = v12;
        else
            v4 = -16384;
        texCoord = trailDef->verts[vertIter].texCoord;
        if ((int)((2 * LODWORD(texCoord)) ^ 0x80000000) >> 14 < 0x3FFF)
            v10 = (int)((2 * LODWORD(texCoord)) ^ 0x80000000) >> 14;
        else
            v10 = 0x3FFF;
        if (v10 > -16384)
            v3 = v10;
        else
            v3 = -16384;
        remoteVerts->texCoord.packed = (v3 & 0x3FFF | ((int)LODWORD(texCoord) >> 16) & 0xC000)
            + ((v4 & 0x3FFF | (uCoord >> 16) & 0xC000) << 16);
        v9 = trailDef->verts[vertIter].normal[0];
        temp = v9 * leftFloat4;
        temp_4 = v9 * leftFloat4_4;
        temp_8 = v9 * leftFloat4_8;
        v8 = trailDef->verts[vertIter].normal[1];
        normal = v8 * upFloat4.v[0] + temp;
        normal_4 = v8 * upFloat4.v[1] + temp_4;
        normal_8 = v8 * upFloat4.v[2] + temp_8;
        v7.array[0] = (int)(normal * 127.0 + 127.5);
        v7.array[1] = (int)(normal_4 * 127.0 + 127.5);
        v7.array[2] = (int)(normal_8 * 127.0 + 127.5);
        v7.array[3] = 63;
        remoteVerts->normal = v7;
        remoteVerts->tangent.packed = 1065320446;
        ++remoteVerts;
    }
}

void __cdecl FX_DrawSpriteEffect(FxSystem *system, FxEffect *effect, int32_t drawTime)
{
    uint16_t elemHandle; // [esp+0h] [ebp-C0h]
    const FxElemDef *elemDef; // [esp+4h] [ebp-BCh]
    FxDrawState drawState; // [esp+8h] [ebp-B8h] BYREF
    FxElem *elem; // [esp+BCh] [ebp-4h]

    if (!system || !effect)
        FX_DropCorruptDrawList("missing sprite draw state");
    if (!FX_CurrentThreadOwnsCooperativeIterator(system)
        || !FX_ThreadOwnsEffectLock(system, effect))
    {
        FX_DropCorruptDrawList(
            "sprite traversal requires cooperative and effect ownership");
    }
    drawState.effect = effect;
    drawState.msecDraw = drawTime;
    elemHandle = effect->firstElemHandle[0];
    if (elemHandle != 0xFFFF)
    {
        drawState.system = system;
        std::size_t elemCount = 0;
        while (elemHandle != 0xFFFF)
        {
            if (elemCount++ >= MAX_ELEMS)
                FX_DropCorruptDrawList("sprite element chain exceeds pool capacity");
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
            elem = FX_GetAllocatedDrawElem(system, elemHandle);
            elemDef = FX_GetValidatedDrawElemDef(effect, elem->defIndex);
            if (elemDef->elemType > FX_ELEM_TYPE_TAIL)
                FX_DropCorruptDrawList("element type does not match its sprite class");
            if (elemDef->visualCount)
                FX_DrawElement(system, elemDef, elem, &drawState);
            elemHandle = elem->nextElemHandleInEffect;
        }
    }
}

void __cdecl FX_GenerateVerts(FxGenerateVertsCmd *cmd)
{
    int32_t drawTime; // [esp+50h] [ebp-8h]
    FxSystem *localSystem; // [esp+54h] [ebp-4h]

    PROF_SCOPED("FX_GenerateVerts");

    localSystem = cmd->system;
    FX_BeginIteratingOverEffects_Cooperative(localSystem);
    FX_BeginReadingCameraPublication(localSystem);
    R_BeginCodeMeshVerts();
    drawTime = localSystem->msecDraw;
    if (drawTime >= 0)
    {
        FX_SpriteGenerateVerts(cmd);
        FX_Beam_GenerateVerts(cmd);
        FX_PostLight_GenerateVerts(cmd->postLightInfo, localSystem);
        if (fx_enable->current.enabled && fx_draw->current.enabled)
            FX_DrawSpriteElems(localSystem, drawTime);
        R_EndCodeMeshVerts();
        FX_ToggleVisBlockerFrame(localSystem);
    }
    else
    {
        R_EndCodeMeshVerts();
    }
    FX_EndReadingCameraPublication(localSystem);
    FX_EndIteratingOverEffects_Cooperative(localSystem);
}

void __cdecl FX_FillGenerateVertsCmd(int32_t localClientNum, FxGenerateVertsCmd* cmd)
{
    uint32_t v2; // [esp+0h] [ebp-10h]
    uint32_t v3; // [esp+Ch] [ebp-4h]
    cg_s *cgameGlob;

    iassert(cmd);
    cmd->system = FX_GetSystem(localClientNum);
    cmd->beamInfo = FX_Beam_GetInfo();
    cmd->postLightInfo = FX_PostLight_GetInfo();
    cmd->spriteInfo = FX_SpriteGetInfo();
    cmd->localClientNum = localClientNum;

    cgameGlob = CG_GetLocalClientGlobals(R_GetLocalClientNum());

    cmd->vieworg[0] = cgameGlob->refdef.vieworg[0];
    cmd->vieworg[1] = cgameGlob->refdef.vieworg[1];
    cmd->vieworg[2] = cgameGlob->refdef.vieworg[2];

    AxisCopy(cgameGlob->refdef.viewaxis, cmd->viewaxis);
}

void __cdecl FX_EvaluateDistanceFade(FxDrawState *draw)
{
    float v1; // [esp+18h] [ebp-3Ch]
    float v2; // [esp+1Ch] [ebp-38h]
    float diff[3]; // [esp+38h] [ebp-1Ch] BYREF
    const FxElemDef *def; // [esp+44h] [ebp-10h]
    float dist; // [esp+48h] [ebp-Ch]
    float fadeInFrac; // [esp+4Ch] [ebp-8h]
    float fadeOutFrac; // [esp+50h] [ebp-4h]

    def = draw->elemDef;
    if (def->fadeInRange.amplitude != 0.0 || def->fadeOutRange.amplitude != 0.0)
    {
        fadeInFrac = 1.0;
        fadeOutFrac = 1.0;
        Vec3Sub(draw->posWorld, draw->camera->origin, diff);
        dist = Vec3Length(diff);
        if (def->fadeInRange.amplitude != 0.0)
            fadeInFrac = FX_ClampRangeLerp(dist, &def->fadeInRange);
        if (def->fadeOutRange.amplitude != 0.0)
            fadeOutFrac = 1.0 - FX_ClampRangeLerp(dist, &def->fadeOutRange);
        v2 = fadeOutFrac - fadeInFrac;
        if (v2 < 0.0)
            v1 = fadeOutFrac;
        else
            v1 = fadeInFrac;
        draw->preVisState.distanceFade = (__int64)(v1 * 255.0 + 0.5);
    }
}

double __cdecl FX_ClampRangeLerp(float dist, const FxFloatRange *range)
{
    float baseDist; // [esp+0h] [ebp-8h]
    float value; // [esp+4h] [ebp-4h]

    baseDist = dist - range->base;
    value = 0.0;
    if (baseDist >= 0.0)
    {
        if (range->amplitude > (double)baseDist)
            return (float)(1.0 - baseDist / range->amplitude);
    }
    else
    {
        return (float)1.0;
    }
    return value;
}

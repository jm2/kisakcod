#include "fx_system.h"
#include "fx_runtime_blob.h"
#include <gfx_d3d/r_material.h>
#include <universal/com_math.h>
#include <xanim/xmodel.h>

#include <limits>

static_assert(alignof(FxEffectDef) >= alignof(FxElemDef));
static_assert(alignof(FxEffectDef) >= alignof(FxElemVelStateSample));
static_assert(alignof(FxEffectDef) >= alignof(FxElemVisStateSample));
static_assert(alignof(FxEffectDef) >= alignof(FxElemMarkVisuals));
static_assert(alignof(FxEffectDef) >= alignof(FxElemVisuals));
static_assert(alignof(FxEffectDef) >= alignof(FxTrailDef));
RUNTIME_SIZE(FxElemDef, 0xFC, 0x120);
RUNTIME_SIZE(FxTrailDef, 0x1C, 0x28);
RUNTIME_SIZE(FxElemMarkVisuals, 0x8, 0x10);
RUNTIME_SIZE(FxElemVisuals, 0x4, 0x8);
RUNTIME_SIZE(FxElemVelStateSample, 0x60, 0x60);
RUNTIME_SIZE(FxElemVisStateSample, 0x30, 0x30);
RUNTIME_SIZE(FxTrailVertex, 0x14, 0x14);
static_assert(alignof(FxElemVelStateSample) == 4);
static_assert(alignof(FxElemVisStateSample) == 4);
static_assert(alignof(FxTrailVertex) == 4);

bool __cdecl FX_ElemUsesMaterial(const FxEditorElemDef *edElemDef)
{
    uint8_t elemType; // [esp+0h] [ebp-4h]

    elemType = edElemDef->elemType;
    return elemType < 5u || elemType > 8u && elemType != 10;
}

char __cdecl FX_ValidateFlags(const FxEditorEffectDef *editorEffect, const FxEditorElemDef *edElemDef)
{
    if ((edElemDef->flags & 0xF0) != 0xC0)
        return 1;
    Com_PrintError(
        21,
        "effect '%s' segment '%s'\nVelocity is 'relative to offset', but generation offset is 'none'\n",
        editorEffect->name,
        edElemDef->name);
    return 0;
}

char __cdecl FX_ValidateAtlasSettings(const FxEditorEffectDef *editorEffect, const FxEditorElemDef *edElemDef)
{
    MaterialInfo mtlInfoRef; // [esp+0h] [ebp-38h] BYREF
    MaterialInfo mtlInfo; // [esp+18h] [ebp-20h] BYREF
    int32_t visualIndex; // [esp+34h] [ebp-4h]

    if (!edElemDef)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 627, 0, "%s", "edElemDef");
    if (!edElemDef->visualCount)
        return 1;
    if (!FX_ElemUsesMaterial(edElemDef))
        return 1;
    Material_GetInfo(edElemDef->visuals[0].material, &mtlInfoRef);
    if (((mtlInfoRef.textureAtlasRowCount - 1) & mtlInfoRef.textureAtlasRowCount) != 0
        || ((mtlInfoRef.textureAtlasColumnCount - 1) & mtlInfoRef.textureAtlasColumnCount) != 0)
    {
        Com_PrintError(
            21,
            "effect '%s' segment '%s':\nmaterial %s is a %i x %i atlas, which is not a power of 2 on both axes\n",
            editorEffect->name,
            edElemDef->name,
            mtlInfoRef.name,
            mtlInfoRef.textureAtlasColumnCount,
            mtlInfoRef.textureAtlasRowCount);
        return 0;
    }
    else
    {
        for (visualIndex = 1; visualIndex < edElemDef->visualCount; ++visualIndex)
        {
            Material_GetInfo(edElemDef->visuals[visualIndex].material, &mtlInfo);
            if (mtlInfo.textureAtlasRowCount != mtlInfoRef.textureAtlasRowCount
                || mtlInfo.textureAtlasColumnCount != mtlInfoRef.textureAtlasColumnCount)
            {
                Com_PrintError(
                    21,
                    "effect '%s' segment '%s':\nmaterial %s is a %i x %i atlas, but material %s is a %i x %i atlas\n",
                    editorEffect->name,
                    edElemDef->name,
                    mtlInfoRef.name,
                    mtlInfoRef.textureAtlasColumnCount,
                    mtlInfoRef.textureAtlasRowCount,
                    mtlInfo.name,
                    mtlInfo.textureAtlasColumnCount,
                    mtlInfo.textureAtlasRowCount);
                return 0;
            }
        }
        return 1;
    }
}

char __cdecl FX_ValidateColor(const FxEditorEffectDef *editorEffect, const FxEditorElemDef *edElemDef)
{
    if (edElemDef->elemType == 9)
    {
        if (edElemDef->lightingFrac != 0.0)
        {
            Com_PrintError(
                21,
                "effect '%s' segment '%s'\nDecals cannot have a non-zero lighting fraction.\n",
                editorEffect->name,
                edElemDef->name);
            return 0;
        }
    }
    else
    {
        if (edElemDef->lightingFrac < 0.0)
        {
            Com_PrintError(21, "effect '%s' segment '%s'\nNegative lighting fraction.\n", editorEffect->name, edElemDef->name);
            return 0;
        }
        if (edElemDef->lightingFrac > 1.0)
        {
            Com_PrintError(
                21,
                "effect '%s' segment '%s'\nLighting fraction larger than 1.0.\n",
                editorEffect->name,
                edElemDef->name);
            return 0;
        }
    }
    return 1;
}

char __cdecl FX_ValidateVisuals(const FxEditorEffectDef *editorEffect, const FxEditorElemDef *edElemDef)
{
    int32_t indIter; // [esp+0h] [ebp-4h]

    if ((edElemDef->elemType == 9 || edElemDef->elemType == 10) && !edElemDef->visualCount)
    {
        Com_PrintError(
            21,
            "effect '%s' segment '%s'\nThis type of segment must have at least one visual specified.\n",
            editorEffect->name,
            edElemDef->name);
        return 0;
    }
    if (edElemDef->elemType != 3)
        return 1;
    if (edElemDef->trailDef.indCount <= 0 || edElemDef->trailDef.vertCount <= 0)
    {
        Com_PrintError(
            21,
            "effect '%s' segment '%s'\nTrail cross-section cannot be empty.\n",
            editorEffect->name,
            edElemDef->name);
        return 0;
    }
    if (edElemDef->trailDef.indCount
            > static_cast<int32_t>(ARRAY_COUNT(edElemDef->trailDef.inds))
        || edElemDef->trailDef.vertCount
            > static_cast<int32_t>(ARRAY_COUNT(edElemDef->trailDef.verts)))
    {
        Com_PrintError(
            21,
            "effect '%s' segment '%s'\nTrail cross-section exceeds editor storage capacity.\n",
            editorEffect->name,
            edElemDef->name);
        return 0;
    }
    if ((edElemDef->trailDef.indCount & 1) != 0)
    {
        Com_PrintError(
            21,
            "effect '%s' segment '%s'\nTrail cross-section must contain index pairs.\n",
            editorEffect->name,
            edElemDef->name);
        return 0;
    }
    for (indIter = 0; indIter != edElemDef->trailDef.indCount; ++indIter)
    {
        if (edElemDef->trailDef.inds[indIter] >= edElemDef->trailDef.vertCount)
        {
            Com_PrintError(
                21,
                "effect '%s' segment '%s'\nIndex references out of range vertex '%i'.\n",
                editorEffect->name,
                edElemDef->name,
                edElemDef->trailDef.inds[indIter]);
            return 0;
        }
    }
    if (edElemDef->trailRepeatDist <= 0)
    {
        Com_PrintError(21, "effect '%s' segment '%s'\nTrail repeat dist <= 0.\n", editorEffect->name, edElemDef->name);
        return 0;
    }
    if (edElemDef->trailSplitDist <= 0)
    {
        Com_PrintError(21, "effect '%s' segment '%s'\nTrail split dist <= 0.\n", editorEffect->name, edElemDef->name);
        return 0;
    }
    if ((int)((double)edElemDef->trailRepeatDist * 1000.0) > 0)
        return 1;
    Com_PrintError(
        21,
        "effect '%s' segment '%s'\nTrail texture repeat dist too close to, or below 0.\n",
        editorEffect->name,
        edElemDef->name);
    return 0;
}

char __cdecl FX_ValidatePhysics(const FxEditorEffectDef *editorEffect, const FxEditorElemDef *edElemDef)
{
    float v3; // [esp+10h] [ebp-20h]
    float v4; // [esp+14h] [ebp-1Ch]
    float v5; // [esp+18h] [ebp-18h]
    float v6; // [esp+1Ch] [ebp-14h]
    float v7; // [esp+20h] [ebp-10h]
    float amplitude; // [esp+24h] [ebp-Ch]
    float elasticityMin; // [esp+28h] [ebp-8h]
    float elasticityMax; // [esp+2Ch] [ebp-4h]

    if ((edElemDef->flags & 0x100) == 0)
        return 1;
    amplitude = edElemDef->elasticity.amplitude;
    v6 = amplitude - 0.0;
    if (v6 < 0.0)
        v5 = amplitude;
    else
        v5 = 0.0;
    elasticityMin = edElemDef->elasticity.base + v5;
    v7 = edElemDef->elasticity.amplitude;
    v4 = 0.0 - v7;
    if (v4 < 0.0)
        v3 = v7;
    else
        v3 = 0.0;
    elasticityMax = edElemDef->elasticity.base + v3;
    if (elasticityMin >= -EQUAL_EPSILON && elasticityMax <= (1.0f + EQUAL_EPSILON))
        return 1;
    Com_PrintError(
        21,
        "effect '%s' segment '%s'\nElasticity %g to %g can go outside the range 0 to 1.\n",
        editorEffect->name,
        edElemDef->name,
        elasticityMin,
        elasticityMax);
    return 0;
}

bool __cdecl FX_Validate(const FxEditorEffectDef *editorEffect, const FxEditorElemDef *edElemDef)
{
    if (!FX_ValidateAtlasSettings(editorEffect, edElemDef))
        return 0;
    if (!FX_ValidateFlags(editorEffect, edElemDef))
        return 0;
    if (!FX_ValidateColor(editorEffect, edElemDef))
        return 0;
    if (FX_ValidateVisuals(editorEffect, edElemDef))
        return FX_ValidatePhysics(editorEffect, edElemDef) != 0;
    return 0;
}

void __cdecl FX_GetVisualSampleRouting(const FxEditorElemDef *edElem, FxSampleChannel *routing)
{
    int32_t v2; // [esp+0h] [ebp-8h]

    switch (edElem->elemType)
    {
    case 0u:
    case 1u:
    case 2u:
    case 3u:
        *routing = FX_CHAN_RGBA;
        *((_DWORD *)routing + 1) = 1;
        if (edElem->elemType == 2 || (edElem->flags & 0x10000000) != 0)
            v2 = 2;
        else
            v2 = 1;
        *((_DWORD *)routing + 2) = v2;
        *((_DWORD *)routing + 4) = 4;
        *((_DWORD *)routing + 3) = 6;
        break;
    case 4u:
        *routing = FX_CHAN_RGBA;
        *((_DWORD *)routing + 1) = 1;
        *((_DWORD *)routing + 2) = 2;
        *((_DWORD *)routing + 4) = 6;
        *((_DWORD *)routing + 3) = 3;
        break;
    case 5u:
    case 8u:
        *routing = FX_CHAN_NONE;
        *((_DWORD *)routing + 1) = 6;
        *((_DWORD *)routing + 2) = 6;
        *((_DWORD *)routing + 4) = 6;
        *((_DWORD *)routing + 3) = 3;
        break;
    case 6u:
        *routing = FX_CHAN_RGBA;
        *((_DWORD *)routing + 1) = 1;
        *((_DWORD *)routing + 2) = 6;
        *((_DWORD *)routing + 4) = 6;
        *((_DWORD *)routing + 3) = 6;
        break;
    case 7u:
    case 9u:
        *routing = FX_CHAN_RGBA;
        *((_DWORD *)routing + 1) = 1;
        *((_DWORD *)routing + 2) = 6;
        *((_DWORD *)routing + 4) = 4;
        *((_DWORD *)routing + 3) = 6;
        break;
    case 0xAu:
        *routing = FX_CHAN_NONE;
        *((_DWORD *)routing + 1) = 6;
        *((_DWORD *)routing + 2) = 6;
        *((_DWORD *)routing + 4) = 6;
        *((_DWORD *)routing + 3) = 6;
        break;
    default:
        return;
    }
}

int32_t __cdecl FX_DecideIntervalLimit(const FxEditorElemDef *edElemDef)
{
    int32_t intervalLimit; // [esp+0h] [ebp-8h]

    intervalLimit = (edElemDef->lifeSpanMsec.base + edElemDef->lifeSpanMsec.amplitude / 2) / 100;
    if (intervalLimit > 80)
        return 80;
    return intervalLimit;
}

void __cdecl FX_InterpolateSamples(
    int32_t dimensions,
    float time0,
    const float *samples0,
    float time1,
    const float *samples1,
    float timeEval,
    float *result)
{
    float lerp; // [esp+18h] [ebp-8h]
    int32_t dimIndex; // [esp+1Ch] [ebp-4h]

    if (dimensions <= 0)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 44, 0, "%s\n\t(dimensions) = %i", "(dimensions > 0)", dimensions);
    if (time1 <= (double)time0)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 45, 0, "time0 < time1\n\t%g, %g", time0, time1);
    if (timeEval < (double)time0 || time1 < (double)timeEval)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            46,
            0,
            "timeEval not in [time0, time1]\n\t%g not in [%g, %g]",
            timeEval,
            time0,
            time1);
    if (!samples0)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 47, 0, "%s", "samples0");
    if (!samples1)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 48, 0, "%s", "samples1");
    if (!result)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 49, 0, "%s", "result");
    for (dimIndex = 0; dimIndex < dimensions; ++dimIndex)
    {
        lerp = (timeEval - time0) / (time1 - time0);
        result[dimIndex] = (samples1[dimIndex] - samples0[dimIndex]) * lerp + samples0[dimIndex];
    }
}

double __cdecl FX_MaxErrorForIntervalCount(
    int32_t dimensions,
    int32_t sampleCount,
    const float *samples,
    int32_t intervalCount,
    float errorCutoff)
{
    float v6; // [esp+18h] [ebp-5Ch]
    float v7; // [esp+1Ch] [ebp-58h]
    float timeNext; // [esp+20h] [ebp-54h]
    float lerpedValueIter[3]; // [esp+24h] [ebp-50h] BYREF
    float timePrev; // [esp+30h] [ebp-44h]
    const float *samplesTo; // [esp+34h] [ebp-40h]
    int32_t componentIndex; // [esp+38h] [ebp-3Ch]
    int32_t intervalIndex; // [esp+3Ch] [ebp-38h]
    int32_t sampleIndexPrev; // [esp+40h] [ebp-34h]
    int32_t sampleIndexIter; // [esp+44h] [ebp-30h]
    float error; // [esp+48h] [ebp-2Ch]
    float errorMax; // [esp+4Ch] [ebp-28h]
    int32_t sampleIndexNext; // [esp+50h] [ebp-24h]
    float lerpedValueNext[3]; // [esp+54h] [ebp-20h] BYREF
    int32_t componentCount; // [esp+60h] [ebp-14h]
    float lerpedValuePrev[3]; // [esp+64h] [ebp-10h] BYREF
    const float *samplesFrom; // [esp+70h] [ebp-4h]

    if (dimensions <= 0)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 98, 0, "%s\n\t(dimensions) = %i", "(dimensions > 0)", dimensions);
    if (sampleCount <= 1)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            99,
            0,
            "%s\n\t(sampleCount) = %i",
            "(sampleCount > 1)",
            sampleCount);
    if (!samples)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 100, 0, "%s", "samples");
    componentCount = dimensions + 1;
    errorMax = 0.0;
    timePrev = 0.0;
    sampleIndexPrev = 1;
    for (componentIndex = 1; componentIndex != componentCount; ++componentIndex)
        lerpedValuePrev[componentIndex - 1] = samples[componentIndex];
    for (intervalIndex = 1; intervalIndex <= intervalCount; ++intervalIndex)
    {
        for (sampleIndexNext = sampleIndexPrev;
            (double)intervalIndex > (double)intervalCount * samples[componentCount * sampleIndexNext];
            ++sampleIndexNext)
        {
            ;
        }
        timeNext = (double)intervalIndex / (double)intervalCount;
        samplesTo = &samples[componentCount * sampleIndexNext];
        samplesFrom = &samplesTo[-componentCount];
        FX_InterpolateSamples(
            dimensions,
            *samplesFrom,
            samplesFrom + 1,
            *samplesTo,
            samplesTo + 1,
            timeNext,
            lerpedValueNext);
        for (sampleIndexIter = sampleIndexPrev; sampleIndexIter < sampleIndexNext; ++sampleIndexIter)
        {
            FX_InterpolateSamples(
                dimensions,
                timePrev,
                lerpedValuePrev,
                timeNext,
                lerpedValueNext,
                samples[componentCount * sampleIndexIter],
                lerpedValueIter);
            for (componentIndex = 1; componentIndex < componentCount; ++componentIndex)
            {
                v7 = samples[componentIndex + componentCount * sampleIndexIter] - lerpedValueIter[componentIndex - 1];
                v6 = I_fabs(v7);
                error = v6;
                if (v6 > (double)errorMax)
                {
                    errorMax = error;
                    if (errorCutoff < (double)error)
                        return errorMax;
                }
            }
        }
        for (componentIndex = 1; componentIndex != componentCount; ++componentIndex)
            lerpedValuePrev[componentIndex - 1] = lerpedValueNext[componentIndex - 1];
        sampleIndexPrev = sampleIndexNext;
        timePrev = timeNext;
    }
    return errorMax;
}

int32_t __cdecl FX_DecideSampleCount(int32_t curveCount, const FxCurve **curves, int32_t intervalLimit)
{
    int32_t intervalCountBest; // [esp+4h] [ebp-18h]
    float errorBest; // [esp+8h] [ebp-14h]
    float error; // [esp+Ch] [ebp-10h]
    int32_t intervalCount; // [esp+10h] [ebp-Ch]
    int32_t curveIndex; // [esp+14h] [ebp-8h]
    float errorCumulative; // [esp+18h] [ebp-4h]

    errorBest = FLT_MAX;
    intervalCountBest = 1;
    for (intervalCount = 1; intervalCount <= intervalLimit; ++intervalCount)
    {
        errorCumulative = 0.0;
        for (curveIndex = 0; curveIndex < curveCount; ++curveIndex)
        {
            error = FX_MaxErrorForIntervalCount(
                curves[curveIndex]->dimensionCount,
                curves[curveIndex]->keyCount,
                curves[curveIndex]->keys,
                intervalCount,
                errorBest);
            if (errorCumulative < (double)error)
            {
                errorCumulative = error;
                if (errorBest < (double)error)
                    break;
            }
        }
        if (errorCumulative < (double)errorBest)
        {
            intervalCountBest = intervalCount;
            errorBest = errorCumulative - 0.01999999955296516;
            if (errorBest <= 0.0)
                break;
        }
    }
    return intervalCountBest + 1;
}

int32_t __cdecl FX_DecideVelocitySampleCount(const FxEditorElemDef *edElem, int32_t intervalLimit)
{
    const FxCurve *curves[12]; // [esp+0h] [ebp-30h] BYREF

    int32_t curveIndex = 0;
    for (int32_t frameIndex = 0; frameIndex < 2; ++frameIndex)
    {
        for (int32_t dimensionIndex = 0; dimensionIndex < 3; ++dimensionIndex)
        {
            for (int32_t graphIndex = 0; graphIndex < 2; ++graphIndex)
                curves[curveIndex++] = edElem->velShape[frameIndex][dimensionIndex][graphIndex];
        }
    }
    return FX_DecideSampleCount(12, curves, intervalLimit);
}

int32_t __cdecl FX_DecideVisualSampleCount(
    const FxEditorElemDef *edElem,
    const FxSampleChannel *routing,
    int32_t intervalLimit)
{
    uint32_t curveCount; // [esp+4h] [ebp-38h]
    uint32_t curveCounta; // [esp+4h] [ebp-38h]
    const FxCurve *curves[12]; // [esp+8h] [ebp-34h] BYREF
    uint32_t chanIndex; // [esp+38h] [ebp-4h]

    curveCount = 0;
    for (chanIndex = 0; chanIndex < 5; ++chanIndex)
    {
        switch (routing[chanIndex])
        {
        case FX_CHAN_RGBA:
            curves[curveCount] = edElem->color[0];
            curveCounta = curveCount + 1;
            if ((edElem->editorFlags & 2) != 0)
                curves[curveCounta++] = edElem->color[1];
            curves[curveCounta] = edElem->alpha[0];
            curveCount = curveCounta + 1;
            if ((edElem->editorFlags & 4) != 0)
                curves[curveCount++] = edElem->alpha[1];
            break;
        case FX_CHAN_SIZE_0:
            curves[curveCount++] = edElem->sizeShape[0][0];
            if ((edElem->editorFlags & 8) != 0)
                curves[curveCount++] = edElem->sizeShape[0][1];
            break;
        case FX_CHAN_SIZE_1:
            curves[curveCount++] = edElem->sizeShape[1][0];
            if ((edElem->editorFlags & 0x10) != 0)
                curves[curveCount++] = edElem->sizeShape[1][1];
            break;
        case FX_CHAN_SCALE:
            curves[curveCount++] = edElem->scaleShape[0];
            if ((edElem->editorFlags & 0x20) != 0)
                curves[curveCount++] = edElem->scaleShape[1];
            break;
        case FX_CHAN_ROTATION:
            curves[curveCount++] = edElem->rotationShape[0];
            if ((edElem->editorFlags & 0x40) != 0)
                curves[curveCount++] = edElem->rotationShape[1];
            break;
        default:
            if (routing[chanIndex] != FX_CHAN_NONE)
                MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 331, 0, "%s", "routing[chanIndex] == FX_CHAN_NONE");
            break;
        }
    }
    if (curveCount > 0xC)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            336,
            0,
            "curveCount not in [0, ARRAY_COUNT( curves )]\n\t%i not in [%i, %i]",
            curveCount,
            0,
            12);
    if (curveCount)
        return FX_DecideSampleCount(curveCount, curves, intervalLimit);
    else
        return 0;
}

bool FX_ReserveElemDefPayload(
    FxRuntimeBlobCursor *cursor,
    uint8_t elemType,
    int32_t velStateSampleCount,
    int32_t visStateSampleCount,
    int32_t visualCount,
    FxElemVelStateSample **velSamples = nullptr,
    FxElemVisStateSample **visSamples = nullptr,
    void **visuals = nullptr)
{
    if (!cursor || velStateSampleCount < 0 || visStateSampleCount < 0 || visualCount < 0
        || !cursor->ReserveArray(
            static_cast<uint32_t>(velStateSampleCount), velSamples))
    {
        return false;
    }
    if (visStateSampleCount != 0
        && !cursor->ReserveArray(static_cast<uint32_t>(visStateSampleCount), visSamples))
    {
        return false;
    }
    if (visStateSampleCount == 0 && visSamples)
        *visSamples = nullptr;

    if (elemType == 9)
    {
        FxElemMarkVisuals *markVisuals = nullptr;
        if (!cursor->ReserveArray(static_cast<uint32_t>(visualCount), &markVisuals))
            return false;
        if (visuals)
            *visuals = markVisuals;
    }
    else if (visualCount > 1)
    {
        FxElemVisuals *visualArray = nullptr;
        if (!cursor->ReserveArray(static_cast<uint32_t>(visualCount), &visualArray))
            return false;
        if (visuals)
            *visuals = visualArray;
    }
    else if (visuals)
    {
        *visuals = nullptr;
    }
    return true;
}

bool FX_ReserveTrailPayload(
    FxRuntimeBlobCursor *cursor,
    int32_t indCount,
    FxTrailDef **trailDef = nullptr,
    FxTrailVertex **vertices = nullptr,
    uint16_t **indices = nullptr)
{
    return cursor && indCount >= 0
        && cursor->ReserveArray(1, trailDef)
        && cursor->ReserveArray(static_cast<uint32_t>(indCount), vertices)
        && cursor->ReserveArray(static_cast<uint32_t>(indCount), indices);
}

int32_t __cdecl FX_FindEmission(const FxEffectDef *emission, const FxEditorEffectDef *editorEffect)
{
    int32_t elemIndex; // [esp+0h] [ebp-4h]

    if (!editorEffect)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1109, 0, "%s", "editorEffect");
    if (editorEffect == (const FxEditorEffectDef *)-68)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1110, 0, "%s", "editorEffect->elems");
    if (!editorEffect->elemCount)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1111, 0, "%s", "editorEffect->elemCount");
    for (elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        if (emission == editorEffect->elems[elemIndex].emission)
            return elemIndex;
    }
    if (!alwaysfails)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1124, 1, "emission not in editor effect");
    return -1;
}

void __cdecl FX_ConvertEffectDefRef(FxEffectDefRef *ref, const FxEffectDef *effectDef)
{
    ref->handle = effectDef;
}

void __cdecl FX_CopyCanonicalFloatRange(FxFloatRange *to, const FxFloatRange *from)
{
    if (from->amplitude >= 0.0)
    {
        *to = *from;
    }
    else
    {
        to->base = from->base + from->amplitude;
        to->amplitude = -from->amplitude;
    }
}

void __cdecl FX_CopyCanonicalIntRange(FxIntRange *to, const FxIntRange *from)
{
    if (from->amplitude >= 0)
    {
        *to = *from;
    }
    else
    {
        to->base = from->amplitude + from->base;
        to->amplitude = -from->amplitude;
    }
}

void __cdecl FX_ScaleFloatRange(FxFloatRange *to, const FxFloatRange *from, float scale)
{
    to->base = from->base * scale;
    to->amplitude = from->amplitude * scale;
}

void __cdecl FX_BoundFloatRange(FxFloatRange *range, float lower, float upper)
{
    float v3; // [esp+10h] [ebp-8h]

    if (range->amplitude < 0.0)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 803, 0, "%s", "range->amplitude >= 0.0f");
    if (lower > (double)range->base || upper < range->base + range->amplitude)
    {
        range->base = range->base + (float)0.000099999997;
        range->amplitude = range->amplitude - ((float)0.000099999997 + (float)0.000099999997);
        if (lower > (double)range->base)
            MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 810, 0, "range->base >= lower\n\t%g, %g", range->base, lower);
        if (upper < range->base + range->amplitude)
        {
            v3 = range->base + range->amplitude;
            MyAssertHandler(
                ".\\EffectsCore\\fx_convert.cpp",
                811,
                0,
                "range->base + range->amplitude <= upper\n\t%g, %g",
                v3,
                upper);
        }
    }
}

void __cdecl FX_ConvertAtlas(FxElemDef *elemDef, const FxEditorElemDef *edElemDef)
{
    MaterialInfo mtlInfo; // [esp+0h] [ebp-18h] BYREF

    if (!elemDef)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 660, 0, "%s", "elemDef");
    if (!edElemDef)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 661, 0, "%s", "edElemDef");
    if (edElemDef->visualCount && FX_ElemUsesMaterial(edElemDef))
    {
        elemDef->atlas.behavior = edElemDef->atlas.behavior;
        elemDef->atlas.index = edElemDef->atlas.index;
        elemDef->atlas.fps = edElemDef->atlas.fps;
        elemDef->atlas.loopCount = edElemDef->atlas.loopCount + 1;
        elemDef->atlas.colIndexBits = edElemDef->atlas.colIndexBits;
        elemDef->atlas.rowIndexBits = edElemDef->atlas.rowIndexBits;
        elemDef->atlas.entryCount = edElemDef->atlas.entryCount;
        Material_GetInfo(edElemDef->visuals[0].material, &mtlInfo);
        for (elemDef->atlas.rowIndexBits = 0;
            1 << elemDef->atlas.rowIndexBits < mtlInfo.textureAtlasRowCount;
            ++elemDef->atlas.rowIndexBits)
        {
            ;
        }
        for (elemDef->atlas.colIndexBits = 0;
            1 << elemDef->atlas.colIndexBits < mtlInfo.textureAtlasColumnCount;
            ++elemDef->atlas.colIndexBits)
        {
            ;
        }
        elemDef->atlas.entryCount = 1 << (elemDef->atlas.colIndexBits + elemDef->atlas.rowIndexBits);
    }
    else
    {
        *(_DWORD *)&elemDef->atlas.behavior = 0;
        *(_DWORD *)&elemDef->atlas.colIndexBits = 0;
    }
}

bool __cdecl FX_ReserveElemDefMemory(FxElemDef *elemDef, FxRuntimeBlobCursor *cursor)
{
    if (!elemDef)
    {
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 703, 0, "%s", "elemDef");
        return false;
    }
    if (!cursor)
    {
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 704, 0, "%s", "cursor");
        return false;
    }
    if (!elemDef->velIntervalCount)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 707, 0, "%s", "elemDef->velIntervalCount");

    void *visuals = nullptr;
    if (!FX_ReserveElemDefPayload(
            cursor,
            elemDef->elemType,
            elemDef->velIntervalCount + 1,
            elemDef->visStateIntervalCount ? elemDef->visStateIntervalCount + 1 : 0,
            elemDef->visualCount,
            &elemDef->velSamples,
            &elemDef->visSamples,
            &visuals))
    {
        return false;
    }
    if (elemDef->elemType == 9)
        elemDef->visuals.markArray = static_cast<FxElemMarkVisuals *>(visuals);
    else if (elemDef->visualCount > 1u)
        elemDef->visuals.array = static_cast<FxElemVisuals *>(visuals);
    return true;
}

double __cdecl FX_SampleCurve1D(const FxCurve *curve, float scale, float time)
{
    FxCurveIterator iter; // [esp+24h] [ebp-Ch] BYREF
    float value; // [esp+2Ch] [ebp-4h]

    FxCurveIterator_Create(&iter, curve);
    value = FxCurveIterator_SampleTime(&iter, time) * scale;
    FxCurveIterator_Release(&iter);
    return value;
}

void __cdecl FX_SampleVelocityInFrame(
    FxElemDef *elemDef,
    const float (*velScale)[3],
    FxElemVelStateInFrame *velState,
    int32_t velStateStride,
    int32_t useGraphBit,
    const FxEditorElemDef *edElemDef)
{
    bool v6; // [esp+Ch] [ebp-4Ch]
    bool v7; // [esp+10h] [ebp-48h]
    bool anyNonZero; // [esp+23h] [ebp-35h]
    float velEpsilonSq; // [esp+24h] [ebp-34h]
    float velEpsilonSqa; // [esp+24h] [ebp-34h]
    float sampleTime; // [esp+28h] [ebp-30h]
    bool useVelocityRand[4]; // [esp+2Ch] [ebp-2Ch]
    float deltaInSegment[3]; // [esp+30h] [ebp-28h] BYREF
    float velocitySample[3]; // [esp+3Ch] [ebp-1Ch] BYREF
    int32_t sampleIndex; // [esp+48h] [ebp-10h]
    bool brokenCompatibilityMode; // [esp+4Fh] [ebp-9h]
    FxElemVelStateInFrame *velStatePrev; // [esp+50h] [ebp-8h]
    bool useVelocity[4]; // [esp+54h] [ebp-4h]

    useVelocity[0] = (edElemDef->editorFlags & 0x800) == (useGraphBit != 0x2000000 ? 0 : 0x800);
    useVelocity[1] = (edElemDef->editorFlags & 0x1000) == (useGraphBit != 0x2000000 ? 0 : 0x1000);
    v7 = useVelocity[0] && (edElemDef->editorFlags & 0x100) != 0;
    useVelocityRand[0] = v7;
    v6 = useVelocity[1] && (edElemDef->editorFlags & 0x200) != 0;
    useVelocityRand[1] = v6;
    brokenCompatibilityMode = (edElemDef->editorFlags & 0x400) != 0;
    velEpsilonSq = 0.0;
    if (useVelocity[0])
        velEpsilonSq = Vec3LengthSq((const float *)velScale) + velEpsilonSq;
    if (useVelocity[1])
        velEpsilonSq = Vec3LengthSq(&(*velScale)[3]) + velEpsilonSq;
    velEpsilonSqa = velEpsilonSq * 0.00000100000011116208;
    anyNonZero = 0;
    velStatePrev = 0;
    for (sampleIndex = 0; sampleIndex <= elemDef->velIntervalCount; ++sampleIndex)
    {
        sampleTime = (double)sampleIndex / (double)elemDef->velIntervalCount;
        velState->velocity.base[0] = 0.0;
        velState->velocity.base[1] = 0.0;
        velState->velocity.base[2] = 0.0;
        velState->velocity.amplitude[0] = 0.0;
        velState->velocity.amplitude[1] = 0.0;
        velState->velocity.amplitude[2] = 0.0;
        if (useVelocity[0])
        {
            velocitySample[0] = FX_SampleCurve1D(edElemDef->velShape[0][0][0], (*velScale)[0], sampleTime);
            velocitySample[1] = FX_SampleCurve1D(edElemDef->velShape[0][1][0], (*velScale)[1], sampleTime);
            velocitySample[2] = FX_SampleCurve1D(edElemDef->velShape[0][2][0], (*velScale)[2], sampleTime);
            Vec3Add(velState->velocity.base, velocitySample, velState->velocity.base);
            if (useVelocityRand[0])
                Vec3Sub(velState->velocity.amplitude, velocitySample, velState->velocity.amplitude);
        }
        if (useVelocity[1])
        {
            velocitySample[0] = FX_SampleCurve1D(edElemDef->velShape[1][0][0], (*velScale)[3], sampleTime);
            velocitySample[1] = FX_SampleCurve1D(edElemDef->velShape[1][1][0], (*velScale)[4], sampleTime);
            velocitySample[2] = FX_SampleCurve1D(edElemDef->velShape[1][2][0], (*velScale)[5], sampleTime);
            Vec3Add(velState->velocity.base, velocitySample, velState->velocity.base);
            if (useVelocityRand[!brokenCompatibilityMode])
                Vec3Sub(velState->velocity.amplitude, velocitySample, velState->velocity.amplitude);
        }
        if (velStatePrev)
        {
            Vec3Avg(velStatePrev->velocity.base, velState->velocity.base, deltaInSegment);
            Vec3Add(deltaInSegment, velStatePrev->totalDelta.base, velState->totalDelta.base);
            if (!anyNonZero)
                anyNonZero = velEpsilonSqa < Vec3LengthSq(velState->totalDelta.base);
        }
        else
        {
            velState->totalDelta.base[0] = 0.0;
            velState->totalDelta.base[1] = 0.0;
            velState->totalDelta.base[2] = 0.0;
        }
        if (useVelocityRand[0])
        {
            velState->velocity.amplitude[0] = FX_SampleCurve1D(edElemDef->velShape[0][0][1], (*velScale)[0], sampleTime)
                + velState->velocity.amplitude[0];
            velState->velocity.amplitude[1] = FX_SampleCurve1D(edElemDef->velShape[0][1][1], (*velScale)[1], sampleTime)
                + velState->velocity.amplitude[1];
            velState->velocity.amplitude[2] = FX_SampleCurve1D(edElemDef->velShape[0][2][1], (*velScale)[2], sampleTime)
                + velState->velocity.amplitude[2];
        }
        if (useVelocityRand[1])
        {
            velState->velocity.amplitude[0] = FX_SampleCurve1D(edElemDef->velShape[1][0][1], (*velScale)[3], sampleTime)
                + velState->velocity.amplitude[0];
            velState->velocity.amplitude[1] = FX_SampleCurve1D(edElemDef->velShape[1][1][1], (*velScale)[4], sampleTime)
                + velState->velocity.amplitude[1];
            velState->velocity.amplitude[2] = FX_SampleCurve1D(edElemDef->velShape[1][2][1], (*velScale)[5], sampleTime)
                + velState->velocity.amplitude[2];
        }
        if (velStatePrev)
        {
            Vec3Avg(velStatePrev->velocity.amplitude, velState->velocity.amplitude, deltaInSegment);
            Vec3Add(deltaInSegment, velStatePrev->totalDelta.amplitude, velState->totalDelta.amplitude);
            if (!anyNonZero)
                anyNonZero = velEpsilonSqa < Vec3LengthSq(velState->totalDelta.amplitude);
        }
        else
        {
            velState->totalDelta.amplitude[0] = 0.0;
            velState->totalDelta.amplitude[1] = 0.0;
            velState->totalDelta.amplitude[2] = 0.0;
        }
        velStatePrev = velState;
        velState += velStateStride;
    }
    if (anyNonZero)
        elemDef->flags |= useGraphBit;
}

void __cdecl FX_SampleVelocity(FxElemDef *elemDef, const FxEditorElemDef *edElemDef)
{
    float v2; // [esp+8h] [ebp-30h]
    float scale; // [esp+10h] [ebp-28h]
    float velScale[2][3]; // [esp+18h] [ebp-20h] BYREF
    FxElemVelStateSample *velStateRange; // [esp+30h] [ebp-8h]
    int32_t velStateStride; // [esp+34h] [ebp-4h]

    scale = 1.0 / ((double)elemDef->velIntervalCount * 1000.0);
    Vec3Scale(edElemDef->velScale[0], scale, velScale[0]);
    v2 = 1.0 / ((double)elemDef->velIntervalCount * 1000.0);
    Vec3Scale(edElemDef->velScale[1], v2, velScale[1]);
    velStateStride = 2;
    velStateRange = elemDef->velSamples;
    FX_SampleVelocityInFrame(elemDef, velScale, &velStateRange->local, 2, 0x1000000, edElemDef);
    FX_SampleVelocityInFrame(elemDef, velScale, &velStateRange->world, velStateStride, 0x2000000, edElemDef);
}

void __cdecl FX_SampleVisualStateScalar(
    const FxEditorElemDef *edElemDef,
    float sampleTime,
    FxSampleChannel routing,
    float scaleFactor,
    float *base,
    float *amplitude)
{
    float v6; // [esp+8h] [ebp-1Ch]
    float v7; // [esp+Ch] [ebp-18h]
    float v8; // [esp+10h] [ebp-14h]
    float v9; // [esp+14h] [ebp-10h]
    float v10; // [esp+18h] [ebp-Ch]
    float scale; // [esp+1Ch] [ebp-8h]

    switch (routing)
    {
    case FX_CHAN_SIZE_0:
        scale = edElemDef->sizeScale[0] * scaleFactor;
        *base = FX_SampleCurve1D(edElemDef->sizeShape[0][0], scale, sampleTime);
        if ((edElemDef->editorFlags & 8) != 0)
        {
            v10 = edElemDef->sizeScale[0] * scaleFactor;
            *amplitude = FX_SampleCurve1D(edElemDef->sizeShape[0][1], v10, sampleTime) - *base;
        }
        else
        {
            *amplitude = 0.0;
        }
        break;
    case FX_CHAN_SIZE_1:
        v9 = edElemDef->sizeScale[1] * scaleFactor;
        *base = FX_SampleCurve1D(edElemDef->sizeShape[1][0], v9, sampleTime);
        if ((edElemDef->editorFlags & 0x10) != 0)
        {
            v8 = edElemDef->sizeScale[1] * scaleFactor;
            *amplitude = FX_SampleCurve1D(edElemDef->sizeShape[1][1], v8, sampleTime) - *base;
        }
        else
        {
            *amplitude = 0.0;
        }
        break;
    case FX_CHAN_SCALE:
        v7 = edElemDef->scaleScale * scaleFactor;
        *base = FX_SampleCurve1D(edElemDef->scaleShape[0], v7, sampleTime);
        if ((edElemDef->editorFlags & 0x20) != 0)
        {
            v6 = edElemDef->scaleScale * scaleFactor;
            *amplitude = FX_SampleCurve1D(edElemDef->scaleShape[1], v6, sampleTime) - *base;
        }
        else
        {
            *amplitude = 0.0;
        }
        break;
    default:
        if (routing != FX_CHAN_NONE)
            MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 507, 0, "%s", "routing == FX_CHAN_NONE");
        *base = 0.0;
        *amplitude = 0.0;
        break;
    }
}

void __cdecl FX_SampleCurve3D(const FxCurve *curve, float scale, float time, float *value)
{
    FxCurveIterator iter; // [esp+1Ch] [ebp-8h] BYREF

    FxCurveIterator_Create(&iter, curve);
    FxCurveIterator_SampleTimeVec3(&iter, value, time);
    Vec3Scale(value, scale, value);
    FxCurveIterator_Release(&iter);
}

void __cdecl FX_SampleVisualState(FxElemDef *elemDef, const FxEditorElemDef *edElemDef)
{
    double v2; // st7
    double v3; // st7
    float sampleTime; // [esp+11Ch] [ebp-3Ch]
    BOOL secondAlphaSrc; // [esp+120h] [ebp-38h]
    float rotationScale; // [esp+124h] [ebp-34h]
    int32_t sampleIndex; // [esp+128h] [ebp-30h]
    float rgba[4]; // [esp+12Ch] [ebp-2Ch] BYREF
    int32_t secondColorSrc; // [esp+13Ch] [ebp-1Ch]
    FxElemVisStateSample *visStateRange; // [esp+140h] [ebp-18h]
    FxSampleChannel routing[5]; // [esp+144h] [ebp-14h] BYREF

    FX_GetVisualSampleRouting(edElemDef, routing);
    rotationScale = edElemDef->rotationScale * 0.01745329238474369 / ((double)elemDef->visStateIntervalCount * 1000.0);
    secondColorSrc = (edElemDef->editorFlags & 2) != 0;
    secondAlphaSrc = (edElemDef->editorFlags & 4) != 0;
    for (sampleIndex = 0; sampleIndex <= elemDef->visStateIntervalCount; ++sampleIndex)
    {
        sampleTime = (double)sampleIndex / (double)elemDef->visStateIntervalCount;
        visStateRange = &elemDef->visSamples[sampleIndex];
        if (routing[0])
        {
            if (routing[0] != FX_CHAN_NONE)
                MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 561, 0, "%s", "routing[FX_CHAN_RGBA] == FX_CHAN_NONE");
            *(_DWORD *)visStateRange->base.color = -1;
            *(_DWORD *)visStateRange->amplitude.color = -1;
        }
        else
        {
            FX_SampleCurve3D(edElemDef->color[0], 1.0, sampleTime, rgba);
            rgba[3] = FX_SampleCurve1D(edElemDef->alpha[0], 1.0, sampleTime);
            if ((edElemDef->editorFlags & 0x80) != 0)
            {
                Vec3Scale(rgba, rgba[3], rgba);
                rgba[3] = 1.0;
            }
            Byte4PackVertexColor(rgba, visStateRange->base.color);
            FX_SampleCurve3D(edElemDef->color[secondColorSrc], 1.0, sampleTime, rgba);
            rgba[3] = FX_SampleCurve1D(edElemDef->alpha[secondAlphaSrc], 1.0, sampleTime);
            if ((edElemDef->editorFlags & 0x80) != 0)
            {
                Vec3Scale(rgba, rgba[3], rgba);
                rgba[3] = 1.0;
            }
            Byte4PackVertexColor(rgba, visStateRange->amplitude.color);
        }
        if (routing[4] == FX_CHAN_ROTATION)
        {
            v2 = FX_SampleCurve1D(edElemDef->rotationShape[0], rotationScale, sampleTime);
            visStateRange->base.rotationDelta = v2;
            if (sampleIndex)
                visStateRange->base.rotationTotal = (visStateRange[-1].base.rotationDelta + visStateRange->base.rotationDelta)
                * 0.5
                + visStateRange[-1].base.rotationTotal;
            else
                visStateRange->base.rotationTotal = 0.0;
            if ((edElemDef->editorFlags & 0x40) != 0)
            {
                v3 = FX_SampleCurve1D(edElemDef->rotationShape[1], rotationScale, sampleTime);
                visStateRange->amplitude.rotationDelta = v3 - visStateRange->base.rotationDelta;
                if (sampleIndex)
                    visStateRange->amplitude.rotationTotal = (visStateRange[-1].amplitude.rotationDelta
                        + visStateRange->amplitude.rotationDelta)
                    * 0.5
                    + visStateRange[-1].amplitude.rotationTotal;
                else
                    visStateRange->amplitude.rotationTotal = 0.0;
            }
            else
            {
                visStateRange->amplitude.rotationDelta = 0.0;
                visStateRange->amplitude.rotationTotal = 0.0;
            }
        }
        else
        {
            if (routing[4] != FX_CHAN_NONE)
                MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 589, 0, "%s", "routing[FX_CHAN_ROTATION] == FX_CHAN_NONE");
            visStateRange->base.rotationDelta = 0.0;
            visStateRange->base.rotationTotal = 0.0;
            visStateRange->amplitude.rotationDelta = 0.0;
            visStateRange->amplitude.rotationTotal = 0.0;
        }
        FX_SampleVisualStateScalar(
            edElemDef,
            sampleTime,
            routing[1],
            0.5,
            visStateRange->base.size,
            visStateRange->amplitude.size);
        FX_SampleVisualStateScalar(
            edElemDef,
            sampleTime,
            routing[2],
            0.5,
            &visStateRange->base.size[1],
            &visStateRange->amplitude.size[1]);
        FX_SampleVisualStateScalar(
            edElemDef,
            sampleTime,
            routing[3],
            1.0,
            &visStateRange->base.scale,
            &visStateRange->amplitude.scale);
    }
}

void __cdecl FX_CopyMarkVisuals(const FxEditorElemDef *edElemDef, FxElemMarkVisuals *markVisualsArray)
{
    memcpy(
        markVisualsArray,
        &edElemDef->markVisuals,
        sizeof(FxElemMarkVisuals) * edElemDef->visualCount);
}

void __cdecl FX_CopyVisuals(const FxEditorElemDef *edElemDef, FxElemVisuals *visualsArray)
{
    memcpy(
        visualsArray,
        &edElemDef->visuals,
        sizeof(FxElemVisuals) * edElemDef->visualCount);
}

void __cdecl FX_ConvertTrail_CalcNormForSegment(const float *vert0, const float *vert1, float *outNormal)
{
    *outNormal = vert0[1] - vert1[1];
    outNormal[1] = *vert1 - *vert0;
    Vec2Normalize(outNormal);
}

void __cdecl FX_ConvertTrail_CompileVertices(
    const FxEditorElemDef *edElemDef,
    FxTrailDef *outTrailDef,
    FxTrailVertex *outVertStorage,
    uint16_t *outIndexStorage)
{
    double v3; // st7
    double v4; // st7
    double v5; // st7
    float v6; // [esp+4h] [ebp-8Ch]
    float v7; // [esp+8h] [ebp-88h]
    float v8; // [esp+Ch] [ebp-84h]
    float v9; // [esp+18h] [ebp-78h]
    float *normal; // [esp+1Ch] [ebp-74h]
    FxTrailVertex *v11; // [esp+20h] [ebp-70h]
    float *pos; // [esp+24h] [ebp-6Ch]
    FxTrailVertex *emittedVertPtrIter; // [esp+28h] [ebp-68h]
    float secondaryEdgeNorm[2]; // [esp+2Ch] [ebp-64h] BYREF
    float primaryEdgeNorm[2]; // [esp+34h] [ebp-5Ch] BYREF
    __int64 accumNorm; // [esp+3Ch] [ebp-54h] BYREF
    int32_t edgeIter; // [esp+44h] [ebp-4Ch]
    uint16_t *emittedIndPtrBegin; // [esp+48h] [ebp-48h]
    int32_t indCount; // [esp+4Ch] [ebp-44h]
    FxTrailVertex *outVertPtrBegin; // [esp+50h] [ebp-40h]
    const uint16_t *primaryEdgeIndPtr; // [esp+54h] [ebp-3Ch]
    const uint16_t *indPtrEnd; // [esp+58h] [ebp-38h]
    const FxEditorTrailDef *trailDef; // [esp+5Ch] [ebp-34h]
    int32_t vertBytes; // [esp+60h] [ebp-30h]
    float SNAP_TOLERANCE_POS; // [esp+64h] [ebp-2Ch]
    FxTrailVertex *emittedVertPtrBegin; // [esp+68h] [ebp-28h]
    FxTrailVertex *outVertPtrIter; // [esp+6Ch] [ebp-24h]
    float SMOOTH_THRESHOLD; // [esp+70h] [ebp-20h]
    uint16_t *emittedIndPtrEnd; // [esp+74h] [ebp-1Ch]
    const uint16_t *secondaryEdgeIndPtr; // [esp+78h] [ebp-18h]
    float SNAP_TOLERANCE_NORM; // [esp+7Ch] [ebp-14h]
    FxTrailVertex *emittedVertPtrEnd; // [esp+80h] [ebp-10h]
    float SNAP_TOLERANCE_TEXCOORD; // [esp+84h] [ebp-Ch]
    int32_t indBytes; // [esp+88h] [ebp-8h]
    FxTrailVertex *outVertPtrEnd; // [esp+8Ch] [ebp-4h]

    SNAP_TOLERANCE_POS = 0.0099999998f;
    SNAP_TOLERANCE_TEXCOORD = 0.0099999998f;
    SNAP_TOLERANCE_NORM = 0.94999999f;
    SMOOTH_THRESHOLD = 0.0;
    trailDef = &edElemDef->trailDef;
    if (edElemDef == (const FxEditorElemDef *)-592)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 862, 0, "%s", "trailDef");
    indCount = trailDef->indCount;
    if ((indCount & 1) != 0)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 865, 0, "%s", "(indCount & 1) == 0");
    vertBytes = 20 * indCount;
    outVertPtrBegin = outVertStorage;
    outVertPtrEnd = &outVertPtrBegin[indCount];
    outVertPtrIter = outVertPtrBegin;
    indPtrEnd = &trailDef->inds[indCount];
    for (primaryEdgeIndPtr = trailDef->inds; primaryEdgeIndPtr != indPtrEnd; primaryEdgeIndPtr += 2)
    {
        FX_ConvertTrail_CalcNormForSegment(
            trailDef->verts[*primaryEdgeIndPtr].pos,
            trailDef->verts[primaryEdgeIndPtr[1]].pos,
            primaryEdgeNorm);
        for (edgeIter = 0; edgeIter != 2; ++edgeIter)
        {
            *(float *)&accumNorm = 0.0;
            *((float *)&accumNorm + 1) = 0.0;
            for (secondaryEdgeIndPtr = trailDef->inds; secondaryEdgeIndPtr != indPtrEnd; secondaryEdgeIndPtr += 2)
            {
                v3 = Vec2Distance(trailDef->verts[*secondaryEdgeIndPtr].pos, trailDef->verts[primaryEdgeIndPtr[edgeIter]].pos);
                if (SNAP_TOLERANCE_POS < v3)
                {
                    v4 = Vec2Distance(
                        trailDef->verts[secondaryEdgeIndPtr[1]].pos,
                        trailDef->verts[primaryEdgeIndPtr[edgeIter]].pos);
                    if (SNAP_TOLERANCE_POS < v4)
                        continue;
                }
                FX_ConvertTrail_CalcNormForSegment(
                    trailDef->verts[*secondaryEdgeIndPtr].pos,
                    trailDef->verts[secondaryEdgeIndPtr[1]].pos,
                    secondaryEdgeNorm);
                v8 = secondaryEdgeNorm[1] * primaryEdgeNorm[1] + secondaryEdgeNorm[0] * primaryEdgeNorm[0];
                if (SMOOTH_THRESHOLD < (double)v8)
                {
                    *(float *)&accumNorm = secondaryEdgeNorm[0] + *(float *)&accumNorm;
                    *((float *)&accumNorm + 1) = secondaryEdgeNorm[1] + *((float *)&accumNorm + 1);
                }
            }
            Vec2Normalize((float *)&accumNorm);
            v11 = &outVertPtrIter[edgeIter];
            pos = (float*)trailDef->verts[primaryEdgeIndPtr[edgeIter]].pos;
            v11->pos[0] = *pos;
            v11->pos[1] = pos[1];
            normal = outVertPtrIter[edgeIter].normal;
            *(_QWORD *)normal = accumNorm;
            outVertPtrIter[edgeIter].texCoord = trailDef->verts[primaryEdgeIndPtr[edgeIter]].texCoord;
        }
        outVertPtrIter += 2;
    }
    if (outVertPtrIter != outVertPtrEnd)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 911, 0, "%s", "outVertPtrIter == outVertPtrEnd");
    emittedVertPtrBegin = outVertPtrBegin;
    emittedVertPtrEnd = outVertPtrBegin;
    indBytes = 2 * indCount;
    emittedIndPtrBegin = outIndexStorage;
    emittedIndPtrEnd = emittedIndPtrBegin;
    for (outVertPtrIter = outVertPtrBegin; outVertPtrIter != outVertPtrEnd; ++outVertPtrIter)
    {
        if (emittedVertPtrEnd > outVertPtrIter)
            MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 923, 0, "%s", "emittedVertPtrEnd <= outVertPtrIter");
        for (emittedVertPtrIter = emittedVertPtrBegin; emittedVertPtrIter != emittedVertPtrEnd; ++emittedVertPtrIter)
        {
            v5 = Vec2Distance(outVertPtrIter->pos, emittedVertPtrIter->pos);
            if (SNAP_TOLERANCE_POS >= v5)
            {
                v9 = outVertPtrIter->texCoord - emittedVertPtrIter->texCoord;
                v7 = I_fabs(v9);
                if (SNAP_TOLERANCE_TEXCOORD >= (double)v7)
                {
                    v6 = emittedVertPtrIter->normal[1] * outVertPtrIter->normal[1]
                        + emittedVertPtrIter->normal[0] * outVertPtrIter->normal[0];
                    if (SNAP_TOLERANCE_NORM <= (double)v6)
                        break;
                }
            }
        }
        if (emittedVertPtrIter == emittedVertPtrEnd)
        {
            *emittedVertPtrIter = *outVertPtrIter;
            ++emittedVertPtrEnd;
        }
        *emittedIndPtrEnd = emittedVertPtrIter - emittedVertPtrBegin;
        if (*emittedIndPtrEnd != emittedVertPtrIter - emittedVertPtrBegin)
            MyAssertHandler(
                ".\\EffectsCore\\fx_convert.cpp",
                943,
                0,
                "%s",
                "*emittedIndPtrEnd == emittedVertPtrIter - emittedVertPtrBegin");
        ++emittedIndPtrEnd;
    }
    outTrailDef->verts = emittedVertPtrBegin;
    outTrailDef->vertCount = emittedVertPtrEnd - emittedVertPtrBegin;
    if (20 * outTrailDef->vertCount > (uint32_t)vertBytes)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            949,
            0,
            "%s",
            "outTrailDef->vertCount * sizeof( FxTrailVertex ) <= static_cast< size_t >( vertBytes )");
    outTrailDef->inds = emittedIndPtrBegin;
    outTrailDef->indCount = emittedIndPtrEnd - emittedIndPtrBegin;
    if (2 * outTrailDef->indCount > (uint32_t)indBytes)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            952,
            0,
            "%s",
            "outTrailDef->indCount * sizeof( ushort ) <= static_cast< size_t >( indBytes )");
}

bool __cdecl FX_ConvertTrail(
    FxTrailDef **outTrailDef,
    const FxEditorElemDef *edElemDef,
    FxRuntimeBlobCursor *cursor)
{
    if (edElemDef->elemType == 3)
    {
        FxTrailVertex *vertices = nullptr;
        uint16_t *indices = nullptr;
        if (!FX_ReserveTrailPayload(
                cursor, edElemDef->trailDef.indCount, outTrailDef, &vertices, &indices))
        {
            return false;
        }
        FX_ConvertTrail_CompileVertices(
            edElemDef, *outTrailDef, vertices, indices);
        if (edElemDef->trailSplitDist <= 0)
            MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 968, 0, "%s", "edElemDef->trailSplitDist > 0");
        if (edElemDef->trailRepeatDist <= 0)
            MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 969, 0, "%s", "edElemDef->trailRepeatDist > 0");
        (*outTrailDef)->splitDist = edElemDef->trailSplitDist;
        (*outTrailDef)->scrollTimeMsec = (int)(edElemDef->trailScrollTime * 1000.0);
        (*outTrailDef)->repeatDist = edElemDef->trailRepeatDist;
    }
    else
    {
        *outTrailDef = 0;
    }
    return true;
}

bool __cdecl FX_ConvertElemDef(
    FxElemDef *elemDef,
    const FxEditorElemDef *edElemDef,
    int32_t velStateCount,
    int32_t visStateCount,
    int32_t emitIndex,
    FxRuntimeBlobCursor *cursor)
{
    int32_t count; // edx
    int32_t amplitude; // ecx
    uint8_t v8; // [esp+8h] [ebp-54h]
    uint8_t v9; // [esp+28h] [ebp-34h]
    int32_t sortOrder; // [esp+30h] [ebp-2Ch]
    float v11; // [esp+40h] [ebp-1Ch]
    float v12; // [esp+44h] [ebp-18h]
    float v13; // [esp+48h] [ebp-14h]
    float v14; // [esp+50h] [ebp-Ch]
    float v15; // [esp+54h] [ebp-8h]
    float v16; // [esp+58h] [ebp-4h]

    if (!elemDef)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 979, 0, "%s", "elemDef");
    if (!edElemDef)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 980, 0, "%s", "edElemDef");
    if (velStateCount < 2)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            981,
            0,
            "%s\n\t(velStateCount) = %i",
            "(velStateCount >= 2)",
            velStateCount);
    if (visStateCount && visStateCount < 2)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            982,
            0,
            "%s\n\t(visStateCount) = %i",
            "(visStateCount == 0 || visStateCount >= 2)",
            visStateCount);
    elemDef->flags = edElemDef->flags;
    FX_CopyCanonicalFloatRange(&elemDef->spawnRange, &edElemDef->spawnRange);
    FX_CopyCanonicalFloatRange(&elemDef->fadeInRange, &edElemDef->fadeInRange);
    FX_CopyCanonicalFloatRange(&elemDef->fadeOutRange, &edElemDef->fadeOutRange);
    elemDef->spawnFrustumCullRadius = edElemDef->spawnFrustumCullRadius;
    if ((edElemDef->editorFlags & 1) != 0)
    {
        count = edElemDef->spawnLooping.count;
        elemDef->spawn.looping.intervalMsec = edElemDef->spawnLooping.intervalMsec;
        elemDef->spawn.looping.count = count;
        if (!elemDef->spawn.looping.count)
            elemDef->spawn.looping.count = 0x7FFFFFFF;
        if (elemDef->spawn.looping.intervalMsec <= 0)
            elemDef->spawn.looping.intervalMsec = 1;
    }
    else
    {
        amplitude = edElemDef->spawnOneShot.count.amplitude;
        elemDef->spawn.looping.intervalMsec = edElemDef->spawnOneShot.count.base;
        elemDef->spawn.looping.count = amplitude;
    }
    FX_CopyCanonicalIntRange(&elemDef->spawnDelayMsec, &edElemDef->spawnDelayMsec);
    FX_CopyCanonicalIntRange(&elemDef->lifeSpanMsec, &edElemDef->lifeSpanMsec);
    FX_CopyCanonicalFloatRange(elemDef->spawnOrigin, edElemDef->spawnOrigin);
    FX_CopyCanonicalFloatRange(&elemDef->spawnOrigin[1], &edElemDef->spawnOrigin[1]);
    FX_CopyCanonicalFloatRange(&elemDef->spawnOrigin[2], &edElemDef->spawnOrigin[2]);
    FX_CopyCanonicalFloatRange(&elemDef->spawnOffsetRadius, &edElemDef->spawnOffsetRadius);
    FX_CopyCanonicalFloatRange(&elemDef->spawnOffsetHeight, &edElemDef->spawnOffsetHeight);
    FX_ScaleFloatRange(elemDef->spawnAngles, edElemDef->spawnAngles, 0.017453292f);
    FX_ScaleFloatRange(&elemDef->spawnAngles[1], &edElemDef->spawnAngles[1], 0.017453292f);
    FX_ScaleFloatRange(&elemDef->spawnAngles[2], &edElemDef->spawnAngles[2], 0.017453292f);
    FX_ScaleFloatRange(elemDef->angularVelocity, edElemDef->angularVelocity, 0.000017453292f);
    FX_ScaleFloatRange(&elemDef->angularVelocity[1], &edElemDef->angularVelocity[1], 0.000017453292f);
    FX_ScaleFloatRange(&elemDef->angularVelocity[2], &edElemDef->angularVelocity[2], 0.000017453292f);
    FX_ScaleFloatRange(&elemDef->initialRotation, &edElemDef->initialRotation, 0.017453292f);
    FX_ScaleFloatRange(&elemDef->gravity, &edElemDef->gravity, 0.0099999998f);
    if (elemDef->gravity.base != 0.0 || elemDef->gravity.amplitude != 0.0)
        elemDef->flags |= 0x4000000u;
    FX_CopyCanonicalFloatRange(&elemDef->reflectionFactor, &edElemDef->elasticity);
    FX_BoundFloatRange(&elemDef->reflectionFactor, 0.0, 1.0);
    FX_ConvertAtlas(elemDef, edElemDef);
    elemDef->elemType = edElemDef->elemType;
    elemDef->visualCount = edElemDef->visualCount;
    elemDef->velIntervalCount = velStateCount - 1;
    if (visStateCount)
        v9 = visStateCount - 1;
    else
        v9 = 0;
    elemDef->visStateIntervalCount = v9;
    if (!FX_ReserveElemDefMemory(elemDef, cursor))
        return false;
    FX_SampleVelocity(elemDef, edElemDef);
    if (visStateCount)
        FX_SampleVisualState(elemDef, edElemDef);
    if (elemDef->elemType == 9)
    {
        FX_CopyMarkVisuals(edElemDef, elemDef->visuals.markArray);
    }
    else if (elemDef->visualCount)
    {
        if (elemDef->visualCount == 1)
            FX_CopyVisuals(edElemDef, (FxElemVisuals *)&elemDef->visuals);
        else
            FX_CopyVisuals(edElemDef, elemDef->visuals.array);
    }
    else
    {
        elemDef->visuals.markArray = 0;
        if (elemDef->elemType == 6 || elemDef->elemType == 7)
        {
            elemDef->visualCount = 1;
        }
        else if (elemDef->elemType == 5)
        {
            elemDef->flags &= ~0x8000000u;
        }
    }
    if (edElemDef->lightingFrac > 255.0f)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            1075,
            0,
            "%s\n\t(edElemDef->lightingFrac) = %g",
            "(edElemDef->lightingFrac <= 255.0f)",
            edElemDef->lightingFrac);
    if ((int)(edElemDef->lightingFrac * 255.0f) >= 256)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            1076,
            0,
            "%s\n\t(edElemDef->lightingFrac) = %g",
            "(static_cast< int32_t >( edElemDef->lightingFrac * 255.0f ) < 256)",
            edElemDef->lightingFrac);
    elemDef->lightingFrac = (int)(edElemDef->lightingFrac * 255.0f);
    elemDef->useItemClip = (edElemDef->editorFlags & 0x20000) != 0;
    if ((edElemDef->editorFlags & 0x10000) != 0)
    {
        v14 = edElemDef->collOffset[0] - edElemDef->collRadius;
        v15 = edElemDef->collOffset[1] - edElemDef->collRadius;
        v16 = edElemDef->collOffset[2] - edElemDef->collRadius;
        elemDef->collMins[0] = v14;
        elemDef->collMins[1] = v15;
        elemDef->collMins[2] = v16;
        v11 = edElemDef->collRadius + edElemDef->collOffset[0];
        v12 = edElemDef->collRadius + edElemDef->collOffset[1];
        v13 = edElemDef->collRadius + edElemDef->collOffset[2];
        elemDef->collMaxs[0] = v11;
        elemDef->collMaxs[1] = v12;
        elemDef->collMaxs[2] = v13;
    }
    else
    {
        elemDef->collMins[0] = 0.0f;
        elemDef->collMins[1] = 0.0f;
        elemDef->collMins[2] = 0.0f;
        elemDef->collMaxs[0] = 0.0f;
        elemDef->collMaxs[1] = 0.0f;
        elemDef->collMaxs[2] = 0.0f;
    }
    if ((edElemDef->editorFlags & 0x2000) != 0)
        FX_ConvertEffectDefRef(&elemDef->effectOnImpact, edElemDef->effectOnImpact);
    else
        FX_ConvertEffectDefRef(&elemDef->effectOnImpact, 0);
    if ((edElemDef->editorFlags & 0x4000) != 0)
        FX_ConvertEffectDefRef(&elemDef->effectOnDeath, edElemDef->effectOnDeath);
    else
        FX_ConvertEffectDefRef(&elemDef->effectOnDeath, 0);
    if ((edElemDef->editorFlags & 0x8000) != 0)
        FX_ConvertEffectDefRef(&elemDef->effectEmitted, edElemDef->emission);
    else
        FX_ConvertEffectDefRef(&elemDef->effectEmitted, 0);
    FX_CopyCanonicalFloatRange(&elemDef->emitDist, &edElemDef->emitDist);
    FX_CopyCanonicalFloatRange(&elemDef->emitDistVariance, &edElemDef->emitDistVariance);
    if (!FX_ConvertTrail(&elemDef->trailDef, edElemDef, cursor))
        return false;
    if (edElemDef->sortOrder < 255)
        sortOrder = edElemDef->sortOrder;
    else
        sortOrder = 255;
    if (sortOrder > 0)
        v8 = sortOrder;
    else
        v8 = 0;
    elemDef->sortOrder = v8;
    return true;
}

int32_t __cdecl FX_ConvertElemDefsOfType(
    FxElemDef *elemDefArray,
    const FxEditorEffectDef *editorEffect,
    uint32_t loopingFlagState,
    const int32_t *velStateCount,
    const int32_t *visStateCount,
    const int32_t *emitIndex,
    FxRuntimeBlobCursor *cursor)
{
    FxElemDef *elemDef; // [esp+0h] [ebp-Ch]
    int32_t elemIndex; // [esp+4h] [ebp-8h]
    int32_t elemCount; // [esp+8h] [ebp-4h]

    if (!elemDefArray)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1135, 0, "%s", "elemDefArray");
    if (!editorEffect)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1136, 0, "%s", "editorEffect");
    if (loopingFlagState > 1)
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            1137,
            0,
            "%s\n\t(loopingFlagState) = %i",
            "(loopingFlagState == FX_ED_FLAG_LOOPING || loopingFlagState == 0)",
            loopingFlagState);
    if (!velStateCount)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1138, 0, "%s", "velStateCount");
    if (!visStateCount)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1139, 0, "%s", "visStateCount");
    elemCount = 0;
    for (elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        if ((editorEffect->elems[elemIndex].editorFlags & 0x80000001) == loopingFlagState)
        {
            elemDef = &elemDefArray[elemCount++];
            if (!FX_ConvertElemDef(
                    elemDef,
                    &editorEffect->elems[elemIndex],
                    velStateCount[elemIndex],
                    visStateCount[elemIndex],
                    emitIndex[elemIndex],
                    cursor))
            {
                return -1;
            }
        }
    }
    return elemCount;
}

int32_t __cdecl FX_CopyEmittedElemDefs(
    FxElemDef *elemDefArray,
    const FxEditorEffectDef *editorEffect,
    FxRuntimeBlobCursor *cursor)
{
    const FxElemDef *elemDefEmit; // [esp+8h] [ebp-20h]
    const FxEffectDef *emission; // [esp+Ch] [ebp-1Ch]
    int32_t elemIndexEmit; // [esp+10h] [ebp-18h]
    FxElemDef *elemDef; // [esp+14h] [ebp-14h]
    int32_t elemIndex; // [esp+1Ch] [ebp-Ch]
    int32_t elemIndexStop; // [esp+20h] [ebp-8h]
    int32_t elemCount; // [esp+24h] [ebp-4h]

    if (!editorEffect)
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1167, 0, "%s", "editorEffect");
    elemCount = 0;
    for (elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        emission = editorEffect->elems[elemIndex].emission;
        if (emission
            && (emission->elemDefCountOneShot < 0
                || emission->elemDefCountLooping < 0
                || emission->elemDefCountOneShot
                    > (std::numeric_limits<int32_t>::max)() - emission->elemDefCountLooping))
        {
            return -1;
        }
        if (emission && emission->elemDefCountOneShot && FX_FindEmission(emission, editorEffect) == elemIndex)
        {
            elemIndexStop = emission->elemDefCountOneShot + emission->elemDefCountLooping;
            if (!emission->elemDefs)
                return -1;
            for (elemIndexEmit = emission->elemDefCountLooping; elemIndexEmit != elemIndexStop; ++elemIndexEmit)
            {
                elemDef = &elemDefArray[elemCount++];
                elemDefEmit = &emission->elemDefs[elemIndexEmit];
                memcpy((void *)elemDef, elemDefEmit, sizeof(FxElemDef));
                if ((elemDef->flags & 0xC0) == 0x40)
                {
                    elemDef->flags &= 0xFFFFFF3F;
                    elemDef->flags = elemDef->flags;
                }
                if (!FX_ReserveElemDefMemory(elemDef, cursor))
                    return -1;
                memcpy(
                    (uint8_t *)elemDef->velSamples,
                    elemDefEmit->velSamples,
                    sizeof(FxElemVelStateSample) * (elemDef->velIntervalCount + 1));
                if (elemDef->visSamples)
                {
                    memcpy(
                        elemDef->visSamples->base.color,
                        elemDefEmit->visSamples,
                        sizeof(FxElemVisStateSample) * (elemDef->visStateIntervalCount + 1));
                }
                if (elemDef->elemType == 9 || elemDef->visualCount > 1u)
                {
                    const size_t visualSize = elemDef->elemType == 9
                        ? sizeof(FxElemMarkVisuals)
                        : sizeof(FxElemVisuals);
                    memcpy(
                        (uint8_t *)elemDef->visuals.markArray,
                        elemDefEmit->visuals.anonymous,
                        visualSize * elemDef->visualCount);
                }
            }
        }
    }
    return elemCount;
}

bool FX_PlanEditorElemDefPayload(
    FxRuntimeBlobCursor *cursor,
    const FxEditorElemDef *elemDef,
    int32_t velStateSampleCount,
    int32_t visStateSampleCount)
{
    return FX_ReserveElemDefPayload(
            cursor,
            elemDef->elemType,
            velStateSampleCount,
            visStateSampleCount,
            elemDef->visualCount)
        && (elemDef->elemType != 3
            || FX_ReserveTrailPayload(cursor, elemDef->trailDef.indCount));
}

bool FX_PlanElemDefsOfType(
    FxRuntimeBlobCursor *cursor,
    const FxEditorEffectDef *editorEffect,
    uint32_t loopingFlagState,
    const int32_t *velStateCount,
    const int32_t *visStateCount)
{
    for (int32_t elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        if ((editorEffect->elems[elemIndex].editorFlags & 0x80000001) == loopingFlagState
            && !FX_PlanEditorElemDefPayload(
                cursor,
                &editorEffect->elems[elemIndex],
                velStateCount[elemIndex],
                visStateCount[elemIndex]))
        {
            return false;
        }
    }
    return true;
}

bool FX_PlanEmittedElemDefPayloads(
    FxRuntimeBlobCursor *cursor,
    const FxEditorEffectDef *editorEffect)
{
    for (int32_t elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        const FxEffectDef *const emission = editorEffect->elems[elemIndex].emission;
        if (!emission)
        {
            continue;
        }
        if (emission->elemDefCountOneShot < 0
            || emission->elemDefCountLooping < 0
            || emission->elemDefCountOneShot
                > (std::numeric_limits<int32_t>::max)() - emission->elemDefCountLooping)
        {
            return false;
        }
        if (!emission->elemDefCountOneShot
            || FX_FindEmission(emission, editorEffect) != elemIndex)
        {
            continue;
        }

        const int32_t elemDefStop =
            emission->elemDefCountLooping + emission->elemDefCountOneShot;
        if (elemDefStop != 0 && !emission->elemDefs)
            return false;
        for (int32_t elemDefIndex = emission->elemDefCountLooping;
            elemDefIndex < elemDefStop;
            ++elemDefIndex)
        {
            const FxElemDef *const elemDef = &emission->elemDefs[elemDefIndex];
            if (!FX_ReserveElemDefPayload(
                    cursor,
                    elemDef->elemType,
                    elemDef->velIntervalCount + 1,
                    elemDef->visStateIntervalCount ? elemDef->visStateIntervalCount + 1 : 0,
                    elemDef->visualCount))
            {
                return false;
            }
        }
    }
    return true;
}

int32_t __cdecl FX_GetLoopingLife(const FxEffectDef *effectDef)
{
    const FxElemDef *elemDef; // [esp+4h] [ebp-Ch]
    int32_t elemIndex; // [esp+8h] [ebp-8h]
    int32_t msecLoopingLifeMax; // [esp+Ch] [ebp-4h]

    msecLoopingLifeMax = 0;
    for (elemIndex = 0; elemIndex < effectDef->elemDefCountLooping; ++elemIndex)
    {
        elemDef = &effectDef->elemDefs[elemIndex];
        if (elemDef->spawn.looping.count == 0x7FFFFFFF)
            return 0x7FFFFFFF;
        if (msecLoopingLifeMax < elemDef->spawn.looping.intervalMsec * (elemDef->spawn.looping.count - 1))
            msecLoopingLifeMax = elemDef->spawn.looping.intervalMsec * (elemDef->spawn.looping.count - 1);
    }
    return msecLoopingLifeMax;
}

const FxEffectDef *__cdecl FX_Convert(const FxEditorEffectDef *editorEffect, void *(*Alloc)(uint32_t))
{
    PhysPreset *v2; // eax
    int32_t v4; // eax
    int32_t v5; // eax
    int32_t v9; // eax
    int32_t v10; // eax
    int32_t v11; // eax
    int32_t LoopingLife; // eax
    int32_t intervalLimit; // [esp+34h] [ebp-1C0h]
    int32_t elemCountTotal; // [esp+38h] [ebp-1BCh]
    int32_t velStateCount[32]; // [esp+3Ch] [ebp-1B8h] BYREF
    FxEffectDef *effect; // [esp+BCh] [ebp-138h]
    int32_t visualIndex; // [esp+C0h] [ebp-134h]
    int32_t emitIndex[32]; // [esp+C4h] [ebp-130h] BYREF
    int32_t firstEmitted; // [esp+144h] [ebp-B0h]
    uint32_t totalBytesNeeded; // [esp+148h] [ebp-ACh]
    int32_t visStateCount[33]; // [esp+14Ch] [ebp-A8h] BYREF
    const FxEditorElemDef *edElemDef; // [esp+1D0h] [ebp-24h]
    int32_t elemIndex; // [esp+1D4h] [ebp-20h]
    FxSampleChannel routing[5]; // [esp+1DCh] [ebp-18h] BYREF
    const FxElemVisuals *elemVisual; // [esp+1F0h] [ebp-4h]

    if (!editorEffect)
    {
        MyAssertHandler(".\\EffectsCore\\fx_convert.cpp", 1429, 0, "%s", "editorEffect");
        return 0;
    }
    if (editorEffect->elemCount < 0
        || editorEffect->elemCount > static_cast<int32_t>(ARRAY_COUNT(emitIndex)))
    {
        return 0;
    }
    memset((uint8_t *)emitIndex, 0xFFu, sizeof(emitIndex));
    elemCountTotal = 0;
    for (elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        if ((editorEffect->elems[elemIndex].editorFlags & 0x80000000) == 0)
            ++elemCountTotal;
    }
    for (elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        edElemDef = &editorEffect->elems[elemIndex];
        if (editorEffect->elems[elemIndex].elemType == 5 && (edElemDef->flags & 0x8000000) != 0)
        {
            for (visualIndex = 0; visualIndex < edElemDef->visualCount; ++visualIndex)
            {
                elemVisual = &edElemDef->visuals[visualIndex];
                XModel *const model = elemVisual->model;
                if (model)
                {
                    if (!model->physPreset)
                    {
                        v2 = FX_RegisterPhysPreset("default");
                        model->physPreset = v2;
                        Com_PrintError(
                            20,
                            "ERROR: no physics preset specified for the FX model [%s]\n",
                            model->name);
                    }
                }
            }
        }
    }
    for (elemIndex = 0; elemIndex < editorEffect->elemCount; ++elemIndex)
    {
        edElemDef = &editorEffect->elems[elemIndex];
        if (!FX_Validate(editorEffect, edElemDef))
            return 0;
        FX_GetVisualSampleRouting(edElemDef, routing);
        intervalLimit = FX_DecideIntervalLimit(edElemDef);
        v4 = FX_DecideVelocitySampleCount(edElemDef, intervalLimit);
        velStateCount[elemIndex] = v4;
        v5 = FX_DecideVisualSampleCount(edElemDef, routing, intervalLimit);
        visStateCount[elemIndex] = v5;
        if (edElemDef->emission && edElemDef->emission->elemDefCountOneShot)
        {
            firstEmitted = FX_FindEmission(edElemDef->emission, editorEffect);
            if (firstEmitted == elemIndex)
            {
                emitIndex[elemIndex] = elemCountTotal;
                if (edElemDef->emission->elemDefCountOneShot < 0
                    || edElemDef->emission->elemDefCountLooping < 0
                    || elemCountTotal > (std::numeric_limits<int32_t>::max)()
                        - edElemDef->emission->elemDefCountOneShot)
                {
                    return 0;
                }
                elemCountTotal += edElemDef->emission->elemDefCountOneShot;
            }
            else
            {
                if (firstEmitted < 0 || firstEmitted >= editorEffect->elemCount)
                    return 0;
                emitIndex[elemIndex] = emitIndex[firstEmitted];
            }
        }
    }
    const size_t effectNameBytes = strlen(editorEffect->name) + 1;
    FxRuntimeBlobCursor sizePlanner;
    if (!sizePlanner.ReserveArray<FxEffectDef>(1)
        || !sizePlanner.ReserveArray<FxElemDef>(static_cast<uint32_t>(elemCountTotal))
        || !FX_PlanElemDefsOfType(
            &sizePlanner, editorEffect, 1u, velStateCount, visStateCount)
        || !FX_PlanElemDefsOfType(
            &sizePlanner, editorEffect, 0u, velStateCount, visStateCount)
        || !FX_PlanEmittedElemDefPayloads(&sizePlanner, editorEffect)
        || !sizePlanner.ReserveBytes(effectNameBytes, alignof(char)))
    {
        return 0;
    }
    totalBytesNeeded = sizePlanner.Offset();
    effect = (FxEffectDef *)Alloc(totalBytesNeeded);
    if (!effect
        || reinterpret_cast<uintptr_t>(effect) % alignof(FxEffectDef) != 0)
    {
        return 0;
    }
    FxRuntimeBlobCursor writer(reinterpret_cast<uint8_t *>(effect), totalBytesNeeded);
    FxEffectDef *effectStorage = nullptr;
    FxElemDef *elemDefStorage = nullptr;
    if (!writer.ReserveArray(1, &effectStorage)
        || !writer.ReserveArray(static_cast<uint32_t>(elemCountTotal), &elemDefStorage)
        || effectStorage != effect)
    {
        return 0;
    }
    effect->elemDefs = elemDefStorage;
    v9 = FX_ConvertElemDefsOfType(
        elemDefStorage, editorEffect, 1u, velStateCount, visStateCount, emitIndex, &writer);
    if (v9 < 0)
        return 0;
    effect->elemDefCountLooping = v9;
    v10 = FX_ConvertElemDefsOfType(
        &elemDefStorage[effect->elemDefCountLooping],
        editorEffect,
        0,
        velStateCount,
        visStateCount,
        emitIndex,
        &writer);
    if (v10 < 0)
        return 0;
    effect->elemDefCountOneShot = v10;
    v11 = FX_CopyEmittedElemDefs(
        &elemDefStorage[effect->elemDefCountOneShot + effect->elemDefCountLooping],
        editorEffect,
        &writer);
    if (v11 < 0)
        return 0;
    effect->elemDefCountEmission = v11;
    if (effect->elemDefCountEmission + effect->elemDefCountOneShot + effect->elemDefCountLooping != elemCountTotal)
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            1515,
            1,
            "effect->elemDefCountLooping + effect->elemDefCountOneShot + effect->elemDefCountEmission == elemCountTotal\n"
            "\t%i, %i",
            effect->elemDefCountEmission + effect->elemDefCountOneShot + effect->elemDefCountLooping,
            elemCountTotal);
        return 0;
    }
    effect->flags = 0;
    for (elemIndex = 0; elemIndex != elemCountTotal; ++elemIndex)
    {
        if ((double)effect->elemDefs[elemIndex].lightingFrac != 0.0)
        {
            effect->flags |= 1u;
            break;
        }
    }
    LoopingLife = FX_GetLoopingLife(effect);
    effect->msecLoopingLife = LoopingLife;
    effect->totalSize = static_cast<int32_t>(totalBytesNeeded);
    void *effectNameStorage = nullptr;
    if (!writer.ReserveBytes(effectNameBytes, alignof(char), &effectNameStorage))
        return 0;
    effect->name = static_cast<const char *>(effectNameStorage);
    memcpy(effectNameStorage, editorEffect->name, effectNameBytes);
    const ptrdiff_t convertedBytes = writer.Offset();
    if (convertedBytes != static_cast<ptrdiff_t>(effect->totalSize))
    {
        MyAssertHandler(
            ".\\EffectsCore\\fx_convert.cpp",
            1536,
            1,
            "memPool - reinterpret_cast< byte * >( effect ) == effect->totalSize\n\t%lld, %i",
            static_cast<long long>(convertedBytes),
            effect->totalSize);
        return 0;
    }
    return effect;
}

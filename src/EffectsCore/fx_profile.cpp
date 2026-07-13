#include "fx_system.h"

#include <universal/sys_atomic.h>

#include <cstdlib>

namespace
{
[[noreturn]] void FX_DropCorruptProfilePool(
    const char *const poolName,
    const std::uint16_t handle)
{
    Com_Error(
        ERR_DROP,
        "Corrupt FX profile %s handle %u is invalid or refers to a free pool slot",
        poolName,
        static_cast<unsigned int>(handle));
    std::abort();
}

[[noreturn]] void FX_DropCorruptProfileList(const char *const reason)
{
    Com_Error(ERR_DROP, "Corrupt FX profile list: %s", reason);
    std::abort();
}

FxPool<FxElem> *FX_GetAllocatedProfileElem(
    FxSystem *const system,
    const std::uint16_t handle)
{
    if (!system || !system->elems)
        FX_DropCorruptProfilePool("element", handle);
    FxPool<FxElem> *const slot =
        FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(system->elems, handle);
    if (!FX_IsElemAllocated(system, &slot->item))
        FX_DropCorruptProfilePool("element", handle);
    return slot;
}

FxPool<FxTrail> *FX_GetAllocatedProfileTrail(
    FxSystem *const system,
    const std::uint16_t handle)
{
    if (!system || !system->trails)
        FX_DropCorruptProfilePool("trail", handle);
    FxPool<FxTrail> *const slot =
        FX_PoolFromHandle_Generic<FxTrail, MAX_TRAILS>(system->trails, handle);
    if (!FX_IsTrailAllocated(system, &slot->item))
        FX_DropCorruptProfilePool("trail", handle);
    return slot;
}

FxPool<FxTrailElem> *FX_GetAllocatedProfileTrailElem(
    FxSystem *const system,
    const std::uint16_t handle)
{
    if (!system || !system->trailElems)
        FX_DropCorruptProfilePool("trail element", handle);
    FxPool<FxTrailElem> *const slot =
        FX_PoolFromHandle_Generic<FxTrailElem, MAX_TRAIL_ELEMS>(
            system->trailElems, handle);
    if (!FX_IsTrailElemAllocated(system, &slot->item))
        FX_DropCorruptProfilePool("trail element", handle);
    return slot;
}

int __cdecl FX_CompareProfileEntriesForQsort(
    const void *const lhs,
    const void *const rhs)
{
    return FX_CompareProfileEntries(
        static_cast<const FxProfileEntry *>(lhs),
        static_cast<const FxProfileEntry *>(rhs));
}
}

void __cdecl FX_DrawProfile(
    int32_t clientIndex,
    FxProfileDrawFunc drawFunc,
    float *profilePos)
{
    char *v3; // eax
    char *v4; // eax
    char *v5; // eax
    char *v6; // eax
    char *v7; // eax
    char *v8; // eax
    char *v9; // eax
    char *v10; // eax
    int32_t v11; // [esp+38h] [ebp-701Ch]
    int32_t j; // [esp+3Ch] [ebp-7018h]
    FxEffect *effect; // [esp+40h] [ebp-7014h]
    int32_t entryCount; // [esp+44h] [ebp-7010h] BYREF
    FxSystem *system; // [esp+48h] [ebp-700Ch]
    FxProfileEntry entryPool[1024]; // [esp+4Ch] [ebp-7008h] BYREF
    FxProfileEntry *entry; // [esp+704Ch] [ebp-8h]
    std::uint32_t i; // [esp+7050h] [ebp-4h]

    system = FX_GetSystem(clientIndex);
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_profile.cpp", 181, 0, "%s", "system");
    FX_BeginIteratingOverEffects_Cooperative(system);
    entryCount = 0;
    const std::int32_t firstActiveEffect =
        Sys_AtomicLoad(&system->firstActiveEffect);
    const std::int32_t firstNewEffect =
        Sys_AtomicLoad(&system->firstNewEffect);
    const std::int64_t activeEffectCount64 =
        static_cast<std::int64_t>(firstNewEffect) - firstActiveEffect;
    if (firstActiveEffect < 0 || firstNewEffect < firstActiveEffect
        || activeEffectCount64 > FX_EFFECT_LIMIT)
        FX_DropCorruptProfileList("active effect ring exceeds capacity");
    const std::uint32_t activeEffectCount =
        static_cast<std::uint32_t>(activeEffectCount64);
    for (std::uint32_t activeOffset = 0;
         activeOffset < activeEffectCount;
         ++activeOffset)
    {
        i = static_cast<std::uint32_t>(firstActiveEffect) + activeOffset;
        effect = FX_EffectFromHandle(system, system->allEffectHandles[i & 0x3FF]);
        if ((static_cast<std::uint32_t>(
                 Sys_AtomicLoad(&effect->status))
             & FX_STATUS_REF_COUNT_MASK) == 0)
        {
            continue;
        }
        if (!effect->def)
            FX_DropCorruptProfileList("referenced effect has no definition");
        entry = FX_GetProfileEntry(effect->def, entryPool, &entryCount);
        FX_ProfileSingleEffect(system, effect, entry);
    }
    const std::int32_t activeTrailCount =
        Sys_AtomicLoad(&system->activeTrailCount);
    const std::int32_t activeElemCount =
        Sys_AtomicLoad(&system->activeElemCount);
    const std::int32_t activeTrailElemCount =
        Sys_AtomicLoad(&system->activeTrailElemCount);
    FxSpotLightStateSnapshot spotLightSnapshot{};
    if (!FX_GetSpotLightStateSnapshot(system, &spotLightSnapshot))
        FX_DropCorruptProfileList("missing spotlight state");
    const std::int32_t activeSpotLightEffectCount =
        spotLightSnapshot.effectCount;
    const std::int32_t activeSpotLightElemCount =
        spotLightSnapshot.elemCount;
    const std::int32_t gfxCloudCount =
        Sys_AtomicLoad(&system->gfxCloudCount);
    const bool validPoolCounts = activeTrailCount >= 0
        && activeTrailCount <= MAX_TRAILS
        && activeElemCount >= 0
        && activeElemCount <= MAX_ELEMS
        && activeTrailElemCount >= 0
        && activeTrailElemCount <= MAX_TRAIL_ELEMS
        && gfxCloudCount >= 0
        && gfxCloudCount <= 256;
    const bool validSpotLightCountPair =
        activeSpotLightEffectCount >= 0
        && activeSpotLightEffectCount <= 1
        && activeSpotLightElemCount >= 0
        && activeSpotLightElemCount <= activeSpotLightEffectCount;
    FX_EndIteratingOverEffects_Cooperative(system);
    if (!validPoolCounts)
        FX_DropCorruptProfileList("invalid FX pool count");
    if (!validSpotLightCountPair)
        FX_DropCorruptProfileList("invalid spotlight count pair");
    qsort(
        entryPool,
        entryCount,
        sizeof(FxProfileEntry),
        FX_CompareProfileEntriesForQsort);
    v11 = static_cast<std::int32_t>(activeEffectCount);
    v3 = va("%4i of %4i effect objects in use (%.0f%%; %i free)", v11, 1024, (double)v11 * 0.09765625, 1024 - v11);
    drawFunc(v3, profilePos);
    v4 = va(
        "%4i of %4i trails in use (%.0f%%; %i free)",
        activeTrailCount,
        128,
        (double)activeTrailCount * 0.78125,
        128 - activeTrailCount);
    drawFunc(v4, profilePos);
    v5 = va(
        "%4i of %4i spot lights in use (%.0f%%; %i free)",
        activeSpotLightEffectCount,
        1,
        (double)activeSpotLightEffectCount * 100.0,
        1 - activeSpotLightEffectCount);
    drawFunc(v5, profilePos);
    v6 = va(
        "%4i of %4i effect elements in use (%.0f%%; %i free)",
        activeElemCount,
        2048,
        (double)activeElemCount * 0.048828125,
        2048 - activeElemCount);
    drawFunc(v6, profilePos);
    v7 = va(
        "%4i of %4i trail elements in use (%.0f%%; %i free)",
        activeTrailElemCount,
        2048,
        (double)activeTrailElemCount * 0.048828125,
        2048 - activeTrailElemCount);
    drawFunc(v7, profilePos);
    v8 = va(
        "%4i of %4i cloud elements in use (%.0f%%; %i free)",
        gfxCloudCount,
        256,
        static_cast<double>(gfxCloudCount) * 0.390625,
        256 - gfxCloudCount);
    drawFunc(v8, profilePos);
    drawFunc("", profilePos);
    drawFunc(
        "Effects are sorted by percent of available resources used",
        profilePos);
    v9 = va(
        "%4s  %4s (%4s/%4s)  %4s  %4s (%4s/%4s)  %s",
        "objs",
        "elem",
        "actv",
        "pend",
        "trls",
        "telm",
        "actv",
        "pend",
        "effectname");
    drawFunc(v9, profilePos);
    drawFunc(
        "-------------------------------------------------------------------",
        profilePos);
    for (j = 0; j < entryCount; ++j)
    {
        entry = &entryPool[j];
        v10 = va(
            "%4i  %4i (%4i/%4i)  %4i  %4i (%4i/%4i)  %s",
            entry->effectCount,
            entry->pendingElemCount + entry->activeElemCount,
            entry->activeElemCount,
            entry->pendingElemCount,
            entry->trailCount,
            entry->pendingTrailElemCount + entry->activeTrailElemCount,
            entry->activeTrailElemCount,
            entry->pendingTrailElemCount,
            entry->effectDef->name);
        drawFunc(v10, profilePos);
    }
}

FxProfileEntry *__cdecl FX_GetProfileEntry(const FxEffectDef *effectDef, FxProfileEntry *entryPool, int32_t *entryCount)
{
    int32_t entryIndex; // [esp+0h] [ebp-4h]

    if (!effectDef || !entryPool || !entryCount
        || *entryCount < 0 || *entryCount > FX_EFFECT_LIMIT)
    {
        FX_DropCorruptProfileList("invalid profile entry state");
    }
    for (entryIndex = 0; entryIndex < *entryCount; ++entryIndex)
    {
        if (entryPool[entryIndex].effectDef == effectDef)
            return &entryPool[entryIndex];
    }
    if (entryIndex != *entryCount)
        MyAssertHandler(
            ".\\EffectsCore\\fx_profile.cpp",
            84,
            0,
            "entryIndex == *entryCount\n\t%i, %i",
            entryIndex,
            *entryCount);
    if (*entryCount == FX_EFFECT_LIMIT)
        FX_DropCorruptProfileList("profile entry pool exceeds capacity");
    ++*entryCount;
    entryPool[entryIndex].effectDef = effectDef;
    entryPool[entryIndex].effectCount = 0;
    entryPool[entryIndex].activeElemCount = 0;
    entryPool[entryIndex].pendingElemCount = 0;
    entryPool[entryIndex].trailCount = 0;
    entryPool[entryIndex].activeTrailElemCount = 0;
    entryPool[entryIndex].pendingTrailElemCount = 0;
    return &entryPool[entryIndex];
}

void __cdecl FX_ProfileSingleEffect(FxSystem *system, const FxEffect *effect, FxProfileEntry *entry)
{
    uint16_t elemHandle; // [esp+0h] [ebp-1Ch]
    uint16_t trailHandle; // [esp+4h] [ebp-18h]
    FxPool<FxTrail> *trail; // [esp+8h] [ebp-14h]
    FxPool<FxTrailElem> *trailElem; // [esp+Ch] [ebp-10h]
    FxPool<FxElem> *elem; // [esp+10h] [ebp-Ch]
    uint32_t elemClass; // [esp+14h] [ebp-8h]
    uint16_t trailElemHandle; // [esp+18h] [ebp-4h]

    if (!system || !effect || !entry)
        FX_DropCorruptProfileList("missing effect profile state");
    ++entry->effectCount;
    for (elemClass = 0; elemClass < 3; ++elemClass)
    {
        std::size_t elemCount = 0;
        for (elemHandle = effect->firstElemHandle[elemClass];
            elemHandle != 0xFFFF;
            elemHandle = elem->item.nextElemHandleInEffect)
        {
            if (elemCount++ >= MAX_ELEMS)
                FX_DropCorruptProfileList("element chain exceeds pool capacity");
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 334, 0, "%s", "system");
            elem = FX_GetAllocatedProfileElem(system, elemHandle);
            if (elem->item.msecBegin > system->msecNow)
                ++entry->pendingElemCount;
            else
                ++entry->activeElemCount;
        }
    }
    std::size_t trailCount = 0;
    for (trailHandle = effect->firstTrailHandle; trailHandle != 0xFFFF; trailHandle = trail->item.nextTrailHandle)
    {
        if (trailCount++ >= MAX_TRAILS)
            FX_DropCorruptProfileList("trail chain exceeds pool capacity");
        if (!system)
            MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 362, 0, "%s", "system");
        trail = FX_GetAllocatedProfileTrail(system, trailHandle);
        ++entry->trailCount;
        std::size_t trailElemCount = 0;
        for (trailElemHandle = trail->item.firstElemHandle;
            trailElemHandle != 0xFFFF;
            trailElemHandle = trailElem->item.nextTrailElemHandle)
        {
            if (trailElemCount++ >= MAX_TRAIL_ELEMS)
                FX_DropCorruptProfileList("trail-element chain exceeds pool capacity");
            if (!system)
                MyAssertHandler("c:\\trees\\cod3\\src\\effectscore\\fx_system.h", 348, 0, "%s", "system");
            trailElem =
                FX_GetAllocatedProfileTrailElem(system, trailElemHandle);
            if (trailElem->item.msecBegin > system->msecNow)
                ++entry->pendingTrailElemCount;
            else
                ++entry->activeTrailElemCount;
        }
    }
}

int32_t __cdecl FX_CompareProfileEntries(const FxProfileEntry *e0, const FxProfileEntry *e1)
{
    float cost; // [esp+8h] [ebp-8h]
    float cost_4; // [esp+Ch] [ebp-4h]

    cost = FX_GetProfileEntryCost(e0);
    cost_4 = FX_GetProfileEntryCost(e1);
    if (cost_4 > (double)cost)
        return 1;
    if (cost_4 >= (double)cost)
        return 0;
    return -1;
}

double __cdecl FX_GetProfileEntryCost(const FxProfileEntry *entry)
{
    float v3; // [esp+4h] [ebp-10h]
    float costEffect; // [esp+Ch] [ebp-8h]
    float costElem; // [esp+10h] [ebp-4h]

    costEffect = (double)entry->effectCount * (1.0f / 1024.0f);
    costElem = (double)(entry->pendingElemCount + entry->activeElemCount) * (1.0f / 2048.0f);
    v3 = costEffect - costElem;
    if (v3 < 0.0)
        return (float)((double)(entry->pendingElemCount + entry->activeElemCount) * (1.0f / 2048.0f));
    else
        return (float)((double)entry->effectCount * (1.0f / 1024.0f));
}
void __cdecl FX_DrawMarkProfile(
    int32_t clientIndex,
    FxProfileDrawFunc drawFunc,
    float *profilePos)
{
    const char* v3; // eax
    char* v4; // eax
    char* v5; // eax
    char* v6; // eax
    char* v7; // eax
    char* v8; // eax
    char* v9; // eax
    char* v10; // eax
    uint16_t v11; // ax
    int32_t modelIndex; // [esp-Ch] [ebp-60h]
    int32_t worldBrushMarks; // [esp+10h] [ebp-44h]
    int32_t freeMarks2; // [esp+1Ch] [ebp-38h]
    int32_t entityIndex; // [esp+20h] [ebp-34h]
    int32_t wastedPointGroups; // [esp+24h] [ebp-30h]
    FxTriGroupPool* triGroup; // [esp+28h] [ebp-2Ch]
    FxPointGroupPool* pointGroup; // [esp+2Ch] [ebp-28h]
    int32_t entBrushMarks; // [esp+30h] [ebp-24h]
    int32_t freeMarks; // [esp+34h] [ebp-20h]
    int32_t wastedTriGroups; // [esp+38h] [ebp-1Ch]
    int32_t freePointGroups; // [esp+3Ch] [ebp-18h]
    int32_t entModelMarks; // [esp+40h] [ebp-14h]
    int32_t worldModelMarks; // [esp+44h] [ebp-10h]
    uint16_t markHandle; // [esp+48h] [ebp-Ch]
    int32_t freeTriGroups; // [esp+4Ch] [ebp-8h]
    FxMark* markIter; // [esp+50h] [ebp-4h]
    FxMark* markItera; // [esp+50h] [ebp-4h]

    if (clientIndex)
        MyAssertHandler(
            "c:\\trees\\cod3\\src\\effectscore\\fx_marks.h",
            139,
            0,
            "%s\n\t(clientIndex) = %i",
            "(clientIndex == 0)",
            clientIndex);
    markHandle = fx_marksSystemPool[0].firstFreeMarkHandle;
    freeMarks = 0;
    while (markHandle != 0xFFFF)
    {
        ++freeMarks;
        markHandle = FX_MarkFromHandle(fx_marksSystemPool, markHandle)->nextMark;
    }
    freeTriGroups = 0;
    for (triGroup = fx_marksSystemPool[0].firstFreeTriGroup; triGroup; triGroup = triGroup->nextFreeTriGroup)
        ++freeTriGroups;
    freePointGroups = 0;
    for (pointGroup = fx_marksSystemPool[0].firstFreePointGroup; pointGroup; pointGroup = pointGroup->nextFreePointGroup)
        ++freePointGroups;
    freeMarks2 = 0;
    worldBrushMarks = 0;
    entModelMarks = 0;
    entBrushMarks = 0;
    worldModelMarks = 0;
    wastedPointGroups = 0;
    wastedTriGroups = 0;
    for (markIter = fx_marksSystemPool[0].marks; markIter != (FxMark*)fx_marksSystemPool[0].triGroups; ++markIter)
    {
        if (markIter->frameCountDrawn == -1)
        {
            ++freeMarks2;
        }
        else
        {
            wastedPointGroups += markIter->pointCount & 0x80000001;
            wastedTriGroups += markIter->triCount & 1;
            switch (markIter->context.modelTypeAndSurf & 0xC0)
            {
            case 0:
                ++worldBrushMarks;
                break;
            case 0x40:
                ++worldModelMarks;
                break;
            case 0x80:
                ++entBrushMarks;
                break;
            case 0xC0:
                ++entModelMarks;
                break;
            default:
                continue;
            }
        }
    }
    if (freeMarks != freeMarks2)
    {
        v3 = va("%i %i", freeMarks, freeMarks2);
        MyAssertHandler(".\\EffectsCore\\fx_profile.cpp", 397, 0, "%s\n\t%s", "freeMarks == freeMarks2", v3);
    }
    v4 = va(
        "%4i of %4i marks in use (%i alloced, %i freed)",
        512 - freeMarks,
        512,
        fx_marksSystemPool[0].allocedMarkCount,
        fx_marksSystemPool[0].freedMarkCount);
    drawFunc(v4, profilePos);
    v5 = va("%4i of %4i triGroups in use (%4i wasted)", 2048 - freeTriGroups, 2048, wastedTriGroups);
    drawFunc(v5, profilePos);
    v6 = va("%4i of %4i pointGroups in use (%4i wasted)", 3072 - freePointGroups, 3072, wastedPointGroups);
    drawFunc(v6, profilePos);
    drawFunc(" ", profilePos);
    v7 = va("%4i World Brush Marks", worldBrushMarks);
    drawFunc(v7, profilePos);
    v8 = va("%4i World Model Marks", worldModelMarks);
    drawFunc(v8, profilePos);
    v9 = va("%4i Ent Brush Marks", entBrushMarks);
    drawFunc(v9, profilePos);
    v10 = va("%4i Ent Model", entModelMarks);
    drawFunc(v10, profilePos);
    FX_DrawMarkProfile_MarkPrint(
        fx_marksSystemPool,
        fx_marksSystemPool[0].firstActiveWorldMarkHandle,
        "world",
        0,
        drawFunc,
        profilePos);

    for (entityIndex = 0; entityIndex != MAX_GENTITIES; ++entityIndex)
        FX_DrawMarkProfile_MarkPrint(
            fx_marksSystemPool,
            fx_marksSystemPool[0].entFirstMarkHandles[entityIndex],
            "ent",
            entityIndex,
            drawFunc,
            profilePos);
    for (markItera = fx_marksSystemPool[0].marks; markItera != (FxMark*)fx_marksSystemPool[0].triGroups; ++markItera)
    {
        if (markItera->frameCountDrawn != -1
            && markItera->prevMark == 0xFFFF
            && (markItera->context.modelTypeAndSurf & 0xC0) == 0x40)
        {
            modelIndex = markItera->context.modelIndex;
            v11 = FX_MarkToHandle(fx_marksSystemPool, markItera);
            FX_DrawMarkProfile_MarkPrint(fx_marksSystemPool, v11, "sm", modelIndex, drawFunc, profilePos);
        }
    }
}

const char *__cdecl typeAsString(uint8_t type)
{
    const char *result; // eax

    switch (type)
    {
    case 0u:
        result = "wbrush";
        break;
    case 0x40u:
        result = "wmodel";
        break;
    case 0x80u:
        result = "ebrush";
        break;
    case 0xC0u:
        result = "emodel";
        break;
    default:
        if (!alwaysfails)
            MyAssertHandler(".\\EffectsCore\\fx_profile.cpp", 228, 0, "unpossible");
        result = "unknown";
        break;
    }
    return result;
}

void __cdecl FX_DrawMarkProfile_MarkPrint(
    FxMarksSystem *marksSystem,
    uint16_t head,
    const char *name,
    int32_t index,
    FxProfileDrawFunc drawFunc,
    float *profilePos)
{
    const char *v6; // eax
    const char *v7; // eax
    const char *v8; // eax
    const char *v9; // eax
    uint8_t thisMarkType; // [esp+3h] [ebp-1Dh]
    FxMark *mark; // [esp+4h] [ebp-1Ch]
    int32_t markCount; // [esp+8h] [ebp-18h]
    int32_t triCount; // [esp+Ch] [ebp-14h]
    int32_t pointCount; // [esp+10h] [ebp-10h]
    uint8_t type; // [esp+17h] [ebp-9h]
    uint16_t lastMarkHandle; // [esp+18h] [ebp-8h]

    markCount = 0;
    triCount = 0;
    pointCount = 0;
    type = 0;
    lastMarkHandle = -1;
    while (head != 0xFFFF)
    {
        mark = FX_MarkFromHandle(marksSystem, head);
        if (mark->frameCountDrawn == -1)
            MyAssertHandler(".\\EffectsCore\\fx_profile.cpp", 299, 0, "%s", "mark->frameCountDrawn != FX_MARK_FREE");
        if (mark->prevMark != lastMarkHandle)
            MyAssertHandler(".\\EffectsCore\\fx_profile.cpp", 300, 0, "%s", "mark->prevMark == lastMarkHandle");
        triCount += mark->triCount;
        pointCount += mark->pointCount;
        thisMarkType = mark->context.modelTypeAndSurf & 0xC0;
        if (!markCount)
            type = mark->context.modelTypeAndSurf & 0xC0;
        if (type != thisMarkType)
        {
            v6 = va("%i %i", type, thisMarkType);
            MyAssertHandler(".\\EffectsCore\\fx_profile.cpp", 306, 0, "%s\n\t%s", "type == thisMarkType", v6);
        }
        if (mark->context.modelIndex != index)
        {
            v7 = va("%i %i", mark->context.modelIndex, index);
            MyAssertHandler(".\\EffectsCore\\fx_profile.cpp", 307, 0, "%s\n\t%s", "mark->context.modelIndex == index", v7);
        }
        ++markCount;
        lastMarkHandle = head;
        head = mark->nextMark;
    }
    if (markCount)
    {
        v8 = typeAsString(type);
        v9 = va("%s%5i %s %i (%i pts, %i tris)", name, index, v8, markCount, pointCount, triCount);
        drawFunc(v9, profilePos);
    }
}

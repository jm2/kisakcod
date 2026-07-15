#include "fx_system.h"
#include "fx_iterator_atomic.h"
#include <universal/profile.h>
#include <universal/sys_atomic.h>

#include <array>
#include <cstdlib>


namespace
{
struct FxSortExclusiveIteratorThreadState
{
    FxSystem *system = nullptr;
    std::uint32_t generation = 0;
};

thread_local FxSortExclusiveIteratorThreadState
    fx_sortExclusiveIteratorThreadState{};

[[noreturn]] void FX_DropCorruptSortList(const char *const reason)
{
    Com_Error(ERR_DROP, "Corrupt FX element sort list: %s", reason);
    std::abort();
}

std::size_t FX_GetValidatedSortElemDefCount(const FxEffect *const effect)
{
    if (!effect || !effect->def
        || effect->def->elemDefCountLooping < 0
        || effect->def->elemDefCountOneShot < 0
        || effect->def->elemDefCountEmission < 0)
    {
        FX_DropCorruptSortList("invalid effect definition state");
    }
    const std::int64_t elemDefCount =
        static_cast<std::int64_t>(effect->def->elemDefCountLooping)
        + effect->def->elemDefCountOneShot
        + effect->def->elemDefCountEmission;
    if (elemDefCount > 256
        || (elemDefCount > 0 && !effect->def->elemDefs))
    {
        FX_DropCorruptSortList("invalid effect definition count");
    }
    return static_cast<std::size_t>(elemDefCount);
}

const FxElemDef *FX_GetValidatedSortElemDef(
    const FxEffect *const effect,
    const std::uint32_t elemDefIndex)
{
    const std::size_t elemDefCount =
        FX_GetValidatedSortElemDefCount(effect);
    if (elemDefIndex >= elemDefCount)
        FX_DropCorruptSortList("invalid element definition index");
    return &effect->def->elemDefs[elemDefIndex];
}

void FX_ValidateSortChains(
    FxSystem *const system,
    const FxEffect *const effect,
    const std::uint16_t firstElemHandle,
    const std::uint16_t firstSortedElemHandle)
{
    std::array<bool, MAX_ELEMS> visited{};
    std::size_t visitedCount = 0;

    const auto visit = [&](const std::uint16_t handle) {
        if (visitedCount >= MAX_ELEMS)
            FX_DropCorruptSortList("chain exceeds the element pool capacity");

        FxPool<FxElem> *const elem =
            FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                system->elems, handle);
        if (!FX_IsElemAllocated(system, &elem->item))
            FX_DropCorruptSortList("element handle refers to a free pool slot");
        const std::size_t elemIndex =
            static_cast<std::size_t>(elem - system->elems);
        if (visited[elemIndex])
            FX_DropCorruptSortList("cycle or duplicate element handle");
        const FxElemDef *const elemDef =
            FX_GetValidatedSortElemDef(effect, elem->item.defIndex);
        if (elemDef->elemType > FX_ELEM_TYPE_TAIL)
        {
            FX_DropCorruptSortList(
                "non-sprite element in sprite sort chain");
        }

        visited[elemIndex] = true;
        ++visitedCount;
        return elem;
    };

    std::uint16_t handle = firstElemHandle;
    while (handle != firstSortedElemHandle)
        handle = visit(handle)->item.nextElemHandleInEffect;

    handle = firstSortedElemHandle;
    while (handle != FX_INVALID_HANDLE)
        handle = visit(handle)->item.nextElemHandleInEffect;
}

void FX_ValidateInsertionList(
    FxSystem *const system,
    const FxEffect *const effect,
    const std::uint16_t firstElemHandle,
    const std::uint16_t insertedElemHandle)
{
    std::array<bool, MAX_ELEMS> visited{};
    std::size_t visitedCount = 0;
    std::uint16_t handle = firstElemHandle;
    std::uint16_t expectedPreviousHandle = FX_INVALID_HANDLE;
    while (handle != FX_INVALID_HANDLE)
    {
        if (visitedCount >= MAX_ELEMS)
            FX_DropCorruptSortList("insertion chain exceeds the element pool capacity");
        if (handle == insertedElemHandle)
            FX_DropCorruptSortList("element is already linked into its insertion chain");

        FxPool<FxElem> *const elem =
            FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                system->elems, handle);
        if (!FX_IsElemAllocated(system, &elem->item))
            FX_DropCorruptSortList("insertion handle refers to a free pool slot");
        const std::size_t elemIndex =
            static_cast<std::size_t>(elem - system->elems);
        if (visited[elemIndex])
            FX_DropCorruptSortList("cycle in insertion chain");
        if (elem->item.prevElemHandleInEffect != expectedPreviousHandle)
            FX_DropCorruptSortList("insertion chain has an invalid backlink");
        const FxElemDef *const elemDef =
            FX_GetValidatedSortElemDef(effect, elem->item.defIndex);
        if (elemDef->elemType > FX_ELEM_TYPE_TAIL)
            FX_DropCorruptSortList("non-sprite element in insertion chain");

        visited[elemIndex] = true;
        ++visitedCount;
        expectedPreviousHandle = handle;
        handle = elem->item.nextElemHandleInEffect;
    }
}
}

bool __cdecl FX_CurrentThreadOwnsSortExclusive(
    const FxSystem *const system) noexcept
{
    if (!fx_sortExclusiveIteratorThreadState.system)
        return false;
    if (fx_sortExclusiveIteratorThreadState.generation
        != FX_GetCooperativeIteratorGeneration(
            fx_sortExclusiveIteratorThreadState.system))
    {
        fx_sortExclusiveIteratorThreadState = {};
        return false;
    }
    return fx_sortExclusiveIteratorThreadState.system == system;
}

void __cdecl FX_AbandonCurrentThreadSortExclusiveForError() noexcept
{
    const FxSortExclusiveIteratorThreadState state =
        fx_sortExclusiveIteratorThreadState;
    fx_sortExclusiveIteratorThreadState = {};
    if (state.system
        && state.generation
            == FX_GetCooperativeIteratorGeneration(state.system))
    {
        (void)FxIteratorEndExclusive(&state.system->iteratorCount);
    }
}


void __cdecl FX_SortEffects(FxSystem *system)
{
    float diff[9]; // [esp+20h] [ebp-1048h] BYREF
    FxEffect *firstEffect; // [esp+44h] [ebp-1024h]
    float v3[1024]; // [esp+48h] [ebp-1020h]
    int v4; // [esp+1048h] [ebp-20h]
    uint16_t v5; // [esp+104Ch] [ebp-1Ch]
    float *a; // [esp+1050h] [ebp-18h]
    std::uint32_t j; // [esp+1054h] [ebp-14h]
    std::uint32_t v8; // [esp+1058h] [ebp-10h]
    FxEffect *secondEffect; // [esp+105Ch] [ebp-Ch]
    float v10; // [esp+1060h] [ebp-8h]
    std::uint32_t i; // [esp+1064h] [ebp-4h]

    PROF_SCOPED("FX_Sort");
    if (!system)
        MyAssertHandler(".\\EffectsCore\\fx_sort.cpp", 98, 0, "%s", "system");
    a = (float *)system;
    FX_WaitBeginIteratingOverEffects_Exclusive(system);
    const std::int32_t firstActiveEffect =
        Sys_AtomicLoad(&system->firstActiveEffect);
    const std::int32_t firstNewEffect =
        Sys_AtomicLoad(&system->firstNewEffect);
    const std::int64_t activeEffectCount64 =
        static_cast<std::int64_t>(firstNewEffect) - firstActiveEffect;
    if (firstActiveEffect < 0 || firstNewEffect < firstActiveEffect
        || activeEffectCount64 > FX_EFFECT_LIMIT)
        FX_DropCorruptSortList("active effect ring exceeds capacity");
    const std::uint32_t activeEffectCount =
        static_cast<std::uint32_t>(activeEffectCount64);
    for (std::uint32_t activeOffset = 0;
         activeOffset < activeEffectCount;
         ++activeOffset)
    {
        i = static_cast<std::uint32_t>(firstActiveEffect) + activeOffset;
        v5 = system->allEffectHandles[i & 0x3FF];
        firstEffect = FX_EffectFromHandle(system, v5);
        Vec3Sub(a, firstEffect->frameNow.origin, diff);
        v10 = Vec3LengthSq(diff);
        for (j = i;
             j != static_cast<std::uint32_t>(firstActiveEffect);
             --j)
        {
            v4 = static_cast<int>((j - 1u) & 0x3FFu);
            if (v3[v4] >= v10 * 0.9998999834060669)
            {
                if (v3[v4] >= v10 * 1.000100016593933)
                    break;
                secondEffect = FX_EffectFromHandle(system, system->allEffectHandles[v4]);
                if (secondEffect->owner != firstEffect->owner || !FX_FirstEffectIsFurther(firstEffect, secondEffect))
                    break;
            }
            v8 = j & 0x3FF;
            v3[v8] = v3[v4];
            system->allEffectHandles[v8] = system->allEffectHandles[v4];
        }
        v8 = j & 0x3FF;
        v3[v8] = v10;
        system->allEffectHandles[v8] = v5;
    }
    if (fx_sortExclusiveIteratorThreadState.system != system
        || fx_sortExclusiveIteratorThreadState.generation
            != FX_GetCooperativeIteratorGeneration(system))
    {
        FX_DropCorruptSortList(
            "exclusive iterator release lacks thread ownership");
    }
    const bool releasedExclusive =
        FxIteratorEndExclusive(&system->iteratorCount);
    fx_sortExclusiveIteratorThreadState = {};
    if (!releasedExclusive)
        FX_DropCorruptSortList("failed to release exclusive iterator ownership");
}

void __cdecl FX_WaitBeginIteratingOverEffects_Exclusive(FxSystem *system)
{
    if (!system)
        FX_DropCorruptSortList("missing exclusive iterator system");
    const std::uint32_t admissionGeneration =
        FX_GetCooperativeIteratorGeneration(system);
    if (fx_sortExclusiveIteratorThreadState.system)
    {
        if (fx_sortExclusiveIteratorThreadState.generation
            == FX_GetCooperativeIteratorGeneration(
                fx_sortExclusiveIteratorThreadState.system))
        {
            FX_DropCorruptSortList("nested exclusive iterator admission");
        }
        fx_sortExclusiveIteratorThreadState = {};
    }
    if (FX_CurrentThreadOwnsCooperativeIterator(system))
        FX_DropCorruptSortList(
            "exclusive admission while owning a cooperative iterator");
    if (FX_ThreadOwnsEffectKillExclusive(system))
        FX_DropCorruptSortList(
            "sort admission while owning kill exclusivity");
    for (;;)
    {
        FX_WaitForArchiveGate(system);
        FxIteratorWaitBeginExclusive(&system->iteratorCount);
        const std::uint32_t currentGeneration =
            FX_GetCooperativeIteratorGeneration(system);
        const bool initialized = system->isInitialized != 0;
        const bool archiveActive = FX_ArchiveGateIsActive(system);
        if (!archiveActive && initialized
            && currentGeneration == admissionGeneration)
        {
            fx_sortExclusiveIteratorThreadState.system = system;
            fx_sortExclusiveIteratorThreadState.generation =
                currentGeneration;
            return;
        }
        if (!FxIteratorEndExclusive(&system->iteratorCount))
            FX_DropCorruptSortList("failed to roll back exclusive iterator admission");
        if (currentGeneration != admissionGeneration)
        {
            FX_DropCorruptSortList(
                "exclusive iterator crossed an FX lifecycle boundary");
        }
        if (!initialized)
        {
            FX_DropCorruptSortList(
                "exclusive iterator entered an uninitialized FX system");
        }
    }
}

bool __cdecl FX_FirstEffectIsFurther(FxEffect *firstEffect, FxEffect *secondEffect)
{
    if (firstEffect->boltAndSortOrder.sortOrder == 255
        && secondEffect->boltAndSortOrder.sortOrder == 255)
    {
        return 0;
    }
    if (firstEffect->boltAndSortOrder.sortOrder == 255)
        firstEffect->boltAndSortOrder.sortOrder = FX_CalcRunnerParentSortOrder(firstEffect);
    if (secondEffect->boltAndSortOrder.sortOrder == 255)
        secondEffect->boltAndSortOrder.sortOrder = FX_CalcRunnerParentSortOrder(secondEffect);
    return firstEffect->boltAndSortOrder.sortOrder < secondEffect->boltAndSortOrder.sortOrder;
}

int __cdecl FX_CalcRunnerParentSortOrder(FxEffect *effect)
{
    int v3; // [esp+8h] [ebp-20h]
    const FxEffectDef *def; // [esp+10h] [ebp-18h]
    int totalNonRunnerElemDefs; // [esp+14h] [ebp-14h]
    const FxElemDef *elemDef; // [esp+18h] [ebp-10h]
    int totalSortOrder; // [esp+20h] [ebp-8h]
    int elemDefIndex; // [esp+24h] [ebp-4h]

    if (!effect)
        FX_DropCorruptSortList("missing effect while calculating sort order");
    def = effect->def;
    const std::size_t elemDefCount =
        FX_GetValidatedSortElemDefCount(effect);
    totalSortOrder = 0;
    totalNonRunnerElemDefs = 0;
    for (elemDefIndex = 0;
        static_cast<std::size_t>(elemDefIndex) < elemDefCount;
        ++elemDefIndex)
    {
        elemDef = &def->elemDefs[elemDefIndex];
        if (elemDef->elemType != 10)
        {
            totalSortOrder += elemDef->sortOrder;
            ++totalNonRunnerElemDefs;
        }
    }
    if (totalNonRunnerElemDefs <= 0)
        return 0;
    if (totalSortOrder / totalNonRunnerElemDefs < 254)
        v3 = totalSortOrder / totalNonRunnerElemDefs;
    else
        v3 = 254;
    if (v3 > 0)
        return v3;
    else
        return 0;
}

void __cdecl FX_SortNewElemsInEffect(FxSystem *system, FxEffect *effect)
{
    uint16_t elemHandle; // [esp+8h] [ebp-Ch]
    uint16_t elemHandlea; // [esp+8h] [ebp-Ch]
    uint16_t stopElemHandle; // [esp+Ch] [ebp-8h]
    FxPool<FxElem> *stopElem; // [esp+10h] [ebp-4h]
    FxPool<FxElem> *elema; // [esp+10h] [ebp-4h]
    FxPool<FxElem> *elem; // [esp+10h] [ebp-4h]

    if (!system || !effect)
        FX_DropCorruptSortList("missing system or effect");
    FX_BeginReadingCameraPublication(system);
    elemHandle = effect->firstElemHandle[0];
    stopElemHandle = effect->firstSortedElemHandle;
    // Validate both the unsorted prefix and existing sorted suffix, including
    // every definition index/type, before detaching or relinking any node.
    FX_ValidateSortChains(
        system, effect, elemHandle, stopElemHandle);
    if (elemHandle != stopElemHandle)
    {
        stopElem = nullptr;
        if (stopElemHandle != FX_INVALID_HANDLE)
        {
            stopElem = FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                system->elems, stopElemHandle);
            if (!FX_IsElemAllocated(system, &stopElem->item))
                FX_DropCorruptSortList("sort boundary refers to a free pool slot");
        }
        effect->firstElemHandle[0] = stopElemHandle;
        if (stopElem)
            stopElem->item.prevElemHandleInEffect = FX_INVALID_HANDLE;

        std::size_t sortedElemCount = 0;
        do
        {
            if (sortedElemCount++ >= MAX_ELEMS)
                FX_DropCorruptSortList("new-element chain exceeds the element pool capacity");
            elema = FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                system->elems, elemHandle);
            if (!FX_IsElemAllocated(system, &elema->item))
                FX_DropCorruptSortList("new-element handle refers to a free pool slot");
            elemHandle = elema->item.nextElemHandleInEffect;
            FX_SortSpriteElemIntoEffect(system, effect, &elema->item);
        } while (elemHandle != stopElemHandle);
        effect->firstSortedElemHandle = effect->firstElemHandle[0];

        std::size_t validatedElemCount = 0;
        for (elemHandlea = effect->firstElemHandle[0]; elemHandlea != 0xFFFF; elemHandlea = elem->item.nextElemHandleInEffect)
        {
            if (validatedElemCount++ >= MAX_ELEMS)
                FX_DropCorruptSortList("sorted chain exceeds the element pool capacity");
            elem = FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                system->elems, elemHandlea);
            if (!FX_IsElemAllocated(system, &elem->item))
                FX_DropCorruptSortList("sorted handle refers to a free pool slot");
            const FxElemDef *const elemDef =
                FX_GetValidatedSortElemDef(effect, elem->item.defIndex);
            if (elemDef->elemType > FX_ELEM_TYPE_TAIL)
                FX_DropCorruptSortList("non-sprite element in sorted list");
        }
    }
    FX_EndReadingCameraPublication(system);
}

void __cdecl FX_SortSpriteElemIntoEffect(FxSystem *system, FxEffect *effect, FxElem *elem)
{
    uint16_t v3; // [esp+5Ah] [ebp-2Ah]
    FxInsertSortElem sortElem; // [esp+5Ch] [ebp-28h] BYREF
    uint16_t elemHandle; // [esp+70h] [ebp-14h]
    FxElem *nextElem; // [esp+74h] [ebp-10h]
    uint16_t *prevNextElemHandle; // [esp+78h] [ebp-Ch]
    FxElem *prevElem; // [esp+7Ch] [ebp-8h]
    uint16_t prevElemHandle; // [esp+80h] [ebp-4h]
    std::size_t traversedElemCount;

    nextElem = 0;
    prevElemHandle = -1;
    if (!system || !effect || !elem)
        FX_DropCorruptSortList("missing insertion state");
    if (!FX_IsElemAllocated(system, elem))
        FX_DropCorruptSortList("inserted element refers to a free pool slot");
    prevNextElemHandle = effect->firstElemHandle;
    elemHandle = FX_PoolToHandle_Generic<FxElem, MAX_ELEMS>(system->elems, elem);
    if (elemHandle == FX_INVALID_HANDLE)
        FX_InvalidPoolHandle(system->elems, elemHandle);
    FX_GetInsertSortElem(system, effect, elem, &sortElem);
    if (sortElem.defSortOrder < 0)
        MyAssertHandler(".\\EffectsCore\\fx_sort.cpp", 215, 0, "%s", "sortElem.defSortOrder >= 0");

    FX_ValidateInsertionList(
        system, effect, effect->firstElemHandle[0], elemHandle);
    traversedElemCount = 0;
    if (effect->firstElemHandle[0] != 0xFFFF)
    {
        do
        {
            if (traversedElemCount++ >= MAX_ELEMS)
                FX_DropCorruptSortList("insertion traversal exceeds the element pool capacity");
            v3 = *prevNextElemHandle;
            nextElem = &FX_PoolFromHandle_Generic<FxElem, MAX_ELEMS>(
                system->elems, v3)->item;
            if (!FX_IsElemAllocated(system, nextElem))
                FX_DropCorruptSortList("insertion traversal refers to a free pool slot");
            if (!FX_ExistingElemSortsBeforeNewElem(system, effect, nextElem, &sortElem))
                break;
            prevElem = nextElem;
            prevElemHandle = *prevNextElemHandle;
            prevNextElemHandle = &nextElem->nextElemHandleInEffect;
        } while (nextElem->nextElemHandleInEffect != 0xFFFF);
    }
    elem->nextElemHandleInEffect = *prevNextElemHandle;
    elem->prevElemHandleInEffect = prevElemHandle;
    *prevNextElemHandle = elemHandle;
    if (elem->nextElemHandleInEffect != 0xFFFF)
        nextElem->prevElemHandleInEffect = elemHandle;
}

void __cdecl FX_GetInsertSortElem(
    const FxSystem *system,
    const FxEffect *effect,
    const FxElem *elem,
    FxInsertSortElem *sortElem)
{
    float diff[3]; // [esp+0h] [ebp-54h] BYREF
    float posWorld[3]; // [esp+10h] [ebp-44h] BYREF
    const FxElemDef *elemDef; // [esp+1Ch] [ebp-38h]
    int randomSeed; // [esp+20h] [ebp-34h]
    orientation_t orient; // [esp+24h] [ebp-30h] BYREF

    sortElem->msecBegin = elem->msecBegin;
    sortElem->defIndex = elem->defIndex;
    elemDef = FX_GetValidatedSortElemDef(effect, elem->defIndex);
    sortElem->elemType = elemDef->elemType;
    if (elemDef->elemType > FX_ELEM_TYPE_TAIL)
        FX_DropCorruptSortList("non-sprite element in sprite insertion");
    sortElem->defSortOrder = elemDef->sortOrder;
    randomSeed = (296 * elem->sequence + elem->msecBegin + (uint32_t)effect->randomSeed) % 0x1DF;
    FX_GetOrientation(elemDef, &effect->frameAtSpawn, &effect->frameNow, randomSeed, &orient);
    FX_OrientationPosToWorldPos(&orient, elem->origin, posWorld);
    Vec3Sub(system->cameraPrev.origin, posWorld, diff);
    sortElem->distToCamSq = Vec3LengthSq(diff);
}

bool __cdecl FX_ExistingElemSortsBeforeNewElem(
    const FxSystem *system,
    const FxEffect *effect,
    const FxElem *elem,
    const FxInsertSortElem *sortElemNew)
{
    float diff[3]; // [esp+4h] [ebp-58h] BYREF
    float posWorld[3]; // [esp+14h] [ebp-48h] BYREF
    float distToCamSq; // [esp+20h] [ebp-3Ch]
    const FxElemDef *elemDef; // [esp+24h] [ebp-38h]
    int randomSeed; // [esp+28h] [ebp-34h]
    orientation_t orient; // [esp+2Ch] [ebp-30h] BYREF

    elemDef = FX_GetValidatedSortElemDef(effect, elem->defIndex);
    if (elemDef->elemType > FX_ELEM_TYPE_TAIL)
        FX_DropCorruptSortList("non-sprite element in sprite sort list");
    if (!elemDef->visualCount)
        return 0;
    if (elemDef->sortOrder < sortElemNew->defSortOrder)
        return 1;
    if (elemDef->sortOrder > sortElemNew->defSortOrder)
        return 0;
    randomSeed = (elem->msecBegin + effect->randomSeed + 296 * (uint32_t)elem->sequence) % 0x1DF;
    FX_GetOrientation(elemDef, &effect->frameAtSpawn, &effect->frameNow, randomSeed, &orient);
    FX_OrientationPosToWorldPos(&orient, elem->origin, posWorld);
    Vec3Sub(system->cameraPrev.origin, posWorld, diff);
    distToCamSq = Vec3LengthSq(diff);
    return sortElemNew->distToCamSq < (double)distToCamSq;
}

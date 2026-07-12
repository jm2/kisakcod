#include <EffectsCore/fx_runtime.h>
#include <universal/sys_atomic.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

#define CHECK_FX_ATOMIC_WORD(type, member)                                      \
    static_assert(std::is_same_v<decltype(type::member), volatile std::int32_t>); \
    static_assert(offsetof(type, member) % alignof(std::int32_t) == 0)

CHECK_FX_ATOMIC_WORD(FxEffect, status);
CHECK_FX_ATOMIC_WORD(FxEffect, frameCount);
CHECK_FX_ATOMIC_WORD(FxCamera, isValid);
CHECK_FX_ATOMIC_WORD(FxVisState, blockerCount);
CHECK_FX_ATOMIC_WORD(FxSystem, firstFreeElem);
CHECK_FX_ATOMIC_WORD(FxSystem, firstFreeTrailElem);
CHECK_FX_ATOMIC_WORD(FxSystem, firstFreeTrail);
CHECK_FX_ATOMIC_WORD(FxSystem, deferredElemCount);
CHECK_FX_ATOMIC_WORD(FxSystem, activeElemCount);
CHECK_FX_ATOMIC_WORD(FxSystem, activeTrailElemCount);
CHECK_FX_ATOMIC_WORD(FxSystem, activeTrailCount);
CHECK_FX_ATOMIC_WORD(FxSystem, gfxCloudCount);
CHECK_FX_ATOMIC_WORD(FxSystem, firstActiveEffect);
CHECK_FX_ATOMIC_WORD(FxSystem, firstNewEffect);
CHECK_FX_ATOMIC_WORD(FxSystem, firstFreeEffect);
CHECK_FX_ATOMIC_WORD(FxSystem, activeSpotLightEffectCount);
CHECK_FX_ATOMIC_WORD(FxSystem, activeSpotLightElemCount);
CHECK_FX_ATOMIC_WORD(FxSystem, iteratorCount);
CHECK_FX_ATOMIC_WORD(FxSystem, msecDraw);

#undef CHECK_FX_ATOMIC_WORD

static_assert(sizeof(FxEffectDef) == (KISAK_PTR_BITS == 32 ? 0x20 : 0x28));
static_assert(alignof(FxEffectDef) == (KISAK_PTR_BITS == 32 ? 0x4 : 0x8));
static_assert(sizeof(FxProfileEntry) == (KISAK_PTR_BITS == 32 ? 0x1C : 0x20));
static_assert(sizeof(FxEffect) == (KISAK_PTR_BITS == 32 ? 0x80 : 0x88));
static_assert(sizeof(FxCamera) == 0xB0);
static_assert(sizeof(FxSpriteInfo) == (KISAK_PTR_BITS == 32 ? 0x10 : 0x20));
static_assert(sizeof(FxVisState) == 0x1010);
static_assert(sizeof(FxSystem) == (KISAK_PTR_BITS == 32 ? 0xA60 : 0xA90));
static_assert(sizeof(FxSystemBuffers) == (KISAK_PTR_BITS == 32 ? 0x47480 : 0x49480));
static_assert(sizeof(FxImpactEntry) == (KISAK_PTR_BITS == 32 ? 0x84 : 0x108));
static_assert(sizeof(FxImpactTable) == (KISAK_PTR_BITS == 32 ? 0x8 : 0x10));

#define CHECK_FX_RUNTIME_AGGREGATE(type)        \
    static_assert(std::is_standard_layout_v<type>); \
    static_assert(std::is_trivially_copyable_v<type>)

CHECK_FX_RUNTIME_AGGREGATE(FxEffectDef);
CHECK_FX_RUNTIME_AGGREGATE(FxProfileEntry);
CHECK_FX_RUNTIME_AGGREGATE(FxEffect);
CHECK_FX_RUNTIME_AGGREGATE(FxCamera);
CHECK_FX_RUNTIME_AGGREGATE(FxSpriteInfo);
CHECK_FX_RUNTIME_AGGREGATE(FxVisState);
CHECK_FX_RUNTIME_AGGREGATE(FxSystem);
CHECK_FX_RUNTIME_AGGREGATE(FxSystemBuffers);
CHECK_FX_RUNTIME_AGGREGATE(FxImpactEntry);
CHECK_FX_RUNTIME_AGGREGATE(FxImpactTable);

#undef CHECK_FX_RUNTIME_AGGREGATE

static bool ExerciseWord(volatile std::int32_t *word)
{
    const bool exchangeMatches = Sys_AtomicExchange(word, 1) == 0;
    const bool incrementMatches = Sys_AtomicIncrement(word) == 2;
    const bool fetchAddMatches = Sys_AtomicFetchAdd(word, 3) == 2;
    const bool compareExchangeMatches = Sys_AtomicCompareExchange(word, 7, 5) == 5;
    const bool decrementMatches = Sys_AtomicDecrement(word) == 6;
    return exchangeMatches && incrementMatches && fetchAddMatches
        && compareExchangeMatches && decrementMatches;
}

int main()
{
    FxEffect effect{};
    FxCamera camera{};
    FxVisState visState{};
    FxSystem system{};

    bool success = true;
    success &= ExerciseWord(&effect.status);
    success &= ExerciseWord(&effect.frameCount);
    success &= ExerciseWord(&camera.isValid);
    success &= ExerciseWord(&visState.blockerCount);
    success &= ExerciseWord(&system.firstFreeElem);
    success &= ExerciseWord(&system.firstFreeTrailElem);
    success &= ExerciseWord(&system.firstFreeTrail);
    success &= ExerciseWord(&system.deferredElemCount);
    success &= ExerciseWord(&system.activeElemCount);
    success &= ExerciseWord(&system.activeTrailElemCount);
    success &= ExerciseWord(&system.activeTrailCount);
    success &= ExerciseWord(&system.gfxCloudCount);
    success &= ExerciseWord(&system.firstActiveEffect);
    success &= ExerciseWord(&system.firstNewEffect);
    success &= ExerciseWord(&system.firstFreeEffect);
    success &= ExerciseWord(&system.activeSpotLightEffectCount);
    success &= ExerciseWord(&system.activeSpotLightElemCount);
    success &= ExerciseWord(&system.iteratorCount);
    success &= ExerciseWord(&system.msecDraw);
    return success ? 0 : 1;
}

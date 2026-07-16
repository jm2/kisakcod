#pragma once

#include <universal/kisak_abi.h>
#include <universal/q_shared.h>
#include <universal/com_math.h>

#include <EffectsCore/fx_runtime.h>
#include <EffectsCore/fx_pool.h>
           
struct orientation_t // sizeof=0x30
{                                       // ...
    float origin[3];                    // ...
    float axis[3][3];                   // ...
};

struct GfxMarkContext // sizeof=0x6
{                                       // ...
    uint8_t lmapIndex;          // ...
    uint8_t primaryLightIndex;  // ...
    uint8_t reflectionProbeIndex; // ...
    uint8_t modelTypeAndSurf;   // ...
    uint16_t modelIndex;        // ...
};

/////////////////////////////////////////////////////////////////////////////////
struct FxMarkPoint // sizeof=0x20
{                                       // ...
    float xyz[3];
    float lmapCoord[2];
    float normal[3];
};
struct FxPointGroup // sizeof=0x44
{                                       // ...
    FxMarkPoint points[2];
    int next;
};
union FxPointGroupPool // sizeof=0x44
{                                       // ...
    FxPointGroupPool *nextFreePointGroup;
    FxPointGroup pointGroup;
};

struct FxTriGroup // sizeof=0x18
{                                       // ...
    uint16_t indices[2][3];
    GfxMarkContext context;
    uint8_t triCount;
    uint8_t unused[1];
    int next;
};

union FxTriGroupPool // sizeof=0x18
{                                       // ...
    FxTriGroupPool* nextFreeTriGroup;
    FxTriGroup triGroup;
};

struct FxMark // sizeof=0x44
{                                       // ...
    uint16_t prevMark;
    uint16_t nextMark;
    int frameCountDrawn;
    int frameCountAlloced;
    float origin[3];
    float radius;
    float texCoordAxis[3];
    uint8_t nativeColor[4];
    Material *material;
    GfxMarkContext context;
    uint8_t triCount;
    // padding byte
    uint16_t pointCount;
    // padding byte
    // padding byte
    int tris;
    int points;
};

struct FxMarksSystem
{                                       // ...
    int frameCount;
    uint16_t firstFreeMarkHandle;
    uint16_t firstActiveWorldMarkHandle;
    uint16_t entFirstMarkHandles[MAX_GENTITIES];
    FxTriGroupPool *firstFreeTriGroup;
    FxPointGroupPool *firstFreePointGroup;
    FxMark marks[512];
    FxTriGroupPool triGroups[2048];
    FxPointGroupPool pointGroups[3072]; // ...
    bool noMarks;
    bool hasCarryIndex;
    uint16_t carryIndex;
    uint32_t allocedMarkCount;
    uint32_t freedMarkCount;
};
struct FxUpdateElem // sizeof=0x7C
{                                       // ...
    FxEffect *effect;
    int elemIndex;
    int atRestFraction;                 // ...
    orientation_t orient;
    int randomSeed;
    int sequence;
    float msecLifeSpan;
    int msecElemBegin;
    int msecElemEnd;
    int msecUpdateBegin;
    int msecUpdateEnd;                  // ...
    float msecElapsed;
    float normTimeUpdateEnd;
    float *elemOrigin;
    float *elemBaseVel;                 // ...
    float posWorld[3];
    bool onGround;                      // ...
    // padding byte
    // padding byte
    // padding byte
    int physObjId;                      // ...
};
struct FxCmd // sizeof=0xC
{                                       // ...
    FxSystem *system;
    int localClientNum;
    volatile std::int32_t *spawnLock;
};
RUNTIME_SIZE(FxCmd, 0xC, 0x18);
RUNTIME_OFFSET(FxCmd, localClientNum, 0x4, 0x8);
RUNTIME_OFFSET(FxCmd, spawnLock, 0x8, 0x10);

struct FxElemPreVisualState // sizeof=0x1C
{                                       // ...
    float sampleLerp;                   // ...
    float sampleLerpInv;                // ...
    const FxElemDef *elemDef;
    const FxEffect *effect;
    const FxElemVisStateSample *refState; // ...
    int randomSeed;
    uint32_t distanceFade;
};
template<typename ITEM_TYPE, size_t LIMIT>
uint16 FX_PoolToHandle_Generic(FxPool<ITEM_TYPE>* poolArray, ITEM_TYPE* item)
{
    static_assert(FxPoolSlotLayoutIsCompatible<ITEM_TYPE>(),
                  "FX handle stride must match the legacy item stride");
    const std::uint16_t handle =
        FxEncodeHandle<FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
            poolArray, item);
    vassert(handle != FX_INVALID_HANDLE, "%p %p", poolArray, item);
    return handle;
}

template<typename ITEM_TYPE, size_t LIMIT>
FxPool<ITEM_TYPE>* FX_PoolFromHandle_Generic(FxPool<ITEM_TYPE>* poolArray, uint handle)
{
    static_assert(FxPoolSlotLayoutIsCompatible<ITEM_TYPE>(),
                  "FX handle stride must match the legacy item stride");
    FxPool<ITEM_TYPE> *const item =
        FxDecodeHandle<FxPool<ITEM_TYPE>, LIMIT, ITEM_TYPE::HANDLE_SCALE>(
            poolArray, handle);
    vassert(item != nullptr, "%p %u", poolArray, handle);
    if (!item)
        FX_InvalidPoolHandle(poolArray, handle);
    return item;
}

#pragma once

#include <cstddef>
#include <cstdint>

#include <universal/kisak_abi.h>

constexpr std::size_t MAX_EFFECTS = 1024;
constexpr std::size_t MAX_ELEMS = 2048;
constexpr std::size_t MAX_TRAILS = 128;
constexpr std::size_t MAX_TRAIL_ELEMS = 2048;

struct Material;
struct FxElemDef;

struct r_double_index_t
{
    r_double_index_t()
        : kisak(0)
    {
    }

    r_double_index_t(int value)
        : kisak(static_cast<std::uint32_t>(value))
    {
    }

    union
    {
        std::uint16_t value[2];
        std::uint32_t kisak;
    };
};

struct FxBoltAndSortOrder
{
    std::uint32_t dobjHandle : 12;
    std::uint32_t temporalBits : 1;
    std::uint32_t boneIndex : 11;
    std::uint32_t sortOrder : 8;
};

struct FxSpatialFrame
{
    float quat[4];
    float origin[3];
};

struct FxEffectDef
{
    const char *name;
    int flags;
    int totalSize;
    int msecLoopingLife;
    int elemDefCountLooping;
    int elemDefCountOneShot;
    int elemDefCountEmission;
    const FxElemDef *elemDefs;
};
RUNTIME_SIZE(FxEffectDef, 0x20, 0x28);

struct FxProfileEntry
{
    const FxEffectDef *effectDef;
    std::int32_t effectCount;
    std::int32_t activeElemCount;
    std::int32_t pendingElemCount;
    std::int32_t trailCount;
    std::int32_t activeTrailElemCount;
    std::int32_t pendingTrailElemCount;
};
RUNTIME_SIZE(FxProfileEntry, 0x1C, 0x20);

struct FxEffect
{
    const FxEffectDef *def;
    alignas(4) volatile std::int32_t status;
    std::uint16_t firstElemHandle[3];
    std::uint16_t firstSortedElemHandle;
    std::uint16_t firstTrailHandle;
    std::uint16_t randomSeed;
    std::uint16_t owner;
    std::uint16_t packedLighting;
    FxBoltAndSortOrder boltAndSortOrder;
    alignas(4) volatile std::int32_t frameCount;
    int msecBegin;
    int msecLastUpdate;
    FxSpatialFrame frameAtSpawn;
    FxSpatialFrame frameNow;
    FxSpatialFrame framePrev;
    float distanceTraveled;
};
RUNTIME_SIZE(FxEffect, 0x80, 0x88);

template <typename T>
struct FxPool
{
    union
    {
        int nextFree;
        T item;
    };
};

struct FxCamera
{
    float origin[3];
    alignas(4) volatile std::int32_t isValid;
    float frustum[6][4];
    float axis[3][3];
    std::uint32_t frustumPlaneCount;
    float viewOffset[3];
    std::uint32_t pad[3];
};
RUNTIME_SIZE(FxCamera, 0xB0, 0xB0);

struct FxSpriteInfo
{
    r_double_index_t *indices;
    std::uint32_t indexCount;
    Material *material;
    const char *name;
};
RUNTIME_SIZE(FxSpriteInfo, 0x10, 0x20);

union FxElem_u
{
    float trailTexCoord;
    std::uint16_t lightingHandle;
};

struct FxElem
{
    std::uint8_t defIndex;
    std::uint8_t sequence;
    std::uint8_t atRestFraction;
    std::uint8_t emitResidual;
    std::uint16_t nextElemHandleInEffect;
    std::uint16_t prevElemHandleInEffect;
    int msecBegin;
    float baseVel[3];
    union
    {
        int physObjId;
        float origin[3];
    };
    FxElem_u u;

    static constexpr std::size_t HANDLE_SCALE = 4;
};

struct FxTrail
{
    std::uint16_t nextTrailHandle;
    std::uint16_t firstElemHandle;
    std::uint16_t lastElemHandle;
    char defIndex;
    char sequence;

    static constexpr std::size_t HANDLE_SCALE = 4;
};

struct FxTrailElem
{
    float origin[3];
    float spawnDist;
    int msecBegin;
    std::uint16_t nextTrailElemHandle;
    std::int16_t baseVelZ;
    char basis[2][3];
    std::uint8_t sequence;
    std::uint8_t unused;

    static constexpr std::size_t HANDLE_SCALE = 4;
};

struct FxVisBlocker
{
    float origin[3];
    std::uint16_t radius;
    std::uint16_t visibility;
};
RUNTIME_SIZE(FxVisBlocker, 0x10, 0x10);

struct FxVisState
{
    FxVisBlocker blocker[256];
    alignas(4) volatile std::int32_t blockerCount;
    std::uint32_t pad[3];
};
RUNTIME_SIZE(FxVisState, 0x1010, 0x1010);
RUNTIME_OFFSET(FxVisState, blockerCount, 0x1000, 0x1000);

struct FxSystem
{
    FxCamera camera;
    FxCamera cameraPrev;
    FxSpriteInfo sprite;
    FxEffect *effects;
    FxPool<FxElem> *elems;
    FxPool<FxTrail> *trails;
    FxPool<FxTrailElem> *trailElems;
    std::uint16_t *deferredElems;
    alignas(4) volatile std::int32_t firstFreeElem;
    alignas(4) volatile std::int32_t firstFreeTrailElem;
    alignas(4) volatile std::int32_t firstFreeTrail;
    alignas(4) volatile std::int32_t deferredElemCount;
    alignas(4) volatile std::int32_t activeElemCount;
    alignas(4) volatile std::int32_t activeTrailElemCount;
    alignas(4) volatile std::int32_t activeTrailCount;
    alignas(4) volatile std::int32_t gfxCloudCount;
    FxVisState *visState;
    const FxVisState *visStateBufferRead;
    FxVisState *visStateBufferWrite;
    alignas(4) volatile std::int32_t firstActiveEffect;
    alignas(4) volatile std::int32_t firstNewEffect;
    alignas(4) volatile std::int32_t firstFreeEffect;
    std::uint16_t allEffectHandles[MAX_EFFECTS];
    alignas(4) volatile std::int32_t activeSpotLightEffectCount;
    alignas(4) volatile std::int32_t activeSpotLightElemCount;
    std::uint16_t activeSpotLightEffectHandle;
    std::uint16_t activeSpotLightElemHandle;
    std::int16_t activeSpotLightBoltDobj;
    alignas(4) volatile std::int32_t iteratorCount;
    int msecNow;
    alignas(4) volatile std::int32_t msecDraw;
    int frameCount;
    bool isInitialized;
    bool needsGarbageCollection;
    bool isArchiving;
    std::uint8_t localClientNum;
    std::uint32_t restartList[32];
};
RUNTIME_SIZE(FxSystem, 0xA60, 0xA90);

struct FxImpactEntry
{
    const FxEffectDef *nonflesh[29];
    const FxEffectDef *flesh[4];
};
RUNTIME_SIZE(FxImpactEntry, 0x84, 0x108);

struct FxImpactTable
{
    const char *name;
    FxImpactEntry *table;
};
RUNTIME_SIZE(FxImpactTable, 0x8, 0x10);

struct FxSystemBuffers
{
    FxEffect effects[MAX_EFFECTS];
    FxPool<FxElem> elems[MAX_ELEMS];
    FxPool<FxTrail> trails[MAX_TRAILS];
    FxPool<FxTrailElem> trailElems[MAX_TRAIL_ELEMS];
    FxVisState visState[2];
    std::uint16_t deferredElems[MAX_ELEMS];
    std::uint8_t padBuffer[96];
};
RUNTIME_SIZE(FxSystemBuffers, 0x47480, 0x49480);

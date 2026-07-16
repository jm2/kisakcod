#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <universal/kisak_abi.h>

struct FxEffect;
struct Material;
struct XModel;
struct FxElemDef;

// Canonical native-width runtime effect definition.  These records are shared
// by portable fast-file conversion and renderer-facing FX code; serialized
// Disk32 mirrors remain separate in fx_fastfile_disk32.h.
constexpr std::size_t FX_ELEM_DEF_RUNTIME_SIZE =
    KISAK_ARCH_64BIT ? 0x120u : 0xFCu;
constexpr std::size_t FX_ELEM_DEF_RUNTIME_ALIGNMENT =
    KISAK_ARCH_64BIT ? 8u : 4u;

struct FxEffectDef
{
    const char *name;
    std::int32_t flags;
    std::int32_t totalSize;
    std::int32_t msecLoopingLife;
    std::int32_t elemDefCountLooping;
    std::int32_t elemDefCountOneShot;
    std::int32_t elemDefCountEmission;
    const FxElemDef *elemDefs;
};
RUNTIME_SIZE(FxEffectDef, 0x20, 0x28);
RUNTIME_OFFSET(FxEffectDef, name, 0x0, 0x0);
RUNTIME_OFFSET(FxEffectDef, flags, 0x4, 0x8);
RUNTIME_OFFSET(FxEffectDef, elemDefs, 0x1C, 0x20);

struct FxFloatRange
{
    float base;
    float amplitude;
};
RUNTIME_SIZE(FxFloatRange, 0x8, 0x8);

struct FxSpawnDefLooping
{
    std::int32_t intervalMsec;
    std::int32_t count;
};
RUNTIME_SIZE(FxSpawnDefLooping, 0x8, 0x8);
RUNTIME_OFFSET(FxSpawnDefLooping, intervalMsec, 0x0, 0x0);
RUNTIME_OFFSET(FxSpawnDefLooping, count, 0x4, 0x4);

struct FxIntRange
{
    std::int32_t base;
    std::int32_t amplitude;
};
RUNTIME_SIZE(FxIntRange, 0x8, 0x8);
RUNTIME_OFFSET(FxIntRange, base, 0x0, 0x0);
RUNTIME_OFFSET(FxIntRange, amplitude, 0x4, 0x4);

struct FxSpawnDefOneShot
{
    FxIntRange count;
};
RUNTIME_SIZE(FxSpawnDefOneShot, 0x8, 0x8);
RUNTIME_OFFSET(FxSpawnDefOneShot, count, 0x0, 0x0);

union FxSpawnDef
{
    FxSpawnDefLooping looping;
    FxSpawnDefOneShot oneShot;
};
RUNTIME_SIZE(FxSpawnDef, 0x8, 0x8);

struct FxElemAtlas
{
    std::uint8_t behavior;
    std::uint8_t index;
    std::uint8_t fps;
    std::uint8_t loopCount;
    std::uint8_t colIndexBits;
    std::uint8_t rowIndexBits;
    std::int16_t entryCount;
};
RUNTIME_SIZE(FxElemAtlas, 0x8, 0x8);

struct FxElemVec3Range
{
    float base[3];
    float amplitude[3];
};
RUNTIME_SIZE(FxElemVec3Range, 0x18, 0x18);

struct FxElemVisualState
{
    std::uint8_t color[4];
    float rotationDelta;
    float rotationTotal;
    float size[2];
    float scale;
};
RUNTIME_SIZE(FxElemVisualState, 0x18, 0x18);

struct FxElemVisStateSample
{
    FxElemVisualState base;
    FxElemVisualState amplitude;
};
RUNTIME_SIZE(FxElemVisStateSample, 0x30, 0x30);

struct FxElemVelStateInFrame
{
    FxElemVec3Range velocity;
    FxElemVec3Range totalDelta;
};
RUNTIME_SIZE(FxElemVelStateInFrame, 0x30, 0x30);

struct FxElemVelStateSample
{
    FxElemVelStateInFrame local;
    FxElemVelStateInFrame world;
};
RUNTIME_SIZE(FxElemVelStateSample, 0x60, 0x60);

union FxEffectDefRef
{
    const FxEffectDef *handle;
    const char *name;
};
RUNTIME_SIZE(FxEffectDefRef, 0x4, 0x8);

union FxElemVisuals
{
    const void *anonymous;
    Material *material;
    XModel *model;
    FxEffectDefRef effectDef;
    const char *soundName;

    FxElemVisuals() = default;
    FxElemVisuals(Material *const value) : material(value) {}
};
RUNTIME_SIZE(FxElemVisuals, 0x4, 0x8);

struct FxElemMarkVisuals
{
    Material *materials[2];
};
RUNTIME_SIZE(FxElemMarkVisuals, 0x8, 0x10);

union FxElemDefVisuals
{
    FxElemMarkVisuals *markArray;
    FxElemVisuals *array;
    FxElemVisuals instance;
};
RUNTIME_SIZE(FxElemDefVisuals, 0x4, 0x8);

struct FxTrailVertex
{
    float pos[2];
    float normal[2];
    float texCoord;
};
RUNTIME_SIZE(FxTrailVertex, 0x14, 0x14);

struct FxTrailDef
{
    std::int32_t scrollTimeMsec;
    std::int32_t repeatDist;
    std::int32_t splitDist;
    std::int32_t vertCount;
    FxTrailVertex *verts;
    std::int32_t indCount;
    std::uint16_t *inds;
};
RUNTIME_SIZE(FxTrailDef, 0x1C, 0x28);
RUNTIME_OFFSET(FxTrailDef, scrollTimeMsec, 0x0, 0x0);
RUNTIME_OFFSET(FxTrailDef, repeatDist, 0x4, 0x4);
RUNTIME_OFFSET(FxTrailDef, splitDist, 0x8, 0x8);

struct FxElemDef
{
    std::int32_t flags;
    FxSpawnDef spawn;
    FxFloatRange spawnRange;
    FxFloatRange fadeInRange;
    FxFloatRange fadeOutRange;
    float spawnFrustumCullRadius;
    FxIntRange spawnDelayMsec;
    FxIntRange lifeSpanMsec;
    FxFloatRange spawnOrigin[3];
    FxFloatRange spawnOffsetRadius;
    FxFloatRange spawnOffsetHeight;
    FxFloatRange spawnAngles[3];
    FxFloatRange angularVelocity[3];
    FxFloatRange initialRotation;
    FxFloatRange gravity;
    FxFloatRange reflectionFactor;
    FxElemAtlas atlas;
    std::uint8_t elemType;
    std::uint8_t visualCount;
    std::uint8_t velIntervalCount;
    std::uint8_t visStateIntervalCount;
    FxElemVelStateSample *velSamples;
    FxElemVisStateSample *visSamples;
    FxElemDefVisuals visuals;
    float collMins[3];
    float collMaxs[3];
    FxEffectDefRef effectOnImpact;
    FxEffectDefRef effectOnDeath;
    FxEffectDefRef effectEmitted;
    FxFloatRange emitDist;
    FxFloatRange emitDistVariance;
    FxTrailDef *trailDef;
    std::uint8_t sortOrder;
    std::uint8_t lightingFrac;
    std::uint8_t useItemClip;
    std::uint8_t unused[1];
};
RUNTIME_SIZE(FxElemDef, 0xFC, 0x120);
RUNTIME_OFFSET(FxElemDef, elemType, 0xB0, 0xB0);
RUNTIME_OFFSET(FxElemDef, velSamples, 0xB4, 0xB8);
RUNTIME_OFFSET(FxElemDef, visSamples, 0xB8, 0xC0);
RUNTIME_OFFSET(FxElemDef, visuals, 0xBC, 0xC8);
RUNTIME_OFFSET(FxElemDef, effectOnImpact, 0xD8, 0xE8);
RUNTIME_OFFSET(FxElemDef, effectOnDeath, 0xDC, 0xF0);
RUNTIME_OFFSET(FxElemDef, effectEmitted, 0xE0, 0xF8);
RUNTIME_OFFSET(FxElemDef, trailDef, 0xF4, 0x110);

static_assert(alignof(FxEffectDef) == (KISAK_ARCH_64BIT ? 8u : 4u));
static_assert(alignof(FxElemDef) == FX_ELEM_DEF_RUNTIME_ALIGNMENT);
static_assert(std::is_standard_layout_v<FxEffectDef>);
static_assert(std::is_standard_layout_v<FxElemDef>);
static_assert(std::is_standard_layout_v<FxTrailDef>);
static_assert(std::is_trivially_copyable_v<FxEffectDef>);
static_assert(std::is_trivially_copyable_v<FxElemDef>);
static_assert(std::is_trivially_copyable_v<FxElemVisStateSample>);
static_assert(std::is_trivially_copyable_v<FxElemVelStateSample>);
static_assert(std::is_trivially_copyable_v<FxTrailDef>);
static_assert(std::is_trivially_copyable_v<FxTrailVertex>);
static_assert(std::is_nothrow_default_constructible_v<FxEffectDef>);
static_assert(std::is_nothrow_default_constructible_v<FxElemDef>);
static_assert(std::is_nothrow_default_constructible_v<FxElemVisuals>);
static_assert(std::is_nothrow_default_constructible_v<FxElemMarkVisuals>);
static_assert(std::is_nothrow_default_constructible_v<FxTrailDef>);

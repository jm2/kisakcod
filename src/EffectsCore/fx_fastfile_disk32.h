#pragma once

#include <database/db_disk32.h>

#include <universal/kisak_abi.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fx::fastfile
{
constexpr std::size_t kImpactSurfaceCount = 12;
constexpr std::size_t kImpactNonFleshEffectCount = 29;
constexpr std::size_t kImpactFleshEffectCount = 4;

// The discriminator values are part of the fast-file ABI.  Keep this enum
// independent of the renderer's native enum so a disk byte can never acquire
// native enum semantics before validation.
enum class FxElemTypeDisk32 : std::uint8_t
{
    SpriteBillboard = 0,
    SpriteOriented = 1,
    Tail = 2,
    Trail = 3,
    Cloud = 4,
    Model = 5,
    OmniLight = 6,
    SpotLight = 7,
    Sound = 8,
    Decal = 9,
    Runner = 10,
    Count = 11,
};

struct FxFloatRangeDisk32 final
{
    float base;
    float amplitude;
};

struct FxIntRangeDisk32 final
{
    std::int32_t base;
    std::int32_t amplitude;
};

// The native record uses a union of looping and one-shot views.  The disk
// mirror deliberately stores the two words without a C++ union: which meaning
// is active is decided only after the owning effect has been validated.
struct FxSpawnDefDisk32 final
{
    std::int32_t intervalMsecOrCountBase;
    std::int32_t loopCountOrCountAmplitude;
};

struct FxElemAtlasDisk32 final
{
    std::uint8_t behavior;
    std::uint8_t index;
    std::uint8_t fps;
    std::uint8_t loopCount;
    std::uint8_t colIndexBits;
    std::uint8_t rowIndexBits;
    std::int16_t entryCount;
};

struct FxElemVec3RangeDisk32 final
{
    float base[3];
    float amplitude[3];
};

struct FxElemVisualStateDisk32 final
{
    std::uint8_t color[4];
    float rotationDelta;
    float rotationTotal;
    float size[2];
    float scale;
};

struct FxElemVisStateSampleDisk32 final
{
    FxElemVisualStateDisk32 base;
    FxElemVisualStateDisk32 amplitude;
};

struct FxElemVelStateInFrameDisk32 final
{
    FxElemVec3RangeDisk32 velocity;
    FxElemVec3RangeDisk32 totalDelta;
};

struct FxElemVelStateSampleDisk32 final
{
    FxElemVelStateInFrameDisk32 local;
    FxElemVelStateInFrameDisk32 world;
};

// These three records mirror native unions whose active members have different
// pointer grammars.  They intentionally expose only the canonical raw token;
// callers must select the grammar from the validated element discriminator and
// visual count, never by reading an inactive C++ union member.
struct FxEffectDefRefDisk32 final
{
    disk32::PointerToken token;
};

struct FxElemVisualsDisk32 final
{
    disk32::PointerToken token;
};

struct FxElemDefVisualsDisk32 final
{
    disk32::PointerToken token;
};

// Asset handles use the -1/-2/alias grammar, unlike FxEffectDefRefDisk32's
// XString/name grammar.  A distinct wrapper prevents accidental interchange.
struct FxEffectDefHandleDisk32 final
{
    disk32::PointerToken token;
};

struct FxElemMarkVisualsDisk32 final
{
    disk32::Ptr32<void> materials[2];
};

struct FxTrailVertexDisk32 final
{
    float pos[2];
    float normal[2];
    float texCoord;
};

struct FxTrailDefDisk32 final
{
    std::int32_t scrollTimeMsec;
    std::int32_t repeatDist;
    std::int32_t splitDist;
    std::int32_t vertCount;
    disk32::Ptr32<FxTrailVertexDisk32> verts;
    std::int32_t indCount;
    disk32::Ptr32<std::uint16_t> inds;
};

struct FxElemDefDisk32 final
{
    std::int32_t flags;
    FxSpawnDefDisk32 spawn;
    FxFloatRangeDisk32 spawnRange;
    FxFloatRangeDisk32 fadeInRange;
    FxFloatRangeDisk32 fadeOutRange;
    float spawnFrustumCullRadius;
    FxIntRangeDisk32 spawnDelayMsec;
    FxIntRangeDisk32 lifeSpanMsec;
    FxFloatRangeDisk32 spawnOrigin[3];
    FxFloatRangeDisk32 spawnOffsetRadius;
    FxFloatRangeDisk32 spawnOffsetHeight;
    FxFloatRangeDisk32 spawnAngles[3];
    FxFloatRangeDisk32 angularVelocity[3];
    FxFloatRangeDisk32 initialRotation;
    FxFloatRangeDisk32 gravity;
    FxFloatRangeDisk32 reflectionFactor;
    FxElemAtlasDisk32 atlas;
    FxElemTypeDisk32 elemType;
    std::uint8_t visualCount;
    std::uint8_t velIntervalCount;
    std::uint8_t visStateIntervalCount;
    disk32::Ptr32<FxElemVelStateSampleDisk32> velSamples;
    disk32::Ptr32<FxElemVisStateSampleDisk32> visSamples;
    FxElemDefVisualsDisk32 visuals;
    float collMins[3];
    float collMaxs[3];
    FxEffectDefRefDisk32 effectOnImpact;
    FxEffectDefRefDisk32 effectOnDeath;
    FxEffectDefRefDisk32 effectEmitted;
    FxFloatRangeDisk32 emitDist;
    FxFloatRangeDisk32 emitDistVariance;
    disk32::Ptr32<FxTrailDefDisk32> trailDef;
    std::uint8_t sortOrder;
    std::uint8_t lightingFrac;
    std::uint8_t useItemClip;
    std::uint8_t unused[1];
};

struct FxEffectDefDisk32 final
{
    disk32::Ptr32<const char> name;
    std::int32_t flags;
    std::int32_t totalSize;
    std::int32_t msecLoopingLife;
    std::int32_t elemDefCountLooping;
    std::int32_t elemDefCountOneShot;
    std::int32_t elemDefCountEmission;
    disk32::Ptr32<FxElemDefDisk32> elemDefs;
};

struct FxImpactEntryDisk32 final
{
    FxEffectDefHandleDisk32 nonflesh[kImpactNonFleshEffectCount];
    FxEffectDefHandleDisk32 flesh[kImpactFleshEffectCount];
};

struct FxImpactTableDisk32 final
{
    disk32::Ptr32<const char> name;
    disk32::Ptr32<FxImpactEntryDisk32> table;
};

static_assert(std::endian::native == std::endian::little,
              "FX fast-file Disk32 records require a little-endian target");
ONDISK_SIZE(float, 0x04);
static_assert(std::numeric_limits<float>::is_iec559,
              "FX fast-file Disk32 records require IEC 60559 binary32 float");
ONDISK_SIZE(FxElemTypeDisk32, 0x01);
static_assert(alignof(FxElemTypeDisk32) == 1);
static_assert(std::is_trivial_v<FxElemTypeDisk32>);
static_assert(std::is_standard_layout_v<FxElemTypeDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemTypeDisk32>);

ONDISK_SIZE(FxFloatRangeDisk32, 0x08);
ONDISK_OFFSET(FxFloatRangeDisk32, base, 0x00);
ONDISK_OFFSET(FxFloatRangeDisk32, amplitude, 0x04);
static_assert(alignof(FxFloatRangeDisk32) == 4);
static_assert(std::is_trivial_v<FxFloatRangeDisk32>);
static_assert(std::is_standard_layout_v<FxFloatRangeDisk32>);
static_assert(std::is_trivially_copyable_v<FxFloatRangeDisk32>);

ONDISK_SIZE(FxIntRangeDisk32, 0x08);
ONDISK_OFFSET(FxIntRangeDisk32, base, 0x00);
ONDISK_OFFSET(FxIntRangeDisk32, amplitude, 0x04);
static_assert(alignof(FxIntRangeDisk32) == 4);
static_assert(std::is_trivial_v<FxIntRangeDisk32>);
static_assert(std::is_standard_layout_v<FxIntRangeDisk32>);
static_assert(std::is_trivially_copyable_v<FxIntRangeDisk32>);

ONDISK_SIZE(FxSpawnDefDisk32, 0x08);
ONDISK_OFFSET(FxSpawnDefDisk32, intervalMsecOrCountBase, 0x00);
ONDISK_OFFSET(FxSpawnDefDisk32, loopCountOrCountAmplitude, 0x04);
static_assert(alignof(FxSpawnDefDisk32) == 4);
static_assert(std::is_trivial_v<FxSpawnDefDisk32>);
static_assert(std::is_standard_layout_v<FxSpawnDefDisk32>);
static_assert(std::is_trivially_copyable_v<FxSpawnDefDisk32>);

ONDISK_SIZE(FxElemAtlasDisk32, 0x08);
ONDISK_OFFSET(FxElemAtlasDisk32, behavior, 0x00);
ONDISK_OFFSET(FxElemAtlasDisk32, index, 0x01);
ONDISK_OFFSET(FxElemAtlasDisk32, fps, 0x02);
ONDISK_OFFSET(FxElemAtlasDisk32, loopCount, 0x03);
ONDISK_OFFSET(FxElemAtlasDisk32, colIndexBits, 0x04);
ONDISK_OFFSET(FxElemAtlasDisk32, rowIndexBits, 0x05);
ONDISK_OFFSET(FxElemAtlasDisk32, entryCount, 0x06);
static_assert(alignof(FxElemAtlasDisk32) == 2);
static_assert(std::is_trivial_v<FxElemAtlasDisk32>);
static_assert(std::is_standard_layout_v<FxElemAtlasDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemAtlasDisk32>);

ONDISK_SIZE(FxElemVec3RangeDisk32, 0x18);
ONDISK_OFFSET(FxElemVec3RangeDisk32, base, 0x00);
ONDISK_OFFSET(FxElemVec3RangeDisk32, amplitude, 0x0C);
static_assert(alignof(FxElemVec3RangeDisk32) == 4);
static_assert(std::is_trivial_v<FxElemVec3RangeDisk32>);
static_assert(std::is_standard_layout_v<FxElemVec3RangeDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemVec3RangeDisk32>);

ONDISK_SIZE(FxElemVisualStateDisk32, 0x18);
ONDISK_OFFSET(FxElemVisualStateDisk32, color, 0x00);
ONDISK_OFFSET(FxElemVisualStateDisk32, rotationDelta, 0x04);
ONDISK_OFFSET(FxElemVisualStateDisk32, rotationTotal, 0x08);
ONDISK_OFFSET(FxElemVisualStateDisk32, size, 0x0C);
ONDISK_OFFSET(FxElemVisualStateDisk32, scale, 0x14);
static_assert(alignof(FxElemVisualStateDisk32) == 4);
static_assert(std::is_trivial_v<FxElemVisualStateDisk32>);
static_assert(std::is_standard_layout_v<FxElemVisualStateDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemVisualStateDisk32>);

ONDISK_SIZE(FxElemVisStateSampleDisk32, 0x30);
ONDISK_OFFSET(FxElemVisStateSampleDisk32, base, 0x00);
ONDISK_OFFSET(FxElemVisStateSampleDisk32, amplitude, 0x18);
static_assert(alignof(FxElemVisStateSampleDisk32) == 4);
static_assert(std::is_trivial_v<FxElemVisStateSampleDisk32>);
static_assert(std::is_standard_layout_v<FxElemVisStateSampleDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemVisStateSampleDisk32>);

ONDISK_SIZE(FxElemVelStateInFrameDisk32, 0x30);
ONDISK_OFFSET(FxElemVelStateInFrameDisk32, velocity, 0x00);
ONDISK_OFFSET(FxElemVelStateInFrameDisk32, totalDelta, 0x18);
static_assert(alignof(FxElemVelStateInFrameDisk32) == 4);
static_assert(std::is_trivial_v<FxElemVelStateInFrameDisk32>);
static_assert(std::is_standard_layout_v<FxElemVelStateInFrameDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemVelStateInFrameDisk32>);

ONDISK_SIZE(FxElemVelStateSampleDisk32, 0x60);
ONDISK_OFFSET(FxElemVelStateSampleDisk32, local, 0x00);
ONDISK_OFFSET(FxElemVelStateSampleDisk32, world, 0x30);
static_assert(alignof(FxElemVelStateSampleDisk32) == 4);
static_assert(std::is_trivial_v<FxElemVelStateSampleDisk32>);
static_assert(std::is_standard_layout_v<FxElemVelStateSampleDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemVelStateSampleDisk32>);

ONDISK_SIZE(FxEffectDefRefDisk32, 0x04);
ONDISK_OFFSET(FxEffectDefRefDisk32, token, 0x00);
static_assert(alignof(FxEffectDefRefDisk32) == 4);
static_assert(std::is_trivial_v<FxEffectDefRefDisk32>);
static_assert(std::is_standard_layout_v<FxEffectDefRefDisk32>);
static_assert(std::is_trivially_copyable_v<FxEffectDefRefDisk32>);

ONDISK_SIZE(FxElemVisualsDisk32, 0x04);
ONDISK_OFFSET(FxElemVisualsDisk32, token, 0x00);
static_assert(alignof(FxElemVisualsDisk32) == 4);
static_assert(std::is_trivial_v<FxElemVisualsDisk32>);
static_assert(std::is_standard_layout_v<FxElemVisualsDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemVisualsDisk32>);

ONDISK_SIZE(FxElemDefVisualsDisk32, 0x04);
ONDISK_OFFSET(FxElemDefVisualsDisk32, token, 0x00);
static_assert(alignof(FxElemDefVisualsDisk32) == 4);
static_assert(std::is_trivial_v<FxElemDefVisualsDisk32>);
static_assert(std::is_standard_layout_v<FxElemDefVisualsDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemDefVisualsDisk32>);

ONDISK_SIZE(FxEffectDefHandleDisk32, 0x04);
ONDISK_OFFSET(FxEffectDefHandleDisk32, token, 0x00);
static_assert(alignof(FxEffectDefHandleDisk32) == 4);
static_assert(std::is_trivial_v<FxEffectDefHandleDisk32>);
static_assert(std::is_standard_layout_v<FxEffectDefHandleDisk32>);
static_assert(std::is_trivially_copyable_v<FxEffectDefHandleDisk32>);

ONDISK_SIZE(FxElemMarkVisualsDisk32, 0x08);
ONDISK_OFFSET(FxElemMarkVisualsDisk32, materials, 0x00);
static_assert(alignof(FxElemMarkVisualsDisk32) == 4);
static_assert(std::is_trivial_v<FxElemMarkVisualsDisk32>);
static_assert(std::is_standard_layout_v<FxElemMarkVisualsDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemMarkVisualsDisk32>);

ONDISK_SIZE(FxTrailVertexDisk32, 0x14);
ONDISK_OFFSET(FxTrailVertexDisk32, pos, 0x00);
ONDISK_OFFSET(FxTrailVertexDisk32, normal, 0x08);
ONDISK_OFFSET(FxTrailVertexDisk32, texCoord, 0x10);
static_assert(alignof(FxTrailVertexDisk32) == 4);
static_assert(std::is_trivial_v<FxTrailVertexDisk32>);
static_assert(std::is_standard_layout_v<FxTrailVertexDisk32>);
static_assert(std::is_trivially_copyable_v<FxTrailVertexDisk32>);

ONDISK_SIZE(FxTrailDefDisk32, 0x1C);
ONDISK_OFFSET(FxTrailDefDisk32, scrollTimeMsec, 0x00);
ONDISK_OFFSET(FxTrailDefDisk32, repeatDist, 0x04);
ONDISK_OFFSET(FxTrailDefDisk32, splitDist, 0x08);
ONDISK_OFFSET(FxTrailDefDisk32, vertCount, 0x0C);
ONDISK_OFFSET(FxTrailDefDisk32, verts, 0x10);
ONDISK_OFFSET(FxTrailDefDisk32, indCount, 0x14);
ONDISK_OFFSET(FxTrailDefDisk32, inds, 0x18);
static_assert(alignof(FxTrailDefDisk32) == 4);
static_assert(std::is_trivial_v<FxTrailDefDisk32>);
static_assert(std::is_standard_layout_v<FxTrailDefDisk32>);
static_assert(std::is_trivially_copyable_v<FxTrailDefDisk32>);

ONDISK_SIZE(FxElemDefDisk32, 0xFC);
ONDISK_OFFSET(FxElemDefDisk32, flags, 0x00);
ONDISK_OFFSET(FxElemDefDisk32, spawn, 0x04);
ONDISK_OFFSET(FxElemDefDisk32, spawnRange, 0x0C);
ONDISK_OFFSET(FxElemDefDisk32, fadeInRange, 0x14);
ONDISK_OFFSET(FxElemDefDisk32, fadeOutRange, 0x1C);
ONDISK_OFFSET(FxElemDefDisk32, spawnFrustumCullRadius, 0x24);
ONDISK_OFFSET(FxElemDefDisk32, spawnDelayMsec, 0x28);
ONDISK_OFFSET(FxElemDefDisk32, lifeSpanMsec, 0x30);
ONDISK_OFFSET(FxElemDefDisk32, spawnOrigin, 0x38);
ONDISK_OFFSET(FxElemDefDisk32, spawnOffsetRadius, 0x50);
ONDISK_OFFSET(FxElemDefDisk32, spawnOffsetHeight, 0x58);
ONDISK_OFFSET(FxElemDefDisk32, spawnAngles, 0x60);
ONDISK_OFFSET(FxElemDefDisk32, angularVelocity, 0x78);
ONDISK_OFFSET(FxElemDefDisk32, initialRotation, 0x90);
ONDISK_OFFSET(FxElemDefDisk32, gravity, 0x98);
ONDISK_OFFSET(FxElemDefDisk32, reflectionFactor, 0xA0);
ONDISK_OFFSET(FxElemDefDisk32, atlas, 0xA8);
ONDISK_OFFSET(FxElemDefDisk32, elemType, 0xB0);
ONDISK_OFFSET(FxElemDefDisk32, visualCount, 0xB1);
ONDISK_OFFSET(FxElemDefDisk32, velIntervalCount, 0xB2);
ONDISK_OFFSET(FxElemDefDisk32, visStateIntervalCount, 0xB3);
ONDISK_OFFSET(FxElemDefDisk32, velSamples, 0xB4);
ONDISK_OFFSET(FxElemDefDisk32, visSamples, 0xB8);
ONDISK_OFFSET(FxElemDefDisk32, visuals, 0xBC);
ONDISK_OFFSET(FxElemDefDisk32, collMins, 0xC0);
ONDISK_OFFSET(FxElemDefDisk32, collMaxs, 0xCC);
ONDISK_OFFSET(FxElemDefDisk32, effectOnImpact, 0xD8);
ONDISK_OFFSET(FxElemDefDisk32, effectOnDeath, 0xDC);
ONDISK_OFFSET(FxElemDefDisk32, effectEmitted, 0xE0);
ONDISK_OFFSET(FxElemDefDisk32, emitDist, 0xE4);
ONDISK_OFFSET(FxElemDefDisk32, emitDistVariance, 0xEC);
ONDISK_OFFSET(FxElemDefDisk32, trailDef, 0xF4);
ONDISK_OFFSET(FxElemDefDisk32, sortOrder, 0xF8);
ONDISK_OFFSET(FxElemDefDisk32, lightingFrac, 0xF9);
ONDISK_OFFSET(FxElemDefDisk32, useItemClip, 0xFA);
ONDISK_OFFSET(FxElemDefDisk32, unused, 0xFB);
static_assert(alignof(FxElemDefDisk32) == 4);
static_assert(std::is_trivial_v<FxElemDefDisk32>);
static_assert(std::is_standard_layout_v<FxElemDefDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemDefDisk32>);

ONDISK_SIZE(FxEffectDefDisk32, 0x20);
ONDISK_OFFSET(FxEffectDefDisk32, name, 0x00);
ONDISK_OFFSET(FxEffectDefDisk32, flags, 0x04);
ONDISK_OFFSET(FxEffectDefDisk32, totalSize, 0x08);
ONDISK_OFFSET(FxEffectDefDisk32, msecLoopingLife, 0x0C);
ONDISK_OFFSET(FxEffectDefDisk32, elemDefCountLooping, 0x10);
ONDISK_OFFSET(FxEffectDefDisk32, elemDefCountOneShot, 0x14);
ONDISK_OFFSET(FxEffectDefDisk32, elemDefCountEmission, 0x18);
ONDISK_OFFSET(FxEffectDefDisk32, elemDefs, 0x1C);
static_assert(alignof(FxEffectDefDisk32) == 4);
static_assert(std::is_trivial_v<FxEffectDefDisk32>);
static_assert(std::is_standard_layout_v<FxEffectDefDisk32>);
static_assert(std::is_trivially_copyable_v<FxEffectDefDisk32>);

ONDISK_SIZE(FxImpactEntryDisk32, 0x84);
ONDISK_OFFSET(FxImpactEntryDisk32, nonflesh, 0x00);
ONDISK_OFFSET(FxImpactEntryDisk32, flesh, 0x74);
static_assert(alignof(FxImpactEntryDisk32) == 4);
static_assert(std::is_trivial_v<FxImpactEntryDisk32>);
static_assert(std::is_standard_layout_v<FxImpactEntryDisk32>);
static_assert(std::is_trivially_copyable_v<FxImpactEntryDisk32>);

ONDISK_SIZE(FxImpactTableDisk32, 0x08);
ONDISK_OFFSET(FxImpactTableDisk32, name, 0x00);
ONDISK_OFFSET(FxImpactTableDisk32, table, 0x04);
static_assert(alignof(FxImpactTableDisk32) == 4);
static_assert(std::is_trivial_v<FxImpactTableDisk32>);
static_assert(std::is_standard_layout_v<FxImpactTableDisk32>);
static_assert(std::is_trivially_copyable_v<FxImpactTableDisk32>);
} // namespace fx::fastfile

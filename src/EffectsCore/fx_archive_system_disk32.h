#pragma once

#include <EffectsCore/fx_archive_disk32.h>

#include <universal/kisak_abi.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fx::archive
{
// A process address captured in the legacy x86 archive image.  It is kept
// distinct from both effect-definition keys and fast-file pointer tokens: the
// system decoder may compare its numeric value, but must never form a native
// pointer from it.
struct ArchiveAddress32 final
{
    std::uint32_t value = 0;

    constexpr ArchiveAddress32() noexcept = default;
    explicit constexpr ArchiveAddress32(const std::uint32_t address) noexcept
        : value(address)
    {
    }
};

struct FxCameraDisk32 final
{
    float origin[3];
    std::int32_t isValid;
    float frustum[6][4];
    float axis[3][3];
    std::uint32_t frustumPlaneCount;
    float viewOffset[3];
    std::uint32_t pad[3];
};

struct FxSpriteInfoDisk32 final
{
    ArchiveAddress32 indices;
    std::uint32_t indexCount;
    ArchiveAddress32 material;
    ArchiveAddress32 name;
};

struct FxSystemDisk32 final
{
    FxCameraDisk32 camera;
    FxCameraDisk32 cameraPrev;
    FxSpriteInfoDisk32 sprite;
    ArchiveAddress32 effects;
    ArchiveAddress32 elems;
    ArchiveAddress32 trails;
    ArchiveAddress32 trailElems;
    ArchiveAddress32 deferredElems;
    std::int32_t firstFreeElem;
    std::int32_t firstFreeTrailElem;
    std::int32_t firstFreeTrail;
    std::int32_t deferredElemCount;
    std::int32_t activeElemCount;
    std::int32_t activeTrailElemCount;
    std::int32_t activeTrailCount;
    std::int32_t gfxCloudCount;
    ArchiveAddress32 visState;
    ArchiveAddress32 visStateBufferRead;
    ArchiveAddress32 visStateBufferWrite;
    std::int32_t firstActiveEffect;
    std::int32_t firstNewEffect;
    std::int32_t firstFreeEffect;
    std::uint16_t allEffectHandles[MAX_EFFECTS];
    std::int32_t activeSpotLightEffectCount;
    std::int32_t activeSpotLightElemCount;
    std::uint16_t activeSpotLightEffectHandle;
    std::uint16_t activeSpotLightElemHandle;
    std::int16_t activeSpotLightBoltDobj;
    std::uint16_t spotLightPadding;
    std::int32_t iteratorCount;
    std::int32_t msecNow;
    std::int32_t msecDraw;
    std::int32_t frameCount;
    std::uint8_t isInitialized;
    std::uint8_t needsGarbageCollection;
    std::uint8_t isArchiving;
    std::uint8_t localClientNum;
    std::uint32_t restartList[32];
};

// Address-independent facts derived while validating one system image.  The
// active array is indexed by physical effect slot, not by ring position.
struct FxSystemDisk32Metadata final
{
    std::uint8_t readVisibilitySelector;
    std::uint8_t writeVisibilitySelector;
    std::uint8_t activeEffectSlots[MAX_EFFECTS];
};

static_assert(
    std::endian::native == std::endian::little,
    "FX Disk32 records require a little-endian target");
ONDISK_SIZE(float, 0x04);
static_assert(
    std::numeric_limits<float>::is_iec559,
    "FX Disk32 records require IEC 60559 binary32 float");

ONDISK_SIZE(ArchiveAddress32, 0x04);
ONDISK_OFFSET(ArchiveAddress32, value, 0x00);
static_assert(alignof(ArchiveAddress32) == 4);
static_assert(std::is_standard_layout_v<ArchiveAddress32>);
static_assert(std::is_trivially_copyable_v<ArchiveAddress32>);

ONDISK_SIZE(FxCameraDisk32, 0xB0);
ONDISK_OFFSET(FxCameraDisk32, origin, 0x00);
ONDISK_OFFSET(FxCameraDisk32, isValid, 0x0C);
ONDISK_OFFSET(FxCameraDisk32, frustum, 0x10);
ONDISK_OFFSET(FxCameraDisk32, axis, 0x70);
ONDISK_OFFSET(FxCameraDisk32, frustumPlaneCount, 0x94);
ONDISK_OFFSET(FxCameraDisk32, viewOffset, 0x98);
ONDISK_OFFSET(FxCameraDisk32, pad, 0xA4);
static_assert(alignof(FxCameraDisk32) == 4);
static_assert(std::is_standard_layout_v<FxCameraDisk32>);
static_assert(std::is_trivially_copyable_v<FxCameraDisk32>);

ONDISK_SIZE(FxSpriteInfoDisk32, 0x10);
ONDISK_OFFSET(FxSpriteInfoDisk32, indices, 0x00);
ONDISK_OFFSET(FxSpriteInfoDisk32, indexCount, 0x04);
ONDISK_OFFSET(FxSpriteInfoDisk32, material, 0x08);
ONDISK_OFFSET(FxSpriteInfoDisk32, name, 0x0C);
static_assert(alignof(FxSpriteInfoDisk32) == 4);
static_assert(std::is_standard_layout_v<FxSpriteInfoDisk32>);
static_assert(std::is_trivially_copyable_v<FxSpriteInfoDisk32>);

ONDISK_SIZE(FxSystemDisk32, 0xA60);
ONDISK_OFFSET(FxSystemDisk32, camera, 0x000);
ONDISK_OFFSET(FxSystemDisk32, cameraPrev, 0x0B0);
ONDISK_OFFSET(FxSystemDisk32, sprite, 0x160);
ONDISK_OFFSET(FxSystemDisk32, effects, 0x170);
ONDISK_OFFSET(FxSystemDisk32, elems, 0x174);
ONDISK_OFFSET(FxSystemDisk32, trails, 0x178);
ONDISK_OFFSET(FxSystemDisk32, trailElems, 0x17C);
ONDISK_OFFSET(FxSystemDisk32, deferredElems, 0x180);
ONDISK_OFFSET(FxSystemDisk32, firstFreeElem, 0x184);
ONDISK_OFFSET(FxSystemDisk32, firstFreeTrailElem, 0x188);
ONDISK_OFFSET(FxSystemDisk32, firstFreeTrail, 0x18C);
ONDISK_OFFSET(FxSystemDisk32, deferredElemCount, 0x190);
ONDISK_OFFSET(FxSystemDisk32, activeElemCount, 0x194);
ONDISK_OFFSET(FxSystemDisk32, activeTrailElemCount, 0x198);
ONDISK_OFFSET(FxSystemDisk32, activeTrailCount, 0x19C);
ONDISK_OFFSET(FxSystemDisk32, gfxCloudCount, 0x1A0);
ONDISK_OFFSET(FxSystemDisk32, visState, 0x1A4);
ONDISK_OFFSET(FxSystemDisk32, visStateBufferRead, 0x1A8);
ONDISK_OFFSET(FxSystemDisk32, visStateBufferWrite, 0x1AC);
ONDISK_OFFSET(FxSystemDisk32, firstActiveEffect, 0x1B0);
ONDISK_OFFSET(FxSystemDisk32, firstNewEffect, 0x1B4);
ONDISK_OFFSET(FxSystemDisk32, firstFreeEffect, 0x1B8);
ONDISK_OFFSET(FxSystemDisk32, allEffectHandles, 0x1BC);
ONDISK_OFFSET(FxSystemDisk32, activeSpotLightEffectCount, 0x9BC);
ONDISK_OFFSET(FxSystemDisk32, activeSpotLightElemCount, 0x9C0);
ONDISK_OFFSET(FxSystemDisk32, activeSpotLightEffectHandle, 0x9C4);
ONDISK_OFFSET(FxSystemDisk32, activeSpotLightElemHandle, 0x9C6);
ONDISK_OFFSET(FxSystemDisk32, activeSpotLightBoltDobj, 0x9C8);
ONDISK_OFFSET(FxSystemDisk32, spotLightPadding, 0x9CA);
ONDISK_OFFSET(FxSystemDisk32, iteratorCount, 0x9CC);
ONDISK_OFFSET(FxSystemDisk32, msecNow, 0x9D0);
ONDISK_OFFSET(FxSystemDisk32, msecDraw, 0x9D4);
ONDISK_OFFSET(FxSystemDisk32, frameCount, 0x9D8);
ONDISK_OFFSET(FxSystemDisk32, isInitialized, 0x9DC);
ONDISK_OFFSET(FxSystemDisk32, needsGarbageCollection, 0x9DD);
ONDISK_OFFSET(FxSystemDisk32, isArchiving, 0x9DE);
ONDISK_OFFSET(FxSystemDisk32, localClientNum, 0x9DF);
ONDISK_OFFSET(FxSystemDisk32, restartList, 0x9E0);
static_assert(alignof(FxSystemDisk32) == 4);
static_assert(std::is_standard_layout_v<FxSystemDisk32>);
static_assert(std::is_trivially_copyable_v<FxSystemDisk32>);

RUNTIME_SIZE(
    FxSystemDisk32Metadata, MAX_EFFECTS + 2, MAX_EFFECTS + 2);
static_assert(alignof(FxSystemDisk32Metadata) == 1);
static_assert(std::is_trivial_v<FxSystemDisk32Metadata>);
static_assert(std::is_standard_layout_v<FxSystemDisk32Metadata>);
static_assert(std::is_trivially_copyable_v<FxSystemDisk32Metadata>);

// Validates and converts one complete legacy system image without consulting
// or publishing a buffer image.  Archived address words are checked only as a
// numeric topology and are never converted to pointers.  Consequently every
// native pool, visibility, and sprite pointer in outSystem is null; callers
// link validated native buffers in the later full-image transaction.  Handles
// and bolt state whose spotlight count is zero are likewise canonicalized to
// their inert runtime sentinels.  Metadata is staging information only until
// that later transaction validates the complete buffer ownership graph.  The
// spotlight states are canonicalized as follows: (0,0) publishes two invalid
// handles and bolt -1; (1,0) remaps the active effect, publishes an invalid
// elem handle, and retains the active effect's bolt; (1,1) additionally
// validates and copies the width-stable elem handle.
//
// Both outputs are committed only after every check succeeds.  Null outputs or
// malformed input return false and leave every caller-owned byte unchanged.
[[nodiscard]] bool TryUnpackFxSystemDisk32(
    const FxSystemDisk32 &source,
    FxSystem *outSystem,
    FxSystemDisk32Metadata *outMetadata) noexcept;
} // namespace fx::archive

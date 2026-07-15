#pragma once

#include <EffectsCore/fx_archive_key.h>
#include <EffectsCore/fx_runtime.h>

#include <universal/kisak_abi.h>

#include <cstdint>
#include <type_traits>

namespace fx::archive
{
// Pointer-free mirrors of the two fixed-layout records nested in the legacy
// FxSystemBuffers effect array.  The definition identity is an opaque archive
// key, not a pointer token, and the bolt/sort value is one raw wire word rather
// than a compiler-dependent C++ bitfield.
struct FxSpatialFrameDisk32 final
{
    float quat[4];
    float origin[3];
};

struct FxEffectDisk32 final
{
    EffectDefinitionKey32 definitionKey;
    std::int32_t status;
    std::uint16_t firstElemHandle[3];
    std::uint16_t firstSortedElemHandle;
    std::uint16_t firstTrailHandle;
    std::uint16_t randomSeed;
    std::uint16_t owner;
    std::uint16_t packedLighting;
    std::uint32_t boltAndSortOrder;
    std::int32_t frameCount;
    std::int32_t msecBegin;
    std::int32_t msecLastUpdate;
    FxSpatialFrameDisk32 frameAtSpawn;
    FxSpatialFrameDisk32 frameNow;
    FxSpatialFrameDisk32 framePrev;
    float distanceTraveled;
};

ONDISK_SIZE(FxSpatialFrameDisk32, 0x1C);
ONDISK_OFFSET(FxSpatialFrameDisk32, quat, 0x00);
ONDISK_OFFSET(FxSpatialFrameDisk32, origin, 0x10);
static_assert(alignof(FxSpatialFrameDisk32) == 4);
static_assert(std::is_standard_layout_v<FxSpatialFrameDisk32>);
static_assert(std::is_trivially_copyable_v<FxSpatialFrameDisk32>);

ONDISK_SIZE(FxEffectDisk32, 0x80);
ONDISK_OFFSET(FxEffectDisk32, definitionKey, 0x00);
ONDISK_OFFSET(FxEffectDisk32, status, 0x04);
ONDISK_OFFSET(FxEffectDisk32, firstElemHandle, 0x08);
ONDISK_OFFSET(FxEffectDisk32, firstSortedElemHandle, 0x0E);
ONDISK_OFFSET(FxEffectDisk32, firstTrailHandle, 0x10);
ONDISK_OFFSET(FxEffectDisk32, randomSeed, 0x12);
ONDISK_OFFSET(FxEffectDisk32, owner, 0x14);
ONDISK_OFFSET(FxEffectDisk32, packedLighting, 0x16);
ONDISK_OFFSET(FxEffectDisk32, boltAndSortOrder, 0x18);
ONDISK_OFFSET(FxEffectDisk32, frameCount, 0x1C);
ONDISK_OFFSET(FxEffectDisk32, msecBegin, 0x20);
ONDISK_OFFSET(FxEffectDisk32, msecLastUpdate, 0x24);
ONDISK_OFFSET(FxEffectDisk32, frameAtSpawn, 0x28);
ONDISK_OFFSET(FxEffectDisk32, frameNow, 0x44);
ONDISK_OFFSET(FxEffectDisk32, framePrev, 0x60);
ONDISK_OFFSET(FxEffectDisk32, distanceTraveled, 0x7C);
static_assert(alignof(FxEffectDisk32) == 4);
static_assert(std::is_standard_layout_v<FxEffectDisk32>);
static_assert(std::is_trivially_copyable_v<FxEffectDisk32>);

// Effect handles encode a byte offset divided by FxEffect::HANDLE_SCALE.  The
// legacy 0x80-byte record therefore has stride 32 while native64's 0x88-byte
// record has stride 34.  These converters accept only real in-pool handles;
// the 0xFFFF sentinel and every malformed value are rejected.  On failure the
// output is left unchanged.
[[nodiscard]] bool TryDecodeFxEffectHandleDisk32(
    std::uint16_t diskHandle,
    std::uint16_t *outNativeHandle) noexcept;

[[nodiscard]] bool TryEncodeFxEffectHandleDisk32(
    std::uint16_t nativeHandle,
    std::uint16_t *outDiskHandle) noexcept;

// Convert one effect record without reinterpreting its key as a pointer.
//
// Active records require a nonzero key and a definition pointer already
// resolved by the caller under its effect-table lease. Their owner handle is
// converted between the fixed Disk32 and native strides. Inactive records are
// intentionally inert: unpacking canonicalizes def to null and preserves the
// owner bits without assigning them handle semantics; packing writes the
// caller-supplied key and likewise preserves those inert owner bits. This
// retains canonical x86 byte equivalence without requiring stale inactive
// keys to exist in the definition table.
//
// All validation completes in local staging before either output is changed.
[[nodiscard]] bool TryUnpackFxEffectDisk32(
    const FxEffectDisk32 &source,
    bool isActive,
    const FxEffectDef *resolvedDefinition,
    FxEffect *outEffect) noexcept;

[[nodiscard]] bool TryPackFxEffectDisk32(
    const FxEffect &source,
    bool isActive,
    EffectDefinitionKey32 definitionKey,
    FxEffectDisk32 *outEffect) noexcept;
} // namespace fx::archive

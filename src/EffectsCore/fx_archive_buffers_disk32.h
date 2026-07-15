#pragma once

#include <EffectsCore/fx_archive_system_disk32.h>
#include <EffectsCore/fx_pool.h>

#include <universal/kisak_abi.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fx::archive
{
// Pointer-free semantic mirrors for records stored in the fixed legacy buffer
// image. FxElem's two native union-storage regions remain distinct opaque byte
// spans: interpreting them requires the resolved effect definition and belongs
// to the later full-graph conversion transaction.
struct FxElemDisk32 final
{
    std::uint8_t defIndex;
    std::uint8_t sequence;
    std::uint8_t atRestFraction;
    std::uint8_t emitResidual;
    std::uint16_t nextElemHandleInEffect;
    std::uint16_t prevElemHandleInEffect;
    std::int32_t msecBegin;
    float baseVel[3];
    std::uint8_t payload[0x0C];
    std::uint8_t value[0x04];
};

struct FxTrailDisk32 final
{
    std::uint16_t nextTrailHandle;
    std::uint16_t firstElemHandle;
    std::uint16_t lastElemHandle;
    std::uint8_t defIndex;
    std::uint8_t sequence;
};

struct FxTrailElemDisk32 final
{
    float origin[3];
    float spawnDist;
    std::int32_t msecBegin;
    std::uint16_t nextTrailElemHandle;
    std::int16_t baseVelZ;
    std::int8_t basis[2][3];
    std::uint8_t sequence;
    std::uint8_t unused;
};

struct FxVisBlockerDisk32 final
{
    float origin[3];
    std::uint16_t radius;
    std::uint16_t visibility;
};

struct FxVisStateDisk32 final
{
    FxVisBlockerDisk32 blocker[256];
    std::int32_t blockerCount;
    std::uint32_t pad[3];
};

// A pool slot may contain either an active semantic record or the first
// signed word of a free-list node.  Keeping the complete slot as aligned raw
// bytes avoids an overlapping-object lifetime and representation dependency
// while still preserving the legacy FxPool<T> stride and alignment.
struct alignas(4) FxElemPoolSlotDisk32 final
{
    std::uint8_t bytes[0x28];
};

struct alignas(4) FxTrailPoolSlotDisk32 final
{
    std::uint8_t bytes[0x08];
};

struct alignas(4) FxTrailElemPoolSlotDisk32 final
{
    std::uint8_t bytes[0x20];
};

struct FxSystemBuffersDisk32 final
{
    FxEffectDisk32 effects[MAX_EFFECTS];
    FxElemPoolSlotDisk32 elems[MAX_ELEMS];
    FxTrailPoolSlotDisk32 trails[MAX_TRAILS];
    FxTrailElemPoolSlotDisk32 trailElems[MAX_TRAIL_ELEMS];
    FxVisStateDisk32 visState[2];
    std::uint16_t deferredElems[MAX_ELEMS];
    std::uint8_t padBuffer[96];
};

// Address-independent ownership metadata reconstructed from all three raw
// free lists.  A set bit denotes an allocated physical slot.
struct FxSystemBuffersDisk32PoolStates final
{
    FxPoolAllocationState<MAX_ELEMS> elems;
    FxPoolAllocationState<MAX_TRAILS> trails;
    FxPoolAllocationState<MAX_TRAIL_ELEMS> trailElems;
};

static_assert(
    std::endian::native == std::endian::little,
    "FX buffer Disk32 records require a little-endian target");
ONDISK_SIZE(float, 0x04);
static_assert(
    std::numeric_limits<float>::is_iec559,
    "FX buffer Disk32 records require IEC 60559 binary32 float");

ONDISK_SIZE(FxElemDisk32, 0x28);
ONDISK_OFFSET(FxElemDisk32, defIndex, 0x00);
ONDISK_OFFSET(FxElemDisk32, sequence, 0x01);
ONDISK_OFFSET(FxElemDisk32, atRestFraction, 0x02);
ONDISK_OFFSET(FxElemDisk32, emitResidual, 0x03);
ONDISK_OFFSET(FxElemDisk32, nextElemHandleInEffect, 0x04);
ONDISK_OFFSET(FxElemDisk32, prevElemHandleInEffect, 0x06);
ONDISK_OFFSET(FxElemDisk32, msecBegin, 0x08);
ONDISK_OFFSET(FxElemDisk32, baseVel, 0x0C);
ONDISK_OFFSET(FxElemDisk32, payload, 0x18);
ONDISK_OFFSET(FxElemDisk32, value, 0x24);
static_assert(alignof(FxElemDisk32) == 4);
static_assert(std::is_standard_layout_v<FxElemDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemDisk32>);

ONDISK_SIZE(FxTrailDisk32, 0x08);
ONDISK_OFFSET(FxTrailDisk32, nextTrailHandle, 0x00);
ONDISK_OFFSET(FxTrailDisk32, firstElemHandle, 0x02);
ONDISK_OFFSET(FxTrailDisk32, lastElemHandle, 0x04);
ONDISK_OFFSET(FxTrailDisk32, defIndex, 0x06);
ONDISK_OFFSET(FxTrailDisk32, sequence, 0x07);
static_assert(alignof(FxTrailDisk32) == 2);
static_assert(std::is_standard_layout_v<FxTrailDisk32>);
static_assert(std::is_trivially_copyable_v<FxTrailDisk32>);

ONDISK_SIZE(FxTrailElemDisk32, 0x20);
ONDISK_OFFSET(FxTrailElemDisk32, origin, 0x00);
ONDISK_OFFSET(FxTrailElemDisk32, spawnDist, 0x0C);
ONDISK_OFFSET(FxTrailElemDisk32, msecBegin, 0x10);
ONDISK_OFFSET(FxTrailElemDisk32, nextTrailElemHandle, 0x14);
ONDISK_OFFSET(FxTrailElemDisk32, baseVelZ, 0x16);
ONDISK_OFFSET(FxTrailElemDisk32, basis, 0x18);
ONDISK_OFFSET(FxTrailElemDisk32, sequence, 0x1E);
ONDISK_OFFSET(FxTrailElemDisk32, unused, 0x1F);
static_assert(alignof(FxTrailElemDisk32) == 4);
static_assert(std::is_standard_layout_v<FxTrailElemDisk32>);
static_assert(std::is_trivially_copyable_v<FxTrailElemDisk32>);

ONDISK_SIZE(FxVisBlockerDisk32, 0x10);
ONDISK_OFFSET(FxVisBlockerDisk32, origin, 0x00);
ONDISK_OFFSET(FxVisBlockerDisk32, radius, 0x0C);
ONDISK_OFFSET(FxVisBlockerDisk32, visibility, 0x0E);
static_assert(alignof(FxVisBlockerDisk32) == 4);
static_assert(std::is_standard_layout_v<FxVisBlockerDisk32>);
static_assert(std::is_trivially_copyable_v<FxVisBlockerDisk32>);

ONDISK_SIZE(FxVisStateDisk32, 0x1010);
ONDISK_OFFSET(FxVisStateDisk32, blocker, 0x0000);
ONDISK_OFFSET(FxVisStateDisk32, blockerCount, 0x1000);
ONDISK_OFFSET(FxVisStateDisk32, pad, 0x1004);
static_assert(alignof(FxVisStateDisk32) == 4);
static_assert(std::is_standard_layout_v<FxVisStateDisk32>);
static_assert(std::is_trivially_copyable_v<FxVisStateDisk32>);

ONDISK_SIZE(FxElemPoolSlotDisk32, 0x28);
ONDISK_OFFSET(FxElemPoolSlotDisk32, bytes, 0x00);
static_assert(alignof(FxElemPoolSlotDisk32) == 4);
static_assert(std::is_standard_layout_v<FxElemPoolSlotDisk32>);
static_assert(std::is_trivially_copyable_v<FxElemPoolSlotDisk32>);

ONDISK_SIZE(FxTrailPoolSlotDisk32, 0x08);
ONDISK_OFFSET(FxTrailPoolSlotDisk32, bytes, 0x00);
static_assert(alignof(FxTrailPoolSlotDisk32) == 4);
static_assert(std::is_standard_layout_v<FxTrailPoolSlotDisk32>);
static_assert(std::is_trivially_copyable_v<FxTrailPoolSlotDisk32>);

ONDISK_SIZE(FxTrailElemPoolSlotDisk32, 0x20);
ONDISK_OFFSET(FxTrailElemPoolSlotDisk32, bytes, 0x00);
static_assert(alignof(FxTrailElemPoolSlotDisk32) == 4);
static_assert(std::is_standard_layout_v<FxTrailElemPoolSlotDisk32>);
static_assert(std::is_trivially_copyable_v<FxTrailElemPoolSlotDisk32>);

ONDISK_SIZE(FxSystemBuffersDisk32, 0x47480);
ONDISK_OFFSET(FxSystemBuffersDisk32, effects, 0x00000);
ONDISK_OFFSET(FxSystemBuffersDisk32, elems, 0x20000);
ONDISK_OFFSET(FxSystemBuffersDisk32, trails, 0x34000);
ONDISK_OFFSET(FxSystemBuffersDisk32, trailElems, 0x34400);
ONDISK_OFFSET(FxSystemBuffersDisk32, visState, 0x44400);
ONDISK_OFFSET(FxSystemBuffersDisk32, deferredElems, 0x46420);
ONDISK_OFFSET(FxSystemBuffersDisk32, padBuffer, 0x47420);
static_assert(alignof(FxSystemBuffersDisk32) == 4);
static_assert(std::is_standard_layout_v<FxSystemBuffersDisk32>);
static_assert(std::is_trivially_copyable_v<FxSystemBuffersDisk32>);

static_assert(std::is_standard_layout_v<FxSystemBuffersDisk32PoolStates>);
static_assert(std::is_trivially_copyable_v<FxSystemBuffersDisk32PoolStates>);

// Decodes the explicitly little-endian first word of one raw free slot.  The
// accepted values are -1 or a nonnegative index in that slot's exact pool.
// Null output or a malformed word returns false without changing the output.
[[nodiscard]] bool TryDecodeFxPoolSlotFreeLinkDisk32(
    const FxElemPoolSlotDisk32 &slot,
    std::int32_t *outNextFree) noexcept;

[[nodiscard]] bool TryDecodeFxPoolSlotFreeLinkDisk32(
    const FxTrailPoolSlotDisk32 &slot,
    std::int32_t *outNextFree) noexcept;

[[nodiscard]] bool TryDecodeFxPoolSlotFreeLinkDisk32(
    const FxTrailElemPoolSlotDisk32 &slot,
    std::int32_t *outNextFree) noexcept;

// Reconstructs all three allocation bitmaps by following only the first four
// explicitly little-endian bytes of each visited free slot.  Only the three
// corresponding firstFree/activeCount pairs in system are consumed; its other
// fields and every byte of an allocated slot are intentionally ignored.
//
// Heads and links must be -1 or an in-range nonnegative index.  A full pool
// must have head -1 and a non-full pool must have a real head.  Cycles,
// duplicates, self-links, overlong chains, and a chain length inconsistent
// with activeCount are rejected.  Null output or malformed input returns
// false without changing any byte of the caller-owned output.
[[nodiscard]] bool TryRebuildFxSystemBuffersDisk32PoolStates(
    const FxSystemBuffersDisk32 &buffers,
    const FxSystemDisk32 &system,
    FxSystemBuffersDisk32PoolStates *outStates) noexcept;
} // namespace fx::archive

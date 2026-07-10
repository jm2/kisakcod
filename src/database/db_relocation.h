#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "db_disk32.h"

namespace db::relocation
{
constexpr std::size_t kBlockCount = 9;
constexpr std::uint32_t kAliasBlock = 4;
using BlockMask = std::uint16_t;

constexpr BlockMask BlockBit(std::uint32_t block)
{
    return block < sizeof(BlockMask) * 8
        ? static_cast<BlockMask>(BlockMask{1} << block)
        : BlockMask{0};
}

constexpr BlockMask kAllBlocks =
    static_cast<BlockMask>((BlockMask{1} << kBlockCount) - 1);

// Block-4 alias/completed-object slots are four-byte serialized identities.
// Native pointers live in this side table so registering provenance never
// widens or overwrites the packed slot on 64-bit hosts.

struct BlockView
{
    std::uintptr_t base;
    std::uint32_t size;
};

enum class AliasKind : std::uint8_t
{
    Invalid,
    XAnimParts,
    SoundData,
    LoadedSound,
    SndCurve,
    SndAliasList,
    GfxTexture,
    GfxImage,
    MaterialTechniqueSet,
    Material,
    GfxLightDef,
    PhysPreset,
    XModel,
    GameWorldSp,
    GameWorldMp,
    FxEffectDef,
    MapEnts,
    ClipMap,
    ComWorld,
    MenuDef,
    MenuList,
    LocalizeEntry,
    FxImpactTable,
    WeaponDef,
    RawFile,
    GfxWorld,
    Font,
    XStringPointerSlot,
    MaterialVertexDeclaration,
    MaterialTechnique,
    MaterialVertexShader,
    MaterialPixelShader,
    MaterialWater,
    MaterialTextureTable,
    SoundFile,
    SpeakerMap,
    SndAliasArray,
    WeaponBounceSoundTable,
    GfxLight,
    StringTable,
    XModelPieces,
    XSurfaceCollisionTree,
    XRigidVertListArray,
    BrushWrapper,
    PhysGeomList,
    Count,
};

constexpr bool RequiresExactStartPublication(AliasKind kind)
{
    switch (kind)
    {
    case AliasKind::XStringPointerSlot:
    case AliasKind::MaterialVertexDeclaration:
    case AliasKind::MaterialTechnique:
    case AliasKind::MaterialVertexShader:
    case AliasKind::MaterialPixelShader:
    case AliasKind::MaterialWater:
    case AliasKind::MaterialTextureTable:
    case AliasKind::SoundFile:
    case AliasKind::SpeakerMap:
    case AliasKind::SndAliasArray:
    case AliasKind::WeaponBounceSoundTable:
    case AliasKind::GfxLight:
    case AliasKind::StringTable:
    case AliasKind::XModelPieces:
    case AliasKind::XSurfaceCollisionTree:
    case AliasKind::XRigidVertListArray:
    case AliasKind::BrushWrapper:
    case AliasKind::PhysGeomList:
        return true;
    default:
        return false;
    }
}

constexpr bool CompletedSharedObjectSchemaValid(
    AliasKind kind,
    std::uint32_t metadata,
    std::uint32_t materializedBytes,
    std::uint32_t *headerBytes)
{
    if (!headerBytes)
        return false;
    *headerBytes = 0;

    std::uint32_t fixedBytes = 0;
    switch (kind)
    {
    case AliasKind::SoundFile:
        fixedBytes = disk32::kSoundFileBytes;
        break;
    case AliasKind::SpeakerMap:
        fixedBytes = disk32::kSpeakerMapBytes;
        break;
    case AliasKind::WeaponBounceSoundTable:
        fixedBytes = disk32::kWeaponBounceSoundTableBytes;
        break;
    case AliasKind::GfxLight:
        fixedBytes = disk32::kGfxLightBytes;
        break;
    case AliasKind::StringTable:
        fixedBytes = disk32::kStringTableBytes;
        break;
    case AliasKind::XModelPieces:
        fixedBytes = disk32::kXModelPiecesBytes;
        break;
    case AliasKind::XSurfaceCollisionTree:
        fixedBytes = disk32::kXSurfaceCollisionTreeBytes;
        break;
    case AliasKind::BrushWrapper:
        fixedBytes = disk32::kBrushWrapperBytes;
        break;
    case AliasKind::PhysGeomList:
        fixedBytes = disk32::kPhysGeomListBytes;
        break;
    case AliasKind::XRigidVertListArray:
        if (metadata != materializedBytes
            || metadata < disk32::kXRigidVertListBytes
            || metadata > static_cast<std::uint32_t>(
                (std::numeric_limits<std::int32_t>::max)())
            || metadata % disk32::kXRigidVertListBytes != 0)
        {
            return false;
        }
        *headerBytes = metadata;
        return true;
    case AliasKind::SndAliasArray:
        if (metadata != materializedBytes
            || metadata < disk32::kSndAliasBytes
            || metadata > static_cast<std::uint32_t>(
                (std::numeric_limits<std::int32_t>::max)())
            || metadata % disk32::kSndAliasBytes != 0)
        {
            return false;
        }
        *headerBytes = metadata;
        return true;
    default:
        return false;
    }

    if (metadata != fixedBytes || materializedBytes != fixedBytes)
        return false;
    *headerBytes = fixedBytes;
    return true;
}

enum class Status : std::uint8_t
{
    Ok,
    InvalidArgument,
    InvalidContext,
    InvalidToken,
    WrongBlock,
    UnloadedBlock,
    OutOfRange,
    MisalignedSlot,
    DuplicateSlot,
    NonMonotonicSlot,
    CapacityExceeded,
    InvalidHandle,
    StaleHandle,
    KindMismatch,
    AlreadyPublished,
    UnregisteredSlot,
    PendingSlot,
    MetadataMismatch,
    InvalidAlignment,
    InvalidBlockMask,
    SizeOverflow,
    MisalignedAddress,
    UnmaterializedRange,
    UnterminatedString,
    InvalidStringExtent,
    UnregisteredString,
};

class DirectResolver
{
public:
    explicit DirectResolver(
        std::size_t maxIntervals = 1u << 20,
        std::size_t maxStrings = 1u << 20);

    void Reset(const BlockView *blocks, std::size_t blockCount);

    Status MarkMaterialized(std::uintptr_t address, std::uint32_t size);
    Status RegisterCString(std::uintptr_t address, std::uint32_t byteCount);
    Status ValidateCStringAddress(
        std::uintptr_t address,
        std::uint32_t *byteCount) const;
    Status ValidateAddress(
        std::uintptr_t address,
        std::uint64_t byteCount,
        std::size_t alignment,
        BlockMask allowedBlocks) const;
    // Empty spans are vacuously materialized and may resolve at block end.
    // Callers must continue to pair the returned pointer with the zero count.
    Status ResolveBytes(
        disk32::PointerToken token,
        std::uint64_t byteCount,
        std::size_t alignment,
        BlockMask allowedBlocks,
        std::uintptr_t *address) const;
    Status ResolveArray(
        disk32::PointerToken token,
        std::uint64_t count,
        std::uint64_t elementSize,
        std::size_t alignment,
        BlockMask allowedBlocks,
        std::uintptr_t *address) const;
    Status ResolveCString(
        disk32::PointerToken token,
        BlockMask allowedBlocks,
        std::uintptr_t *address,
        std::uint32_t *byteCount) const;
    Status ContiguousMaterializedBytes(
        std::uint32_t block,
        std::uint32_t offset,
        std::uint32_t *bytes) const;

private:
    struct Interval
    {
        std::uint32_t begin;
        std::uint32_t end;
    };

    struct BlockState
    {
        BlockView view{};
        std::vector<Interval> materialized;
    };

    struct StringRecord
    {
        std::uint32_t block;
        std::uint32_t offset;
        std::uint32_t byteCount;
    };

    bool ContainsMaterialized(
        std::uint32_t block,
        std::uint32_t offset,
        std::uint32_t size) const;

    BlockState blocks_[kBlockCount];
    std::vector<StringRecord> strings_;
    std::size_t maxIntervals_;
    std::size_t maxStrings_;
    std::size_t intervalCount_ = 0;
    bool contextValid_ = false;
};

class AliasHandle
{
public:
    static constexpr std::uint32_t kInvalidRecord =
        (std::numeric_limits<std::uint32_t>::max)();

    constexpr AliasHandle() = default;

    constexpr explicit operator bool() const
    {
        return recordIndex_ != kInvalidRecord && generation_ != 0;
    }

private:
    friend class AliasRegistry;

    std::uint32_t recordIndex_ = kInvalidRecord;
    std::uint64_t generation_ = 0;
};

class AliasRegistry
{
public:
    explicit AliasRegistry(
        std::size_t maxRecords = static_cast<std::size_t>(AliasHandle::kInvalidRecord));

    void Reset(const BlockView *blocks, std::size_t blockCount);

    Status RegisterSlot(
        std::uintptr_t slotAddress,
        AliasKind kind,
        AliasHandle *handle);
    Status Publish(
        AliasHandle handle,
        AliasKind expectedKind,
        std::uintptr_t resolvedAddress,
        std::uint32_t metadata);
    Status Resolve(
        disk32::PointerToken token,
        AliasKind expectedKind,
        std::uint32_t expectedMetadata,
        std::uintptr_t *resolvedAddress) const;

    std::size_t recordCount() const { return records_.size(); }
    std::uint64_t generation() const { return generation_; }

private:
    struct Record
    {
        std::uint32_t offset;
        AliasKind kind;
        std::uintptr_t resolvedAddress;
        std::uint32_t metadata;
        bool published;
    };

    Status FindSlot(std::uintptr_t slotAddress, std::uint32_t *offset) const;

    BlockView blocks_[kBlockCount]{};
    std::vector<Record> records_;
    std::size_t maxRecords_;
    std::uint64_t generation_ = 0;
    bool contextValid_ = false;
};

const char *StatusName(Status status);
}

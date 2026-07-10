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
// Serialized MaterialVertexDeclaration extent in the 32-bit fast-file schema.
// This is deliberately not sizeof(MaterialVertexDeclaration): the native type
// grows when its D3D pointer array is widened on 64-bit hosts.
constexpr std::uint32_t kMaterialVertexDeclarationDiskBytes = 100;
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
    Count,
};

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

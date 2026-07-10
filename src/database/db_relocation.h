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

// Block-4 alias slots are four-byte serialized identities only. Native pointers
// live in this side table so registering an alias never widens or overwrites the
// packed slot on 64-bit hosts.

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

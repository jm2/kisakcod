#include <database/db_relocation.h>

#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
using db::relocation::AliasHandle;
using db::relocation::AliasKind;
using db::relocation::AliasRegistry;
using db::relocation::BlockBit;
using db::relocation::BlockMask;
using db::relocation::BlockView;
using db::relocation::DirectResolver;
using db::relocation::Status;

int failures = 0;

constexpr std::uintptr_t Address(std::uint64_t value)
{
    return static_cast<std::uintptr_t>(value);
}

void Expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void ExpectStatus(Status actual, Status expected, const char *message)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << message << " (got "
                  << db::relocation::StatusName(actual) << ", expected "
                  << db::relocation::StatusName(expected) << ")\n";
        ++failures;
    }
}

disk32::PointerToken Token(std::uint32_t block, std::uint32_t offset)
{
    return {(block << 28) + offset + 1};
}

void FillBlocks(BlockView (&blocks)[db::relocation::kBlockCount])
{
    for (std::size_t i = 0; i < db::relocation::kBlockCount; ++i)
    {
        blocks[i].base = Address(0x10000000) + i * Address(0x1000);
        blocks[i].size = 0x100;
    }
}

void TestDirectResolver()
{
    BlockView blocks[db::relocation::kBlockCount];
    FillBlocks(blocks);

    DirectResolver resolver;
    std::uintptr_t address = Address(0x55);
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), 4, 4, BlockBit(4), &address),
        Status::InvalidContext,
        "direct resolve before reset");
    Expect(address == 0, "failed direct resolve clears output");

    resolver.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(
        resolver.MarkMaterialized(blocks[4].base, 16),
        Status::Ok,
        "mark first direct range");
    ExpectStatus(
        resolver.MarkMaterialized(blocks[4].base + 16, 8),
        Status::Ok,
        "coalesce adjacent direct range");
    ExpectStatus(
        resolver.MarkMaterialized(blocks[4].base + 32, 8),
        Status::Ok,
        "mark direct range after alignment gap");
    ExpectStatus(
        resolver.MarkMaterialized(blocks[4].base + 4, 4),
        Status::Ok,
        "contained direct range is idempotent");
    ExpectStatus(
        resolver.MarkMaterialized(blocks[4].base + 48, 8),
        Status::Ok,
        "mark second separated range");
    ExpectStatus(
        resolver.MarkMaterialized(blocks[4].base + 40, 8),
        Status::Ok,
        "bridge separated direct ranges");

    std::uint32_t contiguous = 0;
    ExpectStatus(
        resolver.ContiguousMaterializedBytes(4, 0, &contiguous),
        Status::Ok,
        "query coalesced materialized range");
    Expect(contiguous == 24, "adjacent materialized ranges coalesce");
    ExpectStatus(
        resolver.ContiguousMaterializedBytes(4, 32, &contiguous),
        Status::Ok,
        "query bridged materialized range");
    Expect(contiguous == 24, "bridging mark coalesces both neighbors");
    ExpectStatus(
        resolver.ContiguousMaterializedBytes(4, 24, &contiguous),
        Status::UnmaterializedRange,
        "alignment hole remains unmaterialized");

    ExpectStatus(
        resolver.ResolveBytes(Token(4, 4), 8, 4, BlockBit(4), &address),
        Status::Ok,
        "resolve complete materialized byte span");
    Expect(address == blocks[4].base + 4, "direct byte address resolved exactly");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 4), 8, 4, BlockBit(4), nullptr),
        Status::InvalidArgument,
        "null direct output rejected");
    ExpectStatus(
        resolver.ResolveArray(Token(4, 0), 3, 4, 4, BlockBit(4), &address),
        Status::Ok,
        "resolve materialized array span");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 20), 8, 4, BlockBit(4), &address),
        Status::UnmaterializedRange,
        "span crossing materialization gap rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 24), 1, 1, BlockBit(4), &address),
        Status::UnmaterializedRange,
        "alignment padding rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 2), 1, 4, BlockBit(4), &address),
        Status::MisalignedAddress,
        "misaligned direct address rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), 4, 4, BlockBit(7), &address),
        Status::WrongBlock,
        "direct block policy enforced");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), 4, 0, BlockBit(4), &address),
        Status::InvalidAlignment,
        "zero direct alignment rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), 4, 3, BlockBit(4), &address),
        Status::InvalidAlignment,
        "non-power-of-two alignment rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), 4, 4, 0, &address),
        Status::InvalidBlockMask,
        "empty direct block mask rejected");
    ExpectStatus(
        resolver.ResolveBytes(
            Token(4, 0),
            4,
            4,
            static_cast<BlockMask>(BlockBit(4) | BlockBit(12)),
            &address),
        Status::InvalidBlockMask,
        "out-of-domain block mask rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), UINT64_C(0x100000000), 4, BlockBit(4), &address),
        Status::SizeOverflow,
        "direct byte span wider than disk block rejected");
    ExpectStatus(
        resolver.ResolveArray(
            Token(4, 0),
            (std::numeric_limits<std::uint64_t>::max)(),
            2,
            4,
            BlockBit(4),
            &address),
        Status::SizeOverflow,
        "direct array multiplication overflow rejected");
    ExpectStatus(
        resolver.ResolveArray(Token(4, 0), 1, 0, 4, BlockBit(4), &address),
        Status::InvalidArgument,
        "zero direct element size rejected");
    ExpectStatus(
        resolver.ResolveBytes({0}, 1, 1, BlockBit(4), &address),
        Status::InvalidToken,
        "null direct token rejected");
    Expect(address == 0, "later failed direct resolve clears successful output");
    ExpectStatus(
        resolver.ResolveBytes(Token(9, 0), 1, 1, db::relocation::kAllBlocks, &address),
        Status::InvalidToken,
        "out-of-domain direct token rejected");
    ExpectStatus(
        resolver.ResolveBytes(
            Token(4, blocks[4].size),
            0,
            1,
            BlockBit(4),
            &address),
        Status::Ok,
        "zero direct span accepted at block end");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, blocks[4].size), 1, 1, BlockBit(4), &address),
        Status::OutOfRange,
        "nonempty direct span rejected at block end");

    resolver.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), 1, 1, BlockBit(4), &address),
        Status::UnmaterializedRange,
        "reset clears materialized ranges");
    ExpectStatus(
        resolver.MarkMaterialized(Address(0x70000000), 4),
        Status::OutOfRange,
        "non-zone materialization ignored by core");
    contiguous = UINT32_MAX;
    ExpectStatus(
        resolver.ContiguousMaterializedBytes(4, 0, nullptr),
        Status::InvalidArgument,
        "null contiguous-range output rejected");
    ExpectStatus(
        resolver.ContiguousMaterializedBytes(4, blocks[4].size + 1, &contiguous),
        Status::OutOfRange,
        "out-of-range contiguous query rejected");
    Expect(contiguous == 0, "failed contiguous query clears output");

    DirectResolver limited(1);
    limited.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(
        limited.MarkMaterialized(blocks[4].base, 1),
        Status::Ok,
        "fill direct interval budget");
    ExpectStatus(
        limited.MarkMaterialized(blocks[7].base, 1),
        Status::CapacityExceeded,
        "fragmented direct interval budget enforced across blocks");
    ExpectStatus(
        limited.MarkMaterialized(blocks[4].base + 1, 1),
        Status::Ok,
        "adjacent range can coalesce at interval capacity");

    DirectResolver reclaimed(2);
    reclaimed.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(
        reclaimed.MarkMaterialized(blocks[4].base, 1),
        Status::Ok,
        "mark first reclaimable interval");
    ExpectStatus(
        reclaimed.MarkMaterialized(blocks[4].base + 2, 1),
        Status::Ok,
        "mark second reclaimable interval");
    ExpectStatus(
        reclaimed.MarkMaterialized(blocks[4].base + 1, 1),
        Status::Ok,
        "bridge reclaims an interval-budget slot");
    ExpectStatus(
        reclaimed.MarkMaterialized(blocks[7].base, 1),
        Status::Ok,
        "reclaimed interval-budget slot can be reused");
    ExpectStatus(
        reclaimed.MarkMaterialized(blocks[8].base, 1),
        Status::CapacityExceeded,
        "capacity failure preserves bounded interval state");
    ExpectStatus(
        reclaimed.ResolveBytes(Token(4, 0), 3, 1, BlockBit(4), &address),
        Status::Ok,
        "capacity failure preserves existing materialized intervals");

    DirectResolver prepended;
    prepended.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(
        prepended.MarkMaterialized(blocks[4].base + 16, 8),
        Status::Ok,
        "mark later interval before earlier data");
    ExpectStatus(
        prepended.MarkMaterialized(blocks[4].base, 8),
        Status::Ok,
        "prepend an out-of-order interval");
    ExpectStatus(
        prepended.MarkMaterialized(blocks[4].base + 8, 8),
        Status::Ok,
        "bridge prepended and later intervals");
    ExpectStatus(
        prepended.ContiguousMaterializedBytes(4, 0, &contiguous),
        Status::Ok,
        "query out-of-order coalesced interval");
    Expect(contiguous == 24, "out-of-order interval insertion remains sorted");

    BlockView unloaded[db::relocation::kBlockCount];
    FillBlocks(unloaded);
    unloaded[7].base = 0;
    resolver.Reset(unloaded, db::relocation::kBlockCount);
    ExpectStatus(
        resolver.ResolveBytes(Token(7, 0), 1, 1, BlockBit(7), &address),
        Status::UnloadedBlock,
        "unloaded direct block rejected");

    BlockView overflow[db::relocation::kBlockCount];
    FillBlocks(overflow);
    overflow[4].base = (std::numeric_limits<std::uintptr_t>::max)() - 3;
    overflow[4].size = 8;
    resolver.Reset(overflow, db::relocation::kBlockCount);
    ExpectStatus(
        resolver.MarkMaterialized(overflow[4].base, 8),
        Status::OutOfRange,
        "native-wrapping materialized range rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 4), 1, 1, BlockBit(4), &address),
        Status::OutOfRange,
        "native direct address overflow rejected");
    ExpectStatus(
        resolver.ResolveBytes(Token(4, 0), 8, 1, BlockBit(4), &address),
        Status::OutOfRange,
        "native direct span end overflow rejected");

    resolver.Reset(nullptr, 0);
    ExpectStatus(
        resolver.MarkMaterialized(blocks[4].base, 1),
        Status::InvalidContext,
        "materialization rejects invalid reset context");
}
}

int main()
{
    TestDirectResolver();

    BlockView blocks[db::relocation::kBlockCount];
    FillBlocks(blocks);

    AliasRegistry registry;
    AliasHandle handle;
    std::uintptr_t resolved = Address(0x55);
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XAnimParts, 0, &resolved),
        Status::InvalidContext,
        "resolve before reset");
    Expect(resolved == 0, "failed resolve clears output");

    registry.Reset(blocks, db::relocation::kBlockCount);
    const std::uint64_t generation = registry.generation();
    Expect(generation != 0, "reset establishes a nonzero generation");

    ExpectStatus(
        registry.RegisterSlot(blocks[3].base, AliasKind::XAnimParts, &handle),
        Status::WrongBlock,
        "registration outside alias block");
    Expect(!handle, "failed registration clears handle");

    resolved = Address(0x55);
    ExpectStatus(
        registry.Resolve(Token(3, 0), AliasKind::XAnimParts, 0, &resolved),
        Status::WrongBlock,
        "resolution outside alias block");
    Expect(resolved == 0, "wrong-block resolution clears output");

    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XAnimParts, 0, &resolved),
        Status::UnregisteredSlot,
        "valid but unregistered alias slot");
    ExpectStatus(
        registry.RegisterSlot(blocks[4].base + 1, AliasKind::XAnimParts, &handle),
        Status::MisalignedSlot,
        "misaligned registration");
    ExpectStatus(
        registry.Resolve(Token(4, 1), AliasKind::XAnimParts, 0, &resolved),
        Status::MisalignedSlot,
        "middle-of-slot alias token");

    ExpectStatus(
        registry.RegisterSlot(blocks[4].base, AliasKind::XAnimParts, &handle),
        Status::Ok,
        "register alias slot");
    Expect(handle && registry.recordCount() == 1, "registration returns a live handle");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XAnimParts, 0, &resolved),
        Status::PendingSlot,
        "forward alias rejected while pending");
    ExpectStatus(
        registry.Publish(handle, AliasKind::LoadedSound, Address(0x1234), 0),
        Status::KindMismatch,
        "publisher kind mismatch");
    ExpectStatus(
        registry.Publish(handle, AliasKind::Invalid, Address(0x1234), 0),
        Status::InvalidArgument,
        "invalid publisher kind rejected");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XAnimParts, 0, &resolved),
        Status::PendingSlot,
        "failed publication leaves slot pending");

    std::uintptr_t published = Address(0x12345678);
    if constexpr (sizeof(std::uintptr_t) > sizeof(std::uint32_t))
        published = static_cast<std::uintptr_t>(UINT64_C(0x12345678ABCDEF00));
    ExpectStatus(
        registry.Publish(handle, AliasKind::XAnimParts, published, 7),
        Status::Ok,
        "publish native alias pointer");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::LoadedSound, 7, &resolved),
        Status::KindMismatch,
        "consumer kind mismatch");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XAnimParts, 8, &resolved),
        Status::MetadataMismatch,
        "consumer metadata mismatch");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XAnimParts, 7, &resolved),
        Status::Ok,
        "resolve published alias");
    Expect(resolved == published, "native-width pointer round trip");

    ExpectStatus(
        registry.Publish(handle, AliasKind::XAnimParts, Address(0x9999), 7),
        Status::AlreadyPublished,
        "duplicate publication rejected");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XAnimParts, 7, &resolved),
        Status::Ok,
        "original publication remains resolvable");
    Expect(resolved == published, "duplicate publication preserves first pointer");
    const AliasHandle stale = handle;
    ExpectStatus(
        registry.RegisterSlot(blocks[4].base, AliasKind::XAnimParts, &handle),
        Status::DuplicateSlot,
        "duplicate slot registration rejected");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle current;
    ExpectStatus(
        registry.RegisterSlot(blocks[4].base, AliasKind::XAnimParts, &current),
        Status::Ok,
        "reuse slot after reset");
    ExpectStatus(
        registry.Publish(stale, AliasKind::XAnimParts, Address(0x1111), 0),
        Status::StaleHandle,
        "stale generation rejected");
    ExpectStatus(
        registry.Publish({}, AliasKind::XAnimParts, Address(0x1111), 0),
        Status::InvalidHandle,
        "default handle rejected");
    ExpectStatus(
        registry.Publish(current, AliasKind::XAnimParts, Address(0x2222), 0),
        Status::Ok,
        "current handle remains publishable");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle higher;
    ExpectStatus(
        registry.RegisterSlot(blocks[4].base + 8, AliasKind::Material, &higher),
        Status::Ok,
        "register higher slot");
    ExpectStatus(
        registry.RegisterSlot(blocks[4].base + 4, AliasKind::Material, &handle),
        Status::NonMonotonicSlot,
        "non-monotonic slot registration rejected");

    AliasRegistry limited(1);
    limited.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle first;
    ExpectStatus(
        limited.RegisterSlot(blocks[4].base, AliasKind::Material, &first),
        Status::Ok,
        "fill registry to capacity");
    ExpectStatus(
        limited.RegisterSlot(blocks[4].base + 4, AliasKind::Material, &handle),
        Status::CapacityExceeded,
        "registry capacity enforced");
    Expect(limited.recordCount() == 1, "capacity failure preserves registry");
    ExpectStatus(
        limited.Publish(first, AliasKind::Material, Address(0x3333), 0),
        Status::Ok,
        "existing record survives capacity failure");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle sorted[3];
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        ExpectStatus(
            registry.RegisterSlot(
                blocks[4].base + i * sizeof(std::uint32_t),
                AliasKind::Material,
                &sorted[i]),
            Status::Ok,
            "register sorted alias record");
        ExpectStatus(
            registry.Publish(
                sorted[i],
                AliasKind::Material,
                Address(0x4000 + i),
                0),
            Status::Ok,
            "publish sorted alias record");
    }
    ExpectStatus(
        registry.Resolve(Token(4, 4), AliasKind::Material, 0, &resolved),
        Status::Ok,
        "binary search finds middle alias record");
    Expect(resolved == Address(0x4001), "middle alias pointer is exact");

    BlockView wideBlocks[db::relocation::kBlockCount];
    FillBlocks(wideBlocks);
    constexpr std::uint32_t lastEncodableAligned = disk32::kOffsetMask & ~UINT32_C(3);
    constexpr std::uint32_t firstUnencodableAligned = lastEncodableAligned + 4;
    wideBlocks[4].size = firstUnencodableAligned + sizeof(std::uint32_t);
    registry.Reset(wideBlocks, db::relocation::kBlockCount);
    ExpectStatus(
        registry.RegisterSlot(
            wideBlocks[4].base + lastEncodableAligned,
            AliasKind::Material,
            &handle),
        Status::Ok,
        "last encodable aligned alias slot accepted");
    ExpectStatus(
        registry.RegisterSlot(
            wideBlocks[4].base + firstUnencodableAligned,
            AliasKind::Material,
            &handle),
        Status::OutOfRange,
        "first unencodable alias slot rejected");

    BlockView overflowBlocks[db::relocation::kBlockCount];
    FillBlocks(overflowBlocks);
    overflowBlocks[4].base = (std::numeric_limits<std::uintptr_t>::max)() - 3;
    overflowBlocks[4].size = 8;
    registry.Reset(overflowBlocks, db::relocation::kBlockCount);
    ExpectStatus(
        registry.Resolve(Token(4, 4), AliasKind::Material, 0, &resolved),
        Status::OutOfRange,
        "native slot address overflow rejected");

    registry.Reset(nullptr, 0);
    ExpectStatus(
        registry.RegisterSlot(blocks[4].base, AliasKind::Material, &handle),
        Status::InvalidContext,
        "registration rejects invalid reset context");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::Material, 0, &resolved),
        Status::InvalidContext,
        "resolution rejects invalid reset context");

    registry.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(
        registry.RegisterSlot(blocks[4].base, AliasKind::GfxTexture, &handle),
        Status::Ok,
        "register nullable texture alias");
    ExpectStatus(
        registry.Publish(handle, AliasKind::GfxTexture, 0, 2),
        Status::Ok,
        "publish nullable texture alias");
    resolved = Address(0x55);
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::GfxTexture, 2, &resolved),
        Status::Ok,
        "resolve nullable texture alias");
    Expect(resolved == 0, "nullable alias remains null");

    resolved = Address(0x55);
    ExpectStatus(
        registry.Resolve({0}, AliasKind::GfxTexture, 2, &resolved),
        Status::InvalidToken,
        "null token rejected");
    ExpectStatus(
        registry.Resolve({disk32::kInline}, AliasKind::GfxTexture, 2, &resolved),
        Status::InvalidToken,
        "inline sentinel rejected");
    ExpectStatus(
        registry.Resolve(Token(4, blocks[4].size), AliasKind::GfxTexture, 2, &resolved),
        Status::OutOfRange,
        "slot span past block end rejected");

    BlockView unloaded[db::relocation::kBlockCount];
    FillBlocks(unloaded);
    unloaded[4].base = 0;
    registry.Reset(unloaded, db::relocation::kBlockCount);
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::GfxTexture, 2, &resolved),
        Status::UnloadedBlock,
        "unloaded alias block rejected");

    return failures == 0 ? 0 : 1;
}

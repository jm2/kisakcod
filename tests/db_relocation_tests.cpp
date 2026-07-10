#include <database/db_relocation.h>

#include <array>
#include <cstdint>
#include <cstring>
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
    ExpectStatus(
        resolver.ValidateAddress(Address(0x1000), 4, 4, BlockBit(4)),
        Status::InvalidContext,
        "native address validation before reset");
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
        resolver.ValidateAddress(blocks[4].base + 4, 8, 4, BlockBit(4)),
        Status::Ok,
        "validate materialized native address span");
    ExpectStatus(
        resolver.ValidateAddress(blocks[4].base + 20, 8, 4, BlockBit(4)),
        Status::UnmaterializedRange,
        "native address crossing materialization gap rejected");
    ExpectStatus(
        resolver.ValidateAddress(blocks[4].base + 2, 1, 4, BlockBit(4)),
        Status::MisalignedAddress,
        "misaligned native address rejected");
    ExpectStatus(
        resolver.ValidateAddress(blocks[4].base, 4, 4, BlockBit(7)),
        Status::WrongBlock,
        "native address block policy enforced");
    ExpectStatus(
        resolver.ValidateAddress(blocks[4].base, 4, 0, BlockBit(4)),
        Status::InvalidAlignment,
        "zero native address alignment rejected");
    ExpectStatus(
        resolver.ValidateAddress(blocks[4].base, 4, 3, BlockBit(4)),
        Status::InvalidAlignment,
        "non-power-of-two native address alignment rejected");
    ExpectStatus(
        resolver.ValidateAddress(blocks[4].base, 4, 4, 0),
        Status::InvalidBlockMask,
        "empty native address block mask rejected");
    ExpectStatus(
        resolver.ValidateAddress(
            blocks[4].base,
            4,
            4,
            static_cast<BlockMask>(BlockBit(4) | BlockBit(12))),
        Status::InvalidBlockMask,
        "out-of-domain native address block mask rejected");
    ExpectStatus(
        resolver.ValidateAddress(Address(0x70000000), 4, 4, BlockBit(4)),
        Status::OutOfRange,
        "non-zone native address rejected");
    ExpectStatus(
        resolver.ValidateAddress(
            blocks[4].base,
            UINT64_C(0x100000000),
            4,
            BlockBit(4)),
        Status::SizeOverflow,
        "native address span wider than disk block rejected");

    BlockView adjacentBlocks[db::relocation::kBlockCount];
    FillBlocks(adjacentBlocks);
    adjacentBlocks[3].base = Address(0x20000000);
    adjacentBlocks[3].size = 0x100;
    adjacentBlocks[4].base = adjacentBlocks[3].base + adjacentBlocks[3].size;
    adjacentBlocks[4].size = 0x100;
    DirectResolver adjacentResolver;
    adjacentResolver.Reset(adjacentBlocks, db::relocation::kBlockCount);
    ExpectStatus(
        adjacentResolver.MarkMaterialized(
            adjacentBlocks[4].base,
            adjacentBlocks[4].size),
        Status::Ok,
        "materialize block adjacent to prior block end");
    ExpectStatus(
        adjacentResolver.ValidateAddress(
            adjacentBlocks[4].base,
            disk32::kMaterialVertexDeclarationBytes,
            4,
            BlockBit(4)),
        Status::Ok,
        "later adjacent block start is not shadowed by prior block end");
    ExpectStatus(
        adjacentResolver.ValidateAddress(
            adjacentBlocks[4].base
                + adjacentBlocks[4].size
                - disk32::kMaterialVertexDeclarationBytes,
            disk32::kMaterialVertexDeclarationBytes,
            4,
            BlockBit(4)),
        Status::Ok,
        "last complete native address span accepted");
    ExpectStatus(
        adjacentResolver.ValidateAddress(
            adjacentBlocks[4].base
                + adjacentBlocks[4].size
                - disk32::kMaterialVertexDeclarationBytes
                + 1,
            disk32::kMaterialVertexDeclarationBytes,
            1,
            BlockBit(4)),
        Status::OutOfRange,
        "native address span crossing block end rejected");

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

void TestDirectCString()
{
    alignas(2) std::array<std::uint8_t, 64> storage{};
    storage.fill(static_cast<std::uint8_t>('x'));
    storage[0] = 0;
    std::memcpy(storage.data() + 9, "alpha", 6);
    storage[20] = 0;
    storage[24] = static_cast<std::uint8_t>('a');
    storage[25] = static_cast<std::uint8_t>('b');
    storage[27] = 0;
    storage[30] = 0;
    storage[31] = static_cast<std::uint8_t>('z');
    std::memcpy(storage.data() + 40, "seq", 4);

    BlockView blocks[db::relocation::kBlockCount]{};
    blocks[4].base = reinterpret_cast<std::uintptr_t>(storage.data());
    blocks[4].size = static_cast<std::uint32_t>(storage.size());

    DirectResolver resolver;
    resolver.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(resolver.MarkMaterialized(blocks[4].base, 1), Status::Ok, "mark empty C string");
    ExpectStatus(resolver.MarkMaterialized(blocks[4].base + 9, 6), Status::Ok, "mark terminated C string");
    ExpectStatus(resolver.MarkMaterialized(blocks[4].base + 16, 4), Status::Ok, "mark unterminated C string");
    ExpectStatus(resolver.MarkMaterialized(blocks[4].base + 24, 2), Status::Ok, "mark C string prefix before gap");
    ExpectStatus(resolver.MarkMaterialized(blocks[4].base + 27, 1), Status::Ok, "mark terminator after gap");
    ExpectStatus(resolver.MarkMaterialized(blocks[4].base + 30, 2), Status::Ok, "mark extent with early terminator");
    for (std::uint32_t offset = 40; offset < 44; ++offset)
    {
        ExpectStatus(
            resolver.MarkMaterialized(blocks[4].base + offset, 1),
            Status::Ok,
            "coalesce sequential one-byte C string writes");
    }
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base, 1),
        Status::Ok,
        "register empty C string provenance");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 9, 6),
        Status::Ok,
        "register terminated C string provenance");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 9, 6),
        Status::Ok,
        "duplicate exact C string registration is idempotent");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 9, 7),
        Status::MetadataMismatch,
        "duplicate C string start with different extent rejected");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 40, 4),
        Status::Ok,
        "register sequentially written C string provenance");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 16, 4),
        Status::UnterminatedString,
        "C string registration rejects terminator outside its extent");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 24, 4),
        Status::UnmaterializedRange,
        "C string registration rejects a materialization gap");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 30, 2),
        Status::InvalidStringExtent,
        "C string registration rejects bytes after its first terminator");
    ExpectStatus(
        resolver.RegisterCString(blocks[4].base + 9, 0),
        Status::InvalidArgument,
        "zero-length C string registration rejected");

    std::uint32_t registeredBytes = UINT32_MAX;
    ExpectStatus(
        resolver.ValidateCStringAddress(blocks[4].base + 9, &registeredBytes),
        Status::Ok,
        "validate exact registered C string address");
    Expect(registeredBytes == 6, "registered C string extent is preserved");
    ExpectStatus(
        resolver.ValidateCStringAddress(blocks[4].base + 10, &registeredBytes),
        Status::UnregisteredString,
        "interior C string address lacks start provenance");
    Expect(registeredBytes == 0, "failed C string address validation clears extent");
    ExpectStatus(
        resolver.ValidateCStringAddress(blocks[4].base + 9, nullptr),
        Status::InvalidArgument,
        "null C string validation output rejected");

    std::uintptr_t address = Address(0x55);
    std::uint32_t byteCount = UINT32_MAX;
    ExpectStatus(
        resolver.ResolveCString(Token(4, 0), BlockBit(4), &address, &byteCount),
        Status::Ok,
        "resolve empty materialized C string");
    Expect(address == blocks[4].base && byteCount == 1, "empty C string includes its terminator");
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(4), &address, &byteCount),
        Status::Ok,
        "resolve terminated materialized C string");
    Expect(address == blocks[4].base + 9 && byteCount == 6, "C string stops at bounded terminator");
    Expect((address & 1) != 0, "C string resolution permits byte-aligned addresses");
    storage[14] = static_cast<std::uint8_t>('x');
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(4), &address, &byteCount),
        Status::InvalidStringExtent,
        "mutated registered C string terminator rejected");
    Expect(address == 0 && byteCount == 0, "mutated C string clears outputs");
    storage[14] = 0;
    storage[10] = 0;
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(4), &address, &byteCount),
        Status::InvalidStringExtent,
        "mutated interior C string terminator rejected");
    Expect(address == 0 && byteCount == 0, "interior-NUL C string clears outputs");
    storage[10] = static_cast<std::uint8_t>('l');
    ExpectStatus(
        resolver.ResolveCString(Token(4, 40), BlockBit(4), &address, &byteCount),
        Status::Ok,
        "resolve C string materialized one byte at a time");
    Expect(byteCount == 4, "sequential C string writes coalesce through the terminator");

    ExpectStatus(
        resolver.ResolveCString(Token(4, 16), BlockBit(4), &address, &byteCount),
        Status::UnregisteredString,
        "materialized bytes without C string provenance rejected");
    Expect(address == 0 && byteCount == 0, "unregistered C string clears outputs");
    ExpectStatus(
        resolver.ResolveCString(Token(4, 24), BlockBit(4), &address, &byteCount),
        Status::UnregisteredString,
        "C string cannot cross a gap to unregistered terminator");
    ExpectStatus(
        resolver.ResolveCString(Token(4, 32), BlockBit(4), &address, &byteCount),
        Status::UnmaterializedRange,
        "unmaterialized C string rejected");
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(7), &address, &byteCount),
        Status::WrongBlock,
        "C string block policy enforced");
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), 0, &address, &byteCount),
        Status::InvalidBlockMask,
        "empty C string block policy rejected");
    ExpectStatus(
        resolver.ResolveCString({0}, BlockBit(4), &address, &byteCount),
        Status::InvalidToken,
        "null token rejected as direct C string");
    ExpectStatus(
        resolver.ResolveCString({disk32::kInline}, BlockBit(4), &address, &byteCount),
        Status::InvalidToken,
        "inline sentinel rejected as direct C string");
    ExpectStatus(
        resolver.ResolveCString({disk32::kSharedInline}, BlockBit(4), &address, &byteCount),
        Status::InvalidToken,
        "shared-inline sentinel rejected as direct C string");
    byteCount = UINT32_MAX;
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(4), nullptr, &byteCount),
        Status::InvalidArgument,
        "null C string address output rejected");
    Expect(byteCount == 0, "null C string address clears length output");
    address = Address(0x55);
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(4), &address, nullptr),
        Status::InvalidArgument,
        "null C string length output rejected");
    Expect(address == 0, "null C string length clears address output");

    DirectResolver stringLimited(16, 1);
    stringLimited.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(stringLimited.MarkMaterialized(blocks[4].base, 1), Status::Ok, "mark first limited C string");
    ExpectStatus(stringLimited.MarkMaterialized(blocks[4].base + 9, 6), Status::Ok, "mark second limited C string");
    ExpectStatus(stringLimited.RegisterCString(blocks[4].base, 1), Status::Ok, "fill C string provenance budget");
    ExpectStatus(
        stringLimited.RegisterCString(blocks[4].base + 9, 6),
        Status::CapacityExceeded,
        "C string provenance budget enforced");
    ExpectStatus(
        stringLimited.ResolveCString(Token(4, 0), BlockBit(4), &address, &byteCount),
        Status::Ok,
        "C string capacity failure preserves registered provenance");

    resolver.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(resolver.MarkMaterialized(blocks[4].base + 9, 6), Status::Ok, "remark C string after reset");
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(4), &address, &byteCount),
        Status::UnregisteredString,
        "C string reset clears registered provenance");
    blocks[4].base = 0;
    resolver.Reset(blocks, db::relocation::kBlockCount);
    ExpectStatus(
        resolver.ResolveCString(Token(4, 9), BlockBit(4), &address, &byteCount),
        Status::UnloadedBlock,
        "C string in unloaded block rejected");

    resolver.Reset(nullptr, 0);
    ExpectStatus(
        resolver.RegisterCString(reinterpret_cast<std::uintptr_t>(storage.data()), 1),
        Status::InvalidContext,
        "C string registration rejects invalid context");
    byteCount = UINT32_MAX;
    ExpectStatus(
        resolver.ValidateCStringAddress(
            reinterpret_cast<std::uintptr_t>(storage.data()),
            &byteCount),
        Status::InvalidContext,
        "C string validation rejects invalid context");
    Expect(byteCount == 0, "invalid-context C string validation clears extent");
}
}

int main()
{
    TestDirectResolver();
    TestDirectCString();

    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::MaterialVertexDeclaration),
        "material vertex declarations require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::MaterialTechnique),
        "material techniques require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::MaterialVertexShader),
        "material vertex shaders require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::MaterialPixelShader),
        "material pixel shaders require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::MaterialWater),
        "material water requires exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::MaterialTextureTable),
        "material texture tables require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::SoundFile),
        "sound files require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::SpeakerMap),
        "speaker maps require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::SndAliasArray),
        "sound-alias arrays require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::WeaponBounceSoundTable),
        "weapon bounce-sound tables require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::GfxLight),
        "graphics lights require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::StringTable),
        "string tables require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::XModelPieces),
        "model-pieces headers require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::XSurfaceCollisionTree),
        "surface collision trees require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::XRigidVertListArray),
        "rigid-vertex lists require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::BrushWrapper),
        "physics brush wrappers require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(AliasKind::PhysGeomList),
        "physics geometry lists require exact completed starts");
    Expect(
        db::relocation::RequiresExactStartPublication(
            AliasKind::ClipMapBoxBrush),
        "clipmap box brushes require exact completed starts");
    Expect(
        !db::relocation::RequiresExactStartPublication(AliasKind::Material),
        "redirectable asset aliases do not require their slot address");
    Expect(
        disk32::kSoundFileBytes == 12u
            && disk32::kSpeakerMapBytes == 408u
            && disk32::kSndAliasBytes == 92u
            && disk32::kWeaponBounceSoundCount == 29u
            && disk32::kWeaponBounceSoundTableBytes == 116u
            && disk32::kGfxLightBytes == 64u
            && disk32::kGfxWorldBytes == 732u
            && disk32::kGfxReflectionProbeBytes == 16u
            && disk32::kGfxTextureBytes == 4u
            && disk32::kGfxCellBytes == 56u
            && disk32::kGfxBrushModelBytes == 56u
            && disk32::kGfxAabbTreeBytes == 44u
            && disk32::kGfxPortalBytes == 68u
            && disk32::kGfxCullGroupBytes == 32u
            && disk32::kDpvsPlaneBytes == 20u
            && disk32::kVec3Bytes == 12u
            && disk32::kGameWorldSpBytes == 44u
            && disk32::kPathDataBytes == 40u
            && disk32::kPathNodeBytes == 128u
            && disk32::kPathNodeConstantBytes == 68u
            && disk32::kPathLinkBytes == 12u
            && disk32::kPathBaseNodeBytes == 16u
            && disk32::kPathTreeBytes == 16u
            && disk32::kPathTreeLeafInfoBytes == 8u
            && disk32::kPathNodeIndexBytes == 2u
            && disk32::kStringTableBytes == 16u
            && disk32::kXModelPieceBytes == 16u
            && disk32::kXModelPiecesBytes == 12u
            && disk32::kXSurfaceCollisionTreeBytes == 40u
            && disk32::kXSurfaceCollisionNodeBytes == 16u
            && disk32::kXSurfaceCollisionLeafBytes == 2u
            && disk32::kXRigidVertListBytes == 12u
            && disk32::kBrushWrapperBytes == 80u
            && disk32::kPhysGeomInfoBytes == 68u
            && disk32::kPhysGeomListBytes == 44u
            && disk32::kCBrushBytes == 80u
            && disk32::kCBrushSideBytes == 12u
            && disk32::kCPlaneBytes == 20u,
        "completed shared-object disk32 schemas remain fixed");

    struct CompletedSchemaCase
    {
        AliasKind kind;
        std::uint32_t bytes;
    };
    constexpr CompletedSchemaCase completedSchemas[] = {
        {AliasKind::SoundFile, disk32::kSoundFileBytes},
        {AliasKind::SpeakerMap, disk32::kSpeakerMapBytes},
        {AliasKind::SndAliasArray, 2 * disk32::kSndAliasBytes},
        {AliasKind::WeaponBounceSoundTable,
            disk32::kWeaponBounceSoundTableBytes},
        {AliasKind::GfxLight, disk32::kGfxLightBytes},
        {AliasKind::StringTable, disk32::kStringTableBytes},
        {AliasKind::XModelPieces, disk32::kXModelPiecesBytes},
        {AliasKind::XSurfaceCollisionTree,
            disk32::kXSurfaceCollisionTreeBytes},
        {AliasKind::XRigidVertListArray,
            2 * disk32::kXRigidVertListBytes},
        {AliasKind::BrushWrapper, disk32::kBrushWrapperBytes},
        {AliasKind::PhysGeomList, disk32::kPhysGeomListBytes},
        {AliasKind::ClipMapBoxBrush, disk32::kCBrushBytes},
    };
    for (const CompletedSchemaCase &schema : completedSchemas)
    {
        std::uint32_t headerBytes = UINT32_MAX;
        Expect(
            db::relocation::CompletedSharedObjectSchemaValid(
                schema.kind,
                schema.bytes,
                schema.bytes,
                &headerBytes)
                && headerBytes == schema.bytes,
            "completed shared-object schema accepts its exact disk extent");
        headerBytes = UINT32_MAX;
        Expect(
            !db::relocation::CompletedSharedObjectSchemaValid(
                schema.kind,
                schema.bytes - 1,
                schema.bytes,
                &headerBytes)
                && headerBytes == 0,
            "completed shared-object schema rejects wrong metadata");
        headerBytes = UINT32_MAX;
        Expect(
            !db::relocation::CompletedSharedObjectSchemaValid(
                schema.kind,
                schema.bytes,
                schema.bytes - 1,
                &headerBytes)
                && headerBytes == 0,
            "completed shared-object schema rejects a short materialized span");
    }
    std::uint32_t schemaHeader = UINT32_MAX;
    const std::uint32_t maximumAliasBytes =
        static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)())
        / disk32::kSndAliasBytes
        * disk32::kSndAliasBytes;
    Expect(
        db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::SndAliasArray,
            maximumAliasBytes,
            maximumAliasBytes,
            &schemaHeader)
            && schemaHeader == maximumAliasBytes,
        "largest int32-representable sound-alias array accepted");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::SndAliasArray,
            0,
            0,
            &schemaHeader),
        "empty completed sound-alias array rejected");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::SndAliasArray,
            disk32::kSndAliasBytes + 1,
            disk32::kSndAliasBytes + 1,
            &schemaHeader),
        "non-integral completed sound-alias array rejected");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::SndAliasArray,
            maximumAliasBytes + disk32::kSndAliasBytes,
            maximumAliasBytes + disk32::kSndAliasBytes,
            &schemaHeader),
        "oversized completed sound-alias array rejected");
    const std::uint32_t maximumRigidListBytes =
        static_cast<std::uint32_t>(
            (std::numeric_limits<std::int32_t>::max)())
        / disk32::kXRigidVertListBytes
        * disk32::kXRigidVertListBytes;
    Expect(
        db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::XRigidVertListArray,
            maximumRigidListBytes,
            maximumRigidListBytes,
            &schemaHeader)
            && schemaHeader == maximumRigidListBytes,
        "largest int32-representable rigid-vertex list accepted");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::XRigidVertListArray,
            0,
            0,
            &schemaHeader),
        "empty completed rigid-vertex list rejected");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::XRigidVertListArray,
            disk32::kXRigidVertListBytes + 1,
            disk32::kXRigidVertListBytes + 1,
            &schemaHeader),
        "non-integral completed rigid-vertex list rejected");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::XRigidVertListArray,
            maximumRigidListBytes + disk32::kXRigidVertListBytes,
            maximumRigidListBytes + disk32::kXRigidVertListBytes,
            &schemaHeader),
        "oversized completed rigid-vertex list rejected");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::Material,
            4,
            4,
            &schemaHeader)
            && schemaHeader == 0,
        "unsupported shared-object schema rejected");
    Expect(
        !db::relocation::CompletedSharedObjectSchemaValid(
            AliasKind::SoundFile,
            disk32::kSoundFileBytes,
            disk32::kSoundFileBytes,
            nullptr),
        "completed shared-object schema requires an extent output");

    BlockView blocks[db::relocation::kBlockCount];
    FillBlocks(blocks);

    for (const CompletedSchemaCase &schema : completedSchemas)
    {
        AliasRegistry completedRegistry;
        completedRegistry.Reset(blocks, db::relocation::kBlockCount);
        AliasHandle completedHandle;
        ExpectStatus(
            completedRegistry.RegisterSlot(
                blocks[4].base,
                schema.kind,
                &completedHandle),
            Status::Ok,
            "register completed shared-object start");
        std::uintptr_t completedAddress = Address(0x55);
        ExpectStatus(
            completedRegistry.Resolve(
                Token(4, 0),
                schema.kind,
                schema.bytes,
                &completedAddress),
            Status::PendingSlot,
            "completed shared object remains pending before publication");
        ExpectStatus(
            completedRegistry.Publish(
                completedHandle,
                schema.kind,
                blocks[4].base + 4,
                schema.bytes),
            Status::MetadataMismatch,
            "completed shared object rejects a shifted publication");
        ExpectStatus(
            completedRegistry.Resolve(
                Token(4, 0),
                schema.kind,
                schema.bytes,
                &completedAddress),
            Status::PendingSlot,
            "failed shared-object publication preserves pending state");
        ExpectStatus(
            completedRegistry.Publish(
                completedHandle,
                schema.kind,
                blocks[4].base,
                schema.bytes),
            Status::Ok,
            "publish exact completed shared-object start");
        ExpectStatus(
            completedRegistry.Resolve(
                Token(4, 0),
                AliasKind::Material,
                schema.bytes,
                &completedAddress),
            Status::KindMismatch,
            "completed shared object rejects a different type");
        ExpectStatus(
            completedRegistry.Resolve(
                Token(4, 0),
                schema.kind,
                schema.bytes - 1,
                &completedAddress),
            Status::MetadataMismatch,
            "completed shared object rejects different metadata");
        ExpectStatus(
            completedRegistry.Resolve(
                Token(4, 4),
                schema.kind,
                schema.bytes,
                &completedAddress),
            Status::UnregisteredSlot,
            "completed shared-object interior is not a registered start");
        ExpectStatus(
            completedRegistry.Resolve(
                Token(4, 0),
                schema.kind,
                schema.bytes,
                &completedAddress),
            Status::Ok,
            "resolve exact completed shared-object start");
        Expect(
            completedAddress == blocks[4].base,
            "completed shared-object address preserves serialized identity");
    }

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

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle stringSlot;
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base,
            AliasKind::XStringPointerSlot,
            &stringSlot),
        Status::Ok,
        "register completed C string pointer slot");
    ExpectStatus(
        registry.Publish(
            stringSlot,
            AliasKind::XStringPointerSlot,
            blocks[4].base + 4,
            0),
        Status::MetadataMismatch,
        "completed C string pointer must publish its own slot");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XStringPointerSlot, 0, &resolved),
        Status::PendingSlot,
        "wrong completed-slot publication leaves provenance pending");
    ExpectStatus(
        registry.Publish(
            stringSlot,
            AliasKind::XStringPointerSlot,
            blocks[4].base,
            0),
        Status::Ok,
        "publish exact completed C string pointer slot");
    ExpectStatus(
        registry.Resolve(Token(4, 0), AliasKind::XStringPointerSlot, 0, &resolved),
        Status::Ok,
        "resolve exact completed C string pointer slot");
    Expect(resolved == blocks[4].base, "completed C string pointer slot is exact");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle declaration;
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base,
            AliasKind::MaterialVertexDeclaration,
            &declaration),
        Status::Ok,
        "register completed material vertex declaration start");
    ExpectStatus(
        registry.Publish(
            declaration,
            AliasKind::MaterialVertexDeclaration,
            blocks[4].base + 4,
            disk32::kMaterialVertexDeclarationBytes),
        Status::MetadataMismatch,
        "completed material vertex declaration must publish its own start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialVertexDeclaration,
            disk32::kMaterialVertexDeclarationBytes,
            &resolved),
        Status::PendingSlot,
        "wrong declaration publication leaves provenance pending");
    ExpectStatus(
        registry.Publish(
            declaration,
            AliasKind::MaterialVertexDeclaration,
            blocks[4].base,
            disk32::kMaterialVertexDeclarationBytes),
        Status::Ok,
        "publish exact completed material vertex declaration start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialVertexDeclaration,
            0,
            &resolved),
        Status::MetadataMismatch,
        "material vertex declaration schema metadata is exact");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::Material,
            disk32::kMaterialVertexDeclarationBytes,
            &resolved),
        Status::KindMismatch,
        "material vertex declaration cannot resolve as another object type");
    ExpectStatus(
        registry.Resolve(
            Token(4, 4),
            AliasKind::MaterialVertexDeclaration,
            disk32::kMaterialVertexDeclarationBytes,
            &resolved),
        Status::UnregisteredSlot,
        "interior material vertex declaration address is not an object start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialVertexDeclaration,
            disk32::kMaterialVertexDeclarationBytes,
            &resolved),
        Status::Ok,
        "resolve exact completed material vertex declaration start");
    Expect(
        resolved == blocks[4].base,
        "completed material vertex declaration address is exact");

    AliasHandle secondDeclaration;
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base + disk32::kMaterialVertexDeclarationBytes,
            AliasKind::MaterialVertexDeclaration,
            &secondDeclaration),
        Status::Ok,
        "register distinct completed declaration with identical shape");
    ExpectStatus(
        registry.Publish(
            secondDeclaration,
            AliasKind::MaterialVertexDeclaration,
            blocks[4].base + disk32::kMaterialVertexDeclarationBytes,
            disk32::kMaterialVertexDeclarationBytes),
        Status::Ok,
        "publish distinct completed declaration start");
    ExpectStatus(
        registry.Resolve(
            Token(4, disk32::kMaterialVertexDeclarationBytes),
            AliasKind::MaterialVertexDeclaration,
            disk32::kMaterialVertexDeclarationBytes,
            &resolved),
        Status::Ok,
        "resolve second completed declaration independently");
    Expect(
        resolved == blocks[4].base
            + disk32::kMaterialVertexDeclarationBytes,
        "identical declarations retain distinct serialized identities");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle technique;
    AliasHandle nestedDeclaration;
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base,
            AliasKind::MaterialTechnique,
            &technique),
        Status::Ok,
        "register completed material technique start");
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base + 28,
            AliasKind::MaterialVertexDeclaration,
            &nestedDeclaration),
        Status::Ok,
        "register nested declaration after material technique parent");
    ExpectStatus(
        registry.Publish(
            nestedDeclaration,
            AliasKind::MaterialVertexDeclaration,
            blocks[4].base + 28,
            disk32::kMaterialVertexDeclarationBytes),
        Status::Ok,
        "nested declaration may complete before technique parent");
    ExpectStatus(
        registry.Resolve(
            Token(4, 28),
            AliasKind::MaterialVertexDeclaration,
            disk32::kMaterialVertexDeclarationBytes,
            &resolved),
        Status::Ok,
        "completed nested declaration resolves while parent is pending");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialTechnique,
            disk32::kMaterialTechniqueSchema,
            &resolved),
        Status::PendingSlot,
        "material technique remains pending until its graph is complete");
    ExpectStatus(
        registry.Publish(
            technique,
            AliasKind::MaterialTechnique,
            blocks[4].base + 4,
            disk32::kMaterialTechniqueSchema),
        Status::MetadataMismatch,
        "completed material technique must publish its own start");
    ExpectStatus(
        registry.Publish(
            technique,
            AliasKind::MaterialTechnique,
            blocks[4].base,
            disk32::kMaterialTechniqueSchema),
        Status::Ok,
        "publish exact completed material technique start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialTechnique,
            0,
            &resolved),
        Status::MetadataMismatch,
        "material technique layout metadata is exact");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialVertexDeclaration,
            disk32::kMaterialTechniqueSchema,
            &resolved),
        Status::KindMismatch,
        "material technique cannot resolve as a vertex declaration");
    ExpectStatus(
        registry.Resolve(
            Token(4, disk32::kMaterialTechniqueHeaderBytes),
            AliasKind::MaterialTechnique,
            disk32::kMaterialTechniqueSchema,
            &resolved),
        Status::UnregisteredSlot,
        "material technique pass-array interior is not an object start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialTechnique,
            disk32::kMaterialTechniqueSchema,
            &resolved),
        Status::Ok,
        "resolve exact completed material technique start");
    Expect(resolved == blocks[4].base, "completed material technique address is exact");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle vertexShader;
    AliasHandle pixelShader;
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base,
            AliasKind::MaterialVertexShader,
            &vertexShader),
        Status::Ok,
        "register completed material vertex shader start");
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base + disk32::kMaterialVertexShaderBytes,
            AliasKind::MaterialPixelShader,
            &pixelShader),
        Status::Ok,
        "register completed material pixel shader start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialVertexShader,
            disk32::kMaterialVertexShaderBytes,
            &resolved),
        Status::PendingSlot,
        "material vertex shader is pending before completion");
    ExpectStatus(
        registry.Publish(
            vertexShader,
            AliasKind::MaterialVertexShader,
            blocks[4].base + 4,
            disk32::kMaterialVertexShaderBytes),
        Status::MetadataMismatch,
        "completed vertex shader must publish its own start");
    ExpectStatus(
        registry.Publish(
            vertexShader,
            AliasKind::MaterialVertexShader,
            blocks[4].base,
            disk32::kMaterialVertexShaderBytes),
        Status::Ok,
        "publish exact completed material vertex shader start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialPixelShader,
            disk32::kMaterialPixelShaderBytes,
            &resolved),
        Status::KindMismatch,
        "vertex shader cannot resolve as pixel shader despite equal extent");
    ExpectStatus(
        registry.Resolve(
            Token(4, 4),
            AliasKind::MaterialVertexShader,
            disk32::kMaterialVertexShaderBytes,
            &resolved),
        Status::UnregisteredSlot,
        "material vertex shader interior is not an object start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialVertexShader,
            disk32::kMaterialVertexShaderBytes,
            &resolved),
        Status::Ok,
        "resolve exact completed material vertex shader start");
    Expect(resolved == blocks[4].base, "completed vertex shader address is exact");

    ExpectStatus(
        registry.Publish(
            pixelShader,
            AliasKind::MaterialPixelShader,
            blocks[4].base + disk32::kMaterialVertexShaderBytes,
            disk32::kMaterialPixelShaderBytes),
        Status::Ok,
        "publish exact completed material pixel shader start");
    ExpectStatus(
        registry.Resolve(
            Token(4, disk32::kMaterialVertexShaderBytes),
            AliasKind::MaterialPixelShader,
            0,
            &resolved),
        Status::MetadataMismatch,
        "material pixel shader disk metadata is exact");
    ExpectStatus(
        registry.Resolve(
            Token(4, disk32::kMaterialVertexShaderBytes),
            AliasKind::MaterialPixelShader,
            disk32::kMaterialPixelShaderBytes,
            &resolved),
        Status::Ok,
        "resolve exact completed material pixel shader start");
    Expect(
        resolved == blocks[4].base + disk32::kMaterialVertexShaderBytes,
        "completed pixel shader address is exact");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle materialWater;
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base,
            AliasKind::MaterialWater,
            &materialWater),
        Status::Ok,
        "register completed material water start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialWater,
            disk32::kMaterialWaterBytes,
            &resolved),
        Status::PendingSlot,
        "material water is pending before completion");
    ExpectStatus(
        registry.Publish(
            materialWater,
            AliasKind::MaterialWater,
            blocks[4].base + 4,
            disk32::kMaterialWaterBytes),
        Status::MetadataMismatch,
        "completed material water must publish its own start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialWater,
            disk32::kMaterialWaterBytes,
            &resolved),
        Status::PendingSlot,
        "failed material water publication remains pending");
    ExpectStatus(
        registry.Publish(
            materialWater,
            AliasKind::MaterialWater,
            blocks[4].base,
            disk32::kMaterialWaterBytes),
        Status::Ok,
        "publish exact completed material water start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialWater,
            0,
            &resolved),
        Status::MetadataMismatch,
        "material water disk metadata is exact");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialPixelShader,
            disk32::kMaterialPixelShaderBytes,
            &resolved),
        Status::KindMismatch,
        "material water cannot resolve as another completed material object");
    ExpectStatus(
        registry.Resolve(
            Token(4, 4),
            AliasKind::MaterialWater,
            disk32::kMaterialWaterBytes,
            &resolved),
        Status::UnregisteredSlot,
        "material water interior is not an object start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialWater,
            disk32::kMaterialWaterBytes,
            &resolved),
        Status::Ok,
        "resolve exact completed material water start");
    Expect(resolved == blocks[4].base, "completed material water address is exact");

    registry.Reset(blocks, db::relocation::kBlockCount);
    AliasHandle materialTextureTable;
    constexpr uint32_t twoTextureBytes =
        2 * disk32::kMaterialTextureDefBytes;
    ExpectStatus(
        registry.RegisterSlot(
            blocks[4].base,
            AliasKind::MaterialTextureTable,
            &materialTextureTable),
        Status::Ok,
        "register completed material texture-table start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialTextureTable,
            twoTextureBytes,
            &resolved),
        Status::PendingSlot,
        "material texture table is pending before completion");
    ExpectStatus(
        registry.Publish(
            materialTextureTable,
            AliasKind::MaterialTextureTable,
            blocks[4].base + 4,
            twoTextureBytes),
        Status::MetadataMismatch,
        "completed material texture table must publish its own start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialTextureTable,
            twoTextureBytes,
            &resolved),
        Status::PendingSlot,
        "failed material texture-table publication remains pending");
    ExpectStatus(
        registry.Publish(
            materialTextureTable,
            AliasKind::MaterialTextureTable,
            blocks[4].base,
            twoTextureBytes),
        Status::Ok,
        "publish exact completed material texture-table start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialTextureTable,
            disk32::kMaterialTextureDefBytes,
            &resolved),
        Status::MetadataMismatch,
        "material texture-table disk extent is exact");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialWater,
            disk32::kMaterialWaterBytes,
            &resolved),
        Status::KindMismatch,
        "material texture table cannot resolve as another material object");
    ExpectStatus(
        registry.Resolve(
            Token(4, 4),
            AliasKind::MaterialTextureTable,
            twoTextureBytes,
            &resolved),
        Status::UnregisteredSlot,
        "material texture-table interior is not an object start");
    ExpectStatus(
        registry.Resolve(
            Token(4, 0),
            AliasKind::MaterialTextureTable,
            twoTextureBytes,
            &resolved),
        Status::Ok,
        "resolve exact completed material texture-table start");
    Expect(
        resolved == blocks[4].base,
        "completed material texture-table address is exact");

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

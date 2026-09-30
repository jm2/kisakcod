// db_disk32_physpreset_tests.cpp: the 64-bit PhysPreset loader (NOW row 12)
// on hand-built disk32 zone images. The production stream code (db_stream.cpp,
// db_stream_load.cpp, db_relocation.cpp) runs against a synthetic zone; only
// the inflater (DB_LoadXFileData) and the asset pool (Load_PhysPresetAsset)
// are replaced. Retail data never enters tests (docs/ROADMAP.md).

#include <database/database.h>
#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <algorithm>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace
{
int g_failures = 0;

void Expect(bool ok, const char *what, const char *detail = "")
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s %s\n", what, detail);
        ++g_failures;
    }
}

// A production ERR_DROP longjmps and never returns; this seam throws instead.
struct Drop
{
    char message[256];
};

struct Zone;
const Zone *g_zone = nullptr;     // the zone the pool stub checks names against
std::vector<std::uint8_t> g_file; // the inflated fast-file bytes
std::size_t g_read = 0;
PhysPreset g_pool[4];             // what Load_PhysPresetAsset published
int g_published = 0;

constexpr std::uint32_t kInline = disk32::kInline;

constexpr std::uint32_t VirtualOffset(std::uint32_t offset)
{
    return ((4u << 28) | offset) + 1;
}

// The scalar fields of one record. Every value differs from every other, so a
// field read at a wrong offset (its native one, say) reads a wrong value.
struct Scalars
{
    std::int32_t type;
    float mass, bounce, friction, bulletForceScale, explosiveForceScale;
    float piecesSpreadFraction, piecesUpwardVelocity;
    std::uint8_t cylinder; // the disk byte; any nonzero value is true
};

constexpr Scalars kMetal{7, 1.5f, 0.25f, 0.75f, 2.5f, 3.5f, 0.125f, 4.5f, 0x02};
constexpr Scalars kWood{-3, 6.5f, 0.5f, 0.375f, 8.5f, 9.5f, 0.0625f, 10.5f, 0x00};

struct File
{
    File &Word(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            g_file.push_back(static_cast<std::uint8_t>(value >> shift));
        return *this;
    }
    File &Float(float value)
    {
        return Word(std::bit_cast<std::uint32_t>(value));
    }
    File &Text(std::string_view text)
    {
        g_file.insert(g_file.end(), text.begin(), text.end());
        g_file.push_back(0);
        return *this;
    }
    // The 44-byte retail record. The three padding bytes after the bool hold
    // junk, which the loader must ignore.
    File &Record(std::uint32_t name, const Scalars &s, std::uint32_t sndAliasPrefix)
    {
        Word(name).Word(static_cast<std::uint32_t>(s.type)).Float(s.mass).Float(s.bounce).Float(s.friction);
        Float(s.bulletForceScale).Float(s.explosiveForceScale).Word(sndAliasPrefix);
        Float(s.piecesSpreadFraction).Float(s.piecesUpwardVelocity);
        return Word(0xCDCDCD00u | s.cylinder);
    }
};

// A zone with the two blocks a PhysPreset touches: temp (0) and virtual (4).
struct Zone
{
    alignas(16) std::uint8_t temp[64]{};
    alignas(16) std::uint8_t virt[96]{};
    XZoneMemory memory{};

    Zone()
    {
        g_file.clear();
        g_read = 0;
        g_published = 0;
        g_zone = this;
        memory.blocks[0] = {temp, sizeof(temp)};
        memory.blocks[4] = {virt, sizeof(virt)};
        DB_InitStreams(&memory);
        DB_PushStreamPos(4); // DB_LoadXFile walks the asset array in block 4
    }
    ~Zone() { g_zone = nullptr; }
    // Whether text is a terminated string inside block 4. It reads nothing
    // outside the block, so a mislanded pointer fails here instead of crashing.
    bool Holds(const char *text) const
    {
        const auto at = reinterpret_cast<std::uintptr_t>(text);
        const auto begin = reinterpret_cast<std::uintptr_t>(virt);
        if (at < begin || at >= begin + sizeof(virt))
            return false;
        return std::memchr(text, 0, begin + sizeof(virt) - at) != nullptr;
    }
};

// Load_XAssetHeader's 64-bit call: the header slot holds the disk32 token.
PhysPreset *Load(std::uintptr_t slotValue)
{
    PhysPreset *slot = nullptr;
    std::memcpy(&slot, &slotValue, sizeof(slot));
    DB_LoadPhysPresetPtrDisk32(false, &slot);
    return slot;
}

bool Matches(const PhysPreset &loaded, const Scalars &s)
{
    std::uint8_t cylinderByte = 0xFF; // a converted bool is exactly 0 or 1
    std::memcpy(&cylinderByte, &loaded.tempDefaultToCylinder, 1);
    return loaded.type == s.type && loaded.mass == s.mass && loaded.bounce == s.bounce
        && loaded.friction == s.friction && loaded.bulletForceScale == s.bulletForceScale
        && loaded.explosiveForceScale == s.explosiveForceScale
        && loaded.piecesSpreadFraction == s.piecesSpreadFraction
        && loaded.piecesUpwardVelocity == s.piecesUpwardVelocity && cylinderByte == (s.cylinder != 0 ? 1 : 0);
}

void TestInlinePhysPresets()
{
    Zone zone;
    File().Record(kInline, kMetal, kInline).Text("physic/metal").Text("metal");
    File().Record(kInline, kWood, 0).Text("physic/wood");
    const PhysPreset *const metal = Load(kInline);
    Expect(metal == &g_pool[0] && g_published == 1, "inline preset publishes one pool entry");
    if (metal != &g_pool[0])
        return;
    Expect(Matches(*metal, kMetal), "every scalar converts from its retail offset; byte 0x02 is true");
    Expect(zone.Holds(metal->name) && !std::strcmp(metal->name, "physic/metal"),
           "name points at its bytes in block 4");
    Expect(zone.Holds(metal->sndAliasPrefix) && !std::strcmp(metal->sndAliasPrefix, "metal"),
           "sound-alias prefix points at its bytes in block 4");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::PhysPresetDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(DB_GetStreamPos() == zone.virt + 13 + 6, "block 4 advances by the two strings");

    const PhysPreset *const wood = Load(kInline);
    Expect(wood == &g_pool[1], "the second record publishes the second pool entry");
    if (wood != &g_pool[1])
        return;
    Expect(Matches(*wood, kWood) && !wood->sndAliasPrefix, "a zero byte is false and a null prefix stays null");
    Expect(zone.Holds(wood->name) && !std::strcmp(wood->name, "physic/wood") && g_read == g_file.size(),
           "every disk byte is consumed");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline, kMetal, kInline).Text("phys/shared").Text("wood") // after the 4-byte alias slot
        .Record(VirtualOffset(4), kWood, VirtualOffset(16));
    const PhysPreset *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Matches(g_pool[0], kMetal), "shared-inline preset publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const PhysPreset *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name && second->sndAliasPrefix == shared->sndAliasPrefix,
           "offset tokens resolve both strings to the earlier bytes");
    Expect(Matches(*second, kWood), "the second record keeps its own scalars");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
};

void RunOff()
{
    for (int i = 0; i < 30; ++i)
        File().Word(0x42424242);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Record(kInline, kMetal, kInline); g_file.resize(40); },
     kInline, "ended unexpectedly"},
    {"null name", [] { File().Record(0, kMetal, kInline).Text("metal"); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(8), kMetal, 0); }, kInline, "string offset"},
    {"unmapped prefix offset", [] { File().Record(kInline, kMetal, VirtualOffset(40)).Text("a"); },
     kInline, "string offset"},
    {"shared-inline string token", [] { File().Record(kInline, kMetal, disk32::kSharedInline).Text("a"); },
     kInline, "string offset"},
    {"unterminated name", [] { File().Record(kInline, kMetal, 0); RunOff(); }, kInline, "Unterminated"},
    {"unterminated prefix", [] { File().Record(kInline, kMetal, kInline).Text("a"); RunOff(); },
     kInline, "Unterminated"},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        test.build();
        Drop drop{"(none)"};
        try
        {
            Load(test.slot);
        }
        catch (const Drop &caught)
        {
            drop = caught;
        }
        Expect(std::strstr(drop.message, test.error) && g_published == 0 && g_read <= g_file.size(),
               test.what, drop.message);
    }
}
} // namespace

// Engine seams: the error handler, the inflater and the asset pool.
void __cdecl Com_Error(errorParm_t code, const char *fmt, ...)
{
    Drop drop{};
    va_list args;
    va_start(args, fmt);
    // Flawfinder: ignore -- the engine's literal formats into a bounded, terminated buffer.
    std::vsnprintf(drop.message, sizeof(drop.message), fmt, args);
    va_end(args);
    if (code != ERR_DROP)
        std::snprintf(drop.message, sizeof(drop.message), "unexpected error code %d", code);
    throw drop;
}

void __cdecl DB_LoadXFileData(std::uint8_t *pos, std::uint32_t size)
{
    if (!pos || !size || size > g_file.size() - g_read)
        Com_Error(ERR_DROP, "Fast-file ended unexpectedly");
    std::copy_n(g_file.data() + g_read, size, pos);
    g_read += size;
    if (DB_MarkStreamRangeMaterialized(pos, size) != db::relocation::Status::Ok)
        Com_Error(ERR_DROP, "Cannot record fast-file output range");
}

void __cdecl Load_PhysPresetAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    PhysPreset &entry = g_pool[g_published++];
    entry = *header->physPreset;
    Expect(g_zone && g_zone->Holds(entry.name) && entry.name[0] != '\0', "a published preset has a name in block 4");
    header->physPreset = &entry;
}

// Script-string interning is not reached by PhysPreset; db_stream_load.cpp links it.
db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *, std::uint32_t, LegacyBridgeStringId *) noexcept
{
    return LegacyBridgeStatus::InvalidState;
}

int main()
{
    for (void (*test)() : {TestInlinePhysPresets, TestSharedInlineAndOffsets, TestMalformedFailsClosed})
    {
        try
        {
            test();
        }
        catch (const Drop &drop)
        {
            Expect(false, "a well-formed image raised ERR_DROP:", drop.message);
        }
    }
    return g_failures ? 1 : 0;
}

// db_disk32_physpreset_tests.cpp: the 64-bit PhysPreset loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_PhysPresetAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

struct Zone;
const Zone *g_zone = nullptr; // the zone the pool stub checks names against
PhysPreset g_pool[4];         // what Load_PhysPresetAsset published

// The scalar fields of one record. Every value differs from every other, so a
// field read at a wrong offset (its native one, say) reads a wrong value.
struct Scalars
{
    std::int32_t type;
    float mass;
    float bounce;
    float friction;
    float bulletForceScale;
    float explosiveForceScale;
    float piecesSpreadFraction;
    float piecesUpwardVelocity;
    std::uint8_t cylinder; // the disk byte; any nonzero value is true
};

constexpr Scalars kMetal{7, 1.5f, 0.25f, 0.75f, 2.5f, 3.5f, 0.125f, 4.5f, 0x02};
constexpr Scalars kWood{-3, 6.5f, 0.5f, 0.375f, 8.5f, 9.5f, 0.0625f, 10.5f, 0x00};

struct File : FileBuilder<File>
{
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
struct Zone : disk32_test::Zone<96>
{
    Zone() { g_zone = this; }
    ~Zone() { g_zone = nullptr; }
};

PhysPreset *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadPhysPresetPtrDisk32, slotValue);
}

bool Matches(const PhysPreset &loaded, const Scalars &s)
{
    // A converted bool is exactly 0 or 1. Its byte is read as unsigned char,
    // so an invalid bool value is seen rather than loaded.
    const unsigned char cylinderByte = *reinterpret_cast<const unsigned char *>(&loaded.tempDefaultToCylinder);
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
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_PhysPresetAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    PhysPreset &entry = g_pool[g_published++];
    entry = *header->physPreset;
    Expect(g_zone && g_zone->Holds(entry.name) && entry.name[0] != '\0', "a published preset has a name in block 4");
    header->physPreset = &entry;
}

int main()
{
    return Run({TestInlinePhysPresets, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}

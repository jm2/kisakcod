// db_disk32_mapents_tests.cpp: the 64-bit MapEnts loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_MapEntsAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <string_view>

namespace
{
using namespace disk32_test;

MapEnts g_pool[4]; // what Load_MapEntsAsset published

// Entity strings as the linker writes them: numEntityChars counts the NUL.
constexpr std::string_view kWorld = "{\n\"classname\" \"worldspawn\"\n}\n";
constexpr std::string_view kSpawn = "{\n\"classname\" \"mp_tdm_spawn\"\n}\n";
constexpr auto Chars(std::string_view text)
{
    return static_cast<std::int32_t>(text.size() + 1);
}

struct File : FileBuilder<File>
{
    // The 12-byte retail record.
    File &Record(std::uint32_t name, std::uint32_t entityString, std::int32_t numEntityChars)
    {
        return Word(name).Word(entityString).Word(static_cast<std::uint32_t>(numEntityChars));
    }
};

// A zone with the two blocks a MapEnts touches: temp (0) and virtual (4).
struct Zone : disk32_test::Zone<128>
{
    using disk32_test::Zone<128>::Zone;
    bool Is(const char *text, std::string_view expected) const
    {
        return Holds(text) && text == expected;
    }
};

MapEnts *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadMapEntsPtrDisk32, slotValue);
}

void TestInlineMapEnts()
{
    Zone zone;
    File().Record(kInline, kInline, Chars(kWorld)).Text("maps/mp/mp_test.d3dbsp").Text(kWorld);
    File().Record(kInline, 0, 0).Text("maps/mp/mp_empty.d3dbsp");
    const MapEnts *const ents = Load(kInline);
    Expect(ents == &g_pool[0] && g_published == 1, "inline map entities publish one pool entry");
    if (ents != &g_pool[0])
        return;
    Expect(zone.Is(ents->name, "maps/mp/mp_test.d3dbsp") && ents->name == reinterpret_cast<char *>(zone.virt),
           "name streams first and points at its bytes in block 4");
    Expect(ents->entityString == ents->name + 23 && zone.Is(ents->entityString, kWorld),
           "the entity string follows the name in block 4");
    Expect(ents->numEntityChars == Chars(kWorld), "numEntityChars converts from its retail offset");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::MapEntsDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(DB_GetStreamPos() == zone.virt + 23 + Chars(kWorld), "block 4 advances by the name and the entity string");

    const MapEnts *const empty = Load(kInline);
    Expect(empty == &g_pool[1], "the second record publishes the second pool entry");
    if (empty != &g_pool[1])
        return;
    Expect(zone.Is(empty->name, "maps/mp/mp_empty.d3dbsp") && !empty->entityString && !empty->numEntityChars
               && g_read == g_file.size(),
           "a null entity string with no characters stays null");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline, kInline, Chars(kWorld)).Text("mp_shared").Text(kWorld) // after the 4-byte alias slot
        // The 32-bit loader streams any non-null entity token inline.
        .Record(VirtualOffset(4), VirtualOffset(99), Chars(kSpawn)).Text(kSpawn);
    const MapEnts *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && zone.Is(g_pool[0].entityString, kWorld), "shared-inline map entities publish");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const MapEnts *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name, "a name offset token resolves to the earlier string");
    Expect(zone.Is(second->entityString, kSpawn) && second->numEntityChars == Chars(kSpawn),
           "an offset-valued entity token streams its bytes inline");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::uint32_t tempBytes = 64;
};

void RunOff()
{
    for (int i = 0; i < 40; ++i)
        File().Word(0x42424242);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(kInline); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kInline, 0, 0).Text("a"); },
     kInline, "exceeds stream block", 8},
    {"truncated entity string", [] { File().Record(kInline, kInline, 40).Text("a").Text("{"); },
     kInline, "ended unexpectedly"},
    {"entity string past its block", [] { File().Record(kInline, kInline, 200).Text("a"); RunOff(); },
     kInline, "exceeds stream block"},
    {"unterminated entity string", [] { File().Record(kInline, kInline, 4).Text("a").Word(0x7D0A7D7B); },
     kInline, "not terminated"},
    {"zero-length entity string", [] { File().Record(kInline, kInline, 0).Text("a"); }, kInline, "entity-string length"},
    {"negative length", [] { File().Record(kInline, kInline, -1).Text("a"); }, kInline, "entity-string length"},
    {"characters without a string", [] { File().Record(kInline, 0, 5).Text("a"); }, kInline, "no entity string"},
    {"null name", [] { File().Record(0, kInline, Chars(kWorld)).Text(kWorld); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(8), 0, 0); }, kInline, "string offset"},
    {"name runs off its block", [] { File().Record(kInline, 0, 0); RunOff(); }, kInline, "Unterminated"},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        test.build();
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_MapEntsAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    MapEnts &entry = g_pool[g_published++];
    entry = *header->mapEnts;
    Expect(entry.name && entry.name[0] != '\0', "published map entities have a name");
    header->mapEnts = &entry;
}

int main()
{
    return Run({TestInlineMapEnts, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}

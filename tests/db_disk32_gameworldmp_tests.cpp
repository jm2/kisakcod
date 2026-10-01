// db_disk32_gameworldmp_tests.cpp: the 64-bit GameWorldMp loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_GameWorldMpAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

GameWorldMp g_pool[4]; // what Load_GameWorldMpAsset published

struct File : FileBuilder<File>
{
    // The 4-byte retail record: the name token.
    File &Record(std::uint32_t name)
    {
        return Word(name);
    }
};

// A zone with the two blocks a GameWorldMp touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<96>;

GameWorldMp *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadGameWorldMpPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

void TestInlineWorlds()
{
    Zone zone;
    File().Record(kInline).Text("maps/mp/mp_crash.d3dbsp");
    File().Record(kInline).Text("maps/mp/mp_strike.d3dbsp");
    const GameWorldMp *const crash = Load(kInline);
    Expect(crash == &g_pool[0] && g_published == 1, "inline world publishes one pool entry");
    if (crash != &g_pool[0])
        return;
    Expect(Is(zone, crash->name, "maps/mp/mp_crash.d3dbsp") && crash->name == reinterpret_cast<char *>(zone.virt),
           "name points at its bytes in block 4");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::GameWorldMpDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(DB_GetStreamPos() == zone.virt + 24, "block 4 advances by the name");

    const GameWorldMp *const strike = Load(kInline);
    Expect(strike == &g_pool[1], "the second record publishes the second pool entry");
    if (strike != &g_pool[1])
        return;
    Expect(Is(zone, strike->name, "maps/mp/mp_strike.d3dbsp") && strike->name == crash->name + 24
               && g_read == g_file.size(),
           "the second name follows the first and every disk byte is consumed");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline).Text("maps/mp/mp_shared.d3dbsp") // after the 4-byte alias slot
        .Record(VirtualOffset(4));
    const GameWorldMp *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Is(zone, g_pool[0].name, "maps/mp/mp_shared.d3dbsp"),
           "shared-inline world publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const GameWorldMp *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name, "a name offset token resolves to the earlier string");
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
    for (int i = 0; i < 30; ++i)
        File().Word(0x42424242);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Text("ab"); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kInline).Text("a"); }, kInline, "exceeds stream block", 2},
    {"null name", [] { File().Record(0); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(40)); }, kInline, "string offset"},
    {"shared-inline name token", [] { File().Record(disk32::kSharedInline).Text("a"); }, kInline, "string offset"},
    {"unterminated name", [] { File().Record(kInline); RunOff(); }, kInline, "Unterminated"},
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

void __cdecl Load_GameWorldMpAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    GameWorldMp &entry = g_pool[g_published++];
    entry = *header->gameWorldMp;
    Expect(entry.name && entry.name[0] != '\0', "a published world has a name");
    header->gameWorldMp = &entry;
}

int main()
{
    return Run({TestInlineWorlds, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}

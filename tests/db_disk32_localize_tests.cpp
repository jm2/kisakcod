// db_disk32_localize_tests.cpp: the 64-bit LocalizeEntry loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_LocalizeEntryAsset) is replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

LocalizeEntry g_pool[4]; // what Load_LocalizeEntryAsset published

struct File : FileBuilder<File>
{
    // The 8-byte retail record: value, then name.
    File &Record(std::uint32_t value, std::uint32_t name)
    {
        return Word(value).Word(name);
    }
};

// A zone with the two blocks a LocalizeEntry touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<96>;

LocalizeEntry *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadLocalizeEntryPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

void TestInlineEntries()
{
    Zone zone;
    File().Record(kInline, kInline).Text("Press ^3[USE]^7 to plant").Text("MPUI_PLANT");
    File().Record(0, kInline).Text("MPUI_UNTRANSLATED");
    const LocalizeEntry *const plant = Load(kInline);
    Expect(plant == &g_pool[0] && g_published == 1, "inline entry publishes one pool entry");
    if (plant != &g_pool[0])
        return;
    Expect(Is(zone, plant->value, "Press ^3[USE]^7 to plant") && plant->value == reinterpret_cast<char *>(zone.virt),
           "value streams first and points at its bytes in block 4");
    Expect(Is(zone, plant->name, "MPUI_PLANT"), "name streams after the value");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::LocalizeEntryDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(DB_GetStreamPos() == zone.virt + 25 + 11, "block 4 advances by the two strings");

    const LocalizeEntry *const untranslated = Load(kInline);
    Expect(untranslated == &g_pool[1], "the second record publishes the second pool entry");
    if (untranslated != &g_pool[1])
        return;
    Expect(!untranslated->value && Is(zone, untranslated->name, "MPUI_UNTRANSLATED") && g_read == g_file.size(),
           "a null value stays null and every disk byte is consumed");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline, kInline).Text("Game over").Text("MP_GAME_OVER") // after the 4-byte alias slot
        .Record(VirtualOffset(4), VirtualOffset(14));
    const LocalizeEntry *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Is(zone, g_pool[0].name, "MP_GAME_OVER"), "shared-inline entry publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const LocalizeEntry *const second = Load(kInline);
    Expect(second == &g_pool[1] && g_read == g_file.size(), "the second record streams after the first");
    if (second != &g_pool[1])
        return;
    Expect(second->value == shared->value && second->name == shared->name,
           "offset tokens resolve both strings to the earlier bytes");
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
    {"truncated record", [] { File().Word(kInline); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kInline, kInline).Text("a").Text("b"); },
     kInline, "exceeds stream block", 4},
    {"null name", [] { File().Record(kInline, 0).Text("orphan"); }, kInline, "no name"},
    {"unmapped value offset", [] { File().Record(VirtualOffset(8), kInline).Text("A"); }, kInline, "string offset"},
    {"unmapped name offset", [] { File().Record(kInline, VirtualOffset(40)).Text("a"); }, kInline, "string offset"},
    {"shared-inline string token", [] { File().Record(disk32::kSharedInline, kInline).Text("A"); },
     kInline, "string offset"},
    {"unterminated value", [] { File().Record(kInline, kInline); RunOff(); }, kInline, "Unterminated"},
    {"unterminated name", [] { File().Record(0, kInline); RunOff(); }, kInline, "Unterminated"},
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

void __cdecl Load_LocalizeEntryAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    LocalizeEntry &entry = g_pool[g_published++];
    entry = *header->localize;
    Expect(entry.name && entry.name[0] != '\0', "a published entry has a name");
    header->localize = &entry;
}

int main()
{
    return Run({TestInlineEntries, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}

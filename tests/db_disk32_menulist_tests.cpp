// db_disk32_menulist_tests.cpp: the 64-bit MenuList loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_MenuListAsset) is replaced; the menu
// pointers live in the fixture's native storage.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>

namespace
{
using namespace disk32_test;

MenuList g_pool[4]; // what Load_MenuListAsset published

struct File : FileBuilder<File>
{
    // The 12-byte retail record.
    File &Record(std::uint32_t name, std::int32_t menuCount, std::uint32_t menus)
    {
        return Word(name).Word(static_cast<std::uint32_t>(menuCount)).Word(menus);
    }
    // count menu tokens, every one null but index `at`, which holds `token`.
    File &Menus(std::int32_t count, std::int32_t at = -1, std::uint32_t token = 0)
    {
        for (std::int32_t index = 0; index < count; ++index)
            Word(index == at ? token : 0);
        return *this;
    }
};

// A zone with the two blocks a menu list touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<256>;

MenuList *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadMenuListPtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

void TestInlineList()
{
    Zone zone;
    // Block 4: the name (0..6), then three menu tokens 4-aligned at 8.
    File().Record(kInline, 3, kInline).Text("menus").Menus(3);
    const MenuList *const list = Load(kInline);
    Expect(list == &g_pool[0] && g_published == 1, "an inline list publishes one pool entry");
    if (list != &g_pool[0])
        return;
    Expect(Is(zone, list->name, "menus") && list->name == zone.At(0) && list->menuCount == 3,
           "the name points at its bytes in block 4 and the count converts");
    Expect(InArena(list->menus) && g_arenaUsed == 3 * sizeof(menuDef_t *),
           "the three native 8-byte menu pointers live in native storage, above 4 GiB");
    Expect(InArena(list->menus) && !list->menus[0] && !list->menus[1] && !list->menus[2],
           "every null menu token converts to a null pointer");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::MenuListDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 8 + 3 * 4,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestEmptyAndAbsentMenus()
{
    Zone zone;
    File().Record(kInline, 0, kInline).Text("empty"); // name 0..6
    File().Record(kInline, 5, 0).Text("none");        // name 8..13
    const MenuList *const empty = Load(kInline);
    const MenuList *const none = Load(kInline);
    Expect(empty == &g_pool[0] && none == &g_pool[1], "both lists publish");
    if (empty != &g_pool[0] || none != &g_pool[1])
        return;
    Expect(empty->menus == reinterpret_cast<menuDef_t **>(zone.virt + 8) && !empty->menuCount,
           "an empty inline list still points at its 4-aligned stream position, as on x86");
    Expect(!none->menus && none->menuCount == 5 && g_arenaUsed == 0 && g_read == g_file.size(),
           "a null menus token loads nothing, whatever its count, and takes no native storage");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    // Block 4: the alias slot (0), the name (4..10), then one token at 12.
    File().Record(kInline, 1, kInline).Text("shared").Menus(1);
    File().Record(VirtualOffset(4), 0, 0);
    const MenuList *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Is(zone, g_pool[0].name, "shared"), "a shared-inline list publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const MenuList *const second = Load(kInline);
    Expect(second == &g_pool[1] && second->name == shared->name && !second->menus,
           "a name offset token resolves to the earlier string");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 16, "block 4 holds one list");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::size_t arena = kArenaBytes;
    std::uint32_t tempBytes = 128;
};

void RunOff()
{
    for (int i = 0; i < 70; ++i) // 280 bytes: past the 256-byte block
        File().Word(0x42424242);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline).Word(1); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kInline, 0, 0).Text("a"); }, kInline, "exceeds stream block",
     kArenaBytes, 8},
    {"null name", [] { File().Record(0, 0, 0); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(40), 0, 0); }, kInline, "string offset"},
    {"name runs off its block", [] { File().Record(kInline, 0, 0); RunOff(); }, kInline, "Unterminated"},
    {"negative menu count", [] { File().Record(kInline, -1, kInline).Text("a"); }, kInline, "menu count"},
    {"menu bytes overflow", [] { File().Record(kInline, 0x20000000, kInline).Text("a"); }, kInline, "menu count"},
    {"menus past their block", [] { File().Record(kInline, 100, kInline).Text("a"); RunOff(); }, kInline,
     "exceeds stream block"},
    {"truncated menus", [] { File().Record(kInline, 3, kInline).Text("a").Menus(2); }, kInline, "ended unexpectedly"},
    {"an inline menu", [] { File().Record(kInline, 3, kInline).Text("a").Menus(3, 0, kInline); }, kInline,
     "names a menu"},
    {"a shared-inline menu", [] { File().Record(kInline, 3, kInline).Text("a").Menus(3, 1, disk32::kSharedInline); },
     kInline, "names a menu"},
    {"a menu offset last", [] { File().Record(kInline, 3, kInline).Text("a").Menus(3, 2, VirtualOffset(0)); },
     kInline, "names a menu"},
    {"native storage exhausted", [] { File().Record(kInline, 3, kInline).Text("a").Menus(3); }, kInline,
     "exhausted", 3 * sizeof(menuDef_t *) - 1},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone(test.tempBytes);
        g_arenaCapacity = test.arena;
        test.build();
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
}
} // namespace

void __cdecl Load_MenuListAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    MenuList &entry = g_pool[g_published++];
    entry = *header->menuList;
    Expect(entry.name && entry.name[0] != '\0', "a published list has a name");
    header->menuList = &entry;
}

int main()
{
    return Run({TestInlineList, TestEmptyAndAbsentMenus, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}

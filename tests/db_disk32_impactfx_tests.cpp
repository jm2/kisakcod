// db_disk32_impactfx_tests.cpp: the 64-bit ImpactFx loader (NOW row 12) on
// hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_FxImpactTableAsset) is replaced; the
// entries live in the fixture's native storage.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <cstring>
#include <string>

namespace
{
using namespace disk32_test;

FxImpactTable g_pool[4]; // what Load_FxImpactTableAsset published

constexpr std::size_t kEntries = 12;
constexpr std::size_t kSlots = 29 + 4; // nonflesh, then flesh
constexpr std::size_t kEntryBytes = sizeof(disk32::FxImpactEntryDisk32);
constexpr std::size_t kTableBytes = kEntries * kEntryBytes;

struct File : FileBuilder<File>
{
    // The 8-byte retail record.
    File &Record(std::uint32_t name, std::uint32_t table) { return Word(name).Word(table); }
    // The 12 retail entries: every effect slot null but slot `at` (an entry
    // index times kSlots plus a slot), which holds `token`.
    File &Entries(std::size_t at = kEntries * kSlots, std::uint32_t token = 0)
    {
        for (std::size_t slot = 0; slot < kEntries * kSlots; ++slot)
            Word(slot == at ? token : 0);
        return *this;
    }
};

// A zone with the two blocks an impact table touches: temp (0) and virtual (4).
using Zone = disk32_test::Zone<2048>;

FxImpactTable *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadFxImpactTablePtrDisk32, slotValue);
}

bool Is(const Zone &zone, const char *text, const char *expected)
{
    return zone.Holds(text) && !std::strcmp(text, expected);
}

// Every native effect slot of every entry is null (native storage starts as junk).
bool AllNull(const FxImpactEntry *table)
{
    for (std::size_t entry = 0; entry < kEntries; ++entry)
    {
        for (const FxEffectDef *effect : table[entry].nonflesh)
            if (effect)
                return false;
        for (const FxEffectDef *effect : table[entry].flesh)
            if (effect)
                return false;
    }
    return true;
}

void TestInlineTables()
{
    Zone zone;
    // Block 4: the name (0..7), the entries 4-aligned at 8, then "bare".
    File().Record(kInline, kInline).Text("fx_imp").Entries();
    File().Record(kInline, 0).Text("bare");
    const FxImpactTable *const table = Load(kInline);
    Expect(table == &g_pool[0] && g_published == 1, "an inline table publishes one pool entry");
    if (table != &g_pool[0])
        return;
    Expect(Is(zone, table->name, "fx_imp") && table->name == zone.At(0), "the name points at its bytes in block 4");
    Expect(InArena(table->table) && g_arenaUsed == kEntries * sizeof(FxImpactEntry),
           "the 12 native 264-byte entries live in native storage, above 4 GiB");
    Expect(InArena(table->table) && AllNull(table->table), "every null effect token converts to a null pointer");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::FxImpactTableDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(!std::memcmp(zone.virt + 8, g_file.data() + 8 + 7, kTableBytes),
           "the disk32 entries stay at their 4-aligned retail block-4 offset");
    const FxImpactTable *const bare = Load(kInline);
    Expect(bare == &g_pool[1] && !bare->table && Is(zone, bare->name, "bare")
               && g_arenaUsed == kEntries * sizeof(FxImpactEntry),
           "a null table token loads no entries and takes no native storage");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 8 + kTableBytes + 5,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    // Block 4: the alias slot (0), the name (4..12), then the entries at 12.
    File().Record(kInline, kInline).Text("fx_shrd").Entries();
    File().Record(VirtualOffset(4), 0);
    const FxImpactTable *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && Is(zone, g_pool[0].name, "fx_shrd"), "a shared-inline table publishes");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const FxImpactTable *const second = Load(kInline);
    Expect(second == &g_pool[1] && second->name == shared->name && !second->table,
           "a name offset token resolves to the earlier string");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 12 + kTableBytes, "block 4 holds one table");
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
    for (int i = 0; i < 520; ++i) // 2080 bytes: past the 2048-byte block
        File().Word(0x42424242);
}

const Malformed kMalformed[] = {
    {"truncated record", [] { File().Word(kInline); }, kInline, "ended unexpectedly"},
    {"record past the temp block", [] { File().Record(kInline, 0).Text("a"); }, kInline, "exceeds stream block",
     kArenaBytes, 4},
    {"null name", [] { File().Record(0, 0); }, kInline, "have no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(40), 0); }, kInline, "string offset"},
    {"name runs off its block", [] { File().Record(kInline, 0); RunOff(); }, kInline, "Unterminated"},
    {"an inline nonflesh effect", [] { File().Record(kInline, kInline).Text("a").Entries(0, kInline); }, kInline,
     "FX effect"},
    {"a shared-inline effect mid-table", [] { File().Record(kInline, kInline).Text("a").Entries(5 * kSlots + 17,
                                                                                          disk32::kSharedInline); },
     kInline, "FX effect"},
    {"an offset in the last flesh slot",
     [] { File().Record(kInline, kInline).Text("a").Entries(kEntries * kSlots - 1, VirtualOffset(0)); }, kInline,
     "FX effect"},
    {"truncated entries", [] { File().Record(kInline, kInline).Text("a").Word(0).Word(0); }, kInline,
     "ended unexpectedly"},
    {"entries past their block", [] { File().Record(kInline, kInline).Text(std::string(600, 'n')).Entries(); },
     kInline, "exceeds stream block"},
    {"native storage exhausted", [] { File().Record(kInline, kInline).Text("a").Entries(); }, kInline, "exhausted",
     kEntries * sizeof(FxImpactEntry) - 1},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset"},
    {"alias token in the temp block", [] {}, 1, "alias offset"},
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

void __cdecl Load_FxImpactTableAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    FxImpactTable &entry = g_pool[g_published++];
    entry = *header->impactFx;
    Expect(entry.name && entry.name[0] != '\0', "a published table has a name");
    header->impactFx = &entry;
}

int main()
{
    return Run({TestInlineTables, TestSharedInlineAndOffsets, TestMalformedFailsClosed});
}

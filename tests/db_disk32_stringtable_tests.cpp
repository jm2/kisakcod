// db_disk32_stringtable_tests.cpp: the 64-bit StringTable loader (NOW row 12)
// on hand-built disk32 zone images (disk32_fixture.hpp). Beyond the fixture's
// seams, only the asset pool (Load_StringTableAsset) and the zone's native
// storage (DB_AllocZoneNative) are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>

#include <algorithm>
#include <cstring>

namespace
{
using namespace disk32_test;

StringTable g_pool[4]; // what Load_StringTableAsset published

// The zone's native storage. As a static it lies above 4 GiB, so a pointer
// narrowed to 32 bits cannot land back on it.
constexpr std::size_t kArenaBytes = 256;
alignas(16) std::uint8_t g_arena[kArenaBytes];
std::size_t g_arenaUsed = 0;
std::size_t g_arenaCapacity = kArenaBytes;

struct File : FileBuilder<File>
{
    File &Record(std::uint32_t name, std::int32_t columns, std::int32_t rows, std::uint32_t values)
    {
        return Word(name).Word(static_cast<std::uint32_t>(columns)).Word(static_cast<std::uint32_t>(rows)).Word(values);
    }
};

// A zone with a temp block and the virtual block (4) a StringTable streams
// into, with empty native storage.
struct Zone : disk32_test::Zone<16, 256>
{
    Zone()
    {
        g_arenaUsed = 0;
        g_arenaCapacity = kArenaBytes;
    }
};

bool InArena(const void *pointer)
{
    const auto *bytes = static_cast<const std::uint8_t *>(pointer);
    return bytes >= g_arena && bytes < g_arena + g_arenaUsed
        && reinterpret_cast<std::uintptr_t>(pointer) > UINT32_MAX;
}

StringTable *Load(std::uintptr_t slotValue)
{
    return LoadHeader(DB_LoadStringTablePtrDisk32, slotValue);
}

// TestInlineTable's four values: each points at its string in block 4.
void ExpectInlineValues(const Zone &zone, const char *const *values)
{
    const char *const expected[] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; ++i)
        Expect(values[i] == zone.At(48 + 2 * i) && !std::strcmp(values[i], expected[i]),
               "each value points at its inline string in block 4");
}

void TestInlineTable()
{
    Zone zone;
    File()
        .Record(kInline, 2, 2, kInline).Text("mp/table.csv") // record 0..16, name 16..29
        .Word(kInline).Word(kInline).Word(kInline).Word(kInline) // tokens 32..48
        .Text("a").Text("b").Text("c").Text("d");                // strings 48..56
    const StringTable *const table = Load(kInline);
    Expect(table == &g_pool[0] && g_published == 1, "inline table publishes one pool entry");
    if (table != &g_pool[0])
        return;
    Expect(table->name == zone.At(16) && !std::strcmp(table->name, "mp/table.csv"),
           "name points at its bytes in block 4");
    Expect(table->columnCount == 2 && table->rowCount == 2, "counts convert from the mirror");
    Expect(InArena(table->values) && g_arenaUsed == sizeof(StringTable) + 4 * sizeof(const char *),
           "the 8-byte value pointers live in native storage above 4 GiB");
    if (InArena(table->values))
        ExpectInlineValues(zone, table->values);
    Expect(!std::memcmp(zone.virt, g_file.data(), 16) && !std::memcmp(zone.virt + 32, g_file.data() + 29, 16),
           "the record and the token array stay at their retail offsets");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 56,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

// An alias token names the first table's record: it must resolve to that
// table's native twin, never to the disk32 bytes or the pool entry.
void ExpectAliasOf(const StringTable *first)
{
    const StringTable *const alias = Load(VirtualOffset(0));
    Expect(InArena(alias) && alias != first && g_published == 1,
           "an alias token resolves to the native table, above 4 GiB, and publishes nothing");
    if (InArena(alias))
        Expect(alias->name == first->name && alias->values == first->values
                   && alias->rowCount == 3 && alias->columnCount == 1,
               "the aliased native table holds the converted fields");
}

void ExpectSecondTable(const Zone &zone, const StringTable *first)
{
    const StringTable *const second = Load(kInline);
    Expect(second == &g_pool[1], "the second table publishes the second pool entry");
    if (second != &g_pool[1] || !InArena(second->values))
        return;
    Expect(second->name == first->values[0] && second->values[0] == first->name,
           "name and value offset tokens resolve to strings of the first table");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 64,
           "the second table streams after the first");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

void TestOffsetsAndAlias()
{
    Zone zone;
    File()
        .Record(kInline, 1, 3, kInline).Text("first.csv")         // record 0..16, name 16..26
        .Word(kInline).Word(VirtualOffset(16)).Word(kInline)      // tokens 28..40
        .Text("x").Text("y")                                      // strings 40..44
        .Record(VirtualOffset(40), 1, 1, kInline)                 // record 44..60
        .Word(VirtualOffset(16));                                 // tokens 60..64
    const StringTable *const first = Load(kInline);
    Expect(first == &g_pool[0], "the first table publishes the first pool entry");
    if (first != &g_pool[0] || !InArena(first->values))
        return;
    Expect(first->values[0] == zone.At(40) && first->values[2] == zone.At(42),
           "inline values point at their strings");
    Expect(first->values[1] == first->name, "a value offset token resolves to the earlier name");
    ExpectAliasOf(first);
    ExpectSecondTable(zone, first);
}

void TestEmptyTables()
{
    Zone zone;
    File()
        .Record(kInline, 0, 5, 0).Text("empty.csv") // record 0..16, name 16..26
        .Record(kInline, 0, 0, 0).Text("none.csv"); // record 28..44, name 44..53
    const StringTable *const noColumns = Load(kInline);
    const StringTable *const noCells = Load(kInline);
    Expect(noColumns == &g_pool[0] && noCells == &g_pool[1], "empty tables publish");
    if (noColumns != &g_pool[0] || noCells != &g_pool[1])
        return;
    Expect(!noColumns->values && noColumns->rowCount == 5 && !noColumns->columnCount && !noCells->values,
           "an empty table has no values array");
    Expect(noCells->name == zone.At(44) && g_arenaUsed == 2 * sizeof(StringTable) && g_read == g_file.size(),
           "an empty table takes only its native header");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
    std::size_t arena = kArenaBytes;
};

const Malformed kMalformed[] = {
    {"count overflow", [] { File().Record(kInline, 0x10000, 0x10000, kInline); }, kInline, "string-table size"},
    {"token bytes overflow", [] { File().Record(kInline, 1, 0x20000000, kInline).Text("t"); },
     kInline, "string-table size"},
    {"negative rows", [] { File().Record(kInline, 2, -1, kInline); }, kInline, "string-table size"},
    {"negative columns, no rows", [] { File().Record(kInline, -3, 0, 0); }, kInline, "string-table size"},
    {"cells without values", [] { File().Record(kInline, 1, 1, 0).Text("t"); }, kInline, "string-table values"},
    {"values without cells", [] { File().Record(kInline, 0, 1, kInline).Text("t"); },
     kInline, "string-table values"},
    {"null name", [] { File().Record(0, 0, 0, 0); }, kInline, "no name"},
    {"empty name", [] { File().Record(kInline, 0, 0, 0).Text(""); }, kInline, "no name"},
    {"truncated array", [] { File().Record(kInline, 1, 4, kInline).Text("t").Word(kInline).Word(kInline); },
     kInline, "ended unexpectedly"},
    {"array past its block", [] { File().Record(kInline, 1, 100, kInline).Text("t"); },
     kInline, "exceeds stream block"},
    {"unmapped string offset", [] { File().Record(kInline, 1, 1, kInline).Text("t").Word(VirtualOffset(8)); },
     kInline, "string offset"},
    {"shared-inline value", [] { File().Record(kInline, 1, 1, kInline).Text("t").Word(disk32::kSharedInline); },
     kInline, "string offset"},
    {"unterminated value",
     [] { File().Record(kInline, 1, 1, kInline).Text("t").Word(kInline); for (int i = 0; i < 80; ++i) File().Word(0x42424242); },
     kInline, "Unterminated"},
    {"native storage exhausted",
     [] { File().Record(kInline, 1, 2, kInline).Text("t").Word(kInline).Word(kInline).Text("a").Text("b"); },
     kInline, "exhausted", sizeof(StringTable) + sizeof(const char *)},
    {"unmapped alias", [] {}, VirtualOffset(16), "alias offset: unregistered slot"},
    {"shared-inline header", [] {}, disk32::kSharedInline, "alias offset: invalid token"},
    {"slot wider than a token", [] {}, std::uintptr_t{1} << 32, "no disk32 token"},
};

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        g_arenaCapacity = test.arena;
        test.build();
        ExpectDrop(test.what, test.error, [&] { Load(test.slot); });
    }
    Zone zone;
    StringTable *slot = nullptr;
    const Drop drop = Catch([&] { DB_LoadStringTablePtrDisk32(true, &slot); });
    Expect(std::strstr(drop.message, "header request") && !slot, "a header is never at the stream start");
}
} // namespace

// Engine seams beyond the fixture's: the asset pool and the zone's native storage.
void __cdecl Load_StringTableAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    StringTable &entry = g_pool[g_published++];
    entry = *header->stringTable;
    Expect(entry.name && entry.name[0] != '\0', "a published string table has a name");
    header->stringTable = &entry;
}

std::uint8_t *__cdecl DB_AllocZoneNative(std::size_t size, std::size_t alignment)
{
    const std::size_t start = (g_arenaUsed + alignment - 1) & ~(alignment - 1);
    if (!size || alignment != alignof(StringTable) || start > g_arenaCapacity || size > g_arenaCapacity - start)
        return nullptr;
    g_arenaUsed = start + size;
    std::fill_n(g_arena + start, size, std::uint8_t{0xCD}); // PMem does not zero
    return g_arena + start;
}

int main()
{
    return Run({TestInlineTable, TestOffsetsAndAlias, TestEmptyTables, TestMalformedFailsClosed});
}

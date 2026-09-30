// db_disk32_stringtable_tests.cpp: the 64-bit StringTable loader (NOW row 12)
// on hand-built disk32 zone images. The production stream code (db_stream.cpp,
// db_stream_load.cpp, db_relocation.cpp) runs against a synthetic zone; only
// the inflater (DB_LoadXFileData), the asset pool (Load_StringTableAsset) and
// the zone's native storage (DB_AllocZoneNative) are replaced. Retail data
// never enters tests (docs/ROADMAP.md).

#include <database/database.h>
#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <algorithm>
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

std::vector<std::uint8_t> g_file; // the inflated fast-file bytes
std::size_t g_read = 0;
StringTable g_pool[4]; // what Load_StringTableAsset published
int g_published = 0;

// The zone's native storage. As a static it lies above 4 GiB, so a pointer
// narrowed to 32 bits cannot land back on it.
constexpr std::size_t kArenaBytes = 256;
alignas(16) std::uint8_t g_arena[kArenaBytes];
std::size_t g_arenaUsed = 0;
std::size_t g_arenaCapacity = kArenaBytes;

constexpr std::uint32_t kInline = disk32::kInline;

constexpr std::uint32_t VirtualOffset(std::uint32_t offset)
{
    return ((4u << 28) | offset) + 1;
}

struct File
{
    File &Word(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            g_file.push_back(static_cast<std::uint8_t>(value >> shift));
        return *this;
    }
    File &Text(std::string_view text)
    {
        g_file.insert(g_file.end(), text.begin(), text.end());
        g_file.push_back(0);
        return *this;
    }
    File &Record(std::uint32_t name, std::int32_t columns, std::int32_t rows, std::uint32_t values)
    {
        return Word(name).Word(static_cast<std::uint32_t>(columns)).Word(static_cast<std::uint32_t>(rows)).Word(values);
    }
};

// A zone with a temp block and the virtual block (4) a StringTable streams into.
struct Zone
{
    alignas(16) std::uint8_t temp[16]{};
    alignas(16) std::uint8_t virt[256]{};
    XZoneMemory memory{};

    Zone()
    {
        g_file.clear();
        g_read = 0;
        g_published = 0;
        g_arenaUsed = 0;
        g_arenaCapacity = kArenaBytes;
        memory.blocks[0] = {temp, sizeof(temp)};
        memory.blocks[4] = {virt, sizeof(virt)};
        DB_InitStreams(&memory);
        DB_PushStreamPos(4); // DB_LoadXFile walks the asset array in block 4
    }
    const char *At(std::size_t offset) const { return reinterpret_cast<const char *>(virt + offset); }
};

bool InArena(const void *pointer)
{
    const auto *bytes = static_cast<const std::uint8_t *>(pointer);
    return bytes >= g_arena && bytes < g_arena + g_arenaUsed
        && reinterpret_cast<std::uintptr_t>(pointer) > UINT32_MAX;
}

// Load_XAssetHeader's 64-bit call: the header slot holds the disk32 token.
StringTable *Load(std::uintptr_t slotValue)
{
    StringTable *slot = nullptr;
    std::memcpy(&slot, &slotValue, sizeof(slot));
    DB_LoadStringTablePtrDisk32(false, &slot);
    return slot;
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
    if (!InArena(table->values))
        return;
    const char *const expected[] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; ++i)
        Expect(table->values[i] == zone.At(48 + 2 * i) && !std::strcmp(table->values[i], expected[i]),
               "each value points at its inline string in block 4");
    Expect(!std::memcmp(zone.virt, g_file.data(), 16) && !std::memcmp(zone.virt + 32, g_file.data() + 29, 16),
           "the record and the token array stay at their retail offsets");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 56,
           "every disk byte is consumed and block 4 advances by the retail extent");
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

    const StringTable *const alias = Load(VirtualOffset(0));
    Expect(InArena(alias) && alias != first && g_published == 1,
           "an alias token resolves to the native table, above 4 GiB, and publishes nothing");
    if (InArena(alias))
        Expect(alias->name == first->name && alias->values == first->values
                   && alias->rowCount == 3 && alias->columnCount == 1,
               "the aliased native table holds the converted fields");

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
    Zone zone;
    StringTable *slot = nullptr;
    Drop drop{"(none)"};
    try
    {
        DB_LoadStringTablePtrDisk32(true, &slot);
    }
    catch (const Drop &caught)
    {
        drop = caught;
    }
    Expect(std::strstr(drop.message, "header request") && !slot, "a header is never at the stream start");
}
} // namespace

// Engine seams: the error handler, the inflater, the asset pool and the
// zone's native storage.
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

// Script-string interning is not reached by StringTable; db_stream_load.cpp links it.
db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *, std::uint32_t, LegacyBridgeStringId *) noexcept
{
    return LegacyBridgeStatus::InvalidState;
}

int main()
{
    for (void (*test)() : {TestInlineTable, TestOffsetsAndAlias, TestEmptyTables, TestMalformedFailsClosed})
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

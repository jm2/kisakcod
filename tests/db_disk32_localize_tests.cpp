// db_disk32_localize_tests.cpp: the 64-bit LocalizeEntry loader (NOW row 12)
// on hand-built disk32 zone images. The production stream code (db_stream.cpp,
// db_stream_load.cpp, db_relocation.cpp) runs against a synthetic zone; only
// the inflater (DB_LoadXFileData) and the asset pool (Load_LocalizeEntryAsset)
// are replaced. Retail data never enters tests (docs/ROADMAP.md).

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
LocalizeEntry g_pool[4];          // what Load_LocalizeEntryAsset published
int g_published = 0;

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
    // The 8-byte retail record: value, then name.
    File &Record(std::uint32_t value, std::uint32_t name)
    {
        return Word(value).Word(name);
    }
};

// A zone with the two blocks a LocalizeEntry touches: temp (0) and virtual (4).
struct Zone
{
    alignas(16) std::uint8_t temp[64]{};
    alignas(16) std::uint8_t virt[96]{};
    XZoneMemory memory{};

    explicit Zone(std::uint32_t tempBytes = sizeof(temp))
    {
        g_file.clear();
        g_read = 0;
        g_published = 0;
        memory.blocks[0] = {temp, tempBytes};
        memory.blocks[4] = {virt, sizeof(virt)};
        DB_InitStreams(&memory);
        DB_PushStreamPos(4); // DB_LoadXFile walks the asset array in block 4
    }
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
LocalizeEntry *Load(std::uintptr_t slotValue)
{
    LocalizeEntry *slot = nullptr;
    std::memcpy(&slot, &slotValue, sizeof(slot));
    DB_LoadLocalizeEntryPtrDisk32(false, &slot);
    return slot;
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

void __cdecl Load_LocalizeEntryAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    LocalizeEntry &entry = g_pool[g_published++];
    entry = *header->localize;
    Expect(entry.name && entry.name[0] != '\0', "a published entry has a name");
    header->localize = &entry;
}

// Script-string interning is not reached by LocalizeEntry; db_stream_load.cpp links it.
db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *, std::uint32_t, LegacyBridgeStringId *) noexcept
{
    return LegacyBridgeStatus::InvalidState;
}

int main()
{
    for (void (*test)() : {TestInlineEntries, TestSharedInlineAndOffsets, TestMalformedFailsClosed})
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

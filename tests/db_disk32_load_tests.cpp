// db_disk32_load_tests.cpp: the 64-bit RawFile loader (NOW row 12) on
// hand-built disk32 zone images. The production stream code (db_stream.cpp,
// db_stream_load.cpp, db_relocation.cpp) runs against a synthetic zone; only
// the inflater (DB_LoadXFileData) and the asset pool (Load_RawFileAsset) are
// replaced. Retail data never enters tests (docs/ROADMAP.md).

#include <database/database.h>
#include <database/db_disk32_load.h>
#include <database/db_disk32_mirrors.h>
#include <database/db_load_legacy_bridge.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
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
RawFile g_pool[4];                // what Load_RawFileAsset published
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
    File &Text(const char *text)
    {
        g_file.insert(g_file.end(), text, text + std::strlen(text) + 1);
        return *this;
    }
    File &Record(std::uint32_t name, std::int32_t len, std::uint32_t buffer)
    {
        return Word(name).Word(static_cast<std::uint32_t>(len)).Word(buffer);
    }
};

// A zone with the two blocks a RawFile touches: temp (0) and virtual (4).
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
        memory.blocks[0] = {temp, sizeof(temp)};
        memory.blocks[4] = {virt, sizeof(virt)};
        DB_InitStreams(&memory);
        DB_PushStreamPos(4); // DB_LoadXFile walks the asset array in block 4
    }
    bool Holds(const char *text) const
    {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(text);
        return bytes >= virt && bytes + std::strlen(text) < virt + sizeof(virt);
    }
};

// Load_XAssetHeader's 64-bit call: the header slot holds the disk32 token.
RawFile *Load(std::uintptr_t slotValue)
{
    RawFile *slot = nullptr;
    std::memcpy(&slot, &slotValue, sizeof(slot));
    DB_LoadRawFilePtrDisk32(false, &slot);
    return slot;
}

void TestInlineRawFile()
{
    Zone zone;
    File().Record(kInline, 5, kInline).Text("maps/mp/test.gsc").Text("hello");
    const RawFile *const loaded = Load(kInline);
    Expect(loaded == &g_pool[0] && g_published == 1, "inline raw file publishes one pool entry");
    if (loaded != &g_pool[0])
        return;
    Expect(!std::strcmp(loaded->name, "maps/mp/test.gsc") && zone.Holds(loaded->name),
           "name points at its bytes in block 4");
    Expect(loaded->len == 5 && !std::strcmp(loaded->buffer, "hello") && zone.Holds(loaded->buffer),
           "buffer points at len + 1 bytes in block 4");
    Expect(!std::memcmp(zone.temp, g_file.data(), sizeof(disk32::RawFileDisk32)),
           "the disk32 record is streamed into the temp block at the retail offset");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.virt + 17 + 6,
           "every disk byte is consumed and block 4 advances by the retail extent");
}

void TestSharedInlineAndOffsets()
{
    Zone zone;
    File()
        .Record(kInline, 2, 0).Text("shared.cfg") // after its 4-byte alias slot
        .Record(VirtualOffset(4), 3, kInline).Text("abc");
    const RawFile *const shared = Load(disk32::kSharedInline);
    Expect(shared == &g_pool[0] && !g_pool[0].buffer && g_pool[0].len == 2,
           "shared-inline raw file publishes with a null buffer");
    if (shared != &g_pool[0])
        return;
    Expect(reinterpret_cast<std::uintptr_t>(shared) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed pointer would differ");
    Expect(Load(VirtualOffset(0)) == shared, "an alias token resolves to the full native pointer");
    const RawFile *const second = Load(kInline);
    Expect(second == &g_pool[1], "the second record publishes the second pool entry");
    if (second != &g_pool[1])
        return;
    Expect(second->name == shared->name, "a name offset token resolves to the earlier string");
    Expect(!std::strcmp(second->buffer, "abc") && g_read == g_file.size(),
           "the second record streams after the first");
    Expect(!Load(0) && g_published == 2, "a null token loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    std::uintptr_t slot;
    const char *error;
};

const Malformed kMalformed[] = {
    {"truncated buffer", [] { File().Record(kInline, 32, kInline).Text("a").Text("short"); },
     kInline, "ended unexpectedly"},
    {"buffer past its block", [] { File().Record(kInline, 200, kInline).Text("a"); },
     kInline, "exceeds stream block"},
    {"unterminated buffer", [] { File().Record(kInline, 1, kInline).Text("a").Word(0x41414141); },
     kInline, "not terminated"},
    {"negative length", [] { File().Record(kInline, -1, kInline).Text("a"); }, kInline, "raw-file length"},
    {"null name", [] { File().Record(0, 0, 0); }, kInline, "no name"},
    {"unmapped name offset", [] { File().Record(VirtualOffset(8), 0, 0); }, kInline, "string offset"},
    {"name runs off its block",
     [] { File().Record(kInline, 0, 0); for (int i = 0; i < 40; ++i) File().Word(0x42424242); },
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
    std::memcpy(pos, g_file.data() + g_read, size);
    g_read += size;
    if (DB_MarkStreamRangeMaterialized(pos, size) != db::relocation::Status::Ok)
        Com_Error(ERR_DROP, "Cannot record fast-file output range");
}

void __cdecl Load_RawFileAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    RawFile &entry = g_pool[g_published++];
    entry = *header->rawfile;
    Expect(std::strlen(entry.name) > 0, "a published raw file has a name");
    header->rawfile = &entry;
}

// Script-string interning is not reached by RawFile; db_stream_load.cpp links it.
db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *, std::uint32_t, LegacyBridgeStringId *) noexcept
{
    return LegacyBridgeStatus::InvalidState;
}

int main()
{
    for (void (*test)() : {TestInlineRawFile, TestSharedInlineAndOffsets, TestMalformedFailsClosed})
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

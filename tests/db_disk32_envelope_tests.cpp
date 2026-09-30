// db_disk32_envelope_tests.cpp: the 64-bit XAssetList/XAsset envelope (NOW
// row 12) on hand-built disk32 zone images. The envelope, the 64-bit RawFile
// loader, Load_ScriptStringCustom, the family guard and the production stream
// code run against a synthetic zone. Only the inflater (DB_LoadXFileData), the
// string interner, the asset pool (Load_RawFileAsset) and Load_XAsset's family
// switch (db_load.cpp) are replaced. Retail data never enters tests.

#include <database/database.h>
#include <database/db_disk32_load.h>
#include <database/db_load_legacy_bridge.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

// The loader globals db_load.cpp defines.
XAsset *varXAsset;
XAssetList *varXAssetList;
ScriptStringList *varScriptStringList;

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
std::vector<std::string> g_interned;      // script-string id n is entry n - 1
std::vector<std::uintptr_t> g_entrySlots; // each dispatched header slot on entry

constexpr std::uint32_t kInline = disk32::kInline;
constexpr std::uint32_t kShared = disk32::kSharedInline;
constexpr std::uint32_t kRawFile = ASSET_TYPE_RAWFILE;

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
    // The 16-byte XAssetListDisk32: script strings, then assets.
    File &Root(std::int32_t strings, std::uint32_t stringArray, std::int32_t assets, std::uint32_t assetArray)
    {
        return Word(static_cast<std::uint32_t>(strings)).Word(stringArray)
            .Word(static_cast<std::uint32_t>(assets)).Word(assetArray);
    }
};

// A zone with the blocks the envelope and a RawFile touch: temp and virtual.
struct Zone
{
    alignas(16) std::uint8_t temp[64]{};
    alignas(16) std::uint8_t virt[128]{};
    XZoneMemory memory{};
    XAssetList list{};

    Zone()
    {
        g_file.clear();
        g_read = 0;
        g_published = 0;
        g_interned.clear();
        g_entrySlots.clear();
        memory.blocks[0] = {temp, sizeof(temp)};
        memory.blocks[4] = {virt, sizeof(virt)};
        DB_InitStreams(&memory);
    }
    bool Holds(const void *pointer) const
    {
        const auto *byte = static_cast<const std::uint8_t *>(pointer);
        return byte >= virt && byte < virt + sizeof(virt);
    }
};

// Four script strings (inline, an offset to the first, null, inline), then
// three RawFiles (inline, shared-inline, an alias to the shared one). The
// comments give each item's retail block-4 offset.
constexpr std::size_t kRecordsInFile = 16 + 16 + 6 + 5;
void BuildZone()
{
    File()
        .Root(4, kInline, 3, kInline)
        .Word(kInline).Word(VirtualOffset(16)).Word(0).Word(kInline) // 0: tokens
        .Text("alpha").Text("beta")                                   // 16, 22
        .Word(kRawFile).Word(kInline)                                 // 28: records
        .Word(kRawFile).Word(kShared)
        .Word(kRawFile).Word(VirtualOffset(64))
        .Word(kInline).Word(2).Word(kInline).Text("a.gsc").Text("hi") // temp; 52, 58
        .Word(kInline).Word(0).Word(0).Text("b.cfg");                 // slot 64; temp; 68
}

// Script strings: each id sits in a native 8-byte slot that the production
// Load_ScriptStringCustom reads.
void CheckScriptStrings(const XAssetList &list)
{
    Expect(list.stringList.count == 4 && g_interned == std::vector<std::string>{"alpha", "beta"},
           "inline script strings are interned once each");
    const std::uint16_t ids[] = {1, 1, 0, 2};
    for (std::uint16_t index = 0; index < 4; ++index)
    {
        std::uint16_t value = index;
        Load_ScriptStringCustom(&value);
        Expect(value == ids[index], "a script-string index resolves to its interned id");
    }
}

// Header slots: the zero-extended token on entry, the full pointer on return.
void CheckHeaderSlots(const XAssetList &list)
{
    Expect(g_entrySlots == std::vector<std::uintptr_t>{kInline, kShared, VirtualOffset(64)},
           "each header slot holds its zero-extended disk32 token on entry");
    Expect(reinterpret_cast<std::uintptr_t>(&g_pool[0]) > UINT32_MAX,
           "the pool lies above 4 GiB, so a narrowed header would differ");
    const RawFile *const expected[] = {&g_pool[0], &g_pool[1], &g_pool[1]};
    for (int index = 0; index < 3; ++index)
    {
        Expect(list.assets[index].type == ASSET_TYPE_RAWFILE && list.assets[index].header.rawfile == expected[index],
               "each header slot holds the full native pointer on return");
    }
}

void CheckRawFiles(const Zone &zone)
{
    Expect(!std::strcmp(g_pool[0].name, "a.gsc") && !std::strcmp(g_pool[0].buffer, "hi") && zone.Holds(g_pool[0].name),
           "the inline raw file points at its block-4 bytes");
    Expect(!std::strcmp(g_pool[1].name, "b.cfg") && !g_pool[1].buffer, "the shared raw file loads after its slot");
}

void TestZoneLoads()
{
    Zone zone;
    BuildZone();
    DB_LoadXAssetListDisk32(&zone.list);
    const XAssetList &list = zone.list;
    Expect(varXAssetList == &list && varScriptStringList == &list.stringList, "the loader globals name the list");
    Expect(g_read == g_file.size() && DB_GetStreamPos() == zone.temp,
           "every disk byte is consumed and the stream stack unwinds");
    Expect(!std::memcmp(zone.virt + 28, g_file.data() + kRecordsInFile, 3 * 8),
           "the disk32 records stay at their retail block-4 offset");
    CheckScriptStrings(list);
    if (list.assetCount != 3 || !list.assets || zone.Holds(list.assets) || g_published != 2)
        return Expect(false, "three assets load beside the zone and two raw files publish");
    CheckHeaderSlots(list);
    CheckRawFiles(zone);
}

void TestEmptyList()
{
    Zone zone;
    File().Root(0, 0, 0, 0);
    DB_LoadXAssetListDisk32(&zone.list);
    Expect(!zone.list.stringList.strings && !zone.list.assets && g_entrySlots.empty() && g_read == 16,
           "an empty list loads nothing");
}

struct Malformed
{
    const char *what;
    void (*build)();
    const char *error;
    std::size_t dispatched = 0;
};

const Malformed kMalformed[] = {
    {"asset count overflow", [] { File().Root(0, 0, 32769, kInline); }, "asset count 32769"},
    {"asset bytes overflow", [] { File().Root(0, 0, 0x20000000, kInline); }, "asset count 536870912"},
    {"negative asset count", [] { File().Root(0, 0, -1, kInline); }, "asset count -1"},
    {"script-string count overflow", [] { File().Root(65537, kInline, 0, 0); }, "script-string list count 65537"},
    {"script strings without an array", [] { File().Root(1, 0, 0, 0); }, "script-string list count 1"},
    {"assets without an array", [] { File().Root(0, 0, 1, 0); }, "pointer/count"},
    {"an array without assets", [] { File().Root(0, 0, 0, kInline); }, "pointer/count"},
    {"shared-inline asset array", [] { File().Root(0, 0, 1, kShared); }, "not inline"},
    {"offset asset array", [] { File().Root(0, 0, 1, VirtualOffset(0)); }, "not inline"},
    {"offset script-string array", [] { File().Root(1, VirtualOffset(0), 0, 0); }, "not inline"},
    {"shared-inline script string", [] { File().Root(1, kInline, 0, 0).Word(kShared); }, "shared-inline"},
    {"unmapped script-string offset", [] { File().Root(1, kInline, 0, 0).Word(VirtualOffset(64)); }, "string offset"},
    {"asset type past the enum",
     [] { File().Root(0, 0, 2, kInline).Word(kRawFile).Word(kInline).Word(ASSET_TYPE_COUNT).Word(kInline); },
     "Invalid fast-file asset type"},
    {"negative asset type", [] { File().Root(0, 0, 1, kInline).Word(UINT32_MAX).Word(kInline); },
     "Invalid fast-file asset type"},
    {"type the MP build rejects", [] { File().Root(0, 0, 1, kInline).Word(ASSET_TYPE_CLIPMAP).Word(kInline); },
     "does not support"},
    {"truncated asset array", [] { File().Root(0, 0, 3, kInline).Word(kRawFile).Word(kInline).Word(kRawFile); },
     "ended unexpectedly"},
    {"asset array past its block", [] { File().Root(0, 0, 17, kInline); }, "exceeds stream block"},
    {"an unported family after a raw file",
     []
     {
         File().Root(0, 0, 2, kInline).Word(kRawFile).Word(kInline).Word(ASSET_TYPE_LOCALIZE_ENTRY).Word(kInline)
             .Word(kInline).Word(0).Word(0).Text("c.cfg");
     },
     "no 64-bit on-disk/runtime layout pair", 2},
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
            DB_LoadXAssetListDisk32(&zone.list);
        }
        catch (const Drop &caught)
        {
            drop = caught;
        }
        const int published = test.dispatched ? 1 : 0;
        Expect(std::strstr(drop.message, test.error) && g_published == published
                   && g_entrySlots.size() == test.dispatched && g_read <= g_file.size(),
               test.what, drop.message);
    }
}
} // namespace

// Engine seams: the error handler, the inflater, the interner and the pool.
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
    // As in production: the root lies outside the zone blocks.
    const db::relocation::Status status = DB_MarkStreamRangeMaterialized(pos, size);
    if (status != db::relocation::Status::Ok && status != db::relocation::Status::InvalidContext
        && status != db::relocation::Status::OutOfRange)
    {
        Com_Error(ERR_DROP, "Cannot record fast-file output range");
    }
}

void __cdecl Load_RawFileAsset(XAssetHeader *header)
{
    // DB_AddXAsset hashes the name, then copies the header into the pool.
    RawFile &entry = g_pool[g_published++];
    entry = *header->rawfile;
    header->rawfile = &entry;
}

// db_load.cpp's Load_XAsset and Load_XAssetHeader switch. RawFile goes to its
// 64-bit loader, as the switch will once the K4 PR lifts its guard; every
// other family meets the production guard, which refuses it at 64-bit.
void __cdecl Load_XAsset(bool atStreamStart)
{
    g_entrySlots.push_back(reinterpret_cast<std::uintptr_t>(varXAsset->header.data));
    Expect(!atStreamStart, "the header record is already streamed");
    if (varXAsset->type == ASSET_TYPE_RAWFILE)
        DB_LoadRawFilePtrDisk32(atStreamStart, &varXAsset->header.rawfile);
    else if (DB_AdmitAssetFamilyLoad(varXAsset->type, "unported", true, false))
        Com_Error(ERR_DROP, "the test routes no other family");
}

db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryInternUser4StringOfSize(
    const char *bytes, std::uint32_t byteCount, LegacyBridgeStringId *outString) noexcept
{
    const std::string_view bytesView(bytes, byteCount); // with its terminator
    const std::string text(bytesView.substr(0, bytesView.find('\0')));
    auto found = std::find(g_interned.begin(), g_interned.end(), text);
    if (found == g_interned.end())
        found = g_interned.insert(found, text);
    outString->stringId = static_cast<std::uint32_t>(found - g_interned.begin()) + 1;
    return LegacyBridgeStatus::Success;
}

// Mark_ScriptStringCustom's reference count; the envelope never marks.
db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryAddUser4(std::uint32_t) noexcept
{
    return LegacyBridgeStatus::InvalidState;
}

int main()
{
    // The zone loads twice: the second load reuses native slots that still
    // hold the first load's pointers.
    for (void (*test)() : {TestZoneLoads, TestZoneLoads, TestEmptyList, TestMalformedFailsClosed})
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

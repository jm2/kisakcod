// db_disk32_envelope_tests.cpp: the 64-bit XAssetList/XAsset envelope (NOW
// row 12) on hand-built disk32 zone images (disk32_fixture.hpp). The envelope,
// the 64-bit RawFile loader, Load_ScriptStringCustom, the family guard and the
// production stream code run against a synthetic zone. Beyond the fixture's
// seams, only the asset pool (Load_RawFileAsset) and Load_XAsset's family
// switch (db_load.cpp) are replaced.

#include "disk32_fixture.hpp"

#include <database/db_disk32_load.h>
#include <database/db_load_legacy_bridge.h>

#include <cstring>
#include <string>
#include <vector>

// The loader globals db_load.cpp defines.
XAsset *varXAsset;
XAssetList *varXAssetList;
ScriptStringList *varScriptStringList;

namespace
{
using namespace disk32_test;

RawFile g_pool[4];                        // what Load_RawFileAsset published
std::vector<std::uintptr_t> g_entrySlots; // each dispatched header slot on entry

constexpr std::uint32_t kShared = disk32::kSharedInline;
constexpr std::uint32_t kRawFile = ASSET_TYPE_RAWFILE;

struct File : FileBuilder<File>
{
    // The 16-byte XAssetListDisk32: script strings, then assets.
    File &Root(std::int32_t strings, std::uint32_t stringArray, std::int32_t assets, std::uint32_t assetArray)
    {
        return Word(static_cast<std::uint32_t>(strings)).Word(stringArray)
            .Word(static_cast<std::uint32_t>(assets)).Word(assetArray);
    }
};

// A zone with the blocks the envelope and a RawFile touch: temp and virtual.
// DB_LoadXFileInternal calls the envelope with no stream pushed, and reads the
// root outside the blocks.
struct Zone
{
    alignas(16) std::uint8_t temp[64]{};
    alignas(16) std::uint8_t virt[128]{};
    XZoneMemory memory{};
    XAssetList list{};

    Zone()
    {
        ResetImage();
        g_entrySlots.clear();
        g_allowReadsOutsideBlocks = true;
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
    if (list.assetCount != 3 || !InArena(list.assets) || g_published != 2)
        return Expect(false, "three assets load into native storage and two raw files publish");
    Expect(g_arenaUsed == 4 * sizeof(const char *) + 3 * sizeof(XAsset),
           "native storage holds exactly the 8-byte string slots and the 16-byte assets");
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
    std::size_t arena = kArenaBytes;
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
    {"script strings exhaust native storage", [] { File().Root(2, kInline, 0, 0).Word(0).Word(0); },
     "exhausted", 0, 8},
    {"assets exhaust native storage", [] { File().Root(0, 0, 1, kInline).Word(kRawFile).Word(kInline); },
     "exhausted", 0, 8},
};

// Retail code_post_gfx_mp lists a SndDriverGlobals asset whose header slot holds
// a stale build-tool value and no record: the list admits it, and the RawFile
// after it still loads, so nothing was consumed for it.
void TestListedOnlyAssetSkipped()
{
    Zone zone;
    File()
        .Root(0, 0, 2, kInline)
        .Word(ASSET_TYPE_SNDDRIVER_GLOBALS).Word(0x0051C354u)
        .Word(kRawFile).Word(kInline)
        .Word(kInline).Word(2).Word(kInline).Text("a.gsc").Text("hi");
    const Drop drop = Catch([&] { DB_LoadXAssetListDisk32(&zone.list); });
    Expect(!std::strcmp(drop.message, "(none)") && g_published == 1 && g_entrySlots.size() == 2 && g_entrySlots[0] == 0x0051C354u
               && !zone.list.assets[0].header.data,
           "a listed-only SndDriverGlobals is admitted and skipped", drop.message);
}

void TestMalformedFailsClosed()
{
    for (const Malformed &test : kMalformed)
    {
        Zone zone;
        g_arenaCapacity = test.arena;
        test.build();
        const Drop drop = Catch([&] { DB_LoadXAssetListDisk32(&zone.list); });
        const int published = test.dispatched ? 1 : 0;
        Expect(std::strstr(drop.message, test.error) && g_published == published
                   && g_entrySlots.size() == test.dispatched && g_read <= g_file.size(),
               test.what, drop.message);
    }
}
} // namespace

// Engine seams beyond the fixture's: the pool and the family switch.
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
    else if (db::asset_mode::IsSkippedByLoader(varXAsset->type)) // as Load_XAssetHeader skips it
        varXAsset->header.data = nullptr;
    else if (DB_AdmitAssetFamilyLoad(varXAsset->type, "unported", true, false))
        Com_Error(ERR_DROP, "the test routes no other family");
}

// db_load.cpp's header-slot alias; the zone test (database-disk32-zone-*)
// runs the real ones.
void __cdecl DB_RecordXAssetHeaderSlot(const void *, int32_t, const void *) {}

// Mark_ScriptStringCustom's reference count; the envelope never marks.
db::load_legacy_bridge::LegacyBridgeStatus
db::load_legacy_bridge::DbLoadLegacyBridge::TryAddUser4(std::uint32_t) noexcept
{
    return LegacyBridgeStatus::InvalidState;
}

bool db::load_legacy_bridge::DbLoadLegacyBridge::InSession() noexcept
{
    return false;
}

int main()
{
    // The zone loads twice: the second load reuses native storage that still
    // holds the first load's data.
    return Run({TestZoneLoads, TestZoneLoads, TestEmptyList, TestListedOnlyAssetSkipped, TestMalformedFailsClosed});
}

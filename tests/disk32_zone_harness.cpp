// disk32_zone_harness.cpp: the harness disk32_zone_harness.hpp describes. The
// test writes the zone as a .ff and the production code does the rest:
// DB_LoadXAssets queues it, the database thread's DB_TryLoadXFile opens and
// inflates it, the envelope streams the asset list, Load_XAssetHeader
// dispatches each asset to its family's loader, the pools publish them,
// DB_FindXAssetHeader finds them and DB_ShutdownXAssets unloads the zone.
// Only the engine boundary at the end is stubbed. Retail data never enters
// tests: every byte is built in db_disk32_zone_tests.cpp.
//
// `guard <family>` loads the same zone with that family's guard left on
// (db_asset_layout.h's test seam admits every other family): the load must
// fail closed naming it.
//
// `expect-drop <variant>` loads the zone the test file builds for that
// variant (zone_test::Variant): the load must raise the ERR_DROP its
// VariantDrop names.

#include "disk32_zone_harness.hpp"

#include <database/db_load_legacy_bridge.h>

#include <game/g_bsp.h>
#include <qcommon/cmd.h>
#include <qcommon/sys_time.h>
#include <qcommon/threads.h>
#include <script/scr_stringlist.h>
#include <universal/com_files.h>
#include <universal/physicalmemory.h>
#include <zlib/zlib.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <strings.h>
#include <unistd.h> // mkdtemp on macOS

extern XAssetList g_varXAssetList;
extern int32_t g_zoneCount;
extern int32_t g_poolSize[ASSET_TYPE_COUNT];

namespace zone_test
{
int g_failures = 0;
void Expect(bool ok, const char *what, const char *detail)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s %s\n", what, detail);
        ++g_failures;
    }
}

// The pool lookup the engine makes. A missing asset would get a default
// entry, so the hash is checked first.
XAssetHeader Find(XAssetType type, const char *name)
{
    return DB_FindXAssetEntry(type, name) ? DB_FindXAssetHeader(type, name) : XAssetHeader{};
}
} // namespace zone_test

namespace
{
using namespace zone_test;

// A production ERR_DROP longjmps out of the load; the stub throws this.
struct Drop
{
    char message[512]; // Flawfinder: ignore (Com_Error fills it with a bounded vsnprintf)
};

// The XFile header (block sizes), the asset list root, block 4's script
// strings and asset records, then each asset. Runs twice, so every offset a
// writer records is known when the second pass writes its tokens.
std::vector<std::uint8_t> BuildZone()
{
    const std::span<const Asset> assets = ZoneAssets();
    const std::span<const char *const> strings = ZoneScriptStrings();
    const auto count = [](auto span) { return static_cast<std::uint32_t>(span.size()); };
    Image z;
    z.Word(0).Word(0);
    for (std::uint32_t size : {0x10000, 0x10000, 0, 0, 0x40000, 0, 0, 0x1000, 0x1000})
        z.Word(size);
    z.Word(count(strings)).Word(strings.empty() ? 0 : kInline).Word(count(assets)).Word(kInline);
    z.VAlloc(4 * count(strings));
    for (std::size_t i = 0; i < strings.size(); ++i)
        z.Word(kInline);
    for (const char *text : strings)
        z.V(text);
    z.VAlloc(8 * count(assets));
    for (const Asset &asset : assets)
        z.Word(static_cast<std::uint32_t>(asset.type)).Word(asset.Token());
    for (const Asset &asset : assets)
        asset.write(z);
    return z.bytes;
}

std::filesystem::path g_root; // fs_basepath
constexpr const char *kZoneName = "e2e_zone";

// IWffu100 (unsigned), version 5, then the deflated zone.
void WriteFastFile()
{
    BuildZone();
    const std::vector<std::uint8_t> zone = BuildZone();
    uLongf packedBytes = static_cast<uLongf>(zone.size() + zone.size() / 100 + 64); // zlib 1.1.4's bound
    std::vector<std::uint8_t> packed(packedBytes);
    Expect(compress(packed.data(), &packedBytes, zone.data(), static_cast<uLong>(zone.size())) == Z_OK,
           "the zone deflates");
    const std::filesystem::path dir = g_root / "zone" / "english";
    std::filesystem::create_directories(dir);
    std::ofstream out(dir / (std::string(kZoneName) + ".ff"), std::ios::binary);
    const std::uint32_t version = 5;
    out.write("IWffu100", 8);
    out.write(reinterpret_cast<const char *>(&version), sizeof(version));
    out.write(reinterpret_cast<const char *>(packed.data()), static_cast<std::streamsize>(packedBytes));
}

XAssetType g_guarded = ASSET_TYPE_COUNT; // the family whose guard stays on
bool g_unverifiedMode = false; // "unverified": the seam admits nothing; db_unverified64 is on

// Queues the zone as the main thread does, then runs the database thread's
// load in line; returns the ERR_DROP it raised, or "(none)".
Drop LoadZone()
{
    Drop drop{"(none)"};
    XZoneInfo info{kZoneName, 4, 0};
    try
    {
        DB_LoadXAssets(&info, 1, 0);
        DB_TryLoadXFile();
    }
    catch (const Drop &caught)
    {
        drop = caught;
    }
    return drop;
}

// The name the pool hashes: a stub (",name") publishes under its name.
const char *PoolName(const char *name)
{
    return name[0] == ',' ? name + 1 : name;
}

// Every asset publishes, and the envelope's header slot holds the pool's pointer.
void CheckPublished()
{
    const std::span<const Asset> assets = ZoneAssets();
    Expect(g_varXAssetList.assetCount == static_cast<int>(assets.size()), "the asset list holds every asset");
    for (std::size_t i = 0; i < assets.size() && g_varXAssetList.assets; ++i)
    {
        const XAsset &asset = g_varXAssetList.assets[i];
        if (assets[i].skipped)
            Expect(asset.type == assets[i].type && !asset.header.data, "a listed-only asset loads nothing:",
                   assets[i].name);
        else
            Expect(asset.type == assets[i].type && Is(asset.header.data, assets[i].type, PoolName(assets[i].name)),
                   "an asset publishes and its header slot is the pool's:", assets[i].name);
    }
    for (const char *text : ZoneScriptStrings())
        Expect(SL_FindString(text) != 0, "a script string is interned:", text);
}

// The quit's unload: no asset outlives the zone, and its memory comes back.
void CheckUnload(std::uint32_t freeBefore)
{
    DB_ShutdownXAssets();
    Expect(g_zoneCount == 0, "the zone unloads");
    for (const Asset &asset : ZoneAssets())
        Expect(asset.skipped || !DB_FindXAssetEntry(asset.type, PoolName(asset.name)), "an asset outlives its zone:",
               asset.name);
    // Nor does a default entry a stub or a lookup made.
    for (std::int32_t type = 0; type < ASSET_TYPE_COUNT; ++type)
    {
        int count = 0;
        if (g_poolSize[type] > 0)
            DB_EnumXAssets(static_cast<XAssetType>(type), [](XAssetHeader, void *data) { ++*static_cast<int *>(data); },
                           &count, true);
        Expect(count == 0, "an entry outlives the zone in pool", DB_GetXAssetTypeName(type));
    }
    Expect(PMem_GetFreeAmount() == freeBefore, "the zone's memory is freed");
}

const char *g_variant = nullptr; // expect-drop's variant

int Run(const char *guarded)
{
    PMem_Init();
    SL_Init();
    WriteFastFile();
    const std::uint32_t freeBefore = PMem_GetFreeAmount();
    const Drop drop = LoadZone();
    if (guarded)
    {
        char quoted[64]; // Flawfinder: ignore (bounded snprintf of a type name)
        std::snprintf(quoted, sizeof(quoted), "'%s'", guarded);
        Expect(std::strstr(drop.message, quoted) && std::strstr(drop.message, "refusing to load"),
               "the guarded family fails closed naming itself:", drop.message);
    }
    else if (g_variant)
    {
        const char *const expected = VariantDrop();
        Expect(expected && std::strstr(drop.message, expected), "the variant's zone fails closed:", drop.message);
        // The drop left no registry session holding the hash.
        Expect(!db::load_legacy_bridge::DbLoadLegacyBridge::InSession(), "the drop leaves a registry session open");
    }
    else if (std::strcmp(drop.message, "(none)"))
        Expect(false, "the zone raised ERR_DROP:", drop.message);
    else
    {
        CheckPublished();
        CheckZone();
        CheckUnload(freeBefore);
    }
    return g_failures ? 1 : 0;
}

std::array<std::recursive_mutex, CRITSECT_COUNT> g_criticalSections;
dvar_s g_basePath{};
dvar_s g_emptyDvar{};
} // namespace

const char *zone_test::Variant()
{
    return g_variant;
}

bool DB_TestAdmitsAssetFamily(std::int32_t assetType) noexcept
{
    return !g_unverifiedMode && assetType != g_guarded;
}

int main(int argc, char **argv)
{
    std::string root = (std::filesystem::temp_directory_path() / "kisak-e2e-zone-XXXXXX").string();
    if (!mkdtemp(root.data()))
        return 1;
    g_root = root;
    g_basePath.current.string = root.c_str();
    g_emptyDvar.current.string = "";
    const char *guarded = argc == 3 && !std::strcmp(argv[1], "guard") ? argv[2] : nullptr;
    g_variant = argc == 3 && !std::strcmp(argv[1], "expect-drop") ? argv[2] : nullptr;
    g_unverifiedMode = argc == 2 && !std::strcmp(argv[1], "unverified");
    for (std::int32_t type = 0; guarded && type < ASSET_TYPE_COUNT; ++type)
        g_guarded = std::strcmp(DB_GetXAssetTypeName(type), guarded) ? g_guarded : static_cast<XAssetType>(type);
    Expect(!guarded || g_guarded != ASSET_TYPE_COUNT, "the guarded family is named", guarded ? guarded : "");
    const int result = Run(guarded);
    std::filesystem::remove_all(g_root);
    return result;
}

// ---- Engine boundary ------------------------------------------------------

void __cdecl Com_Error(errorParm_t, const char *fmt, ...)
{
    Drop drop{};
    va_list args;
    va_start(args, fmt);
    // Flawfinder: ignore -- the engine's literal formats into a bounded, terminated buffer.
    std::vsnprintf(drop.message, sizeof(drop.message), fmt, args);
    va_end(args);
    throw drop;
}
void Sys_Error(const char *fmt, ...)
{
    std::fprintf(stderr, "Sys_Error: %s\n", fmt);
    std::exit(1);
}
void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::exit(1);
}
void Com_Printf(int, const char *, ...) {}
void Com_PrintWarning(int, const char *, ...) {}
// db_unverified64: off, except in the "unverified" mode, where only it admits the families.
bool Dvar_GetBool(const char *name) { return g_unverifiedMode && name && !std::strcmp(name, "db_unverified64"); }
void Com_PrintError(int, const char *, ...) {}
int Com_sprintf(char *dest, unsigned int size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    // Flawfinder: ignore -- bounded by size, as the engine's is.
    const int written = std::vsnprintf(dest, size, fmt, args);
    va_end(args);
    return written;
}
const dvar_s *fs_basepath = &g_basePath;
const dvar_s *fs_gameDirVar = &g_emptyDvar;
const dvar_t *com_sv_running = nullptr;
const dvar_t *loc_warnings = nullptr;
const dvar_t *loc_warningsAsErrors = nullptr;
int com_missingAssetOpenFailed;
int fs_numServerReferencedFFs;
const char *fs_serverReferencedFFNames[32]; // Flawfinder: ignore (the engine global, never written here)
clipMap_t cm;
ComWorld comWorld;
GameWorldMp gameWorldMp;
const dvar_s *Dvar_RegisterString(const char *, const char *, unsigned short, const char *) { return &g_emptyDvar; }
char *__cdecl Win_GetLanguage() { return const_cast<char *>("english"); }
char *__cdecl Sys_DefaultInstallPath() { return const_cast<char *>(g_basePath.current.string); }
int I_stricmp(const char *s0, const char *s1) { return strcasecmp(s0, s1); }
int I_strnicmp(const char *s0, const char *s1, int n) { return strncasecmp(s0, s1, static_cast<size_t>(n)); }
int I_strncmp(const char *s0, const char *s1, int n) { return std::strncmp(s0, s1, static_cast<size_t>(n)); }
void I_strncpyz(char *dest, const char *src, int size) { std::snprintf(dest, static_cast<size_t>(size), "%s", src); }
const char *I_stristr(const char *s0, const char *s1) { return strcasestr(s0, s1); }
const char *Com_GetExtensionSubString(const char *name) { const char *dot = std::strrchr(name, '.'); return dot ? dot : ""; }
void Sys_EnterCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).lock(); }
void Sys_LeaveCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).unlock(); }
bool Sys_IsMainThread() { return true; }
bool Sys_IsDatabaseThread() { return false; }
bool Sys_IsRenderThread() { return false; }
bool Sys_IsDatabaseReady() { return true; }
bool Sys_IsDatabaseReady2() { return true; }
bool Sys_HaveSuspendedDatabaseThread(ThreadOwner) { return false; }
void Sys_ResumeDatabaseThread(ThreadOwner) {}
void Sys_SuspendDatabaseThread(ThreadOwner) {}
void Sys_WaitDatabaseThread() {}
void Sys_DatabaseCompleted() {}
void Sys_DatabaseCompleted2() {}
void Sys_SyncDatabase() {}
void Sys_Sleep(unsigned int) {}
void NET_Sleep(int) {}
std::uint32_t Sys_Milliseconds()
{
    static std::uint32_t now = 0;
    return ++now;
}
void ProfLoad_Begin(const char *) {}
void ProfLoad_End() {}
void Z_Free(void *, int) {}
void CM_Unload() {}
void Com_UnloadWorld() {}
void BG_FillInAllWeaponItems() {}
DObj_s *Com_GetClientDObj(unsigned int, int) { return nullptr; }
DObj_s *Com_GetServerDObj(unsigned int) { return nullptr; }
void DObjArchive(DObj_s *) {}
void DObjUnarchive(DObj_s *) {}
// Reached only by the zone-reorder and missing-asset logs, which stay off.
FILE *FS_FileOpenReadBinary(const char *) { return nullptr; }
FILE *FS_FileOpenWriteBinary(const char *) { return nullptr; }
uint32_t FS_FileRead(void *, uint32_t, FILE *) { return 0; }
uint32_t FS_FileWrite(const void *, uint32_t, FILE *) { return 0; }
void FS_FileClose(FILE *) {}
int FS_FileGetFileSize(FILE *) { return 0; }
int FS_FOpenTextFileWrite(const char *) { return 0; }
int FS_FOpenFileAppend(const char *) { return 0; }
uint32_t FS_Write(const char *, uint32_t, int) { return 0; }
void FS_FCloseFile(int) {}
void Sys_OutOfMemErrorInternal(const char *file, int line) { Sys_Error("out of memory at %s:%d", file, line); }
void Com_Memset(void *dest, const int val, const size_t count) { std::memset(dest, val, count); }
// The main thread's queue: the database thread runs in-line here.
void Cmd_AddCommandInternal(const char *, void (*)(), cmd_function_s *) {}
const char *Cmd_Argv(int) { return ""; }
void Com_SyncThreads() {}
void Sys_WakeDatabase() {}
void Sys_WakeDatabase2() {}
void Sys_NotifyDatabase() {}

// db_registry_unload_tests.cpp: the db_registry unload sequence against the real
// bridge, facade, coordinator and script-string registry. On the code that held
// db_hashCritSect around the bridge calls, the quit checks reach the Com_Error
// stub with the hash held (in production that error never returns). The
// production TUs at 64-bit with the headless defines, as in
// game_mp_hazard_tests.cpp: weak engine boundary, --gc-sections. Also the
// menu asset's dynamic clone, which db_registry.cpp holds, and a map zone's
// post-load override link, whose marks need the same registry window.

#include <database/database.h>
#include <database/db_load_legacy_bridge.h>
#include <database/db_registry_ownership_coordinator.h>
#include <database/db_zone_runtime_table.h>
#include <game/g_bsp.h>
#include <qcommon/com_bsp.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_sync.h>
#include <script/scr_stringlist.h>
#include <ui/ui_shared.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <strings.h>

extern FastCriticalSection db_hashCritSect;
extern int32_t g_zoneCount;
extern uint8_t g_zoneHandles[];
extern uint16_t db_hashTable[];
extern XAssetEntryPoolEntry g_assetEntryPool[];
extern uint32_t g_copyInfoCount;
extern XZone g_zones[];

namespace
{
using db::load_legacy_bridge::DbLoadLegacyBridge;
using db::load_legacy_bridge::LegacyBridgeStatus;

int g_failures = 0;
#define CHECK(expr) \
    ((expr) ? void() : (void)(std::fprintf(stderr, "line %d: CHECK(%s)\n", __LINE__, #expr), ++g_failures))

int g_reports = 0;               // Com_PrintError calls
bool g_databaseReady2 = true;    // Sys_IsDatabaseReady2
uint16_t g_overrideMark = 0;     // the name Mark_XAsset marks, when set
int g_marks = 0;                 // Mark_XAsset calls that marked it
bool g_reportedUnderHash = false; // one of them while db_hashCritSect was held

// A name a zone load interned for its assets: user 4 only.
void InternZoneName(const char *name)
{
    db::load_legacy_bridge::LegacyBridgeStringId id{};
    CHECK(DbLoadLegacyBridge::TryInternUser4String(name, &id) == LegacyBridgeStatus::Success);
    CHECK(SL_FindString(name) == id.stringId);
}

// Quit after Com_Init. With every zone unloaded, the user-8 shutdown frees the
// zones' names, and the quit returns with the hash released.
void TestQuitFreesZoneNames()
{
    InternZoneName("unload-quit-name");
    DB_ShutdownXAssets();
    CHECK(!Sys_IsWriteLocked(&db_hashCritSect));
    CHECK(SL_FindString("unload-quit-name") == 0);
    CHECK(g_reports == 0);
}

// A registry call fails inside the session: a mark of an id no string has.
// Nothing is raised under the hash, the user-8 shutdown is skipped so the
// transferred name survives, and the failure comes back once the hash is free.
void TestFailureInSessionKeepsNames()
{
    InternZoneName("unload-kept-name");
    DbLoadLegacyBridge::BeginSession();
    CHECK(Sys_IsWriteLocked(&db_hashCritSect));
    uint16_t unallocated = 0xFFF0;
    Mark_ScriptStringCustom(&unallocated);
    DB_FreeUnusedResources();
    CHECK(DbLoadLegacyBridge::FinishSession() == LegacyBridgeStatus::OwnershipMismatch);
    CHECK(!Sys_IsWriteLocked(&db_hashCritSect));
    CHECK(SL_FindString("unload-kept-name") != 0);
}

// A map zone overrides a raw file a loaded zone holds and uses. Its post-load
// link marks the held asset's names user 4 (Mark_XAsset), as a map's impact
// table marks its effects' models' bone names. The link holds db_hashCritSect
// through a registry session, so the mark reaches the registry instead of
// failing on the hash its own thread holds.
void TestPostLoadOverrideMarks()
{
    db::load_legacy_bridge::LegacyBridgeStringId id{};
    CHECK(DbLoadLegacyBridge::TryInternUser4String("postload-bone", &id) == LegacyBridgeStatus::Success);
    XAssetEntryPoolEntry &held = g_assetEntryPool[1];
    XAssetEntryPoolEntry &overriding = g_assetEntryPool[2];
    held.entry.asset.type = ASSET_TYPE_RAWFILE;
    held.entry.zoneIndex = 1;
    held.entry.inuse = 1;
    overriding.entry.asset.type = ASSET_TYPE_RAWFILE;
    overriding.entry.zoneIndex = 2;
    g_zones[1].flags = 1;
    g_zones[2].flags = 2; // the map zone overrides
    uint16_t &bucket = db_hashTable[DB_HashForName("", ASSET_TYPE_RAWFILE)];
    bucket = 1;
    g_copyInfo[0] = &overriding.entry;
    g_copyInfoCount = 1;
    g_overrideMark = static_cast<uint16_t>(id.stringId);
    g_databaseReady2 = false;
    DB_PostLoadXZone();
    g_databaseReady2 = true;
    g_overrideMark = 0;
    CHECK(g_marks == 1 && g_copyInfoCount == 0 && g_reports == 0);
    CHECK(!Sys_IsWriteLocked(&db_hashCritSect));
    CHECK(held.entry.nextOverride == 2);
    bucket = 0;
    held = {};
    overriding = {};
    g_zones[1].flags = g_zones[2].flags = 0;
}

// Two unloads with a poisoned registry boundary, so no registry window opens:
// each completes and reports the failure after releasing the hash.
void TestUnloadReportsAfterRelease()
{
    db::registry_ownership::SetRegistryOwnershipCoordinatorBoundaryForTesting(0, 0, 0, 0, 2, 2);
    DB_ShutdownXAssets();
    DB_ShutdownXAssets();
    CHECK(g_reports == 2);
    CHECK(!g_reportedUnderHash);
    CHECK(!Sys_IsWriteLocked(&db_hashCritSect));
}

// Run alone (ctest passes poisoned-window): the registry poisons while the quit's
// window is open, here as a zone's memory is released. The window keeps
// db_hashCritSect (fail closed), so the quit must stop in Sys_Error rather than
// return to a database no lookup can enter. The Sys_Error stub ends the check.
bool g_poisonInWindow = false;
void TestPoisonedWindowIsFatal()
{
    g_zoneCount = 1;
    g_zoneHandles[0] = 1;
    g_poisonInWindow = true;
    DB_ShutdownXAssets();
    std::fprintf(stderr, "the quit returned with its registry window poisoned\n");
    ++g_failures;
}

// A dynamic clone of a two-item menu at 64-bit: the menu's dynamic flags
// copy, a named item's copy from the source item of the same name (not the
// same index; unnamed items match nothing), and every item loses focus
// (flag 2). The source is unchanged.
void TestDynamicCloneMenu()
{
    char buttonName[] = "button";
    char cloneName[] = "button"; // equal text, another pointer
    std::array<itemDef_s, 2> fromItems{};
    std::array<itemDef_s, 2> toItems{};
    fromItems[0].window.dynamicFlags[0] = 0x10; // unnamed: matches nothing
    fromItems[1].window.name = buttonName;
    fromItems[1].window.dynamicFlags[0] = 0x6;
    toItems[0].window.name = cloneName;
    toItems[0].window.dynamicFlags[0] = 0x1;
    toItems[1].window.dynamicFlags[0] = 0x3; // unnamed: keeps its flags but focus
    std::array<itemDef_s *, 2> fromList{&fromItems[0], &fromItems[1]};
    std::array<itemDef_s *, 2> toList{&toItems[0], &toItems[1]};
    menuDef_t fromMenu{};
    menuDef_t toMenu{};
    fromMenu.window.dynamicFlags[0] = 0x20;
    fromMenu.itemCount = 2;
    fromMenu.items = fromList.data();
    toMenu.itemCount = 2;
    toMenu.items = toList.data();
    XAssetHeader from{};
    XAssetHeader to{};
    from.menu = &fromMenu;
    to.menu = &toMenu;
    DB_DynamicCloneMenu(from, to);
    CHECK(toMenu.window.dynamicFlags[0] == 0x20);
    CHECK(toItems[0].window.dynamicFlags[0] == 0x4);
    CHECK(toItems[1].window.dynamicFlags[0] == 0x1);
    CHECK(toMenu.items == toList.data() && toMenu.itemCount == 2);
    CHECK(fromItems[1].window.dynamicFlags[0] == 0x6 && fromItems[0].window.dynamicFlags[0] == 0x10);
}

std::array<std::recursive_mutex, CRITSECT_COUNT> g_criticalSections;
}  // namespace

// Engine boundary (weak).
#define WEAK __attribute__((weak))

WEAK void Com_Error(errorParm_t, const char *fmt, ...)
{
    std::fprintf(stderr, "Com_Error%s: %s\n",
                 Sys_IsWriteLocked(&db_hashCritSect) ? " with db_hashCritSect held (hangs in production)" : "", fmt);
    std::exit(1);
}
WEAK void Com_PrintError(int, const char *, ...)
{
    ++g_reports;
    g_reportedUnderHash = g_reportedUnderHash || Sys_IsWriteLocked(&db_hashCritSect);
}
WEAK void Com_Printf(int, const char *, ...) {}
WEAK void MyAssertHandler(const char *file, int line, int, const char *fmt, ...)
{
    // No physical-memory runtime here: freeing the zone's name only reports.
    if (g_poisonInWindow && std::strstr(file, "physicalmemory"))
        return;
    std::fprintf(stderr, "assert %s:%d %s\n", file, line, fmt);
    std::exit(1);
}
WEAK void Sys_Error(const char *fmt, ...)
{
    if (g_poisonInWindow)
    {
        CHECK(Sys_IsWriteLocked(&db_hashCritSect));
        std::exit(g_failures == 0 ? 0 : 1);
    }
    std::fprintf(stderr, "Sys_Error: %s\n", fmt);
    std::exit(1);
}
WEAK void Sys_EnterCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).lock(); }
WEAK void Sys_LeaveCriticalSection(int section) { g_criticalSections.at(static_cast<size_t>(section)).unlock(); }
WEAK void Sys_Sleep(uint32_t) {}
WEAK bool Sys_IsMainThread() { return true; }
WEAK bool Sys_IsDatabaseReady2() { return g_databaseReady2; }
WEAK void Sys_DatabaseCompleted2() {}
WEAK void Sys_SyncDatabase() {}
WEAK void Com_Memset(void *dest, const int val, const size_t count) { std::memset(dest, val, count); }
WEAK int I_stricmp(const char *s0, const char *s1) { return strcasecmp(s0, s1); }
WEAK void Z_Free(void *, int) {}
WEAK void PMem_Free(const char *, uint32_t) {}
// Reached only through asset tables; no zone or asset exists in these checks.
WEAK clipMap_t cm;
WEAK ComWorld comWorld;
WEAK GameWorldMp gameWorldMp;
WEAK XAsset *varXAsset;
WEAK void CM_Unload() {}
WEAK void Com_UnloadWorld() {}
WEAK void BG_FillInAllWeaponItems() {}
WEAK void Mark_XAsset()
{
    if (!g_overrideMark)
        return;
    Mark_ScriptStringCustom(&g_overrideMark);
    ++g_marks;
}
WEAK void DB_SaveDObjs() {}
WEAK void DB_LoadDObjs() {}
WEAK void DB_ReleaseGeometryBuffers(XZoneMemory *)
{
    if (g_poisonInWindow)
        db::registry_ownership::SetRegistryOwnershipCoordinatorBoundaryForTesting(0, 0, 0, 0, 2, 2);
}
WEAK const char *DB_GetXAssetHeaderName(int32_t, const XAssetHeader *) { return ""; }
WEAK const char *DB_GetXAssetName(const XAsset *) { return ""; }
WEAK void DB_SetXAssetName(XAsset *, const char *) {}
WEAK int32_t DB_GetXAssetTypeSize(int32_t) { return 0; }
WEAK const char *DB_GetXAssetTypeName(uint32_t) { return ""; }

int main(int argc, char **argv)
{
    SL_Init();
    // DB_Init's order: the zone runtime table exists before the first zone.
    CHECK(db::zone_runtime::TryInitializeZoneRuntimeTable(&db::zone_runtime::ProductionZoneRuntimeTable())
          == db::zone_runtime::ZoneRuntimeTableStatus::Success);
    if (argc > 1 && std::strcmp(argv[1], "poisoned-window") == 0)
    {
        TestPoisonedWindowIsFatal();
        return 1;
    }
    TestDynamicCloneMenu();
    TestPostLoadOverrideMarks();
    TestQuitFreesZoneNames();
    TestFailureInSessionKeepsNames();
    TestUnloadReportsAfterRelease(); // last: the poison is process-wide
    return g_failures == 0 ? 0 : 1;
}
